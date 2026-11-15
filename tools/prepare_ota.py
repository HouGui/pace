#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build signed development firmware and verify a local update package; never flash or publish."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from urllib.parse import urlsplit

from package_firmware import copy_notices, validate_profile

ROOT = Path(__file__).resolve().parents[1]
BOARD = 'waveshare-esp32s3-touch-lcd7-800x480'
TARGET = 'esp32s3'


def validate_inputs(key, version, image_url):
    if key.is_relative_to(ROOT):
        raise ValueError('Keep the signing key outside the repository and back it up privately')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,30}', version):
        raise ValueError('Version must contain 1–31 ASCII letters, digits, dots, +, _ or -')
    if image_url:
        url = urlsplit(image_url)
        if (url.scheme != 'https' or not url.hostname or url.username or url.password or url.fragment
                or len(image_url) > 512 or any(ord(c) <= 32 or ord(c) >= 127 for c in image_url)):
            raise ValueError('Image URL must be a plain HTTPS URL without credentials or fragments')


def signing_defaults(key):
    # JSON string quoting also produces the Kconfig string syntax for the path.
    return '\n'.join([
        'CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y',
        'CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y',
        'CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y',
        'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y',
        f'CONFIG_SECURE_BOOT_SIGNING_KEY={json.dumps(str(key))}',
        '# CONFIG_SECURE_BOOT is not set',
        '# CONFIG_SECURE_FLASH_ENC_ENABLED is not set',
        '# CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC is not set', ''])


def idf_environment():
    """Use the pinned SDK and board target without changing the caller's shell."""
    sdk_path = os.environ.get('IDF_PATH')
    if not sdk_path or not (Path(sdk_path) / 'tools/idf.py').is_file():
        raise ValueError('Open an ESP-IDF terminal or activate the SDK, then retry')
    sdk = Path(sdk_path).resolve()
    environment = os.environ.copy()
    environment['IDF_TARGET'] = TARGET
    expected = (ROOT / '.esp-idf-version').read_text().strip()
    version = subprocess.check_output(
        [sys.executable, str(sdk / 'tools/idf.py'), '--version'],
        env=environment, text=True).strip()
    if version != f'ESP-IDF {expected}':
        raise ValueError(f'PACE requires ESP-IDF {expected}; activate that SDK and retry')
    if os.environ.get('IDF_TARGET') not in (None, '', TARGET):
        print(f'PACE selects {TARGET} for this build; the terminal target is unchanged.', flush=True)
    return sdk, environment


def validate_build_directory(output):
    # Never reset a user's saved signing/security configuration to fix a target mismatch.
    for path, pattern in (
        (output / 'sdkconfig', r'^CONFIG_IDF_TARGET="([^"]+)"$'),
        (output / 'build/CMakeCache.txt', r'^IDF_TARGET:[^=]+=([^\r\n]+)$'),
    ):
        if path.is_file():
            match = re.search(pattern, path.read_text(), re.MULTILINE)
            if match and match[1] != TARGET:
                raise ValueError(
                    f'The output directory contains a {match[1]} build. '
                    'Use a fresh --output directory, for example build-ota-s3; '
                    'keep the same signing key')


def build_firmware(sdk, environment, output, version):
    subprocess.run([
        sys.executable, str(sdk / 'tools/idf.py'), '-C', str(ROOT / 'firmware'),
        '-B', str(output / 'build'), '-D', f'IDF_TARGET={TARGET}',
        '-D', f'SDKCONFIG={output / "sdkconfig"}',
        '-D', f'SDKCONFIG_DEFAULTS={ROOT / "firmware/sdkconfig.defaults"};{output / "signing.defaults"}',
        '-D', f'PROJECT_VER={version}', 'build',
    ], env=environment, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--key', type=Path, required=True)
    parser.add_argument('--create-key', action='store_true', help='Create a key only if the external path does not exist')
    parser.add_argument('--version', required=True, help='Use a different version for each update')
    parser.add_argument('--output', type=Path, default=ROOT / 'build-ota')
    parser.add_argument('--image-url', default='', help='Optional final HTTPS image URL for manifest generation')
    args = parser.parse_args()
    key, output = args.key.expanduser().resolve(), args.output.resolve()
    validate_inputs(key, args.version, args.image_url)
    if output == ROOT or (output.is_relative_to(ROOT) and not output.relative_to(ROOT).parts[0].startswith('build-')):
        raise ValueError('Choose a dedicated build directory')
    package = output / f'update-{args.version}'
    if package.exists():
        raise ValueError(
            f'Package already exists: {package}. Reuse it if preparation completed successfully, '
            'or choose a new --version for changed firmware')
    validate_build_directory(output)
    sdk, environment = idf_environment()
    if not key.exists():
        if not args.create_key:
            raise ValueError('Key not found; use --create-key once or supply your existing key')
        key.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        subprocess.run([sys.executable, '-m', 'espsecure', 'generate_signing_key', '--version', '2', str(key)], check=True)
        key.chmod(0o600)
    output.mkdir(parents=True, exist_ok=True)
    defaults = output / 'signing.defaults'
    defaults.write_text(signing_defaults(key))
    build = output / 'build'
    build_firmware(sdk, environment, output, args.version)
    config = json.loads((build / 'config/sdkconfig.json').read_text())
    validate_profile(config)
    if config.get('IDF_TARGET') != TARGET:
        raise ValueError('Build did not select the PACE ESP32-S3 target')
    if not config.get('SECURE_SIGNED_ON_UPDATE'):
        raise ValueError('Build did not enable signature verification')
    image = build / 'pace.bin'
    if image.stat().st_size > 3 * 1024 * 1024:
        raise ValueError('Image exceeds the application slot')
    subprocess.run([sys.executable, '-m', 'espsecure', 'verify_signature', '--version', '2',
                    '--keyfile', str(key), str(image)], check=True)
    package.mkdir(exist_ok=False)
    shutil.copy2(image, package / 'pace.bin')
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    (package / 'SHA256SUMS').write_text(f'{digest}  pace.bin\n')
    if args.image_url:
        manifest = dict(schema_version=1, board_profile=BOARD, version=args.version,
                        image_url=args.image_url, size=image.stat().st_size, sha256=digest)
        (package / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    copy_notices(package, sdk)
    print(f'Verified update: {package / "pace.bin"}')
    print('OTA: hold Settings for 2 seconds, join the displayed PACE Wi-Fi, open '
          'http://192.168.4.1, then Diagnostics and recovery > Upload and install.')
    print('If OTA is disabled, install this signed build once via USB TO UART.')
    command = ['idf.py', '-C', str(ROOT / 'firmware'), '-B', str(build),
               '-D', f'IDF_TARGET={TARGET}', '-p', 'PORT', 'flash', 'monitor']
    print('USB only: set the terminal IDF_TARGET to esp32s3 and replace PORT with your serial port:')
    print(subprocess.list2cmdline(command) if os.name == 'nt' else shlex.join(command))
    print('Keep USB power connected. Wait at least 30 seconds after boot before another update.')
    print('Nothing was flashed or published. Keep the external signing key private and backed up.')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(f'PACE: build/signing command failed (exit {error.returncode}). '
                 'Read the tool error above, correct it, and retry. Nothing was flashed.')
    except (ValueError, OSError) as error:
        sys.exit(f'PACE: {error}')

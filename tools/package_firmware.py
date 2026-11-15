#!/usr/bin/env python3
"""Create local install artifacts from ESP-IDF outputs; never publish or copy private files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def validate_profile(config):
    # These SDK options may provision irreversible hardware state at first boot.
    # The beginner installer is deliberately limited to development images.
    if any(config.get(name) for name in ('SECURE_BOOT', 'SECURE_FLASH_ENC_ENABLED',
                                        'NVS_SEC_KEY_PROTECT_USING_HMAC')):
        raise ValueError('Security provisioning builds need a separately reviewed install procedure')


def build_parts(build, arguments):
    if arguments['extra_esptool_args']['chip'] != 'esp32s3':
        raise ValueError('Only the reference ESP32-S3 build is supported')
    allowed = {arguments[name]['file'] for name in ('app', 'bootloader', 'partition-table', 'otadata')}
    files = arguments['flash_files']
    if set(files.values()) != allowed:
        raise ValueError('Unexpected flash inputs; refuse to package settings/keys/cache')
    parts = []
    end = 0
    for offset, filename in sorted(files.items(), key=lambda row: int(row[0], 0)):
        path = (build / filename).resolve()
        if not path.is_relative_to(build) or not path.is_file() or path.suffix != '.bin':
            raise ValueError('Invalid build artifact path')
        address = int(offset, 0)
        if address < end or address + path.stat().st_size > 8 * 1024 * 1024:
            raise ValueError('Overlapping or oversized flash input')
        end = address + path.stat().st_size
        parts.extend([hex(address), str(path)])
    return parts


def copy_notices(output, sdk):
    shutil.copy2(ROOT / 'LICENSE', output / 'LICENSE')
    shutil.copy2(ROOT / 'README.md', output / 'SOURCE-README.md')
    dependencies = {'esp-idf': sdk, 'lvgl': ROOT / 'firmware/managed_components/lvgl__lvgl'}
    for name, source in dependencies.items():
        for path in source.rglob('*'):
            if not path.is_file() or '.git' in path.parts:
                continue
            if path.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'NOTICE', 'COPYRIGHT', 'OFL.')):
                destination = output / 'licenses' / name / path.relative_to(source)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'firmware/build')
    parser.add_argument('--output', type=Path, default=ROOT / 'build-package')
    args = parser.parse_args()
    build = args.build.resolve()
    validate_profile(json.loads((build / 'config/sdkconfig.json').read_text()))
    data = json.loads((build / 'flasher_args.json').read_text())
    parts = build_parts(build, data)
    description = json.loads((build / 'project_description.json').read_text())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    # esptool patches boot headers using the actual build settings. No guessed offsets.
    settings = data['flash_settings']
    subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3', 'merge_bin',
                    '-o', str(output / 'factory.bin'), '--flash_mode', settings['flash_mode'],
                    '--flash_freq', settings['flash_freq'], '--flash_size', settings['flash_size'],
                    *parts], check=True)
    shutil.copy2(build / data['app']['file'], output / 'pace.bin')
    manifest = {'name': 'PACE development', 'version': description['project_version'],
                'new_install_prompt_erase': True, 'new_install_improv_wait_time': 0,
                'builds': [{'chipFamily': 'ESP32-S3', 'parts': [{'path': 'factory.bin', 'offset': 0}]}]}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Install PACE</title>
<h1>Install PACE development firmware</h1><p>For Waveshare ESP32-S3-Touch-LCD-7 only.
Connect USB TO UART. Factory installation resets Wi-Fi/settings; select erase to clear the calendar cache too. Use disposable credentials.</p>
<script type="module" src="https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module"></script>
<esp-web-install-button manifest="manifest.json"><button slot="activate">Connect and install</button></esp-web-install-button>
<p>After restart, join the PACE Wi-Fi network shown on screen and open http://192.168.4.1.</p>
<p>Installer: ESP Web Tools 10.4.0 (Apache-2.0). Device setup contains no third-party scripts.</p></html>
''')
    hashes = [f'{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}'
              for name in ('factory.bin', 'pace.bin', 'manifest.json', 'index.html')]
    (output / 'SHA256SUMS').write_text('\n'.join(hashes) + '\n')
    copy_notices(output, Path(os.environ['IDF_PATH']).resolve())
    print(f'Local artifacts: {output}. Not published; physical installation remains unverified.')


if __name__ == '__main__':
    main()

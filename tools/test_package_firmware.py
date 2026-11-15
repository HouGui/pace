"""Safety checks for the artifact allowlist; no SDK required."""
import tempfile
import unittest
from pathlib import Path
from package_firmware import build_parts, validate_profile


class PackageTests(unittest.TestCase):
    def test_signed_development_profile_is_allowed(self):
        validate_profile({'SECURE_SIGNED_APPS_NO_SECURE_BOOT': True})

    def test_hardware_security_profiles_are_rejected(self):
        for setting in ('SECURE_BOOT', 'SECURE_FLASH_ENC_ENABLED',
                        'NVS_SEC_KEY_PROTECT_USING_HMAC'):
            with self.subTest(setting=setting), self.assertRaises(ValueError):
                validate_profile({setting: True})

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.args = {'extra_esptool_args': {'chip': 'esp32s3'}, 'flash_files': {}}
        for i, name in enumerate(('bootloader', 'partition-table', 'otadata', 'app')):
            filename = name + '.bin'
            (self.root / filename).write_bytes(b'synthetic')
            self.args[name] = {'file': filename}
            self.args['flash_files'][hex(i * 4096)] = filename

    def test_build_offsets_are_used(self):
        parts = build_parts(self.root, self.args)
        self.assertEqual(parts[::2], ['0x0', '0x1000', '0x2000', '0x3000'])

    def test_private_or_extra_input_is_rejected(self):
        self.args['flash_files']['0x4000'] = 'private.bin'
        with self.assertRaises(ValueError):
            build_parts(self.root, self.args)

    def test_outside_build_is_rejected(self):
        self.args['app']['file'] = '../private.bin'
        self.args['flash_files']['0x3000'] = '../private.bin'
        with self.assertRaises(ValueError):
            build_parts(self.root, self.args)

    def test_wrong_chip_is_rejected(self):
        self.args['extra_esptool_args']['chip'] = 'esp32'
        with self.assertRaises(ValueError):
            build_parts(self.root, self.args)

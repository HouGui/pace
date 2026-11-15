# SPDX-License-Identifier: MIT
from pathlib import Path
import contextlib
import io
import os
import tempfile
import unittest
from unittest.mock import patch
from prepare_ota import (ROOT, TARGET, build_firmware, idf_environment, main,
                         signing_defaults, validate_build_directory, validate_inputs)


class PrepareOtaTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()
        self.sdk = self.directory / 'SDK with spaces'
        (self.sdk / 'tools').mkdir(parents=True)
        (self.sdk / 'tools/idf.py').touch()

    def test_key_must_stay_outside_repository(self):
        with self.assertRaises(ValueError):
            validate_inputs(ROOT / 'local/key.pem', '1.0', '')

    def test_version_and_url_bounds(self):
        for version in ('', 'x' * 32, '../bad', 'v1\nsecret'):
            with self.assertRaises(ValueError):
                validate_inputs(Path('/tmp/test.pem'), version, '')
        for url in ('http://example.invalid/a', 'https://user@example.invalid/a', 'https://example.invalid/a#part'):
            with self.assertRaises(ValueError):
                validate_inputs(Path('/tmp/test.pem'), '1.0', url)
        validate_inputs(Path('/tmp/test.pem'), '1.0-dev', 'https://example.invalid/pace.bin')

    def test_defaults_do_not_enable_hardware_provisioning(self):
        defaults = signing_defaults(Path('/tmp/private/key.pem'))
        self.assertIn('CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y', defaults)
        self.assertNotIn('\nCONFIG_SECURE_BOOT=y', defaults)
        self.assertNotIn('\nCONFIG_SECURE_FLASH_ENC_ENABLED=y', defaults)
        self.assertNotIn('\nCONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y', defaults)

    def test_target_is_selected_for_fresh_and_conflicting_shells(self):
        for target in (None, 'esp32', TARGET):
            environment = {'IDF_PATH': str(self.sdk), 'PACE_TEST_KEEP': 'unchanged'}
            if target is not None:
                environment['IDF_TARGET'] = target
            expected = (ROOT / '.esp-idf-version').read_text().strip()
            with self.subTest(target=target), patch.dict(os.environ, environment, clear=True), \
                    patch('prepare_ota.subprocess.check_output', return_value=f'ESP-IDF {expected}\n') as check, \
                    contextlib.redirect_stdout(io.StringIO()):
                sdk, child = idf_environment()
                self.assertEqual(sdk, self.sdk)
                self.assertEqual(child['IDF_TARGET'], TARGET)
                self.assertEqual(child['PACE_TEST_KEEP'], 'unchanged')
                self.assertEqual(os.environ.get('IDF_TARGET'), target)
                self.assertEqual(check.call_args.kwargs['env']['IDF_TARGET'], TARGET)

    def test_missing_sdk_has_actionable_error(self):
        for environment in ({}, {'IDF_PATH': str(self.directory / 'missing')}):
            with self.subTest(environment=environment), patch.dict(os.environ, environment, clear=True), \
                    self.assertRaisesRegex(ValueError, 'activate the SDK'):
                idf_environment()

    def test_wrong_sdk_version_is_rejected(self):
        with patch.dict(os.environ, {'IDF_PATH': str(self.sdk)}), \
                patch('prepare_ota.subprocess.check_output', return_value='ESP-IDF v0.0.0\n'), \
                self.assertRaisesRegex(ValueError, 'requires ESP-IDF'):
            idf_environment()

    def test_build_pins_target_and_preserves_paths_without_shell_commands(self):
        output = self.directory / 'output with spaces'
        environment = {'IDF_TARGET': TARGET}
        with patch('prepare_ota.subprocess.run') as run:
            build_firmware(self.sdk, environment, output, '0.4.0-test')
        command = run.call_args.args[0]
        self.assertIsInstance(command, list)
        self.assertIn(f'IDF_TARGET={TARGET}', command)
        self.assertIn(f'SDKCONFIG={output / "sdkconfig"}', command)
        self.assertIn(f'PROJECT_VER=0.4.0-test', command)
        self.assertIn(str(output / 'build'), command)
        self.assertEqual(command[-1], 'build')
        self.assertNotIn('set-target', command)
        self.assertNotIn('flash', command)
        self.assertEqual(run.call_args.kwargs, {'env': environment, 'check': True})

    def test_existing_wrong_target_is_preserved_and_rejected(self):
        for relative, content in (
            ('sdkconfig', 'CONFIG_IDF_TARGET="esp32"\n'),
            ('build/CMakeCache.txt', 'IDF_TARGET:STRING=esp32\n'),
        ):
            with self.subTest(file=relative), tempfile.TemporaryDirectory() as directory:
                output = Path(directory)
                path = output / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
                with self.assertRaisesRegex(ValueError, 'fresh --output directory'):
                    validate_build_directory(output)
                self.assertEqual(path.read_text(), content)

    def test_fresh_or_correct_target_directory_is_accepted(self):
        validate_build_directory(self.directory)
        (self.directory / 'sdkconfig').write_text('CONFIG_IDF_TARGET="esp32s3"\n')
        validate_build_directory(self.directory)

    def test_existing_package_is_rejected_before_building(self):
        (self.directory / 'update-test').mkdir()
        with patch('sys.argv', ['prepare_ota.py', '--key', str(self.directory / 'key.pem'),
                                '--version', 'test', '--output', str(self.directory)]), \
                patch('prepare_ota.idf_environment') as environment, \
                self.assertRaisesRegex(ValueError, 'Package already exists'):
            main()
        environment.assert_not_called()


if __name__ == '__main__':
    unittest.main()

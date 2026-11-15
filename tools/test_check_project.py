#!/usr/bin/env python3
"""Regression tests for the repository checker; no SDK or CAD packages needed."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ProjectCheckerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        # Copy only public source; never include local notes or installed tools.
        for name in ('docs', 'hardware', 'firmware', 'tools', '.github', 'adapters', 'tests'):
            shutil.copytree(ROOT / name, self.root / name,
                            ignore=shutil.ignore_patterns('build', 'build-*',
                                                         'managed_components', 'node_modules', '__pycache__', '*.local.json', '*.pem', '*.key'))
        for path in [*ROOT.glob('*.md'), ROOT / '.esp-idf-version', ROOT / 'LICENSE']:
            shutil.copy2(path, self.root / path.name)

    def check(self):
        return subprocess.run([sys.executable, str(self.root / 'tools/check_project.py')],
                              capture_output=True, text=True, timeout=15)

    def test_public_project_passes(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_private_and_generated_files_are_excluded(self):
        for directory in ('local', '.venv/lib/example', 'firmware/build'):
            target = self.root / directory
            target.mkdir(parents=True, exist_ok=True)
            (target / 'notes.md').write_text('[missing](absent.md)\n')
            (target / 'data.json').write_text('{ invalid json')
            (target / 'diagram.svg').write_text('<broken')
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_broken_public_link_is_rejected(self):
        for directory in ('', 'docs', 'docs/interfaces', 'hardware'):
            with self.subTest(directory=directory):
                note = self.root / directory / 'broken-link.md'
                note.write_text('[missing](absent.md)\n')
                result = self.check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('missing absent.md', result.stderr)
                note.unlink()

    def test_malformed_public_artifacts_are_rejected(self):
        for name, content in (('docs/broken.json', '{ invalid json'),
                              ('hardware/broken.svg', '<broken')):
            with self.subTest(name=name):
                path = self.root / name
                path.write_text(content)
                result = self.check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(name, result.stderr)
                path.unlink()

    def test_missing_heading_is_rejected(self):
        (self.root / 'docs/broken-anchor.md').write_text('[bad](development.md#missing-heading)\n')
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing anchor', result.stderr)


if __name__ == '__main__':
    unittest.main()

import ast
import contextlib
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from audit_metadata import audit, read_regular
from section_metadata import IMAGE_HASHES, MetadataError
from test_section_metadata import fixture


ROOT = Path(__file__).resolve().parents[1]


class AuditTests(unittest.TestCase):
    def test_real_audit_has_no_native_or_link_claim(self):
        result = audit(ROOT / 'recomp-input/rush2049.toml')
        self.assertTrue(result['metadata_integrity_passed'])
        self.assertFalse(result['guest_code_executed'])
        self.assertFalse(result['runtime_implementations_verified'])
        self.assertEqual(result['images_verified'], [])
        self.assertEqual(result['function_entries'], 2136)
        self.assertEqual(len(result['runtime_replacements_required']), 7)

    def test_cli_runs_outside_repository(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/audit_metadata.py')],
                                cwd=tempfile.gettempdir(), capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['function_entries'], 2136)

    def test_malformed_config_does_not_echo_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'config.toml'
            config.write_text('PRIVATE_PAYLOAD_SECRET = [broken')
            result = subprocess.run([sys.executable, str(ROOT / 'tools/audit_metadata.py'),
                                     '--config', str(config)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn('PRIVATE_PAYLOAD_SECRET', result.stdout + result.stderr)
            self.assertNotIn(str(config), result.stdout + result.stderr)

    def test_fifo_and_oversized_files_fail_without_blocking(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / 'fifo'
            os.mkfifo(fifo)
            result = subprocess.run([sys.executable, str(ROOT / 'tools/audit_metadata.py'),
                                     '--config', str(fifo)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            oversized = Path(directory) / 'large'
            oversized.write_bytes(b'abcd')
            with self.assertRaises(MetadataError):
                read_regular(oversized, 3)

    def test_explicit_image_check_reads_only_configured_file(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            config = base / 'config.toml'
            config.write_text('[input]\nentrypoint = 0x80000400\nsymbols_file_path = "symbols.toml"\nrom_file_path = "image.bin"\n')
            sections, image, hashes, _ = fixture()
            lines = []
            for s in sections:
                f = s['functions'][0]
                lines.extend(['[[section]]', 'name = ' + json.dumps(s['name']),
                              f'rom = {s["rom"]}', f'vram = {s["vram"]}', f'size = {s["size"]}',
                              'functions = [{name = ' + json.dumps(f['name']) +
                              f', vram = {f["vram"]}, size = {f["size"]}' + '}]'])
            (base / 'symbols.toml').write_text('\n'.join(lines))
            (base / 'image.bin').write_bytes(image)
            before = {p.name: p.read_bytes() for p in base.iterdir()}
            with patch.dict(IMAGE_HASHES, hashes, clear=True):
                result = audit(config, True)
            self.assertEqual(result['images_verified'], list(hashes))
            self.assertEqual(result['expanded_image_sha256'], hashlib.sha256(image).hexdigest())
            self.assertEqual(before, {p.name: p.read_bytes() for p in base.iterdir()})

    def checker_prefix(self, sections, rom_ok=True, prefix_ok=True, optimize=0):
        """Execute the real checker up to its first success message, without SDK/ROM."""
        script = Path(os.environ.get('CHECK_SECTIONS_SCRIPT', ROOT / 'tools/check_sections.py'))
        module = ast.parse(script.read_text())
        # The first top-level print is the image-validation success message.
        prefix = []
        for node in module.body:
            prefix.append(node)
            if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name) and node.value.func.id == 'print':
                break
        code = compile(ast.Module(body=prefix, type_ignores=[]), str(script), 'exec', optimize=optimize)
        _, image, _, _ = fixture()
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            inputs = home / 'N64Dev/Rush2049-PC/recomp-input'
            inputs.mkdir(parents=True)
            original = home / 'N64Dev/Rush2049/baserom.us.z64'
            original.parent.mkdir(parents=True)
            original.write_bytes(image[:4] if prefix_ok else b'xxxx')
            (inputs / 'rush2049-all-images.bin').write_bytes(image)
            (inputs / 'symbols.toml').write_text('placeholder')
            digests = {image[i * 4:i * 4 + 4]: IMAGE_HASHES[n] for i, n in enumerate(IMAGE_HASHES)}
            class Digest:
                def __init__(self, value): self.value = value
                def hexdigest(self): return self.value
            output = io.StringIO()
            with patch.object(Path, 'home', return_value=home), \
                 patch('tomllib.loads', return_value={'section': sections}), \
                 patch('hashlib.sha1', return_value=Digest('3f99351d7bb61656614bdb2aa1a90cfe55d1922c' if rom_ok else 'wrong')), \
                 patch('hashlib.sha256', side_effect=lambda data: Digest(digests.get(data, 'wrong'))), \
                 contextlib.redirect_stdout(output):
                exec(code, {'__file__': str(script)})
            return output.getvalue()

    def test_real_checker_rejects_subset_before_success_message(self):
        sections, _, _, _ = fixture()
        with self.assertRaises(MetadataError):
            self.checker_prefix(sections[:2])

    def test_real_checker_accepts_complete_fixture(self):
        sections, _, _, _ = fixture()
        self.assertIn('all four', self.checker_prefix(sections))

    def test_rom_identity_and_prefix_enforced_under_optimization(self):
        sections, _, _, _ = fixture()
        for rom_ok, prefix_ok in ((False, True), (True, False)):
            with self.subTest(rom_ok=rom_ok), self.assertRaises(MetadataError):
                self.checker_prefix(sections, rom_ok, prefix_ok, optimize=2)


if __name__ == '__main__':
    unittest.main()

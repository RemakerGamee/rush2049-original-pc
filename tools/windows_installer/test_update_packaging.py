"""Validate representative resource packages without game binaries or player data."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('package_builder', Path(__file__).with_name('build_installer.py'))
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)

class UpdatePackagingTests(unittest.TestCase):
    def test_four_identities_keep_helper_hashes_notices_and_interface(self):
        for native in (False, True):
            for remaster in (False, True):
                with self.subTest(native=native, remaster=remaster), tempfile.TemporaryDirectory() as folder:
                    root=Path(folder);payload=root/'payload';payload.mkdir()
                    for name in ('RushWindowsRT64.exe','PortUpdateAgent.exe','COPYING','ARTWORK-CREDITS.txt'):
                        (payload/name).write_bytes(('Synthetic fixture '+name).encode())
                    icon=root/'icon.ico';icon.write_bytes(b'\x00\x00\x01\x00fixture')
                    rows=builder.resources(root/'resources',payload,remaster,icon,native_menus=native)
                    table=(root/'resources/payload_table.hpp').read_text()
                    self.assertIn('bool lus='+('false' if native else 'true'),table)
                    self.assertIn('bool remaster_only='+('true' if remaster else 'false'),table)
                    self.assertEqual({row['path'] for row in rows},{p.name for p in payload.iterdir()})
                    for row in rows:self.assertEqual(row['sha256'],hashlib.sha256((payload/row['path']).read_bytes()).hexdigest())

    def test_wrong_runtime_and_missing_helper_refused_before_packaging(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);build=root/'build';(build/'Release').mkdir(parents=True)
            for name in ('RushWindowsRT64.exe','SDL2.dll','dxcompiler.dll','dxil.dll'):
                (build/'Release'/name).write_bytes(b'fixture')
            (build/'rush-build-runtime.txt').write_text('libultraship\n')
            with self.assertRaisesRegex(ValueError,'matching'):builder.validate_build(root,True)
            with self.assertRaisesRegex(ValueError,'PortUpdateAgent'):builder.validate_build(root,False)
            (build/'Release/PortUpdateAgent.exe').write_bytes(b'fixture')
            builder.validate_build(root,False)
            (build/'rush-build-runtime.txt').write_text('native\n')
            builder.validate_build(root,True)
            with self.assertRaisesRegex(ValueError,'matching'):builder.validate_build(root,False)

if __name__=='__main__':unittest.main()

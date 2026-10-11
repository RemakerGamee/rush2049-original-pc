"""Check fresh-tree preservation and pinned Git export with synthetic sources."""
import importlib.util,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('clean_prepare',ROOT/'tools/windows_native/prepare_clean.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def git(root,*args):
    return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
class PreparationTests(unittest.TestCase):
    def test_existing_destination_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'worker';target.mkdir();(target/'save').write_text('preserved')
            with self.assertRaisesRegex(ValueError,'destination must be new'):
                module.prepare(target,Path(folder),Path(folder),Path(folder)/'absent-rom')
            self.assertEqual((target/'save').read_text(),'preserved')
    def test_wrong_rom_refused_before_creation(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);rom=root/'synthetic';rom.write_bytes(b'not a ROM')
            with self.assertRaisesRegex(ValueError,'ROM identity differs'):
                module.prepare(root/'new',root,root,rom)
            self.assertFalse((root/'new').exists())
    def test_export_uses_committed_pin_not_dirty_or_untracked_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);donor=root/'donor';donor.mkdir();git(donor,'init','-q')
            (donor/'source.cpp').write_text('committed source\n');git(donor,'add','source.cpp')
            git(donor,'-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','fixture')
            pin=git(donor,'rev-parse','HEAD');(donor/'source.cpp').write_text('dirty worker source\n')
            (donor/'cached.exe').write_bytes(b'synthetic cache')
            module.export_tree(donor,root/'exported',pin)
            self.assertEqual((root/'exported/source.cpp').read_text(),'committed source\n')
            self.assertFalse((root/'exported/cached.exe').exists())
            self.assertEqual((donor/'source.cpp').read_text(),'dirty worker source\n')
if __name__=='__main__':unittest.main()

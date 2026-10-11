import hashlib, json, os, subprocess, sys, tempfile, unittest, uuid
from pathlib import Path
from unittest.mock import patch
import install, prepare, play

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.mutex_name='Local\\Rush2049-Test-'+uuid.uuid4().hex
        self.mutex_patch=patch.object(play.WindowsSession,'mutex_name',self.mutex_name)
        self.mutex_patch.start();self.addCleanup(self.mutex_patch.stop)
    def symlink(self,link,target,directory=False):
        try: link.symlink_to(target,target_is_directory=directory)
        except OSError as error:
            if os.name=='nt' and getattr(error,'winerror',None)==1314:
                self.skipTest('Windows symlink creation privilege unavailable; no system setting changed')
            raise
    @unittest.skipUnless(os.name=='nt','Windows junction check')
    def test_windows_junction_rejected(self):
        target=self.root/'target';target.mkdir();link=self.root/'junction'
        subprocess.run(['cmd','/c','mklink','/J',str(link),str(target)],check=True,stdout=subprocess.DEVNULL)
        with self.assertRaises(ValueError): install.migrate(link/'data')
        self.assertFalse((target/'data').exists())
    def test_dependency_export_keeps_committed_line_endings(self):
        donor=self.root/'donor';donor.mkdir()
        def git(*arguments):
            return subprocess.check_output(['git','-C',str(donor),*arguments],stderr=subprocess.DEVNULL)
        git('init');git('config','user.name','Fixture');git('config','user.email','fixture@example.invalid')
        (donor/'policy.cpp').write_bytes(b'first line\nsecond line\n')
        git('add','policy.cpp');git('commit','-m','fixture');pin=git('rev-parse','HEAD').decode().strip()
        git('config','core.autocrlf','true')
        prepare.export_tree(donor,self.root/'export',pin)
        self.assertEqual((self.root/'export/policy.cpp').read_bytes(),b'first line\nsecond line\n')
        self.assertEqual(git('config','core.autocrlf').decode().strip(),'true')
    @unittest.skipUnless(os.name=='nt','Real Windows lock API')
    def test_real_windows_file_lock_blocks_launch(self):
        data=self.root/'data';install.migrate(data)
        before=(data/'controller.mpk').read_bytes()
        with play.WindowsSession(data):
            with self.assertRaisesRegex(RuntimeError,'session.lock'):
                with play.WindowsSession(data): pass
        self.assertEqual((data/'controller.mpk').read_bytes(),before)
        with play.WindowsSession(data): pass
    @unittest.skipUnless(os.name=='nt','Real Windows process/mutex API')
    def test_cross_process_named_mutex_blocks(self):
        data=self.root/'data';install.migrate(data)
        command='from pathlib import Path; from play import WindowsSession; WindowsSession.mutex_name='+repr(self.mutex_name)+'; session=WindowsSession(Path('+repr(str(data))+')); session.__enter__()'
        with play.WindowsSession(data):
            completed=subprocess.run([sys.executable,'-c',command],cwd=Path(__file__).parent,capture_output=True,text=True)
        self.assertNotEqual(completed.returncode,0)
        self.assertIn('Another installed Rush session',completed.stderr)
    @unittest.skipUnless(os.name=='nt','Real Windows process API')
    def test_windows_process_enumeration_returns_names(self):
        result=play.other_rush()
        self.assertTrue(result is None or result.lower().startswith('rush'))
    def test_existing_data_never_replaced(self):
        data=self.root/'existing';data.mkdir();save=data/'controller.mpk';save.write_bytes(b'personal')
        with self.assertRaises(ValueError): install.migrate(data)
        self.assertEqual(save.read_bytes(),b'personal')
    def test_import_keeps_original_and_exact_bytes(self):
        pak=self.root/'original.mpk';pak.write_bytes(bytes(range(256))*128)
        cfg=self.root/'original.cfg';cfg.write_bytes(b'# existing user profile\n\nrush-input 1 # supported version\nkeyboard-port 2\ndeadzone 0.22\n')
        hashes=[hashlib.sha256(p.read_bytes()).hexdigest() for p in (pak,cfg)]
        install.migrate(self.root/'new data',pak,cfg)
        self.assertEqual((self.root/'new data/controller.mpk').read_bytes(),pak.read_bytes())
        self.assertEqual((self.root/'new data/input.cfg').read_bytes(),cfg.read_bytes())
        self.assertEqual(hashes,[hashlib.sha256(p.read_bytes()).hexdigest() for p in (pak,cfg)])
    def test_invalid_import_leaves_no_output(self):
        pak=self.root/'bad.mpk';pak.write_bytes(b'bad')
        with self.assertRaises(ValueError): install.migrate(self.root/'data',pak)
        self.assertFalse((self.root/'data').exists())
    def test_linked_data_rejected(self):
        target=self.root/'target';target.mkdir();link=self.root/'link';self.symlink(link,target,True)
        with self.assertRaises(ValueError): install.migrate(link/'data')
        self.assertFalse((target/'data').exists())
    def test_symlink_input_rejected(self):
        pak=self.root/'pak';pak.write_bytes(bytes(32768));link=self.root/'link';self.symlink(link,pak)
        with self.assertRaises(ValueError): install.migrate(self.root/'data',link)
    def test_copy_failure_leaves_no_partial_data(self):
        cfg=self.root/'input';cfg.write_bytes(b'rush-input 1\n')
        with patch.object(install.subprocess,'run',side_effect=RuntimeError('failed')):
            with self.assertRaises(RuntimeError): install.migrate(self.root/'data',config=cfg)
        self.assertFalse((self.root/'data').exists())
        self.assertFalse(list(self.root.glob('.rush-data-*')))
    def test_publish_refuses_even_empty_destination(self):
        stage=self.root/'stage';stage.mkdir();(stage/'file').write_text('keep')
        dest=self.root/'dest';dest.mkdir()
        with self.assertRaises(OSError): install.publish_directory(stage,dest)
        self.assertFalse((dest/'file').exists());self.assertTrue((stage/'file').exists())
    def test_new_data_has_valid_size_and_separate_profile(self):
        install.migrate(self.root/'data')
        self.assertEqual((self.root/'data/controller.mpk').stat().st_size,32768)
        self.assertTrue((self.root/'data/profile').is_dir())
    def test_wrong_rom_rejected_before_destination(self):
        rom=self.root/'rom';rom.write_bytes(bytes(0xC00000))
        with self.assertRaises(ValueError): prepare.setup(self.root/'setup',rom,self.root)
        self.assertFalse((self.root/'setup').exists())
    def test_byte_order_conversion_preserves_input(self):
        # Synthetic identity only checks normalization; real ROM identity is tested separately.
        data=bytes.fromhex('80371240')+bytes(0xC00000-4)
        digest=hashlib.sha1(data).hexdigest()
        for order in (2,4):
            swapped=b''.join(data[i:i+order][::-1] for i in range(0,len(data),order))
            rom=self.root/f'rom{order}';rom.write_bytes(swapped)
            with patch.object(prepare,'ROM_SHA1',digest): self.assertEqual(prepare.read_rom(rom),data)
            self.assertEqual(rom.read_bytes(),swapped)
    def test_install_allowlist_keeps_player_data_separate(self):
        native=self.root/'native';build=native/'build/Release';build.mkdir(parents=True)
        for name in install.FILES: (build/name).write_bytes(b'fixture')
        (build/'private.log').write_text('must not copy')
        notice=native/'source/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt'
        notice.parent.mkdir(parents=True);notice.write_text('fixture license')
        data=self.root/'player data';install.migrate(data)
        before={p.name:p.read_bytes() for p in data.iterdir() if p.is_file()}
        with patch.object(prepare,'read_rom',return_value=b'fixture ROM'), patch.object(prepare,'expanded_rom',return_value=b'fixture expanded images'):
            install.install(native,self.root/'version 1',data)
            install.install(native,self.root/'version 2',data)
        self.assertEqual((self.root/'version 1/recomp-input/rush2049-all-images.bin').read_bytes(),b'fixture expanded images')
        self.assertFalse((self.root/'version 1/private.log').exists())
        self.assertFalse((self.root/'version 1/controller.mpk').exists())
        self.assertEqual(play.validate(self.root/'version 1'),data)
        self.assertTrue((self.root/'version 1/notices/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt').exists())
        self.assertEqual(before,{p.name:p.read_bytes() for p in data.iterdir() if p.is_file()})
    def test_missing_dependency_does_not_create_install(self):
        data=self.root/'data';install.migrate(data)
        with self.assertRaises(OSError): install.install(self.root/'missing',self.root/'install',data)
        self.assertFalse((self.root/'install').exists())
    def test_existing_install_preserved(self):
        dest=self.root/'install';dest.mkdir();(dest/'keep').write_text('keep')
        with self.assertRaises(ValueError): install.install(self.root,dest,self.root/'data')
        self.assertEqual((dest/'keep').read_text(),'keep')
    def test_local_pack_paths_and_duplicate_mappings_refused(self):
        native=self.root/'native';build=native/'build/Release';build.mkdir(parents=True)
        for name in install.FILES: (build/name).write_bytes(b'fixture')
        notice=native/'source/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt'
        notice.parent.mkdir(parents=True);notice.write_text('fixture license')
        data=self.root/'data';install.migrate(data)
        before=(data/'controller.mpk').read_bytes()
        pack=self.root/'pack';pack.mkdir();(pack/'texture.png').write_bytes(b'fixture texture')
        base={'configuration':{'configurationVersion':3,'hashVersion':5},'textures':[]}
        for index,name in enumerate(('../outside.png','C:/outside.png','nested/../../outside.png')):
            with self.subTest(path=name):
                base['textures']=[{'path':name,'hashes':{'rt64':'0123456789abcdef'}}]
                (pack/'rt64.json').write_text(json.dumps(base))
                dest=self.root/('unsafe-'+str(index))
                with patch.object(prepare,'read_rom',return_value=b'fixture ROM'),patch.object(prepare,'expanded_rom',return_value=b'fixture images'):
                    with self.assertRaises(ValueError): install.install(native,dest,data,remaster_pack=pack)
                self.assertFalse(dest.exists())
        base['textures']=[{'path':'texture.png','hashes':{'rt64':'0123456789abcdef'}}]*2
        (pack/'rt64.json').write_text(json.dumps(base))
        with patch.object(prepare,'read_rom',return_value=b'fixture ROM'),patch.object(prepare,'expanded_rom',return_value=b'fixture images'):
            with self.assertRaisesRegex(ValueError,'Duplicate'): install.install(native,self.root/'duplicates',data,remaster_pack=pack)
        self.assertFalse((self.root/'duplicates').exists())
        self.assertEqual((data/'controller.mpk').read_bytes(),before)

if __name__=='__main__': unittest.main()

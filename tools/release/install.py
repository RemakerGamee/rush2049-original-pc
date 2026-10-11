#!/usr/bin/env python3
"""Install a local built game into a NEW directory; preserve separate player data."""
import argparse, ctypes, errno, hashlib, json, os, shutil, stat, subprocess, sys, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
FILES=('RushWindowsRT64.exe','SDL2.dll','dxcompiler.dll','dxil.dll')

def checked_file(path,limit,exact=None):
    path=Path(path)
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('Input must be a regular file')
    size=path.stat().st_size
    if size>limit or (exact is not None and size!=exact): raise ValueError('Input size differs')
    return path.read_bytes()

def safe_path(path):
    path=Path(path).absolute()
    for parent in (path,*path.parents):
        if parent.is_symlink() or (hasattr(parent,'is_junction') and parent.is_junction()):
            raise ValueError('Linked installation/data paths are unsupported')
    return path

def publish_directory(stage,destination):
    # Windows rename already refuses replacement. Linux requires RENAME_NOREPLACE.
    if os.name=='nt': os.rename(stage,destination); return
    libc=ctypes.CDLL(None,use_errno=True)
    rename=getattr(libc,'renameat2',None)
    if rename is None: raise RuntimeError('Atomic non-replacing directory rename unavailable')
    rename.argtypes=(ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint)
    if rename(-100,os.fsencode(stage),-100,os.fsencode(destination),1)!=0:
        error=ctypes.get_errno()
        raise OSError(error,os.strerror(error))

def migrate(destination,pak=None,config=None):
    destination=safe_path(destination)
    if destination.exists(): raise ValueError('Migration requires a new data directory; existing player data is preserved')
    # Validate every explicit import before creating output. No automatic save discovery.
    payload={}
    if pak: payload['controller.mpk']=checked_file(pak,32768,32768)
    if config:
        raw=checked_file(config,1024*1024)
        directives=[line.split('#',1)[0].strip() for line in raw.decode('utf-8').splitlines()]
        first=next((line for line in directives if line),'')
        if first.split()!=['rush-input','1']:
            raise ValueError('Unsupported input configuration version')
        payload['input.cfg']=raw
    destination.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.rush-data-',dir=destination.parent))
    try:
        for name,raw in payload.items():
            target=stage/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
        if 'controller.mpk' not in payload:
            subprocess.run([sys.executable,ROOT/'tools/create_controller_pak.py',stage/'controller.mpk'],check=True,stdout=subprocess.DEVNULL)
        if 'input.cfg' not in payload:
            shutil.copyfile(ROOT/'tools/release/default-input.cfg',stage/'input.cfg')
        (stage/'profile').mkdir()
        receipt={'schema':1,'imports':{name:hashlib.sha256(raw).hexdigest() for name,raw in payload.items()},
                 'source_files_modified':False,'pak_filesystem_integrity_verified':False}
        (stage/'migration-receipt.json').write_text(json.dumps(receipt,indent=2))
        # rename must never overwrite: destination is a nonempty directory on success.
        if destination.exists(): raise ValueError('Data directory appeared during migration')
        publish_directory(stage,destination)
    finally:
        if stage.exists(): shutil.rmtree(stage)

def install(native,destination,data,remaster_pack=None):
    native=Path(native); destination=safe_path(destination); data=safe_path(data)
    if destination.exists(): raise ValueError('Installation destination must be new')
    if data==destination or destination in data.parents or data in destination.parents:
        raise ValueError('Installation and player data must be separate directories')
    payload={name:checked_file(native/'build/Release'/name,128*1024*1024) for name in FILES}
    sys.path.insert(0,str(ROOT/'tools/release'))
    from prepare import read_rom, expanded_rom
    payload['game.z64']=read_rom(native/'source/test-rom.z64')
    payload['recomp-input/rush2049-all-images.bin']=expanded_rom(payload['game.z64'])
    for dependency in ('N64ModernRuntime','N64Recomp','rt64'):
        source=native/'source'/dependency
        for notice in source.rglob('*'):
            if notice.is_file() and notice.name.upper().startswith(('LICENSE','COPYING','NOTICE')):
                content=checked_file(notice,2*1024*1024)
                payload['notices/'+dependency+'/'+notice.relative_to(source).as_posix()]=content
    sdl_notice='notices/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt'
    if sdl_notice not in payload: raise ValueError('SDL dependency license notice missing')
    for script in ('play.py','play.ps1','start.cmd'):
        payload[script]=(ROOT/'tools/release'/script).read_bytes()
    if remaster_pack is not None:
        pack=safe_path(remaster_pack)
        config=checked_file(pack/'rt64.json',2*1024*1024)
        parsed=json.loads(config)
        if parsed.get('configuration',{}).get('configurationVersion')!=3 or parsed['configuration'].get('hashVersion')!=5:
            raise ValueError('Unsupported RT64 remaster configuration')
        entries=parsed.get('textures',[])
        if not entries or len(entries)>16384: raise ValueError('Invalid remaster texture count')
        hashes=set();names=set()
        payload['remaster-pack/rt64.json']=config
        for entry in entries:
            name=entry['path']; relative=Path(name); digest=entry['hashes']['rt64']
            if relative.is_absolute() or relative.drive or '..' in relative.parts or ':' in name or not name.lower().endswith('.png'):
                raise ValueError('Unsafe remaster texture path')
            if not isinstance(digest,str) or len(digest)!=16 or any(c not in '0123456789abcdef' for c in digest):
                raise ValueError('Invalid RT64 texture hash')
            if digest in hashes or name in names: raise ValueError('Duplicate remaster mapping')
            hashes.add(digest);names.add(name)
            target=safe_path(pack/relative)
            if not target.is_relative_to(pack): raise ValueError('Texture escapes remaster pack')
            payload['remaster-pack/'+relative.as_posix()]=checked_file(target,16*1024*1024)
    payload['player-data.json']=(json.dumps({'data_directory':str(data),'schema':1},indent=2)+'\n').encode()
    # Require existing user data; initialization/import is an explicit separate step.
    checked_file(data/'controller.mpk',32768,32768)
    checked_file(data/'input.cfg',1024*1024)
    destination.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.rush-install-',dir=destination.parent))
    try:
        for name,raw in payload.items():
            target=stage/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
        manifest={'schema':1,'files':{name:hashlib.sha256(raw).hexdigest() for name,raw in payload.items()},
                  'private_local_installation':True,'redistributable':False}
        (stage/'installation.json').write_text(json.dumps(manifest,indent=2))
        if destination.exists(): raise ValueError('Installation destination appeared during setup')
        publish_directory(stage,destination)
    finally:
        if stage.exists(): shutil.rmtree(stage)
    print('Installed local build; player saves/settings retained in a separate directory.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    init=commands.add_parser('data');init.add_argument('destination',type=Path)
    init.add_argument('--import-pak',type=Path);init.add_argument('--import-input-config',type=Path)
    copy=commands.add_parser('install');copy.add_argument('native',type=Path);copy.add_argument('destination',type=Path)
    copy.add_argument('--data-dir',required=True,type=Path)
    args=parser.parse_args()
    if args.command=='data': migrate(args.destination,args.import_pak,args.import_input_config)
    else: install(args.native,args.destination,args.data_dir)

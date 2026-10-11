"""Build standalone Windows setup/launcher from a verified native game build.

Run only on the maintainer's build PC. Downloaders need no Python/build tools.
No ROM, generated game source or expanded game-data image is packaged.
The optional remastered edition bundles the reviewed restoration texture pack.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, zipfile

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def command(args,log):
    with log.open('wb') as stream:
        result=subprocess.run([str(x) for x in args],stdout=stream,stderr=subprocess.STDOUT)
    if result.returncode: raise RuntimeError('Build failed; inspect '+str(log))

def resources(directory,payload,remastered=False):
    directory.mkdir(parents=True)
    entries=[];rc=['#include <windows.h>\n']
    for index,path in enumerate(sorted(payload.rglob('*')),100):
        if not path.is_file():continue
        name=path.relative_to(payload).as_posix()
        assert '..' not in Path(name).parts and path.suffix.lower() not in ('.z64','.v64','.n64','.bin','.mpk','.wav','.pcm')
        assert path.suffix.lower()!='.png' or (remastered and name.startswith('remaster-pack/'))
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        entries.append({'id':index,'path':name,'sha256':digest,'size':path.stat().st_size})
        rc.append(f'{index} RCDATA "{path.as_posix()}"\n')
    header='#pragma once\nnamespace payload { struct Entry { int id; const wchar_t* path; const char* sha256; }; inline constexpr Entry files[]={\n'
    header+=''.join('{'+str(row['id'])+',L'+json.dumps(row['path'])+','+json.dumps(row['sha256'])+'},\n' for row in entries)
    header+='}; inline constexpr bool remastered='+('true' if remastered else 'false')+'; inline constexpr wchar_t edition[]=L"Rush 2049 '+('Remastered 4x' if remastered else 'Original PC')+'"; }\n'
    (directory/'payload_table.hpp').write_text(header)
    (directory/'setup.rc').write_text(''.join(rc))
    return entries

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--native-root',required=True,type=Path);parser.add_argument('--output',required=True,type=Path);parser.add_argument('--source-archive',required=True,type=Path);parser.add_argument('--remaster-pack',type=Path);args=parser.parse_args()
    native=args.native_root.resolve();output=args.output.resolve()
    with zipfile.ZipFile(args.source_archive) as source_zip:
        if source_zip.testzip() is not None or 'tools/windows_native/lus-component.cmake' not in source_zip.namelist():
            raise ValueError('Complete reviewed integration source archive required')
    if output.exists():raise ValueError('Choose a new output directory')
    output.mkdir(parents=True);payload=output/'payload';payload.mkdir()
    for name in ('RushWindowsRT64.exe','SDL2.dll','dxcompiler.dll','dxil.dll'):
        shutil.copyfile(native/'build/Release'/name,payload/name)
    vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs=Path(subprocess.check_output([vswhere,'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
    redists=[p for p in (vs/'VC/Redist/MSVC').iterdir() if p.is_dir() and p.name[0].isdigit()]
    crt=max(redists,key=lambda p:tuple(int(x) for x in p.name.split('.')))/'x64'
    crt=next(p for p in crt.iterdir() if p.name.endswith('.CRT'))
    for path in crt.glob('*.dll'):shutil.copyfile(path,payload/path.name)
    for dependency in ('N64ModernRuntime','N64Recomp','rt64'):
        source=native/'source'/dependency
        for notice in source.rglob('*'):
            if notice.is_file() and notice.stat().st_size and notice.name.upper().startswith(('LICENSE','COPYING','NOTICE')):
                destination=payload/'notices'/dependency/notice.relative_to(source);destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(notice,destination)
    (payload/'notices/Microsoft-VC-Runtime.txt').write_text('Microsoft Visual C++ runtime '+crt.parent.parent.name+'\nApp-local redistributable DLLs from the licensed Visual Studio installation.\nhttps://aka.ms/vs/18/redistribution\nhttps://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files\n')
    for notice in (native/'build/notices').glob('*.txt'):
        target=payload/'notices'/notice.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(notice,target)
    (payload/'defaults').mkdir();shutil.copyfile(ROOT/'tools/release/default-input.cfg',payload/'defaults/input.cfg')
    if args.remaster_pack:
        pack=args.remaster_pack.resolve();config=json.loads((pack/'rt64.json').read_text(encoding='utf-8'))
        assert config['configuration']['hashVersion']==5 and len(config['textures'])==5957,'Use the reviewed v5 pack'
        destination=payload/'remaster-pack';destination.mkdir();shutil.copyfile(pack/'rt64.json',destination/'rt64.json')
        hashes=set();paths=set()
        for texture in config['textures']:
            relative=Path(texture['path']);digest=texture['hashes']['rt64']
            assert not relative.is_absolute() and '..' not in relative.parts and relative.suffix.lower()=='.png'
            assert len(digest)==16 and all(c in '0123456789abcdef' for c in digest) and digest not in hashes and str(relative) not in paths
            source=(pack/relative).resolve();assert source.is_relative_to(pack) and not (pack/relative).is_symlink()
            assert source.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
            target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target);hashes.add(digest);paths.add(str(relative))
    shutil.copyfile(args.source_archive,payload/'Integration-Source-v0.3.0.zip')
    shutil.copyfile(ROOT/'COPYING',payload/'COPYING')
    shutil.copyfile(ROOT/'README.md',payload/'README.md')
    table=resources(output/'launcher-resources',payload,bool(args.remaster_pack))
    command(['cmake','-S',HERE,'-B',output/'launcher-build','-G','Visual Studio 18 2026','-A','x64','-DRUSH_RESOURCE_DIR='+str(output/'launcher-resources')],output/'launcher-configure.log')
    command(['cmake','--build',output/'launcher-build','--config','Release','--parallel','2'],output/'launcher-build.log')
    shutil.copyfile(output/'launcher-build/Release/Rush2049Original.exe',payload/'Rush2049Original.exe')
    table=resources(output/'setup-resources',payload,bool(args.remaster_pack))
    command(['cmake','-S',HERE,'-B',output/'setup-build','-G','Visual Studio 18 2026','-A','x64','-DRUSH_BUILD_SETUP=ON','-DRUSH_RESOURCE_DIR='+str(output/'setup-resources')],output/'setup-configure.log')
    command(['cmake','--build',output/'setup-build','--config','Release','--parallel','2'],output/'setup-build.log')
    installer=output/('Rush2049-Remastered-PC-v0.3.0-Setup.exe' if args.remaster_pack else 'Rush2049-Original-PC-v0.3.0-Setup.exe');shutil.copyfile(output/'setup-build/Release/Rush2049OriginalSetup.exe',installer)
    manifest={'version':'v0.3.0','edition':'remastered' if args.remaster_pack else 'original','rom_included':False,'game_data_included':False,'texture_pack_included':bool(args.remaster_pack),'texture_count':5957 if args.remaster_pack else 0,'external_rom_required_every_launch':True,'files':table,'installer_sha256':hashlib.sha256(installer.read_bytes()).hexdigest()}
    (output/'package-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (output/'SHA256SUMS.txt').write_text(manifest['installer_sha256']+'  '+installer.name+'\n')
    print(json.dumps({'installer':str(installer),'size':installer.stat().st_size,'sha256':manifest['installer_sha256'],'payload_files':len(table)}))

if __name__=='__main__':main()

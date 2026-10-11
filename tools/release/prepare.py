#!/usr/bin/env python3
"""Create a private Windows build from the user's US ROM, never pre-generated inputs."""
import argparse, hashlib, json, os, shutil, stat, subprocess, sys, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/windows_native'))
from prepare_clean import export_tree, prepare
ROM_SHA1 = '3f99351d7bb61656614bdb2aa1a90cfe55d1922c'
IMAGES = ((0xB0CB10,647072,'bf7da3fa6283428a97372250cd4076d15e9eae10f9d5709c0387fe0742d43a1d'),
          (0xB5C534,194128,'0d6702c320df84cc6cbc3c5967dde08d44b6e476e110667fe2a43dc2dd536667'),
          (0xB6FEC4,43888,'b55fc2d1b22eb1ebdf01286a69a181b496da7b45ff7aec888ff74b7db748e7cd'))

def read_rom(path):
    path = Path(path)
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode) or path.stat().st_size != 0xC00000:
        raise ValueError('Expected a regular 12 MiB US ROM; original file is never modified')
    data = path.read_bytes()
    if data[:4] == bytes.fromhex('37804012'):
        data = bytes(v for i in range(0,len(data),2) for v in (data[i+1],data[i]))
    elif data[:4] == bytes.fromhex('40123780'):
        data = b''.join(data[i:i+4][::-1] for i in range(0,len(data),4))
    if hashlib.sha1(data).hexdigest() != ROM_SHA1:
        raise ValueError('Unsupported ROM identity; requires the original US release')
    return data

def expanded_rom(data):
    result = bytearray(data)
    for offset,size,digest in IMAGES:
        inflater = zlib.decompressobj(-15)
        image = inflater.decompress(data[offset:],size+1)
        if not inflater.eof or len(image)!=size or hashlib.sha256(image).hexdigest()!=digest:
            raise ValueError('Executable image identity differs')
        result.extend(image)
    return bytes(result)

def run(command, log, cwd=None):
    with log.open('wb') as stream:
        completed = subprocess.run([str(x) for x in command],cwd=cwd,stdout=stream,stderr=subprocess.STDOUT)
    if completed.returncode:
        raise RuntimeError(f'Command failed ({completed.returncode}); inspect local log {log.name}')

def setup(destination,rom,donor,jobs=2):
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError('Destination must be new; an existing installation is never overwritten')
    data=read_rom(rom); expanded=expanded_rom(data)
    for command in ('git','cmake'):
        if shutil.which(command) is None: raise ValueError(f'Missing prerequisite: {command}')
    destination.mkdir(parents=True)
    private=destination/'private-inputs'; private.mkdir()
    logs=destination/'logs';logs.mkdir()
    normalized=private/'user-rom.z64';normalized.write_bytes(data)
    metadata=private/'recomp-input';metadata.mkdir()
    for name in ('symbols.toml','rush2049.toml'):
        shutil.copy2(ROOT/'recomp-input'/name,metadata/name)
    (metadata/'rush2049-all-images.bin').write_bytes(expanded)
    pins=json.loads((ROOT/'tools/windows_native/dependencies.json').read_text())
    toolsource=private/'N64Recomp'
    export_tree(Path(donor)/'N64Recomp',toolsource,pins['N64Recomp']['pin'])
    run(['git','apply',ROOT/'tools/windows_native/N64Recomp-native.patch'],logs/'recompiler-patch.log',toolsource)
    toolbuild=destination/'generator-build'
    run(['cmake','-S',toolsource,'-B',toolbuild,'-DCMAKE_BUILD_TYPE=Release'],logs/'generator-configure.log')
    run(['cmake','--build',toolbuild,'--config','Release','--target','N64RecompCLI','RSPRecomp','--parallel',str(jobs)],logs/'generator-build.log')
    binroot=toolbuild/'Release' if os.name=='nt' else toolbuild
    suffix='.exe' if os.name=='nt' else ''
    run([binroot/('N64Recomp'+suffix),metadata/'rush2049.toml'],logs/'game-generation.log',private)
    (private/'RecompiledRsp').mkdir()
    rsp=metadata/'audio.toml'
    rsp.write_text('text_offset = 0x2AC50\ntext_size = 0xF34\ntext_address = 0x1080\nrom_file_path = "../user-rom.z64"\noutput_file_path = "../RecompiledRsp/rush_audio.cpp"\noutput_function_name = "rush_audio_rsp"\n')
    run([binroot/('RSPRecomp'+suffix),rsp],logs/'rsp-generation.log',private)
    run([sys.executable,ROOT/'tools/audit_metadata.py','--config',metadata/'rush2049.toml','--check-image'],logs/'metadata-audit.json')
    run([sys.executable,ROOT/'tools/audit_generated_calls.py','--root',private],logs/'generated-audit.json')
    prepare(destination/'native',donor,private,normalized)
    receipt={'rom_sha1':ROM_SHA1,'recompiler_pin':pins['N64Recomp']['pin'],
             'generated_from_user_rom':True,'prebuilt_inputs_used':False,
             'generated_audit':json.loads((logs/'generated-audit.json').read_text())['expected_bodies'],
             'native_build_completed':False}
    (destination/'setup-receipt.json').write_text(json.dumps(receipt,indent=2))
    print('ROM-only source preparation passed. Build the private native/source tree next.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination',type=Path,nargs='?')
    parser.add_argument('--validate-rom',type=Path)
    parser.add_argument('--rom',type=Path)
    parser.add_argument('--dependency-donor',type=Path)
    parser.add_argument('--jobs',type=int,choices=range(1,17),default=2)
    args=parser.parse_args()
    if args.validate_rom:
        expanded_rom(read_rom(args.validate_rom)); print('Supported ROM and executable images verified')
    else:
        if not all((args.destination,args.rom,args.dependency_donor)): parser.error('destination, --rom and --dependency-donor are required for setup')
        setup(args.destination,args.rom,args.dependency_donor,args.jobs)

"""Prepare a new private Windows source tree from published authored sources.

Private generated inputs are provided locally; no prebuilt game/archive is copied.
Dependency trees come from pinned Git objects, never the donor's dirty files.
"""
import argparse,hashlib,io,json,re,shutil,subprocess,tarfile
from pathlib import Path

PACKAGE=Path(__file__).resolve().parent
REPO=PACKAGE.parents[1]

def export_tree(donor,target,pin):
    raw=subprocess.check_output(['git','-c','core.autocrlf=false','-c','core.eol=lf','-C',str(donor),'archive',pin])
    target.mkdir(parents=True,exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        archive.extractall(target,filter='data')
    rows=subprocess.check_output(['git','-C',str(donor),'ls-tree','-r',pin],text=True)
    for row in rows.splitlines():
        meta,name=row.split('\t',1)
        mode,kind,digest=meta.split()
        if mode=='160000': export_tree(donor/name,target/name,digest)

def prepare(destination,donor,inputs,rom):
    destination=Path(destination).resolve()
    donor,inputs,rom=map(Path,(donor,inputs,rom))
    if destination.exists(): raise ValueError('destination must be new; existing workers/builds are never overwritten')
    if hashlib.sha1(rom.read_bytes()).hexdigest()!='3f99351d7bb61656614bdb2aa1a90cfe55d1922c':
        raise ValueError('original US ROM identity differs')
    with (inputs/'recomp-input/rush2049-all-images.bin').open('rb') as stream:
        if hashlib.sha1(stream.read(0xC00000)).hexdigest()!='3f99351d7bb61656614bdb2aa1a90cfe55d1922c':
            raise ValueError('expanded original ROM prefix differs')
        for size,digest in ((647072,'bf7da3fa6283428a97372250cd4076d15e9eae10f9d5709c0387fe0742d43a1d'),(194128,'0d6702c320df84cc6cbc3c5967dde08d44b6e476e110667fe2a43dc2dd536667'),(43888,'b55fc2d1b22eb1ebdf01286a69a181b496da7b45ff7aec888ff74b7db748e7cd')):
            if hashlib.sha256(stream.read(size)).hexdigest()!=digest: raise ValueError('expanded executable image differs')
    destination.mkdir(parents=True)
    source=destination/'source';source.mkdir()
    pins=json.loads((PACKAGE/'dependencies.json').read_text(encoding='utf-8'))
    for name in ('N64ModernRuntime','N64Recomp','rt64'):
        export_tree(donor/name,source/name,pins[name]['pin'])
        subprocess.run(['git','apply','--check',str(PACKAGE/(name+'-native.patch'))],cwd=source/name,check=True)
        subprocess.run(['git','apply',str(PACKAGE/(name+'-native.patch'))],cwd=source/name,check=True)
    for name,path in [('plume','rt64/src/contrib/plume'),('dxc','rt64/src/contrib/dxc')]:
        subprocess.run(['git','apply','--check',str(PACKAGE/(name+'-native.patch'))],cwd=source/path,check=True)
        subprocess.run(['git','apply',str(PACKAGE/(name+'-native.patch'))],cwd=source/path,check=True)
    for relative,asset in pins['N64ModernRuntime'].get('added_headers',{}).items():
        shutil.copy2(PACKAGE/asset,source/'N64ModernRuntime'/relative)
    subprocess.run(['git','apply',str(PACKAGE/'completion-dependency.patch')],cwd=source/'N64ModernRuntime',check=True)
    subprocess.run(['git','apply',str(PACKAGE/'external-rom.patch')],cwd=source/'N64ModernRuntime',check=True)
    # Reviewed remaster changes are recipes applied to pinned clean sources.
    for dependency, patch in (('N64ModernRuntime', 'remaster-shutdown.patch'),
                              ('rt64', 'remaster-circuit.patch')):
        command = ['git', 'apply', '-p2', str(PACKAGE/patch)]
        subprocess.run(command[:2] + ['--check'] + command[2:], cwd=source/dependency, check=True)
        subprocess.run(command, cwd=source/dependency, check=True)
    shutil.copy2(PACKAGE/'rush_circuit_flow.h', source/'rt64/src/render/rush_circuit_flow.h')
    shutil.copy2(PACKAGE/'rush_display_state.h', source/'rt64/src/render/rush_display_state.h')
    subprocess.run(['git','apply',str(PACKAGE/'display-vsync.patch')],cwd=source/'rt64',check=True)
    for folder in ('compat','host','tools'):
        shutil.copytree(REPO/folder,source/folder,ignore=shutil.ignore_patterns('__pycache__','windows_native'))
    for name,path in [('runtime_boot_check.cpp','tools'),('renderer_bridge.cpp','host'),
                      ('controller_pak.cpp','compat'),('controller_pak_image.hpp','compat'),
                      ('pi_pio.cpp','compat'),('si.cpp','compat'),
                      ('runtime_poll.cpp','compat'),('controller_settings_check.cpp','tools')]:
        shutil.copy2(PACKAGE/name,source/path/name)
    for name in ('lus-imgui-options.hpp', 'lus-imgui-store.hpp', 'lus-imgui-renderer.hpp', 'lus-imgui-check.cpp', 'lus-component.cmake', 'lus-controller-model.hpp', 'lus-controller-ui.hpp', 'controller-presets.hpp', 'controller-accessories.hpp', 'controller-metadata.hpp', 'controller-submission.hpp', 'controller-submission-check.cpp', 'controller_input.hpp', 'controller_manager.hpp', 'input_profile.hpp', 'input_remap.hpp', 'controller_settings_check.cpp', 'runtime_boot_check.cpp', 'cheat_visibility.hpp', 'cheat_native_state.hpp', 'cheat_visibility_window.hpp', 'cheat_options_check.cpp', 'cheat_codes.hpp'):
        shutil.copy2(PACKAGE/name,source/'tools'/name)
    shutil.copy2(PACKAGE/'CMakeLists.txt',source/'CMakeLists.txt')
    # These are private generated source inputs, not compiled objects or libraries.
    for folder in ('RecompiledFuncs-probe','RecompiledRsp'):
        shutil.copytree(inputs/folder,source/folder,ignore=shutil.ignore_patterns('*.o','*.obj','*.a','*.lib','build','CMakeFiles'))
    shutil.copy2(PACKAGE/'jet_sound_gain.h', source/'tools/jet_sound_gain.h')
    shutil.copy2(PACKAGE/'rom_identity.hpp', source/'tools/rom_identity.hpp')
    for name in ('pc_menu.hpp','cheat_visibility.hpp','cheat_native_state.hpp','cheat_settings_check.cpp'):
        shutil.copy2(PACKAGE/name,source/'tools'/name)
    from remaster_guest import apply_remaster
    apply_remaster(source/'RecompiledFuncs-probe')
    (source/'recomp-input').mkdir()
    shutil.copy2(inputs/'recomp-input/rush2049-all-images.bin',source/'recomp-input/rush2049-all-images.bin')
    shutil.copy2(rom,source/'test-rom.z64')
    # Rebuild explicit declarations from generated calls, never reuse a private header.
    wrappers=set(re.findall(r'\brush_real_(\w+)\s*\(', (PACKAGE/'runtime_boot_check.cpp').read_text(encoding='utf-8')))
    counts={name:0 for name in wrappers}
    files=[p for folder in ('N64ModernRuntime','compat','RecompiledFuncs-probe') for p in (source/folder).rglob('*') if p.suffix in ('.c','.cpp')]
    for p in files:
        text=p.read_text(encoding='utf-8')
        original=text
        for name in wrappers:
            pattern=r'\bvoid\s+(?:rush_real_)?'+re.escape(name)+r'(\s*\([^;{}]*\)\s*\{)'
            text,n=re.subn(pattern,'void rush_real_'+name+r'\1',text)
            counts[name]+=n
        if text!=original: p.write_text(text,encoding='utf-8')
    if any(n!=1 for n in counts.values()): raise ValueError('wrapper definition identities differ: '+repr(counts))
    calls=set()
    for p in (source/'RecompiledFuncs-probe').glob('funcs_*.c'):
        calls.update(re.findall(r'\b([A-Za-z_]\w*)\s*\(\s*rdram\s*,\s*ctx\s*\)',p.read_text(encoding='utf-8')))
    (source/'compile-check').mkdir()
    (source/'compile-check/external_declarations.h').write_text('#pragma once\n#include "recomp.h"\n'+''.join(f'void {name}(uint8_t* rdram, recomp_context* ctx);\n' for name in sorted(calls)))
    for name in ('run.ps1','input_settings.ps1'):
        shutil.copy2(PACKAGE/name,destination/name)
    manifest={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file()}
    (destination/'private-source-identities.json').write_text(json.dumps(manifest,indent=2))
    (destination/'wrapper-definition-counts.json').write_text(json.dumps(counts,indent=2))
    print('Prepared fresh source tree from pinned Git objects and local generated inputs:',destination)
    print('Wrapper definitions verified:',len(counts),'No donor build outputs copied.')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('destination',type=Path)
    ap.add_argument('--dependency-donor',required=True,type=Path)
    ap.add_argument('--private-inputs',required=True,type=Path)
    ap.add_argument('--rom',required=True,type=Path)
    a=ap.parse_args();prepare(a.destination,a.dependency_donor,a.private_inputs,a.rom)

"""Validate expanded images and real runtime dispatch before executing guest code."""
from pathlib import Path
import hashlib
import json
import subprocess
import struct
import sys
import tomllib
from section_metadata import MetadataError, verify_images

root = Path.home() / 'N64Dev/Rush2049-PC'
source = Path(__file__).resolve().parent
image = (root / 'recomp-input/rush2049-all-images.bin').read_bytes()
rom = (Path.home() / 'N64Dev/Rush2049/baserom.us.z64').read_bytes()
if hashlib.sha1(rom).hexdigest() != '3f99351d7bb61656614bdb2aa1a90cfe55d1922c':
    raise MetadataError('Original ROM identity differs from verified US revision.')
if image[:len(rom)] != rom:
    raise MetadataError('Expanded input differs from original ROM prefix.')
sections = tomllib.loads((root / 'recomp-input/symbols.toml').read_text())['section']
verified_images = verify_images(image, sections)
print('Verified US ROM and all four unmodified executable images.', flush=True)

# The dispatch table exposed one additional mislabeled hardware helper that
# had no direct generated callers: it checks SI_STATUS at A4800018, then reads
# an uncached absolute address. This is SI raw read, not public PI ROM read.
symbols_path = root / 'recomp-input/symbols.toml'
symbols_text = symbols_path.read_text()
boot = next(s for s in sections if s['name'] == 'boot')
raw_read = next(f for f in boot['functions'] if f['vram'] == 0x8000D7A0)
if struct.unpack_from('>I', image, 0x8000D7AC - 0x7FFFF400)[0] != 0x0C003F5C:
    raise MetadataError('SI raw-read call instruction differs from native evidence.')
if struct.unpack_from('>II', image, 0x8000FD70 - 0x7FFFF400) != (0x3C0EA480, 0x8DC20018):
    raise MetadataError('SI status helper differs from native evidence.')
if raw_read['name'] != '__osSiRawReadIo':
    if raw_read['name'] != 'osPiReadIo' or 'name = "__osSiRawReadIo"' in symbols_text:
        raise MetadataError('SI raw-read identity cannot be corrected unambiguously.')
    backup = symbols_path.with_suffix('.toml.before-section-check')
    if not backup.exists():
        backup.write_bytes(symbols_path.read_bytes())
    symbols_text = symbols_text.replace('name = "osPiReadIo"', 'name = "__osSiRawReadIo"')
    tomllib.loads(symbols_text)
    symbols_path.write_text(symbols_text)
    print('Corrected verified SI hardware-helper identity in PC metadata.', flush=True)
    subprocess.run([sys.executable, str(source / 'fix_compile_check.py')], check=True)

out = root / 'build/section-check'
out.mkdir(parents=True, exist_ok=True)
command = ['g++', '-std=c++20', '-msse4.1', '-g', str(source / 'section_check.cpp'),
           '-I', str(root / 'N64Recomp/include'),
           '-I', str(root / 'RecompiledFuncs-probe'),
           '-I', str(root / 'N64ModernRuntime/librecomp/include'),
           '-I', str(root / 'N64ModernRuntime/ultramodern/include'),
           '-o', str(out / 'RushSectionCheck')]
command += [str(p) for p in sorted((root / 'compat').glob('*.cpp'))]
command += [str(p) for p in sorted((root / 'RecompiledRsp').glob('*.cpp'))]
command += ['-Wl,--start-group', '-Wl,--whole-archive',
            str(root / 'build/compile-check/librush_recompiled.a'), '-Wl,--no-whole-archive']
command += [str(p) for p in sorted((root / 'build/runtime').rglob('*.a'))]
command += ['-Wl,--end-group', '-pthread', '-ldl', '-lm']
log = out / 'build.log'
with log.open('w') as output:
    result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
if result.returncode:
    print(log.read_text()[-6000:])
    raise SystemExit(result.returncode)
result = subprocess.run([str(out / 'RushSectionCheck')], capture_output=True, text=True)
print(result.stdout, end='')
print(result.stderr, end='')
(out / 'run.log').write_text(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit(result.returncode)
(root / 'recomp-input/section-check.json').write_text(json.dumps({
    'rom_sha1': hashlib.sha1(rom).hexdigest(),
    'images_verified': verified_images, 'runtime_dispatch_passed': True,
    'guest_code_executed': False, 'log': str(out / 'run.log'),
}, indent=2) + '\n')
print('Original ROM and decomp untouched. Section-check report saved.')

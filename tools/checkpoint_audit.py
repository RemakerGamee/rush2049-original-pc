"""Block private game inputs, generated output and obvious credentials in Git."""
import argparse, re, subprocess, sys
from pathlib import PurePosixPath
parser = argparse.ArgumentParser()
parser.add_argument('--staged', action='store_true')
parser.add_argument('--history', action='store_true')
args = parser.parse_args()
def git(*argv): return subprocess.check_output(['git', *argv])
bad_ext = {'.z64','.n64','.v64','.rom','.bin','.pcm','.wav','.mp3','.zip','.tar','.gz','.7z','.exe','.dll','.so','.a','.o','.elf','.pem','.key','.mpk','.pak','.sav','.srm','.sra','.eep','.fla','.state',
           '.png','.jpg','.jpeg','.gif','.webp','.bmp','.ogg','.flac','.aiff','.mp4','.webm'}
bad_roots = {'userdata','build','assets','asm','recompiledrsp','recompiledfuncs-probe','.venv','venv','__pycache__'}
magic = (bytes.fromhex('80371240'),bytes.fromhex('37804012'),bytes.fromhex('40123780'))
secret = re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)')
if args.history:
    raw = git('rev-list','--objects','--all').decode().splitlines()
    items = [(line.split(' ',1)[0],line.split(' ',1)[1]) for line in raw if ' ' in line]
else:
    raw = git('ls-files','--stage','-z').decode().split('\0')
    items = [(row.split()[1],row.split('\t',1)[1]) for row in raw if '\t' in row and not row.startswith('160000 ')]
errors=[]
scanned=0
for oid,name in items:
    if git('cat-file','-t',oid).strip() != b'blob': continue
    p=PurePosixPath(name)
    if p.suffix.lower() in bad_ext or any(part.lower() in bad_roots or part.lower().startswith('recompiledfuncs') for part in p.parts) or p.name.lower().startswith('baserom') or p.name.lower().startswith('.env') and p.name.lower() != '.env.example':
        errors.append('forbidden path: '+name); continue
    if int(git('cat-file','-s',oid)) > 2_000_000:
        errors.append('oversized source: '+name); continue
    data=git('cat-file','blob',oid)
    scanned+=1
    if data.startswith(magic): errors.append('ROM signature: '+name)
    if b'\0' in data: errors.append('binary content: '+name)
    if len(data)>2_000_000: errors.append('oversized source: '+name)
    if secret.search(data): errors.append('credential signature: '+name)
if errors:
    print('\n'.join(sorted(set(errors))))
    sys.exit(1)
print(f'Checkpoint audit passed: {scanned} source/document blobs; no forbidden game inputs or credential signatures.')

#!/usr/bin/env python3
"""Download pinned build sources into a NEW donor directory; install no system software."""
import argparse, json, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
URLS={'N64ModernRuntime':'https://github.com/N64Recomp/N64ModernRuntime.git',
      'N64Recomp':'https://github.com/N64Recomp/N64Recomp.git','rt64':'https://github.com/rt64/rt64.git'}
def setup(destination):
    destination=Path(destination).absolute()
    if destination.exists() or destination.is_symlink(): raise ValueError('Dependency directory must be new')
    if shutil.which('git') is None: raise ValueError('Install Git before setup')
    pins=json.loads((ROOT/'tools/windows_native/dependencies.json').read_text())
    destination.mkdir(parents=True)
    for name,url in URLS.items():
        tree=destination/name
        subprocess.run(['git','clone','--no-checkout',url,str(tree)],check=True)
        subprocess.run(['git','-C',str(tree),'checkout','--detach',pins[name]['pin']],check=True)
        subprocess.run(['git','-C',str(tree),'submodule','update','--init','--recursive'],check=True)
    print('Pinned dependencies ready. No drivers, services or system toolchains changed.')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('destination',type=Path)
    setup(parser.parse_args().destination)

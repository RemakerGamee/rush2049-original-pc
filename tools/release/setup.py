#!/usr/bin/env python3
"""Single-command native Windows ROM setup; install no system software."""
import argparse, os, shutil, subprocess, sys, uuid
from pathlib import Path
import dependencies, install, prepare

def setup(rom,destination,data,donor=None,workspace=None,generator='Visual Studio 18 2026',remaster_pack=None):
    if os.name!='nt': raise ValueError('Native Windows setup required; use prepare.py for Linux generation')
    if sys.version_info<(3,12): raise ValueError('Python 3.12 or newer required')
    destination=Path(destination).absolute();data=Path(data).absolute()
    if destination.exists(): raise ValueError('Installation directory must be new')
    for command in ('git','cmake'):
        if shutil.which(command) is None: raise ValueError('Missing prerequisite: '+command)
    prepare.expanded_rom(prepare.read_rom(rom))
    workspace=Path(workspace).absolute() if workspace else destination.parent/('.rush-build-'+uuid.uuid4().hex[:8])
    if workspace.exists(): raise ValueError('Build workspace must be new')
    if donor is None:
        donor=workspace/'dependencies';dependencies.setup(donor)
    work=workspace/'build-work';prepare.setup(work,rom,donor)
    native=work/'native'
    commands=([shutil.which('cmake'),'-S',str(native/'source'),'-B',str(native/'build'),'-G',generator,'-A','x64'],
              [shutil.which('cmake'),'--build',str(native/'build'),'--config','Release','--target','RushWindowsRT64','--parallel','2'])
    for index,command in enumerate(commands): prepare.run(command,work/'logs'/f'native-{index}.log')
    if not data.exists(): install.migrate(data)
    install.install(native,destination,data,remaster_pack=remaster_pack)
    print('Ready:',destination/'start.cmd','Player data:',data,'Private build:',workspace)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',required=True,type=Path);parser.add_argument('--install-dir',required=True,type=Path)
    parser.add_argument('--data-dir',type=Path,default=Path(os.environ.get('LOCALAPPDATA',str(Path.home())))/'Rush2049/PlayerData')
    parser.add_argument('--dependency-donor',type=Path);parser.add_argument('--workspace',type=Path)
    parser.add_argument('--generator',default='Visual Studio 18 2026')
    parser.add_argument('--remaster-pack',type=Path)
    args=parser.parse_args()
    setup(args.rom,args.install_dir,args.data_dir,args.dependency_donor,args.workspace,args.generator,args.remaster_pack)

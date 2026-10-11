"""Build the original RT64 Windows preview from your supported US ROM."""
import argparse, os, sys
from pathlib import Path
from setup import setup

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path)
    parser.add_argument('--install-dir',type=Path)
    parser.add_argument('--data-dir',type=Path,default=Path(os.environ.get('LOCALAPPDATA',str(Path.home())))/'Rush2049Original/PlayerData')
    parser.add_argument('--remaster-pack',type=Path,help='Optional existing local RT64 v5 texture pack; no pack is bundled')
    parser.add_argument('--workspace',type=Path)
    parser.add_argument('--dependency-donor',type=Path)
    parser.add_argument('--generator',default='Visual Studio 18 2026')
    args=parser.parse_args()
    if os.name!='nt': raise ValueError('This preview requires 64-bit Windows')
    print('Rush 2049 Original PC — RT64 source-built preview')
    print('Your ROM is never modified. Existing player data is preserved.')
    rom=args.rom or Path(input('Your original US ROM path: ').strip().strip('"'))
    destination=args.install_dir or Path(input('NEW game folder (example C:\\Games\\Rush2049Original): ').strip().strip('"'))
    setup(rom,destination,args.data_dir,args.dependency_donor,args.workspace,args.generator,remaster_pack=args.remaster_pack)

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError) as error:
        print('Setup stopped: '+str(error),file=sys.stderr);sys.exit(1)

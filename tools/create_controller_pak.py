#!/usr/bin/env python3
"""Create a fresh empty 32 KiB Controller Pak. Never overwrite an existing file."""
from pathlib import Path
import argparse, secrets, os
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('path',type=Path)
args=parser.parse_args()
image=bytearray(32768)
identity=bytearray(secrets.token_bytes(24)+bytes([0,1,1,1])+bytes(4))
words=[int.from_bytes(identity[i:i+2],'big') for i in range(0,28,2)]
identity[28:30]=(sum(words)&65535).to_bytes(2,'big')
identity[30:32]=(sum(65535-word for word in words)&65535).to_bytes(2,'big')
for address in (0x20,0x60,0x80,0xC0):image[address:address+32]=identity
inode=bytearray(256)
for page in range(5,128):inode[page*2+1]=3
inode[1]=sum(inode[10:])&255
image[256:512]=image[512:768]=inode
args.path.parent.mkdir(parents=True,exist_ok=True)
with args.path.open('xb') as output:
    output.write(image);output.flush();os.fsync(output.fileno())
print(f'Created empty Controller Pak: {args.path}')

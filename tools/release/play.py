#!/usr/bin/env python3
"""Validate and launch a private Windows installation; never modify player data."""
import argparse, ctypes, hashlib, json, os, stat, subprocess, sys
from ctypes import wintypes
from pathlib import Path

def regular(path,limit):
    if path.is_symlink() or (hasattr(path,'is_junction') and path.is_junction()): raise ValueError('Linked files are unsupported')
    info=path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_size>limit: raise ValueError('Invalid file size/type')
    return path.read_bytes()

def validate(root):
    manifest=json.loads(regular(root/'installation.json',2*1024*1024))
    if manifest.get('schema')!=1 or not isinstance(manifest.get('files'),dict) or len(manifest['files'])>16384:
        raise ValueError('Unsupported installation manifest')
    required={'RushWindowsRT64.exe','SDL2.dll','dxcompiler.dll','dxil.dll','game.z64',
              'recomp-input/rush2049-all-images.bin','player-data.json','play.py','start.cmd'}
    if not required.issubset(manifest['files']): raise ValueError('Required runtime file missing from manifest')
    for name,digest in manifest['files'].items():
        relative=Path(name)
        if relative.is_absolute() or relative.drive or '..' in relative.parts or ':' in name:
            raise ValueError('Unsafe manifest path')
        if hashlib.sha256(regular(root/relative,128*1024*1024)).hexdigest()!=digest:
            raise ValueError('Installation changed: '+name)
    data=Path(json.loads(regular(root/'player-data.json',1024*1024))['data_directory'])
    if not data.is_absolute(): raise ValueError('Player-data path must be absolute')
    for parent in (data,*data.parents):
        if parent.is_symlink() or (hasattr(parent,'is_junction') and parent.is_junction()): raise ValueError('Linked player data unsupported')
    if len(regular(data/'controller.mpk',32768))!=32768: raise ValueError('Pak size differs')
    regular(data/'input.cfg',1024*1024)
    return data

class WindowsSession:
    mutex_name='Local\\Rush2049-Native-Session'
    def __init__(self,data):
        if os.name!='nt': raise RuntimeError('Game execution requires Windows')
        self.api=ctypes.WinDLL('kernel32',use_last_error=True)
        signatures={'CreateMutexW':([wintypes.LPVOID,wintypes.BOOL,wintypes.LPCWSTR],wintypes.HANDLE),
          'WaitForSingleObject':([wintypes.HANDLE,wintypes.DWORD],wintypes.DWORD),
          'ReleaseMutex':([wintypes.HANDLE],wintypes.BOOL),'CloseHandle':([wintypes.HANDLE],wintypes.BOOL),
          'CreateFileW':([wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE],wintypes.HANDLE)}
        for name,(arguments,result) in signatures.items():
            function=getattr(self.api,name);function.argtypes=arguments;function.restype=result
        self.mutex=None;self.file=None;self.owned=False;self.data=data
    def __enter__(self):
        try:
            self.mutex=self.api.CreateMutexW(None,False,self.mutex_name)
            if not self.mutex: raise ctypes.WinError(ctypes.get_last_error())
            result=self.api.WaitForSingleObject(self.mutex,0)
            if result not in (0,0x80): raise RuntimeError('Another installed Rush session is active')
            self.owned=True
            self.file=self.api.CreateFileW(str(self.data/'session.lock'),0xC0000000,0,None,4,0x80,None)
            if self.file==ctypes.c_void_p(-1).value:
                self.file=None;raise RuntimeError('Player-data session.lock is held; launch refused')
            return self
        except BaseException:
            self.__exit__(None,None,None);raise
    def __exit__(self,*unused):
        if self.file: self.api.CloseHandle(self.file);self.file=None
        if self.mutex:
            if self.owned: self.api.ReleaseMutex(self.mutex)
            self.api.CloseHandle(self.mutex);self.mutex=None;self.owned=False

def other_rush():
    api=ctypes.WinDLL('kernel32',use_last_error=True)
    class Entry(ctypes.Structure):
        _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('pid',wintypes.DWORD),
          ('heap',ctypes.c_size_t),('module',wintypes.DWORD),('threads',wintypes.DWORD),
          ('parent',wintypes.DWORD),('priority',wintypes.LONG),('flags',wintypes.DWORD),('name',wintypes.WCHAR*260)]
    api.CreateToolhelp32Snapshot.argtypes=[wintypes.DWORD,wintypes.DWORD];api.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
    for name in ('Process32FirstW','Process32NextW'):
        function=getattr(api,name);function.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)];function.restype=wintypes.BOOL
    api.CloseHandle.argtypes=[wintypes.HANDLE];api.CloseHandle.restype=wintypes.BOOL
    handle=api.CreateToolhelp32Snapshot(2,0)
    if handle==ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry=Entry();entry.size=ctypes.sizeof(entry)
        present=api.Process32FirstW(handle,ctypes.byref(entry))
        if not present and ctypes.get_last_error()!=18: raise ctypes.WinError(ctypes.get_last_error())
        while present:
            if entry.pid!=os.getpid() and entry.name.lower().startswith('rush'): return entry.name
            present=api.Process32NextW(handle,ctypes.byref(entry))
        if ctypes.get_last_error()!=18: raise ctypes.WinError(ctypes.get_last_error())
        return None
    finally: api.CloseHandle(handle)

def launch(root,validate_only=False):
    data=validate(root)
    if validate_only:
        print('Installation and player-data preflight verified; no game launched');return 0
    with WindowsSession(data):
        existing=other_rush()
        if existing: raise RuntimeError('Another Rush instance is active: '+existing)
        environment={key:value for key,value in os.environ.items() if not key.startswith('RUSH_') and key not in ('VK_DRIVER_FILES','VK_ICD_FILENAMES')}
        environment['RUSH_CONTROLLER_PAK']=str(data/'controller.mpk')
        environment['RUSH_INPUT_CONFIG']=str(data/'input.cfg')
        if (root/'remaster-pack/rt64.json').is_file():
            environment['RUSH_REMASTER_TEXTURE_PACK']=str((root/'remaster-pack').resolve())
            environment['RUSH_REMASTER_CIRCUIT_GLOW']='1'

        child=subprocess.Popen([str(root/'RushWindowsRT64.exe'),str(root/'game.z64'),str(data/'profile'),'play'],cwd=root,env=environment)
        try: return child.wait()
        except KeyboardInterrupt:
            child.terminate()
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.kill();child.wait()
            return 130

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--validate-only',action='store_true')
    arguments=parser.parse_args()
    try: sys.exit(launch(Path(__file__).resolve().parent,arguments.validate_only))
    except (OSError,ValueError,RuntimeError,KeyError) as error:
        print('Rush launch blocked: '+str(error),file=sys.stderr);sys.exit(1)

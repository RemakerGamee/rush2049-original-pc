"""Embed a supplied multi-size ICO into an unsigned, staged Windows executable."""
import ctypes
import struct
from pathlib import Path

def embed_icon(executable, icon):
    raw = Path(icon).read_bytes()
    reserved, kind, count = struct.unpack_from('<HHH', raw)
    if reserved or kind != 1 or not count:
        raise ValueError('Windows ICO required')
    entries = []
    for i in range(count):
        width, height, colors, reserved, planes, bits, size, offset = struct.unpack_from('<BBBBHHII', raw, 6 + 16 * i)
        if offset + size > len(raw):
            raise ValueError('Incomplete icon data')
        entries.append((struct.pack('<BBBBHHIH', width, height, colors, reserved, planes, bits, size, i + 1), raw[offset:offset + size]))
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.BeginUpdateResourceW.argtypes = [ctypes.c_wchar_p, ctypes.c_int]
    api.BeginUpdateResourceW.restype = ctypes.c_void_p
    api.UpdateResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ushort, ctypes.c_void_p, ctypes.c_uint]
    api.UpdateResourceW.restype = ctypes.c_int
    api.EndUpdateResourceW.argtypes = [ctypes.c_void_p, ctypes.c_int]
    api.EndUpdateResourceW.restype = ctypes.c_int
    handle = api.BeginUpdateResourceW(str(Path(executable).resolve()), False)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        for i, (_, pixels) in enumerate(entries, 1):
            buffer = ctypes.create_string_buffer(pixels)
            if not api.UpdateResourceW(handle, 3, i, 0, buffer, len(pixels)):
                raise ctypes.WinError(ctypes.get_last_error())
        group = struct.pack('<HHH', 0, 1, count) + b''.join(entry for entry, _ in entries)
        buffer = ctypes.create_string_buffer(group)
        if not api.UpdateResourceW(handle, 14, 1, 0, buffer, len(group)):
            raise ctypes.WinError(ctypes.get_last_error())
    except BaseException:
        api.EndUpdateResourceW(handle, True)
        raise
    if not api.EndUpdateResourceW(handle, False):
        raise ctypes.WinError(ctypes.get_last_error())

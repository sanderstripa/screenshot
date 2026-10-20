"""Verify real Windows EXE icon resources against every approved ICO frame."""
import ctypes as c
from ctypes import wintypes as w
import struct, sys, hashlib, io
from PIL import Image, ImageChops
from pathlib import Path

k=c.windll.kernel32
k.LoadLibraryExW.argtypes=[w.LPCWSTR,w.HANDLE,w.DWORD];k.LoadLibraryExW.restype=w.HMODULE
k.FindResourceW.argtypes=[w.HMODULE,c.c_void_p,c.c_void_p];k.FindResourceW.restype=w.HANDLE
k.LoadResource.argtypes=[w.HMODULE,w.HANDLE];k.LoadResource.restype=w.HANDLE
k.SizeofResource.argtypes=[w.HMODULE,w.HANDLE];k.SizeofResource.restype=w.DWORD
k.LockResource.argtypes=[w.HANDLE];k.LockResource.restype=c.c_void_p
k.FreeLibrary.argtypes=[w.HMODULE]
ico=Path(sys.argv[1]).read_bytes()
count=struct.unpack_from('<H',ico,4)[0]
expected={}
for i in range(count):
    width,height,_,_,_,_,length,offset=struct.unpack_from('<BBBBHHII',ico,6+i*16)
    expected[(width or 256,height or 256)]=ico[offset:offset+length]
if set(expected)!={(n,n) for n in (16,20,24,32,40,48,64,128,256)}:
    raise RuntimeError('Incomplete source icon sizes')
for file in sys.argv[2:]:
    if 'Setup' in Path(file).name and Path(sys.argv[1]).with_suffix('.png').read_bytes() not in Path(file).read_bytes():
        raise RuntimeError('Installer runtime window icon does not contain the approved PNG')
    module=k.LoadLibraryExW(str(Path(file).resolve()),None,0x22)
    if not module:raise c.WinError()
    try:
        def resource(id,type):
            handle=k.FindResourceW(module,c.c_void_p(id),c.c_void_p(type))
            if not handle:raise c.WinError()
            data=k.LoadResource(module,handle)
            return c.string_at(k.LockResource(data),k.SizeofResource(module,handle))
        groups=[]
        callbackType=c.WINFUNCTYPE(w.BOOL,w.HMODULE,c.c_void_p,c.c_void_p,w.LPARAM)
        @callbackType
        def collect(module,type,name,param):
            if name>65535:raise RuntimeError('Unexpected named icon resource')
            groups.append(name);return True
        k.EnumResourceNamesW.argtypes=[w.HMODULE,c.c_void_p,callbackType,w.LPARAM]
        if not k.EnumResourceNamesW(module,c.c_void_p(14),collect,0) or not groups:raise c.WinError()
        group=resource(groups[0],14)
        number=struct.unpack_from('<H',group,4)[0]
        if number!=count:raise RuntimeError(f'{file}: old icon frame count')
        found=set()
        for i in range(number):
            width,height,_,_,_,_,length,id=struct.unpack_from('<BBBBHHIH',group,6+i*14)
            size=(width or 256,height or 256)
            actual=resource(id,3)
            # MSVC preserves PNG bytes; Go/winres can re-encode them as DIBs.
            if actual!=expected[size]:
                def decode(data):
                    header=struct.pack('<HHH',0,1,1)+struct.pack('<BBBBHHII',width,height,0,0,1,32,len(data),22)
                    return Image.open(io.BytesIO(header+data)).convert('RGBA')
                a,b=decode(actual),decode(expected[size])
                if a.getchannel('A').tobytes()!=b.getchannel('A').tobytes():raise RuntimeError(f'{file}: alpha differs at {size}')
                for color in ('white','#202122'):
                    canvas=Image.new('RGBA',size,color)
                    if ImageChops.difference(Image.alpha_composite(canvas,a),Image.alpha_composite(canvas,b)).convert('RGB').getbbox():raise RuntimeError(f'{file}: stale artwork at {size}')
            found.add(size)
        if found!=set(expected):raise RuntimeError('Incomplete embedded icon')
        if 'Setup' in Path(file).name and groups[0]==101:

            master=resource(201,10)
            source=Path(sys.argv[1]).with_suffix('.png').read_bytes()
            if master!=source:raise RuntimeError('Installer master PNG differs')
        print(f'PASS {Path(file).name}: all nine embedded icon frames match approved ICO exactly')
    finally:k.FreeLibrary(module)

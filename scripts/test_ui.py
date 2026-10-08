"""Capture the real native installer at five DPI scales and all six stages."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess, sys, time, struct
from PIL import Image, ImageGrab, ImageDraw

user=c.windll.user32
user.SetProcessDpiAwarenessContext(c.c_void_p(-4))
user.FindWindowW.restype=w.HWND
user.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
user.SetForegroundWindow.argtypes=[w.HWND]
user.ShowWindow.argtypes=[w.HWND,c.c_int]
user.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
user.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT]
gdi=c.windll.gdi32
gdi.CreateCompatibleDC.argtypes=[w.HDC];gdi.CreateCompatibleDC.restype=w.HDC
gdi.CreateDIBSection.argtypes=[w.HDC,c.c_void_p,w.UINT,c.POINTER(c.c_void_p),w.HANDLE,w.DWORD]
gdi.CreateDIBSection.restype=w.HANDLE
gdi.SelectObject.argtypes=[w.HDC,w.HANDLE];gdi.SelectObject.restype=w.HANDLE
gdi.DeleteObject.argtypes=[w.HANDLE];gdi.DeleteDC.argtypes=[w.HDC]
def native_window_shot(hwnd,width,height):
    dc=gdi.CreateCompatibleDC(None)
    info=c.create_string_buffer(struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,0,0,0,0,0))
    bits=c.c_void_p()
    bitmap=gdi.CreateDIBSection(dc,info,0,c.byref(bits),None,0)
    previous=gdi.SelectObject(dc,bitmap)
    try:
        if not user.PrintWindow(hwnd,dc,2):raise RuntimeError('Native window capture failed')
        im=Image.frombytes('RGBA',(width,height),c.string_at(bits,width*height*4),'raw','BGRA').convert('RGB')
        framed=Image.new('RGB',(width+16,height+16),'#20252d');framed.paste(im,(8,8))
        return framed
    finally:
        gdi.SelectObject(dc,previous);gdi.DeleteObject(bitmap);gdi.DeleteDC(dc)
setup=str(Path(sys.argv[1]).resolve())
out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
tiles=[]
for dpi in (96,120,144,192,240):
    for step in range(6):
        info=subprocess.STARTUPINFO()
        info.dwFlags=subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow=0
        process=subprocess.Popen([setup,'--preview',str(step),str(dpi)],startupinfo=info)
        try:
            hwnd=0
            for _ in range(100):
                hwnd=user.FindWindowW('ScreenshotPremiumSetup',None)
                if hwnd:break
                if process.poll() is not None:raise RuntimeError('Preview exited')
                time.sleep(.05)
            if not hwnd:raise RuntimeError('Native window missing')
            user.ShowWindow(hwnd,9)
            user.SetWindowPos(hwnd,w.HWND(-1),0,0,0,0,0x3)
            user.SetForegroundWindow(hwnd)
            time.sleep(.45)
            rect=w.RECT();user.GetWindowRect(hwnd,c.byref(rect))
            width=rect.right-rect.left;height=rect.bottom-rect.top
            if abs(width-640*dpi/96)>2 or abs(height-400*dpi/96)>2:
                raise RuntimeError(f'Wrong DPI dimensions: {width} x {height} at {dpi}')
            shot=ImageGrab.grab((rect.left-8,rect.top-8,rect.right+8,rect.bottom+8),all_screens=True)
            surface=shot.getpixel((int(32*dpi/96)+8,int(300*dpi/96)+8))
            if max(surface[:3])>70 or surface[2]<surface[0]:
                # An unrelated popup may overlap the desktop; capture our actual HWND directly.
                shot=native_window_shot(hwnd,width,height)
            shot.save(out/f'step-{step}-dpi-{dpi}.png')
            # Ensure an actual painted D2D window, rather than a blank or black surface.
            center=shot.getpixel((width//2+8,height//2+8))
            if max(center[:3])<8:raise RuntimeError('Black rendering surface')
            surface=shot.getpixel((int(32*dpi/96)+8,int(300*dpi/96)+8))
            if max(surface[:3])>70 or surface[2]<surface[0]:
                raise RuntimeError('Installer obscured by another window')
            tile=Image.new('RGB',(336,240),'#20252d')
            shot.thumbnail((328,212));tile.paste(shot,((336-shot.width)//2,0))
            ImageDraw.Draw(tile).text((10,221),f'Stage {step} | {round(dpi/96*100)}%',fill='white')
            tiles.append(tile)
            print(f'PASS stage={step}, dpi={dpi}, size={width}x{height}',flush=True)
        finally:
            if hwnd:user.PostMessageW(hwnd,0x10,0,0)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.terminate();process.wait()
sheet=Image.new('RGB',(336*6,240*5))
for i,tile in enumerate(tiles):sheet.paste(tile,((i%6)*336,(i//6)*240))
sheet.save(out/'installer-contact-sheet.png')

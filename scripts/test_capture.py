"""Exercise mouse-driven window/region capture, PNG pixels and clipboard byte equality."""
import ctypes as c
from ctypes import wintypes as w
import os, subprocess, sys, time, tkinter as tk
from pathlib import Path
from PIL import Image

u=c.windll.user32;k=c.windll.kernel32
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.FindWindowW.restype=w.HWND
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
u.GetClipboardData.restype=w.HANDLE
k.GlobalLock.argtypes=[w.HANDLE];k.GlobalLock.restype=c.c_void_p
k.GlobalSize.argtypes=[w.HANDLE];k.GlobalSize.restype=c.c_size_t
k.GlobalUnlock.argtypes=[w.HANDLE]
exe=str(Path(sys.argv[1]).resolve());out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
folder=Path(os.environ['USERPROFILE'])/'Pictures'/'Screenshot'
fixture=tk.Tk();fixture.title('Screenshot capture verification')
fixture.geometry('500x300+100+150');fixture.configure(bg='#1680e8')
fixture.attributes('-topmost',True);fixture.update();time.sleep(.3)
reports=[]
try:
    for mode in ('window','region'):
        before=set(folder.glob('*.png'))
        info=subprocess.STARTUPINFO();info.dwFlags=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=0
        process=subprocess.Popen([exe,'--capture-now'],startupinfo=info)
        try:
            overlay=0
            for _ in range(100):
                overlay=u.FindWindowW('ScreenshotOverlay',None)
                if overlay:break
                fixture.update();time.sleep(.05)
            if not overlay:raise RuntimeError('Capture overlay did not open')
            time.sleep(.4)
            u.SetCursorPos(250,280)
            u.PostMessageW(overlay,0x200,0,0);time.sleep(.08)
            u.PostMessageW(overlay,0x201,1,0);time.sleep(.08)
            if mode=='region':
                u.SetCursorPos(450,400);u.PostMessageW(overlay,0x200,1,0);time.sleep(.1)
            u.PostMessageW(overlay,0x202,0,0)
            created=set()
            for _ in range(100):
                created=set(folder.glob('*.png'))-before
                if created:break
                time.sleep(.05)
            if len(created)!=1:raise RuntimeError(f'{mode}: PNG was not saved')
            path=created.pop();data=path.read_bytes();im=Image.open(path)
            if mode=='region' and im.size!=(248,168):raise RuntimeError(f'Wrong crop: {im.size}')
            pixel=im.convert('RGBA').getpixel((im.width//2,im.height//2))
            if any(abs(a-b)>3 for a,b in zip(pixel,(22,128,232,255))):
                raise RuntimeError(f'{mode}: wrong captured pixels: {pixel}')
            if not u.OpenClipboard(None):raise RuntimeError('Cannot read clipboard')
            try:
                handle=u.GetClipboardData(u.RegisterClipboardFormatW('PNG'))
                if not handle:raise RuntimeError('PNG clipboard format missing')
                pointer=k.GlobalLock(handle)
                clipboard=c.string_at(pointer,k.GlobalSize(handle));k.GlobalUnlock(handle)
                if clipboard[:len(data)]!=data:raise RuntimeError('Clipboard PNG differs from saved file')
            finally:u.CloseClipboard()
            im.save(out/f'capture-{mode}.png')
            reports.append(f'PASS {mode}: {im.size}, matching disk/clipboard PNG, correct pixels')
            print(reports[-1],flush=True)
        finally:
            controller=u.FindWindowW('ScreenshotController',None)
            if controller:u.PostMessageW(controller,0x10,0,0)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.terminate();process.wait()
finally:fixture.destroy()
(out/'capture-tests.txt').write_text('\n'.join(reports),encoding='utf-8')

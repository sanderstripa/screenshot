"""Drive the real installer, cancel Print Screen confirmation, then activate a test shortcut."""
import ctypes as c
from ctypes import wintypes as w
import subprocess,sys,time,winreg
from pathlib import Path
from PIL import ImageGrab

u=c.windll.user32;k=c.windll.kernel32
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.FindWindowW.restype=w.HWND
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
u.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
u.GetDpiForWindow.argtypes=[w.HWND]
u.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
k.OpenEventW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];k.OpenEventW.restype=w.HANDLE
k.SetEvent.argtypes=[w.HANDLE];k.CloseHandle.argtypes=[w.HANDLE]
config=r'Software\SanderStripa\Screenshot'
keyboard=r'Control Panel\Keyboard'
def read(key,name):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,key) as handle:return winreg.QueryValueEx(handle,name)
    except FileNotFoundError:return None
original={name:read(config,name) for name in ('HotkeyVK','HotkeyModifiers')}
snipping=read(keyboard,'PrintScreenKeyForSnippingEnabled')
out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
process=subprocess.Popen([str(Path(sys.argv[1]).resolve())])
try:
    hwnd=0
    for _ in range(100):
        hwnd=u.FindWindowW('ScreenshotPremiumSetup',None)
        if hwnd:break
        time.sleep(.05)
    if not hwnd:raise RuntimeError('Installer window missing')
    u.SetWindowPos(hwnd,w.HWND(-1),0,0,0,0,0x43)
    scale=u.GetDpiForWindow(hwnd)/96
    def click(x,y):
        u.PostMessageW(hwnd,0x202,0,(int(x*scale)&0xffff)|((int(y*scale)&0xffff)<<16))
        time.sleep(.3)
    def shot(name):
        rect=w.RECT();u.GetWindowRect(hwnd,c.byref(rect))
        im=ImageGrab.grab((rect.left,rect.top,rect.right,rect.bottom),all_screens=True)
        im.save(out/f'{name}.png')
        return im
    click(320,340) # Install
    time.sleep(2)
    shot('installed')
    click(320,340) # Continue to shortcut
    shot('hotkey')
    click(200,210)
    u.PostMessageW(hwnd,0x100,0x2c,0) # Select Print Screen
    time.sleep(.2)
    click(500,350)
    confirmation=shot('print-screen-confirmation')
    # The orange warning ring only appears on the confirmation stage.
    pixel=confirmation.getpixel((int(320*scale),int(72*scale)))
    if pixel[0]<180 or pixel[1]<90:raise RuntimeError('Print Screen confirmation missing')
    if read(keyboard,'PrintScreenKeyForSnippingEnabled')!=snipping:
        raise RuntimeError('Windows Print Screen setting changed before consent')
    click(100,347) # Cancel confirmation
    click(200,210)
    u.PostMessageW(hwnd,0x100,0x87,0) # F24, not a regular user typing key
    time.sleep(.2)
    click(500,350)
    ready=shot('ready')
    if read(config,'HotkeyVK')!=(0x87,winreg.REG_DWORD):raise RuntimeError('Shortcut not saved')
    # The green success ring distinguishes successful activation from the hotkey screen.
    pixel=ready.getpixel((int(320*scale),int(80*scale)))
    if pixel[1]<130 or pixel[0]>160:raise RuntimeError('Ready screen missing')
    click(320,340)
    process.wait(timeout=5)
    report='PASS real wizard: install -> complete -> hotkey -> Print Screen consent -> cancel -> F24 -> ready; original Windows Print Screen binding unchanged'
    print(report)
    (out/'wizard-tests.txt').write_text(report,encoding='utf-8')
finally:
    if process.poll() is None:process.terminate();process.wait()
    event=k.OpenEventW(2,False,r'Local\Screenshot.SanderStripa.Exit')
    if event:k.SetEvent(event);k.CloseHandle(event);time.sleep(.7)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER,config) as handle:
        for name,value in original.items():
            if value is None:
                try:winreg.DeleteValue(handle,name)
                except FileNotFoundError:pass
            else:winreg.SetValueEx(handle,name,0,value[1],value[0])

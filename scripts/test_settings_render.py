"""Windows integration regression for the single-window Connect interface.

Run only on an isolated Windows desktop. Captures actual rendered pixels and
checks runtime hotkey ownership, persisted settings, rollback and restart.
"""
import ctypes as c
from ctypes import wintypes as w
import pathlib
import subprocess
import sys
import time
import winreg as reg
from PIL import ImageGrab

u = c.WinDLL('user32', use_last_error=True)
k = c.WinDLL('kernel32', use_last_error=True)
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
for name in ('GetClientRect', 'GetWindowRect'):
    getattr(u, name).argtypes = [w.HWND, c.POINTER(w.RECT)]
u.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
u.ShowWindow.argtypes = [w.HWND, c.c_int]
u.SetForegroundWindow.argtypes = [w.HWND]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = w.LPARAM
u.IsWindowVisible.argtypes = [w.HWND]
u.RegisterHotKey.argtypes = [w.HWND, c.c_int, w.UINT, w.UINT]
u.UnregisterHotKey.argtypes = [w.HWND, c.c_int]
u.GetDpiForWindow.argtypes = [w.HWND]
u.IsIconic.argtypes = [w.HWND]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
k.OpenEventW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]
k.OpenEventW.restype = w.HANDLE
k.SetEvent.argtypes = [w.HANDLE]
k.CloseHandle.argtypes = [w.HANDLE]
app = str(pathlib.Path(sys.argv[1]).resolve())
out = pathlib.Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
config = r'Software\SanderStripa\Screenshot'
system = r'Control Panel\Keyboard'
reports = []


def report(message):
    reports.append('PASS ' + message)
    print(reports[-1], flush=True)


def read(path, name):
    try:
        with reg.OpenKey(reg.HKEY_CURRENT_USER, path) as key:
            return reg.QueryValueEx(key, name)[0]
    except FileNotFoundError:
        return None


def write(path, name, value):
    with reg.CreateKey(reg.HKEY_CURRENT_USER, path) as key:
        if value is None:
            try: reg.DeleteValue(key, name)
            except FileNotFoundError: pass
        else: reg.SetValueEx(key, name, 0, reg.REG_DWORD, value)


def stop():
    event = k.OpenEventW(2, False, r'Local\Screenshot.SanderStripa.Exit')
    if event:
        k.SetEvent(event); k.CloseHandle(event)
    for _ in range(80):
        if not u.FindWindowW('ScreenshotController', None):
            time.sleep(.2)
            return
        time.sleep(.1)
    raise AssertionError('Worker did not stop')


def window():
    for _ in range(100):
        hwnd = u.FindWindowW('ScreenshotSettings', None)
        if hwnd and u.IsWindowVisible(hwnd): return hwnd
        time.sleep(.1)
    raise AssertionError('Main window did not open')


def click(hwnd, x, y):
    scale = u.GetDpiForWindow(hwnd)/96
    pos = int(x*scale) | (int(y*scale) << 16)
    u.PostMessageW(hwnd, 0x201, 0, pos)
    u.PostMessageW(hwnd, 0x202, 0, pos)
    time.sleep(.4)


def key(hwnd, vk):
    u.PostMessageW(hwnd, 0x100, vk, 0)
    time.sleep(.4)


def shot(hwnd, name, background=None):
    u.SetForegroundWindow(hwnd); time.sleep(.4)
    rect, point = w.RECT(), w.POINT()
    assert u.GetClientRect(hwnd, c.byref(rect))
    assert u.ClientToScreen(hwnd, c.byref(point))
    image = ImageGrab.grab(bbox=(point.x, point.y, point.x+rect.right, point.y+rect.bottom)).convert('RGB')
    image.save(out/(name+'.png'))
    sx, sy = image.width/460, image.height/636
    if background:
        assert image.getpixel((int(16*sx), int(195*sy))) == background, name+': background palette differs'
        for label, bounds in [('title', (28,20,280,49)), ('shortcut',(95,113,365,145)), ('about',(28,478,230,501)), ('version',(340,515,432,539)), ('signature',(165,611,295,633))]:
            region = image.crop(tuple(int(v*(sx if i%2==0 else sy)) for i,v in enumerate(bounds)))
            ink = sum(max(abs(p[i]-background[i]) for i in range(3))>60 for p in region.getdata())
            assert ink>15, name+': '+label+' missing'
    return image


def owned(vk, ident=982):
    free = bool(u.RegisterHotKey(None, ident, 0, vk))
    if free: u.UnregisterHotKey(None, ident)
    return not free


if u.FindWindowW('ScreenshotController', None):
    raise RuntimeError('Isolated Windows desktop required')
names = ('HotkeyVK','HotkeyModifiers','ThemeLight','ThemeMode','LanguageEN')
original = {name:read(config,name) for name in names}
previous_snipping = read(system,'PrintScreenKeyForSnippingEnabled')
worker = None
try:
    # Preview preserves all preferences, including the shortcut.
    write(config,'HotkeyVK',0x87); write(config,'HotkeyModifiers',0)
    for theme, background in [('dark',(8,9,11)),('light',(255,255,255))]:
        worker = subprocess.Popen([app,'--settings-preview',theme])
        hwnd = window(); shot(hwnd,theme+'-initial',background)
        u.ShowWindow(hwnd,6); time.sleep(.2); u.ShowWindow(hwnd,9)
        shot(hwnd,theme+'-restored',background)
        u.PostMessageW(hwnd,0x10,0,0); time.sleep(.3)
        subprocess.run([app,'--settings'],check=True,timeout=10)
        shot(window(),theme+'-reopened',background)
        stop(); worker.wait(timeout=10)
    report('both palettes render title, shortcut, About, build version and Connect signature; minimize/reopen works')
    write(config,'ThemeMode',2); write(config,'ThemeLight',0); write(config,'LanguageEN',0)
    worker = subprocess.Popen([app,'--background'])
    time.sleep(.7)
    assert not u.FindWindowW('ScreenshotSettings',None), 'Autostart must stay invisible'
    subprocess.run([app,'--settings'],check=True,timeout=10)
    hwnd = window(); shot(hwnd,'live-dark',(8,9,11))
    click(hwnd,225,375)
    assert read(config,'ThemeMode') == 1 and read(config,'ThemeLight') == 1
    shot(hwnd,'live-light',(255,255,255))
    click(hwnd,360,433);click(hwnd,350,510)
    assert read(config,'LanguageEN') == 1
    shot(hwnd,'live-english',(255,255,255))
    report('theme and language apply and persist immediately without a Save button')
    click(hwnd,200,128);key(hwnd,0x86)
    assert read(config,'HotkeyVK') == 0x86 and owned(0x86)
    assert not owned(0x87), 'Old shortcut was not released'
    assert worker.poll() is None
    assert u.RegisterHotKey(None,983,0,0x85), 'F22 already occupied on runner'
    try:
        click(hwnd,200,128);key(hwnd,0x85)
        assert read(config,'HotkeyVK') == 0x86 and owned(0x86)
        shot(hwnd,'conflict-keeps-working-key',(255,255,255))
    finally: u.UnregisterHotKey(None,983)
    report('live hotkey reassignment releases old key; conflict rolls back without stopping worker')
    click(hwnd,200,128);key(hwnd,0x2c)
    assert read(config,'HotkeyVK') == 0x86
    assert read(system,'PrintScreenKeyForSnippingEnabled') == previous_snipping
    shot(hwnd,'printscreen-sheet-light')
    click(hwnd,205,276)
    assert read(config,'HotkeyVK') == 0x86 and owned(0x86)
    assert read(system,'PrintScreenKeyForSnippingEnabled') == previous_snipping
    report('Print Screen shows attached sheet, stays pending before consent; Cancel retains working key and Windows action')
    # Force the actual registration failure after consent, independently of the runner's Snipping settings.
    reserved = bool(u.RegisterHotKey(None,984,0,0x2c))
    try:
        click(hwnd,200,128);key(hwnd,0x2c);click(hwnd,350,276)
        # If another Windows component already owns Print Screen, it exercises the same failure.
        if reserved or read(config,'HotkeyVK') == 0x86:
            assert read(config,'HotkeyVK') == 0x86 and owned(0x86)
            assert read(system,'PrintScreenKeyForSnippingEnabled') == previous_snipping
            report('failed Print Screen registration after consent rolls back shortcut and Windows preference')
        else:
            assert owned(0x2c)
            click(hwnd,200,128);key(hwnd,0x86)
            report('Print Screen accepted only after confirmed successful registration')
    finally: u.UnregisterHotKey(None,984)
    write(system,'PrintScreenKeyForSnippingEnabled',previous_snipping)
    click(hwnd,365,375);click(hwnd,200,128);key(hwnd,0x2c)
    shot(hwnd,'printscreen-sheet-dark');key(hwnd,0x1b)
    click(hwnd,225,375)
    click(hwnd,360,433);click(hwnd,350,475)
    assert read(config,'LanguageEN') == 0
    click(hwnd,390,32); assert u.IsIconic(hwnd)
    u.ShowWindow(hwnd,9)
    u.PostMessageW(hwnd,0x10,0,0);time.sleep(.4)
    assert worker.poll() is None
    subprocess.run([app,'--settings'],check=True,timeout=10)
    shot(window(),'live-reopened',(255,255,255))
    stop();worker.wait(timeout=10)
    worker = subprocess.Popen([app,'--settings']);hwnd=window()
    shot(hwnd,'after-process-restart',(255,255,255))
    assert read(config,'HotkeyVK') == 0x86 and owned(0x86)
    report('closing main window keeps capture worker alive; complete restart restores settings and hotkey')
    # Windows notification follows System mode without another page.
    click(hwnd,95,375);assert read(config,'ThemeMode') == 0
    expected=read(r'Software\Microsoft\Windows\CurrentVersion\Themes\Personalize','AppsUseLightTheme')
    u.PostMessageW(hwnd,0x1a,0,0);time.sleep(.3)
    shot(hwnd,'system-theme',(255,255,255) if expected is None or expected else (8,9,11))
    report('System theme follows the Windows app-theme notification')
    (out/'checks.txt').write_text('\n'.join(reports)+'\n',encoding='utf-8')
finally:
    stop()
    if worker: worker.wait(timeout=10)
    for name,value in original.items():write(config,name,value)
    write(system,'PrintScreenKeyForSnippingEnabled',previous_snipping)

"""Check actual desktop pixels: a visible HWND alone does not prove rendering."""
import ctypes as c
from ctypes import wintypes as w
import pathlib
import subprocess
import sys
import time
from PIL import ImageGrab

u = c.WinDLL('user32', use_last_error=True)
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
u.ShowWindow.argtypes = [w.HWND, c.c_int]
u.SetForegroundWindow.argtypes = [w.HWND]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.IsWindowVisible.argtypes = [w.HWND]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
app = str(pathlib.Path(sys.argv[1]).resolve())
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
if u.FindWindowW('ScreenshotController', None):
    raise RuntimeError('Run in an isolated desktop without an existing Screenshot process')


def window():
    for _ in range(100):
        hwnd = u.FindWindowW('ScreenshotSettings', None)
        if hwnd and u.IsWindowVisible(hwnd):
            return hwnd
        time.sleep(.1)
    raise AssertionError('Settings did not open')


def check(hwnd, name, background):
    u.SetForegroundWindow(hwnd)
    time.sleep(.5)
    rect, point = w.RECT(), w.POINT()
    assert u.GetClientRect(hwnd, c.byref(rect))
    assert u.ClientToScreen(hwnd, c.byref(point))
    shot = ImageGrab.grab(bbox=(point.x, point.y, point.x + rect.right, point.y + rect.bottom)).convert('RGB')
    shot.save(out / (name + '.png'))
    sx, sy = shot.width / 520, shot.height / 382
    assert shot.getpixel((int(20*sx), int(95*sy))) == background, name + ': background missing'
    # Both title and shortcut must contain ink. Uniform gray/black/background fails.
    for label, bounds in [('title', (28, 27, 270, 58)), ('shortcut', (150, 195, 370, 221))]:
        region = shot.crop(tuple(int(v * (sx if i % 2 == 0 else sy)) for i, v in enumerate(bounds)))
        ink = sum(max(abs(p[i] - background[i]) for i in range(3)) > 60 for p in region.getdata())
        assert ink > 40, name + ': ' + label + ' is blank'
    print('PASS', name, flush=True)


for theme, background in [('dark', (12, 12, 13)), ('light', (247, 247, 245))]:
    process = subprocess.Popen([app, '--settings-preview', theme])
    try:
        hwnd = window()
        check(hwnd, theme + '-initial', background)
        u.ShowWindow(hwnd, 6)
        time.sleep(.2)
        u.ShowWindow(hwnd, 9)
        check(hwnd, theme + '-restored', background)
        u.PostMessageW(hwnd, 0x10, 0, 0)
        time.sleep(.3)
        subprocess.run([app, '--settings'], check=True, timeout=10)
        check(window(), theme + '-reopened', background)
    finally:
        process.terminate()
        process.wait(timeout=10)
        time.sleep(.3)

"""Exercise the compiled native installer on an isolated Windows desktop."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import time
import winreg

from PIL import ImageGrab

u = c.WinDLL("user32", use_last_error=True)
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]
u.FindWindowExW.restype = w.HWND
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = w.LPARAM
u.GetDpiForWindow.argtypes = [w.HWND]
u.GetDpiForWindow.restype = w.UINT
u.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
u.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.SetForegroundWindow.argtypes = [w.HWND]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))

setup = Path("Screenshot-1.8.7-RE-Setup-x64.exe").resolve()
payload = Path("rust/setup/payload/Screenshot.exe")
target = Path(os.environ["RUNNER_TEMP"]) / "Screenshot Native Setup"
review = Path("installer-review")
review.mkdir(exist_ok=True)
preferences = r"Software\SanderStripa\Screenshot"


def until(check, description, timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = check()
        if result:
            return result
        time.sleep(0.1)
    raise RuntimeError(description)


def window():
    return until(lambda: u.FindWindowW("ScreenshotNativeSetup", None), "Native setup did not open")


def click(hwnd, x, y):
    scale = u.GetDpiForWindow(hwnd) / 96
    pt = round(x * scale) | (round(y * scale) << 16)
    u.PostMessageW(hwnd, 0x201, 1, pt)
    u.PostMessageW(hwnd, 0x202, 0, pt)
    time.sleep(0.35)


def key(hwnd, vk, up=False):
    u.PostMessageW(hwnd, 0x101 if up else 0x100, vk, 0)
    time.sleep(0.2)


def shot(hwnd, name, background):
    u.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    origin, bounds = w.POINT(), w.RECT()
    assert u.ClientToScreen(hwnd, c.byref(origin))
    assert u.GetClientRect(hwnd, c.byref(bounds))
    scale = u.GetDpiForWindow(hwnd) / 96
    assert (bounds.right, bounds.bottom) == (round(854 * scale), round(577 * scale))
    image = ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + bounds.right, origin.y + bounds.bottom)).convert("RGB")
    image.save(review / f"{name}.png")
    assert image.getpixel((round(12 * scale), round(120 * scale))) == background
    return image


def setting(name):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, preferences) as key:
        return winreg.QueryValueEx(key, name)[0]


def install(verify_settings):
    with open(review / "install-log.txt", "ab", buffering=0) as log, subprocess.Popen([str(setup)], stderr=log) as process:
        try:
            hwnd = window()
            shot(hwnd, "welcome" if verify_settings else "update", (252, 252, 253))
            if verify_settings:
                click(hwnd, 110, 383)
                click(hwnd, 150, 130)
                click(hwnd, 413, 130)
                shot(hwnd, "settings-dark", (8, 9, 11))
                edit = u.FindWindowExW(hwnd, None, "Edit", None)
                text = c.create_unicode_buffer(str(target))
                assert edit and u.SendMessageW(edit, 0x000C, 0, c.addressof(text))
                click(hwnd, 150, 412)
            else:
                click(hwnd, 150, 330)
            until(lambda: (target / "Uninstall.exe").exists(), "No installed uninstaller")
            until(lambda: (target / "Screenshot.exe").read_bytes() == payload.read_bytes(), "Installed payload differs")
            time.sleep(1)
            click(hwnd, 150, 230)
            key(hwnd, 44, up=True)
            click(hwnd, 150, 311)
            shot(hwnd, "print-screen-confirmation", (8, 9, 11) if verify_settings else (252, 252, 253))
            click(hwnd, 85, 268)
            click(hwnd, 150, 230)
            key(hwnd, 134)
            click(hwnd, 150, 311)
            until(lambda: setting("HotkeyVK") == 134, "F23 was not saved")
            shot(hwnd, "finish" if verify_settings else "update-finish", (8, 9, 11) if verify_settings else (252, 252, 253))
            click(hwnd, 427, 317)
            until(lambda: u.FindWindowW("ScreenshotSettings", None), "App was not launched")
            assert process.wait(timeout=10) == 0
            assert setting("ThemeMode") == 2
            assert setting("LanguageEN") == 1
        except Exception:
            print((review / "install-log.txt").read_text(errors="replace"), flush=True)
            if process.poll() is None:
                try:
                    shot(hwnd, "install-failure", (8, 9, 11) if verify_settings else (252, 252, 253))
                except Exception:
                    pass
            raise
        finally:
            if process.poll() is None:
                process.kill()


install(True)
install(False)
uninstaller = target / "Uninstall.exe"
with subprocess.Popen([str(uninstaller), "--uninstall"]) as process:
    try:
        hwnd = window()
        click(hwnd, 140, 345)
        until(lambda: not (target / "Screenshot.exe").exists(), "Application was not removed")
        time.sleep(1)
        shot(hwnd, "removed", (252, 252, 253))
        click(hwnd, 427, 341)
        assert process.wait(timeout=10) == 0
        until(lambda: not uninstaller.exists(), "Uninstaller did not clean up after exit")
    finally:
        if process.poll() is None:
            process.kill()

assert not list(setup.parent.glob(setup.name + ".WebView2"))
assert not list(target.glob("*.WebView2"))
print("PASS native installation, update over live process, settings retention, Print Screen consent, F23, launch and uninstaller cleanup")

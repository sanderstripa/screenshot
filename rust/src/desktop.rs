use std::{
    fs::{self, OpenOptions},
    io::{self, Write},
    mem::{size_of, zeroed},
    path::PathBuf,
    ptr::{copy_nonoverlapping, null_mut},
    slice, thread,
    time::Duration,
};

use windows_sys::Win32::{
    Foundation::{GlobalFree, HWND, SYSTEMTIME},
    Graphics::Gdi::*,
    System::{DataExchange::*, Memory::*, SystemInformation::GetLocalTime},
    UI::WindowsAndMessaging::*,
};

use crate::image::{Image, Rect};

pub fn capture() -> Option<(Rect, Image)> {
    unsafe {
        let left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        let top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        let width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        let height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        let mut image = Image::new(width.try_into().ok()?, height.try_into().ok()?)?;
        let screen = GetDC(null_mut());
        if screen.is_null() {
            return None;
        }
        let memory = CreateCompatibleDC(screen);
        let mut info: BITMAPINFO = zeroed();
        info.bmiHeader = bitmap_header(width, height);
        let mut bits = null_mut();
        let bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &mut bits, null_mut(), 0);
        let mut captured = false;
        if !memory.is_null() && !bitmap.is_null() && !bits.is_null() {
            let old = SelectObject(memory, bitmap);
            if !old.is_null() {
                captured = BitBlt(
                    memory,
                    0,
                    0,
                    width,
                    height,
                    screen,
                    left,
                    top,
                    SRCCOPY | CAPTUREBLT,
                ) != 0;
                if captured {
                    GdiFlush();
                    let pixels = slice::from_raw_parts(bits.cast::<u8>(), image.rgba.len());
                    for (rgba, bgra) in image.rgba.chunks_exact_mut(4).zip(pixels.chunks_exact(4)) {
                        rgba.copy_from_slice(&[bgra[2], bgra[1], bgra[0], 255]);
                    }
                }
                SelectObject(memory, old);
            }
        }
        if !bitmap.is_null() {
            DeleteObject(bitmap);
        }
        if !memory.is_null() {
            DeleteDC(memory);
        }
        ReleaseDC(null_mut(), screen);
        captured.then_some((
            Rect {
                left,
                top,
                right: left + width,
                bottom: top + height,
            },
            image,
        ))
    }
}

pub fn save(png: &[u8]) -> io::Result<PathBuf> {
    let profile = std::env::var_os("USERPROFILE")
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "USERPROFILE"))?;
    let directory = PathBuf::from(profile).join("Pictures").join("Screenshot");
    fs::create_dir_all(&directory)?;

    let time = unsafe {
        let mut time: SYSTEMTIME = zeroed();
        GetLocalTime(&mut time);
        time
    };
    let stem = format!(
        "Screenshot-{:04}-{:02}-{:02}_{:02}-{:02}-{:02}-{:03}",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds
    );
    for suffix in 0..1000 {
        let name = if suffix == 0 {
            format!("{stem}.png")
        } else {
            format!("{stem}-{suffix}.png")
        };
        let path = directory.join(name);
        let mut file = match OpenOptions::new().write(true).create_new(true).open(&path) {
            Ok(file) => file,
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(error),
        };
        if let Err(error) = file.write_all(png).and_then(|()| file.sync_all()) {
            drop(file);
            let _ = fs::remove_file(&path);
            return Err(error);
        }
        return Ok(path);
    }
    Err(io::Error::new(
        io::ErrorKind::AlreadyExists,
        "Screenshot filename collisions",
    ))
}

fn bitmap_header(width: i32, height: i32) -> BITMAPINFOHEADER {
    BITMAPINFOHEADER {
        biSize: size_of::<BITMAPINFOHEADER>() as u32,
        biWidth: width,
        biHeight: -height,
        biPlanes: 1,
        biBitCount: 32,
        biCompression: BI_RGB,
        ..unsafe { zeroed() }
    }
}

/// The caller supplies a live window owned by the calling GUI thread.
pub unsafe fn publish_clipboard(owner: HWND, image: &Image, png: &[u8]) -> bool {
    let mut opened = false;
    for _ in 0..20 {
        if unsafe { OpenClipboard(owner) } != 0 {
            opened = true;
            break;
        }
        thread::sleep(Duration::from_millis(25));
    }
    if !opened {
        return false;
    }

    let _clipboard = Clipboard;
    if unsafe { EmptyClipboard() } == 0 {
        return false;
    }
    let format = unsafe { RegisterClipboardFormatW([80_u16, 78, 71, 0].as_ptr()) };
    let success = format != 0 && clipboard_block(format, &[], png);

    let mut bgra = image.rgba.clone();
    for pixel in bgra.chunks_exact_mut(4) {
        pixel.swap(0, 2);
    }
    let width = image.width as i32;
    let height = image.height as i32;
    let mut v5: BITMAPV5HEADER = unsafe { zeroed() };
    v5.bV5Size = size_of::<BITMAPV5HEADER>() as u32;
    v5.bV5Width = width;
    v5.bV5Height = -height;
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5RedMask = 0x00ff0000;
    v5.bV5GreenMask = 0x0000ff00;
    v5.bV5BlueMask = 0x000000ff;
    v5.bV5AlphaMask = 0xff000000;
    v5.bV5CSType = 0x73524742;
    v5.bV5Intent = 4;

    let legacy = bitmap_header(width, height);
    unsafe {
        let header = slice::from_raw_parts(
            (&v5 as *const BITMAPV5HEADER).cast(),
            size_of::<BITMAPV5HEADER>(),
        );
        clipboard_block(17, header, &bgra); // CF_DIBV5
        let header = slice::from_raw_parts(
            (&legacy as *const BITMAPINFOHEADER).cast(),
            size_of::<BITMAPINFOHEADER>(),
        );
        clipboard_block(8, header, &bgra); // CF_DIB
    }
    success
}

struct Clipboard;

impl Drop for Clipboard {
    fn drop(&mut self) {
        unsafe { CloseClipboard() };
    }
}

fn clipboard_block(format: u32, header: &[u8], bytes: &[u8]) -> bool {
    unsafe {
        let handle = GlobalAlloc(GMEM_MOVEABLE, header.len() + bytes.len());
        if handle.is_null() {
            return false;
        }
        let dest = GlobalLock(handle).cast::<u8>();
        if !dest.is_null() {
            copy_nonoverlapping(header.as_ptr(), dest, header.len());
            copy_nonoverlapping(bytes.as_ptr(), dest.add(header.len()), bytes.len());
            GlobalUnlock(handle);
            if !SetClipboardData(format, handle).is_null() {
                return true;
            }
        }
        GlobalFree(handle);
        false
    }
}

/// Finds the top visible application window at a screen coordinate.
pub fn window_at(
    point: windows_sys::Win32::Foundation::POINT,
    overlay: HWND,
    controller: HWND,
    desktop: Rect,
) -> Rect {
    use windows_sys::Win32::{
        Foundation::RECT,
        Graphics::Dwm::{DWMWA_CLOAKED, DWMWA_EXTENDED_FRAME_BOUNDS, DwmGetWindowAttribute},
    };
    fn bounds(rect: RECT) -> Rect {
        Rect {
            left: rect.left,
            top: rect.top,
            right: rect.right,
            bottom: rect.bottom,
        }
    }

    unsafe {
        let mut current = GetTopWindow(null_mut());
        while !current.is_null() {
            let window = current;
            current = GetWindow(current, GW_HWNDNEXT);
            if window == overlay
                || window == controller
                || IsWindowVisible(window) == 0
                || IsIconic(window) != 0
                || GetAncestor(window, GA_ROOT) != window
            {
                continue;
            }
            let style = GetWindowLongPtrW(window, GWL_EXSTYLE) as u32;
            if style & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT) != 0 {
                continue;
            }
            let mut cloaked = 0_u32;
            if DwmGetWindowAttribute(
                window,
                DWMWA_CLOAKED as u32,
                (&mut cloaked as *mut u32).cast(),
                size_of::<u32>() as u32,
            ) >= 0
                && cloaked != 0
            {
                continue;
            }
            let mut frame: RECT = zeroed();
            if DwmGetWindowAttribute(
                window,
                DWMWA_EXTENDED_FRAME_BOUNDS as u32,
                (&mut frame as *mut RECT).cast(),
                size_of::<RECT>() as u32,
            ) < 0
                && GetWindowRect(window, &mut frame) == 0
            {
                continue;
            }
            if let Some(frame) = desktop.intersection(bounds(frame)) {
                if frame.right - frame.left >= 50
                    && frame.bottom - frame.top >= 40
                    && (frame.left..frame.right).contains(&point.x)
                    && (frame.top..frame.bottom).contains(&point.y)
                {
                    return frame;
                }
            }
        }
        let monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
        let mut info: MONITORINFO = zeroed();
        info.cbSize = size_of::<MONITORINFO>() as u32;
        if GetMonitorInfoW(monitor, &mut info) != 0 {
            return desktop
                .intersection(bounds(info.rcMonitor))
                .unwrap_or(desktop);
        }
        desktop
    }
}

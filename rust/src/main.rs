#![cfg_attr(windows, windows_subsystem = "windows")]

#[cfg(not(windows))]
fn main() {}

#[cfg(windows)]
fn main() {
    std::process::exit(app::run());
}

#[cfg(windows)]
mod app {
    use std::{
        mem::{size_of, zeroed},
        ptr::{null, null_mut},
        thread,
        time::Duration,
    };

    use screenshot::{
        desktop,
        hotkey::{self, Key},
        image::{Image, Rect},
        overlay, settings,
        win::wide,
    };
    use windows_sys::Win32::{
        Foundation::{
            CloseHandle, ERROR_ALREADY_EXISTS, GetLastError, HANDLE, HWND, LPARAM, LRESULT,
            WAIT_OBJECT_0, WPARAM,
        },
        System::{
            Com::{COINIT_APARTMENTTHREADED, CoInitializeEx, CoUninitialize},
            LibraryLoader::GetModuleHandleW,
            Threading::{CreateEventW, CreateMutexW, WaitForSingleObject},
        },
        UI::{
            HiDpi::{DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, SetProcessDpiAwarenessContext},
            Input::KeyboardAndMouse::{UnregisterHotKey, VK_F24},
            Shell::SetCurrentProcessExplicitAppUserModelID,
            WindowsAndMessaging::*,
        },
    };

    const SETTINGS: u32 = WM_APP + 42;
    const CONTROLLER: &str = "ScreenshotController";
    const QUIT_EVENT: &str = "Local\\Screenshot.SanderStripa.Exit";

    thread_local! {
        static PREVIEW: std::cell::Cell<Option<bool>> = const { std::cell::Cell::new(None) };
    }

    struct Handle(HANDLE);

    impl Drop for Handle {
        fn drop(&mut self) {
            if !self.0.is_null() {
                unsafe {
                    CloseHandle(self.0);
                }
            }
        }
    }

    pub fn run() -> i32 {
        unsafe {
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            if CoInitializeEx(null(), COINIT_APARTMENTTHREADED as u32) < 0 {
                return 2;
            }
            let code = run_initialized();
            CoUninitialize();
            code
        }
    }

    fn run_initialized() -> i32 {
        let args: Vec<_> = std::env::args_os().skip(1).collect();
        let has = |flag: &str| args.iter().any(|arg| arg == flag);
        if has("--hold-install-test") {
            let stop = quit_event();
            if stop.0.is_null() {
                return 18;
            }
            unsafe {
                WaitForSingleObject(stop.0, 30_000);
            }
            return 0;
        }
        if has("--self-test") || has("--self-test-output") {
            return self_test(has("--self-test-output"));
        }

        let capture = has("--capture-now");
        let settings_requested = !capture && !has("--background");
        let preview = has("--settings-preview").then(|| has("light"));
        PREVIEW.set(preview);
        let mutex = Handle(unsafe {
            CreateMutexW(
                null(),
                1,
                wide("Local\\Screenshot.SanderStripa.Singleton").as_ptr(),
            )
        });
        let existing = unsafe { GetLastError() } == ERROR_ALREADY_EXISTS;
        if mutex.0.is_null() {
            return if settings_requested { 19 } else { 0 };
        }
        if existing {
            return if !settings_requested || show_existing() {
                0
            } else {
                19
            };
        }

        let instance = unsafe { GetModuleHandleW(null()) };
        let name = wide(CONTROLLER);
        let class = WNDCLASSEXW {
            cbSize: size_of::<WNDCLASSEXW>() as u32,
            lpfnWndProc: Some(controller_proc),
            hInstance: instance,
            lpszClassName: name.as_ptr(),
            ..unsafe { zeroed() }
        };
        if unsafe { RegisterClassExW(&class) } == 0
            || !overlay::register(instance)
            || !settings::register(instance)
        {
            return 3;
        }
        unsafe {
            SetCurrentProcessExplicitAppUserModelID(wide("SanderStripa.Screenshot").as_ptr());
        }
        let hwnd = unsafe {
            CreateWindowExW(
                WS_EX_TOOLWINDOW,
                name.as_ptr(),
                wide("Screenshot").as_ptr(),
                WS_POPUP,
                0,
                0,
                0,
                0,
                null_mut(),
                null_mut(),
                instance,
                null(),
            )
        };
        if hwnd.is_null() {
            return 3;
        }
        let mut filter = CHANGEFILTERSTRUCT {
            cbSize: size_of::<CHANGEFILTERSTRUCT>() as u32,
            ExtStatus: 0,
        };
        unsafe {
            ChangeWindowMessageFilterEx(hwnd, SETTINGS, MSGFLT_ALLOW, &mut filter);
        }
        let key = if capture {
            Key {
                vk: VK_F24 as u32,
                modifiers: 0,
            }
        } else {
            Key::saved()
        };
        let available = hotkey::initialize(hwnd, key);
        if settings_requested || (!available && !capture) {
            settings::show(instance, preview);
        }

        let stop = quit_event();
        if capture {
            unsafe {
                PostMessageW(hwnd, WM_HOTKEY, hotkey::ID as usize, 0);
            }
        }
        let mut msg: MSG = unsafe { zeroed() };
        'messages: loop {
            unsafe {
                let has_event = !stop.0.is_null();
                let result = MsgWaitForMultipleObjects(
                    has_event as u32,
                    if has_event { &stop.0 } else { null() },
                    0,
                    500,
                    QS_ALLINPUT,
                );
                if has_event && result == WAIT_OBJECT_0 {
                    break;
                }
                while PeekMessageW(&mut msg, null_mut(), 0, 0, PM_REMOVE) != 0 {
                    if msg.message == WM_QUIT {
                        break 'messages;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
        overlay::dismiss();
        settings::close();
        unsafe {
            UnregisterHotKey(hwnd, hotkey::ID);
            if IsWindow(hwnd) != 0 {
                DestroyWindow(hwnd);
            }
        }
        0
    }

    fn quit_event() -> Handle {
        Handle(unsafe { CreateEventW(null(), 1, 0, wide(QUIT_EVENT).as_ptr()) })
    }

    fn show_existing() -> bool {
        for _ in 0..20 {
            unsafe {
                let hwnd = FindWindowW(wide(CONTROLLER).as_ptr(), wide("Screenshot").as_ptr());
                if !hwnd.is_null() {
                    let mut pid = 0;
                    GetWindowThreadProcessId(hwnd, &mut pid);
                    AllowSetForegroundWindow(pid);
                    return PostMessageW(hwnd, SETTINGS, 0, 0) != 0;
                }
            }
            thread::sleep(Duration::from_millis(50));
        }
        false
    }

    unsafe extern "system" fn controller_proc(
        hwnd: HWND,
        message: u32,
        wp: WPARAM,
        lp: LPARAM,
    ) -> LRESULT {
        let instance = unsafe { GetModuleHandleW(null()) };
        match message {
            WM_HOTKEY if wp == hotkey::ID as usize => {
                if !settings::focused() {
                    overlay::begin(instance, hwnd);
                }
            }
            SETTINGS => settings::show(instance, PREVIEW.get()),
            overlay::CAPTURE_FAILED => {
                settings::show(instance, PREVIEW.get());
                settings::capture_notice(wp != 0);
            }
            WM_DESTROY => unsafe {
                PostQuitMessage(0);
            },
            _ => return unsafe { DefWindowProcW(hwnd, message, wp, lp) },
        }
        0
    }

    fn self_test(output: bool) -> i32 {
        let bounds = Rect {
            left: 0,
            top: 0,
            right: 160,
            bottom: 100,
        };
        let Some(mut image) = Image::new(160, 100) else {
            return 10;
        };
        for pixel in image.rgba.chunks_exact_mut(4) {
            pixel.copy_from_slice(&[77, 128, 203, 255]);
        }
        let Some(styled) = image.styled_capture(bounds, bounds) else {
            return 11;
        };
        let Ok(png) = styled.png() else { return 12 };
        if !output {
            return 0;
        }
        let Ok(path) = desktop::save(&png) else {
            return 13;
        };
        let hwnd = unsafe {
            CreateWindowExW(
                0,
                wide("STATIC").as_ptr(),
                wide("Screenshot self-test").as_ptr(),
                WS_POPUP,
                0,
                0,
                0,
                0,
                null_mut(),
                null_mut(),
                GetModuleHandleW(null()),
                null(),
            )
        };
        let copied = !hwnd.is_null() && unsafe { desktop::publish_clipboard(hwnd, &styled, &png) };
        if !hwnd.is_null() {
            unsafe {
                DestroyWindow(hwnd);
            }
        }
        let matches = std::fs::read(&path).is_ok_and(|bytes| bytes == png);
        let _ = std::fs::remove_file(path);
        if copied && matches { 0 } else { 14 }
    }
}

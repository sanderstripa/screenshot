use std::{
    cell::RefCell,
    mem::{size_of, zeroed},
    os::windows::ffi::OsStrExt,
    ptr::{null, null_mut},
    time::Instant,
};

use windows_sys::Win32::{
    Foundation::{HINSTANCE, HWND, LPARAM, LRESULT, POINT, RECT, WPARAM},
    Graphics::{Dwm::*, Gdi::*},
    UI::{HiDpi::*, Input::KeyboardAndMouse::*, Shell::ShellExecuteW, WindowsAndMessaging::*},
};

const WM_MOUSELEAVE: u32 = 0x02a3;
use crate::{
    hotkey::{self, Key},
    settings_draw::Drawing,
    win::{self, CONFIG, KEYBOARD, SNIPPING, wide},
};

const WIDTH: f32 = 460.0;
const HEIGHT: f32 = 636.0;
const SHEET_TIMER: usize = 3;
const RETRY_TIMER: usize = 2;

thread_local! {
    static STATE: RefCell<Option<Settings>> = const { RefCell::new(None) };
}

pub struct Settings {
    hwnd: HWND,
    pub scale: f32,
    pub light: bool,
    pub english: bool,
    pub theme: u32,
    pub saved: Key,
    pending: Key,
    preview: bool,
    pub listening: bool,
    pub language_open: bool,
    pub confirmation: bool,
    pub sheet_progress: f32,
    sheet_tick: Instant,
    pub hovered: i32,
    pub pressed: i32,
    pub focused: i32,
    pub keyboard_focus: bool,
    pub status: String,
    pub notice: String,
    drawing: Option<Drawing>,
    pub backdrop: Option<Backdrop>,
    render_failures: u32,
    position_ready: bool,
}

pub struct Backdrop {
    pub width: u32,
    pub height: u32,
    pub pixels: Vec<u8>,
}

pub fn register(instance: HINSTANCE) -> bool {
    let resource = 101_usize as *const u16;
    let icon = unsafe { LoadImageW(instance, resource, IMAGE_ICON, 256, 256, LR_SHARED) };
    let dpi = unsafe { GetDpiForSystem() };
    let small = unsafe {
        LoadImageW(
            instance,
            resource,
            IMAGE_ICON,
            GetSystemMetricsForDpi(SM_CXSMICON, dpi),
            GetSystemMetricsForDpi(SM_CYSMICON, dpi),
            LR_SHARED,
        )
    };
    let class = WNDCLASSEXW {
        cbSize: size_of::<WNDCLASSEXW>() as u32,
        lpfnWndProc: Some(proc),
        hInstance: instance,
        hIcon: icon,
        hIconSm: small,
        hCursor: unsafe { LoadCursorW(null_mut(), IDC_ARROW) },
        ..unsafe { zeroed() }
    };
    let name = wide("ScreenshotSettings");
    let class = WNDCLASSEXW {
        lpszClassName: name.as_ptr(),
        ..class
    };
    unsafe { RegisterClassExW(&class) != 0 }
}

pub fn show(instance: HINSTANCE, preview: Option<bool>) {
    if let Some(hwnd) = STATE.with_borrow(|state| state.as_ref().map(|s| s.hwnd)) {
        unsafe {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        return;
    }
    let saved = Key::saved();
    let theme = win::read_dword(CONFIG, "ThemeMode").unwrap_or_else(|| {
        if win::read_dword(CONFIG, "ThemeLight").unwrap_or(0) == 0 {
            2
        } else {
            1
        }
    });
    let theme = if theme > 2 { 2 } else { theme };
    let english = win::read_dword(CONFIG, "LanguageEN").unwrap_or(0) != 0;
    let scale = unsafe { GetDpiForSystem() } as f32 / 96.0;
    let (width, height) = dimensions(scale);
    let mut work: RECT = unsafe { zeroed() };
    unsafe {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, (&mut work as *mut RECT).cast(), 0);
    }
    let center = POINT {
        x: work.left + (work.right - work.left - width) / 2,
        y: work.top + (work.bottom - work.top - height) / 2,
    };
    let pos = visible(
        if preview.is_none() {
            win::read_position().unwrap_or(center)
        } else {
            center
        },
        width,
        height,
    );
    let hwnd = unsafe {
        CreateWindowExW(
            WS_EX_APPWINDOW,
            wide("ScreenshotSettings").as_ptr(),
            wide("Screenshot").as_ptr(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            pos.x,
            pos.y,
            width,
            height,
            null_mut(),
            null_mut(),
            instance,
            null(),
        )
    };
    if hwnd.is_null() {
        return;
    }

    let scale = unsafe { GetDpiForWindow(hwnd) } as f32 / 96.0;
    let mut s = Settings {
        hwnd,
        scale,
        theme,
        light: preview.unwrap_or_else(|| light_theme(theme)),
        english,
        saved,
        pending: saved,
        preview: preview.is_some(),
        listening: false,
        language_open: false,
        confirmation: false,
        sheet_progress: 0.0,
        sheet_tick: Instant::now(),
        hovered: -1,
        pressed: -1,
        focused: -1,
        keyboard_focus: false,
        status: String::new(),
        notice: String::new(),
        drawing: None,
        backdrop: None,
        render_failures: 0,
        position_ready: false,
    };
    if !hotkey::available() {
        s.status = s
            .tr(
                "Горячая клавиша недоступна. Назначьте другую.",
                "Shortcut unavailable. Assign another.",
            )
            .into();
    }
    STATE.set(Some(s));
    let (width, height) = dimensions(scale);
    let pos = visible(pos, width, height);
    unsafe {
        SetWindowPos(
            hwnd,
            null_mut(),
            pos.x,
            pos.y,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE,
        );
        let corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(
            hwnd,
            DWMWA_WINDOW_CORNER_PREFERENCE as u32,
            (&corners as *const i32).cast(),
            4,
        );
        let margins = zeroed();
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
    STATE.with_borrow_mut(|state| {
        if let Some(s) = state {
            s.position_ready = true;
            s.apply_theme();
        }
    });
    unsafe {
        SetWindowPos(
            hwnd,
            null_mut(),
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED,
        );
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd);
        UpdateWindow(hwnd);
    }
}

pub fn focused() -> bool {
    STATE.with_borrow(|state| {
        state
            .as_ref()
            .is_some_and(|s| unsafe { GetForegroundWindow() == s.hwnd })
    })
}

pub fn close() {
    if let Some(hwnd) = STATE.with_borrow(|state| state.as_ref().map(|s| s.hwnd)) {
        unsafe {
            DestroyWindow(hwnd);
        }
    }
}

pub fn capture_notice(saved: bool) {
    STATE.with_borrow_mut(|state| {
        if let Some(s) = state {
            s.notice = if saved {
                s.tr(
                    "PNG сохранён, но буфер обмена занят. Повторите захват.",
                    "PNG saved, but the clipboard is busy. Try another capture.",
                )
            } else {
                s.tr(
                    "Не удалось сохранить PNG в Pictures\\Screenshot. Проверьте доступ к папке.",
                    "Could not save PNG to Pictures\\Screenshot. Check folder permissions.",
                )
            }
            .into();
            s.focused = 1;
            s.invalidate();
        }
    });
}

fn dimensions(scale: f32) -> (i32, i32) {
    ((WIDTH * scale) as i32, (HEIGHT * scale) as i32)
}

fn visible(mut point: POINT, width: i32, height: i32) -> POINT {
    unsafe {
        let mut info: MONITORINFO = zeroed();
        info.cbSize = size_of::<MONITORINFO>() as u32;
        if GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &mut info) != 0 {
            let work = info.rcWork;
            point.x = point.x.clamp(work.left, work.left.max(work.right - width));
            point.y = point.y.clamp(work.top, work.top.max(work.bottom - height));
        }
    }
    point
}

fn light_theme(theme: u32) -> bool {
    match theme {
        0 => {
            win::read_dword(
                "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                "AppsUseLightTheme",
            )
            .unwrap_or(1)
                != 0
        }
        1 => true,
        _ => false,
    }
}

fn coords(lp: LPARAM) -> POINT {
    POINT {
        x: lp as i16 as i32,
        y: (lp >> 16) as i16 as i32,
    }
}

enum Action {
    Default,
    Return(LRESULT),
    Close,
    Minimize,
    Resize(i32, i32, i32, i32),
    OpenFolder,
    OpenLink(&'static str),
    ReplaceShortcut(bool),
}

unsafe extern "system" fn proc(hwnd: HWND, message: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    if message == WM_DESTROY {
        STATE.with_borrow_mut(|state| {
            if let Some(s) = state.take() {
                s.save_position();
            }
        });
        return 0;
    }
    let action = STATE
        .try_with(|state| {
            let Ok(mut state) = state.try_borrow_mut() else {
                return Action::Default;
            };
            let Some(s) = state.as_mut().filter(|s| s.hwnd == hwnd) else {
                return if message == WM_NCCALCSIZE && wp != 0 {
                    Action::Return(0)
                } else {
                    Action::Default
                };
            };
            s.handle(message, wp, lp)
        })
        .unwrap_or(Action::Default);

    unsafe {
        match action {
            Action::Default => DefWindowProcW(hwnd, message, wp, lp),
            Action::Return(result) => result,
            Action::Close => {
                DestroyWindow(hwnd);
                0
            }
            Action::Minimize => {
                ShowWindow(hwnd, SW_MINIMIZE);
                0
            }
            Action::Resize(x, y, width, height) => {
                SetWindowPos(
                    hwnd,
                    null_mut(),
                    x,
                    y,
                    width,
                    height,
                    SWP_NOZORDER | SWP_NOACTIVATE,
                );
                0
            }
            Action::OpenFolder => {
                if let Some(profile) = std::env::var_os("USERPROFILE") {
                    let folder = std::path::PathBuf::from(profile)
                        .join("Pictures")
                        .join("Screenshot");
                    let _ = std::fs::create_dir_all(&folder);
                    let path: Vec<_> = folder.as_os_str().encode_wide().chain(Some(0)).collect();
                    ShellExecuteW(
                        hwnd,
                        wide("open").as_ptr(),
                        path.as_ptr(),
                        null(),
                        null(),
                        SW_SHOWNORMAL,
                    );
                }
                0
            }
            Action::OpenLink(url) => {
                ShellExecuteW(
                    hwnd,
                    wide("open").as_ptr(),
                    wide(url).as_ptr(),
                    null(),
                    null(),
                    SW_SHOWNORMAL,
                );
                0
            }
            Action::ReplaceShortcut(confirmed) => {
                change_shortcut(confirmed);
                0
            }
        }
    }
}

impl Settings {
    pub fn tr<'a>(&self, ru: &'a str, en: &'a str) -> &'a str {
        if self.english { en } else { ru }
    }

    fn invalidate(&self) {
        unsafe {
            InvalidateRect(self.hwnd, null(), 0);
        }
    }

    fn apply_theme(&mut self) {
        unsafe {
            let dark = i32::from(!self.light);
            let outline = 0xfffffffe_u32;
            DwmSetWindowAttribute(
                self.hwnd,
                DWMWA_USE_IMMERSIVE_DARK_MODE as u32,
                (&dark as *const i32).cast(),
                4,
            );
            DwmSetWindowAttribute(
                self.hwnd,
                DWMWA_BORDER_COLOR as u32,
                (&outline as *const u32).cast(),
                4,
            );
        }
        self.backdrop = None;
        self.invalidate();
    }

    fn save_position(&self) {
        if self.preview || !self.position_ready {
            return;
        }
        unsafe {
            let mut window = zeroed();
            if IsIconic(self.hwnd) == 0 && GetWindowRect(self.hwnd, &mut window) != 0 {
                win::write_position(POINT {
                    x: window.left,
                    y: window.top,
                });
            }
        }
    }

    fn paint(&mut self) {
        let mut ps: PAINTSTRUCT = unsafe { zeroed() };
        unsafe {
            BeginPaint(self.hwnd, &mut ps);
        }
        if self.drawing.is_none() {
            self.drawing = Drawing::new(self.hwnd).ok();
        }
        if self
            .drawing
            .as_ref()
            .is_some_and(|drawing| drawing.paint(self).is_ok())
        {
            self.render_failures = 0;
            unsafe {
                KillTimer(self.hwnd, RETRY_TIMER);
            }
        } else {
            self.drawing = None;
            self.render_failures += 1;
            unsafe {
                let mut r = zeroed();
                GetClientRect(self.hwnd, &mut r);
                let bg = CreateSolidBrush(if self.light { 0xf5f7f7 } else { 0x0d0c0c });
                FillRect(ps.hdc, &r, bg);
                DeleteObject(bg);
                SetBkMode(ps.hdc, TRANSPARENT as i32);
                SetTextColor(ps.hdc, if self.light { 0x1d1a17 } else { 0xe9e7e6 });
                InflateRect(&mut r, -28, -28);
                DrawTextW(
                    ps.hdc,
                    wide(self.tr(
                        "Не удалось отрисовать настройки. Закройте окно и откройте его снова.",
                        "Could not draw settings. Close this window and open it again.",
                    ))
                    .as_ptr(),
                    -1,
                    &mut r,
                    DT_CENTER | DT_VCENTER | DT_WORDBREAK,
                );
                if self.render_failures <= 3 {
                    SetTimer(self.hwnd, RETRY_TIMER, 250, None);
                }
            }
        }
        unsafe {
            EndPaint(self.hwnd, &ps);
        }
    }

    fn hit(&self, point: POINT) -> i32 {
        let x = point.x as f32 / self.scale;
        let y = point.y as f32 / self.scale;
        let within =
            |left, top, right, bottom| (left..=right).contains(&x) && (top..=bottom).contains(&y);
        if !self.notice.is_empty() {
            return if within(310.0, 248.0, 420.0, 284.0) {
                1
            } else {
                -1
            };
        }
        if self.confirmation || self.sheet_progress > 0.0 {
            return if !self.confirmation || self.sheet_progress < 0.99 {
                -1
            } else if within(154.0, 258.0, 264.0, 294.0) {
                15
            } else if within(276.0, 258.0, 432.0, 294.0) {
                1
            } else {
                -1
            };
        }
        if self.language_open && within(298.0, 458.0, 434.0, 528.0) {
            return if y < 493.0 { 13 } else { 14 };
        }
        if within(28.0, 355.0, 432.0, 395.0) && x < 432.0 {
            return 3 + ((x - 28.0) / (404.0 / 3.0)) as i32;
        }
        for (id, (left, top, right, bottom)) in [
            (11, (414.0, 16.0, 446.0, 48.0)),
            (10, (374.0, 16.0, 406.0, 48.0)),
            (0, (28.0, 110.0, 432.0, 146.0)),
            (8, (28.0, 232.0, 208.0, 268.0)),
            (6, (300.0, 415.0, 432.0, 451.0)),
            (9, (320.0, 580.0, 432.0, 612.0)),
            (12, (165.0, 613.0, 295.0, 633.0)),
        ] {
            if within(left, top, right, bottom) {
                return id;
            }
        }
        -1
    }

    fn activate(&mut self, id: i32) -> Action {
        if !self.notice.is_empty() {
            if id == 1 {
                self.notice.clear();
            }
        } else if self.confirmation {
            if id == 1 {
                return Action::ReplaceShortcut(true);
            }
            if id == 15 {
                self.cancel();
            }
        } else if self.sheet_progress == 0.0 {
            self.focused = id;
            self.listening = false;
            match id {
                11 => return Action::Close,
                10 => return Action::Minimize,
                3..=5 => {
                    self.language_open = false;
                    let next = (id - 3) as u32;
                    let light = light_theme(next);
                    if self.preview
                        || (win::write_dword(CONFIG, "ThemeMode", Some(next))
                            && win::write_dword(CONFIG, "ThemeLight", Some(light as u32)))
                    {
                        self.theme = next;
                        self.light = light;
                        self.status.clear();
                        self.apply_theme();
                    } else {
                        win::write_dword(CONFIG, "ThemeMode", Some(self.theme));
                        win::write_dword(CONFIG, "ThemeLight", Some(self.light as u32));
                        self.status = self
                            .tr("Не удалось сохранить тему.", "Could not save theme.")
                            .into();
                    }
                }
                6 => {
                    self.language_open = !self.language_open;
                    self.focused = if self.english { 14 } else { 13 };
                }
                13 | 14 => {
                    if self.preview
                        || win::write_dword(CONFIG, "LanguageEN", Some((id == 14) as u32))
                    {
                        self.english = id == 14;
                        self.status.clear();
                    } else {
                        self.status = self
                            .tr("Не удалось сохранить язык.", "Could not save language.")
                            .into();
                    }
                    self.language_open = false;
                    self.focused = 6;
                }
                _ => {
                    self.language_open = false;
                    match id {
                        0 => {
                            self.listening = true;
                            self.status.clear();
                        }
                        8 => {
                            self.invalidate();
                            return Action::OpenFolder;
                        }
                        9 => {
                            self.invalidate();
                            return Action::OpenLink(
                                "https://github.com/sanderstripa/screenshot/releases",
                            );
                        }
                        12 => {
                            self.invalidate();
                            return Action::OpenLink("https://connect.sanderstripa.com/");
                        }
                        _ => {}
                    }
                }
            }
        }
        self.invalidate();
        Action::Return(0)
    }

    fn set_sheet(&mut self, open: bool) {
        self.language_open = false;
        self.listening = false;
        if open {
            self.backdrop = Backdrop::capture(self.hwnd, self.scale, !self.light);
        }
        self.confirmation = open;
        self.sheet_tick = Instant::now();
        let mut animate: i32 = 1;
        unsafe {
            SystemParametersInfoW(
                SPI_GETCLIENTAREAANIMATION,
                0,
                (&mut animate as *mut i32).cast(),
                0,
            );
            if animate != 0 {
                SetTimer(self.hwnd, SHEET_TIMER, 16, None);
            } else {
                self.sheet_progress = if open { 1.0 } else { 0.0 };
            }
        }
        self.focused = if open { 15 } else { 0 };
        self.invalidate();
    }

    fn cancel(&mut self) {
        self.pending = self.saved;
        self.set_sheet(false);
    }

    fn key(&mut self, key: u16) -> Action {
        if key == VK_ESCAPE {
            self.listening = false;
            self.status.clear();
            self.invalidate();
            return Action::Return(0);
        }
        if matches!(key, VK_SHIFT | VK_CONTROL | VK_MENU | VK_LWIN | VK_RWIN) {
            return Action::Return(0);
        }
        let mut modifiers = 0;
        for (vk, flag) in [
            (VK_CONTROL, MOD_CONTROL),
            (VK_SHIFT, MOD_SHIFT),
            (VK_MENU, MOD_ALT),
            (VK_LWIN, MOD_WIN),
            (VK_RWIN, MOD_WIN),
        ] {
            if unsafe { GetKeyState(vk as i32) } < 0 {
                modifiers |= flag;
            }
        }
        let special = (VK_F1..=VK_F24).contains(&key)
            || matches!(
                key,
                VK_SNAPSHOT | VK_PAUSE | VK_SCROLL | VK_INSERT | VK_HOME
            );
        if !special && (modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN) == 0 || key < VK_SPACE) {
            unsafe {
                windows_sys::Win32::System::Diagnostics::Debug::MessageBeep(MB_ICONWARNING);
            }
            return Action::Return(0);
        }
        self.pending = Key {
            vk: key as u32,
            modifiers,
        };
        self.listening = false;
        self.status.clear();
        Action::ReplaceShortcut(false)
    }

    fn tab(&mut self) {
        self.listening = false;
        self.keyboard_focus = true;
        let order: &[i32] = if !self.notice.is_empty() {
            &[1]
        } else if self.confirmation {
            &[15, 1]
        } else if self.language_open {
            &[13, 14]
        } else {
            &[0, 8, 3, 4, 5, 6, 9, 12, 10, 11]
        };
        let backwards = unsafe { GetKeyState(VK_SHIFT as i32) } < 0;
        let index = order
            .iter()
            .position(|&id| id == self.focused)
            .map_or(order.len() - 1, |index| index);
        self.focused = order[(index + if backwards { order.len() - 1 } else { 1 }) % order.len()];
        self.invalidate();
    }

    fn handle(&mut self, message: u32, wp: WPARAM, lp: LPARAM) -> Action {
        let key = wp as u16;
        match message {
            WM_SYSCOMMAND if wp & 0xfff0 == SC_MAXIMIZE as usize => {}
            WM_NCLBUTTONDBLCLK if wp == HTCAPTION as usize => {}
            WM_NCCALCSIZE if wp != 0 => {}
            WM_NCHITTEST => {
                let mut p = coords(lp);
                unsafe {
                    ScreenToClient(self.hwnd, &mut p);
                }
                let title = !self.confirmation
                    && self.sheet_progress == 0.0
                    && p.y as f32 / self.scale < 62.0
                    && p.x as f32 / self.scale < 365.0;
                return Action::Return(if title {
                    HTCAPTION as isize
                } else {
                    HTCLIENT as isize
                });
            }
            WM_SETTINGCHANGE => {
                if self.theme == 0 {
                    self.light = light_theme(0);
                    self.apply_theme();
                }
            }
            WM_SETCURSOR if lp as u16 == HTCLIENT as u16 => {
                let mut p = POINT { x: 0, y: 0 };
                unsafe {
                    GetCursorPos(&mut p);
                    ScreenToClient(self.hwnd, &mut p);
                    SetCursor(LoadCursorW(
                        null_mut(),
                        if self.hit(p) == 12 {
                            IDC_HAND
                        } else {
                            IDC_ARROW
                        },
                    ));
                }
                return Action::Return(1);
            }
            WM_MOUSEMOVE => {
                let hover = self.hit(coords(lp));
                if hover != self.hovered {
                    self.hovered = hover;
                    self.invalidate();
                }
                let mut track = TRACKMOUSEEVENT {
                    cbSize: size_of::<TRACKMOUSEEVENT>() as u32,
                    dwFlags: TME_LEAVE,
                    hwndTrack: self.hwnd,
                    dwHoverTime: 0,
                };
                unsafe {
                    TrackMouseEvent(&mut track);
                }
            }
            WM_MOUSELEAVE => {
                self.hovered = -1;
                self.invalidate();
            }
            WM_LBUTTONDOWN => {
                self.pressed = self.hit(coords(lp));
                unsafe {
                    SetFocus(self.hwnd);
                }
                self.invalidate();
            }
            WM_LBUTTONUP => {
                self.pressed = -1;
                self.keyboard_focus = false;
                return self.activate(self.hit(coords(lp)));
            }
            WM_ERASEBKGND => return Action::Return(1),
            WM_PAINT => self.paint(),
            WM_TIMER if wp == SHEET_TIMER => {
                let step = self.sheet_tick.elapsed().as_secs_f32() / 0.220;
                self.sheet_tick = Instant::now();
                self.sheet_progress = (self.sheet_progress
                    + if self.confirmation { step } else { -step })
                .clamp(0.0, 1.0);
                if self.sheet_progress == if self.confirmation { 1.0 } else { 0.0 } {
                    unsafe {
                        KillTimer(self.hwnd, SHEET_TIMER);
                    }
                    if !self.confirmation {
                        self.backdrop = None;
                    }
                }
                self.invalidate();
            }
            WM_TIMER if wp == RETRY_TIMER => {
                unsafe {
                    KillTimer(self.hwnd, RETRY_TIMER);
                }
                self.invalidate();
            }
            WM_DISPLAYCHANGE | WM_SIZE => {
                self.drawing = None;
                self.backdrop = None;
                self.invalidate();
            }
            WM_WINDOWPOSCHANGED => {
                self.save_position();
                return Action::Default;
            }
            WM_QUERYENDSESSION => {
                self.save_position();
                return Action::Return(1);
            }
            WM_DPICHANGED => {
                self.scale = ((wp >> 16) as u16) as f32 / 96.0;
                let (width, height) = dimensions(self.scale);
                let r = unsafe { &*(lp as *const RECT) };
                return Action::Resize(r.left, r.top, width, height);
            }
            WM_KEYDOWN | WM_SYSKEYDOWN => {
                if message == WM_SYSKEYDOWN && key == VK_F4 {
                    return Action::Default;
                }
                if key == VK_TAB {
                    self.tab();
                } else if self.listening {
                    return self.key(key);
                } else if key == VK_ESCAPE {
                    if !self.notice.is_empty() {
                        self.notice.clear();
                    } else if self.confirmation {
                        self.cancel();
                    } else if self.language_open {
                        self.language_open = false;
                    } else {
                        return Action::Close;
                    }
                    self.invalidate();
                } else if matches!(key, VK_RETURN | VK_SPACE) && self.focused >= 0 {
                    return self.activate(self.focused);
                }
            }
            WM_KEYUP | WM_SYSKEYUP => {
                if self.listening && key == VK_SNAPSHOT {
                    return self.key(VK_SNAPSHOT);
                }
            }
            _ => return Action::Default,
        }
        Action::Return(0)
    }
}

fn change_shortcut(confirmed: bool) {
    let snipping = win::read_dword(KEYBOARD, SNIPPING);
    let change = STATE.with_borrow_mut(|state| {
        let s = state.as_mut()?;
        if s.preview {
            s.pending = s.saved;
            s.status = s.tr("Предпросмотр интерфейса", "Interface preview").into();
            s.invalidate();
            return None;
        }
        if s.pending.is_print_screen()
            && !confirmed
            && (snipping != Some(0) || !hotkey::available() || s.pending != s.saved)
        {
            s.set_sheet(true);
            return None;
        }
        Some((s.saved, s.pending))
    });
    let Some((old, next)) = change else { return };
    let notify = || unsafe {
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            wide(KEYBOARD).as_ptr() as LPARAM,
            SMTO_ABORTIFHUNG,
            300,
            null_mut(),
        );
    };
    let changed = confirmed && win::write_dword(KEYBOARD, SNIPPING, Some(0));
    if changed {
        notify();
    }
    let failure = if confirmed && !changed {
        1
    } else if !hotkey::replace(next) {
        2
    } else if !next.persist() {
        old.persist();
        hotkey::replace(old);
        3
    } else {
        0
    };
    if changed && failure != 0 {
        win::write_dword(KEYBOARD, SNIPPING, snipping);
        notify();
    }
    STATE.with_borrow_mut(|state| if let Some(s) = state {
        match failure {
            0 => { s.saved = next; s.status.clear(); }
            1 => s.status = s.tr("Windows не разрешила изменить Print Screen.", "Windows could not change Print Screen.").into(),
            2 => s.status = if confirmed {
                s.tr("Windows пока не разрешила перехват. Прежняя клавиша сохранена; может потребоваться выход из сеанса.",
                    "Windows has not allowed capture. Previous shortcut retained; signing out may be required.")
            } else { s.tr("Это сочетание занято. Прежняя клавиша сохранена.", "Shortcut is in use. Previous shortcut retained.") }.into(),
            _ => s.status = s.tr("Не удалось сохранить настройки. Прежняя клавиша восстановлена.",
                "Could not save settings. Previous shortcut restored.").into(),
        }
        s.pending = s.saved;
        if s.confirmation { s.set_sheet(false); }
        s.invalidate();
    });
}

impl Backdrop {
    fn capture(hwnd: HWND, scale: f32, dark: bool) -> Option<Self> {
        let (width, height, mut pixels) = unsafe {
            let mut r: RECT = zeroed();
            if GetClientRect(hwnd, &mut r) == 0 || r.right <= 0 || r.bottom <= 0 {
                return None;
            }
            let (width, height) = (r.right, r.bottom);
            let dc = GetDC(hwnd);
            if dc.is_null() {
                return None;
            }
            let memory = CreateCompatibleDC(dc);
            let mut info: BITMAPINFO = zeroed();
            info.bmiHeader = BITMAPINFOHEADER {
                biSize: size_of::<BITMAPINFOHEADER>() as u32,
                biWidth: width,
                biHeight: -height,
                biPlanes: 1,
                biBitCount: 32,
                biCompression: BI_RGB,
                ..zeroed()
            };
            let mut data = null_mut();
            let dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &mut data, null_mut(), 0);
            let pixels = if !memory.is_null() && !dib.is_null() && !data.is_null() {
                let old = SelectObject(memory, dib);
                let result = if BitBlt(memory, 0, 0, width, height, dc, 0, 0, SRCCOPY) != 0 {
                    GdiFlush();
                    Some(
                        std::slice::from_raw_parts(
                            data.cast::<u8>(),
                            (width * height * 4) as usize,
                        )
                        .to_vec(),
                    )
                } else {
                    None
                };
                SelectObject(memory, old);
                result
            } else {
                None
            };
            if !dib.is_null() {
                DeleteObject(dib);
            }
            if !memory.is_null() {
                DeleteDC(memory);
            }
            ReleaseDC(hwnd, dc);
            (width, height, pixels?)
        };
        let radius = ((7.0 * scale) as i32).max(3);
        let mut temp = vec![0; pixels.len()];
        for _ in 0..3 {
            for vertical in [false, true] {
                let (lines, length) = if vertical {
                    (width, height)
                } else {
                    (height, width)
                };
                for line in 0..lines {
                    for channel in 0..3 {
                        let at = |n: i32| {
                            let n = n.clamp(0, length - 1);
                            let (x, y) = if vertical { (line, n) } else { (n, line) };
                            ((y * width + x) * 4 + channel) as usize
                        };
                        let mut sum: i32 = (-radius..=radius).map(|k| pixels[at(k)] as i32).sum();
                        for n in 0..length {
                            temp[at(n)] = (sum / (2 * radius + 1)) as u8;
                            sum +=
                                pixels[at(n + radius + 1)] as i32 - pixels[at(n - radius)] as i32;
                        }
                    }
                }
                std::mem::swap(&mut pixels, &mut temp);
            }
        }
        for pixel in pixels.chunks_exact_mut(4) {
            for channel in &mut pixel[..3] {
                *channel = if dark {
                    *channel as f32 * 0.55 + 13.0 * 0.45
                } else {
                    *channel as f32 * 0.48 + 248.0 * 0.52
                } as u8;
            }
            pixel[3] = 255;
        }
        Some(Self {
            width: width as u32,
            height: height as u32,
            pixels,
        })
    }
}

#![windows_subsystem = "windows"]

mod draw;
mod install;

use draw::{Drawing, Rect, contains, rect};
use install::{Installer, PROGRESS, VERSION, wide};
use std::{
    cell::RefCell,
    mem::{size_of, zeroed},
    ptr::{null, null_mut},
    sync::{Arc, Mutex, atomic::Ordering, mpsc},
};
use windows::Win32::{
    Graphics::DirectWrite::{
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_TEXT_ALIGNMENT_TRAILING,
    },
    System::Com::*,
    UI::Shell::*,
};
use windows_sys::Win32::{
    Foundation::*,
    Graphics::{Dwm::*, Gdi::*},
    System::{LibraryLoader::GetModuleHandleW, Threading::CreateMutexW},
    UI::{HiDpi::*, Input::KeyboardAndMouse::*, WindowsAndMessaging::*},
};

const WIDTH: f32 = 854.0;
const HEIGHT: f32 = 577.0;
const TIMER: usize = 20;
const WM_MOUSELEAVE: u32 = 0x02a3;

#[derive(Clone, Copy, PartialEq)]
enum Stage {
    Welcome,
    Settings,
    Installing,
    Hotkey,
    Confirm,
    Finish,
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(usize)]
enum Id {
    Close,
    Minimize,
    Primary,
    Settings,
    Back,
    Ru,
    En,
    Light,
    Dark,
    Path,
    Browse,
    Desktop,
    Menu,
    Accept,
    Key,
    Next,
    Cancel,
    Replace,
    Launch,
    Done,
    Retry,
}

#[derive(Clone, Copy)]
enum Kind {
    Primary,
    Text,
    Outline,
    Choice(bool),
    Toggle(bool),
    Caption,
    Edit,
}
struct Control {
    id: Id,
    area: Rect,
    label: String,
    kind: Kind,
}

struct Palette {
    bg: u32,
    ink: u32,
    muted: u32,
    border: u32,
    button: u32,
    inverse: u32,
    field: u32,
    hover: u32,
}
impl Palette {
    fn new(dark: bool) -> Self {
        if dark {
            Self {
                bg: 0x08090b,
                ink: 0xe6e7e9,
                muted: 0x9a9da3,
                border: 0x35363b,
                button: 0xe3e4df,
                inverse: 0x08090b,
                field: 0x28292d,
                hover: 0x303136,
            }
        } else {
            Self {
                bg: 0xfcfcfd,
                ink: 0x242628,
                muted: 0x73767b,
                border: 0xe6e7e9,
                button: 0x0671ff,
                inverse: 0xffffff,
                field: 0xf3f4f5,
                hover: 0xededef,
            }
        }
    }
}
enum Job {
    Install,
    Remove,
    Launch,
}
type JobResult = Result<(), String>;

struct Setup {
    hwnd: HWND,
    edit: HWND,
    font: HFONT,
    edit_brush: HBRUSH,
    scale: f32,
    drawing: Option<Drawing>,
    backend: Arc<Mutex<Installer>>,
    uninstall: bool,
    english: bool,
    dark: bool,
    desktop: bool,
    menu: bool,
    launch: bool,
    stage: Stage,
    path: String,
    key: u32,
    modifiers: u32,
    key_name: String,
    listening: bool,
    relogin: bool,
    focus: Option<Id>,
    keyboard: bool,
    hover: Option<Id>,
    pressed: Option<Id>,
    error: String,
    job: Option<(Job, mpsc::Receiver<JobResult>)>,
}

thread_local! { static STATE: RefCell<Option<Setup>> = const { RefCell::new(None) }; }

fn main() {
    if let Err(error) = run() {
        unsafe {
            MessageBoxW(
                null_mut(),
                wide(error).as_ptr(),
                wide("Screenshot").as_ptr(),
                MB_ICONERROR,
            );
        }
    }
}

fn run() -> Result<(), String> {
    unsafe {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    let lock = unsafe {
        CreateMutexW(
            null(),
            0,
            wide(r"Local\com.sanderstripa.installer.screenshot").as_ptr(),
        )
    };
    if lock.is_null() {
        return Err(std::io::Error::last_os_error().to_string());
    }
    if unsafe { GetLastError() } == ERROR_ALREADY_EXISTS {
        unsafe {
            CloseHandle(lock);
        }
        return Ok(());
    }
    let result = init();
    unsafe {
        CloseHandle(lock);
    }
    result
}

fn init() -> Result<(), String> {
    unsafe {
        CoInitializeEx(None, COINIT_APARTMENTTHREADED)
            .ok()
            .map_err(|e| e.to_string())?;
    }
    let result = window();
    unsafe {
        CoUninitialize();
    }
    result
}

fn window() -> Result<(), String> {
    let uninstall = std::env::args_os().any(|arg| arg == "--uninstall");
    let backend = Installer::new(uninstall);
    let path = backend.directory.to_string_lossy().into_owned();
    unsafe {
        let instance = GetModuleHandleW(null());
        let name = wide("ScreenshotNativeSetup");
        let icon = LoadImageW(
            instance,
            101_usize as *const u16,
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE | LR_SHARED,
        );
        let class = WNDCLASSEXW {
            cbSize: size_of::<WNDCLASSEXW>() as u32,
            lpfnWndProc: Some(window_proc),
            hInstance: instance,
            lpszClassName: name.as_ptr(),
            hCursor: LoadCursorW(null_mut(), IDC_ARROW),
            hIcon: icon,
            hIconSm: icon,
            ..zeroed()
        };
        if RegisterClassExW(&class) == 0 {
            return Err(std::io::Error::last_os_error().to_string());
        }
        let scale = GetDpiForSystem() as f32 / 96.0;
        let w = (WIDTH * scale).round() as i32;
        let h = (HEIGHT * scale).round() as i32;
        let mut work: RECT = zeroed();
        SystemParametersInfoW(SPI_GETWORKAREA, 0, (&mut work as *mut RECT).cast(), 0);
        let hwnd = CreateWindowExW(
            WS_EX_APPWINDOW,
            name.as_ptr(),
            wide("Screenshot").as_ptr(),
            WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
            work.left + (work.right - work.left - w) / 2,
            work.top + (work.bottom - work.top - h) / 2,
            w,
            h,
            null_mut(),
            null_mut(),
            instance,
            null(),
        );
        if hwnd.is_null() {
            return Err(std::io::Error::last_os_error().to_string());
        }
        let edit = CreateWindowExW(
            0,
            wide("EDIT").as_ptr(),
            wide(&path).as_ptr(),
            WS_CHILD | ES_AUTOHSCROLL as u32,
            0,
            0,
            0,
            0,
            hwnd,
            null_mut(),
            instance,
            null(),
        );
        if edit.is_null() {
            return Err(std::io::Error::last_os_error().to_string());
        }
        let font_data = include_bytes!("../../../assets/fonts/Montserrat.ttf");
        let mut fonts = 0;
        let font_resource = AddFontMemResourceEx(
            font_data.as_ptr() as *mut _,
            font_data.len() as u32,
            null_mut(),
            &mut fonts,
        );
        let drawing = Drawing::new(hwnd).map_err(|e| e.to_string())?;
        let mut setup = Setup {
            hwnd,
            edit,
            font: null_mut(),
            edit_brush: null_mut(),
            scale,
            drawing: Some(drawing),
            backend: Arc::new(Mutex::new(backend)),
            uninstall,
            english: false,
            dark: false,
            desktop: true,
            menu: true,
            launch: true,
            stage: Stage::Welcome,
            path,
            key: 44,
            modifiers: 0,
            key_name: "Print Screen".into(),
            listening: false,
            relogin: false,
            focus: None,
            keyboard: false,
            hover: None,
            pressed: None,
            error: String::new(),
            job: None,
        };
        setup.theme();
        STATE.set(Some(setup));
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        let mut msg: MSG = zeroed();
        while GetMessageW(&mut msg, null_mut(), 0, 0) > 0 {
            if msg.hwnd == edit
                && matches!(msg.message, WM_KEYDOWN | WM_SYSKEYDOWN)
                && matches!(msg.wParam as u16, VK_TAB | VK_ESCAPE | VK_RETURN)
            {
                SendMessageW(hwnd, msg.message, msg.wParam, msg.lParam);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if let Some(state) = STATE.take() {
            DeleteObject(state.font);
            DeleteObject(state.edit_brush);
            if !font_resource.is_null() {
                RemoveFontMemResourceEx(font_resource);
            }
            if state.backend.lock().is_ok_and(|backend| backend.removed) {
                state
                    .backend
                    .lock()
                    .map_err(|e| e.to_string())?
                    .cleanup()
                    .map_err(|e| e.to_string())?;
            }
        }
    }
    Ok(())
}

impl Setup {
    fn tr<'a>(&self, ru: &'a str, en: &'a str) -> &'a str {
        if self.english { en } else { ru }
    }

    fn controls(&self) -> Vec<Control> {
        let mut controls = Vec::new();
        let mut add = |id, area, label: &str, kind| {
            controls.push(Control {
                id,
                area,
                label: label.into(),
                kind,
            })
        };
        let tr = |ru, en| self.tr(ru, en);
        match self.stage {
            Stage::Welcome => {
                let y = if self.uninstall { 324.0 } else { 306.0 };
                add(
                    Id::Primary,
                    rect(45.0, y, 228.0, 49.0),
                    if self.uninstall {
                        tr("Удалить", "Uninstall")
                    } else {
                        tr("Установить", "Install")
                    },
                    Kind::Primary,
                );
                add(
                    Id::Settings,
                    rect(45.0, y + 68.0, 185.0, 20.0),
                    tr("Настройки установки", "Installation settings"),
                    Kind::Text,
                );
            }
            Stage::Settings => {
                add(
                    Id::Back,
                    rect(777.0, 69.0, 60.0, 24.0),
                    tr("Назад", "Back"),
                    Kind::Text,
                );
                let x = if self.english { 123.0 } else { 95.0 };
                add(
                    Id::Ru,
                    rect(x, 116.0, 37.0, 28.0),
                    "RU",
                    Kind::Choice(!self.english),
                );
                add(
                    Id::En,
                    rect(x + 40.0, 116.0, 36.0, 28.0),
                    "EN",
                    Kind::Choice(self.english),
                );
                let x = if self.english { 340.0 } else { 310.0 };
                add(
                    Id::Light,
                    rect(x, 116.0, if self.english { 48.0 } else { 69.0 }, 28.0),
                    tr("Светлая", "Light"),
                    Kind::Choice(!self.dark),
                );
                add(
                    Id::Dark,
                    rect(
                        x + if self.english { 50.0 } else { 72.0 },
                        116.0,
                        if self.english { 48.0 } else { 67.0 },
                        28.0,
                    ),
                    tr("Тёмная", "Dark"),
                    Kind::Choice(self.dark),
                );
                if !self.uninstall {
                    let bw = if self.english { 82.0 } else { 91.0 };
                    add(
                        Id::Path,
                        rect(45.0, 185.0, 764.0 - bw - 10.0, 35.0),
                        "",
                        Kind::Edit,
                    );
                    add(
                        Id::Browse,
                        rect(809.0 - bw, 185.0, bw, 35.0),
                        tr("Выбрать…", "Browse…"),
                        Kind::Outline,
                    );
                    add(
                        Id::Desktop,
                        rect(45.0, 233.0, 764.0, 38.0),
                        tr("Ярлык на рабочем столе", "Desktop shortcut"),
                        Kind::Toggle(self.desktop),
                    );
                    add(
                        Id::Menu,
                        rect(45.0, 271.0, 764.0, 38.0),
                        tr("Ярлык в меню «Пуск»", "Start menu shortcut"),
                        Kind::Toggle(self.menu),
                    );
                }
                add(
                    Id::Accept,
                    rect(
                        45.0,
                        if self.uninstall { 241.0 } else { 387.0 },
                        260.0,
                        49.0,
                    ),
                    if self.uninstall {
                        tr("Удалить", "Uninstall")
                    } else {
                        tr("Принять и установить", "Accept and install")
                    },
                    Kind::Primary,
                );
            }
            Stage::Hotkey => {
                add(
                    Id::Key,
                    rect(45.0, 209.0, 260.0, 49.0),
                    if self.listening {
                        tr("Нажмите клавишу…", "Press a key…")
                    } else {
                        &self.key_name
                    },
                    Kind::Outline,
                );
                add(
                    Id::Next,
                    rect(45.0, 286.0, 228.0, 49.0),
                    tr("Далее", "Next"),
                    Kind::Primary,
                );
            }
            Stage::Confirm => {
                let w = if self.english { 70.0 } else { 82.0 };
                add(
                    Id::Cancel,
                    rect(45.0, 251.0, w, 34.0),
                    tr("Отмена", "Cancel"),
                    Kind::Outline,
                );
                add(
                    Id::Replace,
                    rect(50.0 + w, 242.0, 228.0, 49.0),
                    tr("Да, заменить", "Yes, replace"),
                    Kind::Primary,
                );
            }
            Stage::Finish => {
                let y = if self.uninstall {
                    320.0
                } else if self.relogin {
                    303.0
                } else {
                    292.0
                };
                add(
                    Id::Done,
                    rect(313.0, y, 228.0, 49.0),
                    tr("Готово", "Done"),
                    Kind::Primary,
                );
                if !self.uninstall {
                    let w = if self.english { 250.0 } else { 268.0 };
                    add(
                        Id::Launch,
                        rect((WIDTH - w) / 2.0, y + 64.0, w, 38.0),
                        tr(
                            "Запустить Screenshot после установки",
                            "Launch Screenshot after installation",
                        ),
                        Kind::Toggle(self.launch),
                    );
                }
            }
            Stage::Installing => {}
        }
        if !self.error.is_empty() {
            add(
                Id::Retry,
                rect(45.0, 490.0, 100.0, 22.0),
                tr("Повторить", "Retry"),
                Kind::Text,
            );
        }
        add(
            Id::Minimize,
            rect(740.0, 21.0, 28.0, 30.0),
            "",
            Kind::Caption,
        );
        add(Id::Close, rect(781.0, 21.0, 28.0, 30.0), "", Kind::Caption);
        controls
    }

    fn paint_scene(&self, d: &Drawing) {
        use DWRITE_TEXT_ALIGNMENT_CENTER as C;
        use DWRITE_TEXT_ALIGNMENT_LEADING as L;
        let p = Palette::new(self.dark);
        d.begin(self.scale, p.bg);
        let tr = |ru, en| self.tr(ru, en);
        let text = |value, area, size, weight, ink, align, spacing| {
            d.text(value, area, size, weight, ink, align, spacing)
        };
        d.round(rect(0.5, 0.5, 853.0, 576.0), 0.0, None, Some(p.border));
        d.line(45.0, 527.5, 809.0, 527.5, 1.0, p.border);
        text(
            "by CONNECT.",
            rect(45.0, 528.0, 300.0, 48.0),
            10.0,
            400,
            p.muted,
            L,
            1.0,
        );
        let version = format!("{} {VERSION}", tr("Версия", "Version"));
        text(
            &version,
            rect(550.0, 528.0, 259.0, 48.0),
            10.0,
            400,
            p.muted,
            DWRITE_TEXT_ALIGNMENT_TRAILING,
            1.0,
        );
        match self.stage {
            Stage::Welcome => {
                d.ribbon(rect(391.0, 61.0, 440.0, 365.0));
                let y = if self.uninstall { 177.0 } else { 192.0 };
                text(
                    if self.uninstall {
                        tr("УДАЛЕНИЕ", "UNINSTALLATION")
                    } else {
                        tr("УСТАНОВКА", "INSTALLATION")
                    },
                    rect(45.0, y, 315.0, 17.0),
                    12.0,
                    600,
                    p.ink,
                    L,
                    4.0,
                );
                text(
                    "Screenshot",
                    rect(45.0, y + 30.0, 365.0, 52.0),
                    42.0,
                    500,
                    p.ink,
                    L,
                    -1.8,
                );
                if self.uninstall {
                    text(
                        tr(
                            "Ваши настройки и пользовательские данные сохранятся. Закройте приложение перед удалением.",
                            "Your settings and user data will be kept. Close the application before uninstalling.",
                        ),
                        rect(45.0, 274.0, 310.0, 40.0),
                        12.0,
                        400,
                        p.muted,
                        L,
                        0.0,
                    );
                }
            }
            Stage::Settings => {
                text(
                    tr("Настройки установки", "Installation settings"),
                    rect(45.0, 66.0, 485.0, 29.0),
                    23.0,
                    500,
                    p.ink,
                    L,
                    0.0,
                );
                text(
                    tr("Язык", "Language"),
                    rect(45.0, 116.0, 75.0, 28.0),
                    12.0,
                    600,
                    p.ink,
                    L,
                    0.0,
                );
                text(
                    tr("Тема", "Theme"),
                    rect(if self.english { 279.0 } else { 255.0 }, 116.0, 55.0, 28.0),
                    12.0,
                    600,
                    p.ink,
                    L,
                    0.0,
                );
                if !self.uninstall {
                    text(
                        tr("Папка установки", "Installation folder"),
                        rect(45.0, 161.0, 350.0, 17.0),
                        12.0,
                        400,
                        p.ink,
                        L,
                        0.0,
                    );
                }
            }
            Stage::Installing => {
                d.ribbon(rect(192.0, 69.0, 470.0, 305.0));
                let progress = PROGRESS.load(Ordering::Relaxed).min(99) as f32 / 100.0;
                d.round(rect(45.0, 472.0, 764.0, 4.0), 2.0, Some(p.border), None);
                d.round(
                    rect(45.0, 472.0, 764.0 * progress, 4.0),
                    2.0,
                    Some(p.button),
                    None,
                );
                text(
                    if self.uninstall {
                        tr("Удаление…", "Uninstalling…")
                    } else {
                        tr("Установка…", "Installing…")
                    },
                    rect(45.0, 488.0, 764.0, 15.0),
                    11.0,
                    400,
                    p.muted,
                    C,
                    0.0,
                );
            }
            Stage::Hotkey => {
                text(
                    tr("Горячая клавиша", "Keyboard shortcut"),
                    rect(45.0, 116.0, 600.0, 29.0),
                    23.0,
                    500,
                    p.ink,
                    L,
                    0.0,
                );
                text(
                    tr(
                        "Нажмите поле, затем клавишу для создания скриншота.",
                        "Click the field, then press a key to capture screenshots.",
                    ),
                    rect(45.0, 169.0, 600.0, 19.0),
                    12.0,
                    400,
                    p.muted,
                    L,
                    0.0,
                );
            }
            Stage::Confirm => {
                text(
                    "PRINT SCREEN",
                    rect(45.0, 116.0, 600.0, 15.0),
                    12.0,
                    600,
                    p.ink,
                    L,
                    4.0,
                );
                text(
                    tr(
                        "Заменить системное действие?",
                        "Replace the Windows action?",
                    ),
                    rect(45.0, 146.0, 764.0, 29.0),
                    23.0,
                    500,
                    p.ink,
                    L,
                    0.0,
                );
                text(
                    tr(
                        "Назначить Print Screen приложению Screenshot вместо стандартного инструмента Windows?",
                        "Assign Print Screen to Screenshot instead of the default Windows capture tool?",
                    ),
                    rect(45.0, 195.0, 600.0, 20.0),
                    12.0,
                    400,
                    p.muted,
                    L,
                    0.0,
                );
            }
            Stage::Finish => {
                let y = if self.uninstall {
                    215.0
                } else if self.relogin {
                    177.0
                } else {
                    188.0
                };
                text(
                    "Screenshot",
                    rect(45.0, y, 764.0, 52.0),
                    42.0,
                    500,
                    p.ink,
                    C,
                    -1.8,
                );
                text(
                    if self.uninstall {
                        tr("Screenshot удалён", "Screenshot uninstalled")
                    } else {
                        tr("Установка завершена", "Installation complete")
                    },
                    rect(45.0, y + 65.0, 764.0, 20.0),
                    14.0,
                    400,
                    p.ink,
                    C,
                    0.0,
                );
                if self.relogin {
                    text(
                        tr(
                            "Для активации Print Screen может потребоваться повторный вход в Windows.",
                            "Sign out and back in to activate Print Screen if needed.",
                        ),
                        rect(45.0, 283.0, 764.0, 20.0),
                        10.0,
                        400,
                        p.muted,
                        C,
                        0.0,
                    );
                }
            }
        }
        if !self.error.is_empty() {
            d.round(rect(45.0, 439.0, 764.0, 83.0), 0.0, Some(p.bg), None);
            d.line(45.0, 439.5, 809.0, 439.5, 1.0, p.border);
            text(
                &self.error,
                rect(45.0, 449.0, 764.0, 39.0),
                11.0,
                400,
                p.ink,
                L,
                0.0,
            );
        }
        for c in self.controls() {
            self.draw_control(d, &p, &c);
        }
    }

    fn draw_control(&self, d: &Drawing, p: &Palette, c: &Control) {
        use DWRITE_TEXT_ALIGNMENT_CENTER as C;
        use DWRITE_TEXT_ALIGNMENT_LEADING as L;
        let r = c.area;
        let hot = self.hover == Some(c.id) && self.job.is_none();
        let pressed = self.pressed == Some(c.id);
        match c.kind {
            Kind::Primary => {
                d.round(
                    r,
                    10.0,
                    Some(if hot {
                        mix(p.button, 0, 0.06)
                    } else {
                        p.button
                    }),
                    None,
                );
                d.text(&c.label, r, 14.0, 600, p.inverse, C, 0.2);
            }
            Kind::Text => {
                d.text(
                    &c.label,
                    r,
                    13.0,
                    if c.id == Id::Retry { 600 } else { 400 },
                    if hot { p.ink } else { p.muted },
                    L,
                    0.0,
                );
            }
            Kind::Outline => {
                d.round(r, 6.0, hot.then_some(p.hover), Some(p.border));
                d.text(&c.label, r, 12.0, 400, p.ink, C, 0.0);
            }
            Kind::Choice(active) => {
                d.round(
                    r,
                    5.0,
                    if active {
                        Some(p.button)
                    } else if hot {
                        Some(p.hover)
                    } else {
                        None
                    },
                    active.then_some(p.button),
                );
                d.text(
                    &c.label,
                    r,
                    11.0,
                    400,
                    if active { p.inverse } else { p.ink },
                    C,
                    0.0,
                );
            }
            Kind::Toggle(active) => {
                let y = r.top + 8.0;
                d.round(
                    rect(r.left, y, 38.0, 22.0),
                    11.0,
                    active.then_some(p.button),
                    Some(if active { p.button } else { p.muted }),
                );
                d.round(
                    rect(
                        r.left + if active { 20.0 } else { 4.0 },
                        y + 4.0,
                        14.0,
                        14.0,
                    ),
                    7.0,
                    Some(if active { p.inverse } else { p.muted }),
                    None,
                );
                d.text(
                    &c.label,
                    rect(r.left + 51.0, r.top, r.right - r.left - 51.0, 38.0),
                    if c.id == Id::Launch { 11.0 } else { 13.0 },
                    400,
                    p.ink,
                    L,
                    0.0,
                );
            }
            Kind::Caption => {
                if hot || pressed {
                    let tint = if c.id == Id::Close {
                        mix(p.bg, 0xef4964, if pressed { 0.34 } else { 0.22 })
                    } else {
                        mix(p.bg, p.ink, if pressed { 0.14 } else { 0.08 })
                    };
                    d.round(
                        rect(r.left, r.top + 1.0, 28.0, 28.0),
                        14.0,
                        Some(tint),
                        None,
                    );
                }
                let x = r.left + 14.0;
                let y = r.top + 15.0;
                if c.id == Id::Close {
                    d.line(x - 4.0, y - 4.0, x + 4.0, y + 4.0, 1.25, p.ink);
                    d.line(x + 4.0, y - 4.0, x - 4.0, y + 4.0, 1.25, p.ink);
                } else {
                    d.line(x - 5.0, y, x + 5.0, y, 1.25, p.ink);
                }
            }
            Kind::Edit => d.round(r, 6.0, Some(p.field), Some(p.border)),
        }
        if self.keyboard && self.focus == Some(c.id) && self.job.is_none() {
            let r = if matches!(c.kind, Kind::Toggle(_)) {
                rect(r.left, r.top + 8.0, 38.0, 22.0)
            } else {
                r
            };
            d.round(
                rect(
                    r.left - 3.0,
                    r.top - 3.0,
                    r.right - r.left + 6.0,
                    r.bottom - r.top + 6.0,
                ),
                8.0,
                None,
                Some(p.button),
            );
        }
    }
}

impl Setup {
    fn theme(&mut self) {
        let p = Palette::new(self.dark);
        unsafe {
            let dark = i32::from(self.dark);
            DwmSetWindowAttribute(
                self.hwnd,
                DWMWA_USE_IMMERSIVE_DARK_MODE as u32,
                (&dark as *const i32).cast(),
                4,
            );
            if !self.edit_brush.is_null() {
                DeleteObject(self.edit_brush);
            }
            self.edit_brush = CreateSolidBrush(bgr(p.field));
            if !self.font.is_null() {
                DeleteObject(self.font);
            }
            self.font = CreateFontW(
                -(12.0 * self.scale).round() as i32,
                0,
                0,
                0,
                400,
                0,
                0,
                0,
                1,
                0,
                0,
                5,
                0,
                wide("Montserrat").as_ptr(),
            );
            SendMessageW(self.edit, WM_SETFONT, self.font as usize, 1);
            self.sync_edit();
        }
    }

    unsafe fn sync_edit(&self) {
        unsafe {
            if let Some(control) = self.controls().into_iter().find(|c| c.id == Id::Path) {
                let r = control.area;
                let dip = |x: f32| (x * self.scale).round() as i32;
                SetWindowPos(
                    self.edit,
                    null_mut(),
                    dip(r.left + 11.0),
                    dip(r.top + 10.0),
                    dip(r.right - r.left - 22.0),
                    dip(17.0),
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW,
                );
            } else {
                ShowWindow(self.edit, SW_HIDE);
            }
            InvalidateRect(self.hwnd, null(), 0);
        }
    }

    fn read_path(&mut self) {
        unsafe {
            let count = GetWindowTextLengthW(self.edit).max(0);
            let mut text = vec![0_u16; count as usize + 1];
            let count = GetWindowTextW(self.edit, text.as_mut_ptr(), text.len() as i32).max(0);
            self.path = String::from_utf16_lossy(&text[..count as usize]);
        }
    }

    fn stage(&mut self, stage: Stage) {
        self.stage = stage;
        self.listening = false;
        self.error.clear();
        self.focus = None;
        self.hover = None;
        unsafe {
            self.sync_edit();
            SetFocus(self.hwnd);
        }
    }

    fn activate(&mut self, id: Id) -> bool {
        if id == Id::Minimize {
            unsafe {
                ShowWindow(self.hwnd, SW_MINIMIZE);
            }
            return false;
        }
        if self.job.is_some() {
            return false;
        }
        self.error.clear();
        match id {
            Id::Close => return true,
            Id::Minimize => return false,
            Id::Primary | Id::Accept => self.start(if self.uninstall {Job::Remove}else{Job::Install}),
            Id::Settings => self.stage(Stage::Settings),
            Id::Back => { self.read_path(); self.stage(Stage::Welcome); }
            Id::Ru | Id::En => { self.english=id==Id::En; unsafe {self.sync_edit();} }
            Id::Light | Id::Dark => { self.dark=id==Id::Dark; self.theme(); }
            Id::Path => unsafe { SetFocus(self.edit); },
            Id::Browse => match choose_folder(self.hwnd) {
                Ok(Some(path)) => { self.path=path; unsafe {SetWindowTextW(self.edit,wide(&self.path).as_ptr());} }
                Ok(None) => {}
                Err(e) => self.error=self.tr("Не удалось выбрать папку. Укажите полный путь в поле установки.",
                    "Could not open the folder picker. Enter an absolute installation path in the field.").to_string()+&format!(" {e}"),
            },
            Id::Desktop => self.desktop=!self.desktop,
            Id::Menu => self.menu=!self.menu,
            Id::Launch => self.launch=!self.launch,
            Id::Key => self.listening=true,
            Id::Next if self.key==44&&self.modifiers==0 => self.stage(Stage::Confirm),
            Id::Next | Id::Replace => self.save_key(id==Id::Replace),
            Id::Cancel => self.stage(Stage::Hotkey),
            Id::Done if !self.uninstall&&self.launch => self.start(Job::Launch),
            Id::Done => return true,
            Id::Retry => return self.activate(match self.stage {
                Stage::Settings=>Id::Browse,Stage::Hotkey=>Id::Next,Stage::Confirm=>Id::Replace,
                Stage::Finish=>Id::Done,_=>Id::Primary,
            }),
        }
        false
    }

    fn start(&mut self, job: Job) {
        if matches!(job, Job::Install) {
            self.read_path();
        }
        if !matches!(job, Job::Launch) {
            PROGRESS.store(0, Ordering::Relaxed);
            self.stage(Stage::Installing);
        }
        let operation = match job {
            Job::Install => "install",
            Job::Remove => "uninstall",
            Job::Launch => "open",
        };
        let data =
            serde_json::json!({"path":self.path,"desktop":self.desktop,"startMenu":self.menu,
            "language":if self.english{"en"}else{"ru"},"theme":if self.dark{"dark"}else{"light"}})
            .to_string();
        let backend = Arc::clone(&self.backend);
        let (send, receive) = mpsc::channel();
        self.job = Some((job, receive));
        std::thread::spawn(move || {
            let result = (|| {
                unsafe {
                    CoInitializeEx(None, COINIT_APARTMENTTHREADED)
                        .ok()
                        .map_err(|e| e.to_string())?;
                }
                let result = backend.lock().map_err(|e| e.to_string()).and_then(|mut b| {
                    b.dispatch(operation, data.as_bytes())
                        .map(|_| ())
                        .map_err(|e| e.to_string())
                });
                unsafe {
                    CoUninitialize();
                }
                result
            })();
            let _ = send.send(result);
        });
        unsafe {
            SetTimer(self.hwnd, TIMER, 50, None);
        }
    }

    fn advance(&mut self) -> bool {
        let Some((_, receive)) = &self.job else {
            return false;
        };
        let result = match receive.try_recv() {
            Ok(result) => result,
            Err(mpsc::TryRecvError::Empty) => return false,
            Err(mpsc::TryRecvError::Disconnected) => Err("installer worker stopped".into()),
        };
        let (job, _) = self.job.take().unwrap();
        unsafe {
            KillTimer(self.hwnd, TIMER);
        }
        match result {
            Ok(()) => match job {
                Job::Install => {
                    let key = self
                        .backend
                        .lock()
                        .ok()
                        .and_then(|mut b| b.dispatch("hotkey-current", b"{}").ok());
                    if let Some(key) = key {
                        self.key = key["vk"].as_u64().unwrap_or(44) as u32;
                        self.modifiers = key["mod"].as_u64().unwrap_or(0) as u32;
                        self.key_name = key_name(self.key, self.modifiers);
                    }
                    self.stage(Stage::Hotkey);
                }
                Job::Remove => self.stage(Stage::Finish),
                Job::Launch => return true,
            },
            Err(error) => {
                eprintln!("Screenshot setup: {error}");
                if !matches!(job, Job::Launch) {
                    self.stage(Stage::Welcome);
                }
                self.error = if matches!(job, Job::Launch) {
                    self.tr("Screenshot установлен, но запуск не удался. Повторите или выключите тумблер запуска и нажмите «Готово».",
                        "Screenshot is installed but could not be launched. Retry, or turn off launch and click Done.").into()
                } else {
                    format!(
                        "{} {error}",
                        self.tr(
                            "Не удалось завершить действие. Ваши настройки сохраняются.",
                            "Could not complete the operation. Your settings are preserved."
                        )
                    )
                };
            }
        }
        false
    }

    fn save_key(&mut self, replace: bool) {
        let data =
            serde_json::json!({"vk":self.key,"mod":self.modifiers,"replace":replace}).to_string();
        let result = self
            .backend
            .lock()
            .map_err(|e| e.to_string())
            .and_then(|mut backend| {
                backend
                    .dispatch("hotkey-save", data.as_bytes())
                    .map_err(|e| e.to_string())
            });
        match result {
            Ok(value) => {
                self.relogin = value["relogin"].as_bool().unwrap_or(false);
                self.stage(Stage::Finish);
            }
            Err(error) => self.error = error,
        }
    }

    fn hit(&self, x: f32, y: f32) -> Option<Id> {
        self.controls()
            .into_iter()
            .rev()
            .find(|c| contains(c.area, x, y))
            .map(|c| c.id)
    }

    fn key_event(&mut self, vk: u16, up: bool) -> bool {
        if self.job.is_some() {
            return false;
        }
        if self.listening && self.stage == Stage::Hotkey {
            if matches!(vk, VK_SHIFT | VK_CONTROL | VK_MENU | VK_LWIN | VK_RWIN)
                || up != (vk == VK_SNAPSHOT)
            {
                return false;
            }
            self.key = vk as u32;
            self.modifiers = unsafe {
                u32::from(GetKeyState(VK_CONTROL as i32) < 0) * MOD_CONTROL
                    | u32::from(GetKeyState(VK_SHIFT as i32) < 0) * MOD_SHIFT
                    | u32::from(GetKeyState(VK_MENU as i32) < 0) * MOD_ALT
            };
            self.key_name = key_name(self.key, self.modifiers);
            self.listening = false;
            return false;
        }
        if up {
            return false;
        }
        match vk {
            VK_ESCAPE => match self.stage {
                Stage::Confirm => self.stage(Stage::Hotkey),
                Stage::Settings => return self.activate(Id::Back),
                _ => return true,
            },
            VK_TAB => {
                let controls = self.controls();
                let count = controls.len();
                let reverse = unsafe { GetKeyState(VK_SHIFT as i32) < 0 };
                let found = controls.iter().position(|c| Some(c.id) == self.focus);
                let next = match found {
                    Some(i) => {
                        if reverse {
                            (i + count - 1) % count
                        } else {
                            (i + 1) % count
                        }
                    }
                    None => {
                        if reverse {
                            count - 1
                        } else {
                            0
                        }
                    }
                };
                self.keyboard = true;
                self.focus = Some(controls[next].id);
                if self.focus == Some(Id::Path) {
                    unsafe {
                        SetFocus(self.edit);
                    }
                } else {
                    unsafe {
                        SetFocus(self.hwnd);
                    }
                }
            }
            VK_RETURN | VK_SPACE => {
                if let Some(id) = self.focus {
                    return self.activate(id);
                }
            }
            _ => {}
        }
        false
    }
}

fn key_name(vk: u32, modifiers: u32) -> String {
    let key = match vk {
        44 => "Print Screen".into(),
        13 => "Enter".into(),
        32 => "Space".into(),
        27 => "Escape".into(),
        8 => "Backspace".into(),
        9 => "Tab".into(),
        112..=135 => format!("F{}", vk - 111),
        _ => char::from_u32(vk).unwrap_or('\u{fffd}').to_string(),
    };
    let mut parts = Vec::new();
    for (mask, label) in [
        (MOD_CONTROL, "Ctrl"),
        (MOD_SHIFT, "Shift"),
        (MOD_ALT, "Alt"),
    ] {
        if modifiers & mask != 0 {
            parts.push(label);
        }
    }
    parts.push(&key);
    parts.join(" + ")
}

fn bgr(rgb: u32) -> u32 {
    (rgb & 0x00ff00) | ((rgb >> 16) & 255) | ((rgb & 255) << 16)
}

fn mix(a: u32, b: u32, t: f32) -> u32 {
    let f = |s| {
        ((((a >> s) & 255_u32) as f32 * (1.0 - t) + ((b >> s) & 255_u32) as f32 * t).round() as u32)
            << s
    };
    f(16) | f(8) | f(0)
}

fn choose_folder(hwnd: HWND) -> windows::core::Result<Option<String>> {
    unsafe {
        let dialog: IFileOpenDialog =
            CoCreateInstance(&FileOpenDialog, None, CLSCTX_INPROC_SERVER)?;
        dialog.SetOptions(dialog.GetOptions()? | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM)?;
        if let Err(error) = dialog.Show(windows::Win32::Foundation::HWND(hwnd)) {
            return if error.code().0 as u32 == 0x800704c7 {
                Ok(None)
            } else {
                Err(error)
            };
        }
        let name = dialog.GetResult()?.GetDisplayName(SIGDN_FILESYSPATH)?;
        let path = name.to_string();
        CoTaskMemFree(Some(name.0.cast()));
        Ok(Some(path?))
    }
}

fn point(l: LPARAM) -> POINT {
    POINT {
        x: l as i16 as i32,
        y: (l >> 16) as i16 as i32,
    }
}

unsafe extern "system" fn window_proc(hwnd: HWND, msg: u32, w: WPARAM, l: LPARAM) -> LRESULT {
    if msg == WM_DESTROY {
        unsafe {
            PostQuitMessage(0);
        }
        return 0;
    }
    let mut close = false;
    let mut resize = None;
    let result = STATE.with(|state| {
        let Ok(mut state) = state.try_borrow_mut() else {
            return None;
        };
        let s = state.as_mut()?;
        let pos = point(l);
        let hit = || s.hit(pos.x as f32 / s.scale, pos.y as f32 / s.scale);
        unsafe {
            match msg {
                WM_CLOSE => close = s.job.is_none(),
                WM_ERASEBKGND => return Some(1),
                WM_PAINT => {
                    let mut paint: PAINTSTRUCT = zeroed();
                    BeginPaint(hwnd, &mut paint);
                    if s.drawing.is_none() {
                        s.drawing = Drawing::new(hwnd).ok();
                    }
                    if let Some(d) = s.drawing.as_ref() {
                        s.paint_scene(d);
                        if !d.end() {
                            s.drawing = None;
                            InvalidateRect(hwnd, null(), 0);
                        }
                    }
                    EndPaint(hwnd, &paint);
                    return Some(0);
                }
                WM_CTLCOLOREDIT => {
                    let p = Palette::new(s.dark);
                    SetBkColor(w as HDC, bgr(p.field));
                    SetTextColor(w as HDC, bgr(p.ink));
                    return Some(s.edit_brush as isize);
                }
                WM_NCHITTEST => {
                    let mut pos = pos;
                    ScreenToClient(hwnd, &mut pos);
                    let control = s.hit(pos.x as f32 / s.scale, pos.y as f32 / s.scale);
                    return Some(if control.is_some() {
                        HTCLIENT
                    } else {
                        HTCAPTION
                    } as isize);
                }
                WM_NCLBUTTONDBLCLK => return Some(0),
                WM_MOUSEMOVE => {
                    let hover = hit();
                    if hover != s.hover {
                        s.hover = hover;
                        InvalidateRect(hwnd, null(), 0);
                    }
                    let mut track = TRACKMOUSEEVENT {
                        cbSize: size_of::<TRACKMOUSEEVENT>() as u32,
                        dwFlags: TME_LEAVE,
                        hwndTrack: hwnd,
                        dwHoverTime: 0,
                    };
                    TrackMouseEvent(&mut track);
                    return Some(0);
                }
                WM_MOUSELEAVE => s.hover = None,
                WM_LBUTTONDOWN => {
                    let id = hit();
                    s.keyboard = false;
                    s.focus = id;
                    s.pressed = id;
                    if id != Some(Id::Path) {
                        SetFocus(hwnd);
                    }
                    SetCapture(hwnd);
                }
                WM_LBUTTONUP => {
                    let released = hit();
                    let pressed = s.pressed.take();
                    ReleaseCapture();
                    if let Some(id) = pressed.filter(|id| Some(*id) == released) {
                        close = s.activate(id);
                    }
                }
                WM_KEYDOWN | WM_SYSKEYDOWN | WM_KEYUP | WM_SYSKEYUP => {
                    if matches!(msg, WM_SYSKEYDOWN | WM_SYSKEYUP) && w as u16 == VK_F4 {
                        return None;
                    }
                    close = s.key_event(w as u16, matches!(msg, WM_KEYUP | WM_SYSKEYUP));
                }
                WM_TIMER if w == TIMER => close = s.advance(),
                WM_SIZE => {
                    s.drawing = None;
                    return None;
                }
                WM_DPICHANGED => {
                    s.scale = (w >> 16) as u16 as f32 / 96.0;
                    s.drawing = None;
                    let r = &*(l as *const RECT);
                    resize = Some((
                        r.left,
                        r.top,
                        (WIDTH * s.scale).round() as i32,
                        (HEIGHT * s.scale).round() as i32,
                    ));
                }
                _ => return None,
            }
            InvalidateRect(hwnd, null(), 0);
            Some(0)
        }
    });
    if let Some((x, y, width, height)) = resize {
        unsafe {
            SetWindowPos(
                hwnd,
                null_mut(),
                x,
                y,
                width,
                height,
                SWP_NOZORDER | SWP_NOACTIVATE,
            );
        }
        STATE.with_borrow_mut(|state| {
            if let Some(s) = state {
                s.theme();
            }
        });
    }
    if close {
        unsafe {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    result.unwrap_or_else(|| unsafe { DefWindowProcW(hwnd, msg, w, l) })
}

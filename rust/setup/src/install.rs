use std::{
    fs, io,
    os::windows::{ffi::OsStrExt, fs::MetadataExt, process::CommandExt},
    path::{Component, Path, PathBuf, Prefix},
    process::Command,
    sync::atomic::{AtomicU32, Ordering},
    thread,
    time::Duration,
};

use serde::Deserialize;
use serde_json::{Value, json};
use windows::{
    Win32::{
        System::Com::{CLSCTX_INPROC_SERVER, CoCreateInstance, IPersistFile},
        UI::Shell::{IShellLinkW, ShellLink},
    },
    core::{Interface, PCWSTR},
};
use windows_sys::Win32::{
    Foundation::{CloseHandle, INVALID_HANDLE_VALUE},
    Storage::FileSystem::*,
    System::Threading::{EVENT_MODIFY_STATE, OpenEventW, SetEvent},
    UI::{Input::KeyboardAndMouse::*, WindowsAndMessaging::*},
};
use winreg::{RegKey, enums::*};

const UNINSTALL: &str = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshot";
const RUN: &str = r"Software\Microsoft\Windows\CurrentVersion\Run";
const SETTINGS: &str = r"Software\SanderStripa\Screenshot";
const KEYBOARD: &str = r"Control Panel\Keyboard";
const APP: &str = "Screenshot.exe";
const REMOVE: &str = "Uninstall.exe";
pub const VERSION: &str = "1.8.6 RE";
pub static PROGRESS: AtomicU32 = AtomicU32::new(0);
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct Options {
    path: String,
    desktop: bool,
    start_menu: bool,
    language: String,
    theme: String,
}

#[derive(Deserialize)]
struct Shortcut {
    vk: u32,
    #[serde(rename = "mod")]
    modifiers: u32,
    replace: bool,
}

pub struct Installer {
    pub directory: PathBuf,
    pub uninstall: bool,
    installed: bool,
    hotkey_done: bool,
    pub removed: bool,
}

impl Installer {
    pub fn new(uninstall: bool) -> Self {
        let directory = key(UNINSTALL)
            .and_then(|key| key.get_value::<String, _>("InstallLocation"))
            .map(PathBuf::from)
            .unwrap_or_else(|_| {
                PathBuf::from(std::env::var_os("LOCALAPPDATA").unwrap_or_default())
                    .join("Programs")
                    .join("Screenshot")
            });
        Self {
            directory,
            uninstall,
            installed: false,
            hotkey_done: false,
            removed: false,
        }
    }

    pub fn dispatch(&mut self, action: &str, data: &[u8]) -> Result<Value> {
        if data.len() > 4096 {
            return Err("request too large".into());
        }
        match action {
            "install" if !self.uninstall => self.install(serde_json::from_slice(data)?)?,
            "uninstall" if self.uninstall => self.remove()?,
            "hotkey-current" if self.installed => {
                let settings = key(SETTINGS).ok();
                let read = |name| {
                    settings
                        .as_ref()
                        .and_then(|key| key.get_value::<u32, _>(name).ok())
                };
                return Ok(
                    json!({ "vk": read("HotkeyVK").unwrap_or(44), "mod": read("HotkeyModifiers").unwrap_or(0) }),
                );
            }
            "hotkey-save" if self.installed => {
                return self.save_hotkey(serde_json::from_slice(data)?);
            }
            "open" if self.installed && self.hotkey_done => self.open()?,
            _ => return Err("operation is unavailable".into()),
        }
        Ok(json!({ "ok": true }))
    }

    fn install(&mut self, options: Options) -> Result<()> {
        PROGRESS.store(0, Ordering::Relaxed);
        if !matches!(options.language.as_str(), "ru" | "en")
            || !matches!(options.theme.as_str(), "light" | "dark")
        {
            return Err("invalid preferences".into());
        }
        let directory = validate(&options.path)?;
        let current = self.directory.join(APP);
        if current.exists()
            && !directory
                .to_string_lossy()
                .eq_ignore_ascii_case(&self.directory.to_string_lossy())
        {
            return Err(format!(
                "update the existing installation at {}",
                self.directory.display()
            )
            .into());
        }
        let app = owned(&directory, APP)?;
        let uninstall = owned(&directory, REMOVE)?;
        if let Ok(entries) = fs::read_dir(&directory) {
            if entries.count() > 0 && !app.is_file() {
                return Err("choose an empty folder or existing Screenshot installation".into());
            }
        }
        let preserve = app.is_file() || key(SETTINGS).is_ok();
        ensure_runtime()?;
        stop(&current)?;
        PROGRESS.store(15, Ordering::Relaxed);
        fs::create_dir_all(&directory)?;
        replace(&app, include_bytes!("../payload/Screenshot.exe"))?;
        PROGRESS.store(55, Ordering::Relaxed);

        let running = std::env::current_exe()?;
        if !running.eq(&uninstall) {
            replace(&uninstall, &fs::read(running)?)?;
        }
        create(RUN)?.set_value("Screenshot", &format!("\"{}\" --background", app.display()))?;
        let metadata = create(UNINSTALL)?;
        for (name, value) in [
            ("DisplayName", "Screenshot".to_string()),
            ("DisplayVersion", VERSION.to_string()),
            ("Publisher", "Sander Stripa".to_string()),
            ("InstallLocation", directory.to_string_lossy().into_owned()),
            ("DisplayIcon", app.to_string_lossy().into_owned()),
            (
                "UninstallString",
                format!("\"{}\" --uninstall", uninstall.display()),
            ),
        ] {
            metadata.set_value(name, &value)?;
        }
        shortcuts(&app, options.desktop, options.start_menu)?;
        if !preserve {
            let settings = create(SETTINGS)?;
            let light = options.theme == "light";
            settings.set_value("ThemeMode", &(if light { 1_u32 } else { 2 }))?;
            settings.set_value("ThemeLight", &u32::from(light))?;
            settings.set_value("LanguageEN", &u32::from(options.language == "en"))?;
        }
        self.directory = directory;
        self.installed = true;
        PROGRESS.store(100, Ordering::Relaxed);
        Ok(())
    }

    fn remove(&mut self) -> Result<()> {
        let path = validate(&self.directory.to_string_lossy())?;
        let app = owned(&path, APP)?;
        stop(&app)?;
        ignore_missing(fs::remove_file(&app))?;
        if let Ok(run) = hkcu().open_subkey_with_flags(RUN, KEY_SET_VALUE) {
            ignore_missing(run.delete_value("Screenshot"))?;
        }
        ignore_missing(hkcu().delete_subkey(UNINSTALL))?;
        shortcuts(&app, false, false)?;
        self.removed = true;
        PROGRESS.store(100, Ordering::Relaxed);
        Ok(())
    }

    fn save_hotkey(&mut self, shortcut: Shortcut) -> Result<Value> {
        let Shortcut {
            vk,
            modifiers,
            replace,
        } = shortcut;
        if !(8..=254).contains(&vk) || matches!(vk, 16..=18) || modifiers & !7 != 0 {
            return Err("choose a non-modifier key".into());
        }
        let print_screen = vk == 44 && modifiers == 0;
        if print_screen && !replace {
            return Err("Print Screen requires explicit confirmation".into());
        }
        if !print_screen && !available(vk, modifiers) {
            return Err("keyboard shortcut is already in use".into());
        }
        if print_screen {
            create(KEYBOARD)?.set_value("PrintScreenKeyForSnippingEnabled", &0_u32)?;
            unsafe {
                SendMessageTimeoutW(
                    HWND_BROADCAST,
                    WM_SETTINGCHANGE,
                    0,
                    wide(KEYBOARD).as_ptr() as isize,
                    SMTO_ABORTIFHUNG,
                    300,
                    std::ptr::null_mut(),
                );
            }
        }
        let key = create(SETTINGS)?;
        key.set_value("HotkeyVK", &vk)?;
        key.set_value("HotkeyModifiers", &modifiers)?;
        self.hotkey_done = true;
        Ok(json!({ "relogin": !available(vk, modifiers) }))
    }

    fn open(&self) -> Result<()> {
        Command::new(self.directory.join(APP))
            .arg("--settings")
            .current_dir(&self.directory)
            .spawn()?;
        for _ in 0..100 {
            unsafe {
                let window = FindWindowW(wide("ScreenshotSettings").as_ptr(), std::ptr::null());
                if !window.is_null() && IsWindowVisible(window) != 0 {
                    return Ok(());
                }
            }
            thread::sleep(Duration::from_millis(100));
        }
        Err("Screenshot did not open its main window".into())
    }

    pub fn cleanup(&self) -> Result<()> {
        let uninstall = owned(&self.directory, REMOVE)?;
        let script = format!(
            "Wait-Process -Id {} -ErrorAction SilentlyContinue; Remove-Item -LiteralPath {} -Force; if (@(Get-ChildItem -LiteralPath {} -Force).Count -eq 0) {{ Remove-Item -LiteralPath {} -Force }}",
            std::process::id(),
            quote(&uninstall),
            quote(&self.directory),
            quote(&self.directory),
        );
        powershell(&script).spawn()?;
        Ok(())
    }
}

pub fn wide(value: impl AsRef<std::ffi::OsStr>) -> Vec<u16> {
    value.as_ref().encode_wide().chain(Some(0)).collect()
}

fn hkcu() -> RegKey {
    RegKey::predef(HKEY_CURRENT_USER)
}
fn key(path: &str) -> io::Result<RegKey> {
    hkcu().open_subkey(path)
}
fn create(path: &str) -> io::Result<RegKey> {
    hkcu().create_subkey(path).map(|(key, _)| key)
}

fn ignore_missing(result: io::Result<()>) -> io::Result<()> {
    match result {
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
        other => other,
    }
}

fn available(vk: u32, modifiers: u32) -> bool {
    unsafe {
        let success = RegisterHotKey(std::ptr::null_mut(), 998, modifiers | MOD_NOREPEAT, vk) != 0;
        if success {
            UnregisterHotKey(std::ptr::null_mut(), 998);
        }
        success
    }
}

fn validate(input: &str) -> Result<PathBuf> {
    let input = input.trim();
    let path = Path::new(input);
    if !path.is_absolute()
        || !matches!(path.components().next(), Some(Component::Prefix(prefix)) if matches!(prefix.kind(), Prefix::Disk(_)))
        || input.contains(['<', '>', '"', '|', '?', '*'])
        || input.get(2..).is_none_or(|tail| tail.contains(':'))
    {
        return Err("choose an absolute dedicated application folder".into());
    }
    let mut clean = PathBuf::new();
    for part in path.components() {
        match part {
            Component::ParentDir => {
                clean.pop();
            }
            Component::Normal(name) => {
                let name = name.to_string_lossy();
                let stem = name
                    .split('.')
                    .next()
                    .unwrap_or_default()
                    .to_ascii_uppercase();
                if name.ends_with([' ', '.'])
                    || matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
                    || (stem.len() == 4
                        && (stem.starts_with("COM") || stem.starts_with("LPT"))
                        && stem.as_bytes()[3].is_ascii_digit())
                {
                    return Err("invalid folder name".into());
                }
                clean.push(part);
            }
            _ => clean.push(part),
        }
    }
    if clean.parent().is_none() {
        return Err("choose a dedicated application folder".into());
    }
    let text = clean.to_string_lossy().to_lowercase();
    for var in [
        "WINDIR",
        "ProgramFiles",
        "ProgramFiles(x86)",
        "ProgramData",
        "USERPROFILE",
        "LOCALAPPDATA",
        "APPDATA",
    ] {
        if let Ok(forbidden) = std::env::var(var) {
            let forbidden = forbidden.trim_end_matches(['\\', '/']).to_lowercase();
            if text == forbidden || (var == "WINDIR" && text.starts_with(&(forbidden + "\\"))) {
                return Err("choose a dedicated application folder outside Windows".into());
            }
        }
    }
    for parent in clean.ancestors() {
        match fs::symlink_metadata(parent) {
            Ok(meta)
                if !meta.is_dir() || meta.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0 =>
            {
                return Err("directory must not contain junctions or symbolic links".into());
            }
            Err(err) if err.kind() != io::ErrorKind::NotFound => return Err(err.into()),
            _ => {}
        }
    }
    Ok(clean)
}

fn owned(directory: &Path, name: &str) -> Result<PathBuf> {
    validate(&directory.to_string_lossy())?;
    let path = directory.join(name);
    if fs::symlink_metadata(&path).is_ok_and(|meta| {
        !meta.is_file() || meta.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0
    }) {
        return Err("payload target is not a regular file".into());
    }
    Ok(path)
}

fn replace(path: &Path, bytes: &[u8]) -> Result<()> {
    use std::io::Write;
    let mut staged =
        tempfile::NamedTempFile::new_in(path.parent().ok_or("missing destination directory")?)?;
    staged.write_all(bytes)?;
    staged.as_file().sync_all()?;
    staged.persist(path)?;
    Ok(())
}

fn stop(path: &Path) -> Result<()> {
    unsafe {
        let event = OpenEventW(
            EVENT_MODIFY_STATE,
            0,
            wide(r"Local\Screenshot.SanderStripa.Exit").as_ptr(),
        );
        if !event.is_null() {
            SetEvent(event);
            CloseHandle(event);
        }
    }
    for _ in 0..30 {
        if !path.exists() {
            return Ok(());
        }
        unsafe {
            let handle = CreateFileW(
                wide(path).as_ptr(),
                0x40010000,
                FILE_SHARE_READ,
                std::ptr::null(),
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                std::ptr::null_mut(),
            );
            if handle != INVALID_HANDLE_VALUE {
                CloseHandle(handle);
                return Ok(());
            }
        }
        thread::sleep(Duration::from_millis(100));
    }
    Err("close Screenshot before continuing".into())
}

fn shortcuts(app: &Path, desktop: bool, start_menu: bool) -> Result<()> {
    use windows::Win32::UI::Shell::{
        FOLDERID_Desktop, FOLDERID_Programs, KF_FLAG_DEFAULT, SHGetKnownFolderPath,
    };
    for (folder, enabled) in [(FOLDERID_Desktop, desktop), (FOLDERID_Programs, start_menu)] {
        unsafe {
            let raw = SHGetKnownFolderPath(&folder, KF_FLAG_DEFAULT, None)?;
            let path = raw.to_string();
            windows::Win32::System::Com::CoTaskMemFree(Some(raw.0.cast()));
            let mut link = PathBuf::from(path?);
            if folder == FOLDERID_Programs {
                link.push("Screenshot");
            }
            let link = link.join("Screenshot.lnk");
            if !enabled {
                ignore_missing(fs::remove_file(link))?;
                continue;
            }
            fs::create_dir_all(link.parent().ok_or("missing shortcut directory")?)?;
            let shell: IShellLinkW = CoCreateInstance(&ShellLink, None, CLSCTX_INPROC_SERVER)?;
            shell.SetPath(PCWSTR(wide(app).as_ptr()))?;
            shell.SetArguments(PCWSTR(wide("--settings").as_ptr()))?;
            shell.SetWorkingDirectory(PCWSTR(
                wide(app.parent().ok_or("missing program directory")?).as_ptr(),
            ))?;
            shell.SetIconLocation(PCWSTR(wide(app).as_ptr()), 0)?;
            shell
                .cast::<IPersistFile>()?
                .Save(PCWSTR(wide(link).as_ptr()), true)?;
        }
    }
    Ok(())
}

fn quote(path: &Path) -> String {
    format!("'{}'", path.to_string_lossy().replace('\'', "''"))
}

fn powershell(script: &str) -> Command {
    let mut command = Command::new("powershell.exe");
    command
        .args(["-NoProfile", "-NonInteractive", "-Command", script])
        .creation_flags(0x0800_0000);
    command
}

fn runtime_installed() -> bool {
    let reg = RegKey::predef(HKEY_LOCAL_MACHINE);
    [KEY_WOW64_32KEY, KEY_WOW64_64KEY].into_iter().any(|view| {
        reg.open_subkey_with_flags(
            r"SOFTWARE\Microsoft\VisualStudio.0\VC\Runtimesd",
            KEY_QUERY_VALUE | view,
        )
        .is_ok_and(|runtime| {
            let get = |name| runtime.get_value::<u32, _>(name).unwrap_or_default();
            get("Installed") == 1 && (get("Major"), get("Minor")) >= (14, 44)
        })
    })
}

fn ensure_runtime() -> Result<()> {
    if runtime_installed() {
        return Ok(());
    }
    let temp = tempfile::tempdir()?;
    let package = temp.path().join("vc_redist.x64.exe");
    fs::write(&package, include_bytes!("../payload/vc_redist.x64.exe"))?;
    use std::mem::size_of;
    use windows::Win32::{
        Foundation::{HANDLE, HWND},
        Security::WinTrust::*,
        UI::Shell::{
            SEE_MASK_NOASYNC, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW, ShellExecuteExW,
        },
    };
    use windows_sys::Win32::{
        Foundation::WAIT_OBJECT_0,
        System::Threading::{GetExitCodeProcess, WaitForSingleObject},
    };
    let file_name = wide(&package);
    let verb = wide("runas");
    let arguments = wide("/install /quiet /norestart");
    let mut file = WINTRUST_FILE_INFO {
        cbStruct: size_of::<WINTRUST_FILE_INFO>() as u32,
        pcwszFilePath: PCWSTR(file_name.as_ptr()),
        hFile: HANDLE::default(),
        pgKnownSubject: std::ptr::null_mut(),
    };
    let mut policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    let mut trust = WINTRUST_DATA {
        cbStruct: size_of::<WINTRUST_DATA>() as u32,
        dwUIChoice: WTD_UI_NONE,
        dwUnionChoice: WTD_CHOICE_FILE,
        Anonymous: WINTRUST_DATA_0 { pFile: &mut file },
        ..Default::default()
    };
    let status = unsafe {
        WinVerifyTrust(
            HWND::default(),
            &mut policy,
            (&mut trust as *mut WINTRUST_DATA).cast(),
        )
    };
    if status != 0 {
        return Err(format!(
            "Invalid Microsoft Visual C++ signature (0x{:08X})",
            status as u32
        )
        .into());
    }
    let mut launch = SHELLEXECUTEINFOW {
        cbSize: size_of::<SHELLEXECUTEINFOW>() as u32,
        fMask: SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC,
        lpVerb: PCWSTR(verb.as_ptr()),
        lpFile: PCWSTR(file_name.as_ptr()),
        lpParameters: PCWSTR(arguments.as_ptr()),
        nShow: SW_SHOWNORMAL,
        ..Default::default()
    };
    unsafe {
        ShellExecuteExW(&mut launch)?;
    }
    let handle = launch.hProcess.0;
    let mut code = 0_u32;
    let success = unsafe {
        let completed = WaitForSingleObject(handle, u32::MAX) == WAIT_OBJECT_0;
        let read = completed && GetExitCodeProcess(handle, &mut code) != 0;
        CloseHandle(handle);
        read && matches!(code, 0 | 3010 | 1638)
    };
    if !success || !runtime_installed() {
        return Err(
            format!("Microsoft Visual C++ x64 Runtime installation failed ({code})").into(),
        );
    }
    Ok(())
}

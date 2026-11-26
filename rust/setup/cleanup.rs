#![windows_subsystem = "windows"]

use std::{
    ffi::c_void,
    fs,
    io,
    os::windows::ffi::OsStrExt,
    path::PathBuf,
    thread,
    time::Duration,
};

#[link(name = "kernel32")]
unsafe extern "system" {
    fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
    fn WaitForSingleObject(handle: *mut c_void, ms: u32) -> u32;
    fn CloseHandle(handle: *mut c_void) -> i32;
    fn MoveFileExW(path: *const u16, destination: *const u16, flags: u32) -> i32;
}

fn main() {
    let mut args = std::env::args_os().skip(1);
    let Some(pid) = args.next().and_then(|pid| pid.to_str()?.parse::<u32>().ok()) else { return };
    let Some(path) = args.next().map(PathBuf::from) else { return };
    if !path.is_absolute() || path.file_name() != Some("Uninstall.exe".as_ref()) { return; }
    unsafe {
        let parent = OpenProcess(0x0010_0000, 0, pid);
        if !parent.is_null() {
            let result = WaitForSingleObject(parent, 60_000);
            CloseHandle(parent);
            if result != 0 { return; }
        }
    }
    for _ in 0..30 {
        if fs::remove_file(&path).is_ok_or_missing() {
            if let Some(directory) = path.parent() { let _ = fs::remove_dir(directory); }
            break;
        }
        thread::sleep(Duration::from_millis(100));
    }
    if let Ok(exe) = std::env::current_exe() {
        let wide: Vec<_> = exe.as_os_str().encode_wide().chain(Some(0)).collect();
        unsafe { MoveFileExW(wide.as_ptr(), std::ptr::null(), 4); }
    }
}

trait Missing {
    fn is_ok_or_missing(&self) -> bool;
}

impl Missing for io::Result<()> {
    fn is_ok_or_missing(&self) -> bool {
        match self { Ok(()) => true, Err(error) => error.kind() == io::ErrorKind::NotFound }
    }
}

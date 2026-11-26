pub fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(std::iter::once(0)).collect()
}

use std::{
    mem::size_of,
    ptr::{null, null_mut},
};
use windows_sys::Win32::{
    Foundation::{ERROR_SUCCESS, POINT},
    System::Registry::*,
};

pub const CONFIG: &str = "Software\\SanderStripa\\Screenshot";
pub const KEYBOARD: &str = "Control Panel\\Keyboard";
pub const SNIPPING: &str = "PrintScreenKeyForSnippingEnabled";

pub fn read_dword(path: &str, name: &str) -> Option<u32> {
    let mut value = 0_u32;
    read(path, name, RRF_RT_REG_DWORD, &mut value).then_some(value)
}

pub fn read_position() -> Option<POINT> {
    let mut value = POINT { x: 0, y: 0 };
    read(CONFIG, "WindowPosition", RRF_RT_REG_BINARY, &mut value).then_some(value)
}

fn read<T>(path: &str, name: &str, flags: u32, value: &mut T) -> bool {
    let mut size = size_of::<T>() as u32;
    unsafe {
        RegGetValueW(
            HKEY_CURRENT_USER,
            wide(path).as_ptr(),
            wide(name).as_ptr(),
            flags,
            null_mut(),
            (value as *mut T).cast(),
            &mut size,
        ) == ERROR_SUCCESS
            && size as usize == size_of::<T>()
    }
}

pub fn write_dword(path: &str, name: &str, value: Option<u32>) -> bool {
    match value {
        Some(value) => write(path, name, REG_DWORD, Some(&value)),
        None => write::<u32>(path, name, REG_DWORD, None),
    }
}

pub fn write_position(point: POINT) {
    write(CONFIG, "WindowPosition", REG_BINARY, Some(&point));
}

fn write<T>(path: &str, name: &str, kind: REG_VALUE_TYPE, value: Option<&T>) -> bool {
    unsafe {
        let mut key = null_mut();
        if RegCreateKeyExW(
            HKEY_CURRENT_USER,
            wide(path).as_ptr(),
            0,
            null(),
            0,
            KEY_SET_VALUE,
            null(),
            &mut key,
            null_mut(),
        ) != ERROR_SUCCESS
        {
            return false;
        }
        let name = wide(name);
        let result = if let Some(value) = value {
            RegSetValueExW(
                key,
                name.as_ptr(),
                0,
                kind,
                (value as *const T).cast(),
                size_of::<T>() as u32,
            )
        } else {
            RegDeleteValueW(key, name.as_ptr())
        };
        RegCloseKey(key);
        result == ERROR_SUCCESS
    }
}

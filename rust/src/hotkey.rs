use crate::win::{self, CONFIG};
use std::cell::Cell;
use windows_sys::Win32::{Foundation::HWND, UI::Input::KeyboardAndMouse::*};

pub const ID: i32 = 100;
const MODIFIERS: u32 = MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN;

#[derive(Clone, Copy, PartialEq)]
pub struct Key {
    pub vk: u32,
    pub modifiers: u32,
}

impl Key {
    pub fn saved() -> Self {
        Self {
            vk: win::read_dword(CONFIG, "HotkeyVK")
                .filter(|value| (VK_BACK as u32..=VK_F24 as u32).contains(value))
                .unwrap_or(VK_SNAPSHOT as u32),
            modifiers: win::read_dword(CONFIG, "HotkeyModifiers").unwrap_or(0) & MODIFIERS,
        }
    }

    pub fn persist(self) -> bool {
        win::write_dword(CONFIG, "HotkeyVK", Some(self.vk))
            && win::write_dword(CONFIG, "HotkeyModifiers", Some(self.modifiers))
    }

    pub fn is_print_screen(self) -> bool {
        self.vk == VK_SNAPSHOT as u32 && self.modifiers == 0
    }

    pub fn name(self) -> String {
        let key = self.vk as u16;
        let name = match key {
            VK_SNAPSHOT => "Print Screen".into(),
            VK_F1..=VK_F24 => format!("F{}", key - VK_F1 + 1),
            _ => unsafe {
                let mut text = [0_u16; 100];
                let mut scan = MapVirtualKeyW(self.vk, MAPVK_VK_TO_VSC);
                if matches!(
                    key,
                    VK_INSERT | VK_DELETE | VK_HOME | VK_END | VK_PRIOR | VK_NEXT
                ) {
                    scan |= 0x100;
                }
                let count =
                    GetKeyNameTextW((scan << 16) as i32, text.as_mut_ptr(), text.len() as i32);
                String::from_utf16_lossy(&text[..count.max(0) as usize])
            },
        };
        let mut text = String::new();
        for (mask, label) in [
            (MOD_CONTROL, "Ctrl"),
            (MOD_ALT, "Alt"),
            (MOD_SHIFT, "Shift"),
            (MOD_WIN, "Win"),
        ] {
            if self.modifiers & mask != 0 {
                text.push_str(label);
                text.push_str(" + ");
            }
        }
        text.push_str(&name);
        text
    }
}

thread_local! {
    static REGISTRATION: Cell<Option<(HWND, Key, bool)>> = const { Cell::new(None) };
}

pub fn initialize(hwnd: HWND, key: Key) -> bool {
    let registered = register(hwnd, ID, key);
    REGISTRATION.set(Some((hwnd, key, registered)));
    registered
}

pub fn available() -> bool {
    REGISTRATION
        .get()
        .is_some_and(|(_, _, registered)| registered)
}

pub fn replace(next: Key) -> bool {
    let Some((hwnd, old, registered)) = REGISTRATION.get() else {
        return false;
    };
    if next == old && registered {
        return true;
    }
    if !register(hwnd, ID + 1, next) {
        return false;
    }
    unsafe {
        UnregisterHotKey(hwnd, ID + 1);
        if registered {
            UnregisterHotKey(hwnd, ID);
        }
    }
    if register(hwnd, ID, next) {
        REGISTRATION.set(Some((hwnd, next, true)));
        true
    } else {
        let restored = register(hwnd, ID, old);
        REGISTRATION.set(Some((hwnd, old, restored)));
        false
    }
}

fn register(window: HWND, id: i32, key: Key) -> bool {
    unsafe { RegisterHotKey(window, id, key.modifiers | MOD_NOREPEAT, key.vk) != 0 }
}

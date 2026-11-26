pub mod image;

#[cfg(windows)]
pub mod desktop;

#[cfg(windows)]
pub mod win;

#[cfg(windows)]
pub mod overlay;

#[cfg(windows)]
pub mod hotkey;

#[cfg(windows)]
pub mod settings;

#[cfg(windows)]
mod settings_draw;

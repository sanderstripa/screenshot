use std::{
    cell::RefCell,
    mem::{size_of, zeroed},
    ptr::{null, null_mut},
    time::Instant,
};

use windows::Win32::Graphics::{
    Direct2D::{Common::*, *},
    Dxgi::Common::DXGI_FORMAT_B8G8R8A8_UNORM,
};
use windows_sys::Win32::{
    Foundation::{HINSTANCE, HWND, LPARAM, LRESULT, POINT, WPARAM},
    Graphics::Gdi::{BeginPaint, EndPaint, InvalidateRect, PAINTSTRUCT},
    UI::{
        Input::KeyboardAndMouse::{GetCapture, ReleaseCapture, SetCapture, SetFocus, VK_ESCAPE},
        WindowsAndMessaging::*,
    },
};

use crate::win::wide;
use crate::{
    desktop,
    image::{Image, Rect},
};

const TIMER: usize = 101;
pub const CAPTURE_FAILED: u32 = WM_APP + 2;

thread_local! {
    static ACTIVE: RefCell<Option<Overlay>> = const { RefCell::new(None) };
}

struct Overlay {
    hwnd: HWND,
    controller: HWND,
    desktop: Image,
    bounds: Rect,
    selection: Option<Rect>,
    down: Option<POINT>,
    dragging: bool,
    finishing: bool,
    close_hover: bool,
    started: Instant,
    graphics: Option<Graphics>,
}

pub fn register(instance: HINSTANCE) -> bool {
    let name = wide("ScreenshotOverlay");
    let class = WNDCLASSEXW {
        cbSize: size_of::<WNDCLASSEXW>() as u32,
        lpfnWndProc: Some(proc),
        hInstance: instance,
        lpszClassName: name.as_ptr(),
        hCursor: unsafe { LoadCursorW(null_mut(), IDC_CROSS) },
        ..unsafe { zeroed() }
    };
    unsafe { RegisterClassExW(&class) != 0 }
}

pub fn begin(instance: HINSTANCE, controller: HWND) {
    if ACTIVE.with_borrow(Option::is_some) {
        return;
    }
    let Some((bounds, desktop)) = desktop::capture() else {
        return;
    };
    unsafe {
        let hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            wide("ScreenshotOverlay").as_ptr(),
            wide("Screenshot").as_ptr(),
            WS_POPUP,
            bounds.left,
            bounds.top,
            desktop.width as i32,
            desktop.height as i32,
            null_mut(),
            null_mut(),
            instance,
            null(),
        );
        if hwnd.is_null() {
            return;
        }
        let mut cursor = POINT { x: 0, y: 0 };
        GetCursorPos(&mut cursor);
        let mut overlay = Overlay {
            hwnd,
            controller,
            desktop,
            bounds,
            selection: None,
            down: None,
            dragging: false,
            finishing: false,
            close_hover: false,
            started: Instant::now(),
            graphics: None,
        };
        overlay.update_hover(cursor);
        ACTIVE.set(Some(overlay));
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        SetTimer(hwnd, TIMER, 16, None);
    }
}

pub fn dismiss() {
    if let Some(overlay) = ACTIVE.take() {
        unsafe {
            if GetCapture() == overlay.hwnd {
                ReleaseCapture();
            }
            DestroyWindow(overlay.hwnd);
        }
    }
}

enum Action {
    None,
    Dismiss,
    Failed(HWND, bool),
}

unsafe extern "system" fn proc(hwnd: HWND, message: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    let handled = matches!(
        message,
        WM_ERASEBKGND
            | WM_PAINT
            | WM_SETCURSOR
            | WM_TIMER
            | WM_KEYDOWN
            | WM_LBUTTONDOWN
            | WM_MOUSEMOVE
            | WM_LBUTTONUP
            | WM_RBUTTONDOWN
            | WM_MBUTTONDOWN
            | WM_ACTIVATE
    );
    if !handled {
        return unsafe { DefWindowProcW(hwnd, message, wp, lp) };
    }
    if message == WM_SETCURSOR {
        unsafe { SetCursor(LoadCursorW(null_mut(), IDC_CROSS)) };
        return 1;
    }
    let mut paint: PAINTSTRUCT = unsafe { zeroed() };
    if message == WM_PAINT {
        unsafe { BeginPaint(hwnd, &mut paint) };
    }
    let action = ACTIVE
        .try_with(|active| {
            let Ok(mut state) = active.try_borrow_mut() else {
                return Action::None;
            };
            let Some(overlay) = state.as_mut().filter(|overlay| overlay.hwnd == hwnd) else {
                return Action::None;
            };
            overlay.handle(message, wp, lp)
        })
        .unwrap_or(Action::None);
    if message == WM_PAINT {
        unsafe { EndPaint(hwnd, &paint) };
    }
    match action {
        Action::None => {}
        Action::Dismiss => dismiss(),
        Action::Failed(controller, saved) => {
            dismiss();
            unsafe { PostMessageW(controller, CAPTURE_FAILED, saved as usize, 0) };
        }
    }
    0
}

impl Overlay {
    fn toolbar(&self) -> D2D_RECT_F {
        let x = (self.desktop.width / 2) as f32;
        rect(x - 48.0, 14.0, x + 48.0, 55.0)
    }

    fn close_hit(&self, screen: POINT) -> bool {
        let bar = self.toolbar();
        let x = (screen.x - self.bounds.left) as f32;
        let y = (screen.y - self.bounds.top) as f32;
        x >= bar.right - 43.0 && x < bar.right && y >= bar.top && y < bar.bottom
    }

    fn update_hover(&mut self, point: POINT) {
        self.close_hover = self.close_hit(point);
        if self.close_hover {
            self.selection = None;
        } else if let Some(down) = self.down {
            if self.dragging {
                self.selection = self.bounds.intersection(drag_rect(down, point));
            }
        } else {
            self.selection = Some(desktop::window_at(
                point,
                self.hwnd,
                self.controller,
                self.bounds,
            ));
        }
        unsafe { InvalidateRect(self.hwnd, null(), 0) };
    }

    fn handle(&mut self, message: u32, wp: WPARAM, lp: LPARAM) -> Action {
        let local = POINT {
            x: lp as i16 as i32,
            y: (lp >> 16) as i16 as i32,
        };
        let point = POINT {
            x: self.bounds.left + local.x,
            y: self.bounds.top + local.y,
        };
        match message {
            WM_PAINT => self.paint(),
            WM_TIMER if wp == TIMER => {
                if self.finishing && self.started.elapsed().as_millis() >= 125 {
                    return Action::Dismiss;
                }
                unsafe { InvalidateRect(self.hwnd, null(), 0) };
            }
            WM_KEYDOWN if wp == VK_ESCAPE as usize => return Action::Dismiss,
            WM_RBUTTONDOWN | WM_MBUTTONDOWN => return Action::Dismiss,
            WM_ACTIVATE if wp as u16 == WA_INACTIVE as u16 && !self.finishing => {
                return Action::Dismiss;
            }
            WM_LBUTTONDOWN if !self.finishing => {
                if self.close_hit(point) {
                    return Action::Dismiss;
                }
                self.down = Some(point);
                self.dragging = false;
                unsafe { SetCapture(self.hwnd) };
            }
            WM_MOUSEMOVE if !self.finishing => {
                if let Some(down) = self.down {
                    self.dragging |= (point.x - down.x).abs() > 7 || (point.y - down.y).abs() > 7;
                }
                self.update_hover(point);
            }
            WM_LBUTTONUP if !self.finishing => {
                let Some(down) = self.down.take() else {
                    return Action::None;
                };
                unsafe {
                    if GetCapture() == self.hwnd {
                        ReleaseCapture();
                    }
                }
                let area = if self.dragging {
                    drag_rect(down, point)
                } else {
                    desktop::window_at(point, self.hwnd, self.controller, self.bounds)
                };
                self.dragging = false;
                let Some(area) = self.bounds.intersection(area) else {
                    return Action::Dismiss;
                };
                return self.accept(area);
            }
            _ => {}
        }
        Action::None
    }

    fn accept(&mut self, area: Rect) -> Action {
        let Some(image) = self.desktop.styled_capture(self.bounds, area) else {
            return Action::Failed(self.controller, false);
        };
        let Ok(png) = image.png() else {
            return Action::Failed(self.controller, false);
        };
        let saved = desktop::save(&png).is_ok();
        if !saved || !unsafe { desktop::publish_clipboard(self.hwnd, &image, &png) } {
            return Action::Failed(self.controller, saved);
        }
        self.selection = Some(area);
        self.finishing = true;
        self.started = Instant::now();
        unsafe { InvalidateRect(self.hwnd, null(), 0) };
        Action::None
    }

    fn paint(&mut self) {
        let toolbar = self.toolbar();
        let selection = self.selection.map(|area| {
            rect(
                (area.left - self.bounds.left) as f32,
                (area.top - self.bounds.top) as f32,
                (area.right - self.bounds.left) as f32,
                (area.bottom - self.bounds.top) as f32,
            )
        });
        if self.graphics.is_none() {
            self.graphics = Graphics::new(self.hwnd, &self.desktop).ok();
        }
        let Some(graphics) = &self.graphics else {
            return;
        };
        let duration = if self.finishing { 125.0 } else { 140.0 };
        let progress = (self.started.elapsed().as_secs_f32() * 1000.0 / duration).min(1.0);
        let alpha = if self.finishing {
            1.0 - progress
        } else {
            progress
        };
        if graphics
            .draw(toolbar, selection, self.close_hover, alpha)
            .is_err()
        {
            self.graphics = None;
        }
    }
}

fn drag_rect(a: POINT, b: POINT) -> Rect {
    Rect {
        left: a.x.min(b.x),
        top: a.y.min(b.y),
        right: a.x.max(b.x),
        bottom: a.y.max(b.y),
    }
}

struct Graphics {
    target: ID2D1HwndRenderTarget,
    bitmap: ID2D1Bitmap,
    brush: ID2D1SolidColorBrush,
    bounds: D2D_RECT_F,
}

fn rect(left: f32, top: f32, right: f32, bottom: f32) -> D2D_RECT_F {
    D2D_RECT_F {
        left,
        top,
        right,
        bottom,
    }
}

fn point(x: f32, y: f32) -> D2D_POINT_2F {
    D2D_POINT_2F { x, y }
}

fn color(r: f32, g: f32, b: f32, a: f32) -> D2D1_COLOR_F {
    D2D1_COLOR_F { r, g, b, a }
}

impl Graphics {
    fn new(hwnd: HWND, image: &Image) -> windows::core::Result<Self> {
        let pixels = D2D1_PIXEL_FORMAT {
            format: DXGI_FORMAT_B8G8R8A8_UNORM,
            alphaMode: D2D1_ALPHA_MODE_IGNORE,
        };
        let size = D2D_SIZE_U {
            width: image.width as u32,
            height: image.height as u32,
        };
        let mut bgra = image.rgba.clone();
        for pixel in bgra.chunks_exact_mut(4) {
            pixel.swap(0, 2);
        }
        unsafe {
            let factory: ID2D1Factory = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, None)?;
            let target = factory.CreateHwndRenderTarget(
                &D2D1_RENDER_TARGET_PROPERTIES {
                    pixelFormat: pixels,
                    ..Default::default()
                },
                &D2D1_HWND_RENDER_TARGET_PROPERTIES {
                    hwnd: windows::Win32::Foundation::HWND(hwnd),
                    pixelSize: size,
                    presentOptions: D2D1_PRESENT_OPTIONS_IMMEDIATELY,
                },
            )?;
            let bitmap = target.CreateBitmap(
                size,
                Some(bgra.as_ptr().cast()),
                size.width * 4,
                &D2D1_BITMAP_PROPERTIES {
                    pixelFormat: pixels,
                    dpiX: 96.0,
                    dpiY: 96.0,
                },
            )?;
            let brush = target.CreateSolidColorBrush(&color(0.0, 0.0, 0.0, 1.0), None)?;
            target.SetDpi(96.0, 96.0);
            Ok(Self {
                target,
                bitmap,
                brush,
                bounds: rect(0.0, 0.0, size.width as f32, size.height as f32),
            })
        }
    }

    fn draw(
        &self,
        toolbar: D2D_RECT_F,
        selection: Option<D2D_RECT_F>,
        hovering: bool,
        alpha: f32,
    ) -> windows::core::Result<()> {
        let target = &self.target;
        let brush = &self.brush;
        unsafe {
            target.BeginDraw();
            target.DrawBitmap(
                &self.bitmap,
                None,
                1.0,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                None,
            );
            brush.SetColor(&color(0.015, 0.025, 0.045, 0.57 * alpha));
            target.FillRectangle(&self.bounds, brush);
            if let Some(bounds) = selection {
                target.DrawBitmap(
                    &self.bitmap,
                    Some(&bounds),
                    1.0,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    Some(&bounds),
                );
                let rounded = D2D1_ROUNDED_RECT {
                    rect: bounds,
                    radiusX: 10.0,
                    radiusY: 10.0,
                };
                brush.SetColor(&color(0.15, 0.55, 1.0, 0.24 * alpha));
                target.DrawRoundedRectangle(&rounded, brush, 4.0, None);
                brush.SetColor(&color(0.96, 0.98, 1.0, 0.96 * alpha));
                target.DrawRoundedRectangle(&rounded, brush, 1.5, None);
            }
            let pill = D2D1_ROUNDED_RECT {
                rect: toolbar,
                radiusX: 20.0,
                radiusY: 20.0,
            };
            brush.SetColor(&color(0.065, 0.077, 0.097, 0.92 * alpha));
            target.FillRoundedRectangle(&pill, brush);
            brush.SetColor(&color(0.88, 0.91, 0.96, 0.27 * alpha));
            target.DrawRoundedRectangle(&pill, brush, 0.8, None);
            let x = toolbar.left + 24.0;
            let y = 34.0;
            brush.SetColor(&color(0.92, 0.95, 1.0, 0.96 * alpha));
            for (start, end) in [
                (point(x - 8.0, y - 2.0), point(x - 8.0, y - 7.0)),
                (point(x - 8.0, y - 7.0), point(x - 3.0, y - 7.0)),
                (point(x + 3.0, y + 7.0), point(x + 8.0, y + 7.0)),
                (point(x + 8.0, y + 7.0), point(x + 8.0, y + 2.0)),
            ] {
                target.DrawLine(start, end, brush, 1.7, None);
            }
            brush.SetColor(&color(
                0.85,
                0.88,
                0.92,
                if hovering { alpha } else { 0.75 * alpha },
            ));
            let x = toolbar.right - 22.0;
            target.DrawLine(
                point(x - 5.0, y - 5.0),
                point(x + 5.0, y + 5.0),
                brush,
                1.7,
                None,
            );
            target.DrawLine(
                point(x + 5.0, y - 5.0),
                point(x - 5.0, y + 5.0),
                brush,
                1.7,
                None,
            );
            target.EndDraw(None, None)
        }
    }
}

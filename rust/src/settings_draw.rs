use crate::settings::Settings;
use crate::win::wide;
use windows::{
    Foundation::Numerics::Matrix3x2,
    Win32::Graphics::{
        Direct2D::{Common::*, *},
        DirectWrite::*,
        Dxgi::Common::DXGI_FORMAT_B8G8R8A8_UNORM,
    },
    core::{Interface, w},
};
use windows_sys::Win32::{Foundation::HWND, UI::WindowsAndMessaging::GetClientRect};

pub type BoxRect = D2D_RECT_F;

pub fn rect(left: f32, top: f32, right: f32, bottom: f32) -> BoxRect {
    BoxRect {
        left,
        top,
        right,
        bottom,
    }
}

pub struct Drawing {
    target: ID2D1HwndRenderTarget,
    brush: ID2D1SolidColorBrush,
    fonts: IDWriteFactory5,
    loader: IDWriteInMemoryFontFileLoader,
    formats: [IDWriteTextFormat; 4],
}

const TITLE: usize = 0;
const SECTION: usize = 1;
const BODY: usize = 2;
const HINT: usize = 3;

impl Drawing {
    pub fn new(hwnd: HWND) -> windows::core::Result<Self> {
        unsafe {
            let mut client = std::mem::zeroed();
            GetClientRect(hwnd, &mut client);
            let factory: ID2D1Factory = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, None)?;
            let target = factory.CreateHwndRenderTarget(
                &D2D1_RENDER_TARGET_PROPERTIES {
                    r#type: D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                    pixelFormat: D2D1_PIXEL_FORMAT {
                        format: DXGI_FORMAT_B8G8R8A8_UNORM,
                        alphaMode: D2D1_ALPHA_MODE_IGNORE,
                    },
                    ..Default::default()
                },
                &D2D1_HWND_RENDER_TARGET_PROPERTIES {
                    hwnd: windows::Win32::Foundation::HWND(hwnd),
                    pixelSize: D2D_SIZE_U {
                        width: client.right as u32,
                        height: client.bottom as u32,
                    },
                    ..Default::default()
                },
            )?;
            let brush = target.CreateSolidColorBrush(&color(0, 1.0), None)?;
            let fonts: IDWriteFactory5 = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED)?;
            let loader = fonts.CreateInMemoryFontFileLoader()?;
            fonts.RegisterFontFileLoader(&loader)?;
            let formats = (|| {
                let font = include_bytes!("../../assets/fonts/Montserrat.ttf");
                let file = loader.CreateInMemoryFontFileReference(
                    &fonts,
                    font.as_ptr().cast(),
                    font.len() as u32,
                    None,
                )?;
                let builder = fonts.CreateFontSetBuilder()?;
                builder.AddFontFile(&file)?;
                let collection =
                    fonts.CreateFontCollectionFromFontSet(&builder.CreateFontSet()?)?;
                let format = |size, weight| {
                    fonts.CreateTextFormat(
                        w!("Montserrat"),
                        &collection,
                        weight,
                        DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL,
                        size,
                        w!("ru-ru"),
                    )
                };
                Ok::<_, windows::core::Error>([
                    format(18.0, DWRITE_FONT_WEIGHT_MEDIUM)?,
                    format(12.0, DWRITE_FONT_WEIGHT_SEMI_BOLD)?,
                    format(13.0, DWRITE_FONT_WEIGHT_NORMAL)?,
                    format(11.0, DWRITE_FONT_WEIGHT_NORMAL)?,
                ])
            })();
            match formats {
                Ok(formats) => Ok(Self {
                    target,
                    brush,
                    fonts,
                    loader,
                    formats,
                }),
                Err(error) => {
                    let _ = fonts.UnregisterFontFileLoader(&loader);
                    Err(error)
                }
            }
        }
    }

    pub fn paint(&self, s: &Settings) -> windows::core::Result<()> {
        let p = Palette::new(s.light);
        unsafe {
            self.target.SetDpi(96.0 * s.scale, 96.0 * s.scale);
            self.target.BeginDraw();
            self.target.Clear(Some(&color(p.background, 1.0)));
        }
        self.text(
            "SCREENSHOT.",
            rect(28.0, 20.0, 340.0, 49.0),
            TITLE,
            p.ink,
            false,
            1.6,
        );
        for (id, x) in [(10, 390.0), (11, 430.0)] {
            let r = rect(x - 16.0, 16.0, x + 16.0, 48.0);
            if s.hovered == id || s.pressed == id {
                let tint = match (id, s.light) {
                    (11, true) => 0xf6d1d9,
                    (11, false) => 0x54242f,
                    (_, true) => 0xededef,
                    _ => 0x202125,
                };
                self.round(r, 16.0, tint, true);
            }
            if id == 10 {
                self.line(x - 6.0, 32.0, x + 6.0, 32.0, p.ink);
            } else {
                self.line(x - 5.0, 27.0, x + 5.0, 37.0, p.ink);
                self.line(x + 5.0, 27.0, x - 5.0, 37.0, p.ink);
            }
            self.feedback(r, p.ink, false, false, s.keyboard_focus && s.focused == id);
        }

        let tr = |ru, en| s.tr(ru, en);
        let text = |value, r, font, rgb| self.text(value, r, font, rgb, false, 0.0);
        text(
            tr("Горячая клавиша", "Keyboard shortcut"),
            rect(28.0, 80.0, 432.0, 103.0),
            SECTION,
            p.ink,
        );
        let key = rect(28.0, 110.0, 432.0, 146.0);
        self.field(
            key,
            8.0,
            p.field,
            if s.listening { p.muted } else { p.border },
        );
        self.feedback_control(s, 0, key, p.ink);
        let shortcut = s.saved.name();
        self.text(
            if s.listening {
                tr("Нажмите сочетание…", "Press a shortcut…")
            } else {
                &shortcut
            },
            rect(40.0, 110.0, 420.0, 146.0),
            BODY,
            p.ink,
            true,
            0.0,
        );
        let message = if !s.status.is_empty() {
            &s.status
        } else if s.listening {
            tr("Esc — отмена", "Esc — cancel")
        } else {
            tr(
                "Нажмите поле, затем нужную клавишу или сочетание",
                "Click the field, then press a key or shortcut",
            )
        };
        text(message, rect(28.0, 155.0, 432.0, 195.0), HINT, p.muted);
        text(
            tr("Папка скриншотов", "Screenshot folder"),
            rect(28.0, 201.0, 432.0, 224.0),
            SECTION,
            p.ink,
        );
        self.button(
            s,
            p,
            8,
            rect(28.0, 232.0, 208.0, 268.0),
            tr("Открыть папку", "Open folder"),
            false,
        );
        text(
            tr("Внешний вид", "Appearance"),
            rect(28.0, 292.0, 432.0, 315.0),
            SECTION,
            p.ink,
        );
        text(
            tr("Тема", "Theme"),
            rect(28.0, 326.0, 432.0, 348.0),
            BODY,
            p.ink,
        );
        self.field(rect(28.0, 355.0, 432.0, 395.0), 8.0, p.field, p.border);
        for (index, caption) in [
            tr("Системная", "System"),
            tr("Светлая", "Light"),
            tr("Тёмная", "Dark"),
        ]
        .into_iter()
        .enumerate()
        {
            let id = 3 + index as i32;
            let left = 28.0 + index as f32 * (404.0 / 3.0);
            let r = rect(left + 2.0, 357.0, left + 404.0 / 3.0 - 2.0, 393.0);
            if s.theme == index as u32 {
                self.field(r, 6.0, p.surface, p.border);
            } else if s.hovered == id {
                self.round(r, 6.0, if s.light { 0xededef } else { 0x202125 }, true);
            }
            self.text(caption, r, HINT, p.ink, true, 0.0);
            self.feedback(
                r,
                p.muted,
                false,
                false,
                s.keyboard_focus && s.focused == id,
            );
        }
        text(
            tr("Язык интерфейса", "Interface language"),
            rect(28.0, 421.0, 285.0, 447.0),
            BODY,
            p.ink,
        );
        let language = rect(300.0, 415.0, 432.0, 451.0);
        self.field(language, 7.0, p.field, p.border);
        self.text(
            if s.english {
                "English"
            } else {
                "Русский"
            },
            rect(312.0, 415.0, 410.0, 451.0),
            BODY,
            p.ink,
            true,
            0.0,
        );
        self.line(414.0, 431.0, 418.0, 435.0, p.muted);
        self.line(418.0, 435.0, 422.0, 431.0, p.muted);
        self.feedback(
            language,
            p.ink,
            s.hovered == 6,
            false,
            s.keyboard_focus && s.focused == 6,
        );
        text(
            tr("О Screenshot", "About Screenshot"),
            rect(28.0, 478.0, 432.0, 501.0),
            SECTION,
            p.ink,
        );
        text(
            tr("Версия Screenshot", "Screenshot version"),
            rect(28.0, 515.0, 280.0, 539.0),
            BODY,
            p.ink,
        );
        self.text(
            "1.8.6 RE",
            rect(340.0, 515.0, 432.0, 539.0),
            BODY,
            p.ink,
            true,
            0.0,
        );
        text(
            tr("Разработчик", "Developer"),
            rect(28.0, 547.0, 280.0, 571.0),
            BODY,
            p.ink,
        );
        self.text(
            "Sander Stripa",
            rect(300.0, 547.0, 432.0, 571.0),
            BODY,
            p.muted,
            true,
            0.0,
        );
        self.button(
            s,
            p,
            9,
            rect(320.0, 580.0, 432.0, 612.0),
            tr("Обновить", "Update"),
            false,
        );
        self.text(
            "by connect.",
            rect(165.0, 611.0, 295.0, 633.0),
            HINT,
            if s.hovered != 12 {
                p.muted
            } else if s.light {
                0x55585d
            } else {
                0xb6b7bd
            },
            true,
            0.0,
        );
        if s.language_open {
            self.field(rect(298.0, 454.0, 434.0, 532.0), 8.0, p.surface, p.border);
            for (id, top, label) in [(13, 458.0, "Русский"), (14, 493.0, "English")] {
                let r = rect(302.0, top, 430.0, top + 35.0);
                if s.hovered == id || s.focused == id {
                    self.round(r, 6.0, p.field, true);
                }
                self.text(label, r, BODY, p.ink, true, 0.0);
            }
        }
        if s.confirmation || s.sheet_progress > 0.0 {
            self.sheet(s, p);
        }
        if !s.notice.is_empty() {
            self.round(rect(20.0, 72.0, 440.0, 304.0), 8.0, p.surface, true);
            text(&s.notice, rect(40.0, 100.0, 420.0, 232.0), BODY, p.ink);
            self.button(s, p, 1, rect(310.0, 248.0, 420.0, 284.0), "OK", true);
        }
        unsafe { self.target.EndDraw(None, None) }
    }

    fn text(&self, value: &str, r: BoxRect, font: usize, rgb: u32, centered: bool, spacing: f32) {
        let wide = wide(value);
        unsafe {
            let Ok(layout) = self.fonts.CreateTextLayout(
                &wide[..wide.len() - 1],
                &self.formats[font],
                r.right - r.left,
                r.bottom - r.top,
            ) else {
                return;
            };
            if centered {
                let _ = layout.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                let _ = layout.SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            if spacing != 0.0 {
                if let Ok(layout) = layout.cast::<IDWriteTextLayout1>() {
                    let _ = layout.SetCharacterSpacing(
                        spacing,
                        0.0,
                        0.0,
                        DWRITE_TEXT_RANGE {
                            startPosition: 0,
                            length: (wide.len() - 1) as u32,
                        },
                    );
                }
            }
            self.brush.SetColor(&color(rgb, 1.0));
            self.target.DrawTextLayout(
                D2D_POINT_2F {
                    x: r.left,
                    y: r.top,
                },
                &layout,
                &self.brush,
                D2D1_DRAW_TEXT_OPTIONS_CLIP,
            );
        }
    }

    fn line(&self, x: f32, y: f32, end_x: f32, end_y: f32, rgb: u32) {
        unsafe {
            self.brush.SetColor(&color(rgb, 1.0));
            self.target.DrawLine(
                D2D_POINT_2F { x, y },
                D2D_POINT_2F { x: end_x, y: end_y },
                &self.brush,
                1.25,
                None,
            );
        }
    }

    fn round(&self, r: BoxRect, radius: f32, rgb: u32, fill: bool) {
        let shape = D2D1_ROUNDED_RECT {
            rect: r,
            radiusX: radius,
            radiusY: radius,
        };
        unsafe {
            self.brush.SetColor(&color(rgb, 1.0));
            if fill {
                self.target.FillRoundedRectangle(&shape, &self.brush);
            } else {
                self.target
                    .DrawRoundedRectangle(&shape, &self.brush, 1.0, None);
            }
        }
    }

    fn field(&self, r: BoxRect, radius: f32, fill: u32, stroke: u32) {
        self.round(r, radius, fill, true);
        self.round(r, radius, stroke, false);
    }

    fn feedback_control(&self, s: &Settings, id: i32, r: BoxRect, rgb: u32) {
        self.feedback(
            r,
            rgb,
            s.hovered == id,
            s.pressed == id,
            s.keyboard_focus && s.focused == id,
        );
    }

    fn feedback(&self, r: BoxRect, rgb: u32, hover: bool, pressed: bool, focused: bool) {
        unsafe {
            if hover || pressed {
                self.brush
                    .SetColor(&color(rgb, if pressed { 0.12 } else { 0.06 }));
                self.target.FillRoundedRectangle(
                    &D2D1_ROUNDED_RECT {
                        rect: r,
                        radiusX: 8.0,
                        radiusY: 8.0,
                    },
                    &self.brush,
                );
            }
            if focused {
                self.brush.SetColor(&color(rgb, 1.0));
                self.target.DrawRoundedRectangle(
                    &D2D1_ROUNDED_RECT {
                        rect: rect(r.left - 3.0, r.top - 3.0, r.right + 3.0, r.bottom + 3.0),
                        radiusX: 10.0,
                        radiusY: 10.0,
                    },
                    &self.brush,
                    2.0,
                    None,
                );
            }
        }
    }

    fn button(&self, s: &Settings, p: Palette, id: i32, r: BoxRect, value: &str, primary: bool) {
        if primary {
            self.round(r, 8.0, p.ink, true);
        } else {
            self.field(r, 8.0, if s.light { 0xfcfcfd } else { 0x111215 }, p.border);
        }
        self.feedback_control(s, id, r, p.ink);
        self.text(
            value,
            r,
            BODY,
            if primary { p.background } else { p.ink },
            true,
            0.0,
        );
    }

    fn sheet(&self, s: &Settings, p: Palette) {
        let identity = Matrix3x2 {
            M11: 1.0,
            M22: 1.0,
            ..Default::default()
        };
        unsafe {
            if let Some(backdrop) = &s.backdrop {
                let pixels = D2D1_PIXEL_FORMAT {
                    format: DXGI_FORMAT_B8G8R8A8_UNORM,
                    alphaMode: D2D1_ALPHA_MODE_IGNORE,
                };
                if let Ok(bitmap) = self.target.CreateBitmap(
                    D2D_SIZE_U {
                        width: backdrop.width,
                        height: backdrop.height,
                    },
                    Some(backdrop.pixels.as_ptr().cast()),
                    backdrop.width * 4,
                    &D2D1_BITMAP_PROPERTIES {
                        pixelFormat: pixels,
                        dpiX: 96.0,
                        dpiY: 96.0,
                    },
                ) {
                    self.target.DrawBitmap(
                        &bitmap,
                        Some(&rect(0.0, 0.0, 460.0, 636.0)),
                        1.0,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                        None,
                    );
                }
            } else {
                self.brush.SetColor(&color(p.background, 0.90));
                self.target
                    .FillRectangle(&rect(0.0, 0.0, 460.0, 636.0), &self.brush);
            }
            self.target.SetTransform(&Matrix3x2 {
                M32: -340.0 * (1.0 - s.sheet_progress),
                ..identity
            });
            for i in (1..=20).rev() {
                let i = i as f32;
                self.brush
                    .SetColor(&color(0, if s.light { 0.012 } else { 0.02 }));
                self.target.FillRoundedRectangle(
                    &D2D1_ROUNDED_RECT {
                        rect: rect(-i * 0.5, 0.0, 460.0 + i * 0.5, 314.0 + i),
                        radiusX: 14.0,
                        radiusY: 14.0,
                    },
                    &self.brush,
                );
            }
        }
        self.round(rect(0.0, -20.0, 460.0, 314.0), 14.0, p.surface, true);
        self.text(
            "Print Screen",
            rect(28.0, 24.0, 432.0, 54.0),
            TITLE,
            p.ink,
            false,
            0.0,
        );
        self.text(
            s.tr(
                "Заменить системное действие клавиши?",
                "Replace the Windows key action?",
            ),
            rect(28.0, 76.0, 432.0, 119.0),
            SECTION,
            p.ink,
            false,
            0.0,
        );
        self.text(
            s.tr(
                "Print Screen будет открывать Screenshot вместо стандартного инструмента Windows.",
                "Print Screen will open Screenshot instead of the Windows capture tool.",
            ),
            rect(28.0, 127.0, 432.0, 178.0),
            BODY,
            p.ink,
            false,
            0.0,
        );
        self.text(s.tr("Windows может потребовать выхода из сеанса. Если появится системный запрос, подтвердите изменение в нём.",
            "Windows may require signing out. If Windows requests permission, confirm it in the system dialog."),
            rect(28.0, 187.0, 432.0, 240.0), HINT, p.muted, false, 0.0);
        self.button(
            s,
            p,
            15,
            rect(154.0, 258.0, 264.0, 294.0),
            s.tr("Отмена", "Cancel"),
            false,
        );
        self.button(
            s,
            p,
            1,
            rect(276.0, 258.0, 432.0, 294.0),
            s.tr("Да, заменить", "Yes, replace"),
            true,
        );
        unsafe { self.target.SetTransform(&identity) };
    }
}

impl Drop for Drawing {
    fn drop(&mut self) {
        unsafe {
            let _ = self.fonts.UnregisterFontFileLoader(&self.loader);
        }
    }
}

#[derive(Clone, Copy)]
struct Palette {
    ink: u32,
    muted: u32,
    border: u32,
    surface: u32,
    field: u32,
    background: u32,
}

impl Palette {
    fn new(light: bool) -> Self {
        if light {
            Self {
                ink: 0x242628,
                muted: 0x73767b,
                border: 0xe6e7e9,
                surface: 0xfcfcfd,
                field: 0xf3f4f5,
                background: 0xffffff,
            }
        } else {
            Self {
                ink: 0xeeeeef,
                muted: 0x96979d,
                border: 0x35363b,
                surface: 0x202124,
                field: 0x28292d,
                background: 0x08090b,
            }
        }
    }
}

fn color(rgb: u32, a: f32) -> D2D1_COLOR_F {
    D2D1_COLOR_F {
        r: ((rgb >> 16) & 255) as f32 / 255.0,
        g: ((rgb >> 8) & 255) as f32 / 255.0,
        b: (rgb & 255) as f32 / 255.0,
        a,
    }
}

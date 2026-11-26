use windows::{
    Win32::Graphics::{
        Direct2D::{Common::*, *},
        DirectWrite::*,
        Dxgi::Common::DXGI_FORMAT_B8G8R8A8_UNORM,
    },
    core::{Interface, w},
};
use windows_sys::Win32::{Foundation::HWND, UI::WindowsAndMessaging::GetClientRect};

pub type Rect = D2D_RECT_F;

pub fn rect(x: f32, y: f32, width: f32, height: f32) -> Rect {
    Rect {
        left: x,
        top: y,
        right: x + width,
        bottom: y + height,
    }
}

pub fn contains(area: Rect, x: f32, y: f32) -> bool {
    (area.left..area.right).contains(&x) && (area.top..area.bottom).contains(&y)
}

pub struct Drawing {
    pub target: ID2D1HwndRenderTarget,
    brush: ID2D1SolidColorBrush,
    fonts: IDWriteFactory5,
    loader: IDWriteInMemoryFontFileLoader,
    collection: IDWriteFontCollection1,
    ribbon: ID2D1Bitmap,
    aspect: f32,
}

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
            let mut ribbon = image::load_from_memory(include_bytes!("../payload/ribbon.png"))
                .map_err(|_| {
                    windows::core::Error::from_hresult(windows::core::HRESULT(
                        0x80004005_u32 as i32,
                    ))
                })?
                .into_rgba8();
            let (width, height) = ribbon.dimensions();
            for pixel in ribbon.pixels_mut() {
                let [r, g, b, a] = pixel.0;
                let alpha = u16::from(a);
                let multiply = |v: u8| ((u16::from(v) * alpha + 127) / 255) as u8;
                pixel.0 = [multiply(b), multiply(g), multiply(r), a];
            }
            let ribbon = target.CreateBitmap(
                D2D_SIZE_U { width, height },
                Some(ribbon.as_ptr().cast()),
                width * 4,
                &D2D1_BITMAP_PROPERTIES {
                    pixelFormat: D2D1_PIXEL_FORMAT {
                        format: DXGI_FORMAT_B8G8R8A8_UNORM,
                        alphaMode: D2D1_ALPHA_MODE_PREMULTIPLIED,
                    },
                    dpiX: 96.0,
                    dpiY: 96.0,
                },
            )?;
            let fonts: IDWriteFactory5 = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED)?;
            let loader = fonts.CreateInMemoryFontFileLoader()?;
            fonts.RegisterFontFileLoader(&loader)?;
            let font = include_bytes!("../../../assets/fonts/Montserrat.ttf");
            let file = loader.CreateInMemoryFontFileReference(
                &fonts,
                font.as_ptr().cast(),
                font.len() as u32,
                None,
            )?;
            let builder = fonts.CreateFontSetBuilder()?;
            builder.AddFontFile(&file)?;
            let collection = fonts.CreateFontCollectionFromFontSet(&builder.CreateFontSet()?)?;
            Ok(Self {
                target,
                brush,
                fonts,
                loader,
                collection,
                ribbon,
                aspect: width as f32 / height as f32,
            })
        }
    }

    pub fn begin(&self, scale: f32, background: u32) {
        unsafe {
            self.target.SetDpi(96.0 * scale, 96.0 * scale);
            self.target.BeginDraw();
            self.target.Clear(Some(&color(background, 1.0)));
        }
    }

    pub fn end(&self) -> bool {
        unsafe { self.target.EndDraw(None, None).is_ok() }
    }

    pub fn ribbon(&self, area: Rect) {
        let (w, h) = (area.right - area.left, area.bottom - area.top);
        let width = w.min(h * self.aspect);
        let height = width / self.aspect;
        let destination = rect(
            area.left + (w - width) / 2.0,
            area.top + (h - height) / 2.0,
            width,
            height,
        );
        unsafe {
            self.target.DrawBitmap(
                &self.ribbon,
                Some(&destination),
                1.0,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                None,
            );
        }
    }

    pub fn text(
        &self,
        text: &str,
        area: Rect,
        size: f32,
        weight: u32,
        ink: u32,
        align: DWRITE_TEXT_ALIGNMENT,
        spacing: f32,
    ) {
        unsafe {
            let Ok(format) = self.fonts.CreateTextFormat(
                w!("Montserrat"),
                &self.collection,
                DWRITE_FONT_WEIGHT(weight as i32),
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                w!("ru-ru"),
            ) else {
                return;
            };
            let _ = format.SetTextAlignment(align);
            let _ = format.SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            let text: Vec<u16> = text.encode_utf16().collect();
            let Ok(layout) = self.fonts.CreateTextLayout(
                &text,
                &format,
                area.right - area.left,
                area.bottom - area.top,
            ) else {
                return;
            };
            if spacing != 0.0 {
                if let Ok(layout) = layout.cast::<IDWriteTextLayout1>() {
                    let _ = layout.SetCharacterSpacing(
                        0.0,
                        spacing,
                        0.0,
                        DWRITE_TEXT_RANGE {
                            startPosition: 0,
                            length: text.len() as u32,
                        },
                    );
                }
            }
            self.brush.SetColor(&color(ink, 1.0));
            self.target.DrawTextLayout(
                D2D_POINT_2F {
                    x: area.left,
                    y: area.top,
                },
                &layout,
                &self.brush,
                D2D1_DRAW_TEXT_OPTIONS_CLIP,
            );
        }
    }

    pub fn round(&self, area: Rect, radius: f32, fill: Option<u32>, border: Option<u32>) {
        let shape = D2D1_ROUNDED_RECT {
            rect: area,
            radiusX: radius,
            radiusY: radius,
        };
        unsafe {
            if let Some(fill) = fill {
                self.brush.SetColor(&color(fill, 1.0));
                self.target.FillRoundedRectangle(&shape, &self.brush);
            }
            if let Some(border) = border {
                self.brush.SetColor(&color(border, 1.0));
                self.target
                    .DrawRoundedRectangle(&shape, &self.brush, 1.0, None);
            }
        }
    }

    pub fn line(&self, ax: f32, ay: f32, bx: f32, by: f32, width: f32, ink: u32) {
        unsafe {
            self.brush.SetColor(&color(ink, 1.0));
            self.target.DrawLine(
                D2D_POINT_2F { x: ax, y: ay },
                D2D_POINT_2F { x: bx, y: by },
                &self.brush,
                width,
                None,
            );
        }
    }
}

impl Drop for Drawing {
    fn drop(&mut self) {
        unsafe {
            let _ = self.fonts.UnregisterFontFileLoader(&self.loader);
        }
    }
}

fn color(rgb: u32, a: f32) -> D2D1_COLOR_F {
    let channel = |shift| ((rgb >> shift) & 255_u32) as f32 / 255.0;
    D2D1_COLOR_F {
        r: channel(16),
        g: channel(8),
        b: channel(0),
        a,
    }
}

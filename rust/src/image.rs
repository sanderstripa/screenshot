use std::io;

pub const MAX_PIXELS: usize = 100_000_000;
const PADDING: usize = 24;

/// A top-down image with straight alpha.
pub struct Image {
    pub width: usize,
    pub height: usize,
    pub rgba: Vec<u8>,
}

#[derive(Clone, Copy)]
pub struct Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

impl Rect {
    pub fn intersection(self, other: Self) -> Option<Self> {
        let overlap = Self {
            left: self.left.max(other.left),
            top: self.top.max(other.top),
            right: self.right.min(other.right),
            bottom: self.bottom.min(other.bottom),
        };
        (overlap.right > overlap.left && overlap.bottom > overlap.top).then_some(overlap)
    }
}

impl Image {
    pub fn new(width: usize, height: usize) -> Option<Self> {
        let pixels = width.checked_mul(height)?;
        if pixels == 0 || pixels > MAX_PIXELS {
            return None;
        }
        let mut rgba = Vec::new();
        rgba.try_reserve_exact(pixels * 4).ok()?;
        rgba.resize(pixels * 4, 0);
        Some(Self {
            width,
            height,
            rgba,
        })
    }

    pub fn styled_capture(&self, desktop: Rect, selection: Rect) -> Option<Self> {
        let crop = desktop.intersection(selection)?;
        let width = (crop.right - crop.left) as usize;
        let height = (crop.bottom - crop.top) as usize;
        let source_x = (crop.left - desktop.left) as usize;
        let source_y = (crop.top - desktop.top) as usize;
        if source_x + width > self.width || source_y + height > self.height {
            return None;
        }

        let mut result = Self::new(width + PADDING * 2, height + PADDING * 2)?;
        let (w, h) = (width as f32, height as f32);
        let radius = 12.0_f32.min(w * 0.18).min(h * 0.18);
        for y in 0..result.height {
            for x in 0..result.width {
                let fx = x as f32 - PADDING as f32 + 0.5;
                let fy = y as f32 - PADDING as f32 + 0.5;
                let distance = rounded_distance(fx, fy, w, h, radius);
                let falloff = rounded_distance(fx, fy - 4.0, w, h, radius).max(0.0) / 11.0;
                let shadow = 0.23 * (-0.5 * falloff * falloff).exp();
                let dest = (y * result.width + x) * 4;
                result.rgba[dest + 3] = (255.0 * shadow) as u8;
                if (PADDING..PADDING + width).contains(&x)
                    && (PADDING..PADDING + height).contains(&y)
                {
                    let coverage = (0.5 - distance).clamp(0.0, 1.0);
                    if coverage > 0.0 {
                        let source =
                            ((source_y + y - PADDING) * self.width + source_x + x - PADDING) * 4;
                        result.rgba[dest..dest + 3].copy_from_slice(&self.rgba[source..source + 3]);
                        result.rgba[dest + 3] =
                            (255.0 * (coverage + (1.0 - coverage) * shadow)) as u8;
                    }
                }
            }
        }
        Some(result)
    }

    pub fn png(&self) -> io::Result<Vec<u8>> {
        let mut bytes = Vec::new();
        let mut encoder = png::Encoder::new(&mut bytes, self.width as u32, self.height as u32);
        encoder.set_color(png::ColorType::Rgba);
        encoder.set_depth(png::BitDepth::Eight);
        let mut writer = encoder.write_header().map_err(io::Error::other)?;
        writer
            .write_image_data(&self.rgba)
            .map_err(io::Error::other)?;
        writer.finish().map_err(io::Error::other)?;
        Ok(bytes)
    }
}

fn rounded_distance(x: f32, y: f32, w: f32, h: f32, radius: f32) -> f32 {
    let qx = (x - w * 0.5).abs() - (w * 0.5 - radius);
    let qy = (y - h * 0.5).abs() - (h * 0.5 - radius);
    qx.max(0.0).hypot(qy.max(0.0)) + qx.max(qy).min(0.0) - radius
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clips_to_virtual_desktop_and_encodes_styled_pixels() {
        let bounds = Rect {
            left: -80,
            top: -40,
            right: 0,
            bottom: 0,
        };
        let mut desktop = Image::new(80, 40).unwrap();
        for pixel in desktop.rgba.chunks_exact_mut(4) {
            pixel.copy_from_slice(&[15, 125, 235, 255]);
        }
        let selection = Rect {
            left: -100,
            top: -30,
            right: -30,
            bottom: -10,
        };
        let styled = desktop.styled_capture(bounds, selection).unwrap();
        assert_eq!((styled.width, styled.height), (98, 68));
        let center = (34 * styled.width + 49) * 4;
        assert_eq!(&styled.rgba[center..center + 4], &[15, 125, 235, 255]);
        assert!(styled.rgba[3] < styled.rgba[((styled.height - 5) * styled.width + 49) * 4 + 3]);
        assert!(
            desktop
                .styled_capture(
                    bounds,
                    Rect {
                        left: 10,
                        top: 10,
                        right: 20,
                        bottom: 20,
                    }
                )
                .is_none()
        );

        let bytes = styled.png().unwrap();
        let mut decoder = png::Decoder::new(bytes.as_slice()).read_info().unwrap();
        let mut decoded = vec![0; decoder.output_buffer_size()];
        decoder.next_frame(&mut decoded).unwrap();
        assert_eq!(decoded, styled.rgba);
    }
}

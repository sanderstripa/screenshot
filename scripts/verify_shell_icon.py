"""Compare the Windows shell's resolved shortcut artwork, excluding its system overlay."""
from pathlib import Path
import sys
from PIL import Image, ImageChops, ImageDraw, ImageStat

source = Image.open(sys.argv[1])
directory = Path(sys.argv[2])
for size in (16, 32):
    actual = Image.open(directory / f"shell-{size}.png").convert("RGBA")
    expected = source.ico.getimage((size, size)).convert("RGBA")
    if actual.size != expected.size:
        raise RuntimeError("Unexpected Windows shell icon size")
    mask = Image.new("L", actual.size, 255)
    ImageDraw.Draw(mask).rectangle((0, size // 2, size // 2, size), fill=0)
    for background in ("#f2f0eb", "#202122"):
        canvas = Image.new("RGBA", actual.size, background)
        difference = ImageChops.difference(Image.alpha_composite(canvas, actual), Image.alpha_composite(canvas, expected)).convert("RGB")
        error = sum(ImageStat.Stat(difference, mask).mean) / 3
        if error >= 2:
            raise RuntimeError(f"Windows resolves stale shortcut artwork at {size}px: {error}")
    print(f"PASS actual Windows Start menu shell artwork at {size}px")

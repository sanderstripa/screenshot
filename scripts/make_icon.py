"""Reproducible premium Screenshot icon: dark tile, overlapping windows, blue corner."""
from PIL import Image, ImageDraw
from pathlib import Path
import sys

DEST=Path(sys.argv[1]) if len(sys.argv)>1 else Path('assets')
DEST.mkdir(parents=True,exist_ok=True)
S=2
N=512*S
p=lambda b:tuple(int(v*S) for v in b)
base=Image.new('RGBA',(N,N),(0,0,0,0))
mask=Image.new('L',(N,N),0)
ImageDraw.Draw(mask).rounded_rectangle(p((27,27,485,485)),radius=98*S,fill=255)
surface=Image.new('RGBA',(N,N));pixels=surface.load()
for y in range(N):
    for x in range(N):
        t=min(1.,max(0.,(x+y)/(N*2)))
        pixels[x,y]=(int(21+5*t),int(30+9*t),int(45+15*t),255)
base.paste(surface,(0,0),mask)
over=Image.new('RGBA',(N,N),(0,0,0,0))
d=ImageDraw.Draw(over)
d.rounded_rectangle(p((205,144,420,371)),radius=42*S,
                    fill=(66,83,105,220),outline=(128,152,182,110),width=2*S)
face=Image.new('RGBA',(N,N),(0,0,0,0));fm=Image.new('L',(N,N),0)
ImageDraw.Draw(fm).rounded_rectangle(p((105,202,352,411)),radius=43*S,fill=255)
fp=face.load()
for y in range(202*S,412*S):
    for x in range(105*S,353*S):
        t=(x-105*S)/(247*S)*.3+(y-202*S)/(209*S)*.7
        fp[x,y]=(int(231-37*t),int(240-31*t),int(251-17*t),255)
over.paste(face,(0,0),fm)
d=ImageDraw.Draw(over)
points=[(225,411),(251,397),(272,369),(289,339),(314,322),(339,304),(352,289),(352,411)]
d.polygon([tuple(int(v*S) for v in pt) for pt in points],fill=(22,111,240,255))
for points,width in [([(122,265),(122,232),(156,232)],19),
                     ([(311,388),(335,388),(335,361)],17)]:
    d.line([tuple(int(v*S) for v in pt) for pt in points],
           fill=(255,255,255,255),width=width*S,joint='curve')
for cx,cy,r in [(122,265,9.5),(156,232,9.5),(311,388,8.5),(335,361,8.5)]:
    d.ellipse(p((cx-r,cy-r,cx+r,cy+r)),fill='white')
base=Image.alpha_composite(base,over)
base.resize((256,256),Image.Resampling.LANCZOS).save(DEST/'Screenshot.png')
frames=[base.resize((n,n),Image.Resampling.LANCZOS)
        for n in (256,128,64,48,32,24,16)]
frames[0].save(DEST/'Screenshot.ico',format='ICO',
               sizes=[(n,n) for n in (256,128,64,48,32,24,16)],
               append_images=frames[1:])
print(f"Screenshot artwork: {(DEST/'Screenshot.ico').stat().st_size} bytes")

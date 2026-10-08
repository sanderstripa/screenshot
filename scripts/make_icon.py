"""Package approved artwork into ICO frames; never draw replacement artwork."""
from pathlib import Path
import sys
from PIL import Image

directory=Path(sys.argv[1]) if len(sys.argv)>1 else Path('assets')
master=Image.open(directory/'Screenshot.png').convert('RGBA')
sizes=(16,20,24,32,40,48,64,128,256)
if master.width!=master.height or master.width<256:
    raise SystemExit('A square high-resolution approved master is required')
if master.getchannel('A').getextrema()[0]!=0:
    raise SystemExit('Master must have transparent exterior corners')
master.save(directory/'Screenshot.ico',format='ICO',sizes=[(n,n) for n in sizes])
icon=Image.open(directory/'Screenshot.ico')
if icon.ico.sizes()!={(n,n) for n in sizes}:
    raise SystemExit('ICO frame set is incomplete')
print('Approved artwork packaged:',', '.join(map(str,sizes)))

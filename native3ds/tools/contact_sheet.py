"""contact_sheet.py OUT.png IN.png... : tile screenshots (cropped of the title bar) 2 per row with captions."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw
ims = [(Path(p).stem, Image.open(p).convert("RGB")) for p in sys.argv[2:]]
w, h = 675, 396
cols = 2
rows = (len(ims) + 1) // 2
sheet = Image.new("RGB", (w * cols, h * rows), (30, 30, 30))
for i, (n, im) in enumerate(ims):
    t = im.crop((0, 26, im.width, im.height)).resize((w, h - 14))
    sheet.paste(t, ((i % cols) * w, (i // cols) * h + 14))
    ImageDraw.Draw(sheet).text(((i % cols) * w + 4, (i // cols) * h + 1), n, fill=(255, 255, 0))
sheet.save(sys.argv[1])

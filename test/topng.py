"""Stitch the rendered .pbm frames into one scaled PNG contact sheet."""
import glob
import os

from PIL import Image, ImageDraw

SCALE = 3
COLS = 3

PAD = 8
LABEL_H = 11

import sys

prefix = sys.argv[1] if len(sys.argv) > 1 else ""
pattern = f"preview/{prefix}*.pbm" if prefix else "preview/[0-9]*.pbm"
frames = sorted(glob.glob(pattern))
if not frames:
    raise SystemExit("no frames; run the preview binary first")

tiles = []
for path in frames:
    with open(path) as fh:
        tok = fh.read().split()
    w, h = int(tok[1]), int(tok[2])
    bits = "".join(tok[3:])
    img = Image.new("L", (w, h), 255)
    px = img.load()
    for y in range(h):
        for x in range(w):
            if bits[y * w + x] == "1":
                px[x, y] = 0
    img = img.resize((w * SCALE, h * SCALE), Image.NEAREST)
    tiles.append((os.path.basename(path)[:-4], img))

tw, th = tiles[0][1].size
rows = (len(tiles) + COLS - 1) // COLS
sheet = Image.new("L", (COLS * (tw + PAD) + PAD, rows * (th + PAD + LABEL_H) + PAD), 200)
draw = ImageDraw.Draw(sheet)

for i, (name, img) in enumerate(tiles):
    cx = PAD + (i % COLS) * (tw + PAD)
    cy = PAD + (i // COLS) * (th + PAD + LABEL_H)
    draw.text((cx, cy), name, fill=0)
    sheet.paste(img, (cx, cy + LABEL_H))
    draw.rectangle([cx - 1, cy + LABEL_H - 1, cx + tw, cy + LABEL_H + th], outline=0)

out = f"preview/{prefix or 'sheet'}.png"
sheet.save(out)
print(f"  wrote {out} ({len(tiles)} frames)")

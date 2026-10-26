"""Contact sheet: each concept at 256/64/32/16 px on a dark and a light panel strip.
Usage: python3 sheet.py <dir with NN-name-PX.png> <out.png> [current-icon-256.png]"""
import sys, os, glob
from PIL import Image, ImageDraw, ImageFont

src, out = sys.argv[1], sys.argv[2]
names = sorted({os.path.basename(p).rsplit("-", 1)[0] for p in glob.glob(os.path.join(src, "[0-9]*-256.png"))})
rows = [("current", sys.argv[3])] if len(sys.argv) > 3 else []
rows += [(n, os.path.join(src, n + "-256.png")) for n in names]
W, RH = 760, 300
sheet = Image.new("RGB", (W, RH * len(rows)), "#f4f2ee")
d = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 22)
except OSError:
    font = ImageFont.load_default()
for i, (name, big) in enumerate(rows):
    y = i * RH
    d.text((24, y + 14), name, fill="#222226", font=font)
    sheet.paste(Image.open(big).convert("RGBA").resize((256, 256)), (24, y + 40), Image.open(big).convert("RGBA").resize((256, 256)))
    for j, (bg, x0) in enumerate((("#222226", 310), ("#e8e8ea", 530))):
        d.rounded_rectangle((x0, y + 40, x0 + 200, y + 296), 12, fill=bg)
        yy = y + 56
        for px in (64, 32, 16):
            path = os.path.join(src, f"{name}-{px}.png") if name != "current" else big
            im = Image.open(path).convert("RGBA").resize((px, px), Image.LANCZOS)
            sheet.paste(im, (x0 + 100 - px // 2, yy), im)
            yy += px + 24
sheet.save(out)
print("sheet", out, sheet.size)

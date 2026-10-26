"""Contact sheet of a native frame dump: each resolve as native | emulator (when decoded).

usage: python3 dump_sheet.py DUMP_DIR [first] [count]
"""
import os, sys
from PIL import Image, ImageDraw

D = sys.argv[1]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
count = int(sys.argv[3]) if len(sys.argv) > 3 else 40
nat = sorted(f for f in os.listdir(D) if f.endswith('.ppm') and not f.endswith('_emu.ppm'))[first:first + count]
cols, tw, th = 4, 200, 112
rows = (len(nat) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (2 * tw + 8), rows * (th + 12)), (40, 40, 40))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(nat):
    x, y = (i % cols) * (2 * tw + 8), (i // cols) * (th + 12)
    sheet.paste(Image.open(f'{D}/{f}').resize((tw, th)), (x, y + 12))
    emu = f'{D}/{f[:3]}_emu.ppm'
    if os.path.exists(emu):
        sheet.paste(Image.open(emu).resize((tw, th)), (x + tw, y + 12))
    d.text((x + 2, y), f[:-4][:28], fill=(255, 255, 0))
out = f'{D}/sheet_{first}.png'
sheet.save(out)
print(out)

"""Generate placeholder 320x180 'thumbnails' for UI previews (YouTube's image server is not
reachable from the dev container). usage: python3 tests/make_preview_thumbs.py OUT_DIR"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
ITEMS = [
    ("t0", (30, 90, 200), (120, 200, 255), "WII SPORTS", "EVERY GAME"),
    ("t1", (200, 40, 40), (255, 160, 60), "RESORT", "TRAILER"),
    ("t2", (20, 20, 20), (90, 90, 90), "REVIEW", "8/10"),
    ("t3", (40, 150, 90), (200, 240, 120), "DESERVED", "BETTER"),
    ("t4", (90, 40, 160), (220, 120, 255), "LONGPLAY", "1:17:13"),
    ("t5", (230, 120, 20), (255, 230, 90), "CAN YOU", "LOSE?"),
    ("t6", (20, 120, 160), (90, 220, 220), "CHAMPION", "LUCIA"),
    ("t7", (160, 30, 90), (255, 120, 160), "20 YEARS", "LATER"),
    ("t8", (60, 60, 140), (150, 150, 230), "OBSCURE", "SECRETS"),
    ("t9", (10, 90, 40), (60, 200, 110), "BOSSES", "ALL OF THEM"),
    ("t10", (120, 70, 20), (230, 170, 90), "BETA", "MOTIONPLUS"),
    ("t11", (40, 40, 40), (200, 60, 60), "THE DEATH", "OF WII SPORTS"),
]
for name, c0, c1, line1, line2 in ITEMS:
    im = Image.new("RGB", (320, 180))
    px = im.load()
    for y in range(180):
        for x in range(320):
            t = (x / 320 + y / 180) / 2
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(c0, c1))
    d = ImageDraw.Draw(im)
    big = ImageFont.truetype(BOLD, 38)
    small = ImageFont.truetype(BOLD, 24)
    for text, font, y in ((line1, big, 46), (line2, small, 100)):
        w = d.textlength(text, font=font)
        d.text(((320 - w) / 2 + 3, y + 3), text, font=font, fill=(0, 0, 0))
        d.text(((320 - w) / 2, y), text, font=font, fill=(255, 255, 255))
    im.save(os.path.join(OUT, name + ".jpg"), quality=85)
print("thumbnails in", OUT)

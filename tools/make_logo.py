"""Draw the WiiTube logo and write the app's copies of it.

usage: python3 tools/make_logo.py

The logo is drawn from shapes (in the style of assets/logo_source.webp: grey rounded "Wii"
with ring-dotted i's, white "Tube" on a glossy red rounded box), at 4x the largest size and
then scaled down, so every size has smooth edges instead of enlarged pixels.

Writes:
  assets/logo.png                 transparent, 1200 px wide (docs, README)
  wii/icon.png                    128x48 Homebrew Channel icon
  src/platform/wii/logo_data.c    RGBA copies drawn by the app: header (40 px tall) and
                                  splash (288 px wide)
"""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TUBE_FONTS = [  # bold grotesque for "Tube"; first one found is used
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]

H = 1000  # height of the red box in the master drawing; everything is relative to it


def vgradient(size, top, bottom):
    w, h = size
    col = Image.new("RGBA", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        col.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return col.resize((w, h))


def wii_mask(height):
    """The grey 'Wii' letters as a mask, letter height = height."""
    stroke = int(height * 0.23)
    r = stroke // 2
    w_width = int(height * 1.24)
    i_gap = int(height * 0.11)
    total = w_width + 2 * (i_gap + stroke) + r * 4
    m = Image.new("L", (total + 2 * r, height + 2 * r), 0)
    d = ImageDraw.Draw(m)
    top, bottom = r, height - r
    x0 = r + r
    # W: a single rounded stroke down-up-down-up, the middle peak as tall as the sides.
    pts = [(x0, top), (x0 + w_width * 0.25, bottom), (x0 + w_width * 0.5, top + height * 0.06),
           (x0 + w_width * 0.75, bottom), (x0 + w_width, top)]
    pts = [(int(x), int(y)) for x, y in pts]
    d.line(pts, fill=255, width=stroke, joint="curve")
    for x, y in pts:
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    # Two i's: rounded bars with a ring dot above each.
    x = x0 + w_width + r + i_gap + r
    for _ in range(2):
        bar_top = top + int(height * 0.36)
        d.rounded_rectangle((x - r, bar_top - r, x + r, bottom + r), radius=r, fill=255)
        rr = int(height * 0.125)
        cy = rr
        d.ellipse((x - rr, cy - rr, x + rr, cy + rr), fill=255)
        x += stroke + i_gap
    return m.crop(m.getbbox())


def grey_letters(mask):
    """Fill the letters with a light-to-mid grey gradient and a darker grey rim."""
    w, h = mask.size
    pad = int(h * 0.04)
    big = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
    big.paste(mask, (pad, pad))
    rim = big.filter(ImageFilter.MaxFilter(2 * (pad // 2) + 1))
    inner = big.filter(ImageFilter.MinFilter(2 * (pad // 2) + 1))
    out = Image.new("RGBA", big.size, (0, 0, 0, 0))
    edge = vgradient(big.size, (128, 128, 128, 255), (104, 104, 104, 255))
    out.paste(edge, (0, 0), rim)
    body = vgradient(big.size, (200, 200, 200, 255), (150, 150, 150, 255))
    out.paste(body, (0, 0), inner)
    # A soft white highlight along the top of each stroke (the original's bevel).
    hl = ImageChops.subtract(inner, inner.transform(inner.size, Image.AFFINE, (1, 0, 0, 0, 1, -pad), fillcolor=0))
    hl = hl.filter(ImageFilter.GaussianBlur(pad * 0.6)).point(lambda v: v * 0.55)
    out.paste(Image.new("RGBA", big.size, (255, 255, 255, 255)), (0, 0), ImageChops.multiply(hl, inner))
    return out


def tube_text(height, font_path):
    """White 'Tube', cap height = height, slightly condensed like the original."""
    f = ImageFont.truetype(font_path, int(height * 1.38))
    box = f.getbbox("Tube", anchor="ls")
    img = Image.new("L", (box[2] - box[0] + 4, box[3] - box[1] + 4), 0)
    ImageDraw.Draw(img).text((-box[0] + 2, -box[1] + 2), "Tube", font=f, fill=255, anchor="ls")
    img = img.crop(img.getbbox())
    img = img.resize((int(img.width * 0.86), img.height), Image.LANCZOS)
    return img


def red_box(w, h):
    radius = int(h * 0.24)
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), radius=radius, fill=255)
    box = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    box.paste(vgradient((w, h), (246, 62, 58, 255), (178, 8, 12, 255)), (0, 0), mask)
    # Gloss: a lighter band over the top half, fading downwards.
    gloss = Image.new("L", (w, h), 0)
    gd = ImageDraw.Draw(gloss)
    gd.rounded_rectangle((int(h * 0.05), int(h * 0.04), w - int(h * 0.05), int(h * 0.5)), radius=int(radius * 0.8),
                         fill=70)
    gloss = gloss.filter(ImageFilter.GaussianBlur(h * 0.03))
    fade = vgradient((w, h), (255, 255, 255, 255), (0, 0, 0, 255)).convert("L")
    gloss = ImageChops.multiply(gloss, fade)
    box.paste(Image.new("RGBA", (w, h), (255, 255, 255, 255)), (0, 0), ImageChops.multiply(gloss, mask))
    # Shine in the top-right corner, as in the original.
    shine = Image.new("L", (w, h), 0)
    ImageDraw.Draw(shine).ellipse((w - int(h * 0.42), int(h * 0.03), w - int(h * 0.08), int(h * 0.2)), fill=170)
    shine = shine.filter(ImageFilter.GaussianBlur(h * 0.035))
    box.paste(Image.new("RGBA", (w, h), (255, 236, 236, 255)), (0, 0), ImageChops.multiply(shine, mask))
    # Darker rim.
    rim = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(2 * int(h * 0.012) + 1)))
    box.paste(Image.new("RGBA", (w, h), (150, 6, 10, 255)), (0, 0), rim)
    return box


def draw_master():
    font = next(p for p in TUBE_FONTS if os.path.exists(p))
    letters = grey_letters(wii_mask(int(H * 0.74)))
    tube = tube_text(int(H * 0.60), font)
    pad_x = int(H * 0.14)
    box_w = tube.width + 2 * pad_x
    gap = int(H * 0.05)
    w = letters.width + gap + box_w
    img = Image.new("RGBA", (w, H), (0, 0, 0, 0))
    img.alpha_composite(letters, (0, (H - letters.height) // 2))
    bx = letters.width + gap
    img.alpha_composite(red_box(box_w, H), (bx, 0))
    # "Tube" with a faint dark-red shadow for depth.
    ty = (H - tube.height) // 2 + int(H * 0.01)
    shadow = tube.filter(ImageFilter.GaussianBlur(H * 0.01)).point(lambda v: v * 0.45)
    img.paste(Image.new("RGBA", tube.size, (90, 0, 0, 255)), (bx + pad_x + int(H * 0.012), ty + int(H * 0.018)),
              shadow)
    white = Image.new("RGBA", tube.size, (255, 255, 255, 255))
    img.paste(white, (bx + pad_x, ty), tube)
    return img


def resize(img, width=None, height=None):
    w, h = img.size
    if width is None:
        width = max(1, round(w * height / h))
    if height is None:
        height = max(1, round(h * width / w))
    # Resample premultiplied so transparent pixels don't bleed dark fringes; two steps keep
    # thin features when shrinking a lot.
    pm = img.convert("RGBa")
    while pm.width > width * 3:
        pm = pm.resize((pm.width // 2, pm.height // 2), Image.BOX)
    return pm.resize((width, height), Image.LANCZOS).convert("RGBA")


def c_array(name, img):
    w, h = img.size
    data = img.tobytes()
    lines = [f"const int {name}_w = {w}, {name}_h = {h};",
             f"const unsigned char {name}_rgba[{w * h * 4}] = {{"]
    for i in range(0, len(data), 24):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    logo = draw_master()
    resize(logo, width=1200).save(os.path.join(ROOT, "assets", "logo.png"), optimize=True)

    icon = Image.new("RGBA", (128, 48), (0, 0, 0, 0))
    fit = resize(logo, width=124) if logo.width / logo.height > 124 / 44 else resize(logo, height=44)
    icon.alpha_composite(fit, ((128 - fit.width) // 2, (48 - fit.height) // 2))
    icon.save(os.path.join(ROOT, "wii", "icon.png"), optimize=True)

    small = resize(logo, height=40)   # header (drawn 30 px tall on the 480-line canvas)
    splash = resize(logo, width=288)  # startup / loading screens
    with open(os.path.join(ROOT, "src", "platform", "wii", "logo_data.c"), "w") as f:
        f.write("/* Generated by tools/make_logo.py - do not edit. */\n")
        f.write(c_array("logo_small", small) + "\n\n")
        f.write(c_array("logo_splash", splash) + "\n")
    print("master", logo.size, "icon 128x48", "small", small.size, "splash", splash.size)


if __name__ == "__main__":
    main()

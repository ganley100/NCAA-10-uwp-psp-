"""Render the UWP tile/logo assets in uwp/Assets (original artwork, no game data).

    python3 uwp/make_assets.py
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = Path(__file__).resolve().parent / 'Assets'
SS = 4  # supersampling factor
FONT = 'DejaVuSans-Bold.ttf'


def font(size):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def field(w, h):
    img = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):  # vertical gradient, darker at the bottom
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=(int(28 - 12 * t), int(110 - 50 * t), int(52 - 22 * t), 255))
    marks = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    m = ImageDraw.Draw(marks)
    stripe = max(1, w // 10)
    for i in range(0, 10, 2):  # mowed stripes
        m.rectangle([i * stripe, 0, (i + 1) * stripe, h], fill=(255, 255, 255, 16))
    for i in range(1, 10):  # yard lines
        x = i * w // 10
        m.line([(x, 0), (x, h)], fill=(255, 255, 255, 60), width=max(1, w // 160))
    img.alpha_composite(marks)
    # vignette
    v = Image.new('L', (w, h), 0)
    ImageDraw.Draw(v).ellipse([-w * 0.25, -h * 0.25, w * 1.25, h * 1.25], fill=255)
    v = v.filter(ImageFilter.GaussianBlur(min(w, h) // 6))
    dark = Image.new('RGBA', (w, h), (0, 20, 8, 150))
    img.paste(dark, (0, 0), Image.eval(v, lambda p: 255 - p))
    return img


def football(size, angle=-30):
    """A brown ball with a highlight, white stripes and laces, rotated."""
    w, h = size, int(size * 0.6)
    ball = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(ball)
    d.ellipse([0, 0, w - 1, h - 1], fill=(120, 58, 26, 255), outline=(60, 26, 10, 255), width=max(2, w // 40))
    hl = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse([w * 0.18, h * 0.10, w * 0.75, h * 0.45], fill=(255, 210, 160, 70))
    ball.alpha_composite(hl.filter(ImageFilter.GaussianBlur(w // 30)))
    sw = max(2, w // 28)
    for x in (w * 0.2, w * 0.8):  # end stripes
        d.arc([x - w * 0.06, h * 0.08, x + w * 0.06, h * 0.92], 250 if x < w / 2 else -70,
              110 if x < w / 2 else 70, fill=(245, 245, 240, 255), width=sw)
    lw = max(2, w // 45)
    d.line([(w * 0.33, h * 0.5), (w * 0.67, h * 0.5)], fill=(250, 250, 245, 255), width=lw)
    for i in range(6):
        x = w * 0.37 + i * w * 0.052
        d.line([(x, h * 0.40), (x, h * 0.60)], fill=(250, 250, 245, 255), width=lw)
    shadow = Image.new('RGBA', (w + w // 5, h + w // 5), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse([w // 10, w // 10 + h // 10, w + w // 10, h + w // 10 + h // 10], fill=(0, 0, 0, 120))
    shadow = shadow.filter(ImageFilter.GaussianBlur(w // 25))
    shadow.alpha_composite(ball, (w // 10, w // 10))
    return shadow.rotate(angle, resample=Image.BICUBIC, expand=True)


def text(d, xy, s, f, fill=(255, 255, 255, 255), anchor='mm'):
    off = max(1, f.size // 18)
    d.text((xy[0] + off, xy[1] + off), s, font=f, fill=(0, 0, 0, 140), anchor=anchor)
    d.text(xy, s, font=f, fill=fill, anchor=anchor, stroke_width=max(1, f.size // 30), stroke_fill=(10, 40, 20, 255))


def square(px, small=False):
    w = h = px * SS
    img = field(w, h)
    d = ImageDraw.Draw(img)
    if small:
        ball = football(int(w * 0.95))
        img.alpha_composite(ball, ((w - ball.width) // 2, (h - ball.height) // 2))
    else:
        ball = football(int(w * 0.62), angle=-20)
        img.alpha_composite(ball, ((w - ball.width) // 2, int(h * 0.02)))
        text(d, (w / 2, h * 0.70), '10', font(int(h * 0.27)), fill=(255, 196, 37, 255))
        text(d, (w / 2, h * 0.90), 'COLLEGE FOOTBALL', font(int(h * 0.075)))
    return img.resize((px, px), Image.LANCZOS)


def wide(pw, ph):
    w, h = pw * SS, ph * SS
    img = field(w, h)
    d = ImageDraw.Draw(img)
    ball = football(int(h * 0.80), angle=-20)
    img.alpha_composite(ball, (int(w * 0.02), (h - ball.height) // 2))
    cx = w * 0.69
    text(d, (cx, h * 0.40), '10', font(int(h * 0.48)), fill=(255, 196, 37, 255))
    text(d, (cx, h * 0.78), 'COLLEGE FOOTBALL', font(int(h * 0.095)))
    return img.resize((pw, ph), Image.LANCZOS)


if __name__ == '__main__':
    OUT.mkdir(exist_ok=True)
    square(50, small=True).save(OUT / 'StoreLogo.png')
    square(44, small=True).save(OUT / 'Square44x44Logo.png')
    square(150).save(OUT / 'Square150x150Logo.png')
    square(310).save(OUT / 'Square310x310Logo.png')
    wide(310, 150).save(OUT / 'Wide310x150Logo.png')
    wide(620, 300).save(OUT / 'SplashScreen.png')
    print('wrote', sorted(p.name for p in OUT.iterdir()))

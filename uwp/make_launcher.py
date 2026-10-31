"""Render the start-screen images the UWP launcher loads at run time.

    python uwp/make_launcher.py <Assets directory> [--cover square-cover.png]

Writes launcher.bmp (1280x720 background), font.bmp (monospace glyph atlas
for ASCII 32-127, white on black) and font.txt ("cell_w cell_h advance").
SDL can load BMP without extra libraries, so these are generated per build
rather than stored in the repository: the background comes from your cover
when given (it never enters Git) and otherwise from generic football art.
"""
import argparse
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_assets  # noqa: E402  (generic field / football art)

W, H = 1280, 720
COLS, ROWS = 16, 6
FONTS = ('consolab.ttf', 'C:/Windows/Fonts/consolab.ttf', 'DejaVuSansMono-Bold.ttf',
         '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf')


def background(cover_path):
    if cover_path:
        cover = Image.open(cover_path).convert('RGB')
        side = min(cover.size)
        cover = cover.crop(((cover.width - side) // 2, (cover.height - side) // 2,
                            (cover.width + side) // 2, (cover.height + side) // 2))
        fill = cover.resize((W, W), Image.LANCZOS).crop((0, (W - H) // 2, W, (W + H) // 2))
        fill = fill.filter(ImageFilter.GaussianBlur(30))
        fill = Image.blend(fill, Image.new('RGB', (W, H), (8, 10, 14)), 0.55)
        fill.paste(cover.resize((H, H), Image.LANCZOS), (0, 0))
        return fill
    img = make_assets.field(W, H)
    ball = make_assets.football(int(H * 0.95), angle=-25)
    img.alpha_composite(ball, (int(W * 0.02), (H - ball.height) // 2))
    return img.convert('RGB')


def font_atlas():
    for name in FONTS:
        try:
            font = ImageFont.truetype(name, 56)
            break
        except OSError:
            continue
    else:
        raise SystemExit('no monospace TrueType font found (Consolas or DejaVu Sans Mono)')
    advance = int(round(font.getlength('M')))
    ascent, descent = font.getmetrics()
    cell_w, cell_h = advance + 4, ascent + descent + 4
    atlas = Image.new('L', (COLS * cell_w, ROWS * cell_h), 0)
    draw = ImageDraw.Draw(atlas)
    for code in range(32, 128):
        i = code - 32
        x, y = (i % COLS) * cell_w + 2, (i // COLS) * cell_h + 2
        draw.text((x, y), chr(code) if code < 127 else '?', font=font, fill=255)
    return atlas, (cell_w, cell_h, advance)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('--cover', type=Path)
    args = parser.parse_args()
    args.assets.mkdir(parents=True, exist_ok=True)
    background(args.cover).save(args.assets / 'launcher.bmp')
    atlas, metrics = font_atlas()
    atlas.convert('RGB').save(args.assets / 'font.bmp')
    (args.assets / 'font.txt').write_text('%d %d %d\n' % metrics)
    print('launcher assets written to', args.assets)


if __name__ == '__main__':
    main()

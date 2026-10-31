"""Render the UWP tile/logo assets from your own square cover image.

    python uwp/make_cover_assets.py cover.png <Assets directory>

Box art is not stored in this repository; build_uwp.ps1 -Cover (or a
cover.png/cover.jpg inside ncaa10-input.zip) runs this on the package layout,
replacing the default icons in uwp/Assets.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageStat


def square(cover, px):
    return cover.resize((px, px), Image.LANCZOS)


def wide(cover, w, h):
    # Whole cover centred on its own edge colour, blended in with soft side fades.
    art = cover.resize((h, h), Image.LANCZOS)
    edge = max(1, h // 20)
    sides = [art.crop((0, 0, edge, h)), art.crop((h - edge, 0, h, h))]
    colour = tuple(sum(ImageStat.Stat(side).mean[c] for side in sides) / 2 for c in range(3))
    bg = Image.new('RGB', (w, h), tuple(int(c) for c in colour))
    fade = h // 6
    mask = Image.new('L', (h, h), 255)
    draw = ImageDraw.Draw(mask)
    for i in range(fade):
        a = int(255 * (i / fade) ** 1.5)
        draw.line([(i, 0), (i, h)], fill=a)
        draw.line([(h - 1 - i, 0), (h - 1 - i, h)], fill=a)
    bg.paste(art, ((w - h) // 2, 0), mask)
    return bg


def main():
    cover = Image.open(sys.argv[1]).convert('RGB')
    side = min(cover.size)
    cover = cover.crop(((cover.width - side) // 2, (cover.height - side) // 2,
                        (cover.width + side) // 2, (cover.height + side) // 2))
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for name, px in (('StoreLogo.png', 50), ('Square44x44Logo.png', 44),
                     ('Square150x150Logo.png', 150), ('Square310x310Logo.png', 310)):
        square(cover, px).save(out / name)
    wide(cover, 310, 150).save(out / 'Wide310x150Logo.png')
    wide(cover, 620, 300).save(out / 'SplashScreen.png')
    print('cover assets written to', out)


if __name__ == '__main__':
    main()

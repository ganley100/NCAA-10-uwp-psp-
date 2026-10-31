"""Render the UWP tile/logo assets from your own square cover image.

    python uwp/make_cover_assets.py cover.png <Assets directory>

Box art is not stored in this repository; build_uwp.ps1 -Cover (or a
cover.png/cover.jpg inside ncaa10-input.zip) runs this on the package layout,
replacing the default icons in uwp/Assets.
"""
import sys
from pathlib import Path
from PIL import Image


def square(cover, px):
    return cover.resize((px, px), Image.LANCZOS)


def wide(cover, w, h):
    # Edge to edge: a full-width band from near the top of the cover (the
    # player's helmet and shoulders on box art), no letterboxing.
    art = cover.resize((w, w), Image.LANCZOS)
    top = min(int(w * 0.04), w - h)
    return art.crop((0, top, w, top + h))


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

# Generates CrispPlay's images for upstream's 日落 (dusk) palette and startup intro
# (qml/Veyra/DuskIntro.qml, HomePage.qml, Theme.qml), from custom/branding/logo-1024.png.
# custom/CMakeLists.txt copies the results over upstream's files of the same names in the
# build tree, so the intro's mechanics stay upstream's and no QML hook is needed.
#
#   wordmark-dusk.png        the whole mark on upstream's 1164x969 canvas: the logo above
#                            the word "CrispPlay" in the dusk accent gradient
#   wordmark-dusk-n.png      the intro's three paper layers, far to near; stacked they give
#   wordmark-dusk-r.png      the wordmark back: the logo, "Crisp", "Play" (upstream: N, R,
#   wordmark-dusk-veyra.png  "Veyra"; the file names are upstream's)
#   wordmark-dusk-glow.png   a baked soft glow, the wordmark's box grown by 186/1164 of its
#                            width on every side, at half size (768x670)
#   logo-dusk.png            the logo for the dusk palette (top dock, About), with a soft glow
#
# Usage: python -I custom/scripts/make-brand-dusk.py   (needs Pillow; Segoe UI Black for the text)
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "branding" / "logo-1024.png"
OUT = ROOT / "branding" / "dusk"
FONT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"C:\Windows\Fonts\seguibl.ttf")
W, H = 1164, 969
PAD = 186
SS = 2  # supersampling
PEACH, PINK, LILAC = (0xF0, 0xB1, 0x7B), (0xEF, 0x6F, 0xA3), (0xBB, 0x96, 0xF3)


def gradient(size, stops):
    """Left-to-right gradient with a slight downward lean, as an RGB image."""
    w, h = size
    line = Image.new("RGB", (256, 1))
    px = line.load()
    n = len(stops) - 1
    for i in range(256):
        t = i / 255 * n
        k = min(int(t), n - 1)
        f = t - k
        px[i, 0] = tuple(round(stops[k][c] + (stops[k + 1][c] - stops[k][c]) * f) for c in range(3))
    return line.resize((w, h), Image.BILINEAR)


def trim(im):
    return im.crop(im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox())


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    w, h = W * SS, H * SS
    logo = trim(Image.open(SRC).convert("RGBA"))
    logo_h = round(h * 0.60)
    logo = logo.resize((round(logo.width * logo_h / logo.height), logo_h), Image.LANCZOS)

    # The word, sized to the canvas width; "Crisp" and "Play" are separate layers.
    size = 100
    font = ImageFont.truetype(str(FONT), size)
    probe = ImageDraw.Draw(Image.new("L", (8, 8)))
    full = probe.textbbox((0, 0), "CrispPlay", font=font)
    size = int(size * (w * 0.97) / (full[2] - full[0]))
    font = ImageFont.truetype(str(FONT), size)
    full = probe.textbbox((0, 0), "CrispPlay", font=font)
    crisp_w = probe.textlength("Crisp", font=font)
    text_x = (w - (full[2] - full[0])) // 2 - full[0]
    text_y = h - round(h * 0.015) - full[3]
    fill = gradient((w, h), [PEACH, PINK, LILAC])

    def word(text, x):
        mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(mask).text((x, text_y), text, font=font, fill=255)
        layer = fill.convert("RGBA")
        layer.putalpha(mask)
        return layer

    layers = {
        "wordmark-dusk-n.png": Image.new("RGBA", (w, h), (0, 0, 0, 0)),
        "wordmark-dusk-r.png": word("Crisp", text_x),
        "wordmark-dusk-veyra.png": word("Play", text_x + crisp_w),
    }
    layers["wordmark-dusk-n.png"].alpha_composite(logo, ((w - logo.width) // 2, round(h * 0.012)))
    whole = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for name in ("wordmark-dusk-n.png", "wordmark-dusk-r.png", "wordmark-dusk-veyra.png"):
        whole.alpha_composite(layers[name])
        layers[name].resize((W, H), Image.LANCZOS).save(OUT / name, optimize=True)
    whole = whole.resize((W, H), Image.LANCZOS)
    whole.save(OUT / "wordmark-dusk.png", optimize=True)

    # Glow: the mark's colours lifted toward white, blurred (premultiplied), at half size.
    canvas = Image.new("RGBA", (W + 2 * PAD, H + 2 * PAD), (0, 0, 0, 0))
    canvas.alpha_composite(whole, (PAD, PAD))
    half = canvas.resize((canvas.width // 2, canvas.height // 2), Image.LANCZOS)
    lifted = Image.blend(half.convert("RGB"), Image.new("RGB", half.size, (255, 255, 255)), 0.45)
    alpha = half.getchannel("A")
    pre = Image.composite(lifted, Image.new("RGB", half.size, (0, 0, 0)), alpha)
    sigma = W * 0.03 / 2
    pre = pre.filter(ImageFilter.GaussianBlur(sigma))
    alpha = alpha.filter(ImageFilter.GaussianBlur(sigma))
    # Unpremultiply.
    a_px = alpha.load()
    out = Image.new("RGBA", half.size)
    o_px = out.load()
    c_px = [c.load() for c in pre.split()]
    for y in range(half.height):
        for x in range(half.width):
            a = a_px[x, y]
            if a:
                o_px[x, y] = tuple(min(255, c_px[i][x, y] * 255 // a) for i in range(3)) + (a,)
    assert out.size == (768, 670), out.size
    out.save(OUT / "wordmark-dusk-glow.png", optimize=True)

    # The small logo, upstream's 720x480 canvas, with a faint pink halo of its own.
    box = Image.new("RGBA", (720, 480), (0, 0, 0, 0))
    mark = trim(Image.open(SRC).convert("RGBA"))
    mark = mark.resize((round(mark.width * 400 / mark.height), 400), Image.LANCZOS)
    at = ((720 - mark.width) // 2, 40)
    halo = Image.new("RGBA", box.size, (0, 0, 0, 0))
    tint = Image.new("RGBA", mark.size, (0xF3, 0x96, 0xCE, 255))
    tint.putalpha(mark.getchannel("A").point(lambda a: a * 150 // 255))
    halo.alpha_composite(tint, at)
    box.alpha_composite(halo.filter(ImageFilter.GaussianBlur(14)))
    box.alpha_composite(mark, at)
    box.save(OUT / "logo-dusk.png", optimize=True)
    print("wrote", ", ".join(sorted(p.name for p in OUT.glob("*.png"))))


if __name__ == "__main__":
    main()

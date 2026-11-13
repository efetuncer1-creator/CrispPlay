"""Build the synthetic subtitle test set for the Phase 5 spike.

Takes still frames from local videos (ffmpeg) and draws game-style subtitles on
them. Output images go outside the repo (--out, e.g. deps\\subs-testset); only
the manifest is small enough to keep in git.

    python -I make_frames.py --out ..\\..\\..\\..\\deps\\subs-testset \
        --video C:\\path\\a.mp4 --video C:\\path\\b.mkv

Every sentence is rendered 4 times: 1080p clean, 720p clean, 1080p stream-like,
720p stream-like. Style, size, HUD clutter and background are balanced lists
shuffled with fixed seeds, so the factors are not aliased with each other and
the set can be rebuilt exactly from the same videos.
"""
import argparse
import io
import json
import random
import subprocess
import textwrap
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
FONTS = Path(r"C:\Windows\Fonts")

# name -> (font file, fill RGB, look)
STYLES = {
    "outline_white": ("arialbd.ttf", (255, 255, 255), "outline"),
    "outline_yellow": ("verdana.ttf", (255, 221, 51), "outline"),
    "shadow_white": ("seguisb.ttf", (255, 255, 255), "shadow"),
    "box_white": ("tahoma.ttf", (255, 255, 255), "box"),
    "thin_white": ("segoeuil.ttf", (255, 255, 255), "thin"),
    "serif_outline": ("georgia.ttf", (235, 230, 215), "outline"),
}
STYLE_ORDER = list(STYLES)
# Text height as a fraction of frame height: 32 / 40 / 49 px at 1080p, 22 / 27 / 32 px at 720p.
SIZES = [0.030, 0.037, 0.045]
WRAP_CHARS = 44
FRAMES_PER_VIDEO = 6


def balanced(values, n, seed):
    """n values, each used equally often, in a fixed pseudo-random order."""
    out = [values[i % len(values)] for i in range(n)]
    random.Random(seed).shuffle(out)
    return out


def grab_backgrounds(videos, out):
    """FRAMES_PER_VIDEO evenly spaced frames per video, scaled to 1080p and 720p."""
    bg = out / "backgrounds"
    bg.mkdir(parents=True, exist_ok=True)
    names = []
    for vi, video in enumerate(videos):
        dur = float(subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", video],
            capture_output=True, text=True, check=True).stdout.strip())
        for k in range(FRAMES_PER_VIDEO):
            t = dur * (k + 0.5) / FRAMES_PER_VIDEO
            name = f"v{vi}_{k}"
            for h in (1080, 720):
                dst = bg / f"{name}_{h}.png"
                if not dst.exists():
                    subprocess.run(
                        ["ffmpeg", "-v", "error", "-y", "-ss", f"{t:.3f}", "-i", video, "-frames:v", "1",
                         "-vf", f"scale={h * 16 // 9}:{h}:flags=lanczos", str(dst)], check=True)
            names.append(name)
    return names


def draw_text(img, xy, text, font, fill, look, size):
    """Draw one line with the given look; xy is the top-left of the text box."""
    x, y = xy
    if look == "outline":
        d = ImageDraw.Draw(img)
        d.text((x, y), text, font=font, fill=fill, stroke_width=max(2, round(size / 12)), stroke_fill=(0, 0, 0))
    elif look in ("shadow", "thin"):
        # Soft drop shadow on its own layer, then the text on top. "thin" is the hard case:
        # light font weight and a faint shadow only.
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        off = max(1, round(size / 14))
        alpha = 230 if look == "shadow" else 120
        ImageDraw.Draw(layer).text((x + off, y + off), text, font=font, fill=(0, 0, 0, alpha))
        layer = layer.filter(ImageFilter.GaussianBlur(max(1, size / 20)))
        img.paste(layer, (0, 0), layer)
        ImageDraw.Draw(img).text((x, y), text, font=font, fill=fill)
    else:  # box: the box itself is drawn by the caller
        ImageDraw.Draw(img).text((x, y), text, font=font, fill=fill)


def render(bg_path, lines, style, size_frac, hud):
    img = Image.open(bg_path).convert("RGB")
    w, h = img.size
    size = round(h * size_frac)
    font_file, fill, look = STYLES[style]
    font = ImageFont.truetype(str(FONTS / font_file), size)
    line_h = round(size * 1.25)
    block_h = line_h * len(lines)
    top = round(h * 0.93) - block_h  # block sits just above the bottom 7 %
    widths = [font.getlength(line) for line in lines]
    if look == "box":
        pad = round(size * 0.4)
        bw = max(widths) + 2 * pad
        overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(overlay).rectangle(
            [(w - bw) / 2, top - pad / 2, (w + bw) / 2, top + block_h + pad / 2], fill=(0, 0, 0, 140))
        img.paste(overlay, (0, 0), overlay)
    left = w
    right = 0
    for i, line in enumerate(lines):
        x = round((w - widths[i]) / 2)
        draw_text(img, (x, top + i * line_h), line, font, fill, look, size)
        left = min(left, x)
        right = max(right, x + widths[i])
    if hud:
        # Corner clutter that is not a subtitle: a button prompt and an ammo counter.
        hud_font = ImageFont.truetype(str(FONTS / "arialbd.ttf"), round(h * 0.024))
        d = ImageDraw.Draw(img)
        d.text((w * 0.80, h * 0.86), "R2  Shoot", font=hud_font, fill=(255, 255, 255), stroke_width=2, stroke_fill=(0, 0, 0))
        d.text((w * 0.84, h * 0.91), "24 / 120", font=hud_font, fill=(255, 255, 255), stroke_width=2, stroke_fill=(0, 0, 0))
        d.text((w * 0.04, h * 0.90), "[E] Pick up", font=hud_font, fill=(255, 255, 255), stroke_width=2, stroke_fill=(0, 0, 0))
    box = [left / w, top / h, right / w, (top + block_h) / h]
    # Mean luma behind the subtitle block, measured on the clean background.
    band = Image.open(bg_path).convert("L").crop((int(left), int(top), int(right), int(top + block_h)))
    luma = sum(band.tobytes()) / max(1, band.width * band.height)
    return img, box, size, luma


def stream_like(img):
    """Cheap stand-in for a low-bitrate game stream: JPEG q28 with 4:2:0 chroma."""
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=28, subsampling=2)
    buf.seek(0)
    return Image.open(buf).convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--video", action="append", required=True)
    ap.add_argument("--sentences", default=str(HERE / "sentences.json"))
    ap.add_argument("--manifest", default=str(HERE / "testset_manifest.json"))
    args = ap.parse_args()

    out = Path(args.out)
    (out / "frames").mkdir(parents=True, exist_ok=True)
    data = json.loads(Path(args.sentences).read_text(encoding="utf-8"))
    speakers = data["speakers"]
    backgrounds = grab_backgrounds(args.video, out)

    total = 4 * len(data["sentences"])
    styles = balanced(STYLE_ORDER, total, 1)
    sizes = balanced(SIZES, total, 2)
    huds = balanced([True, False, False, False], total, 3)
    bgs = balanced(backgrounds, total, 4)

    items = []
    for i, s in enumerate(data["sentences"]):
        text = s["en"]
        if str(s["id"]) in speakers:
            text = f'{speakers[str(s["id"])]}: {text}'
        lines = textwrap.wrap(text, WRAP_CHARS)
        for k in range(4):
            height = (1080, 720)[k % 2]
            n = 4 * i + k
            style, size_frac, hud, bg = styles[n], sizes[n], huds[n], bgs[n]
            cond = "clean" if k < 2 else "stream"
            img, box, size, luma = render(out / "backgrounds" / f"{bg}_{height}.png", lines, style, size_frac, hud)
            if cond == "stream":
                img = stream_like(img)
            name = f"s{s['id']:02d}_{k}.png"
            img.save(out / "frames" / name, compress_level=1)
            items.append({"file": name, "id": s["id"], "lines": lines, "height": height, "style": style,
                          "size_px": size, "cond": cond, "hud": hud, "bg": bg,
                          "box": [round(v, 4) for v in box], "luma": round(luma, 1)})
    manifest = {"videos": [Path(v).name for v in args.video], "frames_per_video": FRAMES_PER_VIDEO, "items": items}
    Path(args.manifest).write_text(json.dumps(manifest, ensure_ascii=False, indent=0), encoding="utf-8")
    print(f"{len(items)} frames -> {out / 'frames'}")


if __name__ == "__main__":
    main()

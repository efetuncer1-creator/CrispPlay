"""OCR accuracy and latency on the synthetic test set (Phase 5 spike).

    python -I ocr_bench.py --testset ..\\deps\\subs-testset --engine winocr --out winocr.json
    python -I ocr_bench.py --testset ..\\deps\\subs-testset --engine rapidocr --out rapidocr.json

For every frame it tries each crop strategy x preprocessing and stores the text,
the error against the ground truth and the time of the recognise call alone.
ocr_report.py turns the JSON into tables.
"""
import argparse
import asyncio
import json
import re
import time
from pathlib import Path

import cv2
import numpy as np

# Crop strategies, as fractions of the frame (top, bottom, left, right).
BAND = (0.62, 0.97, 0.14, 0.86)       # CouchSubs' default subtitle region
BAND_WIDE = (0.62, 0.97, 0.0, 1.0)    # same band, full width
CROPS = ["full", "band", "bandwide", "tight"]
PREPS = ["raw", "up2", "gray", "invert", "otsu", "whitetext", "up2_whitetext", "pick2"]


def crop_rect(name, item, w, h):
    if name == "full":
        return 0, h, 0, w
    if name in ("band", "bandwide"):
        t, b, l, r = BAND if name == "band" else BAND_WIDE
        return int(t * h), int(b * h), int(l * w), int(r * w)
    # "tight": what a tracker would use once the subtitle position is known:
    # the ground-truth box plus 0.6 line heights vertically and 3 % of the width sideways.
    l, t, r, b = item["box"]
    my = 0.6 * item["size_px"] / h
    return (max(0, int((t - my) * h)), min(h, int((b + my) * h)),
            max(0, int((l - 0.03) * w)), min(w, int((r + 0.03) * w)))


def white_text_only(bgr, radius=5):
    """Bright pixels that have a dark pixel nearby become black text on white.

    Same idea as CouchSubs' TextImage.WhiteTextOnly (outlined or shadowed light
    text), written from the description, not from their code. "Bright" uses the
    max channel so yellow subtitles count too.
    """
    value = bgr.max(axis=2)
    luma = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    dark_near = cv2.dilate((luma < 90).astype(np.uint8), np.ones((2 * radius + 1, 2 * radius + 1), np.uint8))
    text = (value >= 200) & (dark_near > 0)
    out = np.full(luma.shape, 255, np.uint8)
    out[text] = 0
    return cv2.cvtColor(out, cv2.COLOR_GRAY2BGR)


def preprocess(name, bgr):
    if name == "raw":
        return bgr
    if name == "up2":
        return cv2.resize(bgr, None, fx=2, fy=2, interpolation=cv2.INTER_LINEAR)
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    if name == "gray":
        return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    if name == "invert":
        return cv2.cvtColor(255 - gray, cv2.COLOR_GRAY2BGR)
    if name == "otsu":
        _, th = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
        return cv2.cvtColor(th, cv2.COLOR_GRAY2BGR)
    if name == "whitetext":
        return white_text_only(bgr)
    if name == "up2_whitetext":
        return white_text_only(cv2.resize(bgr, None, fx=2, fy=2, interpolation=cv2.INTER_LINEAR), radius=10)
    raise ValueError(name)


WORD = re.compile(r"^[\"'(\[-]*([A-Za-z]+(?:['’-][A-Za-z]+)*|\d+)[\"')\].,!?:;…-]*$")
PLAIN_CASE = re.compile(r"^(?:[A-Z]?[a-z]+|[A-Z]+)(?:['’-](?:[A-Z]?[a-z]+|[A-Z]+))*$")


def wordiness(text):
    """How word-like a reading is: letters in clean words minus letters in junk tokens."""
    score = 0
    for tok in text.split():
        m = WORD.match(tok)
        core = m.group(1) if m else None
        if core and (core.isdigit() or PLAIN_CASE.match(core)):
            score += len(core)
        else:
            score -= 2 * len(tok)
    return score


def levenshtein(a, b):
    if a == b:
        return 0
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def canon(text):
    return " ".join(re.sub(r"[^a-z0-9]+", " ", text.lower()).split())


def centred(lines, width):
    """Keep lines whose box overlaps the middle fifth of the crop (subtitles are centred)."""
    keep = [t for t, (x0, x1) in lines if x0 < 0.6 * width and x1 > 0.4 * width]
    return " ".join(keep)


class WinOcr:
    name = "winocr"

    def __init__(self):
        from winrt.windows.globalization import Language
        from winrt.windows.graphics.imaging import BitmapAlphaMode, BitmapPixelFormat, SoftwareBitmap
        from winrt.windows.media.ocr import OcrEngine
        from winrt.windows.storage.streams import DataWriter
        self.SoftwareBitmap, self.DataWriter = SoftwareBitmap, DataWriter
        self.fmt, self.alpha = BitmapPixelFormat.BGRA8, BitmapAlphaMode.PREMULTIPLIED
        self.engine = OcrEngine.try_create_from_language(Language("en-US"))
        if self.engine is None:
            raise SystemExit("Windows OCR: en-US language pack is not installed")
        self.max_dim = OcrEngine.max_image_dimension
        self.loop = asyncio.new_event_loop()

    def read(self, bgr):
        """-> ([(text, (x0, x1)) per line], recognise ms, bitmap-copy ms)"""
        h, w = bgr.shape[:2]
        t0 = time.perf_counter()
        bgra = cv2.cvtColor(bgr, cv2.COLOR_BGR2BGRA)
        writer = self.DataWriter()
        writer.write_bytes(bgra.tobytes())
        bitmap = self.SoftwareBitmap.create_copy_from_buffer(writer.detach_buffer(), self.fmt, w, h, self.alpha)
        t1 = time.perf_counter()

        async def run():
            return await self.engine.recognize_async(bitmap)
        result = self.loop.run_until_complete(run())
        t2 = time.perf_counter()
        lines = []
        self.rects = []  # (x0, y0, x1, y1) of each line, same order as the returned lines
        for line in result.lines:
            rs = [wd.bounding_rect for wd in line.words]
            x0, x1 = min(r.x for r in rs), max(r.x + r.width for r in rs)
            self.rects.append((x0, min(r.y for r in rs), x1, max(r.y + r.height for r in rs)))
            lines.append((line.text, (x0, x1)))
        return lines, (t2 - t1) * 1000, (t1 - t0) * 1000


class Rapid:
    """RapidOCR = PaddleOCR's PP-OCR models on ONNX Runtime (CPU)."""
    name = "rapidocr"

    def __init__(self, lang=None):
        from rapidocr import LangRec, RapidOCR
        self.engine = RapidOCR(params={"Rec.lang_type": LangRec(lang)}) if lang else RapidOCR()

    def read(self, bgr):
        t0 = time.perf_counter()
        res = self.engine(bgr)
        ms = (time.perf_counter() - t0) * 1000
        if res is None or not res.txts:
            return [], ms, 0.0
        # The detector returns word or phrase boxes in no useful order: group them into rows
        # by vertical overlap, sort each row left to right, and split a row at a gap wider
        # than 4 box heights (subtitle on the left of a HUD counter is not one line).
        boxes = []
        for box, text in zip(res.boxes, res.txts):
            xs, ys = box[:, 0], box[:, 1]
            boxes.append((float(ys.min()), float(ys.max()), float(xs.min()), float(xs.max()), text))
        boxes.sort(key=lambda b: (b[0] + b[1]) / 2)
        rows = []
        for b in boxes:
            mid = (b[0] + b[1]) / 2
            if rows and rows[-1][0][0] <= mid <= rows[-1][0][1]:
                rows[-1].append(b)
            else:
                rows.append([b])
        lines = []
        for row in rows:
            row.sort(key=lambda b: b[2])
            part = [row[0]]
            for b in row[1:]:
                if b[2] - part[-1][3] > 4 * (b[1] - b[0]):
                    lines.append((" ".join(x[4] for x in part), (part[0][2], part[-1][3])))
                    part = [b]
                else:
                    part.append(b)
            lines.append((" ".join(x[4] for x in part), (part[0][2], part[-1][3])))
        return lines, ms, 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--testset", required=True)
    ap.add_argument("--manifest", default=str(Path(__file__).resolve().parent / "testset_manifest.json"))
    ap.add_argument("--engine", choices=["winocr", "rapidocr"], default="winocr")
    ap.add_argument("--lang", default="", help="rapidocr recognition model language, e.g. en (default: the package default)")
    ap.add_argument("--crops", default=",".join(CROPS))
    ap.add_argument("--preps", default=",".join(PREPS))
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    engine = WinOcr() if args.engine == "winocr" else Rapid(args.lang or None)
    items = json.loads(Path(args.manifest).read_text(encoding="utf-8"))["items"]
    if args.limit:
        items = items[:: max(1, len(items) // args.limit)]
    crops, preps = args.crops.split(","), args.preps.split(",")
    engine.read(np.zeros((200, 800, 3), np.uint8))  # warm-up

    rows = []
    for n, item in enumerate(items):
        frame = cv2.imread(str(Path(args.testset) / "frames" / item["file"]))
        h, w = frame.shape[:2]
        truth = " ".join(item["lines"])
        for crop in crops:
            t, b, l, r = crop_rect(crop, item, w, h)
            area = np.ascontiguousarray(frame[t:b, l:r])
            cache = {}
            for prep in preps:
                if prep == "pick2":
                    # Read raw and white-text-only, keep the more word-like one (two OCR calls).
                    if "raw" not in cache or "whitetext" not in cache:
                        continue
                    a, bb = cache["raw"], cache["whitetext"]
                    best = bb if wordiness(bb["all"]) > wordiness(a["all"]) else a
                    row = dict(best, prep="pick2", ms=a["ms"] + bb["ms"], copy_ms=a["copy_ms"] + bb["copy_ms"])
                else:
                    image = preprocess(prep, area)
                    t0 = time.perf_counter()
                    _ = preprocess(prep, area) if prep != "raw" else None
                    prep_ms = (time.perf_counter() - t0) * 1000
                    lines, ms, copy_ms = engine.read(image)
                    scale = image.shape[1] / area.shape[1]
                    row = {"file": item["file"], "crop": crop, "prep": prep, "px": [image.shape[1], image.shape[0]],
                           "ms": ms, "copy_ms": copy_ms, "prep_ms": prep_ms,
                           "all": " ".join(t for t, _ in lines),
                           "mid": centred(lines, image.shape[1]) if crop != "tight" else " ".join(t for t, _ in lines)}
                    cache[prep] = row
                for key in ("all", "mid"):
                    text = " ".join(row[key].split())
                    row[f"cer_{key}"] = min(1.0, levenshtein(text, truth) / len(truth))
                    row[f"wer_{key}"] = min(1.0, levenshtein(text.split(), truth.split()) / len(truth.split()))
                    row[f"exact_{key}"] = text == truth
                    row[f"canon_{key}"] = canon(text) == canon(truth)
                rows.append(dict(row))
        if n % 32 == 0:
            print(f"{n}/{len(items)}", flush=True)
    Path(args.out).write_text(json.dumps({"engine": engine.name, "rows": rows}, ensure_ascii=False), encoding="utf-8")
    print("wrote", args.out)


if __name__ == "__main__":
    main()

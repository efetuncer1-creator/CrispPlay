r"""Read every sampled frame of a real gameplay clip (Phase 5 spike).

Frames come from ffmpeg, already cut to the lower part of the picture:

    ffmpeg -i clip.mp4 -vf "fps=2,crop=1920:432:0:648" frames\f%04d.png
    python -I -X utf8 real_scan.py --frames ..\deps\subs-real\frames --top 648 --height 1080 --out scan.json

For each frame the default subtitle band is read by Windows OCR twice (as is,
and white-text-only) and every line Windows OCR found is read again by the
PP-OCR recognition network (the "hybrid"). Lines keep their boxes so that
real_report.py can pick the subtitle block and score it against the transcript.
"""
import argparse
import json
import sys
import time
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops the script folder from sys.path
from ocr_bench import BAND, WinOcr, white_text_only


def merge_rows(rects):
    """Union of boxes that share a text row (Windows OCR sometimes splits one line in two)."""
    rows = []
    for x0, y0, x1, y1 in sorted(rects, key=lambda r: (r[1] + r[3]) / 2):
        mid = (y0 + y1) / 2
        if rows and rows[-1][1] <= mid <= rows[-1][3]:
            a = rows[-1]
            rows[-1] = [min(a[0], x0), min(a[1], y0), max(a[2], x1), max(a[3], y1)]
        else:
            rows.append([x0, y0, x1, y1])
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", required=True)
    ap.add_argument("--top", type=int, required=True, help="row of the full picture where the saved cut starts")
    ap.add_argument("--height", type=int, required=True, help="height of the full picture")
    ap.add_argument("--fps", type=float, default=2.0)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    from rapidocr import LangRec, RapidOCR
    rec = RapidOCR(params={"Rec.lang_type": LangRec("en"), "EngineConfig.onnxruntime.intra_op_num_threads": args.threads,
                           "EngineConfig.onnxruntime.inter_op_num_threads": 1})
    win = WinOcr()
    win.read(np.zeros((200, 800, 3), np.uint8))
    rec(np.full((48, 320, 3), 255, np.uint8), use_det=False, use_cls=False, use_rec=True)

    rows = []
    files = sorted(Path(args.frames).glob("*.png"))
    for n, path in enumerate(files):
        cut = cv2.imread(str(path))
        w = cut.shape[1]
        y0 = max(0, int(BAND[0] * args.height) - args.top)
        y1 = min(cut.shape[0], int(BAND[1] * args.height) - args.top)
        area = np.ascontiguousarray(cut[y0:y1, int(BAND[2] * w):int(BAND[3] * w)])
        ah, aw = area.shape[:2]

        t0 = time.perf_counter()
        white_img = white_text_only(area)
        prep_ms = (time.perf_counter() - t0) * 1000
        raw, raw_ms, raw_copy = win.read(area)
        raw_rects = list(win.rects)
        white, white_ms, white_copy = win.read(white_img)
        white_rects = list(win.rects)

        # Hybrid: one PP-OCR recognition per text row found by either Windows OCR reading.
        t1 = time.perf_counter()
        hybrid = []
        for x0, ry0, x1, ry1 in merge_rows(raw_rects + white_rects):
            lh = ry1 - ry0
            # Only centred rows are widened to be symmetric (a subtitle whose first word was missed).
            if x0 < 0.6 * aw and x1 > 0.4 * aw:
                half = max(aw / 2 - x0, x1 - aw / 2) + 0.6 * lh
                cx0, cx1 = max(0, aw / 2 - half), min(aw, aw / 2 + half)
            else:
                cx0, cx1 = max(0, x0 - 0.6 * lh), min(aw, x1 + 0.6 * lh)
            piece = area[int(max(0, ry0 - 0.3 * lh)):int(min(ah, ry1 + 0.3 * lh)), int(cx0):int(cx1)]
            if piece.size:
                res = rec(piece, use_det=False, use_cls=False, use_rec=True)
                if res is not None and res.txts and res.txts[0].strip():
                    hybrid.append({"text": res.txts[0], "box": [cx0, ry0, cx1, ry1]})
        rec_ms = (time.perf_counter() - t1) * 1000
        rows.append({
            "file": path.name, "t": n / args.fps, "size": [aw, ah],
            "raw": [{"text": t, "box": list(r)} for (t, _), r in zip(raw, raw_rects)],
            "white": [{"text": t, "box": list(r)} for (t, _), r in zip(white, white_rects)],
            "hybrid": hybrid,
            "prep_ms": prep_ms, "raw_ms": raw_ms, "white_ms": white_ms, "copy_ms": raw_copy + white_copy, "rec_ms": rec_ms})
        if n % 200 == 0:
            print(n, len(files), flush=True)
    Path(args.out).write_text(json.dumps(rows, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()

"""Hybrid OCR: Windows OCR finds the subtitle lines, a PP-OCR recognition model reads them. (Phase 5 spike)

Windows OCR is fast and finds lines well but misreads letters (m -> nn, y -> v).
PP-OCR reads letters well but its text detector costs ~400 ms on the CPU. Here
the detector is skipped: the band is read by Windows OCR, the centred lines are
cut out (widened to be symmetric about the band centre, since subtitles are
centred and Windows OCR sometimes misses the first or last word) and only the
PP-OCR recognition network (ONNX Runtime, CPU) runs on those cuts.

    python -I -X utf8 ocr_hybrid.py --testset ..\deps\subs-testset --out hybrid.json
"""
import argparse
import json
import statistics
import sys
import time
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops the script folder from sys.path
from ocr_bench import BAND, WinOcr, canon, levenshtein, white_text_only, wordiness

HERE = Path(__file__).resolve().parent


def pct(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(round(p / 100 * (len(values) - 1))))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--testset", required=True)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    from rapidocr import LangRec, RapidOCR
    rec = RapidOCR(params={"Rec.lang_type": LangRec("en"), "EngineConfig.onnxruntime.intra_op_num_threads": args.threads,
                           "EngineConfig.onnxruntime.inter_op_num_threads": 1})
    win = WinOcr()
    win.read(np.zeros((200, 800, 3), np.uint8))
    rec(np.full((48, 320, 3), 255, np.uint8), use_det=False, use_cls=False, use_rec=True)

    items = json.loads((HERE / "testset_manifest.json").read_text(encoding="utf-8"))["items"]
    rows = []
    for item in items:
        frame = cv2.imread(str(Path(args.testset) / "frames" / item["file"]))
        h, w = frame.shape[:2]
        area = np.ascontiguousarray(frame[int(BAND[0] * h):int(BAND[1] * h), int(BAND[2] * w):int(BAND[3] * w)])
        aw = area.shape[1]
        t0 = time.perf_counter()
        raw, _, _ = win.read(area)
        raw_rects = win.rects
        white, _, _ = win.read(white_text_only(area))
        white_rects = win.rects

        def mid(lines, rects):
            keep = [(t, r) for (t, (x0, x1)), r in zip(lines, rects) if x0 < 0.6 * aw and x1 > 0.4 * aw]
            return " ".join(t for t, _ in keep), [r for _, r in keep]
        a_text, a_rects = mid(raw, raw_rects)
        b_text, b_rects = mid(white, white_rects)
        win_text, rects = (b_text, b_rects) if wordiness(b_text) > wordiness(a_text) else (a_text, a_rects)
        t1 = time.perf_counter()
        parts = []
        for x0, y0, x1, y1 in sorted(rects, key=lambda r: r[1]):
            lh = y1 - y0
            half = max(aw / 2 - x0, x1 - aw / 2) + 0.6 * lh   # symmetric about the centre, plus a margin
            cx0, cx1 = int(max(0, aw / 2 - half)), int(min(aw, aw / 2 + half))
            cy0, cy1 = int(max(0, y0 - 0.3 * lh)), int(min(area.shape[0], y1 + 0.3 * lh))
            cut = area[cy0:cy1, cx0:cx1]
            if cut.size == 0:
                continue
            res = rec(cut, use_det=False, use_cls=False, use_rec=True)
            if res is not None and res.txts:
                parts.append(res.txts[0])
        t2 = time.perf_counter()
        text = " ".join(" ".join(parts).split())
        truth = " ".join(item["lines"])
        rows.append({"file": item["file"], "win": " ".join(win_text.split()), "hybrid": text,
                     "win_ms": (t1 - t0) * 1000, "rec_ms": (t2 - t1) * 1000, "lines": len(rects),
                     "win_ok": canon(win_text) == canon(truth), "hybrid_ok": canon(text) == canon(truth),
                     "win_cer": min(1.0, levenshtein(" ".join(win_text.split()), truth) / len(truth)),
                     "hybrid_cer": min(1.0, levenshtein(text, truth) / len(truth)),
                     "bg_text": item["bg"].startswith("v1_"), "style": item["style"]})

    def table(sub, label):
        n = len(sub)
        print(f"| {label} | {n} | {100 * sum(r['win_ok'] for r in sub) / n:.0f} | {100 * statistics.mean(r['win_cer'] for r in sub):.1f} | "
              f"{100 * sum(r['hybrid_ok'] for r in sub) / n:.0f} | {100 * statistics.mean(r['hybrid_cer'] for r in sub):.1f} |")
    print("| alt küme | n | Windows OCR: sözcükler doğru % | CER % | Hibrit: sözcükler doğru % | CER % |\n|---|---|---|---|---|---|")
    table(rows, "hepsi")
    table([r for r in rows if not r["bg_text"]], "arka planda yazı yok")
    table([r for r in rows if r["bg_text"]], "arka planda yazı var")
    for style in sorted({r["style"] for r in rows}):
        table([r for r in rows if r["style"] == style and not r["bg_text"]], f"{style} (yazısız zemin)")
    print(f"\nWindows OCR (2 okuma) p50 {pct([r['win_ms'] for r in rows], 50):.1f} ms, p95 {pct([r['win_ms'] for r in rows], 95):.1f} ms; "
          f"PP-OCR tanıma p50 {pct([r['rec_ms'] for r in rows], 50):.1f} ms, p95 {pct([r['rec_ms'] for r in rows], 95):.1f} ms "
          f"({args.threads} iş parçacığı)")
    Path(args.out).write_text(json.dumps(rows, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

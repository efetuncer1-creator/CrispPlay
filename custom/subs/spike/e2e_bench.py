"""End to end on the synthetic frames: crop -> Windows OCR -> OPUS-MT (Phase 5 spike).

    python -I -X utf8 e2e_bench.py --testset ..\\deps\\subs-testset \
        --model ..\\deps\\models\\opus-mt-tc-big-en-tr-ct2-int8 --out e2e.json

Per frame: cut the default subtitle band, read it twice (raw and white-text-only,
keep the more word-like reading), drop the speaker label, translate sentence by
sentence. Reports the time of each step and the translation score when the
input is what OCR read instead of the clean sentence. No stabiliser here: its
cost is waiting for the next read, which is a scheduling constant, not CPU time.
"""
import argparse
import json
import re
import statistics
import sys
import time
from pathlib import Path

import cv2
import numpy as np
import sacrebleu

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops the script folder from sys.path
from mt_bench import Model, pct, split_sentences
from ocr_bench import BAND, WinOcr, canon, centred, white_text_only, wordiness

HERE = Path(__file__).resolve().parent
SPEAKER = re.compile(r"^([A-Z][A-Za-z]{1,14}):\s+")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--testset", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--hybrid-json", default="", help="ocr_hybrid.py output: also score translations of the hybrid readings")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    items = json.loads((HERE / "testset_manifest.json").read_text(encoding="utf-8"))["items"]
    sentences = {s["id"]: s for s in json.loads((HERE / "sentences.json").read_text(encoding="utf-8"))["sentences"]}
    ocr = WinOcr()
    mt = Model("opus", args.model, args.threads)
    ocr.read(np.zeros((200, 800, 3), np.uint8))
    mt.translate(["Hello there."], 4)

    cache = {}
    rows = []
    for item in items:
        frame = cv2.imread(str(Path(args.testset) / "frames" / item["file"]))
        h, w = frame.shape[:2]
        t0 = time.perf_counter()
        t, b, l, r = (int(BAND[0] * h), int(BAND[1] * h), int(BAND[2] * w), int(BAND[3] * w))
        area = np.ascontiguousarray(frame[t:b, l:r])
        white = white_text_only(area)
        t1 = time.perf_counter()
        raw_lines, raw_ms, raw_copy = ocr.read(area)
        white_lines, white_ms, white_copy = ocr.read(white)
        a, bb = centred(raw_lines, area.shape[1]), centred(white_lines, area.shape[1])
        text = bb if wordiness(bb) > wordiness(a) else a
        text = " ".join(text.split())
        t2 = time.perf_counter()
        source = SPEAKER.sub("", text)
        hit = source in cache
        if not hit and source:
            cache[source] = " ".join(mt.translate(split_sentences(source), 4))
        t3 = time.perf_counter()
        out = cache.get(source, "")
        rows.append({"file": item["file"], "id": item["id"], "ocr": text, "hyp": out, "hit": hit,
                     "ocr_ok": canon(source) == canon(sentences[item["id"]]["en"]),
                     "prep_ms": (t1 - t0) * 1000, "ocr_ms": (t2 - t1) * 1000, "mt_ms": (t3 - t2) * 1000})

    refs = [sentences[r["id"]]["tr"] for r in rows]
    hyps = [r["hyp"] for r in rows]
    miss = [r["mt_ms"] for r in rows if not r["hit"] and r["ocr"]]
    hits = [r["mt_ms"] for r in rows if r["hit"]]
    ok = [r for r in rows if r["ocr_ok"]]
    bad = [r for r in rows if not r["ocr_ok"]]
    summary = {
        "frames": len(rows), "ocr_words_ok_pct": round(100 * len(ok) / len(rows), 1),
        "chrf_all": round(sacrebleu.corpus_chrf(hyps, [refs]).score, 1),
        "chrf_ocr_ok": round(sacrebleu.corpus_chrf([r["hyp"] for r in ok], [[sentences[r["id"]]["tr"] for r in ok]]).score, 1),
        "chrf_ocr_bad": round(sacrebleu.corpus_chrf([r["hyp"] for r in bad], [[sentences[r["id"]]["tr"] for r in bad]]).score, 1) if bad else None,
        "prep_ms_p50": round(pct([r["prep_ms"] for r in rows], 50), 1), "prep_ms_p95": round(pct([r["prep_ms"] for r in rows], 95), 1),
        "ocr_ms_p50": round(pct([r["ocr_ms"] for r in rows], 50), 1), "ocr_ms_p95": round(pct([r["ocr_ms"] for r in rows], 95), 1),
        "mt_miss_ms_p50": round(pct(miss, 50), 1), "mt_miss_ms_p95": round(pct(miss, 95), 1),
        "mt_hit_ms_p50": round(pct(hits, 50), 3) if hits else None, "cache_hits": len(hits), "cache_misses": len(miss),
        "total_miss_ms_p50": round(statistics.median(r["prep_ms"] + r["ocr_ms"] + r["mt_ms"] for r in rows if not r["hit"]), 1),
        "total_miss_ms_p95": round(pct([r["prep_ms"] + r["ocr_ms"] + r["mt_ms"] for r in rows if not r["hit"]], 95), 1),
    }
    if args.hybrid_json:
        # Same frames, but the text comes from Windows OCR line boxes + PP-OCR recognition.
        hybrid = {r["file"]: r for r in json.loads(Path(args.hybrid_json).read_text(encoding="utf-8"))}
        outs = []
        for r in rows:
            source = SPEAKER.sub("", hybrid[r["file"]]["hybrid"])
            if source and source not in cache:
                cache[source] = " ".join(mt.translate(split_sentences(source), 4))
            outs.append(cache.get(source, ""))
            r["hybrid_ocr"], r["hybrid_hyp"] = source, outs[-1]
        summary["hybrid_ocr_words_ok_pct"] = round(100 * sum(hybrid[r["file"]]["hybrid_ok"] for r in rows) / len(rows), 1)
        summary["hybrid_chrf_all"] = round(sacrebleu.corpus_chrf(outs, [refs]).score, 1)
        clean = [i for i, r in enumerate(rows) if not hybrid[r["file"]]["bg_text"]]
        summary["hybrid_chrf_no_bg_text"] = round(sacrebleu.corpus_chrf([outs[i] for i in clean], [[refs[i] for i in clean]]).score, 1)
        summary["win_chrf_no_bg_text"] = round(sacrebleu.corpus_chrf([hyps[i] for i in clean], [[refs[i] for i in clean]]).score, 1)
    print(json.dumps(summary, indent=1))
    Path(args.out).write_text(json.dumps({"summary": summary, "rows": rows}, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

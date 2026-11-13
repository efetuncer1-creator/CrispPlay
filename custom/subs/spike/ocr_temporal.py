"""Does reading the same subtitle on several consecutive frames fix OCR errors? (Phase 5 spike)

A subtitle stays up for 2-3 s while the picture behind it moves, so the real
pipeline sees it 8-12 times at 4 reads a second. Here every sentence is drawn
on 6 consecutive video frames 250 ms apart (same style and position) and read
on each; the table compares one reading with a consensus of the first N.

    python -I -X utf8 ocr_temporal.py --work ..\\deps\\subs-testset\\temporal \
        --video a.mp4 --video b.mkv --out temporal.json

Consensus = the reading closest to all the others (medoid by edit distance on
the simplified text), ties broken by how word-like it is.
"""
import argparse
import json
import random
import subprocess
import sys
import textwrap
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops the script folder from sys.path
from make_frames import SIZES, STYLE_ORDER, WRAP_CHARS, balanced, render, stream_like
from ocr_bench import BAND, WinOcr, canon, centred, levenshtein, white_text_only, wordiness

HERE = Path(__file__).resolve().parent
READS = 6


def consensus(readings):
    if len(readings) == 1:
        return readings[0]
    keys = [canon(r) for r in readings]
    cost = [sum(levenshtein(a, b) for b in keys) for a in keys]
    best = min(range(len(readings)), key=lambda i: (cost[i], -wordiness(readings[i])))
    return readings[best]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", required=True)
    ap.add_argument("--video", action="append", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)

    data = json.loads((HERE / "sentences.json").read_text(encoding="utf-8"))
    sentences, speakers = data["sentences"], data["speakers"]
    n = len(sentences)
    styles = balanced(STYLE_ORDER, n, 11)
    sizes = balanced(SIZES, n, 12)
    heights = balanced([1080, 720], n, 13)
    conds = balanced(["clean", "stream"], n, 14)
    videos = balanced(list(range(len(args.video))), n, 15)
    rng = random.Random(16)
    ocr = WinOcr()
    ocr.read(np.zeros((200, 800, 3), np.uint8))

    rows = []
    for i, s in enumerate(sentences):
        video = args.video[videos[i]]
        dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", video],
                                   capture_output=True, text=True, check=True).stdout.strip())
        start = rng.uniform(0.05, 0.9) * max(0.1, dur - 2.0)
        h = heights[i]
        pattern = work / f"t{s['id']:02d}_%d.png"
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", f"{start:.3f}", "-i", video, "-frames:v", str(READS),
                        "-vf", f"fps=4,scale={h * 16 // 9}:{h}:flags=lanczos", str(pattern)], check=True)
        text = s["en"]
        if str(s["id"]) in speakers:
            text = f'{speakers[str(s["id"])]}: {text}'
        lines = textwrap.wrap(text, WRAP_CHARS)
        truth = canon(" ".join(lines))
        reads = []
        for k in range(1, READS + 1):
            bg = work / f"t{s['id']:02d}_{k}.png"
            if not bg.exists():
                break
            img, _, _, _ = render(bg, lines, styles[i], sizes[i], False)
            if conds[i] == "stream":
                img = stream_like(img)
            frame = cv2.cvtColor(np.asarray(img), cv2.COLOR_RGB2BGR)
            fh, fw = frame.shape[:2]
            area = np.ascontiguousarray(frame[int(BAND[0] * fh):int(BAND[1] * fh), int(BAND[2] * fw):int(BAND[3] * fw)])
            raw, _, _ = ocr.read(area)
            white, _, _ = ocr.read(white_text_only(area))
            a, b = centred(raw, area.shape[1]), centred(white, area.shape[1])
            reads.append(" ".join((b if wordiness(b) > wordiness(a) else a).split()))
            bg.unlink()
        rows.append({"id": s["id"], "video": Path(video).name, "style": styles[i], "height": h, "cond": conds[i],
                     "truth": truth, "reads": reads})

    print("| okuma sayısı | sözcükler doğru % | ortalama CER % |\n|---|---|---|")
    summary = {}
    for count in (1, 2, 3, 4, 6):
        ok, cer = 0, 0.0
        for r in rows:
            got = canon(consensus(r["reads"][:count]))
            ok += got == r["truth"]
            cer += min(1.0, levenshtein(got, r["truth"]) / max(1, len(r["truth"])))
        summary[count] = {"words_ok_pct": round(100 * ok / len(rows), 1), "cer_pct": round(100 * cer / len(rows), 1)}
        print(f"| {count} | {summary[count]['words_ok_pct']} | {summary[count]['cer_pct']} |")
    any_ok = sum(any(canon(x) == r["truth"] for x in r["reads"]) for r in rows)
    summary["any_read_ok_pct"] = round(100 * any_ok / len(rows), 1)
    print(f"\n6 okumadan en az biri doğru: {summary['any_read_ok_pct']} %")
    Path(args.out).write_text(json.dumps({"summary": summary, "rows": rows}, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

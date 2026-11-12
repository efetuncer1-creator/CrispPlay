"""Score real_scan.py output against a hand-made transcript of a gameplay clip (Phase 5 spike).

    python -I -X utf8 real_report.py --scan scan.json --transcript transcript.tsv --frames frames \
        --model ..\\deps\\models\\opus-mt-tc-big-en-tr-ct2-int8 --out real.json

transcript.tsv: start second <TAB> SPEAKER <TAB> English line <TAB> Turkish reference, one subtitle per
row, in order. It is a transcription of a game's script, so it stays outside the repo.

Which frames show a subtitle is decided without OCR: this clip's subtitles are
yellow, so a frame counts when the subtitle rows hold enough yellow pixels
(--min-yellow). A frame belongs to the transcript row whose start time is the
latest one not after it.
"""
import argparse
import collections
import json
import re
import statistics
import sys
from pathlib import Path

import cv2
import numpy as np
import sacrebleu

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops the script folder from sys.path
from mt_bench import Model, pct, split_sentences
from ocr_bench import canon, levenshtein, wordiness
from ocr_temporal import consensus

SPEAKER = re.compile(r"^([A-Z][A-Z' ]{1,18}):\s*")
READERS = ["win_raw", "win_white", "win_pick2", "hybrid", "combo", "win_raw+fix", "hybrid+fix", "combo+fix"]
LONE_L = re.compile(r"(?<![A-Za-z0-9])l(?=(?:'(?:m|ll|ve|d|s))?(?![A-Za-z0-9]))")


def fix(text):
    """Small text repairs of the kind CouchSubs' OcrNoise does (written from its description):
    a lone "l" is "I" ("l've" -> "I've"), a reading that is the same text twice is that text once,
    stray symbols before the first letter go."""
    text = LONE_L.sub("I", text)
    text = re.sub(r"^[^A-Za-z0-9\"]+", "", text)
    words = text.split()
    half = len(words) // 2
    if half >= 2 and len(words) % 2 == 0 and words[:half] == words[half:]:
        text = " ".join(words[:half])
    return text


def tidy(text):
    return " ".join(text.replace("’", "'").replace("‘", "'").replace("“", '"').replace("”", '"').split())


def block(lines, width):
    """The subtitle block: the lowest centred line plus centred lines stacked directly above it."""
    lines = [l for l in lines if re.search(r"[A-Za-z0-9]", l["text"]) and l["box"][0] < 0.6 * width and l["box"][2] > 0.4 * width]
    lines.sort(key=lambda l: l["box"][1])
    if not lines:
        return ""
    keep = [lines[-1]]
    for line in reversed(lines[:-1]):
        height = keep[0]["box"][3] - keep[0]["box"][1]
        if keep[0]["box"][1] - line["box"][3] < 0.9 * height:
            keep.insert(0, line)
        else:
            break
    return tidy(" ".join(l["text"] for l in keep))


def readings(row):
    width = row["size"][0]
    out = {"win_raw": block(row["raw"], width), "win_white": block(row["white"], width), "hybrid": block(row["hybrid"], width)}
    out["win_pick2"] = out["win_white"] if wordiness(out["win_white"]) > wordiness(out["win_raw"]) else out["win_raw"]
    # combo: the hybrid reading unless the Windows reading looks clearly more like words.
    out["combo"] = out["win_pick2"] if wordiness(out["win_pick2"]) > wordiness(out["hybrid"]) else out["hybrid"]
    fixed_raw, fixed_hybrid = fix(out["win_raw"]), fix(out["hybrid"])
    out["win_raw+fix"], out["hybrid+fix"] = fixed_raw, fixed_hybrid
    # Without the white-text reading (it hurts on this clip): Windows raw vs hybrid, after the repairs.
    out["combo+fix"] = fixed_raw if wordiness(fixed_raw) > wordiness(fixed_hybrid) else fixed_hybrid
    return out


def yellow(path):
    img = cv2.imread(str(path))[300:400, 250:1670].astype(np.int32)
    b, g, r = img[..., 0], img[..., 1], img[..., 2]
    return int(((r > 170) & (g > 140) & (b < 0.55 * g)).sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", required=True)
    ap.add_argument("--transcript", required=True)
    ap.add_argument("--frames", required=True)
    ap.add_argument("--min-yellow", type=int, default=250)
    ap.add_argument("--model", default="")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    scan = json.loads(Path(args.scan).read_text(encoding="utf-8"))
    subs = []
    for line in Path(args.transcript).read_text(encoding="utf-8").splitlines():
        start, speaker, en, tr = line.split("\t")
        subs.append({"start": float(start), "speaker": speaker, "en": en, "tr": tr, "truth": tidy(f"{speaker}: {en}"), "frames": []})
    starts = [s["start"] for s in subs]

    empty = []
    for row in scan:
        row["read"] = readings(row)
        if yellow(Path(args.frames) / row["file"]) < args.min_yellow:
            empty.append(row)
            continue
        index = max((i for i, s in enumerate(starts) if s <= row["t"]), default=None)
        if index is not None:
            subs[index]["frames"].append(row)
    subs = [s for s in subs if s["frames"]]
    frames = [(s, r) for s in subs for r in s["frames"]]

    def ok(text, truth):
        return canon(text) == canon(truth)

    def cer(text, truth):
        return min(1.0, levenshtein(tidy(text), truth) / len(truth))

    print(f"{len(subs)} altyazı, {len(frames)} altyazılı kare, {len(empty)} altyazısız kare\n")
    print("### Kare başına\n\n| okuyucu | sözcükler doğru % | etiket hariç doğru % | CER % | boş % |\n|---|---|---|---|---|")
    summary = {"subs": len(subs), "sub_frames": len(frames), "empty_frames": len(empty), "frame": {}, "sub": {}}
    for name in READERS:
        good = sum(ok(r["read"][name], s["truth"]) for s, r in frames)
        body = sum(ok(SPEAKER.sub("", r["read"][name]), s["en"]) for s, r in frames)
        err = statistics.mean(cer(r["read"][name], s["truth"]) for s, r in frames)
        none = sum(not r["read"][name] for s, r in frames)
        summary["frame"][name] = {"ok": round(100 * good / len(frames), 1), "body_ok": round(100 * body / len(frames), 1),
                                  "cer": round(100 * err, 1), "empty": round(100 * none / len(frames), 1)}
        print(f"| {name} | {100 * good / len(frames):.1f} | {100 * body / len(frames):.1f} | {100 * err:.1f} | {100 * none / len(frames):.1f} |")

    print("\n### Altyazı başına (2 okuma/sn)\n\n| okuyucu | ilk kare doğru % | ilk 3 karenin ortak okuması % | art arda aynı iki okuma (ilk gönderilen) % | en sık okuma (ekranda kalan) % | herhangi bir kare % |
|---|---|---|---|---|---|")
    final = {}
    for name in READERS:
        first = cons = stable = anyok = mode = 0
        for s in subs:
            texts = [r["read"][name] for r in s["frames"]]
            first += ok(texts[0], s["truth"])
            cons += ok(consensus(texts[:3]), s["truth"])
            anyok += any(ok(t, s["truth"]) for t in texts)
            # Stabiliser stand-in: the first reading that repeats on the next frame; a one-frame subtitle is sent as read.
            sent = next((a for a, b in zip(texts, texts[1:]) if a and canon(a) == canon(b)), consensus(texts))
            stable += ok(sent, s["truth"])
            # What stays on screen once later readings have replaced an early misread: the most frequent reading.
            common = collections.Counter(canon(t) for t in texts if t).most_common(1)
            settled = next((t for t in texts if common and canon(t) == common[0][0]), "")
            mode += ok(settled, s["truth"])
            if name == "combo+fix":
                final[s["start"]] = settled
        n = len(subs)
        summary["sub"][name] = {"first": round(100 * first / n, 1), "consensus3": round(100 * cons / n, 1),
                                "stable2": round(100 * stable / n, 1), "settled": round(100 * mode / n, 1),
                                "any": round(100 * anyok / n, 1)}
        print(f"| {name} | {100 * first / n:.1f} | {100 * cons / n:.1f} | {100 * stable / n:.1f} | {100 * mode / n:.1f} | {100 * anyok / n:.1f} |")

    print("\n### Altyazısız karelerde okunan yazı (yanlış alarm)\n")
    counts = collections.Counter(canon(r["read"]["combo+fix"]) for r in empty if r["read"]["combo+fix"])
    total = sum(counts.values())
    summary["false_alarm_frames"] = total
    print(f"{len(empty)} altyazısız karenin {total} tanesinde ({100 * total / max(1, len(empty)):.1f} %) altyazı bloğu gibi bir yazı okundu; "
          f"{len(counts)} farklı metin. En sık: " + "; ".join(f"\"{t[:40]}\" x{c}" for t, c in counts.most_common(5)))

    print("\n### Süre (kare başına, ms)\n\n| adım | p50 | p95 |\n|---|---|---|")
    for key, label in (("prep_ms", "beyaz yazı ön işlemi"), ("raw_ms", "Windows OCR (ham)"), ("white_ms", "Windows OCR (beyaz yazı)"),
                       ("rec_ms", "PP-OCR tanıma (bulunan satırlar)")):
        for group, rows in (("altyazılı", [r for _, r in frames]), ("altyazısız", empty)):
            v = [r[key] for r in rows]
            print(f"| {label}, {group} kare | {pct(v, 50):.1f} | {pct(v, 95):.1f} |")
    total_ms = [r["prep_ms"] + r["raw_ms"] + r["white_ms"] + r["rec_ms"] for _, r in frames]
    summary["ocr_total_ms"] = {"p50": round(pct(total_ms, 50), 1), "p95": round(pct(total_ms, 95), 1)}
    print(f"| toplam, altyazılı kare | {pct(total_ms, 50):.1f} | {pct(total_ms, 95):.1f} |")

    lines = []
    if args.model:
        mt = Model("opus", args.model, 2)

        def translate(text):
            body = SPEAKER.sub("", text)
            return " ".join(mt.translate(split_sentences(body), 4)) if body else ""
        refs = [s["tr"] for s in subs]
        clean = [translate(s["truth"]) for s in subs]
        dirty = [translate(final[s["start"]]) for s in subs]
        summary["mt"] = {"chrf_truth": round(sacrebleu.corpus_chrf(clean, [refs]).score, 1),
                         "bleu_truth": round(sacrebleu.corpus_bleu(clean, [refs]).score, 1),
                         "chrf_ocr": round(sacrebleu.corpus_chrf(dirty, [refs]).score, 1),
                         "bleu_ocr": round(sacrebleu.corpus_bleu(dirty, [refs]).score, 1),
                         "same_output_pct": round(100 * sum(a == b for a, b in zip(clean, dirty)) / len(subs), 1)}
        print(f"\n### Çeviri (OPUS-MT)\n\nDoğru metinden: chrF {summary['mt']['chrf_truth']}, BLEU {summary['mt']['bleu_truth']}. "
              f"OCR'ın okuduğu metinden (combo+fix, ekranda kalan okuma): chrF {summary['mt']['chrf_ocr']}, BLEU {summary['mt']['bleu_ocr']}; "
              f"altyazıların {summary['mt']['same_output_pct']} %'inde çeviri doğru metninkiyle aynı.")
        lines = [{"start": s["start"], "en": s["en"], "ref": s["tr"], "mt": c, "ocr": final[s["start"]], "mt_ocr": d}
                 for s, c, d in zip(subs, clean, dirty)]
    Path(args.out).write_text(json.dumps({"summary": summary, "lines": lines}, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

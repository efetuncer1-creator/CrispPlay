"""In-app regression check of the subtitle translation (custom/docs/SUBS.md).

Compares what the app showed while it played a clip (the JSON-lines event log
written with CRISPPLAY_SUBS_EVENT_LOG) with a transcript of that clip
(tab separated: seconds, speaker, English line, Turkish reference).

    python -I -X utf8 subs_regress.py --events out\\subs-test\\x\\events.jsonl \\
        --transcript ..\\deps\\subs-real\\transcript.tsv --out out\\subs-test\\x\\regress.json

The report holds numbers only. Transcript and event log are game text: they are
read at run time and never copied into the repository or the report. Use
--verbose to print the missed lines on the console while debugging.
"""
import argparse
import difflib
import json
import re
import statistics
import sys


def norm(text: str) -> str:
    return re.sub(r"\s+", " ", re.sub(r"[^0-9a-zà-ÿ ]+", " ", text.lower())).strip()


def similar(a: str, b: str) -> float:
    if not a or not b:
        return 0.0
    return difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--events", required=True)
    ap.add_argument("--transcript", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--min-shown", type=float, default=85.0, help="fail below this percentage of transcript lines shown")
    ap.add_argument("--window", type=float, default=8.0, help="seconds around the transcript time a show may lie in")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    transcript = []
    with open(args.transcript, encoding="utf-8") as f:
        for row in f:
            parts = row.rstrip("\n").split("\t")
            if len(parts) >= 3 and parts[2].strip():
                transcript.append({"t": float(parts[0]), "speaker": parts[1], "en": parts[2], "tr": parts[3] if len(parts) > 3 else ""})
    events = []
    with open(args.events, encoding="utf-8") as f:
        for row in f:
            row = row.strip()
            if row:
                events.append(json.loads(row))
    shows = [e for e in events if e["kind"] == "show"]
    samples = [e for e in events if e["kind"] == "sample"]
    if not samples:
        print("no samples in the event log: the feature did not run")
        return 2
    first, last = samples[0]["pos"], max(e["pos"] for e in samples)
    # Only the part of the clip that was actually played counts.
    played = [line for line in transcript if first - 0.5 <= line["t"] <= last - 3.0]

    used = set()
    delays, late, exact, speaker_ok, missed = [], 0, 0, 0, []
    for line in played:
        want = norm(line["en"])
        best, best_score = None, 0.0
        for i, show in enumerate(shows):
            if abs(show["pos"] - line["t"]) > args.window:
                continue
            got = norm(show["source"])
            score = similar(want, got)
            # A line the app showed together with its neighbour, or in two parts, still counts.
            if want and got and (want in got or got in want) and min(len(want), len(got)) >= 6:
                score = max(score, 0.9)
            if score > best_score:
                best, best_score = i, score
        if best is None or best_score < 0.75:
            missed.append(line)
            continue
        show = shows[best]
        used.add(best)
        delays.append(show["pos"] - line["t"])
        if show["durationMs"] < 15000:
            late += 1
        if norm(show["source"]) == want:
            exact += 1
        if not line["speaker"] or norm(line["speaker"]) == norm(show.get("speaker", "")):
            speaker_ok += 1
    in_range = [s for s in shows if first - 0.5 <= s["pos"] <= last]
    extra = [i for i, s in enumerate(shows) if i not in used and not s.get("replaces")]
    originals = sum(1 for s in in_range if s.get("original"))

    def pct(part, whole):
        return round(100.0 * part / whole, 1) if whole else 0.0

    def quantile(values, q):
        if not values:
            return 0.0
        ordered = sorted(values)
        return ordered[int((len(ordered) - 1) * q)]

    ocr = [e["ocrMs"] for e in samples if not e.get("reused")]
    latency = [s["latencyMs"] for s in shows if s.get("latencyMs", -1) >= 0 and not s.get("replaces")]
    report = {
        "playedSeconds": round(last - first, 1),
        "transcriptLines": len(played),
        "shown": len(played) - len(missed),
        "shownPct": pct(len(played) - len(missed), len(played)),
        "sourceExactPct": pct(exact, len(played)),
        "speakerOkPct": pct(speaker_ok, len(played) - len(missed)),
        "shownAfterLineLeft": late,
        "missed": len(missed),
        "showsTotal": len(in_range),
        "showsWithoutTranscriptLine": len(extra),
        "shownAsOriginal": originals,
        # Transcript times have a 0.5 s grid: these are +-0.5 s.
        "appearToShownSecMedian": round(statistics.median(delays), 2) if delays else 0,
        "appearToShownSecP90": round(quantile(delays, 0.9), 2),
        "sampleToShownMsMedian": round(statistics.median(latency)) if latency else 0,
        "sampleToShownMsP95": round(quantile(latency, 0.95)),
        "samples": len(samples),
        "ocrMsMedian": round(statistics.median(ocr), 1) if ocr else 0,
        "ocrMsP95": round(quantile(ocr, 0.95), 1),
        "readyMsMedian": round(statistics.median(e["readyMs"] for e in samples), 2),
        "convertMsMedian": round(statistics.median(e["convertMs"] for e in samples), 2),
        "minShownPct": args.min_shown,
    }
    report["passed"] = report["shownPct"] >= args.min_shown
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    print(json.dumps(report, indent=2))
    if args.verbose:
        for line in missed:
            print(f"MISSED {line['t']:7.1f}s {line['en']}")
        for i in extra:
            print(f"EXTRA  {shows[i]['pos']:7.1f}s {shows[i]['source']} -> {shows[i]['text']}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

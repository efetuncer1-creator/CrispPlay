"""Tables from ocr_bench.py results (Markdown on stdout).

    python -I -X utf8 ocr_report.py winocr.json [rapidocr.json] [--manifest testset_manifest.json]
"""
import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path


def pct(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(round(p / 100 * (len(values) - 1))))]


def agg(rows, key="mid"):
    n = len(rows)
    return {
        "n": n,
        "cer": 100 * statistics.mean(r[f"cer_{key}"] for r in rows),
        "wer": 100 * statistics.mean(r[f"wer_{key}"] for r in rows),
        "exact": 100 * sum(r[f"exact_{key}"] for r in rows) / n,
        "canon": 100 * sum(r[f"canon_{key}"] for r in rows) / n,
        "empty": 100 * sum(not r[key].strip() for r in rows) / n,
        "p50": pct([r["ms"] for r in rows], 50),
        "p95": pct([r["ms"] for r in rows], 95),
    }


def line(label, a):
    return (f"| {label} | {a['n']} | {a['cer']:.1f} | {a['wer']:.1f} | {a['exact']:.0f} | {a['canon']:.0f} | "
            f"{a['empty']:.0f} | {a['p50']:.1f} | {a['p95']:.1f} |")


HEAD = "| {} | n | CER % | WER % | birebir % | sözcükler doğru % | boş % | p50 ms | p95 ms |\n|---|---|---|---|---|---|---|---|---|"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results", nargs="+")
    ap.add_argument("--manifest", default=str(Path(__file__).resolve().parent / "testset_manifest.json"))
    ap.add_argument("--focus", default="band:pick2", help="crop:prep used for the per-factor breakdown")
    args = ap.parse_args()
    items = {i["file"]: i for i in json.loads(Path(args.manifest).read_text(encoding="utf-8"))["items"]}

    for path in args.results:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        rows = data["rows"]
        print(f"\n## {data['engine']}\n")
        groups = defaultdict(list)
        for r in rows:
            groups[(r["crop"], r["prep"])].append(r)
        print(HEAD.format("kırpma / ön işlem"))
        for (crop, prep), g in groups.items():
            print(line(f"{crop} / {prep}", agg(g)))
        print("\nHUD satırları ayıklanmadan (bütün okunan metin), yalnız ham okuma:\n")
        print(HEAD.format("kırpma"))
        for (crop, prep), g in groups.items():
            if prep == "raw":
                print(line(crop, agg(g, "all")))

        crop, prep = args.focus.split(":")
        focus = groups.get((crop, prep)) or groups[next(iter(groups))]
        print(f"\nKırılım ({crop} / {prep}):\n")
        print(HEAD.format("etken"))
        factors = {
            "çözünürlük": lambda i: f"{i['height']}p",
            "stil": lambda i: i["style"],
            "yazı yüksekliği": lambda i: f"{i['size_px']} px",
            "sıkıştırma": lambda i: i["cond"],
            "HUD": lambda i: "var" if i["hud"] else "yok",
            "zemin": lambda i: "açık (luma>=140)" if i["luma"] >= 140 else "orta" if i["luma"] >= 70 else "koyu (<70)",
            "arka planda yazı": lambda i: "var (v1)" if i["bg"].startswith("v1_") else "yok",
            "satır": lambda i: f"{len(i['lines'])} satır",
        }
        for name, fn in factors.items():
            parts = defaultdict(list)
            for r in focus:
                parts[fn(items[r["file"]])].append(r)
            for value in sorted(parts):
                print(line(f"{name}: {value}", agg(parts[value])))

        print("\nPiksel sayısına göre süre (ham okuma):\n")
        print("| kırpma | ortalama boyut | p50 ms | p95 ms | ms / megapiksel |\n|---|---|---|---|---|")
        for (c, p), g in groups.items():
            if p not in ("raw", "up2"):
                continue
            for h in (1080, 720):
                sub = [r for r in g if items[r["file"]]["height"] == h]
                if not sub:
                    continue
                w = statistics.mean(r["px"][0] for r in sub)
                hh = statistics.mean(r["px"][1] for r in sub)
                p50 = pct([r["ms"] for r in sub], 50)
                print(f"| {c} / {p} ({h}p) | {w:.0f}x{hh:.0f} | {p50:.1f} | {pct([r['ms'] for r in sub], 95):.1f} | {p50 / (w * hh / 1e6):.0f} |")


if __name__ == "__main__":
    main()

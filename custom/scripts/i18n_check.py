#!/usr/bin/env python3
"""Compare custom/i18n/tr.json with upstream i18n/catalog.json.

Run after every upstream sync (see custom/PLAN.md, "Upstream güncelleme prosedürü"):

    python custom/scripts/i18n_check.py            # report
    python custom/scripts/i18n_check.py --add-missing   # append untranslated entries
                                                        # with an empty "tr" to tr.json

Reports:
  * untranslated: upstream entries with no Turkish text (new upstream strings);
    the app shows the English text for them until they are translated
  * stale:        our entries whose zh/ctx key is no longer upstream (safe to delete)
  * en changed:   our "en" reference differs from upstream (meaning may have changed)
  * slot errors:  "{}"-style slots (same sequence as the zh source) or Qt "%N"
                  placeholders (same set as zh) differ; also a "%" directly
                  followed by a digit that is not a placeholder (Turkish writes
                  "%50", which Qt's arg() would treat as placeholder %50)

Exit code: 1 on slot errors or unreadable JSON, otherwise 0.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / "i18n" / "catalog.json"
OURS = ROOT / "custom" / "i18n" / "tr.json"
LANG = "tr"

SLOT = re.compile(r"\{[^{}]*\}")
PCT = re.compile(r"%(\d+)")


def key(e):
    ctx = e.get("ctx") or ""
    return (e.get("zh", ""), ctx)


def show(k):
    zh, ctx = k
    text = zh.replace("\n", "\\n")
    return f"{text} [ctx={ctx}]" if ctx else text


def slot_problems(zh, text):
    problems = []
    if SLOT.findall(text) != SLOT.findall(zh):
        problems.append(f"slots {SLOT.findall(text)} != source {SLOT.findall(zh)}")
    if set(PCT.findall(text)) != set(PCT.findall(zh)):
        problems.append(f"%N {sorted(set(PCT.findall(text)))} != source {sorted(set(PCT.findall(zh)))}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--add-missing", action="store_true", help="append untranslated upstream entries to tr.json")
    ap.add_argument("--quiet", action="store_true", help="print only the summary")
    args = ap.parse_args()
    # The Windows console code page cannot print Chinese source strings.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    try:
        upstream = json.loads(UPSTREAM.read_text(encoding="utf-8"))["entries"]
        ours_doc = json.loads(OURS.read_text(encoding="utf-8"))
        ours = ours_doc["entries"]
    except (OSError, ValueError, KeyError) as exc:
        print(f"ERROR: cannot read catalogs: {exc}")
        return 1

    up = {key(e): e for e in upstream}
    mine = {}
    dups = []
    for e in ours:
        k = key(e)
        if k in mine:
            dups.append(k)
        mine[k] = e

    untranslated = [k for k in up if not (mine.get(k, {}).get(LANG) or "").strip()]
    stale = [k for k in mine if k not in up]
    en_changed = [k for k in mine if k in up and mine[k].get("en") != up[k].get("en")]
    slot_errors = []
    for k, e in mine.items():
        text = e.get(LANG) or ""
        if text:
            for p in slot_problems(k[0], text):
                slot_errors.append((k, p))

    def section(title, items, fmt=show):
        print(f"\n{title}: {len(items)}")
        if not args.quiet:
            for item in items:
                print("  " + fmt(item))

    section("Untranslated (new upstream, shown in English until translated)", untranslated)
    section("Stale (no longer upstream, can be deleted)", stale)
    section("English reference changed upstream (check the translation)", en_changed)
    section("Duplicate keys in tr.json", dups)
    section("SLOT ERRORS", slot_errors, lambda x: f"{show(x[0])}: {x[1]}")

    translated = len(up) - len(untranslated)
    print(f"\nCoverage: {translated}/{len(up)} upstream entries translated")

    if args.add_missing and untranslated:
        for k in untranslated:
            if k in mine:
                continue
            src = up[k]
            entry = {"zh": src["zh"]}
            if src.get("ctx"):
                entry["ctx"] = src["ctx"]
            entry["en"] = src.get("en", "")
            entry[LANG] = ""
            ours.append(entry)
        with OURS.open("w", encoding="utf-8", newline="\n") as f:
            json.dump(ours_doc, f, ensure_ascii=False, indent=1)
            f.write("\n")
        print(f"Added {len(untranslated)} empty entries to {OURS.relative_to(ROOT)}")

    return 1 if slot_errors else 0


if __name__ == "__main__":
    sys.exit(main())

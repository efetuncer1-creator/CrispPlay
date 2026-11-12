"""English -> Turkish translation quality and CPU latency (Phase 5 spike).

One model per process so the memory figure is clean:

    python -I mt_bench.py --model opus --dir ..\\deps\\models\\opus-mt-tc-big-en-tr-ct2-int8 --out opus.json
    python -I mt_bench.py --model nllb --dir ..\\deps\\models\\nllb-200-distilled-600M-ct2-int8 --out nllb.json
    python -I mt_bench.py --model m2m  --dir ..\\deps\\models\\m2m100_418M-ct2-int8 --out m2m.json

All models are CTranslate2, int8, CPU. A subtitle is translated sentence by
sentence (one batch per subtitle), which is what CouchSubs does because the
model drops sentences otherwise; --whole translates the subtitle in one piece
to check that claim.
"""
import argparse
import json
import os
import re
import statistics
import time
from pathlib import Path

import ctranslate2
import psutil
import sacrebleu
import sentencepiece as spm

HERE = Path(__file__).resolve().parent
ABBREV = ("Mr.", "Mrs.", "Ms.", "Dr.", "St.", "Sgt.", "Lt.", "Capt.")


def split_sentences(text):
    """Split after . ! ? followed by a space and a capital; keep "Dr. Kovacs" together."""
    parts, start = [], 0
    for m in re.finditer(r"[.!?…]+[\"')]*\s+(?=[\"'(]*[A-Z0-9])", text):
        head = text[start:m.end()].strip()
        if head.endswith(ABBREV):
            continue
        parts.append(head)
        start = m.end()
    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return parts


class Model:
    def __init__(self, kind, path, threads):
        self.kind = kind
        self.translator = ctranslate2.Translator(str(path), device="cpu", compute_type="int8",
                                                 intra_threads=threads, inter_threads=1)
        if kind == "opus":
            self.src = spm.SentencePieceProcessor(model_file=str(Path(path) / "source.spm"))
            self.tgt = spm.SentencePieceProcessor(model_file=str(Path(path) / "target.spm"))
        else:
            self.src = self.tgt = spm.SentencePieceProcessor(model_file=str(Path(path) / "sentencepiece.bpe.model"))

    def encode(self, text):
        pieces = self.src.encode(text, out_type=str)
        if self.kind == "opus":
            return pieces + ["</s>"]
        if self.kind == "nllb":
            return ["eng_Latn"] + pieces + ["</s>"]
        return ["__en__"] + pieces + ["</s>"]

    def translate(self, sentences, beam):
        prefix = None
        if self.kind == "nllb":
            prefix = [["tur_Latn"]] * len(sentences)
        elif self.kind == "m2m":
            prefix = [["__tr__"]] * len(sentences)
        results = self.translator.translate_batch(
            [self.encode(s) for s in sentences], target_prefix=prefix, beam_size=beam,
            max_decoding_length=128, repetition_penalty=1.0)
        out = []
        for r in results:
            tokens = r.hypotheses[0]
            if prefix:
                tokens = tokens[1:]
            out.append(self.tgt.decode(tokens))
        return out


def pct(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(round(p / 100 * (len(values) - 1))))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", choices=["opus", "nllb", "m2m"], required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--sentences", default=str(HERE / "sentences.json"))
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--beam", type=int, default=4)
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--whole", action="store_true")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    data = json.loads(Path(args.sentences).read_text(encoding="utf-8"))["sentences"]
    proc = psutil.Process(os.getpid())
    rss0 = proc.memory_info().rss

    t0 = time.perf_counter()
    model = Model(args.model, args.dir, args.threads)
    load_ms = (time.perf_counter() - t0) * 1000
    t0 = time.perf_counter()
    model.translate(["Hello there."], args.beam)
    first_ms = (time.perf_counter() - t0) * 1000

    def run(text):
        parts = [text] if args.whole else split_sentences(text)
        return " ".join(model.translate(parts, args.beam))

    hyps, times = {}, {}
    for rep in range(args.repeat + 1):  # the first pass is a warm-up and is not timed
        for s in data:
            t0 = time.perf_counter()
            out = run(s["en"])
            ms = (time.perf_counter() - t0) * 1000
            if rep:
                times.setdefault(s["id"], []).append(ms)
            hyps[s["id"]] = out
    rss1 = proc.memory_info().rss
    per_line = [statistics.median(v) for v in times.values()]
    every = [ms for v in times.values() for ms in v]

    refs = [s["tr"] for s in data]
    outs = [hyps[s["id"]] for s in data]
    chrf = sacrebleu.corpus_chrf(outs, [refs]).score
    bleu = sacrebleu.corpus_bleu(outs, [refs]).score
    size = sum(f.stat().st_size for f in Path(args.dir).iterdir() if f.is_file())

    summary = {
        "model": args.model, "dir": Path(args.dir).name, "threads": args.threads, "beam": args.beam,
        "whole": args.whole, "disk_mb": round(size / 2**20), "load_ms": round(load_ms), "first_ms": round(first_ms),
        "rss_mb": round((rss1 - rss0) / 2**20), "p50_ms": round(pct(per_line, 50), 1),
        "p95_ms": round(pct(every, 95), 1), "max_ms": round(max(every), 1),
        "mean_ms": round(statistics.mean(every), 1), "chrf": round(chrf, 1), "bleu": round(bleu, 1),
    }
    print(json.dumps(summary))
    Path(args.out).write_text(json.dumps(
        {"summary": summary,
         "lines": [{"id": s["id"], "en": s["en"], "ref": s["tr"], "hyp": hyps[s["id"]],
                    "ms": round(statistics.median(times[s["id"]]), 1)} for s in data]},
        ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

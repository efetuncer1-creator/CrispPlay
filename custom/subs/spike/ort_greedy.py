"""ONNX Runtime int8 OPUS-MT, greedy decoding: the runtime CouchSubs ships (Phase 5 spike).

Only here to compare the per-sentence CPU cost with CTranslate2 at the same
decoding (run mt_bench.py with --beam 1). Greedy is not what a product would
use; beam search over the same sessions costs roughly the beam width more per step.

    python -I -X utf8 ort_greedy.py --dir ..\\deps\\models\\opus-mt-tc-big-en-tr-onnx-int8 --out ort.json
"""
import argparse
import json
import os
import statistics
import sys
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort
import psutil
import sacrebleu
import sentencepiece as spm

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops the script folder from sys.path
from mt_bench import pct, split_sentences

HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    root = Path(args.dir)
    proc = psutil.Process(os.getpid())
    rss0 = proc.memory_info().rss

    t0 = time.perf_counter()
    opts = ort.SessionOptions()
    opts.intra_op_num_threads = args.threads
    opts.inter_op_num_threads = 1
    enc = ort.InferenceSession(str(root / "onnx" / "encoder_model_int8.onnx"), opts, providers=["CPUExecutionProvider"])
    dec = ort.InferenceSession(str(root / "onnx" / "decoder_model_merged_int8.onnx"), opts, providers=["CPUExecutionProvider"])
    load_ms = (time.perf_counter() - t0) * 1000
    vocab = json.loads((root / "vocab.json").read_text(encoding="utf-8"))
    pieces = {v: k for k, v in vocab.items()}
    src = spm.SentencePieceProcessor(model_file=str(root / "source.spm"))
    tgt = spm.SentencePieceProcessor(model_file=str(root / "target.spm"))
    eos, pad, unk = vocab["</s>"], vocab["<pad>"], vocab["<unk>"]
    past_names = [i.name for i in dec.get_inputs() if i.name.startswith("past_key_values")]
    out_names = [o.name for o in dec.get_outputs()]

    def translate(sentence):
        ids = [vocab.get(p, unk) for p in src.encode(sentence, out_type=str)] + [eos]
        input_ids = np.array([ids], np.int64)
        mask = np.ones_like(input_ids)
        hidden = enc.run(None, {"input_ids": input_ids, "attention_mask": mask})[0]
        past = {n: np.zeros((1, 16, 0, 64), np.float32) for n in past_names}
        token, out = pad, []
        for step in range(128):
            feeds = {"encoder_attention_mask": mask, "input_ids": np.array([[token]], np.int64),
                     "encoder_hidden_states": hidden, "use_cache_branch": np.array([step > 0]), **past}
            res = dict(zip(out_names, dec.run(None, feeds)))
            logits = res["logits"][0, -1]
            logits[pad] = -np.inf
            token = int(logits.argmax())
            if token == eos:
                break
            out.append(token)
            for name in past_names:
                present = name.replace("past_key_values", "present")
                # The merged decoder returns the cross-attention cache only on the first step.
                if ".encoder." in name and step > 0:
                    continue
                past[name] = res[present]
        return tgt.decode([pieces[t] for t in out])

    data = json.loads((HERE / "sentences.json").read_text(encoding="utf-8"))["sentences"]
    t0 = time.perf_counter()
    translate("Hello there.")
    first_ms = (time.perf_counter() - t0) * 1000
    hyps, times = {}, {}
    for rep in range(args.repeat + 1):
        for s in data:
            t0 = time.perf_counter()
            hyps[s["id"]] = " ".join(translate(p) for p in split_sentences(s["en"]))
            if rep:
                times.setdefault(s["id"], []).append((time.perf_counter() - t0) * 1000)
    every = [ms for v in times.values() for ms in v]
    outs = [hyps[s["id"]] for s in data]
    refs = [s["tr"] for s in data]
    summary = {"runtime": "onnxruntime int8 greedy", "threads": args.threads, "load_ms": round(load_ms),
               "first_ms": round(first_ms), "rss_mb": round((proc.memory_info().rss - rss0) / 2**20),
               "p50_ms": round(pct([statistics.median(v) for v in times.values()], 50), 1),
               "p95_ms": round(pct(every, 95), 1), "max_ms": round(max(every), 1),
               "chrf": round(sacrebleu.corpus_chrf(outs, [refs]).score, 1)}
    print(json.dumps(summary))
    Path(args.out).write_text(json.dumps({"summary": summary, "hyps": hyps}, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

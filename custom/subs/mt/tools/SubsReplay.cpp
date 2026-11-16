// CrispPlay subtitle translation: offline replay of an in-app run (development tool).
// Part of CrispPlay, GPL-3.0 (see LICENSE).
//
// Reads the event log the app writes with CRISPPLAY_SUBS_EVENT_LOG (custom/docs/SUBS.md), feeds
// every OCR sample (texts, boxes, band geometry) through a fresh SubtitlePipeline on the sample's
// own clock, translates with the local OPUS-MT model (or an identity translator) and writes the
// sample and show events in the same format, so custom/subs/app/tools/subs_regress.py can score
// the replay like a real run. Translations are waited for after each sample: the result does not
// depend on the machine's speed. The log is game text: keep input and output out of the repository.
//
//   crispplay_subs_replay --events out\subs-test\x\events.jsonl --out out\replay\x.jsonl
//                         [--model <OPUS-MT ct2 folder> | --identity]
#include "crispplay/subs/Json.h"
#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/SpeakerRow.h"
#include "crispplay/subs/SubtitlePipeline.h"
#include "crispplay/subs/mt/OpusMtTranslator.h"
#include "crispplay/subs/ocr/SpellCheck.h"

#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace crispplay::subs;

namespace {

class Identity : public Translator {
public:
    std::optional<std::string> translate(const std::string& text) override { return text; }
};

double number(const json::Value& v, std::string_view key, double fallback = 0) {
    const auto* f = v.find(key);
    return f && f->isNumber() ? f->number : fallback;
}

OcrFrame frameOf(const json::Value& sample) {
    OcrFrame frame;
    frame.timeMs = static_cast<int64_t>(number(sample, "t"));
    if (const auto* size = sample.find("size"); size && size->isArray() && size->array.size() == 2) {
        frame.width = static_cast<int>(size->array[0].number);
        frame.height = static_cast<int>(size->array[1].number);
    }
    if (const auto* band = sample.find("band"); band && band->isArray() && band->array.size() == 4) {
        frame.bandLeft = static_cast<float>(band->array[0].number);
        frame.bandTop = static_cast<float>(band->array[1].number);
        frame.bandRight = static_cast<float>(band->array[2].number);
        frame.bandBottom = static_cast<float>(band->array[3].number);
    }
    const auto* lines = sample.find("lines");
    const auto* boxes = sample.find("boxes");
    if (!lines || !lines->isArray()) return frame;
    for (size_t i = 0; i < lines->array.size(); ++i) {
        OcrLine line;
        line.text = lines->array[i].string;
        if (boxes && boxes->isArray() && i < boxes->array.size() && boxes->array[i].array.size() >= 4) {
            const auto& b = boxes->array[i].array;
            line.box = {static_cast<float>(b[0].number), static_cast<float>(b[1].number), static_cast<float>(b[2].number),
                        static_cast<float>(b[3].number)};
            if (b.size() >= 5) line.confidence = static_cast<float>(b[4].number);
        }
        frame.lines.push_back(std::move(line));
    }
    return frame;
}

}  // namespace

int main(int argc, char** argv) {
    std::string events, out, model;
    bool identity = false;
    // The app's English word list (OcrNoise::setDictionary), unless --no-dictionary.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto spell = std::make_shared<ocr::SpellCheck>();
    if (spell->ready()) OcrNoise::setDictionary([spell](const std::string& w) { return spell->knows(w); });
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--events") events = next();
        else if (a == "--out") out = next();
        else if (a == "--model") model = next();
        else if (a == "--identity") identity = true;
        else if (a == "--no-dictionary") OcrNoise::setDictionary({});
        else if (a == "--words") {
            // Rule check of text lines (a transcript's third column, or one line per row):
            // broken-word count, looksBroken, and whether it reads like a name.
            std::ifstream lines(std::filesystem::u8path(next()), std::ios::binary);
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                std::string text = line;
                if (const auto tab = line.find('\t'); tab != std::string::npos) {
                    const auto second = line.find('\t', tab + 1);
                    if (second == std::string::npos) continue;
                    const auto third = line.find('\t', second + 1);
                    text = line.substr(second + 1, third == std::string::npos ? std::string::npos : third - second - 1);
                }
                std::cout << std::format("{}\t{}\t{}\t{}\n", OcrNoise::brokenWords(text), OcrNoise::looksBroken(text) ? 1 : 0,
                                         SpeakerRow::looksLikeName(text) ? 1 : 0, text);
            }
            return 0;
        }
        else { std::cerr << "unknown argument " << a << "\n"; return 2; }
    }
#ifdef CRISPPLAY_MT_DEFAULT_MODEL
    if (model.empty()) model = CRISPPLAY_MT_DEFAULT_MODEL;
#endif
    if (events.empty() || out.empty()) {
        std::cerr << "usage: crispplay_subs_replay --events <events.jsonl> --out <replay.jsonl> [--model <dir> | --identity] [--no-dictionary]\n";
        return 2;
    }
    std::ifstream in(std::filesystem::u8path(events), std::ios::binary);
    std::ofstream log(std::filesystem::u8path(out), std::ios::binary | std::ios::trunc);
    if (!in || !log) { std::cerr << "cannot open the event log or the output\n"; return 2; }

    std::unique_ptr<Translator> engine;
    if (identity) engine = std::make_unique<Identity>();
    else {
        auto opus = std::make_unique<mt::OpusMtTranslator>(std::filesystem::u8path(model));
        if (!opus->load()) { std::cerr << "model: " << opus->lastError() << "\n"; return 2; }
        engine = std::move(opus);
    }

    std::atomic<int64_t> clock{0};
    double pos = 0;
    int shows = 0, samples = 0;
    PipelineOptions options;
    options.clock = [&] { return clock.load(); };
    options.translator.cacheDirectory.reset();  // memory only: every run starts cold
    SubtitlePipeline pipeline(*engine, [&](const SubtitleUpdate& u) {
        if (u.kind != SubtitleUpdate::Kind::Show) return;
        ++shows;
        log << std::format("{{\"kind\":\"show\",\"t\":{},\"pos\":{:.3f},\"source\":{},\"speaker\":{},\"text\":{},\"original\":{},"
                           "\"durationMs\":{},\"replaces\":{},\"latencyMs\":-1}}\n",
                           clock.load(), pos, json::quote(u.source), json::quote(u.line.speaker), json::quote(u.line.text),
                           u.line.original ? "true" : "false", u.durationMs, u.replaces ? "true" : "false");
    }, options);

    std::string row;
    while (std::getline(in, row)) {
        if (row.empty() || row[0] != '{') continue;
        const auto value = json::parse(row);
        if (!value || !value->isObject()) continue;
        const auto* kind = value->find("kind");
        if (!kind || kind->string != "sample") continue;
        const OcrFrame frame = frameOf(*value);
        clock = frame.timeMs;
        pos = number(*value, "pos");
        log << std::format("{{\"kind\":\"sample\",\"t\":{},\"pos\":{:.3f},\"readyMs\":0,\"convertMs\":0,\"ocrMs\":0,\"reused\":false}}\n",
                           frame.timeMs, pos);
        pipeline.process(frame);
        pipeline.waitForTranslations();
        ++samples;
    }
    std::cout << std::format("samples={} shows={} translations={}\n", samples, shows, pipeline.translationCount());
    return 0;
}

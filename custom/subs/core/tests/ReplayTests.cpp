// CrispPlay subtitle core: offline replay of the Phase 5 spike's real clip (OCR results of
// deps\subs-real\scan.json) through the pipeline with an identity translator. The clip's text is
// a game script: it is read at run time from deps and never printed or committed; the test is
// skipped when the data is missing. Part of CrispPlay, GPL-3.0 (see LICENSE).
#include "Test.h"

#include "crispplay/subs/Json.h"
#include "crispplay/subs/Reading.h"
#include "crispplay/subs/SubtitlePipeline.h"
#include "crispplay/subs/TextMatch.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace crispplay::subs;

namespace {

std::filesystem::path realDir() {
    if (const char* env = std::getenv("CRISPPLAY_SUBS_REAL_DIR")) return env;
    return CRISPPLAY_SUBS_REAL_DIR;
}

std::optional<json::Value> readJson(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return json::parse(s.str());
}

class Identity : public Translator {
public:
    int calls = 0;
    std::optional<std::string> translate(const std::string& text) override {
        ++calls;
        return text;
    }
};

// The spike's Windows OCR output has one entry per word group; same-row groups are joined into
// lines unless the gap separates texts (Reading::isSeparateText), as the OCR stage will do.
std::vector<OcrLine> joinRows(std::vector<OcrLine> parts, int width) {
    std::sort(parts.begin(), parts.end(), [](const OcrLine& a, const OcrLine& b) {
        return a.box.y + a.box.h / 2 < b.box.y + b.box.h / 2;
    });
    std::vector<std::vector<OcrLine>> rows;
    for (auto& p : parts) {
        const float mid = p.box.y + p.box.h / 2;
        if (!rows.empty()) {
            const Rect& last = rows.back().front().box;
            if (std::abs(mid - (last.y + last.h / 2)) < std::max(last.h, p.box.h) * 0.5f) {
                rows.back().push_back(p);
                continue;
            }
        }
        rows.push_back({p});
    }
    std::vector<OcrLine> lines;
    for (auto& row : rows) {
        std::sort(row.begin(), row.end(), [](const OcrLine& a, const OcrLine& b) { return a.box.x < b.box.x; });
        OcrLine current = row.front();
        for (size_t i = 1; i < row.size(); ++i) {
            const float gap = row[i].box.x - current.box.right();
            if (Reading::isSeparateText(gap, std::max(current.box.h, row[i].box.h), width)) {
                lines.push_back(current);
                current = row[i];
            } else {
                current.text += " " + row[i].text;
                current.box = current.box.united(row[i].box);
            }
        }
        lines.push_back(current);
    }
    return lines;
}

}  // namespace

TEST(Replay_RealClipSegmentsMatchTheSpikeDraft) {
    const auto scan = readJson(realDir() / "scan.json");
    const auto draft = readJson(realDir() / "draft_segments.json");
    if (!scan || !draft || !scan->isArray() || !draft->isArray()) {
        subtest::skip("real clip data not found in " + realDir().string());
        return;
    }
    int64_t now = 0;
    Identity engine;
    std::vector<SubtitleUpdate> updates;
    PipelineOptions options;
    options.clock = [&] { return now; };
    options.translator.textPipeline = false;
    SubtitlePipeline pipeline(engine, [&](const SubtitleUpdate& u) { updates.push_back(u); }, options);

    bool coverOk = true;
    for (const auto& frame : scan->array) {
        const auto* t = frame.find("t");
        const auto* size = frame.find("size");
        const auto* raw = frame.find("raw");
        if (!t || !size || !raw || !size->isArray() || size->array.size() != 2) continue;
        OcrFrame f;
        f.timeMs = static_cast<int64_t>(t->number * 1000);
        f.width = static_cast<int>(size->array[0].number);
        f.height = static_cast<int>(size->array[1].number);
        // CouchSubs' default region, which the spike used.
        f.bandLeft = 0.14f, f.bandTop = 0.62f, f.bandRight = 0.86f, f.bandBottom = 0.97f;
        std::vector<OcrLine> parts;
        for (const auto& line : raw->array) {
            const auto* text = line.find("text");
            const auto* box = line.find("box");
            if (!text || !box || box->array.size() != 4) continue;
            const float x0 = static_cast<float>(box->array[0].number), y0 = static_cast<float>(box->array[1].number);
            const float x1 = static_cast<float>(box->array[2].number), y1 = static_cast<float>(box->array[3].number);
            parts.push_back({text->string, Rect{x0, y0, x1 - x0, y1 - y0}, -1.f});
        }
        f.lines = joinRows(std::move(parts), f.width);
        now = f.timeMs;
        const size_t before = updates.size();
        pipeline.process(f);
        pipeline.waitForTranslations();
        // Every shown cover holds the subtitle block's line boxes of the frame it came from.
        const Reading block = Reading::fromFrame(f).withoutButtonPrompts().withoutOcrNoise().subtitleBlock(
            TextBox{f.bandLeft, f.bandTop, f.bandRight, f.bandBottom});
        for (size_t i = before; i < updates.size(); ++i)
            if (updates[i].kind != SubtitleUpdate::Kind::Clear && !block.empty() && !updates[i].line.cover.contains(block.pixelBox()))
                coverOk = false;
    }

    // Segments the pipeline translated vs the spike's draft segmentation (73 subtitles, 77 draft
    // segments incl. hints): every draft segment should match a translated line.
    std::vector<std::string> sources;
    for (const auto& u : updates)
        if (u.kind == SubtitleUpdate::Kind::Show) sources.push_back(u.source);
    int matched = 0;
    for (const auto& segment : draft->array) {
        const auto* text = segment.find("draft");
        if (!text) continue;
        const auto want = TextMatch::normalize(text->string);
        for (const auto& s : sources)
            if (TextMatch::isSimilar(TextMatch::normalize(s), want, 0.8) || TextMatch::isSameSubtitle(TextMatch::normalize(s), want)) {
                ++matched;
                break;
            }
    }
    const int drafts = static_cast<int>(draft->array.size());
    std::printf("  replay: %zu frames, %d engine calls, %zu shown, %d/%d draft segments matched\n", scan->array.size(),
                engine.calls, sources.size(), matched, drafts);
    CHECK(coverOk);
    // Not much more translation work than there are segments, and nearly every segment found.
    CHECK(engine.calls <= drafts * 13 / 10);
    CHECK(matched >= drafts * 85 / 100);
}

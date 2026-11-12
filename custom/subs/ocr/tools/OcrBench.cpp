// crispplay_ocr_bench: runs the C++ OCR readers over the Phase 5 spike data and
// scores them the way custom/subs/spike/real_report.py and ocr_hybrid.py did.
//
//   crispplay_ocr_bench [--real DIR] [--synthetic DIR] [--manifest FILE] [--model FILE]
//                       [--threads N] [--trust C] [--limit N] [--no-real] [--no-synthetic] [--verbose]
//
// Real clip: DIR holds frames/ (1920x432 cuts starting at row 648 of 1080p,
// 2 per second), transcript.tsv and real.json (the spike's numbers). These are a
// game's text and pictures: they stay in deps\, never in the repo.
// Synthetic set: DIR holds frames/; the manifest is custom/subs/spike/testset_manifest.json.
#include <windows.h>
#include <psapi.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "crispplay/subs/ocr/HybridOcr.h"
#include "crispplay/subs/ocr/TextScore.h"

using namespace crispplay::subs;
using namespace crispplay::subs::ocr;

namespace {

// ---- text helpers (real_report.py / ocr_bench.py) -------------------------------

std::u32string u32(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const auto c = uint8_t(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (uint8_t(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
    return s;
}

std::string tidy(std::string s) {
    s = replaceAll(s, "\xE2\x80\x99", "'");
    s = replaceAll(s, "\xE2\x80\x98", "'");
    s = replaceAll(s, "\xE2\x80\x9C", "\"");
    s = replaceAll(s, "\xE2\x80\x9D", "\"");
    return collapseSpaces(s);
}

std::string canon(const std::string& s) {
    std::string out;
    bool space = false;
    for (char32_t c : u32(s)) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (space && !out.empty()) out.push_back(' ');
            space = false;
            out.push_back(char(c));
        } else {
            space = true;
        }
    }
    return out;
}

size_t levenshtein(const std::u32string& a, const std::u32string& b) {
    if (a == b) return 0;
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

double cer(const std::string& text, const std::string& truth) {
    const auto t = u32(truth);
    return std::min(1.0, double(levenshtein(u32(tidy(text)), t)) / double(std::max<size_t>(1, t.size())));
}

bool ok(const std::string& text, const std::string& truth) { return canon(text) == canon(truth); }

double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, size_t(std::nearbyint(p / 100.0 * double(v.size() - 1))));
    return v[i];
}

struct Line {
    std::string text;
    float x0, y0, x1, y1;
};

bool centred(const Line& l, int width) { return l.x0 < 0.6f * width && l.x1 > 0.4f * width; }

// The subtitle block: the lowest centred line plus centred lines stacked directly above it.
std::string block(std::vector<Line> lines, int width) {
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [&](const Line& l) { return !hasAlnum(l.text) || !centred(l, width); }),
                lines.end());
    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.y0 < b.y0; });
    if (lines.empty()) return {};
    std::vector<Line> keep{lines.back()};
    for (int i = int(lines.size()) - 2; i >= 0; --i) {
        const float height = keep.front().y1 - keep.front().y0;
        if (keep.front().y0 - lines[size_t(i)].y1 < 0.9f * height) keep.insert(keep.begin(), lines[size_t(i)]);
        else break;
    }
    std::string out;
    for (const auto& l : keep) out += (out.empty() ? "" : " ") + l.text;
    return tidy(out);
}

Line fromRect(const std::string& text, const Rect& r) { return {text, r.x, r.y, r.x + r.w, r.y + r.h}; }

std::string consensus(const std::vector<std::string>& readings) {
    if (readings.size() == 1) return readings[0];
    std::vector<std::u32string> keys;
    for (const auto& r : readings) keys.push_back(u32(canon(r)));
    size_t best = 0;
    size_t bestCost = SIZE_MAX;
    int bestWord = 0;
    for (size_t i = 0; i < readings.size(); ++i) {
        size_t cost = 0;
        for (const auto& k : keys) cost += levenshtein(keys[i], k);
        const int w = wordiness(readings[i]);
        if (cost < bestCost || (cost == bestCost && w > bestWord)) best = i, bestCost = cost, bestWord = w;
    }
    return readings[best];
}

BandImage load(const QString& path) {
    QImage img(path);
    if (img.isNull()) return {};
    img = img.convertToFormat(QImage::Format_ARGB32);  // BGRA in memory
    return fromBgra8(img.constBits(), img.width(), img.height(), int(img.bytesPerLine()));
}

std::string readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll().toStdString();
}

// ---- readers derived from one hybrid run ----------------------------------------

const char* kReaders[] = {"win_raw", "win_raw+fix", "windows_only", "hybrid+fix", "spike_combo+fix", "hybrid"};
constexpr int kReaderCount = 6;
constexpr int kOurs = 5;  // the library's output

std::vector<std::string> readings(const OcrFrame& frame, const HybridDetail& d, int width) {
    std::vector<Line> raw, winOnly, pp, ours;
    for (const auto& l : d.rawLines) raw.push_back(fromRect(l.text, l.box));
    for (const auto& r : d.rows) {
        const std::string w = chooseReading(r.windowsText, {}, false, true).text;
        if (!w.empty()) winOnly.push_back(fromRect(w, r.windowsBox));
        if (!r.ppocrText.empty()) pp.push_back(fromRect(r.ppocrText, r.cropBox));
    }
    for (const auto& l : frame.lines) ours.push_back(fromRect(l.text, l.box));
    std::vector<std::string> out(kReaderCount);
    out[0] = block(raw, width);
    out[1] = repairReading(out[0]);
    out[2] = block(winOnly, width);
    out[3] = repairReading(block(pp, width));
    out[4] = wordiness(out[1]) > wordiness(out[3]) ? out[1] : out[3];
    out[5] = block(ours, width);
    return out;
}

struct Args {
    QString real, synthetic, manifest, model;
    int threads = 2, limit = 0;
    float trust = -1.f;  // < 0: library default
    bool doReal = true, doSynthetic = true, verbose = false;
};

// ---- real clip ----------------------------------------------------------------

int runReal(HybridOcrReader& reader, const Args& a, bool havePp) {
    struct Sub { double start; std::string truth; std::vector<std::vector<std::string>> frames; };
    std::vector<Sub> subs;
    {
        const std::string tsv = readFile(a.real + "/transcript.tsv");
        if (tsv.empty()) {
            std::printf("real clip: %s/transcript.tsv missing, skipped\n", qPrintable(a.real));
            return 0;
        }
        size_t pos = 0;
        while (pos < tsv.size()) {
            size_t end = tsv.find('\n', pos);
            if (end == std::string::npos) end = tsv.size();
            std::string line = tsv.substr(pos, end - pos);
            pos = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> f;
            size_t s = 0;
            for (size_t t; (t = line.find('\t', s)) != std::string::npos; s = t + 1) f.push_back(line.substr(s, t - s));
            f.push_back(line.substr(s));
            if (f.size() < 3) continue;
            subs.push_back({std::stod(f[0]), tidy(f[1] + ": " + f[2]), {}});
        }
    }
    QDir dir(a.real + "/frames");
    QStringList files = dir.entryList({"*.png"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        std::printf("real clip: no frames in %s\n", qPrintable(dir.path()));
        return 0;
    }
    struct FrameRow { int sub; std::vector<std::string> read; HybridDetail d; };
    std::vector<FrameRow> rows;
    std::vector<double> totalSub, totalEmpty, winSub, ppSub, winEmpty, ppEmpty;
    int emptyFrames = 0, falseAlarm = 0;
    const int step = a.limit > 0 ? std::max<int>(1, int(files.size()) / a.limit) : 1;
    for (int n = 0; n < files.size(); n += step) {
        const BandImage cut = load(dir.filePath(files[n]));
        if (cut.empty()) continue;
        // yellow subtitle pixels (real_report.py yellow()): rows 300..400, cols 250..1670 of the cut.
        int yellow = 0;
        for (int y = 300; y < std::min(400, cut.height); ++y) {
            const uint8_t* p = cut.row(y);
            for (int x = 250; x < std::min(1670, cut.width); ++x) {
                const int b = p[4 * x], g = p[4 * x + 1], r = p[4 * x + 2];
                yellow += r > 170 && g > 140 && b < 0.55 * g;
            }
        }
        // Band: CouchSubs' default region (62..97 % height, 14..86 % width) of the 1080p picture.
        const int top = 648, fullH = 1080;
        const int y0 = std::max(0, int(0.62 * fullH) - top), y1 = std::min(cut.height, int(0.97 * fullH) - top);
        const BandImage band = crop(cut, int(0.14 * cut.width), y0, int(0.86 * cut.width), y1);
        FrameRow row;
        const OcrFrame frame = reader.read(band, int64_t(n) * 500, &row.d);
        row.read = readings(frame, row.d, band.width);
        const double t = n / 2.0;
        row.sub = -1;
        if (yellow >= 250) {
            for (int i = 0; i < int(subs.size()); ++i)
                if (subs[size_t(i)].start <= t) row.sub = i;
            totalSub.push_back(row.d.totalMs);
            winSub.push_back(row.d.windowsMs);
            ppSub.push_back(row.d.ppocrMs);
        } else {
            ++emptyFrames;
            falseAlarm += !row.read[kOurs].empty();
            totalEmpty.push_back(row.d.totalMs);
            winEmpty.push_back(row.d.windowsMs);
            ppEmpty.push_back(row.d.ppocrMs);
        }
        if (row.sub >= 0) subs[size_t(row.sub)].frames.push_back(row.read);
        rows.push_back(std::move(row));
        if (n % 200 == 0) std::fprintf(stderr, "%d/%d\n", n, int(files.size()));
    }
    subs.erase(std::remove_if(subs.begin(), subs.end(), [](const Sub& s) { return s.frames.empty(); }), subs.end());
    size_t subFrames = 0;
    for (const auto& s : subs) subFrames += s.frames.size();

    std::printf("\n## Real clip (%s)\n\n%d subtitles, %d subtitle frames, %d empty frames%s\n", qPrintable(a.real),
                int(subs.size()), int(subFrames), emptyFrames, havePp ? "" : "  [PP-OCR OFF]");
    std::printf("\n### Per frame\n\n| reader | words ok %% | CER %% | empty %% |\n|---|---|---|---|\n");
    for (int k = 0; k < kReaderCount; ++k) {
        int good = 0, none = 0;
        double err = 0;
        for (const auto& s : subs)
            for (const auto& f : s.frames) good += ok(f[k], s.truth), none += f[k].empty(), err += cer(f[k], s.truth);
        std::printf("| %s | %.1f | %.1f | %.1f |\n", kReaders[k], 100.0 * good / subFrames, 100.0 * err / subFrames,
                    100.0 * none / subFrames);
    }
    std::printf("\n### Per subtitle\n\n| reader | first frame %% | first sent (two equal reads) %% | settled (most frequent) %% | any frame %% |\n|---|---|---|---|---|\n");
    for (int k = 0; k < kReaderCount; ++k) {
        int first = 0, stable = 0, settled = 0, any = 0;
        for (const auto& s : subs) {
            std::vector<std::string> texts;
            for (const auto& f : s.frames) texts.push_back(f[k]);
            first += ok(texts[0], s.truth);
            bool anyOk = false;
            for (const auto& t : texts) anyOk |= ok(t, s.truth);
            any += anyOk;
            std::string sent;
            bool found = false;
            for (size_t i = 0; i + 1 < texts.size(); ++i)
                if (!texts[i].empty() && canon(texts[i]) == canon(texts[i + 1])) { sent = texts[i]; found = true; break; }
            if (!found) sent = consensus(texts);
            stable += ok(sent, s.truth);
            std::map<std::string, int> counts;
            std::vector<std::string> order;
            for (const auto& t : texts)
                if (!t.empty()) {
                    if (!counts.count(canon(t))) order.push_back(canon(t));
                    ++counts[canon(t)];
                }
            std::string mode;  // Counter.most_common: highest count, first seen wins ties
            int best = 0;
            for (const auto& c : order)
                if (counts[c] > best) best = counts[c], mode = c;
            std::string settledText;
            for (const auto& t : texts)
                if (!mode.empty() && canon(t) == mode) { settledText = t; break; }
            const bool settledOk = ok(settledText, s.truth);
            settled += settledOk;
            if (a.verbose && k == kOurs && !settledOk)
                std::printf("  MISS %.1fs\n    truth: %s\n    read : %s\n", s.start, s.truth.c_str(), settledText.c_str());
        }
        const double n = double(subs.size());
        std::printf("| %s | %.1f | %.1f | %.1f | %.1f |\n", kReaders[k], 100 * first / n, 100 * stable / n,
                    100 * settled / n, 100 * any / n);
    }
    std::printf("\nFalse alarms (library output on frames without a subtitle): %d of %d (%.1f %%)\n", falseAlarm,
                emptyFrames, 100.0 * falseAlarm / std::max(1, emptyFrames));
    std::printf("\n### Latency per frame (ms, band %dx%d)\n\n| step | subtitle p50 | p95 | empty p50 | p95 |\n|---|---|---|---|---|\n",
                1383, 378);
    std::printf("| Windows OCR | %.1f | %.1f | %.1f | %.1f |\n", pct(winSub, 50), pct(winSub, 95), pct(winEmpty, 50), pct(winEmpty, 95));
    std::printf("| PP-OCR rec (found rows) | %.1f | %.1f | %.1f | %.1f |\n", pct(ppSub, 50), pct(ppSub, 95), pct(ppEmpty, 50), pct(ppEmpty, 95));
    std::printf("| total read() | %.1f | %.1f | %.1f | %.1f |\n", pct(totalSub, 50), pct(totalSub, 95), pct(totalEmpty, 50), pct(totalEmpty, 95));

    const QJsonObject spike = QJsonDocument::fromJson(QByteArray::fromStdString(readFile(a.real + "/real.json"))).object()["summary"].toObject();
    if (!spike.isEmpty()) {
        std::printf("\nSpike (Python, real.json) per subtitle first/settled: ");
        for (const char* name : {"win_raw", "win_raw+fix", "hybrid+fix", "combo+fix"}) {
            const auto o = spike["sub"].toObject()[name].toObject();
            std::printf("%s %.1f/%.1f; ", name, o["first"].toDouble(), o["settled"].toDouble());
        }
        std::printf("per frame combo+fix %.1f %% ok, CER %.1f %%\n", spike["frame"].toObject()["combo+fix"].toObject()["ok"].toDouble(),
                    spike["frame"].toObject()["combo+fix"].toObject()["cer"].toDouble());
    }
    return 0;
}

// ---- synthetic set --------------------------------------------------------------

int runSynthetic(HybridOcrReader& reader, const Args& a) {
    const QJsonArray items = QJsonDocument::fromJson(QByteArray::fromStdString(readFile(a.manifest))).object()["items"].toArray();
    if (items.isEmpty()) {
        std::printf("synthetic: manifest %s missing, skipped\n", qPrintable(a.manifest));
        return 0;
    }
    struct Row { bool bg; std::string style; bool winOk, ourOk; double winCer, ourCer, ms; };
    std::vector<Row> rows;
    const int step = a.limit > 0 ? std::max<int>(1, int(items.size()) / a.limit) : 1;
    for (int i = 0; i < items.size(); i += step) {
        const QJsonObject item = items[i].toObject();
        const BandImage frame = load(a.synthetic + "/frames/" + item["file"].toString());
        if (frame.empty()) continue;
        const int w = frame.width, h = frame.height;
        const BandImage band = crop(frame, int(0.14 * w), int(0.62 * h), int(0.86 * w), int(0.97 * h));
        HybridDetail d;
        const OcrFrame out = reader.read(band, 0, &d);
        std::string truth;
        for (const auto& l : item["lines"].toArray()) truth += (truth.empty() ? "" : " ") + l.toString().toStdString();
        // Centred lines, top to bottom (ocr_hybrid.py `mid`).
        std::string win, ours;
        std::vector<std::pair<float, std::string>> wl, ol;
        const auto& winLines = d.usedWhiteFallback ? d.rawLines : d.rawLines;
        for (const auto& l : winLines)
            if (centred(fromRect(l.text, l.box), band.width)) wl.push_back({l.box.y, l.text});
        for (const auto& l : out.lines)
            if (centred(fromRect(l.text, l.box), band.width)) ol.push_back({l.box.y, l.text});
        std::stable_sort(ol.begin(), ol.end(), [](auto& x, auto& y) { return x.first < y.first; });
        for (auto& [y, t] : wl) win += (win.empty() ? "" : " ") + t;
        for (auto& [y, t] : ol) ours += (ours.empty() ? "" : " ") + t;
        win = collapseSpaces(win), ours = collapseSpaces(ours);
        if (a.verbose && !ok(ours, truth)) {
            std::printf("  MISS %s [%s]\n    truth: %s\n    win  : %s\n    ours : %s\n", qPrintable(item["file"].toString()),
                        qPrintable(item["style"].toString()), truth.c_str(), win.c_str(), ours.c_str());
            for (const auto& r : d.rows)
                std::printf("      row y%.0f h%.0f crop %.0f..%.0f | W: %s | P: %s (%.2f)%s\n", r.windowsBox.y, r.windowsBox.h,
                            r.cropBox.x, r.cropBox.x + r.cropBox.w, r.windowsText.c_str(), r.ppocrText.c_str(),
                            r.ppocrConfidence, r.ppocrChosen ? " *P" : "");
        }
        rows.push_back({item["bg"].toString().startsWith("v1_"), item["style"].toString().toStdString(), ok(win, truth),
                        ok(ours, truth), cer(win, truth), cer(ours, truth), d.totalMs});
    }
    std::printf("\n## Synthetic set (%s, %d frames)\n\n| subset | n | Windows raw words ok %% | CER %% | library words ok %% | CER %% |\n|---|---|---|---|---|---|\n",
                qPrintable(a.synthetic), int(rows.size()));
    auto table = [&](const char* label, auto pred) {
        int n = 0, wo = 0, oo = 0;
        double wc = 0, oc = 0;
        for (const auto& r : rows)
            if (pred(r)) ++n, wo += r.winOk, oo += r.ourOk, wc += r.winCer, oc += r.ourCer;
        if (n) std::printf("| %s | %d | %.0f | %.1f | %.0f | %.1f |\n", label, n, 100.0 * wo / n, 100 * wc / n, 100.0 * oo / n, 100 * oc / n);
    };
    table("all", [](const Row&) { return true; });
    table("no text behind", [](const Row& r) { return !r.bg; });
    table("text behind", [](const Row& r) { return r.bg; });
    std::vector<std::string> styles;
    for (const auto& r : rows)
        if (std::find(styles.begin(), styles.end(), r.style) == styles.end()) styles.push_back(r.style);
    std::sort(styles.begin(), styles.end());
    for (const auto& s : styles) table((s + " (no text behind)").c_str(), [&](const Row& r) { return r.style == s && !r.bg; });
    std::vector<double> ms;
    for (const auto& r : rows) ms.push_back(r.ms);
    std::printf("\nread() p50 %.1f ms, p95 %.1f ms\n", pct(ms, 50), pct(ms, 95));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    a.real = QString(CRISPPLAY_DEPS_DIR) + "/subs-real";
    a.synthetic = QString(CRISPPLAY_DEPS_DIR) + "/subs-testset";
    a.manifest = QString(CRISPPLAY_SPIKE_DIR) + "/testset_manifest.json";
    a.model = CRISPPLAY_PPOCR_MODEL;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto next = [&]() { return i + 1 < argc ? QString::fromLocal8Bit(argv[++i]) : QString(); };
        if (k == "--real") a.real = next();
        else if (k == "--synthetic") a.synthetic = next();
        else if (k == "--manifest") a.manifest = next();
        else if (k == "--model") a.model = next();
        else if (k == "--windows-only") a.model.clear();
        else if (k == "--threads") a.threads = next().toInt();
        else if (k == "--trust") a.trust = next().toFloat();
        else if (k == "--limit") a.limit = next().toInt();
        else if (k == "--no-real") a.doReal = false;
        else if (k == "--no-synthetic") a.doSynthetic = false;
        else if (k == "--verbose") a.verbose = true;
        else {
            std::printf("usage: crispplay_ocr_bench [--real DIR] [--synthetic DIR] [--manifest FILE] [--model FILE | --windows-only]\n"
                        "                           [--threads N] [--trust C] [--limit N] [--no-real] [--no-synthetic] [--verbose]\n");
            return 2;
        }
    }
    HybridOptions options;
    options.ppocrModel = a.model.toStdWString();
    options.ppocrThreads = a.threads;
    if (a.trust >= 0.f) options.ppocrTrustAbove = a.trust;
    HybridOcrReader reader(options);
    const auto& ws = reader.windowsStatus();
    std::printf("Windows OCR: %s (%s)%s%s\n", ws.available ? "ready" : "UNAVAILABLE", ws.languageTag.c_str(),
                ws.message.empty() ? "" : " - ", ws.message.c_str());
    std::printf("PP-OCR: %s%s\n", reader.ppocrReady() ? "ready" : "off - ", reader.ppocrStatus().c_str());
    if (!reader.windowsReady()) return 1;
    if (a.doReal) runReal(reader, a, reader.ppocrReady());
    if (a.doSynthetic) runSynthetic(reader, a);
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        std::printf("\nPeak working set (whole bench process): %.0f MB\n", pmc.PeakWorkingSetSize / 1048576.0);
    return 0;
}

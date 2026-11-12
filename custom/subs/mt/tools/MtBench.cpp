// crispplay_mt_bench: C++ counterpart of custom/subs/spike/mt_bench.py.
//
//   crispplay_mt_bench [--model DIR] [--sentences sentences.json] [--real real.json]
//                      [--threads 2] [--beam 4] [--repeat 5] [--whole]
//                      [--compare python-output.json] [--out result.json]
//
// Translates the spike's synthetic sentences ({"sentences":[{id,en,tr}]}) and, when
// the file exists, the real-clip lines ({"lines":[{en,ref,mt}]}, mt = the Python
// wheel's output). Prints cold load time, first-call time, RSS growth, p50 of the
// per-line medians, p95/max over every timed call, corpus chrF (sacrebleu
// defaults: char order 6, beta 2, whitespace removed) and how many outputs are
// byte-identical to the Python ones (--compare for the synthetic set:
// mt_bench.py --out file, "lines":[{id,hyp}]).

#include <crispplay/subs/mt/OpusMtTranslator.h>
#include <crispplay/subs/mt/TextSplit.h>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using crispplay::subs::mt::OpusMtOptions;
using crispplay::subs::mt::OpusMtTranslator;

namespace {

struct Line {
    QString id;
    std::string en;
    std::string ref;
    std::string python; // empty if unknown
};

double rssMb()
{
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return double(pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

std::string u8(const QJsonValue& v)
{
    return v.toString().toStdString();
}

// --- chrF (sacrebleu 2.x CHRF(), corpus level) -----------------------------------
using Stats = std::vector<double>; // [hyp, ref, match] per order

std::vector<std::map<std::u32string, int>> charNgrams(const std::string& s, int order)
{
    std::u32string text;
    for (const char32_t c : QString::fromStdString(s).toUcs4()) {
        if (!QChar::isSpace(c)) text.push_back(c);
    }
    std::vector<std::map<std::u32string, int>> out(order);
    for (int n = 1; n <= order; ++n) {
        for (size_t i = 0; i + n <= text.size(); ++i) out[n - 1][text.substr(i, n)]++;
    }
    return out;
}

void addChrfStats(Stats& acc, const std::string& hyp, const std::string& ref, int order = 6)
{
    const auto h = charNgrams(hyp, order);
    const auto r = charNgrams(ref, order);
    for (int n = 0; n < order; ++n) {
        double nh = 0, nr = 0, nm = 0;
        for (const auto& [g, c] : h[n]) {
            nh += c;
            const auto it = r[n].find(g);
            if (it != r[n].end()) nm += std::min(c, it->second);
        }
        for (const auto& [g, c] : r[n]) nr += c;
        acc[3 * n] += nh;
        acc[3 * n + 1] += nr;
        acc[3 * n + 2] += nm;
    }
}

double chrfScore(const Stats& s, int order = 6, double beta = 2.0)
{
    double prec = 0, rec = 0;
    int effective = 0;
    for (int n = 0; n < order; ++n) {
        const double nh = s[3 * n], nr = s[3 * n + 1], nm = s[3 * n + 2];
        if (nh > 0 && nr > 0) {
            prec += nm / nh;
            rec += nm / nr;
            ++effective;
        }
    }
    if (!effective) return 0;
    prec /= effective;
    rec /= effective;
    const double f = beta * beta;
    if (prec + rec == 0) return 0;
    return 100.0 * (1 + f) * prec * rec / (f * prec + rec);
}

double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t idx = std::min(v.size() - 1, size_t(std::llround(p / 100.0 * double(v.size() - 1))));
    return v[idx];
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

struct SetResult {
    QJsonObject summary;
    QJsonArray lines;
};

SetResult runSet(const char* name, OpusMtTranslator& t, const std::vector<Line>& lines, int repeat)
{
    std::vector<std::vector<double>> times(lines.size());
    std::vector<std::string> hyps(lines.size());
    for (int rep = 0; rep <= repeat; ++rep) { // pass 0 is the warm-up
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto out = t.translate(lines[i].en);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!out) {
                std::fprintf(stderr, "translation failed: %s\n", lines[i].en.c_str());
                continue;
            }
            hyps[i] = *out;
            if (rep) times[i].push_back(ms);
        }
    }
    std::vector<double> perLine, every;
    Stats stats(18, 0.0), pyStats(18, 0.0);
    int same = 0, compared = 0, unk = 0;
    SetResult r;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (times[i].empty()) continue;
        perLine.push_back(median(times[i]));
        every.insert(every.end(), times[i].begin(), times[i].end());
        addChrfStats(stats, hyps[i], lines[i].ref);
        if (!lines[i].python.empty()) {
            ++compared;
            addChrfStats(pyStats, lines[i].python, lines[i].ref);
            if (hyps[i] == lines[i].python) {
                ++same;
            } else {
                std::printf("  [%s] differs from Python\n    en : %s\n    c++: %s\n    py : %s\n", name, lines[i].en.c_str(),
                            hyps[i].c_str(), lines[i].python.c_str());
            }
        }
        if (crispplay::subs::mt::containsUnknownMarker(hyps[i])) ++unk;
        r.lines.append(QJsonObject{{"id", lines[i].id},
                                   {"en", QString::fromStdString(lines[i].en)},
                                   {"ref", QString::fromStdString(lines[i].ref)},
                                   {"hyp", QString::fromStdString(hyps[i])},
                                   {"python", QString::fromStdString(lines[i].python)},
                                   {"ms", std::round(median(times[i]) * 10) / 10}});
    }
    double mean = 0;
    for (double v : every) mean += v;
    mean = every.empty() ? 0 : mean / double(every.size());
    r.summary = QJsonObject{{"lines", int(perLine.size())},
                            {"p50_ms", std::round(percentile(perLine, 50) * 10) / 10},
                            {"p95_ms", std::round(percentile(every, 95) * 10) / 10},
                            {"max_ms", std::round((every.empty() ? 0 : *std::max_element(every.begin(), every.end())) * 10) / 10},
                            {"mean_ms", std::round(mean * 10) / 10},
                            {"chrf", std::round(chrfScore(stats) * 10) / 10},
                            {"identical_to_python", compared ? QJsonValue(same) : QJsonValue()},
                            {"compared", compared},
                            {"chrf_python", compared ? QJsonValue(std::round(chrfScore(pyStats) * 10) / 10) : QJsonValue()},
                            {"with_unknown_marker", unk}};
    std::printf("%s: %s\n", name, QJsonDocument(r.summary).toJson(QJsonDocument::Compact).constData());
    return r;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    auto opt = [&](const QString& key, const QString& def) {
        const int i = args.indexOf(key);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : def;
    };
    const QString modelDir = opt("--model", QString::fromUtf8(CRISPPLAY_MT_DEFAULT_MODEL));
    const QString sentencesPath = opt("--sentences", QString::fromUtf8(CRISPPLAY_MT_DEFAULT_SENTENCES));
    const QString realPath = opt("--real", QString::fromUtf8(CRISPPLAY_MT_DEFAULT_REAL));
    const QString comparePath = opt("--compare", {});
    const QString outPath = opt("--out", {});
    OpusMtOptions o;
    o.intraThreads = size_t(opt("--threads", "2").toInt());
    o.beamSize = size_t(opt("--beam", "4").toInt());
    o.splitSentences = !args.contains("--whole");
    const int repeat = opt("--repeat", "5").toInt();

    std::unordered_map<std::string, std::string> pythonById;
    if (!comparePath.isEmpty()) {
        for (const auto v : readJson(comparePath)["lines"].toArray()) {
            const auto obj = v.toObject();
            pythonById[QString::number(obj["id"].toInt()).toStdString()] = u8(obj["hyp"]);
        }
    }
    std::vector<Line> synthetic, real;
    for (const auto v : readJson(sentencesPath)["sentences"].toArray()) {
        const auto obj = v.toObject();
        const QString id = QString::number(obj["id"].toInt());
        synthetic.push_back({id, u8(obj["en"]), u8(obj["tr"]), pythonById[id.toStdString()]});
    }
    int n = 0;
    for (const auto v : readJson(realPath)["lines"].toArray()) {
        const auto obj = v.toObject();
        real.push_back({QString::number(++n), u8(obj["en"]), u8(obj["ref"]), u8(obj["mt"])});
    }
    if (synthetic.empty() && real.empty()) {
        std::fprintf(stderr, "no sentences (%s)\n", sentencesPath.toUtf8().constData());
        return 2;
    }

    const double rss0 = rssMb();
    OpusMtTranslator t(std::filesystem::path(modelDir.toStdWString()), o);
    if (!t.load()) {
        std::fprintf(stderr, "model load failed: %s\n", t.lastError().c_str());
        return 3;
    }
    const auto f0 = std::chrono::steady_clock::now();
    t.translate("Hello there.");
    const double firstMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count();

    QJsonObject result{{"threads", int(o.intraThreads)}, {"beam", int(o.beamSize)}, {"whole", !o.splitSentences},
                       {"load_ms", std::round(t.loadTimeMs())}, {"first_ms", std::round(firstMs)}};
    std::printf("load %.0f ms, first call %.0f ms (threads %d, beam %d%s)\n", t.loadTimeMs(), firstMs,
                int(o.intraThreads), int(o.beamSize), o.splitSentences ? "" : ", whole line");
    if (!synthetic.empty()) {
        auto r = runSet("synthetic", t, synthetic, repeat);
        result["synthetic"] = r.summary;
        result["synthetic_lines"] = r.lines;
    }
    if (!real.empty()) {
        auto r = runSet("real", t, real, repeat);
        result["real"] = r.summary;
        result["real_lines"] = r.lines;
    }
    result["rss_mb"] = std::round(rssMb() - rss0);
    std::printf("rss growth %.0f MB\n", rssMb() - rss0);
    if (!outPath.isEmpty()) {
        QFile f(outPath);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(result).toJson());
    }
    return 0;
}

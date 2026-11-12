// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core tests, ported from CouchSubs windows/tests/CouchSubs.Translation.Tests/
// MarianTranslatorTests.cs (text flow cases; sample lines included), plus CrispPlay's
// unknown-mark (U+2047) and speaker label requirements.
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "Test.h"

#include "crispplay/subs/LineTranslator.h"
#include "crispplay/subs/Unicode.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <thread>

using namespace crispplay::subs;
using Opt = std::optional<std::string>;

namespace {

// Upper-cases the sentence (or maps it), records calls and notices concurrent calls.
class FakeEngine : public Translator {
public:
    std::function<Opt(const std::string&)> map;
    int delayMs = 0;
    std::vector<std::string> calls;
    std::atomic<int> active{0};
    bool sawConcurrentCall = false;
    std::mutex mutex;

    std::optional<std::string> translate(const std::string& s) override {
        if (++active > 1) sawConcurrentCall = true;
        if (delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        {
            std::lock_guard lock(mutex);
            calls.push_back(s);
        }
        --active;
        if (map) return map(s);
        return uni::encode(uni::toUpperInvariant(uni::decode(s)));
    }
};

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = std::filesystem::temp_directory_path() /
               ("crispplay-subs-lt-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::string full(LineTranslator& t, const std::string& line) {
    auto r = t.translate(line);
    return r ? r->full : std::string("<failed>");
}

}  // namespace

TEST(LineTranslator_CachesExactInput) {
    FakeEngine engine;
    LineTranslator t(engine, {});
    CHECK_EQ(full(t, "Wait!"), std::string("WAIT!"));
    CHECK_EQ(full(t, "Wait!"), std::string("WAIT!"));
    CHECK_EQ(engine.calls.size(), size_t(1));
    full(t, "wait!");
    CHECK_EQ(engine.calls.size(), size_t(2));
}

TEST(LineTranslator_ReusesCachedSentencesAcrossLines) {
    FakeEngine engine;
    LineTranslator t(engine, {});
    full(t, "We go to the mountain now. Stay behind me, boy.");
    CHECK_EQ(full(t, "We go to the mountain now. Stay behind me, boy. Keep your eyes open."),
             std::string("WE GO TO THE MOUNTAIN NOW. STAY BEHIND ME, BOY. KEEP YOUR EYES OPEN."));
    CHECK_EQ(engine.calls, (std::vector<std::string>{"We go to the mountain now.", "Stay behind me, boy.", "Keep your eyes open."}));
}

TEST(LineTranslator_KeepsLabelsAndSkipsPunctuationOnly) {
    FakeEngine engine;
    LineTranslator t(engine, {});
    auto line = t.translate("ATREUS: Father, look!\nKRATOS: Stay.");
    CHECK(line.has_value());
    CHECK_EQ(line->full, std::string("ATREUS: FATHER, LOOK!\nKRATOS: STAY."));
    CHECK_EQ(line->speaker, std::string("ATREUS"));
    CHECK_EQ(line->text, std::string("FATHER, LOOK!\nKRATOS: STAY."));
    CHECK_EQ(full(t, "..."), std::string("..."));
    CHECK_EQ(engine.calls, (std::vector<std::string>{"Father, look!", "Father, look! *** Stay."}));
}

TEST(LineTranslator_NamesFromLabelsStayCapitalizedInLaterSentences) {
    FakeEngine engine;
    LineTranslator t(engine, {});
    full(t, "ATREUS: Father, look!");
    full(t, "THE GATE WILL NOT HOLD, ATREUS.");
    CHECK_EQ(engine.calls.back(), std::string("The gate will not hold, Atreus."));
}

TEST(LineTranslator_PersistentCacheSkipsTheModelInTheNextSession) {
    TempDir dir;
    const auto file = dir.path / "cache.jsonl";
    {
        FakeEngine first;
        LineTranslator::Options o;
        o.cache = std::make_shared<TranslationCache>(file);
        LineTranslator(first, o).translate("KRATOS: We go now. Stay close.");
    }
    FakeEngine engine;
    LineTranslator::Options o;
    o.cache = std::make_shared<TranslationCache>(file);
    LineTranslator t(engine, o);
    CHECK_EQ(full(t, "KRATOS: We go now. Stay close."), std::string("KRATOS: WE GO NOW. STAY CLOSE."));
    CHECK_EQ(full(t, "We go now. Run to the boat, boy!"), std::string("WE GO NOW. RUN TO THE BOAT, BOY!"));
    CHECK_EQ(engine.calls, (std::vector<std::string>{"Stay close. *** We go now.", "Run to the boat, boy!"}));
}

TEST(LineTranslator_PersistentCacheInADirectory) {
    TempDir dir;
    {
        FakeEngine first;
        LineTranslator::Options o;
        o.cacheDirectory = dir.path;
        LineTranslator(first, o).translate("Hold the gate.");
    }
    CHECK(std::filesystem::exists(dir.path / "v1-opus_mt_p2-en-tr.jsonl"));
    FakeEngine engine;
    LineTranslator::Options o;
    o.cache = std::make_shared<TranslationCache>(dir.path / "v1-opus_mt_p2-en-tr.jsonl");
    LineTranslator t(engine, o);
    CHECK_EQ(full(t, "Hold the gate."), std::string("HOLD THE GATE."));
    CHECK(engine.calls.empty());
}

TEST(LineTranslator_SpeakerNamesAreNotTranslated) {
    const char* cases[][3] = {
        {"ATREUS: Father!", "ATREUS: FATHER!", "Father!"},
        {"OLD MAN: Who goes there?", "OLD MAN: WHO GOES THERE?", "Who goes there?"},
        {"Mimir Head: Brother, wait.", "Mimir Head: BROTHER, WAIT.", "Brother, wait."},
        {"Note: Press X.", "NOTE: PRESS X.", "Note: Press X."},
    };
    for (auto& c : cases) {
        FakeEngine engine;
        LineTranslator t(engine, {});
        CHECK_EQ(full(t, c[0]), std::string(c[1]));
        CHECK_EQ(engine.calls, (std::vector<std::string>{c[2]}));
    }
}

TEST(LineTranslator_MultiWordNamesStayCapitalizedInLaterSentences) {
    FakeEngine engine;
    LineTranslator t(engine, {});
    full(t, "OLD MAN: Hello.");
    full(t, "RUN, OLD MAN!");
    CHECK_EQ(engine.calls.back(), std::string("Hello. *** Run, Old Man!"));
}

TEST(LineTranslator_SerializesConcurrentCalls) {
    FakeEngine engine;
    engine.delayMs = 20;
    LineTranslator t(engine, {});
    std::thread a([&] { t.translate("We go to the mountain now."); });
    std::thread b([&] { t.translate("Stay behind me, boy, stay."); });
    a.join();
    b.join();
    CHECK(!engine.sawConcurrentCall);
    CHECK_EQ(engine.calls.size(), size_t(2));
}

TEST(LineTranslator_ShortLineIsTranslatedWithThePreviousLine) {
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt {
        if (s == "Where is your mother?") return "Annen nerede?";
        if (s == "Where is your mother? *** Not you.") return "Annen nerede? *** Sen değil.";
        return "?";
    };
    LineTranslator t(engine, {});
    full(t, "Where is your mother?");
    CHECK_EQ(full(t, "Not you."), std::string("Sen değil."));
    CHECK_EQ(full(t, "Not you."), std::string("Sen değil."));
    CHECK_EQ(engine.calls.size(), size_t(2));
}

TEST(LineTranslator_ShortLineAfterALongPauseHasNoContext) {
    int64_t now = 0;
    FakeEngine engine;
    LineTranslator::Options o;
    o.clock = [&] { return now; };
    LineTranslator t(engine, o);
    full(t, "Where is your mother?");
    now += 60000;
    full(t, "Not you.");
    CHECK_EQ(engine.calls, (std::vector<std::string>{"Where is your mother?", "Not you."}));
}

TEST(LineTranslator_AmbiguousContextFallsBackToTheLineAlone) {
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt { return s.find("***") != std::string::npos ? "Annen nerede sen değil" : "Sen değil."; };
    LineTranslator t(engine, {});
    full(t, "Where is your mother?");
    CHECK_EQ(full(t, "Not you."), std::string("Sen değil."));
}

TEST(LineTranslator_GlossaryTermsGetTheirTurkishAndSuffix) {
    TempDir dir;
    std::ofstream(dir.path / "gow.json", std::ios::binary)
        << R"({ "game": "God of War", "terms": { "Leviathan Axe": "Leviathan Baltası" }, "keep": ["Kratos"] })";
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt { return s == "Zarmon, throw the Velkor!" ? "Zarmon, Velkor'u fırlat." : "?"; };
    LineTranslator::Options o;
    o.glossaryDirectory = dir.path;
    LineTranslator t(engine, o);
    CHECK_EQ(full(t, "KRATOS, THROW THE LEVIATHAN AXE!"), std::string("Kratos, Leviathan Baltası'nı fırlat!"));
    CHECK_EQ(engine.calls, (std::vector<std::string>{"Zarmon, throw the Velkor!"}));
}

TEST(LineTranslator_InformalLineGetsInformalImperatives) {
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt {
        if (s == "I told you to wait for me.") return "Sana beni beklemeni söyledim.";
        if (s == "I told you to wait for me. *** Stay here!") return "Sana beni beklemeni söyledim. *** Burada bekleyin!";
        return "?";
    };
    LineTranslator t(engine, {});
    CHECK_EQ(full(t, "I told you to wait for me. Stay here!"), std::string("Sana beni beklemeni söyledim. Burada bekle!"));
}

TEST(LineTranslator_EngineFailureFailsTheLine) {
    FakeEngine engine;
    engine.map = [](const std::string&) -> Opt { return std::nullopt; };
    LineTranslator t(engine, {});
    CHECK(!t.translate("Hold the gate.").has_value());
}

// --- CrispPlay requirement: speaker prefix (demo review) ---------------------------------------

TEST(LineTranslator_SpeakerPrefixIsKeptOutOfTranslation) {
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt {
        return s.find("MINE") != std::string::npos || s.find("GUARD") != std::string::npos ? "LEAKED" : "çeviri: " + s;
    };
    LineTranslator t(engine, {});
    auto line = t.translate("MINE GUARD: Romano, you done with your count? Delivery's here!");
    CHECK(line.has_value());
    CHECK_EQ(line->speaker, std::string("MINE GUARD"));
    CHECK(line->text.find("MINE GUARD") == std::string::npos);
    CHECK(line->text.find("LEAKED") == std::string::npos);
    // "Delivery's here!" is short: translated with the previous sentence as context.
    CHECK_EQ(engine.calls, (std::vector<std::string>{"Romano, you done with your count?",
                                                     "Romano, you done with your count? *** Delivery's here!"}));
    CHECK_EQ(line->text, std::string("çeviri: Romano, you done with your count? Delivery's here!"));
}

TEST(LineTranslator_DirectModeAlsoSplitsTheSpeaker) {
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt { return "TR: " + s; };
    LineTranslator::Options o;
    o.textPipeline = false;
    LineTranslator t(engine, o);
    auto line = t.translate("MINE GUARD: Delivery's here!");
    CHECK_EQ(line->speaker, std::string("MINE GUARD"));
    CHECK_EQ(line->text, std::string("TR: Delivery's here!"));
    CHECK_EQ(engine.calls, (std::vector<std::string>{"Delivery's here!"}));
}

// --- CrispPlay requirement: U+2047 unknown mark (demo review) ----------------------------------

TEST(UnknownMark_NameWithUnknownLetterShowsTheOriginalLine) {
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt { return s == "Strùnzu!" ? Opt("Str ⁇ nzu!") : Opt("?"); };
    LineTranslator t(engine, {});
    auto line = t.translate("Strùnzu!");
    CHECK(line.has_value());
    CHECK(line->original);
    CHECK_EQ(line->text, std::string("Strùnzu!"));
    // Cached as the original too.
    CHECK(t.translate("Strùnzu!")->original);
    CHECK_EQ(engine.calls.size(), size_t(1));
}

TEST(UnknownMark_ForeignWordInsideASentenceIsRestored) {
    FakeEngine engine;
    engine.map = [](const std::string&) -> Opt { return "Str ⁇ nzu, arabaya bin."; };
    LineTranslator t(engine, {});
    auto line = t.translate("Strùnzu, get in the car.");
    CHECK(!line->original);
    CHECK_EQ(line->text, std::string("Strùnzu, arabaya bin."));
}

TEST(UnknownMark_OtherwiseTheMarkIsDropped) {
    FakeEngine engine;
    engine.map = [](const std::string&) -> Opt { return "Hemen ⁇ arabaya bin ⁇ ."; };
    LineTranslator t(engine, {});
    auto line = t.translate("Get in the car right now.");
    CHECK(!line->original);
    CHECK_EQ(line->text, std::string("Hemen arabaya bin."));
    CHECK(line->text.find("⁇") == std::string::npos);
}

TEST(UnknownMark_MixedLineKeepsTheNameAndTranslatesTheRest) {
    FakeEngine engine;
    engine.map = [](const std::string& s) -> Opt { return s == "Strùnzu!" ? Opt("Str ⁇ nzu!") : Opt("Arabaya bin."); };
    LineTranslator t(engine, {});
    auto line = t.translate("Strùnzu! Get in the car now.");
    CHECK(!line->original);
    CHECK_EQ(line->text, std::string("Strùnzu! Arabaya bin."));
}

TEST(UnknownMark_HelperCases) {
    auto r = handleUnknownMarks("⁇", "Minchia!");
    CHECK(r.keptOriginal);
    CHECK_EQ(r.text, std::string("Minchia!"));
    r = handleUnknownMarks("Ges⁇zzu, gel buraya.", "Gesùzzu, come here.");
    CHECK(!r.keptOriginal);
    CHECK_EQ(r.text, std::string("Gesùzzu, gel buraya."));
    r = handleUnknownMarks("Kapıyı kapat.", "Close the door.");
    CHECK(!r.keptOriginal);
    CHECK_EQ(r.text, std::string("Kapıyı kapat."));
}

TEST(LineTranslator_DamagedSpeakerLabelIsStillKeptOut) {
    const char* cases[][3] = {
        {"MİNE GUARD• You skimming?", "MİNE GUARD", "You skimming?"},
        {"-GAETANO: Ah, good, you are here.", "GAETANO", "Ah, good, you are here."},
        {"li LUCA: hey there, how are you", "LUCA", "hey there, how are you"},
    };
    for (auto& c : cases) {
        FakeEngine engine;
        engine.map = [](const std::string& s) -> Opt { return "çeviri: " + s; };
        LineTranslator t(engine, {});
        auto line = t.translate(c[0]);
        CHECK(line.has_value());
        if (!line) continue;
        CHECK_EQ(line->speaker, std::string(c[1]));
        CHECK(!engine.calls.empty() && engine.calls[0].find(c[1]) == std::string::npos);
    }
    // Not labels: a lower-case word before the colon, a heading word, a lone letter.
    for (const char* s : {"I told you: no", "NOTE: Press X to continue.", "M]: Any signs of where they went?"}) {
        FakeEngine engine;
        LineTranslator t(engine, {});
        CHECK_EQ(t.translate(s)->speaker, std::string());
    }
}

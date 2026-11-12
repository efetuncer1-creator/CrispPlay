// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core tests, ported from CouchSubs windows/tests: CouchSubs.Translation.Tests/
// SubtitleSegmenterTests.cs and CouchSubs.Core.Tests/ GameEnglishTests.cs, GlossaryTests.cs,
// TurkishSuffixTests.cs, FreeTranslationTests.cs, TranslationCacheTests.cs (sample lines included).
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "Test.h"

#include "crispplay/subs/FreeTranslation.h"
#include "crispplay/subs/GameEnglish.h"
#include "crispplay/subs/Glossary.h"
#include "crispplay/subs/SubtitleSegmenter.h"
#include "crispplay/subs/TermProtection.h"
#include "crispplay/subs/TranslationCache.h"
#include "crispplay/subs/TurkishPostEdit.h"
#include "crispplay/subs/TurkishSuffix.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <random>

using namespace crispplay::subs;
using Opt = std::optional<std::string>;

namespace subtest {
inline std::string show(const Opt& v) { return v ? show(*v) : "null"; }
}  // namespace subtest

namespace {

// A fresh directory under %TEMP%, removed at scope exit.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("crispplay-subs-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void writeFile(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

std::string render(const std::string& text) {
    std::string out;
    for (const auto& p : SubtitleSegmenter::split(text)) out += p.translate ? "[" + p.text + "]" : p.text;
    return out;
}

const char* kGodOfWar = R"({ "game": "God of War",
  "terms": { "Leviathan Axe": "Leviathan Baltası", "Axe": "Balta", "Midgard": "Midgard", "World Serpent": "Dünya Yılanı" },
  "keep": ["Kratos", "Atreus"] })";

Glossary gow() { return *Glossary::parse(kGodOfWar); }

}  // namespace

// --- SubtitleSegmenterTests.cs ----------------------------------------------------------------

TEST(Segmenter_SplitsSentences) {
    const char* cases[][2] = {
        {"Wait!", "[Wait!]"},
        {"Boy! Focus. The draugr are still coming.", "[Boy!] [Focus.] [The draugr are still coming.]"},
        {"What is that?! It looks like a door.", "[What is that?!] [It looks like a door.]"},
        {"\"Run!\" he said. Then nothing.", "[\"Run!\" he said.] [Then nothing.]"},
        {"I know… but we must go.", "[I know… but we must go.]"},
        {"Wait... what is that?", "[Wait... what is that?]"},
        {"Mr. Smith is here. Dr. Jones too.", "[Mr. Smith is here.] [Dr. Jones too.]"},
        {"No. We go now.", "[No.] [We go now.]"},
        {"Kratos.", "[Kratos.]"},
    };
    for (auto& c : cases) CHECK_EQ(render(c[0]), std::string(c[1]));
}

TEST(Segmenter_KeepsSpeakerLabels) {
    const char* cases[][2] = {
        {"ATREUS: Father, look over there!", "ATREUS: [Father, look over there!]"},
        {"ATREUS: Look! KRATOS: Stay behind me.", "ATREUS: [Look!] KRATOS: [Stay behind me.]"},
        {"SINDRI JR.: Hello there.", "SINDRI JR.: [Hello there.]"},
        {"Note: this is not a label.", "[Note: this is not a label.]"},
        {"Kratos: Stay close.", "Kratos: [Stay close.]"},
        {"Old Man: Who goes there?", "Old Man: [Who goes there?]"},
        {"Objective: Find the gate.", "[Objective: Find the gate.]"},
        // CrispPlay demo clip: two-word all-caps speaker.
        {"MINE GUARD: Romano, you done with your count?", "MINE GUARD: [Romano, you done with your count?]"},
    };
    for (auto& c : cases) CHECK_EQ(render(c[0]), std::string(c[1]));
}

TEST(Segmenter_JoinsWrappedLines) {
    CHECK_EQ(render("If we do not find the giant before nightfall,\nwe will have to make camp."),
             std::string("[If we do not find the giant before nightfall, we will have to make camp.]"));
}

TEST(Segmenter_KeepsLineBreaksBetweenSpeakers) {
    CHECK_EQ(render("ATREUS: Father, look!\r\nKRATOS: Stay behind me."), std::string("ATREUS: [Father, look!]\nKRATOS: [Stay behind me.]"));
    CHECK_EQ(render("- Who are you?\n- A friend."), std::string("- [Who are you?]\n- [A friend.]"));
}

TEST(Segmenter_DoesNotTranslatePiecesWithoutLetters) {
    CHECK_EQ(render("..."), std::string("..."));
    CHECK_EQ(render("?!"), std::string("?!"));
    CHECK_EQ(render(""), std::string(""));
    CHECK_EQ(render("   "), std::string(""));
}

TEST(Segmenter_NormalizesAllCapsSentences) {
    const char* cases[][2] = {
        {"THE GATE WILL NOT HOLD MUCH LONGER.", "The gate will not hold much longer."},
        {"I KNOW, BUT I CAN'T.", "I know, but I can't."},
        {"WAIT!", "WAIT!"},
        {"KRATOS.", "KRATOS."},
        {"Who are you?", "Who are you?"},
        {"NASA is here.", "NASA is here."},
    };
    for (auto& c : cases) CHECK_EQ(SubtitleSegmenter::normalizeCase(c[0]), std::string(c[1]));
}

TEST(Segmenter_KnownNamesStayCapitalized) {
    std::set<std::string> names{"atreus"};
    CHECK_EQ(SubtitleSegmenter::normalizeCase("THE GATE WILL NOT HOLD, ATREUS.", &names), std::string("The gate will not hold, Atreus."));
}

TEST(Segmenter_SpeakerLabelHelper) {
    CHECK_EQ(SubtitleSegmenter::speakerLabel("MINE GUARD: Delivery's here!"), Opt("MINE GUARD"));
    CHECK_EQ(SubtitleSegmenter::speakerLabel("Note: press X"), Opt());
    CHECK_EQ(SubtitleSegmenter::speakerLabel("Hold the gate."), Opt());
}

// --- GameEnglishTests.cs ----------------------------------------------------------------------

TEST(GameEnglish_GameIdiomsBecomePlainEnglish) {
    const char* cases[][2] = {
        {"Hold your fire! They're friendlies!", "Hold your fire! They're allies!"},
        {"I got your six.", "I've got your back."},
        {"Check your six!", "Watch behind you!"},
        {"Heads up! Incoming!", "Watch out! Incoming!"},
        {"Took you long enough.", "You're late."},
        {"Careful, we've got company.", "Careful, enemies are coming."},
        {"We're sitting ducks out here.", "We're easy targets out here."},
        {"Copy that. Moving in.", "Copy that. Going in."},
    };
    for (auto& c : cases) CHECK_EQ(GameEnglish::plain(c[0]), std::string(c[1]));
}

TEST(GameEnglish_OtherTextIsUnchanged) {
    for (const char* s : {"The friendliest town in the north.", "Six of them, behind the gate.", "Stand down!"})
        CHECK_EQ(GameEnglish::plain(s), std::string(s));
}

// --- GlossaryTests.cs -------------------------------------------------------------------------

TEST(Glossary_ParsesTermsAndKeepNames) {
    auto g = gow();
    CHECK_EQ(g.count(), size_t(6));
    CHECK_EQ(g.turkish("leviathan  axe"), Opt("Leviathan Baltası"));
    CHECK_EQ(g.turkish("KRATOS"), Opt("Kratos"));
    CHECK_EQ(g.turkish("Freya"), Opt());
}

TEST(Glossary_FindsWholeWordsCaseInsensitiveLongestFirst) {
    auto found = gow().find(U"THROW THE LEVIATHAN AXE, then the axe. Kratos's axes. Midgardian.");
    CHECK_EQ(found.size(), size_t(3));
    if (found.size() == 3) {
        CHECK(found[0].english == U"Leviathan Axe");
        CHECK(found[1].english == U"Axe");
        CHECK(found[2].english == U"Kratos");
        CHECK_EQ(found[0].start, size_t(10));
    }
}

TEST(Glossary_CapitalizedTermsAndKeepNamesAreCapitalWords) {
    auto g = gow();
    CHECK_EQ(std::vector<std::string>(g.capitalWords().begin(), g.capitalWords().end()),
             (std::vector<std::string>{"atreus", "axe", "kratos", "leviathan", "midgard", "serpent", "world"}));
    CHECK(Glossary::builtIn().capitalWords().count("health") == 0);
}

TEST(Glossary_BuiltInGlossaryHasGeneralGameTerms) {
    CHECK(Glossary::builtIn().count() >= 10);
    CHECK_EQ(Glossary::builtIn().turkish("Health Potion"), Opt("can iksiri"));
    CHECK_EQ(Glossary::builtIn().turkish("skill tree"), Opt("yetenek ağacı"));
}

TEST(Glossary_LaterGlossaryWinsAndFingerprintFollowsContent) {
    auto user = *Glossary::parse(R"({ "terms": { "skill tree": "beceri ağacı" } })");
    auto merged = Glossary::merge({&Glossary::builtIn(), &user});
    CHECK_EQ(merged.turkish("skill tree"), Opt("beceri ağacı"));
    CHECK(Glossary::builtIn().fingerprint() != merged.fingerprint());
    CHECK_EQ(merged.fingerprint(), Glossary::merge({&Glossary::builtIn(), &user}).fingerprint());
}

TEST(Glossary_IgnoresEntriesWithoutLetters) {
    CHECK_EQ(Glossary::parse(R"({ "terms": { "...": "x", "Odin": "Odin", "": "y" }, "keep": [3] })")->count(), size_t(1));
}

TEST(Glossary_AcceptsCommentsAndTrailingCommas) {
    auto g = Glossary::parse("{ // user notes\n \"keep\": [\"Enzo\",], }");
    CHECK(g.has_value());
    CHECK_EQ(g->turkish("enzo"), Opt("Enzo"));
    CHECK(!Glossary::parse("[1, 2]").has_value());
}

TEST(Glossary_LoadsUserFolderAndSkipsBrokenFiles) {
    TempDir dir;
    writeFile(dir.path / "gow.json", kGodOfWar);
    writeFile(dir.path / "broken.json", "{ not json");
    writeFile(dir.path / "notes.txt", R"({ "keep": ["Ignored"] })");
    auto g = Glossary::load(dir.path);
    CHECK_EQ(g.turkish("Leviathan Axe"), Opt("Leviathan Baltası"));
    CHECK_EQ(g.turkish("health potion"), Opt("can iksiri"));
    CHECK_EQ(g.turkish("Ignored"), Opt());
    CHECK_EQ(Glossary::load(dir.path / "missing").count(), Glossary::builtIn().count());
}

TEST(Glossary_StoreReloadsWhenAFileChanges) {
    TempDir dir;
    int64_t now = 0;
    GlossaryStore store(dir.path, [&] { return now; });
    CHECK_EQ(store.current()->turkish("Kratos"), Opt());
    writeFile(dir.path / "gow.json", kGodOfWar);
    CHECK_EQ(store.current()->turkish("Kratos"), Opt());
    now += 6000;
    CHECK_EQ(store.current()->turkish("Kratos"), Opt("Kratos"));
}

// --- TermProtectionTests (GlossaryTests.cs) ---------------------------------------------------

namespace {
Glossary protectionGlossary() {
    return *Glossary::parse(R"({ "terms": { "Leviathan Axe": "Leviathan Baltası", "Midgard": "Midgard", "health potion": "can iksiri" },
      "keep": ["Kratos", "Atreus"] })");
}
}  // namespace

TEST(TermProtection_ReplacesTermsWithPlaceholderNames) {
    auto g = protectionGlossary();
    TermProtector protector(g, {""});
    auto p = protector.protect("Kratos, take the Leviathan Axe to Midgard. Kratos!");
    CHECK_EQ(p.text, std::string("Zarmon, take the Velkor to Dorvan. Zarmon!"));
    std::vector<std::string> english;
    for (auto& s : p.slots) english.push_back(s.english);
    CHECK_EQ(english, (std::vector<std::string>{"Kratos", "Leviathan Axe", "Midgard"}));
}

TEST(TermProtection_RestoresTermsWithSuffixesDespiteCaseAndSpacing) {
    const char* cases[][2] = {
        {"Zarmon, Velkor'u Dorvan'a götür. Zarmon!", "Kratos, Leviathan Baltası'nı Midgard'a götür. Kratos!"},
        {"ZARMON, VELKOR'U DORVAN'A GÖTÜR. ZARMON!", "Kratos, Leviathan Baltası'nı Midgard'a GÖTÜR. Kratos!"},
        {"Zarmon, Velkor ’u Dorvana götür. Zarmon!", "Kratos, Leviathan Baltası'nı Midgard'a götür. Kratos!"},
    };
    auto g = protectionGlossary();
    for (auto& c : cases) {
        TermProtector protector(g, {""});
        auto p = protector.protect("Kratos, take the Leviathan Axe to Midgard. Kratos!");
        CHECK_EQ(TermProtector::restore(c[0], p.slots, protector.allSlots()), Opt(c[1]));
    }
}

TEST(TermProtection_MissingPlaceholderMeansUnreliable) {
    auto g = protectionGlossary();
    TermProtector protector(g, {""});
    auto p = protector.protect("Take the Leviathan Axe to Midgard.");
    CHECK_EQ(TermProtector::restore("Baltayı Dorvan'a götür.", p.slots, protector.allSlots()), Opt());
}

TEST(TermProtection_PlaceholderFromAnotherPartMeansUnreliable) {
    auto g = protectionGlossary();
    TermProtector protector(g, {""});
    protector.protect("Kratos!");
    auto p = protector.protect("Go to Midgard.");
    CHECK_EQ(TermProtector::restore("Zarmon Velkor'a git.", p.slots, protector.allSlots()), Opt());
    CHECK_EQ(TermProtector::restore("Velkor'a git.", p.slots, protector.allSlots()), Opt("Midgard'a git."));
}

TEST(TermProtection_CommonNounAtSentenceStartIsCapitalized) {
    auto g = protectionGlossary();
    TermProtector protector(g, {""});
    auto p = protector.protect("Drink the health potion. Health potion first!");
    CHECK_EQ(TermProtector::restore("Zarmon'u iç. Önce Zarmon!", p.slots), Opt("Can iksirini iç. Önce can iksiri!"));
}

TEST(TermProtection_QuotedPlaceholderIsNotASuffix) {
    auto g = protectionGlossary();
    TermProtector protector(g, {""});
    auto p = protector.protect("He said 'Midgard' again.");
    CHECK_EQ(TermProtector::restore("Yine 'Zarmon' dedi.", p.slots), Opt("Yine 'Midgard' dedi."));
}

TEST(TermProtection_PlaceholderAlreadyInTheSourceIsSkipped) {
    auto g = protectionGlossary();
    TermProtector protector(g, {"Zarmon met Kratos."});
    CHECK_EQ(protector.protect("Zarmon met Kratos.").text, std::string("Zarmon met Velkor."));
}

TEST(TermProtection_TextWithoutTermsIsUnchanged) {
    auto g = protectionGlossary();
    TermProtector protector(g, {""});
    auto p = protector.protect("We go now.");
    CHECK_EQ(p.text, std::string("We go now."));
    CHECK(p.slots.empty());
    CHECK_EQ(TermProtector::restore("Şimdi gidiyoruz.", p.slots, protector.allSlots()), Opt("Şimdi gidiyoruz."));
}

// --- TurkishSuffixTests.cs --------------------------------------------------------------------

TEST(TurkishSuffix_ProperNounsTakeHarmonizedSuffixWithApostrophe) {
    const char* cases[][3] = {
        {"Kratos", "u", "Kratos'u"}, {"Kratos", "un", "Kratos'un"}, {"Kratos", "a", "Kratos'a"}, {"Kratos", "da", "Kratos'ta"},
        {"Kratos", "dan", "Kratos'tan"}, {"Kratos", "la", "Kratos'la"}, {"Kratos", "dur", "Kratos'tur"},
        {"Atreus", "un", "Atreus'un"}, {"Atreus", "a", "Atreus'a"}, {"Midgard", "a", "Midgard'a"}, {"Midgard", "da", "Midgard'da"},
        {"Midgard", "u", "Midgard'ı"}, {"Midgard", "un", "Midgard'ın"}, {"Midgard", "daki", "Midgard'daki"},
        {"Freya", "u", "Freya'yı"}, {"Freya", "a", "Freya'ya"}, {"Freya", "un", "Freya'nın"}, {"Freya", "la", "Freya'yla"},
        {"Freya", "da", "Freya'da"}, {"Mimir", "a", "Mimir'e"}, {"Mimir", "u", "Mimir'i"}, {"Mimir", "un", "Mimir'in"},
        {"Mimir", "dan", "Mimir'den"}, {"Brok", "da", "Brok'ta"}, {"Brok", "u", "Brok'u"}, {"Jötunheim", "a", "Jötunheim'e"},
        {"Jötunheim", "da", "Jötunheim'de"}, {"Thor", "un", "Thor'un"}, {"Baldur", "u", "Baldur'u"},
        {"Sindri", "un", "Sindri'nin"}, {"Sindri", "u", "Sindri'yi"}, {"Odin", "a", "Odin'e"}, {"Odin", "ya", "Odin'e"},
        {"Freya", "nın", "Freya'nın"}, {"Kratos", "nın", "Kratos'un"}, {"Kratos", "", "Kratos"},
    };
    for (auto& c : cases) CHECK_EQ(TurkishSuffix::attach(c[0], c[1]), std::string(c[2]));
}

TEST(TurkishSuffix_CompoundNamesTakeTheBufferN) {
    const char* cases[][3] = {
        {"Leviathan Baltası", "u", "Leviathan Baltası'nı"}, {"Leviathan Baltası", "a", "Leviathan Baltası'na"},
        {"Leviathan Baltası", "da", "Leviathan Baltası'nda"}, {"Leviathan Baltası", "dan", "Leviathan Baltası'ndan"},
        {"Leviathan Baltası", "un", "Leviathan Baltası'nın"}, {"Leviathan Baltası", "la", "Leviathan Baltası'yla"},
        {"Dünya Ağacı", "a", "Dünya Ağacı'na"}, {"Kaos Bıçakları", "u", "Kaos Bıçakları'nı"},
    };
    for (auto& c : cases) CHECK_EQ(TurkishSuffix::attach(c[0], c[1]), std::string(c[2]));
}

TEST(TurkishSuffix_CommonNounsAreWrittenTogether) {
    const char* cases[][3] = {
        {"can iksiri", "u", "can iksirini"}, {"kontrol noktası", "a", "kontrol noktasına"},
        {"bekleme süresi", "da", "bekleme süresinde"}, {"envanter", "a", "envantere"}, {"envanter", "da", "envanterde"},
        {"kılıç", "u", "kılıcı"}, {"kitap", "u", "kitabı"}, {"bıçak", "u", "bıçağı"}, {"renk", "u", "rengi"},
        {"top", "u", "topu"}, {"balta", "u", "baltayı"}, {"balta", "lar", "baltalar"}, {"kalkan", "lar", "kalkanlar"},
        {"kalkan", "dan", "kalkandan"}, {"kalkan", "la", "kalkanla"},
    };
    for (auto& c : cases) CHECK_EQ(TurkishSuffix::attach(c[0], c[1]), std::string(c[2]));
}

TEST(TurkishSuffix_Misc) {
    CHECK_EQ(TurkishSuffix::attach("Kratos", "un", U'’'), std::string("Kratos’un"));
    CHECK_EQ(TurkishSuffix::attach("XP", "u"), std::string("XP'yi"));
    CHECK_EQ(TurkishSuffix::attach("Midgard", "A"), std::string("Midgard'a"));
    // Turkish lower casing of the suffix: "I" is dotless ı ("NIN" -> "nın").
    CHECK_EQ(TurkishSuffix::attach("Freya", "NIN"), std::string("Freya'nın"));
}

// --- TurkishPostEditTests (FreeTranslationTests.cs) -------------------------------------------

TEST(TurkishPostEdit_SourceEndingIsKept) {
    const char* cases[][3] = {
        {"Run!", "Koş.", "Koş!"}, {"Run!", "Koş", "Koş!"}, {"What?!", "Ne?", "Ne?!"}, {"Wait…", "Bekle.", "Bekle…"},
        {"Wait...", "Bekle.", "Bekle..."}, {"Where is he?", "O nerede.", "O nerede?"},
        {"He said \"Run!\"", "\"Koş.\" dedi.", "\"Koş.\" dedi."}, {"He said \"Run!\"", "\"Koş\" dedi", "\"Koş\" dedi"},
        {"\"Run!\"", "\"Koş.\"", "\"Koş!\""}, {"We go.", "Gidiyoruz!", "Gidiyoruz!"}, {"We go.", "Gidiyoruz", "Gidiyoruz"},
        {"Boy!", "Evlat!", "Evlat!"},
    };
    for (auto& c : cases) CHECK_EQ(TurkishPostEdit::apply(c[0], c[1]), std::string(c[2]));
}

TEST(TurkishPostEdit_SpaceBeforePunctuationIsRemoved) {
    CHECK_EQ(TurkishPostEdit::apply("Okay. Go.", "Dur , bekle !"), std::string("Dur, bekle!"));
    CHECK_EQ(TurkishPostEdit::apply("Okay. Go.", "Ne  oldu ?"), std::string("Ne oldu?"));
    CHECK_EQ(TurkishPostEdit::apply("Okay. Go.", "Peki ... gidelim ."), std::string("Peki... gidelim."));
}

TEST(TurkishPostEdit_QuotesFollowTheSource) {
    CHECK_EQ(TurkishPostEdit::apply("He said \"run\", Kratos's son.", "“Koş” dedi, Kratos’un oğlu."),
             std::string("\"Koş\" dedi, Kratos'un oğlu."));
    CHECK_EQ(TurkishPostEdit::apply("He said “run”, Kratos’s son.", "\"Koş\" dedi, Kratos'un oğlu."),
             std::string("“Koş” dedi, Kratos’un oğlu."));
}

TEST(TurkishPostEdit_ImperativesFollowAnInformalLine) {
    CHECK_EQ(TurkishPostEdit::registerForm({"Sana söyledim.", "Bekleyin!", "Dikkat edin, evlat."}),
             (std::vector<Opt>{"Sana söyledim.", "Bekle!", "Dikkat et, evlat."}));
    CHECK_EQ(TurkishPostEdit::registerForm({"Seni gördüm. Hemen gelin!"}), (std::vector<Opt>{"Seni gördüm. Hemen gel!"}));
}

TEST(TurkishPostEdit_OtherLinesKeepTheirRegister) {
    for (const char* s : {"Bekleyin!", "Siz bekleyin, sana sonra bakarım!", "Sen gelin misin?"})
        CHECK_EQ(TurkishPostEdit::registerForm({s}), (std::vector<Opt>{s}));
}

TEST(TurkishPostEdit_NullPiecesStayNull) {
    CHECK_EQ(TurkishPostEdit::registerForm({std::nullopt, "Sana söyledim.", std::nullopt, "Durun!"}),
             (std::vector<Opt>{std::nullopt, "Sana söyledim.", std::nullopt, "Dur!"}));
}

// --- FreeTranslationTests.cs ------------------------------------------------------------------

namespace {
Glossary freeGlossary() {
    return *Glossary::parse(R"({ "terms": { "Leviathan Axe": "Leviathan Baltası" }, "keep": ["Kratos", "Atreus"] })");
}
std::string same(const std::string& s) { return s; }
}  // namespace

TEST(FreeTranslation_PipelineVersionIsBumped) { CHECK(FreeTranslation::PipelineVersion >= 2); }

TEST(FreeTranslation_ShortLinesHaveFewerThanFourWords) {
    CHECK(FreeTranslation::isShort("Boy!"));
    CHECK(FreeTranslation::isShort("Do it."));
    CHECK(FreeTranslation::isShort("Not you, boy."));
    CHECK(!FreeTranslation::isShort("We go to the mountain."));
    CHECK(!FreeTranslation::isShort("..."));
}

TEST(FreeTranslation_PartAfterTheSeparatorOnlyWhenUnambiguous) {
    const char* cases[][2] = {
        {"Annen nerede? *** Sen değil.", "Sen değil."},
        {"Annen nerede?***Sen değil.", "Sen değil."},
        {"ANNEN NEREDE? *** SEN DEĞİL.", "SEN DEĞİL."},
        {"Annen nerede? Sen değil.", nullptr},
        {"Annen *** nerede? *** Sen değil.", nullptr},
        {"Annen nerede? **** Sen değil.", nullptr},
        {"*** Sen değil.", nullptr},
        {"Annen nerede? ***", nullptr},
        {"Annen nerede? *** Sen değil, sen değil, sen değil, sen değil, sen değil, sen değil, sen değil.", nullptr},
    };
    for (auto& c : cases) CHECK_EQ(FreeTranslation::afterSeparator(c[0], "Not you."), c[1] ? Opt(c[1]) : Opt());
}

TEST(FreeTranslation_CacheKeyCarriesContextAndGlossary) {
    auto g = freeGlossary();
    CHECK_EQ(FreeTranslation::cacheKey("We go.", std::nullopt, g), std::string("We go."));
    CHECK_EQ(FreeTranslation::cacheKey("We go.", "Where?", g), std::string("Where?\x1FWe go."));
    CHECK_EQ(FreeTranslation::cacheKey("Kratos, go.", std::nullopt, g), g.fingerprint() + "\x1FKratos, go.");
    CHECK(FreeTranslation::cacheKey("Go.", "Kratos!", g).rfind(g.fingerprint(), 0) == 0);
}

TEST(FreeTranslation_TermsAreProtectedAndRestored) {
    auto g = freeGlossary();
    std::vector<std::string> calls;
    auto result = FreeTranslation::translate("Kratos, throw the Leviathan Axe!", std::nullopt, g, same, [&](const std::string& in) {
        calls.push_back(in);
        return Opt("Zarmon, Velkor'u fırlat.");
    });
    CHECK_EQ(calls, (std::vector<std::string>{"Zarmon, throw the Velkor!"}));
    CHECK_EQ(result, Opt("Kratos, Leviathan Baltası'nı fırlat!"));
}

TEST(FreeTranslation_LostPlaceholderFallsBackToPlainTranslation) {
    auto g = freeGlossary();
    std::vector<std::string> calls;
    auto result = FreeTranslation::translate("Throw the Leviathan Axe!", std::nullopt, g, same, [&](const std::string& in) {
        calls.push_back(in);
        return Opt(in.find("Zarmon") != std::string::npos ? "Baltayı fırlat!" : "Leviathan Baltasını fırlat!");
    });
    CHECK_EQ(calls, (std::vector<std::string>{"Throw the Zarmon!", "Throw the Leviathan Axe!"}));
    CHECK_EQ(result, Opt("Leviathan Baltasını fırlat!"));
}

TEST(FreeTranslation_ShortLineIsTranslatedWithItsContext) {
    auto g = freeGlossary();
    std::vector<std::string> calls;
    auto result = FreeTranslation::translate("Not you.", "Where is your mother?", g, same, [&](const std::string& in) {
        calls.push_back(in);
        return Opt("Annen nerede? *** Sen değil.");
    });
    CHECK_EQ(calls, (std::vector<std::string>{"Where is your mother? *** Not you."}));
    CHECK_EQ(result, Opt("Sen değil."));
}

TEST(FreeTranslation_AmbiguousContextFallsBackToTheLineAlone) {
    auto g = freeGlossary();
    std::vector<std::string> calls;
    auto result = FreeTranslation::translate("Not you.", "Where is your mother?", g, same, [&](const std::string& in) {
        calls.push_back(in);
        return Opt(in.find("***") != std::string::npos ? "Annen nerede, sen değil." : "Sen değil.");
    });
    CHECK_EQ(calls, (std::vector<std::string>{"Where is your mother? *** Not you.", "Not you."}));
    CHECK_EQ(result, Opt("Sen değil."));
}

TEST(FreeTranslation_ContextTermsDoNotLeakIntoTheLine) {
    auto g = freeGlossary();
    auto result = FreeTranslation::translate("Go, Atreus!", "Kratos is here.", g, same, [&](const std::string& in) {
        return Opt(in.find("***") != std::string::npos ? "Burada. *** Git, Zarmon Velkor!" : "Git, Zarmon!");
    });
    CHECK_EQ(result, Opt("Git, Atreus!"));
}

TEST(FreeTranslation_ContextualLineKeepsTermsOfItsOwn) {
    auto g = freeGlossary();
    auto result = FreeTranslation::translate("Go, Atreus!", "Kratos is here.", g, same, [&](const std::string& in) {
        CHECK_EQ(in, std::string("Zarmon is here. *** Go, Velkor!"));
        return Opt("Zarmon burada. *** Git, Velkor!");
    });
    CHECK_EQ(result, Opt("Git, Atreus!"));
}

TEST(FreeTranslation_NormalizeAndGameEnglishRunBeforeProtection) {
    auto g = freeGlossary();
    std::string sent;
    FreeTranslation::translate(
        "HEADS UP, KRATOS!", std::nullopt, g,
        [](const std::string& t) { return t == "HEADS UP, KRATOS!" ? std::string("Heads up, kratos!") : t; },
        [&](const std::string& in) {
            sent = in;
            return Opt("Dikkat et, Zarmon!");
        });
    CHECK_EQ(sent, std::string("Watch out, Zarmon!"));
}

TEST(FreeTranslation_EngineFailureIsReported) {
    auto g = freeGlossary();
    CHECK_EQ(FreeTranslation::translate("Run!", std::nullopt, g, same, [](const std::string&) { return Opt(); }), Opt());
}

TEST(RecentLines_PreviousLineIsTheContextAndRereadsKeepIt) {
    int64_t now = 0;
    RecentLines recent([&] { return now; });
    CHECK_EQ(recent.shown("Where is your mother?"), Opt());
    CHECK_EQ(recent.shown("Not you."), Opt("Where is your mother?"));
    now += 5 * 60 * 1000;
    CHECK_EQ(recent.shown("Not you."), Opt("Where is your mother?"));
    CHECK_EQ(recent.shown("Go."), Opt("Not you."));
}

TEST(RecentLines_OldLineIsNotContext) {
    int64_t now = 0;
    RecentLines recent([&] { return now; });
    recent.shown("Where is your mother?");
    now += 25000;
    CHECK_EQ(recent.shown("Not you."), Opt());
}

// --- TranslationCacheTests.cs -----------------------------------------------------------------

namespace {
std::filesystem::path cacheFile(const TempDir& dir, const std::string& engine = "deepl") {
    return TranslationCache::fileFor(dir.path, engine, "en", "tr");
}
size_t lineCount(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    size_t n = 0;
    while (std::getline(in, line)) ++n;
    return n;
}
}  // namespace

TEST(TranslationCache_TranslationsSurviveRestart) {
    TempDir dir;
    TranslationCache first(cacheFile(dir));
    first.store("Close the door.", "Kapıyı kapat.");
    first.store("ATREUS: Father!", "ATREUS: Baba!");
    TranslationCache second(cacheFile(dir));
    CHECK_EQ(second.get("Close the door."), Opt("Kapıyı kapat."));
    CHECK_EQ(second.get("ATREUS: Father!"), Opt("ATREUS: Baba!"));
    CHECK_EQ(second.get("Run!"), Opt());
}

TEST(TranslationCache_EnginesHaveSeparateFiles) {
    TempDir dir;
    TranslationCache(cacheFile(dir, "deepl")).store("Run!", "DeepL çevirisi");
    CHECK_EQ(TranslationCache(cacheFile(dir, "opus-mt")).get("Run!"), Opt());
    CHECK(cacheFile(dir, "opus-mt").filename() == std::filesystem::path("v1-opus_mt-en-tr.jsonl"));
}

TEST(TranslationCache_LinesWithQuotesAndNewlinesRoundTrip) {
    TempDir dir;
    const std::string source = "KRATOS: \"Boy.\"\nATREUS: Yes?";
    TranslationCache(cacheFile(dir)).store(source, "KRATOS: \"Evlat.\"\nATREUS: Evet?");
    CHECK_EQ(TranslationCache(cacheFile(dir)).get(source), Opt("KRATOS: \"Evlat.\"\nATREUS: Evet?"));
}

TEST(TranslationCache_LeastRecentlyUsedEntriesAreDropped) {
    TempDir dir;
    TranslationCache cache(cacheFile(dir), 10);
    for (int i = 0; i < 10; ++i) cache.store("s" + std::to_string(i), "t" + std::to_string(i));
    CHECK_EQ(cache.get("s0"), Opt("t0"));
    cache.store("s10", "t10");
    CHECK(cache.count() <= 10);
    CHECK_EQ(cache.get("s0"), Opt("t0"));
    CHECK_EQ(cache.get("s1"), Opt());
    CHECK_EQ(cache.get("s10"), Opt("t10"));
}

TEST(TranslationCache_FileIsCompactedWhenItGrows) {
    TempDir dir;
    TranslationCache cache(cacheFile(dir), 5);
    for (int round = 0; round < 6; ++round)
        for (int i = 0; i < 5; ++i) cache.store("s" + std::to_string(i), "t" + std::to_string(i) + "-" + std::to_string(round));
    CHECK(lineCount(cacheFile(dir)) <= 10);
    CHECK_EQ(TranslationCache(cacheFile(dir), 5).get("s3"), Opt("t3-5"));
}

TEST(TranslationCache_BrokenLinesAreSkipped) {
    TempDir dir;
    writeFile(cacheFile(dir), "[\"Run!\",\"Koş!\"]\nnot json\n[\"only one\"]\n");
    TranslationCache cache(cacheFile(dir));
    CHECK_EQ(cache.get("Run!"), Opt("Koş!"));
    CHECK_EQ(cache.count(), size_t(1));
}

TEST(TranslationCache_ReadsEscapedAndUnescapedLines) {
    TempDir dir;
    // .NET writes "Koş!"; the Mac app and this port write UTF-8 as is.
    writeFile(cacheFile(dir), "[\"Who are you?\",\"Sen de kimsin?\"]\r\n[\"Run!\",\"Ko\\u015F!\"]\n");
    TranslationCache cache(cacheFile(dir));
    CHECK_EQ(cache.get("Run!"), Opt("Koş!"));
    CHECK_EQ(cache.get("Who are you?"), Opt("Sen de kimsin?"));
}

TEST(TranslationCache_EmptyValuesAreNotStored) {
    TempDir dir;
    TranslationCache cache(cacheFile(dir));
    cache.store("Run!", "");
    CHECK_EQ(cache.get("Run!"), Opt());
    CHECK(!std::filesystem::exists(cacheFile(dir)));
}

TEST(TranslationCache_MemoryOnlyCacheWorksWithoutFile) {
    TranslationCache cache;
    cache.store("Run!", "Koş!");
    CHECK_EQ(cache.get("Run!"), Opt("Koş!"));
}

TEST(TranslationCache_SharedReturnsOneCachePerFile) {
    TempDir dir;
    auto a = TranslationCache::shared(dir.path, "opus-mt", "en", "tr");
    CHECK(a == TranslationCache::shared(dir.path, "opus-mt", "en", "tr"));
    CHECK(a != TranslationCache::shared(dir.path, "deepl", "en", "tr"));
}

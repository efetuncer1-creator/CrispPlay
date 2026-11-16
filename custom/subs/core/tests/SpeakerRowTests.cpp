// CrispPlay subtitle core tests: speaker name row above the dialogue (SpeakerRow), broken readings
// (OcrNoise::looksBroken, stabilizer) and leftover English in translations (hasLeftoverEnglish).
// The layouts are synthetic; the lines are written for the tests. Part of CrispPlay, GPL-3.0 (see LICENSE).
#include "Test.h"

#include "crispplay/subs/LineTranslator.h"
#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/SpeakerRow.h"
#include "crispplay/subs/SubtitlePipeline.h"
#include "crispplay/subs/SubtitleStabilizer.h"

#include <mutex>
#include <set>

using namespace crispplay::subs;
using Kind = SubtitleUpdate::Kind;

namespace {

constexpr int kBandW = 1380, kBandH = 378;

// A row of `width` px centred in the band (the band itself is centred on the screen).
OcrLine row(const std::string& text, float y, float width, float height) {
    return {text, Rect{(kBandW - width) / 2, y, width, height}, -1.f};
}

OcrFrame frame(std::vector<OcrLine> lines, int64_t t = 0) {
    OcrFrame f;
    f.timeMs = t;
    f.width = kBandW;
    f.height = kBandH;
    f.bandLeft = 0.14f, f.bandTop = 0.62f, f.bandRight = 0.86f, f.bandBottom = 0.97f;
    f.lines = std::move(lines);
    return f;
}

TextBox area() { return {0.14, 0.62, 0.86, 0.97}; }

Reading block(const OcrFrame& f) { return Reading::fromFrame(f).withoutButtonPrompts().withoutOcrNoise().subtitleBlock(area()); }

// "Brooding Old Archer" (smaller, centred) over one wide dialogue row.
OcrFrame archer(const std::string& dialogue = "I'll mend this bow for you. Take it, and kill those who deserve it.") {
    return frame({row("Brooding Old Archer", 236, 300, 26), row(dialogue, 272, 1180, 34)});
}

// Maps whole sentences; anything else becomes a fixed Turkish sentence.
class MapEngine : public Translator {
public:
    std::vector<std::pair<std::string, std::string>> map;
    std::vector<std::string> calls;
    std::mutex mutex;
    std::optional<std::string> translate(const std::string& s) override {
        std::lock_guard lock(mutex);
        calls.push_back(s);
        for (const auto& [from, to] : map)
            if (s == from) return to;
        return "Bunu senin için yapacağım.";
    }
};

struct Sink {
    std::mutex mutex;
    std::vector<SubtitleUpdate> updates;
    SubtitlePipeline::Output output() {
        return [this](const SubtitleUpdate& u) { std::lock_guard lock(mutex); updates.push_back(u); };
    }
    std::vector<SubtitleUpdate> shows() {
        std::lock_guard lock(mutex);
        std::vector<SubtitleUpdate> out;
        for (auto& u : updates)
            if (u.kind == Kind::Show) out.push_back(u);
        return out;
    }
};

PipelineOptions clocked(int64_t* now) {
    PipelineOptions o;
    o.clock = [now] { return *now; };
    return o;
}

}  // namespace

// --- name text -----------------------------------------------------------------------------------

TEST(SpeakerRow_NameText) {
    for (const char* name : {"Brooding Old Archer", "Soul", "Lin Mei", "The Bone Carver", "MINE GUARD", "Sword of the North",
                             "Dr. Petra Vale", "O'Brien", "Thread-Binding Elder"})
        if (!SpeakerRow::looksLikeName(name)) subtest::fail(__FILE__, __LINE__, std::string("not a name: ") + name);
    for (const char* text : {"Run!", "Hey, Romano!", "I'll mend this bow", "Take it and go", "come here", "Wait for me at",
                             "One Two Three Four Five", "Agent 47", "Really...?", "Old Man Tao:"})
        if (SpeakerRow::looksLikeName(text)) subtest::fail(__FILE__, __LINE__, std::string("taken for a name: ") + text);
}

// --- geometry ------------------------------------------------------------------------------------

TEST(SpeakerRow_ArcherLayoutSplitsTheNameOff) {
    KnownSpeakers known;
    const auto split = SpeakerRow::split(block(archer()), known);
    CHECK(split.has_value());
    if (!split) return;
    CHECK_EQ(split->speaker, std::string("Brooding Old Archer"));
    CHECK_EQ(split->body.text(), std::string("I'll mend this bow for you. Take it, and kill those who deserve it."));
    CHECK(split->certain);
}

TEST(SpeakerRow_NameOverTwoDialogueRowsAndOverALowerCaseContinuation) {
    KnownSpeakers known;
    auto two = SpeakerRow::split(block(frame({row("Lin Mei", 200, 170, 28), row("There was a storm in the valley last night. The abbot is", 236, 1000, 33),
                                              row("probably looking into it.", 272, 420, 33)})),
                                 known);
    CHECK(two.has_value());
    if (two) CHECK_EQ(two->body.text(), std::string("There was a storm in the valley last night. The abbot is\nprobably looking into it."));
    // A line that continues the previous subtitle starts in lower case; the name is still far narrower.
    auto lower = SpeakerRow::split(block(frame({row("The Bone Carver", 236, 260, 28), row("but take away every last memory.", 272, 560, 32)})), known);
    CHECK(lower.has_value());
}

TEST(SpeakerRow_WrappedDialogueIsNotSplit) {
    KnownSpeakers known;
    // First row ends without punctuation and continues on the (shorter) second row.
    CHECK(!SpeakerRow::split(block(frame({row("We sweep these same old floors every day. Apart from talking with", 236, 1060, 34),
                                         row("the old cook, what's the point?", 272, 560, 34)})),
                             known));
    // A short Title Case first row continued in lower case, not much narrower: one sentence.
    CHECK(!SpeakerRow::split(block(frame({row("Hold On Now", 236, 380, 34), row("there is more I have to tell you.", 272, 600, 34)})), known));
    // Short exclamations are dialogue.
    CHECK(!SpeakerRow::split(block(frame({row("Run!", 236, 90, 34), row("They're coming for all of us, go now!", 272, 640, 34)})), known));
    CHECK(!SpeakerRow::split(block(frame({row("Hey, Romano!", 236, 220, 34), row("You done with your count already?", 272, 600, 34)})), known));
    // A name-like row wider than its dialogue is no evidence without a known speaker.
    CHECK(!SpeakerRow::split(block(frame({row("Old Man Tao", 236, 190, 26), row("And you?", 272, 180, 34)})), known));
    // Not centred over the dialogue: a hint at the side.
    CHECK(!SpeakerRow::split(block(frame({OcrLine{"Crouch", Rect{1000, 236, 120, 30}, -1.f}, row("Stay low and follow me.", 272, 420, 34)})), known));
}

TEST(SpeakerRow_KnownSpeakerOverShortDialogueAndMisreadName) {
    KnownSpeakers known;
    known.add("Old Man Tao");
    auto shortLine = SpeakerRow::split(block(frame({row("Old Man Tao", 236, 190, 26), row("And you?", 272, 180, 34)})), known);
    CHECK(shortLine.has_value());
    if (shortLine) CHECK_EQ(shortLine->body.text(), std::string("And you?"));
    auto misread = SpeakerRow::split(block(frame({row("Olk MaE Tao", 236, 190, 26), row("You still owe me for the boat!", 272, 520, 34)})), known);
    CHECK(misread.has_value());
    if (misread) CHECK_EQ(misread->speaker, std::string("Old Man Tao"));
    CHECK(SpeakerRow::isLoneSpeaker(block(frame({row("Old Man Tao", 236, 190, 26)})), known));
    CHECK(!SpeakerRow::isLoneSpeaker(block(frame({row("Old Man Tao!", 236, 190, 26)})), known));
}

// --- pipeline ------------------------------------------------------------------------------------

TEST(SpeakerRow_PipelineShowsTheArcherAsSpeakerAndNeverTranslatesTheName) {
    MapEngine engine;
    Sink sink;
    int64_t now = 1000;
    SubtitlePipeline p(engine, sink.output(), clocked(&now));
    p.process(archer());
    p.waitForTranslations();
    const auto shows = sink.shows();
    CHECK_EQ(shows.size(), size_t(1));
    if (shows.empty()) return;
    CHECK_EQ(shows[0].line.speaker, std::string("Brooding Old Archer"));
    CHECK_EQ(shows[0].source, std::string("I'll mend this bow for you. Take it, and kill those who deserve it."));
    for (const auto& call : engine.calls) CHECK(call.find("Archer") == std::string::npos);
    // The cover hides the name row too.
    CHECK(shows[0].line.cover.y <= 236.f);
}

TEST(SpeakerRow_NameReadAfterTheDialogueIsAddedToTheShownLine) {
    MapEngine engine;
    Sink sink;
    int64_t now = 1000;
    SubtitlePipeline p(engine, sink.output(), clocked(&now));
    p.process(frame({row("I'll mend this bow for you. Take it, and kill those who deserve it.", 272, 1180, 34)}));
    p.waitForTranslations();
    now += 250;
    p.process(archer());
    p.waitForTranslations();
    const auto shows = sink.shows();
    CHECK_EQ(shows.size(), size_t(2));
    if (shows.size() < 2) return;
    CHECK(shows[0].line.speaker.empty());
    CHECK_EQ(shows[1].line.speaker, std::string("Brooding Old Archer"));
    CHECK(shows[1].replaces);
    CHECK_EQ(shows[1].line.text, shows[0].line.text);
    CHECK_EQ(engine.calls.size(), size_t(2));  // two sentences, translated once
}

// --- broken readings -----------------------------------------------------------------------------

TEST(OcrNoise_BrokenWords) {
    CHECK_EQ(OcrNoise::brokenWords("I'll mend this bow for you. Take it, nd kill shose wh deserve it."), 2);
    CHECK(OcrNoise::looksBroken("Take it, nd kill shose wh deserve it."));
    CHECK(!OcrNoise::looksBroken("I'll mend this bow for you. Take it, and kill those who deserve it."));
    CHECK(!OcrNoise::looksBroken("Huh? Hira's still not back? We go now, it's the 2 nd time."));
    CHECK(!OcrNoise::looksBroken("I ll grind your ambition to dust. Shh, quiet now."));
    CHECK(!OcrNoise::looksBroken("Strùnzu... Lin Mei, Qiantang, Mr. Ng."));
    CHECK(OcrNoise::looksBroken("When wil einaly becme trong nou hp h d e Gld?"));
    CHECK(OcrNoise::isBrokenWord("nd"));
    CHECK(OcrNoise::isBrokenWord("rnnk"));
    CHECK(!OcrNoise::isBrokenWord("strength"));
    CHECK(!OcrNoise::isBrokenWord("Ng"));
    CHECK(OcrNoise::quality("Take it, and kill those who deserve it.") > OcrNoise::quality("Take it, nd kill shose wh deserve it."));
}

TEST(OcrNoise_DictionaryFindsPartlyReadWords) {
    // A tiny word list in place of Windows' spell checker.
    const std::set<std::string> known = {"when", "will", "finally", "become", "strong", "enough", "help", "and", "the",
                                         "look", "at", "this", "mess", "take", "kill", "those", "who", "deserve"};
    OcrNoise::setDictionary([known](const std::string& w) { return known.count(w) > 0; });
    CHECK(OcrNoise::looksBroken("When wil wenally ecome strong enoug to hel hira and the Guild?"));
    CHECK(!OcrNoise::looksBroken("When will we finally become strong enough to help Hira and the Guild?"));
    CHECK(!OcrNoise::looksBroken("Minchia, look at this mess."));  // one foreign word is no broken line
    CHECK(OcrNoise::quality("Take it, and kill those who deserve it.") > OcrNoise::quality("Take it, and kilthosewo deserve it."));
    CHECK(hasLeftoverEnglish("Ill mend this bowforyou.Take it,and kilthosewo deserve it.", "Al ve kilthesewo bunu hak ediyor."));
    CHECK(!hasLeftoverEnglish("Minchia, look at this mess.", "Minchia, şu karmaşaya bak."));
    OcrNoise::setDictionary({});
    CHECK_EQ(OcrNoise::unknownWords("wenally ecome enoug"), 0);
}

TEST(OcrNoise_LostSpaceIsBroken) {
    CHECK(OcrNoise::looksBroken("I'll mend this bow for you.Take it, and kill those who deserve it."));
    CHECK(OcrNoise::looksBroken("Take it,and kill those who deserve it."));
    CHECK(!OcrNoise::looksBroken("A heart that can remake flesh...raise the dead, e.g. the U.S. way."));
}

TEST(Stabilizer_BrokenReadingWaitsAndTheBestReadingIsSent) {
    int64_t now = 0;
    SubtitleStabilizer s([&] { return now; });
    const std::string broken = "Take it, nd kill those wh deserve it.";
    const std::string clean = "Take it, and kill those who deserve it.";
    CHECK(s.feed(broken) == StabilizerAction::none());  // ends a sentence, but broken: waits
    now += 250;
    CHECK(s.feed(broken) == StabilizerAction::none());
    now += 250;
    CHECK(s.feed(clean) == StabilizerAction::translate(clean));
}

TEST(Stabilizer_CleanReadingRevisesALineSentFromABrokenReading) {
    int64_t now = 0;
    SubtitleStabilizer s([&] { return now; });
    const std::string broken = "Take it, nd kill those wh deserve it.";
    StabilizerAction first;
    for (int i = 0; i < 5 && first.kind == StabilizerKind::None; ++i, now += 250) first = s.feed(broken);
    CHECK(first == StabilizerAction::translate(broken));  // stable for 700 ms: sent anyway
    CHECK(s.feed(broken) == StabilizerAction::none());
    now += 250;
    CHECK(s.feed("Take it, and kill those who deserve it.") == StabilizerAction::translate("Take it, and kill those who deserve it."));
    now += 250;
    CHECK(s.feed("Take it, and kill those who deserve it.") == StabilizerAction::none());
}

// --- leftover English ----------------------------------------------------------------------------

TEST(LeftoverEnglish_Detection) {
    CHECK(hasLeftoverEnglish("Take it, nd kill shose wh deserve it.", "Al bunu, nd kill shose wh hakettiğimi."));
    CHECK(hasLeftoverEnglish("Take it and kill those who deserve it.", "Al bunu ve kill those hak edenleri."));
    CHECK(!hasLeftoverEnglish("Take it, and kill those who deserve it.", "Al bunu ve hak edenleri öldür."));
    CHECK(!hasLeftoverEnglish("Lin Mei, show me what you've got!", "Lin Mei, neyin var göster bana!"));
    CHECK(!hasLeftoverEnglish("Can you see her son at the bar?", "Her şeyi görebilir misin, son bar?"));
    CHECK(!hasLeftoverEnglish("Mind Splitting... could it be the Red Lantern Guild?",
                              "Mind Splitting... Kızıl Fener Loncası olabilir mi?"));
    CHECK(!hasLeftoverEnglish("Did you see the qi gate?", "Qi kapısını gördün mü?", {"qi"}));
}

TEST(LeftoverEnglish_SentenceStaysEnglishLikeTheUnknownMark) {
    MapEngine engine;
    engine.map = {{"I'll mend this bow for you.", "Bu yayı senin için onaracağım."},
                  {"Take it, nd kill shose wh deserve it.", "Al bunu, nd kill shose wh hakettiğimi."}};
    LineTranslator t(engine, {});
    auto line = t.translate("I'll mend this bow for you. Take it, nd kill shose wh deserve it.");
    CHECK(line.has_value());
    if (!line) return;
    CHECK(line->partial);
    CHECK(!line->original);
    CHECK(line->text.find("hakettiğimi") == std::string::npos);
    CHECK(line->text.find("Bu yayı senin için onaracağım.") == 0);
}

TEST(SpeakerRow_PipelineWaitsForABetterReadingInsteadOfShowingLeftoverEnglish) {
    MapEngine engine;
    engine.map = {{"I'll mend this bow for you.", "Bu yayı senin için onaracağım."},
                  {"Take it, nd kill shose wh deserve it.", "Al bunu, nd kill shose wh hakettiğimi."},
                  {"Take it, and kill those who deserve it.", "Al bunu ve hak edenleri öldür."}};
    Sink sink;
    int64_t now = 1000;
    SubtitlePipeline p(engine, sink.output(), clocked(&now));
    const auto broken = archer("I'll mend this bow for you. Take it, nd kill shose wh deserve it.");
    for (int i = 0; i < 5; ++i, now += 250) {
        p.process(broken);
        p.waitForTranslations();
    }
    CHECK(sink.shows().empty());  // translated after 700 ms, but English was left in: not shown
    p.process(archer());
    p.waitForTranslations();
    const auto shows = sink.shows();
    CHECK_EQ(shows.size(), size_t(1));
    if (shows.empty()) return;
    CHECK_EQ(shows[0].line.speaker, std::string("Brooding Old Archer"));
    CHECK(shows[0].line.text.find("hak edenleri") != std::string::npos);
    CHECK(!shows[0].line.original);
    for (const auto& u : sink.updates) CHECK(u.line.text.find("kill") == std::string::npos);
}

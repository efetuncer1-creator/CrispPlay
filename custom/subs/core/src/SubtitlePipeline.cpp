// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core, C++ port of CouchSubs windows/src/CouchSubs.Core/SubtitlePipeline.cs
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "crispplay/subs/SubtitlePipeline.h"

#include "crispplay/subs/OcrNoise.h"
#include "crispplay/subs/TextMatch.h"
#include "crispplay/subs/Unicode.h"

#include <algorithm>
#include <chrono>

namespace crispplay::subs {
namespace {

int64_t steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

constexpr int kOnScreenMs = 15000;
constexpr size_t kRememberedPairs = 8;

}  // namespace

struct SubtitlePipeline::Request {
    int number;
    std::string source;
    Rect cover;
    int session;
    std::optional<int64_t> leftAt;  // when the line left the screen
    bool superseded = false;        // a grown reading of the same sentence replaced it
    std::string speaker;            // read on its own row above the line (SpeakerRow)
    bool broken = false;            // OcrNoise::looksBroken(source), judged on the reading thread
};

SubtitlePipeline::SubtitlePipeline(Translator& engine, Output output, PipelineOptions options)
    : translator_(engine, [&] {
          auto o = options.translator;
          if (!o.clock) o.clock = options.clock;
          return o;
      }()),
      output_(std::move(output)), options_(std::move(options)),
      now_(options_.clock ? options_.clock : std::function<int64_t()>(steadyMs)), stabilizer_(now_) {
    thread_ = std::thread([this] { worker(); });
}

SubtitlePipeline::~SubtitlePipeline() {
    bool cancel;
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        cancel = inFlight_ && started_;
    }
    wake_.notify_all();
    if (cancel) translator_.cancel();
    thread_.join();
}

int SubtitlePipeline::readingTime(const std::string& text) {
    return std::min(8000, 2000 + static_cast<int>(uni::decode(text).size()) * 60);
}

Rect SubtitlePipeline::coverFor(const Reading& block, const OcrFrame& frame, float margin, bool widenCentered) {
    const Rect lines = block.pixelBox();
    if (lines.empty()) return {};
    std::vector<float> heights;
    for (const auto& line : block.lines())
        if (line.pixels) heights.push_back(line.pixels->h);
    std::sort(heights.begin(), heights.end());
    const float m = std::max(2.f, margin * heights[heights.size() / 2]);
    Rect cover{lines.x - m, lines.y - m, lines.w + 2 * m, lines.h + 2 * m};
    if (frame.width > 0 && frame.height > 0) {
        const float bw = frame.bandRight - frame.bandLeft;
        if (widenCentered && bw > 0) {
            const float l = frame.bandLeft + cover.x / frame.width * bw;
            const float r = frame.bandLeft + cover.right() / frame.width * bw;
            const float mid = (l + r) / 2;
            if (mid > 0.3f && mid < 0.7f) {
                const float nl = std::max(0.f, std::min(l, 1 - r)), nr = std::min(1.f, std::max(r, 1 - l));
                const float x0 = (nl - frame.bandLeft) / bw * frame.width, x1 = (nr - frame.bandLeft) / bw * frame.width;
                cover = cover.united(Rect{x0, cover.y, x1 - x0, cover.h});
            }
        }
        const float x0 = std::max(0.f, cover.x), y0 = std::max(0.f, cover.y);
        const float x1 = std::min(static_cast<float>(frame.width), cover.right());
        const float y1 = std::min(static_cast<float>(frame.height), cover.bottom());
        cover = Rect{x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    }
    return cover.united(lines);  // never smaller than the lines themselves
}

void SubtitlePipeline::process(const OcrFrame& frame) {
    const TextBox area{frame.bandLeft, frame.bandTop, frame.bandRight, frame.bandBottom};
    // Controller prompts, OCR word garbage, menus and corner gauges are not subtitles; only the
    // subtitle block is translated and its lines give the cover.
    // A name read apart on the sentence's row joins it (CouchSubs 376c08a).
    const Reading block =
        Reading::fromFrame(frame).withoutButtonPrompts().withoutOcrNoise().joiningRows().subtitleBlock(area);
    // The cover hides the whole block, the speaker row too.
    const Rect cover = coverFor(block, frame, options_.coverMargin, options_.widenCentered);
    // CrispPlay: a name row above the dialogue is the speaker, not part of the sentence.
    std::string speaker;
    std::string text;
    if (auto split = SpeakerRow::split(block, speakers_)) {
        speaker = split->speaker;
        if (split->certain) speakers_.add(split->speaker);
        text = split->body.text();
    } else if (!SpeakerRow::isLoneSpeaker(block, speakers_)) {
        text = block.text();  // a known name alone (its dialogue unread) is no line
    }

    Events events;
    bool cancel = false;
    {
        std::lock_guard lock(mutex_);
        lastFrame_.width = frame.width;
        lastFrame_.height = frame.height;
        lastFrame_.bandLeft = frame.bandLeft;
        lastFrame_.bandTop = frame.bandTop;
        lastFrame_.bandRight = frame.bandRight;
        lastFrame_.bandBottom = frame.bandBottom;
        const auto action = stabilizer_.feed(text, &frame.signature, block.box());
        switch (action.kind) {
        case StabilizerKind::Clear:
            lineLeft();
            events.push_back({SubtitleUpdate::Kind::Clear, {}, 0, false, {}});
            break;
        case StabilizerKind::Translate:
            lineLeft();
            cancel = enqueue(action.text, cover, speaker, events);
            break;
        case StabilizerKind::Restore:
            lineLeft();
            cancel = restore(action.text, cover, speaker, events);
            break;
        case StabilizerKind::None:
            // Same subtitle still on screen: its cover follows every reading (grows only, so a
            // reading that missed a word never uncovers the English).
            if (screen_ && !text.empty() && !speaker.empty() && screen_->speaker.empty() &&
                (TextMatch::isSameSubtitle(TextMatch::normalize(text), TextMatch::normalizeRows(screen_->source)) ||
                 TextMatch::isRevision(text, screen_->source)))
                addSpeaker(speaker, events);
            if (screen_ && !text.empty() && !cover.empty() &&
                (TextMatch::isSameSubtitle(TextMatch::normalize(text), TextMatch::normalizeRows(screen_->source)) ||
                 TextMatch::isRevision(text, screen_->source))) {
                const Rect grown = screen_->cover.united(cover);
                if (!screen_->cover.contains(grown)) {
                    screen_->cover = grown;
                    if (inFlight_ && inFlight_->number == screen_->number) inFlight_->cover = grown;
                    for (auto& w : waiting_)
                        if (w->number == screen_->number) w->cover = grown;
                    if (drawn_ && drawn_->first == screen_->number) {
                        const auto old = drawn_->second;
                        drawn_->second = makeLine(TranslatedLine{old.speaker, old.text, old.original, {}}, grown);
                        drawn_->second.sinceMs = old.sinceMs;
                        events.push_back({SubtitleUpdate::Kind::Cover, drawn_->second, 0, true, screen_->source});
                    }
                }
            }
            break;
        }
    }
    if (cancel) translator_.cancel();
    emit(events);
}

void SubtitlePipeline::lineLeft() {
    if (!lineOnScreen_) return;
    const int number = *lineOnScreen_;
    const int64_t time = now_();
    if (inFlight_ && inFlight_->number == number && !inFlight_->leftAt) inFlight_->leftAt = time;
    for (auto& w : waiting_)
        if (w->number == number && !w->leftAt) w->leftAt = time;
    lineOnScreen_.reset();
    screen_.reset();
}

bool SubtitlePipeline::enqueue(const std::string& source, const Rect& cover, const std::string& speaker, Events&) {
    auto request = std::make_shared<Request>(Request{++lastRequested_, source, cover, session_, std::nullopt, false, speaker,
                                                                OcrNoise::looksBroken(source)});
    lineOnScreen_ = request->number;
    screen_ = Screen{request->number, source, cover, speaker};
    if (!inFlight_) {
        inFlight_ = request;
        started_ = false;
        wake_.notify_one();
        return false;
    }
    // An older reading of the same sentence leaves the queue; a full queue drops its oldest.
    waiting_.erase(std::remove_if(waiting_.begin(), waiting_.end(),
                                  [&](const auto& w) { return TextMatch::isRevision(source, w->source); }),
                   waiting_.end());
    waiting_.push_back(request);
    while (waiting_.size() > MaxWaiting) waiting_.pop_front();
    // The running translation is of this line's start (typewriter): drop it, translate the whole.
    if (!inFlight_->superseded && inFlight_->session == session_ && TextMatch::isGrowth(source, inFlight_->source)) {
        inFlight_->superseded = true;
        return started_;
    }
    return false;
}

bool SubtitlePipeline::restore(const std::string& source, const Rect& cover, const std::string& speaker, Events& events) {
    // A line that left and came back: its old translation is shown again without the engine.
    for (auto it = pairs_.rbegin(); it != pairs_.rend(); ++it) {
        if (it->source != source) continue;
        lineOnScreen_ = it->number;
        if (it->translation && it->translation->speaker.empty()) it->translation->speaker = speaker;
        screen_ = Screen{it->number, source, cover, it->translation ? it->translation->speaker : speaker};
        if (!it->translation) return false;
        const ShownLine line = makeLine(*it->translation, cover);
        events.push_back({SubtitleUpdate::Kind::Show, line, kOnScreenMs, revisesShown(source), source});
        drawn_ = std::make_pair(it->number, line);
        lastPair_ = std::make_pair(source, it->translation->full);
        return false;
    }
    // Still being translated: the line is on screen again.
    std::vector<std::shared_ptr<Request>> requests(waiting_.begin(), waiting_.end());
    if (inFlight_) requests.insert(requests.begin(), inFlight_);
    for (auto& r : requests) {
        if (r->source != source || r->session != session_ || r->superseded) continue;
        r->leftAt.reset();
        if (r->speaker.empty()) r->speaker = speaker;
        lineOnScreen_ = r->number;
        screen_ = Screen{r->number, source, cover, r->speaker};
        r->cover = r->cover.united(cover);
        return false;
    }
    // Never translated (skipped or failed): the normal way.
    return enqueue(source, cover, speaker, events);
}

void SubtitlePipeline::addSpeaker(const std::string& speaker, Events& events) {
    const int number = screen_->number;
    screen_->speaker = speaker;
    if (inFlight_ && inFlight_->number == number && inFlight_->speaker.empty()) inFlight_->speaker = speaker;
    for (auto& w : waiting_)
        if (w->number == number && w->speaker.empty()) w->speaker = speaker;
    for (auto& p : pairs_)
        if (p.number == number && p.translation && p.translation->speaker.empty()) p.translation->speaker = speaker;
    if (drawn_ && drawn_->first == number && drawn_->second.speaker.empty()) {
        const auto old = drawn_->second;
        drawn_->second = makeLine(TranslatedLine{speaker, old.text, old.original, {}}, screen_->cover);
        drawn_->second.sinceMs = old.sinceMs;
        events.push_back({SubtitleUpdate::Kind::Show, drawn_->second, kOnScreenMs, true, screen_->source});
    }
}

bool SubtitlePipeline::revisesShown(const std::string& source) const {
    return lastPair_ && TextMatch::isRevision(source, lastPair_->first);
}

ShownLine SubtitlePipeline::makeLine(const TranslatedLine& line, const Rect& cover) const {
    ShownLine out;
    out.speaker = line.speaker;
    out.text = line.text;
    out.original = line.original;
    out.cover = cover;
    out.sinceMs = now_();
    if (lastFrame_.width > 0 && lastFrame_.height > 0 && !cover.empty()) {
        const float bw = lastFrame_.bandRight - lastFrame_.bandLeft, bh = lastFrame_.bandBottom - lastFrame_.bandTop;
        out.frameLeft = lastFrame_.bandLeft + cover.x / lastFrame_.width * bw;
        out.frameRight = lastFrame_.bandLeft + cover.right() / lastFrame_.width * bw;
        out.frameTop = lastFrame_.bandTop + cover.y / lastFrame_.height * bh;
        out.frameBottom = lastFrame_.bandTop + cover.bottom() / lastFrame_.height * bh;
    }
    return out;
}

void SubtitlePipeline::remember(int number, const std::string& source, std::optional<TranslatedLine> translation) {
    pairs_.erase(std::remove_if(pairs_.begin(), pairs_.end(), [&](const Pair& p) { return p.source == source; }), pairs_.end());
    pairs_.push_back({number, source, std::move(translation)});
    if (pairs_.size() > kRememberedPairs) pairs_.erase(pairs_.begin());
}

void SubtitlePipeline::handleResult(const std::shared_ptr<Request>& request, const std::optional<TranslatedLine>& result,
                                    Events& events) {
    if (!result) {
        // The dropped translation of a superseded line is no error.
        if (request->session != session_ || request->superseded) return;
        // Missing model or a passing error: retried while the line stays (at most once a second).
        lastFailed_ = true;
        if (lineOnScreen_ && *lineOnScreen_ == request->number) stabilizer_.forgetSent();
        return;
    }
    if (request->session != session_) return;
    lastFailed_ = false;
    TranslatedLine translated = *result;
    if (translated.speaker.empty()) translated.speaker = request->speaker;
    // CrispPlay: a translation the engine left English in (LineTranslator's leftover rule:
    // "Al bunu, nd kill shose wh hakettiğimi.") is not shown, nor one of a broken reading
    // ("Take it, nd kill shose wh") that kept a sentence English: the game's own line stays
    // visible until a better reading of it is translated (the stabilizer sends that one).
    if (translated.leftover || (request->broken && (translated.original || translated.partial))) {
        remember(request->number, request->source, std::nullopt);
        if (lineOnScreen_ && *lineOnScreen_ == request->number) stabilizer_.sentUnusable();
        return;
    }
    // A translation equal to its source means the text was already Turkish; an unusable
    // translation (original=true) is still shown, as the English line itself.
    const bool send = translated.original || stabilizer_.shouldSend(request->source, translated.full);
    remember(request->number, request->source, send ? std::optional<TranslatedLine>(translated) : std::nullopt);
    if (request->number <= lastShown_ || !send || request->superseded) return;
    const bool onScreen = lineOnScreen_ && *lineOnScreen_ == request->number;
    // A line that left long ago: its translation must not land on the next scene.
    if (!onScreen && request->leftAt && now_() - *request->leftAt > LateResultMs) return;
    lastShown_ = request->number;
    const Rect cover = screen_ && screen_->number == request->number ? screen_->cover : request->cover;
    if (translated.speaker.empty() && screen_ && screen_->number == request->number) translated.speaker = screen_->speaker;
    const ShownLine line = makeLine(translated, cover);
    events.push_back({SubtitleUpdate::Kind::Show, line, onScreen ? kOnScreenMs : readingTime(translated.full),
                      revisesShown(request->source), request->source});
    drawn_ = std::make_pair(request->number, line);
    lastPair_ = std::make_pair(request->source, translated.full);
}

void SubtitlePipeline::worker() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [&] { return stop_ || (inFlight_ && !started_); });
        if (stop_) return;
        const auto request = inFlight_;
        started_ = true;
        Events events;
        if (!request->superseded && request->session == session_) {
            const std::string speaker = request->speaker;  // addSpeaker() may change it meanwhile
            lock.unlock();
            const auto result = translator_.translate(request->source, speaker);
            lock.lock();
            ++translations_;
            if (stop_) return;
            handleResult(request, result, events);
        }
        // Lines that left long ago are not translated at all.
        const int64_t time = now_();
        waiting_.erase(std::remove_if(waiting_.begin(), waiting_.end(),
                                      [&](const auto& w) {
                                          return w->session != session_ || (w->leftAt && time - *w->leftAt > LateResultMs);
                                      }),
                       waiting_.end());
        if (waiting_.empty()) {
            inFlight_.reset();
        } else {
            inFlight_ = waiting_.front();
            waiting_.pop_front();
        }
        started_ = false;
        if (!events.empty()) {
            emitting_ = true;
            lock.unlock();
            emit(events);
            lock.lock();
            emitting_ = false;
        }
        if (!inFlight_) idle_.notify_all();
    }
}

void SubtitlePipeline::emit(const Events& events) {
    if (!output_) return;
    for (const auto& e : events) output_(e);
}

void SubtitlePipeline::waitForTranslations() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&] { return !inFlight_ && !emitting_; });
}

void SubtitlePipeline::reset() {
    Events events;
    bool cancel;
    {
        std::lock_guard lock(mutex_);
        ++session_;
        lineOnScreen_.reset();
        screen_.reset();
        drawn_.reset();
        waiting_.clear();
        cancel = inFlight_ && started_;
        pairs_.clear();
        if (stabilizer_.reset().kind == StabilizerKind::Clear) events.push_back({SubtitleUpdate::Kind::Clear, {}, 0, false, {}});
    }
    if (cancel) translator_.cancel();
    emit(events);
}

std::optional<std::pair<std::string, std::string>> SubtitlePipeline::lastPair() const {
    std::lock_guard lock(mutex_);
    return lastPair_;
}

bool SubtitlePipeline::lastFailed() const {
    std::lock_guard lock(mutex_);
    return lastFailed_;
}

int SubtitlePipeline::translationCount() const {
    std::lock_guard lock(mutex_);
    return translations_;
}

}  // namespace crispplay::subs

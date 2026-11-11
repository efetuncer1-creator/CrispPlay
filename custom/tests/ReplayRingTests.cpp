// Unit tests for the instant-replay packet ring (pure C++, no GPU).
#include <QtTest/QtTest>

#include "crispplay/replay/PacketRing.h"

using namespace crispplay::replay;

namespace {
constexpr int64_t kSecond = 1'000'000;
constexpr int64_t kAudioFrameUs = 21'333;   // 1024 samples at 48 kHz

Packet videoPacket(int64_t ptsUs, bool key, size_t bytes = 1000) {
    Packet p;
    p.stream = Stream::Video; p.ptsUs = ptsUs; p.key = key;
    p.data = std::make_shared<const std::vector<uint8_t>>(bytes, uint8_t(key ? 1 : 2));
    return p;
}
Packet audioPacket(int64_t ptsUs, size_t bytes = 100) {
    Packet p;
    p.stream = Stream::Audio; p.ptsUs = ptsUs; p.durationUs = kAudioFrameUs; p.key = true;
    p.data = std::make_shared<const std::vector<uint8_t>>(bytes, uint8_t(3));
    return p;
}
// 60 fps with a keyframe every 2 s (120 frames), starting at `originUs`.
void feedVideo(PacketRing& ring, int64_t originUs, int frames, size_t bytes = 1000, int gop = 120) {
    for (int i = 0; i < frames; ++i) ring.push(videoPacket(originUs + int64_t(i) * kSecond / 60, i % gop == 0, bytes));
}
}

class ReplayRingTests : public QObject {
    Q_OBJECT
private slots:
    void refusesVideoBeforeFirstKeyframe() {
        PacketRing ring;
        ring.push(videoPacket(0, false));
        ring.push(videoPacket(16'667, false));
        QCOMPARE(ring.stats().videoPackets, size_t(0));
        QCOMPARE(ring.stats().rejected, uint64_t(2));
        ring.push(videoPacket(33'333, true));
        ring.push(videoPacket(50'000, false));
        QCOMPARE(ring.stats().videoPackets, size_t(2));
        QVERIFY(ring.snapshot().packets.front().key);
    }

    void durationEvictionKeepsWindowPlusOneGop() {
        PacketRing ring({10 * kSecond, size_t(1) << 30});
        feedVideo(ring, 5 * kSecond, 60 * 60);   // one minute
        const auto stats = ring.stats();
        // At least the window, at most the window plus one 2 s GOP.
        QVERIFY2(stats.spanUs >= 10 * kSecond, qPrintable(QString::number(stats.spanUs)));
        QVERIFY2(stats.spanUs <= 12 * kSecond + 20'000, qPrintable(QString::number(stats.spanUs)));
        QCOMPARE(stats.byteEvictions, uint64_t(0));
        QCOMPARE(stats.bytes, stats.videoPackets * size_t(1000));
        QVERIFY(ring.snapshot().packets.front().key);
    }

    void byteCapEvictsWholeGops() {
        // 120 kB per GOP; the cap holds three GOPs and a bit.
        PacketRing ring({60 * kSecond, 400'000});
        feedVideo(ring, 0, 60 * 30);
        const auto stats = ring.stats();
        QVERIFY2(stats.bytes <= 400'000, qPrintable(QString::number(stats.bytes)));
        QVERIFY(stats.byteEvictions > 0);
        QVERIFY(stats.keyframes >= 1);
        const Clip clip = ring.snapshot();
        QVERIFY(clip.packets.front().key);
        QVERIFY(clip.durationUs < 60 * kSecond);   // shorter than asked: the cap won
        QCOMPARE(clip.bytes, stats.bytes);
    }

    void byteCapNeverDropsTheOnlyGop() {
        PacketRing ring({60 * kSecond, 10'000});
        feedVideo(ring, 0, 100, 1000, 1000);   // one long GOP, 100 kB
        QCOMPARE(ring.stats().videoPackets, size_t(100));
        QCOMPARE(ring.stats().keyframes, size_t(1));
    }

    void snapshotStartsAtKeyframeAtOrBeforeRequest() {
        PacketRing ring({30 * kSecond, size_t(1) << 30});
        const int64_t origin = 1000 * kSecond;
        feedVideo(ring, origin, 60 * 20);   // 20 s, keyframes at 0, 2, 4, ... s
        const int64_t endUs = origin + 20 * kSecond;   // newest pts + one frame
        // 5 s back is t = 15 s; the keyframe at or before it is 14 s.
        Clip clip = ring.snapshot(5 * kSecond);
        QCOMPARE(clip.originUs, origin + 14 * kSecond);
        QVERIFY(clip.packets.front().key);
        QCOMPARE(clip.packets.front().ptsUs, int64_t(0));
        QVERIFY(qAbs(clip.durationUs - 6 * kSecond) < 100);
        QCOMPARE(clip.videoPackets, size_t(6 * 60));
        // Exactly on a keyframe: 6 s back is t = 14 s.
        clip = ring.snapshot(endUs - (origin + 14 * kSecond));
        QCOMPARE(clip.originUs, origin + 14 * kSecond);
        // More than the ring holds: everything, from its first keyframe.
        clip = ring.snapshot(120 * kSecond);
        QCOMPARE(clip.originUs, origin);
        QCOMPARE(clip.videoPackets, size_t(20 * 60));
        // 0 uses the configured window.
        QCOMPARE(ring.snapshot().originUs, origin);
    }

    void snapshotRebasesTimestampsAndDurations() {
        PacketRing ring({4 * kSecond, size_t(1) << 30});
        const int64_t origin = 777 * kSecond + 123;
        feedVideo(ring, origin, 60 * 10);
        const Clip clip = ring.snapshot();
        int64_t previous = -1, end = 0;
        for (const auto& p : clip.packets) {
            QCOMPARE(p.stream, Stream::Video);
            QVERIFY(p.ptsUs > previous);
            QVERIFY(p.durationUs > 0);
            if (previous >= 0) QCOMPARE(p.ptsUs, end);   // contiguous: pts = previous pts + its duration
            previous = p.ptsUs; end = p.ptsUs + p.durationUs;
        }
        QCOMPARE(clip.packets.front().ptsUs, int64_t(0));
        QCOMPARE(end, clip.durationUs);
        // The ring itself is untouched by a snapshot.
        QCOMPARE(ring.snapshot().originUs, clip.originUs);
    }

    void nonMonotonicVideoIsNudgedForward() {
        PacketRing ring;
        ring.push(videoPacket(1000, true));
        ring.push(videoPacket(1000, false));
        ring.push(videoPacket(900, false));
        const Clip clip = ring.snapshot();
        QCOMPARE(clip.videoPackets, size_t(3));
        QCOMPARE(clip.packets[1].ptsUs, int64_t(1));
        QCOMPARE(clip.packets[2].ptsUs, int64_t(2));
    }

    void audioAndVideoAreInterleavedByTime() {
        PacketRing ring({30 * kSecond, size_t(1) << 30});
        const int64_t origin = 50 * kSecond;
        // Audio starts half a second before the picture and runs alongside it.
        for (int64_t t = origin - kSecond / 2; t < origin + 6 * kSecond; t += kAudioFrameUs) ring.push(audioPacket(t));
        feedVideo(ring, origin, 60 * 6);
        const Clip clip = ring.snapshot(3 * kSecond);   // keyframe at 2 s
        QCOMPARE(clip.originUs, origin + 2 * kSecond);
        QVERIFY(clip.audioPackets > 0);
        QCOMPARE(clip.packets.front().stream, Stream::Video);
        int64_t previous = -1;
        size_t audio = 0, video = 0;
        for (const auto& p : clip.packets) {
            QVERIFY2(p.ptsUs >= previous, "packets must be in timestamp order");
            QVERIFY(p.ptsUs >= 0);
            QVERIFY(p.ptsUs < clip.durationUs);
            previous = p.ptsUs;
            (p.stream == Stream::Audio ? audio : video)++;
        }
        QCOMPARE(audio, clip.audioPackets);
        QCOMPARE(video, clip.videoPackets);
        // About 4 s of audio frames, none from before the first picture.
        QVERIFY(qAbs(int(audio) - int(4 * kSecond / kAudioFrameUs)) <= 2);
        QCOMPARE(clip.bytes, video * 1000 + audio * 100);
    }

    void audioOlderThanThePictureIsEvicted() {
        PacketRing ring({4 * kSecond, size_t(1) << 30});
        for (int64_t t = 0; t < 60 * kSecond; t += kAudioFrameUs) ring.push(audioPacket(t));
        feedVideo(ring, 0, 60 * 60);
        const auto stats = ring.stats();
        // Audio is bounded by the picture's span (window + one GOP), not unbounded.
        QVERIFY2(stats.audioPackets <= size_t((6 * kSecond + 100'000) / kAudioFrameUs) + 2, qPrintable(QString::number(stats.audioPackets)));
        QCOMPARE(stats.bytes, stats.videoPackets * size_t(1000) + stats.audioPackets * size_t(100));
    }

    void audioWithoutPictureStaysBounded() {
        PacketRing ring({2 * kSecond, size_t(1) << 30});
        for (int64_t t = 0; t < 30 * kSecond; t += kAudioFrameUs) ring.push(audioPacket(t));
        QVERIFY(ring.stats().audioPackets <= size_t(2 * kSecond / kAudioFrameUs) + 2);
        QVERIFY(ring.snapshot().empty());
    }

    void shrinkingTheWindowEvictsAndClearEmpties() {
        PacketRing ring({30 * kSecond, size_t(1) << 30});
        feedVideo(ring, 0, 60 * 30);
        QVERIFY(ring.stats().spanUs > 29 * kSecond);
        ring.setLimits({4 * kSecond, size_t(1) << 30});
        QVERIFY(ring.stats().spanUs <= 6 * kSecond + 20'000);
        ring.clear();
        QCOMPARE(ring.stats().bytes, size_t(0));
        QVERIFY(ring.snapshot().empty());
    }

    void audioLayoutKeepsContiguousFrames() {
        std::vector<int64_t> pts;
        for (int i = 0; i < 100; ++i) pts.push_back(int64_t(i) * 1024 * kSecond / 48000);
        const auto layout = layoutAudio(pts, 100LL * 1024 * kSecond / 48000);
        QCOMPARE(layout.size(), size_t(100));
        for (int i = 0; i < 100; ++i) {
            QCOMPARE(layout[size_t(i)].source, i);
            QCOMPARE(layout[size_t(i)].ptsSamples, int64_t(i) * 1024);
        }
    }

    void audioLayoutFillsGapsWithSilence() {
        // 10 frames, a 1 s hole (pause), 10 more; the clip runs 0.5 s past the sound.
        std::vector<int64_t> pts;
        const int64_t lead = 100'000;   // sound starts 100 ms into the clip
        for (int i = 0; i < 10; ++i) pts.push_back(lead + int64_t(i) * 1024 * kSecond / 48000);
        const int64_t resume = pts.back() + kSecond;
        for (int i = 0; i < 10; ++i) pts.push_back(resume + int64_t(i) * 1024 * kSecond / 48000);
        const int64_t duration = pts.back() + kSecond / 2;
        const auto layout = layoutAudio(pts, duration);
        int real = 0, silent = 0;
        for (size_t i = 0; i < layout.size(); ++i) {
            QCOMPARE(layout[i].ptsSamples, int64_t(i) * 1024);   // one unbroken frame grid
            if (layout[i].source < 0) ++silent; else { QCOMPARE(layout[i].source, real); ++real; }
        }
        QCOMPARE(real, 20);
        QCOMPARE(layout.front().source, -1);   // leading silence
        QCOMPARE(layout.back().source, -1);    // trailing silence up to the picture's end
        // The track covers the clip to within one frame.
        QVERIFY(qAbs(int64_t(layout.size()) * 1024 - duration * 48000 / kSecond) <= 1024);
        // Each real frame stays within half a frame of where it was heard.
        for (const auto& slot : layout)
            if (slot.source >= 0) QVERIFY(qAbs(slot.ptsSamples - pts[size_t(slot.source)] * 48000 / kSecond) <= 513);
        QVERIFY(silent > 60);
    }

    void audioLayoutDropsOverlapsAndEmptyInput() {
        QVERIFY(layoutAudio({}, kSecond).empty());
        // A duplicate timestamp cannot be placed twice.
        const auto layout = layoutAudio({0, 0, 21'333}, 64'000);
        QCOMPARE(layout.size(), size_t(3));
        QCOMPARE(layout[0].source, 0);
        QCOMPARE(layout[1].source, 2);
        QCOMPARE(layout[2].source, -1);
    }
};

QTEST_APPLESS_MAIN(ReplayRingTests)
#include "ReplayRingTests.moc"

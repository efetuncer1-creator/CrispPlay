#include "crispplay/discord/IpcClient.h"

#include "crispplay/discord/Presence.h"

#include <QJsonDocument>
#include <QUuid>
#include <QDebug>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace crispplay::discord {

using Clock = std::chrono::steady_clock;

struct IpcClient::Impl {
    Options opt;
    std::function<void(State)> onState;

    // Mailbox: written by any thread under `m`, read by the worker.
    std::mutex m;
    std::optional<QJsonObject> desired;
    quint64 version = 0;

    HANDLE wakeEv = nullptr;   // auto-reset: the mailbox changed
    HANDLE stopEv = nullptr;   // manual-reset: shut down
    HANDLE ioEv = nullptr;     // manual-reset: overlapped completion (worker only)
    std::thread worker;
    std::atomic<State> state{State::Idle};
    std::atomic<bool> stopping{false};

    // Worker-thread only.
    HANDLE pipe = INVALID_HANDLE_VALUE;
    FrameParser parser;
    bool loggedAbsent = false;

    explicit Impl(Options o, std::function<void(State)> cb) : opt(std::move(o)), onState(std::move(cb)) {
        wakeEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stopEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ioEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        worker = std::thread([this] { run(); });
    }
    ~Impl() {
        CloseHandle(wakeEv);
        CloseHandle(stopEv);
        CloseHandle(ioEv);
    }

    void setState(State s) {
        if (state.exchange(s) != s && onState) onState(s);
    }

    // --- overlapped I/O with a timeout ---------------------------------------

    bool aborted = false;   // last finishIo() was cut short by timeout or stop (pipe still fine)

    bool finishIo(OVERLAPPED& ov, bool started, DWORD timeoutMs, bool honorStop, DWORD& transferred) {
        transferred = 0;
        aborted = false;
        if (!started) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING && err != ERROR_MORE_DATA) return false;
            if (err == ERROR_IO_PENDING) {
                HANDLE handles[2] = {ioEv, stopEv};
                const DWORD r = WaitForMultipleObjects(honorStop ? 2 : 1, handles, FALSE, timeoutMs);
                if (r != WAIT_OBJECT_0) {   // timeout or stop: abandon the operation
                    aborted = true;
                    CancelIoEx(pipe, &ov);
                    GetOverlappedResult(pipe, &ov, &transferred, TRUE);
                    return false;
                }
            }
        }
        if (GetOverlappedResult(pipe, &ov, &transferred, FALSE)) return true;
        return GetLastError() == ERROR_MORE_DATA;   // message-mode pipe, partial read
    }

    bool writeAll(const QByteArray& bytes, DWORD timeoutMs, bool honorStop) {
        OVERLAPPED ov{};
        ov.hEvent = ioEv;
        ResetEvent(ioEv);
        DWORD n = 0;
        const bool started = WriteFile(pipe, bytes.constData(), DWORD(bytes.size()), nullptr, &ov) != FALSE;
        return finishIo(ov, started, timeoutMs, honorStop, n) && n == DWORD(bytes.size());
    }

    // Reads whatever arrives within timeoutMs into the parser. false = pipe broken.
    bool readSome(DWORD timeoutMs, bool& gotData) {
        gotData = false;
        char buf[4096];
        OVERLAPPED ov{};
        ov.hEvent = ioEv;
        ResetEvent(ioEv);
        DWORD n = 0;
        const bool started = ReadFile(pipe, buf, sizeof buf, nullptr, &ov) != FALSE;
        const bool ok = finishIo(ov, started, timeoutMs, true, n);
        if (n > 0) { parser.append(QByteArray(buf, int(n))); gotData = true; return true; }
        if (ok || aborted) return true;   // empty read or timeout: the pipe is still usable
        return false;                     // broken pipe and friends
    }

    // --- connection ----------------------------------------------------------

    void closePipe() {
        if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
        parser = FrameParser();
    }

    bool sendFrame(quint32 op, const QByteArray& payload, DWORD timeoutMs, bool honorStop = true) {
        return writeAll(encodeFrame(op, payload), timeoutMs, honorStop);
    }

    bool openPipe() {
        for (int i = 0; i < opt.pipeCount; ++i) {
            const QString name = opt.pipeBase + QString::number(i);
            HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), GENERIC_READ | GENERIC_WRITE, 0,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h != INVALID_HANDLE_VALUE) { pipe = h; return true; }
        }
        return false;
    }

    // Opens the pipe and completes the handshake (waits for READY).
    bool connect() {
        if (!openPipe()) {
            if (!loggedAbsent) qInfo().noquote() << "[discord] Discord is not running (no IPC pipe); retrying with backoff";
            loggedAbsent = true;
            return false;
        }
        loggedAbsent = false;
        if (!sendFrame(OpHandshake, buildHandshake(opt.clientId), DWORD(opt.ioTimeoutMs))) { closePipe(); return false; }
        const auto deadline = Clock::now() + std::chrono::milliseconds(opt.handshakeTimeoutMs);
        while (Clock::now() < deadline && !stopping) {
            Frame f;
            while (parser.next(f)) {
                if (f.opcode == OpClose) { closePipe(); return false; }
                if (f.opcode == OpFrame) {
                    const QJsonObject o = QJsonDocument::fromJson(f.payload).object();
                    const QString evt = o.value(QStringLiteral("evt")).toString();
                    if (evt == QLatin1String("READY")) { qInfo().noquote() << "[discord] connected"; return true; }
                    if (evt == QLatin1String("ERROR")) {
                        qInfo().noquote() << "[discord] handshake rejected:" << o.value(QStringLiteral("data")).toObject()
                                                 .value(QStringLiteral("message")).toString();
                        closePipe();
                        return false;
                    }
                }
            }
            if (parser.failed()) break;
            bool got = false;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (!readSome(DWORD(std::max<long long>(left, 1)), got)) break;
        }
        closePipe();
        return false;
    }

    // Handles incoming frames (PING/CLOSE/errors). false = connection is gone.
    bool pump() {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return false;
            if (avail == 0) break;
            bool got = false;
            if (!readSome(DWORD(opt.ioTimeoutMs), got)) return false;
            if (!got) break;
        }
        Frame f;
        while (parser.next(f)) {
            if (f.opcode == OpClose) return false;
            if (f.opcode == OpPing) {
                if (!sendFrame(OpPong, f.payload, DWORD(opt.ioTimeoutMs))) return false;
            } else if (f.opcode == OpFrame) {
                const QJsonObject o = QJsonDocument::fromJson(f.payload).object();
                if (o.value(QStringLiteral("evt")).toString() == QLatin1String("ERROR"))
                    qInfo().noquote() << "[discord] activity rejected:" << o.value(QStringLiteral("data")).toObject()
                                             .value(QStringLiteral("message")).toString();
            }
        }
        return !parser.failed();
    }

    bool sendActivity(const std::optional<QJsonObject>& activity, bool honorStop, DWORD timeoutMs) {
        const QByteArray json = buildSetActivity(GetCurrentProcessId(), activity,
                                                 QUuid::createUuid().toString(QUuid::WithoutBraces));
        return sendFrame(OpFrame, json, timeoutMs, honorStop);
    }

    static DWORD untilMs(Clock::time_point t) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t - Clock::now()).count();
        return DWORD(std::max<long long>(ms, 0) + 1);
    }

    DWORD waitFor(DWORD ms) {
        HANDLE handles[2] = {stopEv, wakeEv};
        return WaitForMultipleObjects(2, handles, FALSE, ms);
    }

    // --- worker --------------------------------------------------------------

    void run() {
        int backoff = opt.backoffMinMs;
        auto nextAttempt = Clock::now();
        auto lastSend = Clock::time_point::min();
        quint64 sentVersion = 0;
        bool sentAny = false;

        while (!stopping) {
            std::optional<QJsonObject> want;
            quint64 ver;
            { std::lock_guard lock(m); want = desired; ver = version; }
            const bool dirty = !sentAny || ver != sentVersion;
            const auto now = Clock::now();
            DWORD waitMs = INFINITE;

            if (pipe == INVALID_HANDLE_VALUE) {
                if (!want) {
                    setState(State::Idle);
                } else if (now >= nextAttempt) {
                    setState(State::Searching);
                    if (connect()) {
                        backoff = opt.backoffMinMs;
                        lastSend = Clock::time_point::min();
                        sentAny = false;   // resend the current activity
                        setState(State::Connected);
                        continue;
                    }
                    nextAttempt = Clock::now() + std::chrono::milliseconds(backoff);
                    backoff = std::min(backoff * 2, opt.backoffMaxMs);
                    waitMs = untilMs(nextAttempt);
                } else {
                    waitMs = untilMs(nextAttempt);
                }
            } else {
                waitMs = 250;   // also the cadence for reading PING / CLOSE
                bool alive = pump();
                if (alive && dirty) {
                    const auto sinceSend = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSend).count();
                    if (!want) {
                        // Clear, then close: Discord drops the activity with the connection anyway.
                        sendActivity(std::nullopt, true, DWORD(opt.ioTimeoutMs));
                        closePipe();
                        sentVersion = ver; sentAny = true;
                        setState(State::Idle);
                        continue;
                    }
                    if (lastSend == Clock::time_point::min() || sinceSend >= opt.minUpdateIntervalMs) {
                        alive = sendActivity(want, true, DWORD(opt.ioTimeoutMs));
                        if (alive) { lastSend = Clock::now(); sentVersion = ver; sentAny = true; }
                    } else {
                        waitMs = std::min<DWORD>(waitMs, DWORD(opt.minUpdateIntervalMs - sinceSend) + 1);
                    }
                }
                if (!alive && !stopping) {
                    qInfo().noquote() << "[discord] connection lost; will reconnect";
                    closePipe();
                    nextAttempt = Clock::now() + std::chrono::milliseconds(opt.backoffMinMs);
                    backoff = opt.backoffMinMs;
                    setState(State::Searching);
                    continue;
                }
            }
            if (stopping) break;
            waitFor(waitMs);
        }

        if (pipe != INVALID_HANDLE_VALUE) {
            sendActivity(std::nullopt, false, 500);   // best-effort clear on exit
            closePipe();
        }
        setState(State::Idle);
    }

    void requestStop() {
        stopping = true;
        SetEvent(stopEv);
    }
};

IpcClient::IpcClient(Options options, std::function<void(State)> onState)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(onState))) {}

IpcClient::~IpcClient() { shutdown(); }

void IpcClient::setActivity(std::optional<QJsonObject> activity) {
    if (!impl_) return;
    {
        std::lock_guard lock(impl_->m);
        impl_->desired = std::move(activity);
        ++impl_->version;
    }
    SetEvent(impl_->wakeEv);
}

void IpcClient::shutdown() {
    if (!impl_ || !impl_->worker.joinable()) return;
    impl_->requestStop();
    impl_->worker.join();
}

IpcClient::State IpcClient::state() const { return impl_ ? impl_->state.load() : State::Idle; }

} // namespace crispplay::discord

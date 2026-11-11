// Discord IPC client over the local named pipe (\\.\pipe\discord-ipc-0..9).
//
// Everything blocking happens on one worker thread. The calling thread only
// stores the newest activity in a mailbox (latest wins, so the queue is bounded
// by construction) and signals an event. Pipe I/O is overlapped and every wait
// has a timeout; reconnecting uses exponential backoff, and while nothing is
// to be shown the pipe is not touched at all.
#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace crispplay::discord {

class IpcClient {
public:
    enum class State { Idle, Searching, Connected };

    struct Options {
        QString clientId;
        QString pipeBase = QStringLiteral("\\\\.\\pipe\\discord-ipc-");
        int pipeCount = 10;
        int ioTimeoutMs = 2000;           // one read or write
        int handshakeTimeoutMs = 3000;    // waiting for READY
        int minUpdateIntervalMs = 5000;   // Discord rate-limits activity updates
        int backoffMinMs = 2000;
        int backoffMaxMs = 30000;
    };

    // `onState` is called on the worker thread.
    explicit IpcClient(Options options, std::function<void(State)> onState = {});
    ~IpcClient();   // = shutdown()
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    // Newest wins. nullopt clears the activity. Never blocks.
    void setActivity(std::optional<QJsonObject> activity);
    // Clears the activity (best effort, short timeout), closes the pipe, joins
    // the thread. Idempotent.
    void shutdown();
    State state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crispplay::discord

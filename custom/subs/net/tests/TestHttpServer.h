// Minimal HTTP/1.1 server on 127.0.0.1 for the tests (own thread, Connection: close).
// Lets a test serve, truncate, redirect, stall or trickle responses. Part of CrispPlay, GPL-3.0.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QThread>

#include <functional>

struct TestRequest {
    QByteArray method, path, body;
    QHash<QByteArray, QByteArray> headers; // lower-case names
};

struct TestResponse {
    int status = 200;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    qint64 sendBytes = -1;  // >= 0: send only this many body bytes, then close (Content-Length stays full)
    bool hang = false;      // read the request, never answer
    int chunkDelayMs = 0;   // > 0: send the body in 16 KiB pieces with this delay
};

class TestHttpServer {
public:
    using Handler = std::function<TestResponse(const TestRequest&)>;
    explicit TestHttpServer(Handler handler);
    ~TestHttpServer();

    quint16 port() const { return port_; }
    QByteArray url(const QByteArray& path) const;
    QList<TestRequest> requests() const;
    void setHandler(Handler handler);

private:
    friend class TestHttpWorker;
    TestResponse handle(const TestRequest& request);

    QThread thread_;
    QObject* worker_ = nullptr;
    quint16 port_ = 0;
    mutable QMutex mutex_;
    Handler handler_;
    QList<TestRequest> requests_;
};

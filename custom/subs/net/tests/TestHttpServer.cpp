#include "TestHttpServer.h"

#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class TestHttpWorker : public QObject {
    Q_OBJECT
public:
    explicit TestHttpWorker(TestHttpServer* owner) : owner_(owner) {}

public slots:
    quint16 listen() {
        server_ = new QTcpServer(this);
        if (!server_->listen(QHostAddress::LocalHost, 0)) return 0;
        connect(server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server_->nextPendingConnection()) accept(s);
        });
        return server_->serverPort();
    }
    void stop() {
        for (const QPointer<QTcpSocket>& s : sockets_)
            if (s) s->abort();
        delete server_;
        server_ = nullptr;
    }

private:
    void accept(QTcpSocket* s) {
        sockets_.append(s);
        auto buffer = std::make_shared<QByteArray>();
        auto done = std::make_shared<bool>(false);
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QTcpSocket::readyRead, this, [this, s, buffer, done] {
            if (*done) { s->readAll(); return; }
            buffer->append(s->readAll());
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0) return;
            TestRequest req;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            req.method = first.value(0);
            req.path = first.value(1);
            for (int i = 1; i < lines.size(); ++i) {
                const qsizetype colon = lines[i].indexOf(':');
                if (colon > 0) req.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
            }
            const qint64 length = req.headers.value("content-length", "0").toLongLong();
            if (buffer->size() - (end + 4) < length) return;
            req.body = buffer->mid(end + 4, length);
            *done = true;
            respond(s, owner_->handle(req));
        });
    }

    void respond(QTcpSocket* s, const TestResponse& r) {
        if (r.hang) return;
        QByteArray head = "HTTP/1.1 " + QByteArray::number(r.status) + " X\r\nConnection: close\r\n";
        bool hasLength = false;
        for (const auto& h : r.headers) {
            head += h.first + ": " + h.second + "\r\n";
            if (h.first.toLower() == "content-length") hasLength = true;
        }
        if (!hasLength) head += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\n";
        head += "\r\n";
        s->write(head);
        const QByteArray body = r.sendBytes >= 0 ? r.body.left(r.sendBytes) : r.body;
        if (r.chunkDelayMs <= 0) {
            s->write(body);
            s->disconnectFromHost();
            return;
        }
        auto offset = std::make_shared<qint64>(0);
        auto timer = new QTimer(s);
        QPointer<QTcpSocket> guard(s);
        connect(timer, &QTimer::timeout, s, [guard, timer, offset, body] {
            if (!guard || guard->state() != QAbstractSocket::ConnectedState) { timer->stop(); return; }
            guard->write(body.mid(*offset, 16 * 1024));
            *offset += 16 * 1024;
            if (*offset >= body.size()) { timer->stop(); guard->disconnectFromHost(); }
        });
        timer->start(r.chunkDelayMs);
    }

    TestHttpServer* owner_;
    QTcpServer* server_ = nullptr;
    QList<QPointer<QTcpSocket>> sockets_;
};

TestHttpServer::TestHttpServer(Handler handler) : handler_(std::move(handler)) {
    auto* w = new TestHttpWorker(this);
    worker_ = w;
    w->moveToThread(&thread_);
    thread_.start();
    QMetaObject::invokeMethod(w, &TestHttpWorker::listen, Qt::BlockingQueuedConnection, &port_);
}

TestHttpServer::~TestHttpServer() {
    QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(worker_, "deleteLater", Qt::QueuedConnection);
    thread_.quit();
    thread_.wait();
}

QByteArray TestHttpServer::url(const QByteArray& path) const {
    return "http://127.0.0.1:" + QByteArray::number(port_) + path;
}

QList<TestRequest> TestHttpServer::requests() const {
    QMutexLocker lock(&mutex_);
    return requests_;
}

void TestHttpServer::setHandler(Handler handler) {
    QMutexLocker lock(&mutex_);
    handler_ = std::move(handler);
}

TestResponse TestHttpServer::handle(const TestRequest& request) {
    Handler h;
    {
        QMutexLocker lock(&mutex_);
        requests_.append(request);
        h = handler_;
    }
    return h ? h(request) : TestResponse{404, {}, {}};
}

#include "TestHttpServer.moc"

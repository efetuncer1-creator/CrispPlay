// Blocking HTTPS POST for the online translators. Part of CrispPlay, GPL-3.0 (see LICENSE).
//
// WinHTTP instead of QNetworkAccessManager: translate() runs on a pipeline worker thread
// that has no Qt event loop; WinHTTP's synchronous mode needs none, has per-operation
// timeouts that we shrink to the time left before the deadline, and cancel() can close
// the request handle from another thread (the documented way to abort a WinHTTP call).
// No redirects (an API answer never redirects, and a redirect must not carry the key),
// no cookies, no logging.
#pragma once

#include <QByteArray>
#include <QUrl>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

namespace crispplay::subs::detail {

struct HttpResponse {
    int status = 0;
    QByteArray body;
};

class WinHttpPost {
public:
    WinHttpPost();
    ~WinHttpPost();
    WinHttpPost(const WinHttpPost&) = delete;
    WinHttpPost& operator=(const WinHttpPost&) = delete;

    // `headers`: CRLF separated "Name: value" lines. nullopt on any transport error,
    // timeout, cancel or a body larger than maxBody. The URL must be https, or http to a
    // loopback host (checked by the caller as well).
    std::optional<HttpResponse> post(const QUrl& url, const std::wstring& headers, const QByteArray& body,
                                     int timeoutMs, qint64 maxBody = 256 * 1024);
    void cancel();

private:
    void* session_ = nullptr;
    std::mutex mutex_;
    void* current_ = nullptr;
    std::atomic<bool> cancelled_{false};
};

bool isLoopbackHost(const QString& host);
// https anywhere, http only to 127.0.0.1 / localhost / ::1; no user:password@.
bool onlineEndpointAllowed(const QUrl& url);

} // namespace crispplay::subs::detail

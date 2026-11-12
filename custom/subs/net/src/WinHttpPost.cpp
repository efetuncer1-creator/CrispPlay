// Part of CrispPlay, GPL-3.0 (see LICENSE). Written for CrispPlay.
#include "WinHttpPost.h"

#include <windows.h>
#include <winhttp.h>

#include <chrono>

namespace crispplay::subs::detail {

bool isLoopbackHost(const QString& host) {
    return host == QLatin1String("127.0.0.1") || host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0
        || host == QLatin1String("::1");
}

bool onlineEndpointAllowed(const QUrl& url) {
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()) return false;
    if (url.scheme() == QLatin1String("https")) return true;
    return url.scheme() == QLatin1String("http") && isLoopbackHost(url.host());
}

WinHttpPost::WinHttpPost() {
    session_ = WinHttpOpen(L"CrispPlay", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_)
        session_ = WinHttpOpen(L"CrispPlay", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2; // TLS 1.3 flag unknown before Windows 11
            WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
    }
}

WinHttpPost::~WinHttpPost() {
    if (session_) WinHttpCloseHandle(session_);
}

void WinHttpPost::cancel() {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    if (current_) {
        WinHttpCloseHandle(current_); // makes the blocked call on the worker thread fail
        current_ = nullptr;
    }
}

std::optional<HttpResponse> WinHttpPost::post(const QUrl& url, const std::wstring& headers, const QByteArray& body,
                                              int timeoutMs, qint64 maxBody) {
    if (!session_ || !onlineEndpointAllowed(url) || timeoutMs <= 0) return std::nullopt;
    cancelled_ = false;
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    auto remaining = [&]() -> int {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return left > 0 ? int(left) : 0;
    };

    const bool secure = url.scheme() == QLatin1String("https");
    const std::wstring host = url.host().toStdWString();
    const INTERNET_PORT port = INTERNET_PORT(url.port(secure ? 443 : 80));
    QString pathQuery = url.path(QUrl::FullyEncoded);
    if (pathQuery.isEmpty()) pathQuery = QStringLiteral("/");
    if (url.hasQuery()) pathQuery += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
    const std::wstring path = pathQuery.toStdWString();

    HINTERNET connection = WinHttpConnect(session_, host.c_str(), port, 0);
    if (!connection) return std::nullopt;
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        WinHttpCloseHandle(connection);
        return std::nullopt;
    }
    {
        std::lock_guard lock(mutex_);
        current_ = request;
    }
    auto finish = [&]() {
        std::lock_guard lock(mutex_);
        if (current_) { // not closed by cancel()
            WinHttpCloseHandle(current_);
            current_ = nullptr;
        }
        WinHttpCloseHandle(connection);
    };
    auto alive = [&]() { return !cancelled_.load() && remaining() > 0; };

    DWORD features = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof features);
    if (isLoopbackHost(url.host())) { // a local test or LLM server is never reached through a proxy
        WINHTTP_PROXY_INFO none{WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr};
        WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &none, sizeof none);
    }
    int t = remaining();
    WinHttpSetTimeouts(request, t, t, t, t);

    std::optional<HttpResponse> result;
    if (alive() && WinHttpSendRequest(request, headers.c_str(), DWORD(-1L), const_cast<char*>(body.constData()),
                                      DWORD(body.size()), DWORD(body.size()), 0)) {
        t = remaining();
        if (alive()) WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &t, sizeof t);
        if (alive() && WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof status;
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
                HttpResponse response;
                response.status = int(status);
                bool ok = true;
                for (;;) {
                    if (!alive()) { ok = false; break; }
                    t = remaining();
                    WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof t);
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
                    if (available == 0) break;
                    if (response.body.size() + qint64(available) > maxBody) { ok = false; break; }
                    const qsizetype old = response.body.size();
                    response.body.resize(old + available);
                    DWORD read = 0;
                    if (!alive() || !WinHttpReadData(request, response.body.data() + old, available, &read)) {
                        ok = false;
                        break;
                    }
                    response.body.resize(old + read);
                }
                if (ok) result = std::move(response);
            }
        }
    }
    finish();
    if (cancelled_.load()) return std::nullopt;
    return result;
}

} // namespace crispplay::subs::detail

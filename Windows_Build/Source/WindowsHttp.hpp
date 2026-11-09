#pragma once

// One background HTTP GET (WinHTTP) for the editor's "Check for updates":
// the answer is posted to a window as `message`, then read with Take().
// NO THREAD OUTLIVES ITS OWNER: Cancel() (also the destructor) closes the
// WinHTTP handles, which ends a blocked call at once, and joins; the host
// may unload the plugin right after closing the editor. Timeouts bound the
// worst case anyway.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace oxo::win {

class HttpGet {
public:
    HttpGet() = default;
    HttpGet(const HttpGet &) = delete;
    HttpGet &operator=(const HttpGet &) = delete;
    ~HttpGet() { Cancel(); }

    // false = a request is still running.
    bool Start(const std::wstring &url, HWND window, UINT message) {
        if (Running()) return false;
        Cancel(); // joins a finished one
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = done_ = false;
        body_.reset();
        window_ = window;
        message_ = message;
        worker_ = std::thread([this, url] { Run(url); });
        return true;
    }
    bool Running() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return worker_.joinable() && !done_;
    }
    // After the message: the body of a 200 answer, nullopt = failed.
    std::optional<std::string> Take() {
        if (worker_.joinable()) worker_.join(); // it posted as its last act
        std::lock_guard<std::mutex> lock(mutex_);
        return std::move(body_);
    }
    void Cancel() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
            CloseAll();
        }
        if (worker_.joinable()) worker_.join();
    }

private:
    void CloseAll() { // under the mutex
        for (HINTERNET *h : {&request_, &connect_, &session_})
            if (*h) {
                WinHttpCloseHandle(*h);
                *h = nullptr;
            }
    }
    // Keeps a new handle where Cancel() can close it; false = cancelled.
    bool Keep(HINTERNET &slot, HINTERNET h) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!h) return false;
        if (cancelled_) {
            WinHttpCloseHandle(h);
            return false;
        }
        slot = h;
        return true;
    }
    HINTERNET Handle(HINTERNET &slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancelled_ ? nullptr : slot;
    }

    void Run(const std::wstring &url) {
        std::optional<std::string> body = Fetch(url);
        std::lock_guard<std::mutex> lock(mutex_);
        CloseAll();
        done_ = true;
        if (cancelled_) return;
        body_ = std::move(body);
        if (window_) PostMessageW(window_, message_, 0, 0);
    }

    std::optional<std::string> Fetch(const std::wstring &url) {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        wchar_t host[256] = {}, path[2048] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = DWORD(std::size(host));
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = DWORD(std::size(path));
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return std::nullopt;
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
        const DWORD access = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY; // the system's proxy settings (8.1+)
#else
        const DWORD access = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
        if (!Keep(session_, WinHttpOpen(L"Synth2", access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)))
            return std::nullopt;
        HINTERNET session = Handle(session_);
        if (!session) return std::nullopt;
        WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000); // resolve, connect, send, receive
        if (!Keep(connect_, WinHttpConnect(session, host, parts.nPort, 0))) return std::nullopt;
        HINTERNET connect = Handle(connect_);
        if (!connect) return std::nullopt;
        const DWORD secure = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        if (!Keep(request_, WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, secure | WINHTTP_FLAG_REFRESH)))
            return std::nullopt;
        HINTERNET request = Handle(request_);
        if (!request ||
            !WinHttpSendRequest(request, L"Accept: application/json\r\nCache-Control: no-cache\r\n", DWORD(-1),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr))
            return std::nullopt;
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) ||
            status != 200)
            return std::nullopt;
        std::string body;
        for (;;) {
            char buffer[8192];
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer, DWORD(sizeof(buffer)), &read)) return std::nullopt;
            if (read == 0) break;
            body.append(buffer, read);
            if (body.size() > (1u << 20)) return std::nullopt; // not a releases.json
        }
        return body;
    }

    mutable std::mutex mutex_;
    std::thread worker_;
    HINTERNET session_ = nullptr, connect_ = nullptr, request_ = nullptr;
    bool cancelled_ = false, done_ = false;
    std::optional<std::string> body_;
    HWND window_ = nullptr;
    UINT message_ = 0;
};

} // namespace oxo::win

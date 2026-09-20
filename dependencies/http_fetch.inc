#ifndef AIP_DEPENDENCIES_HTTP_FETCH_INC
#define AIP_DEPENDENCIES_HTTP_FETCH_INC

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cwctype>
#include <cwchar>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#ifdef AIP_HTTP_FETCH_TESTING
#include <atomic>
#endif

namespace aip {
struct HttpFetchOptions {
    std::wstring url;
    std::wstring userAgent = L"AIProjects";
    std::wstring accept = L"*/*";
    std::vector<std::pair<std::wstring, std::wstring>> headers;
    std::chrono::milliseconds timeout{20000};
    size_t maximumBytes = 2u * 1024u * 1024u;
    bool useSystemProxy = true;
    // Evaluated on the calling worker only, never from a WinHTTP callback.
    std::function<bool()> canceled;
};
struct HttpFetchResponse {
    DWORD statusCode = 0;
    std::wstring contentType;
    std::vector<BYTE> body;
};

namespace http_detail {
using Clock = std::chrono::steady_clock;
inline bool BuildRequestHeaders(const HttpFetchOptions& options, std::wstring& headers, std::wstring& error)
{
    headers.clear();
    error.clear();
    if (options.accept.find_first_of(L"\r\n\0", 0, 3) != std::wstring::npos ||
        options.accept.size() > 65526 || options.headers.size() > 64)
    {
        error = L"HTTP headers have invalid syntax or exceed the supported size.";
        return false;
    }
    headers = L"Accept: " + options.accept + L"\r\n";
    for (const auto& item : options.headers)
    {
        const auto validName = [](wchar_t ch) {
            return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'a' && ch <= L'z') || (ch != L'\0' && std::wcschr(L"!#$%&'*+-.^_`|~", ch) != nullptr);
        };
        if (item.first.empty() || !std::all_of(item.first.begin(), item.first.end(), validName) ||
            std::any_of(item.second.begin(), item.second.end(), [](wchar_t ch) { return (ch < 0x20 && ch != L'\t') || ch == 0x7F; }) ||
            headers.size() > 65532 || item.first.size() > 65532 - headers.size() ||
            item.second.size() > 65532 - headers.size() - item.first.size())
        {
            // Never include header names/values in diagnostics: callers may
            // supply cookies or other authentication material.
            error = L"HTTP headers have invalid syntax or exceed the supported size.";
            headers.clear();
            return false;
        }
        headers += item.first + L": " + item.second + L"\r\n";
    }
    return true;
}
#ifdef AIP_HTTP_FETCH_TESTING
inline std::atomic<unsigned> liveCallbackStates{0};
#endif
struct Url {
    std::wstring host, target;
    INTERNET_PORT port = 0;
    bool secure = false;
};
inline bool ParseUrl(const std::wstring& input, Url& result, std::wstring& error)
{
    result = {};
    if (input.empty() || input.size() > 32768 || std::any_of(input.begin(), input.end(),
        [](wchar_t ch) { return ch == L'\0' || iswspace(ch) || ch < 0x20; }))
    {
        error = L"HTTP URL must be nonempty, at most 32768 characters, and contain no raw whitespace/control characters.";
        return false;
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(input.c_str(), static_cast<DWORD>(input.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        parts.dwHostNameLength == 0)
    {
        error = L"HTTP URL requires an http/https scheme and a host.";
        return false;
    }
    if (parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0)
    {
        error = L"HTTP URL user-info credentials are not supported by this downloader.";
        return false;
    }
    result.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    if (parts.dwUrlPathLength != 0) result.target.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (result.target.empty()) result.target = L"/";
    if (parts.dwExtraInfoLength != 0)
    {
        std::wstring extra(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        const auto fragment = extra.find(L'#');
        if (fragment != std::wstring::npos) extra.resize(fragment);
        if (!extra.empty() && extra.front() != L'?')
        {
            error = L"HTTP URL has an invalid query component.";
            return false;
        }
        result.target += extra;
    }
    result.port = parts.nPort;
    result.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}
struct Handle {
    HINTERNET value = nullptr;
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct State {
#ifdef AIP_HTTP_FETCH_TESTING
    State() { ++liveCallbackStates; }
    ~State() { --liveCallbackStates; }
#endif
    std::mutex mutex;
    std::condition_variable changed;
    DWORD completion = 0, error = 0, bytesRead = 0;
    bool closing = false;
    // An asynchronous read buffer belongs to callback state, not caller stack
    // storage. It survives a timed-out fetch until HANDLE_CLOSING.
    std::array<BYTE, 16384> buffer{};
};
struct CallbackLease { std::shared_ptr<State> state; };

inline void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void* information, DWORD length) noexcept
{
    if (!context) return;
    auto* lease = reinterpret_cast<CallbackLease*>(context);
    const auto state = lease->state;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        {
            state->error = information && length >= sizeof(WINHTTP_ASYNC_RESULT)
                ? static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError : ERROR_GEN_FAILURE;
            if (!state->error) state->error = ERROR_GEN_FAILURE;
        }
        else if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
            state->closing = true;
        else
        {
            state->completion = status;
            if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) state->bytesRead = length;
        }
    }
    state->changed.notify_all();
    // Microsoft defines HANDLE_CLOSING as the final callback for this handle.
    // Do not unregister the callback and prematurely free its context.
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) delete lease;
}
inline bool Continue(const HttpFetchOptions& options, Clock::time_point deadline, std::wstring& error)
{
    if (options.canceled && options.canceled())
    {
        error = L"HTTP request canceled.";
        return false;
    }
    if (Clock::now() >= deadline)
    {
        error = L"HTTP request exceeded its total timeout.";
        return false;
    }
    return true;
}
inline void Prepare(const std::shared_ptr<State>& state)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    state->completion = 0;
    state->bytesRead = 0;
}
inline bool Wait(const std::shared_ptr<State>& state, DWORD completion,
    const HttpFetchOptions& options, Clock::time_point deadline, std::wstring& error)
{
    for (;;)
    {
        if (!Continue(options, deadline, error)) return false;
        std::unique_lock<std::mutex> lock(state->mutex);
        if (state->error || state->closing)
        {
            error = L"HTTP asynchronous operation failed (WinHTTP " +
                std::to_wstring(state->error ? state->error : ERROR_OPERATION_ABORTED) + L").";
            return false;
        }
        if (state->completion == completion) return true;
        state->changed.wait_until(lock, (std::min)(deadline, Clock::now() + std::chrono::milliseconds(25)));
    }
}
inline bool Started(BOOL result, std::wstring& error)
{
    if (result) return true;
    const DWORD code = GetLastError();
    if (code == ERROR_IO_PENDING) return true;
    error = L"HTTP operation failed (WinHTTP " + std::to_wstring(code) + L").";
    return false;
}
inline std::wstring Header(HINTERNET request, DWORD header)
{
    DWORD size = 0;
    WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0 || size > 65536) return {};
    std::vector<wchar_t> buffer((size + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) return {};
    return std::wstring(buffer.data());
}
inline bool LengthExceeds(const std::wstring& length, size_t maximum)
{
    if (length.empty()) return false;
    size_t parsed = 0;
    for (const auto ch : length)
    {
        if (ch < L'0' || ch > L'9') return false;
        const auto digit = static_cast<size_t>(ch - L'0');
        if (parsed > maximum / 10 || (parsed == maximum / 10 && digit > maximum % 10)) return true;
        parsed = parsed * 10 + digit;
    }
    return false;
}
} // namespace http_detail

// Blocking worker-facing facade over ASYNCHRONOUS WinHTTP. Every pending
// network wait shares one monotonic deadline and checks cancellation. Only the
// caller issues request APIs; closing its async request cancels pending I/O.
inline bool FetchHttpBytes(const HttpFetchOptions& options, HttpFetchResponse& response, std::wstring& error)
{
    response = {};
    error.clear();
    try
    {
        using namespace http_detail;
        if (options.timeout.count() < 1 || options.timeout.count() > INT_MAX || options.maximumBytes == 0 ||
            options.userAgent.empty() || options.userAgent.find_first_of(L"\r\n\0", 0, 3) != std::wstring::npos ||
            options.accept.find_first_of(L"\r\n\0", 0, 3) != std::wstring::npos)
        {
            error = L"HTTP options require a positive finite timeout/byte limit and valid single-line headers.";
            return false;
        }
        const auto deadline = Clock::now() + options.timeout;
        std::wstring headers;
        if (!BuildRequestHeaders(options, headers, error)) return false;
        if (!Continue(options, deadline, error)) return false;
        Url url;
        if (!ParseUrl(options.url, url, error)) return false;
        auto state = std::make_shared<State>();
        Handle session, connection, request;
        session.value = WinHttpOpen(options.userAgent.c_str(), options.useSystemProxy
            ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        // Automatic-proxy mode was added in Windows 8.1. Preserve Windows 8
        // operation using that system's default-proxy mode when unavailable.
        if (!session.value && options.useSystemProxy && GetLastError() == ERROR_INVALID_PARAMETER)
            session.value = WinHttpOpen(options.userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (!session.value) return Started(FALSE, error);
        const auto timeout = static_cast<int>(options.timeout.count());
        if (!WinHttpSetTimeouts(session.value, timeout, timeout, timeout, timeout)) return Started(FALSE, error);
        connection.value = WinHttpConnect(session.value, url.host.c_str(), url.port, 0);
        if (!connection.value) return Started(FALSE, error);
        request.value = WinHttpOpenRequest(connection.value, L"GET", url.target.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0);
        if (!request.value) return Started(FALSE, error);
        const DWORD notifications = WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES;
        if (WinHttpSetStatusCallback(request.value, Callback, notifications, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
            return Started(FALSE, error);
        auto lease = std::unique_ptr<CallbackLease>(new CallbackLease{state});
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(lease.get());
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
            return Started(FALSE, error);
        lease.release(); // Request handle's final callback owns this reference.
        if (!WinHttpAddRequestHeaders(request.value, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            return Started(FALSE, error);
        if (!Continue(options, deadline, error)) return false;
        Prepare(state);
        if (!Started(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, context), error) ||
            !Wait(state, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, options, deadline, error)) return false;
        Prepare(state);
        if (!Started(WinHttpReceiveResponse(request.value, nullptr), error) ||
            !Wait(state, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, options, deadline, error)) return false;
        DWORD statusSize = sizeof(response.statusCode);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &response.statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) return Started(FALSE, error);
        if (response.statusCode < 200 || response.statusCode >= 300)
        {
            error = L"HTTP request failed with status " + std::to_wstring(response.statusCode) + L".";
            return false;
        }
        if (LengthExceeds(Header(request.value, WINHTTP_QUERY_CONTENT_LENGTH), options.maximumBytes))
        {
            error = L"HTTP response exceeded the configured byte limit.";
            return false;
        }
        HttpFetchResponse completed;
        completed.statusCode = response.statusCode;
        completed.contentType = Header(request.value, WINHTTP_QUERY_CONTENT_TYPE);
        for (;;)
        {
            if (!Continue(options, deadline, error)) return false;
            Prepare(state);
            if (!Started(WinHttpReadData(request.value, state->buffer.data(),
                    static_cast<DWORD>(state->buffer.size()), nullptr), error) ||
                !Wait(state, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, options, deadline, error)) return false;
            DWORD read = 0;
            { std::lock_guard<std::mutex> lock(state->mutex); read = state->bytesRead; }
            if (read == 0) break;
            if (read > state->buffer.size() || read > options.maximumBytes - completed.body.size())
            {
                error = L"HTTP response exceeded the configured byte limit.";
                return false;
            }
            completed.body.insert(completed.body.end(), state->buffer.begin(), state->buffer.begin() + read);
        }
        response = std::move(completed);
        return true;
    }
    catch (const std::exception&)
    {
        error = L"HTTP response could not be retrieved or buffered.";
    }
    catch (...)
    {
        error = L"HTTP request failed unexpectedly.";
    }
    response.body.clear();
    return false;
}
} // namespace aip
#endif

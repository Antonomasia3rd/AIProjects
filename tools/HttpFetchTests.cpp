#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define AIP_HTTP_FETCH_TESTING
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../dependencies/http_fetch.inc"
#include <atomic>
#include <iostream>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
int failures = 0;
void Check(bool passed, const char* name)
{
    std::cout << (passed ? "ok - " : "FAIL - ") << name << '\n';
    if (!passed) ++failures;
}
bool Send(SOCKET client, const std::string& data)
{
    size_t offset = 0;
    while (offset < data.size())
    {
        const int written = send(client, data.data() + offset, static_cast<int>(data.size() - offset), 0);
        if (written <= 0) return false;
        offset += written;
    }
    return true;
}
class Server {
    SOCKET listener_ = INVALID_SOCKET;
    std::atomic<bool> running_{true};
    std::thread thread_;
public:
    std::atomic<int> requests{0};
    unsigned short port = 0;
    using Handler = std::function<void(SOCKET, const std::string&, const std::atomic<bool>&)>;
    explicit Server(Handler handler)
    {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int size = sizeof(address);
        if (listener_ == INVALID_SOCKET || bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
            getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) || listen(listener_, SOMAXCONN))
            throw std::runtime_error("Could not bind a loopback HTTP fixture.");
        port = ntohs(address.sin_port);
        thread_ = std::thread([this, handler] {
            while (running_)
            {
                fd_set readers;
                FD_ZERO(&readers);
                FD_SET(listener_, &readers);
                timeval timeout{0, 25000};
                if (select(0, &readers, nullptr, nullptr, &timeout) <= 0) continue;
                SOCKET client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) continue;
                DWORD socketTimeout = 500;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&socketTimeout), sizeof(socketTimeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&socketTimeout), sizeof(socketTimeout));
                std::string request;
                char buffer[2048];
                while (running_ && request.size() < 65536 && request.find("\r\n\r\n") == std::string::npos)
                {
                    const int read = recv(client, buffer, sizeof(buffer), 0);
                    if (read <= 0) break;
                    request.append(buffer, read);
                }
                if (request.find("\r\n\r\n") != std::string::npos)
                {
                    ++requests;
                    const auto first = request.find(' ');
                    const auto last = request.find(' ', first + 1);
                    handler(client, request.substr(first + 1, last - first - 1), running_);
                }
                shutdown(client, SD_BOTH);
                closesocket(client);
            }
        });
    }
    ~Server()
    {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (listener_ != INVALID_SOCKET) closesocket(listener_);
    }
    std::wstring Url(const std::wstring& target = L"/") const
    { return L"http://127.0.0.1:" + std::to_wstring(port) + target; }
};

void Reply(SOCKET socket, const std::string& body, const char* code = "200 OK")
{
    Send(socket, std::string("HTTP/1.1 ") + code + "\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body);
}
void Trickle(SOCKET socket, const std::string&, const std::atomic<bool>& running)
{
    if (!Send(socket, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n")) return;
    for (int i = 0; i < 100 && running; ++i)
    {
        if (!Send(socket, "1\r\nx\r\n")) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    Send(socket, "0\r\n\r\n");
}
aip::HttpFetchOptions Options(const Server& server, const std::wstring& target = L"/")
{
    aip::HttpFetchOptions options;
    options.url = server.Url(target);
    options.useSystemProxy = false;
    options.timeout = std::chrono::milliseconds(1000);
    return options;
}

void TestUrls()
{
    aip::http_detail::Url url;
    std::wstring error;
    Check(aip::http_detail::ParseUrl(L"https://example.test?value=1#fragment", url, error) &&
        url.target == L"/?value=1" && url.secure && url.port == 443,
        "URL parsing keeps query parameters and excludes the client-only fragment");
    Check(!aip::http_detail::ParseUrl(L"file:///tmp/a", url, error) &&
        !aip::http_detail::ParseUrl(L"http://user:pass@example.test/", url, error) &&
        !aip::http_detail::ParseUrl(std::wstring(L"http://host/\0hidden", 19), url, error),
        "URL parsing rejects unsupported schemes, ignored credentials, and embedded NULs");
}
void TestResponses()
{
    Server server([](SOCKET socket, const std::string& target, const std::atomic<bool>&) {
        if (target == "/empty") Reply(socket, "", "204 No Content");
        else if (target == "/error") Reply(socket, "not found", "404 Not Found");
        else if (target == "/large-header") Send(socket,
            "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\nConnection: close\r\n\r\n");
        else if (target == "/large-chunk") Send(socket,
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n100\r\n" +
            std::string(256, 'x') + "\r\n0\r\n\r\n");
        else if (target == "/exact") Reply(socket, std::string(64, 'x'));
        else Reply(socket, target);
    });
    aip::HttpFetchResponse response;
    std::wstring error;
    auto options = Options(server, L"/feed?first=1&second=%2F#ignored");
    Check(aip::FetchHttpBytes(options, response, error) && response.statusCode == 200 &&
        std::string(response.body.begin(), response.body.end()) == "/feed?first=1&second=%2F" &&
        response.contentType.find(L"charset=utf-8") != std::wstring::npos,
        "loopback GET sends the exact query and returns charset metadata");
    options = Options(server, L"/" + std::wstring(4000, L'x') + L"?query=kept");
    Check(aip::FetchHttpBytes(options, response, error) && response.body.size() == 4012,
        "generic HTTP target parsing supports paths longer than the old fixed 2048 buffer");
    options = Options(server, L"/empty");
    Check(aip::FetchHttpBytes(options, response, error) && response.statusCode == 204 && response.body.empty(),
        "successful empty HTTP bodies remain a valid transport result");
    options = Options(server, L"/error");
    Check(!aip::FetchHttpBytes(options, response, error) && response.statusCode == 404 &&
        response.body.empty() && error.find(L"404") != std::wstring::npos,
        "HTTP error status is explicit and does not expose partial content as success");
    options = Options(server, L"/exact");
    options.maximumBytes = 64;
    Check(aip::FetchHttpBytes(options, response, error) && response.body.size() == 64,
        "an HTTP body exactly at the byte limit succeeds");
    options = Options(server, L"/large-header");
    options.maximumBytes = 64;
    Check(!aip::FetchHttpBytes(options, response, error) && response.body.empty(),
        "oversized or invalid Content-Length is rejected without body allocation");
    options = Options(server, L"/large-chunk");
    options.maximumBytes = 64;
    Check(!aip::FetchHttpBytes(options, response, error) && response.body.empty() && error.find(L"byte limit") != std::wstring::npos,
        "chunked HTTP bodies enforce the byte limit without Content-Length");
}
void TestTimeoutAndCancel()
{
    Server server(Trickle);
    auto options = Options(server);
    options.timeout = std::chrono::milliseconds(150);
    aip::HttpFetchResponse response;
    std::wstring error;
    auto started = Clock::now();
    Check(!aip::FetchHttpBytes(options, response, error) &&
        Clock::now() - started < std::chrono::seconds(1) && error.find(L"total timeout") != std::wstring::npos,
        "a trickling response cannot extend the total HTTP deadline");
    options.timeout = std::chrono::milliseconds(3000);
    started = Clock::now();
    options.canceled = [&started] { return Clock::now() - started >= std::chrono::milliseconds(100); };
    Check(!aip::FetchHttpBytes(options, response, error) &&
        Clock::now() - started < std::chrono::seconds(1) && error.find(L"canceled") != std::wstring::npos,
        "cooperative shutdown cancels an in-flight asynchronous response promptly");
    const int requestsBefore = server.requests;
    options.canceled = [] { return true; };
    Check(!aip::FetchHttpBytes(options, response, error) && server.requests == requestsBefore,
        "an already canceled request does not contact the server");

    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    options.canceled = {};
    options.timeout = std::chrono::milliseconds(35);
    bool allBounded = true;
    for (int i = 0; i < 30; ++i)
    {
        started = Clock::now();
        allBounded = !aip::FetchHttpBytes(options, response, error) &&
            Clock::now() - started < std::chrono::seconds(1) && allBounded;
    }
    // Allow the local server to observe disconnects and final callbacks to run.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    GetProcessHandleCount(GetCurrentProcess(), &after);
    Check(allBounded && after <= before + 64,
        "thirty timeout/late-callback cycles remain bounded without runaway handle growth");
    const auto releaseDeadline = Clock::now() + std::chrono::seconds(1);
    while (aip::http_detail::liveCallbackStates != 0 && Clock::now() < releaseDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Check(aip::http_detail::liveCallbackStates == 0,
        "HANDLE_CLOSING releases every asynchronous callback state and read buffer after cancellation");
}
void TestSlowHeaders()
{
    Server server([](SOCKET socket, const std::string&, const std::atomic<bool>& running) {
        for (int i = 0; i < 50 && running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (running) Reply(socket, "late");
    });
    auto options = Options(server);
    options.timeout = std::chrono::milliseconds(120);
    aip::HttpFetchResponse response;
    std::wstring error;
    const auto started = Clock::now();
    Check(!aip::FetchHttpBytes(options, response, error) &&
        Clock::now() - started < std::chrono::seconds(1) && response.body.empty(),
        "a server that delays headers is bounded by the same total HTTP deadline");
}
}
int main()
{
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) return 1;
    try { TestUrls(); TestResponses(); TestTimeoutAndCancel(); TestSlowHeaders(); }
    catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; ++failures; }
    WSACleanup();
    std::cout << (failures ? "HTTP fetch tests failed.\n" : "HTTP fetch tests passed.\n");
    return failures ? 1 : 0;
}

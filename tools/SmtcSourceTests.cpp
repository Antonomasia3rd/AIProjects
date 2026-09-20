#define NOMINMAX
#include "../dependencies/content_sources/smtc.inc"
#include <iostream>
#include <atomic>
#include <thread>

namespace {
int failures = 0;
std::atomic<int> successfulReads{0}, unavailableReads{0};
void Check(bool condition, const char* message)
{
    std::cout << (condition ? "ok - " : "FAIL - ") << message << '\n';
    if (!condition) ++failures;
}

struct FakeOperation {
    mutable bool canceled = false, waited = false;
    mutable std::chrono::milliseconds wait{0};
    winrt::Windows::Foundation::AsyncStatus status;
    bool completes = false;
    auto Status() const { return status; }
    auto wait_for(std::chrono::milliseconds duration) const {
        waited = true;
        wait = duration;
        return completes ? winrt::Windows::Foundation::AsyncStatus::Completed : status;
    }
    void Cancel() const { canceled = true; }
    int GetResults() const {
        if (status == winrt::Windows::Foundation::AsyncStatus::Error)
            throw winrt::hresult_error(E_ACCESSDENIED);
        return 42;
    }
};

void TestDeadline()
{
    using namespace aip::content::smtc_detail;
    using winrt::Windows::Foundation::AsyncStatus;
    FakeOperation pending{ false, false, std::chrono::milliseconds(0), AsyncStatus::Started };
    bool timedOut = false;
    try { Await(pending, Clock::now() + std::chrono::milliseconds(250)); }
    catch (const winrt::hresult_error& ex) { timedOut = ex.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
    Check(timedOut && pending.canceled && pending.waited && pending.wait.count() <= 250,
        "pending SMTC operations use their remaining deadline and request cancellation");

    FakeOperation expired{ false, false, std::chrono::milliseconds(0), AsyncStatus::Started };
    try { Await(expired, Clock::now() - std::chrono::milliseconds(1)); } catch (...) {}
    Check(expired.canceled && !expired.waited, "an expired SMTC deadline starts no additional wait");

    FakeOperation completed{ false, false, std::chrono::milliseconds(0), AsyncStatus::Completed };
    Check(Await(completed, Clock::now()) == 42 && !completed.waited && !completed.canceled,
        "completed SMTC operations read results without an unbounded get or second completion handler");

    FakeOperation denied{ false, false, std::chrono::milliseconds(0), AsyncStatus::Error };
    bool propagated = false;
    try { Await(denied, Clock::now()); }
    catch (const winrt::hresult_error& ex) { propagated = ex.code() == E_ACCESSDENIED; }
    Check(propagated && !denied.waited, "SMTC terminal errors retain their HRESULT");
}

void TestContract()
{
    using namespace aip::content;
    Text text{ L"stale", L"stale", L"stale" };
    std::wstring error;
    Check(!ReadSmtcContent(text, error, std::chrono::milliseconds(0)) &&
        text.primary.empty() && text.secondary.empty() && text.badge.empty() && !error.empty(),
        "invalid SMTC timeout clears the previous snapshot and reports a useful error");
    Check(!ReadSmtcContent(text, error, std::chrono::milliseconds(60001)),
        "SMTC rejects unbounded timeout requests");
    Check(smtc_detail::Clean(std::wstring(L"\0 artist ", 9)) == L"artist" &&
        smtc_detail::Clean(std::wstring(MaxText - 1, L'x') + L"\xD83D\xDE00").size() == MaxText - 1,
        "SMTC text is visible, bounded, and preserves complete Unicode pairs");
    Check(smtc_detail::SourceName(L"Vendor.Player_123!App") == L"Player" &&
        smtc_detail::SourceName(L"") == L"SMTC",
        "SMTC app identifiers become readable source names");

    HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    Check(SUCCEEDED(initialized), "SMTC test initializes an STA fixture");
    if (SUCCEEDED(initialized))
    {
        Check(!ReadSmtcContent(text, error) && error.find(L"multithreaded") != std::wstring::npos,
            "SMTC rejects UI-thread STA blocking before requesting media data");
        RoUninitialize();
    }
}

bool ReadOnlySmoke()
{
    aip::content::Text text;
    std::wstring error;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = aip::content::ReadSmtcContent(text, error, std::chrono::milliseconds(400));
    if (ok) ++successfulReads;
    else ++unavailableReads;
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // No playback, registration, file, tray, or configuration mutation occurs.
    // Do not print the user's song title/artist in a test log.
    const bool contract = ok
        ? !text.primary.empty() && !text.secondary.empty() && text.badge.empty() && error.empty()
        : text.primary.empty() && text.secondary.empty() && text.badge.empty() && !error.empty();
    return contract && elapsed < std::chrono::seconds(5) &&
        text.primary.size() <= aip::content::MaxText &&
        text.secondary.size() <= aip::content::MaxText &&
        text.badge.size() <= aip::content::MaxText;
}

void TestSystemReadOnly()
{
    Check(ReadOnlySmoke(), "read-only SMTC smoke returns media, idle, or a useful bounded error");
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    std::atomic<int> errors{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker)
        workers.emplace_back([&errors] {
            for (int iteration = 0; iteration < 5; ++iteration)
                if (!ReadOnlySmoke()) ++errors;
        });
    for (auto& worker : workers) worker.join();
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    Check(errors == 0, "twenty concurrent SMTC snapshot reads leave each result isolated");
    std::cout << "SMTC successful/idle snapshots: " << successfulReads.load() <<
        "; unavailable/error snapshots: " << unavailableReads.load() << '\n';
    Check(handlesAfter <= handlesBefore + 128,
        "SMTC concurrent smoke has no runaway handle growth (allows WinRT cache warmup)");
}
}

int main()
{
    TestDeadline();
    TestContract();
    TestSystemReadOnly();
    std::cout << (failures == 0 ? "SMTC source tests passed.\n" : "SMTC source tests failed.\n");
    return failures == 0 ? 0 : 1;
}

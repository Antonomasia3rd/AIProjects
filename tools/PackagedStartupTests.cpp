// Fake-only tests: NEVER query/mutate a real StartupTask or launch an app.
#define NOMINMAX
#include "../dependencies/desktop_app_baseline.h"
#include "../dependencies/startup_shortcut.inc"
#include "../dependencies/packaged_startup.inc"
#include "../dependencies/packaged_startup_manifest.h"
#include <cstdio>
#include <stdexcept>

static int checks = 0, failures = 0;
static void Check(bool ok, const char* label)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL - %s\n", label); }
}
struct FakeAsync
{
    using State = winrt::Windows::Foundation::AsyncStatus;
    State initial = State::Completed, afterWait = State::Completed;
    mutable int waits = 0, cancels = 0, results = 0;
    mutable long long timeoutMs = -1;
    bool failResults = false;
    State Status() const { return initial; }
    State wait_for(std::chrono::milliseconds timeout) const { ++waits; timeoutMs = timeout.count(); return afterWait; }
    void Cancel() const { ++cancels; }
    int GetResults() const { ++results; if (failResults) throw std::runtime_error("fake terminal error"); return 42; }
};
int main()
{
    FakeAsync complete;
    Check(aip::AwaitPackagedStartupOperation(complete) == 42 && complete.waits == 0 && complete.results == 1,
        "completed operation returns without waiting");
    FakeAsync pending;
    pending.initial = FakeAsync::State::Started;
    Check(aip::AwaitPackagedStartupOperation(pending) == 42 && pending.waits == 1 && pending.timeoutMs == 5000,
        "pending operation receives one finite default wait");
    FakeAsync timeout;
    timeout.initial = timeout.afterWait = FakeAsync::State::Started;
    bool timedOut = false;
    try { aip::AwaitPackagedStartupOperation(timeout, std::chrono::milliseconds(7)); }
    catch (const winrt::hresult_error& ex) { timedOut = ex.code().value == HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
    Check(timedOut && timeout.waits == 1 && timeout.cancels == 1 && timeout.results == 0 && timeout.timeoutMs == 7,
        "timeout cancels once and never reads unfinished results");
    FakeAsync noWait;
    noWait.initial = noWait.afterWait = FakeAsync::State::Started;
    try { aip::AwaitPackagedStartupOperation(noWait, std::chrono::milliseconds(0)); } catch (...) {}
    Check(noWait.waits == 0 && noWait.cancels == 1 && noWait.results == 0, "zero deadline does not block");
    FakeAsync failed;
    failed.initial = FakeAsync::State::Error; failed.failResults = true;
    bool propagated = false;
    try { aip::AwaitPackagedStartupOperation(failed); } catch (const std::runtime_error&) { propagated = true; }
    Check(propagated && failed.waits == 0 && failed.cancels == 0, "terminal errors propagate without waiting");
    using OsState = winrt::Windows::ApplicationModel::StartupTaskState;
    Check(!aip::DescribePackagedStartupState(OsState::DisabledByUser).enabled, "user-disabled state is not enabled");
    Check(!aip::DescribePackagedStartupState(OsState::DisabledByPolicy).enabled, "disabled policy is preserved");
    Check(aip::DescribePackagedStartupState(OsState::EnabledByPolicy).enabled, "enabled policy is reported");
    Check(aip::DescribePackagedStartupState(static_cast<OsState>(999)).state == aip::PackagedStartupState::Unknown,
        "unknown future enum does not report enabled");
    const auto xml = aip::BuildDesktopStartupExtension(L"task&name", L"app&name.exe", L"A \"quoted\" <name>",
        [](const std::wstring& s) { return aip::XmlEscape(s); });
    Check(xml.find(L"Enabled=\"false\"") != std::wstring::npos && xml.find(L"Enabled=\"true\"") == std::wstring::npos,
        "manifest creation always leaves task initially disabled");
    Check(xml.find(L"task&amp;name") != std::wstring::npos && xml.find(L"app&amp;name.exe") != std::wstring::npos &&
        xml.find(L"&quot;quoted&quot;") != std::wstring::npos && xml.find(L"&lt;name&gt;") != std::wstring::npos,
        "all manifest attributes use the host XML escaper");
    Check(xml.find(L"EntryPoint=\"Windows.FullTrustApplication\"") != std::wstring::npos &&
        xml.find(L"Category=\"windows.startupTask\"") != std::wstring::npos,
        "desktop manifest uses the expected extension contract");
    std::printf("Packaged startup fake tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

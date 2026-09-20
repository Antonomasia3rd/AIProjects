// Package-safe tests: scripts/XML are inspected only. Process tests spawn this
// same test executable as an inert stdout writer, NEVER PowerShell or an app.
#define NOMINMAX
#include "../dependencies/desktop_app_baseline.h"
#include "../dependencies/powershell_runner.inc"
#include "../dependencies/appx_registration_script.h"
#include "../dependencies/packaged_startup_manifest.h"
#include <cstdio>

static int checks = 0, failures = 0;
static void Check(bool ok, const char* label)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL - %s\n", label); }
}
int wmain(int argc, wchar_t** argv)
{
    if (argc > 1 && std::wstring(argv[1]) == L"-NoLogo")
    {
        // The runner passes PowerShell-style arguments to this fixture. They
        // are ignored; this process does nothing except write a fixed buffer.
        const char bytes[2048] = {};
        for (;;) {
            DWORD written = 0;
            if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes, sizeof(bytes), &written, nullptr)) return 0;
        }
    }
    const std::wstring paths[] = { L"C:\\tiles\\AppxManifest.xml", L"D:\\A $folder\\a`b\\O'Brien\\AppxManifest.xml",
        L"D:\\$(not-a-command);'x'\\AppxManifest.xml", L"D:\\Unicode-\x4e2d\x6587\\AppxManifest.xml" };
    for (const auto& path : paths) {
        const auto script = aip::BuildAppxRegistrationScript({path, true}, aip::PowerShellSingleQuotedString);
        Check(script == L"Add-AppxPackage -Register " + aip::PowerShellSingleQuotedString(path) +
            L" -ForceUpdateFromAnyVersion -ErrorAction Stop; ", "registration uses a literal path and terminating errors");
    }
    const auto normal = aip::BuildAppxRegistrationScript({paths[0], false}, aip::PowerShellSingleQuotedString);
    Check(normal.find(L"ForceUpdateFromAnyVersion") == std::wstring::npos, "spec can omit force-update option");
    bool rejected = false;
    try { aip::BuildAppxRegistrationScript({L"", true}, aip::PowerShellSingleQuotedString); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "empty manifest path is rejected before any execution");
    rejected = false;
    try { aip::BuildAppxRegistrationScript({std::wstring(L"x\0y", 3), true}, aip::PowerShellSingleQuotedString); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "embedded NUL is rejected before any execution");
    Check(aip::PowerShellSingleQuotedString(L"a'b$()`\"") == L"'a''b$()`\"'", "literal formatter only doubles apostrophes");
    for (const auto& task : { L"DesktopStubStartup", L"NowPlayingTileStartup" }) {
        const auto xml = aip::BuildDesktopStartupExtension(task, L"Tile&App.exe", L"Tile <Name>",
            [](const std::wstring& s) { return aip::XmlEscape(s); });
        Check(xml.find(task) != std::wstring::npos && xml.find(L"Enabled=\"false\"") != std::wstring::npos,
            "both consumer startup identities begin disabled");
        Check(xml.find(L"Tile&amp;App.exe") != std::wstring::npos && xml.find(L"Tile &lt;Name&gt;") != std::wstring::npos,
            "shared startup fragment escapes product data");
    }
    aip::PowerShellRunOptions options;
    options.powerShellExe = aip::GetCurrentExecutablePath();
    options.timeoutMs = 1000;
    options.pollMs = 1;
    options.terminateWaitMs = 1000;
    options.outputLimitBytes = 8192;
    auto capped = aip::RunPowerShellCaptured(L"inert fixture only", options);
    Check(capped.started && !capped.ok && capped.outputLimitExceeded && capped.exitCode == ERROR_FILE_TOO_LARGE,
        "continuous inert output stops at configured capture limit");
    Check(capped.terminateConfirmed, "output-limit child termination is confirmed");
    options.outputLimitBytes = 0;
    options.timeoutMs = 50;
    const auto began = GetTickCount64();
    auto timed = aip::RunPowerShellCaptured(L"inert fixture only", options);
    const auto elapsed = GetTickCount64() - began;
    Check(timed.started && !timed.ok && timed.timedOut && timed.exitCode == WAIT_TIMEOUT,
        "continuous output cannot starve deadline when capture is uncapped");
    Check(timed.terminateConfirmed && elapsed < 5000, "timeout child terminates within bounded test interval");
    std::printf("AppX sharing tests: %d checks, %d failures; flood timeout elapsed=%llu ms\n", checks, failures, elapsed);
    return failures == 0 ? 0 : 1;
}

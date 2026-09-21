// Compile the real host code with raw-hardware source providers omitted.
// This never calls the application entry point and performs no device, AppX,
// Startup, notification, playback, or network operation.
#define NOMINMAX
#include <windows.h>

static int contentDialogs = 0;
static int ContentTestMessageBox(HWND, LPCWSTR, LPCWSTR, UINT flags)
{
    ++contentDialogs;
    return (flags & MB_YESNO) ? IDNO : IDOK;
}

#define DESKTOPSTUB_CONTENT_MESSAGEBOX ContentTestMessageBox
#define DESKTOPSTUB_INERT_HARDWARE
#define DESKTOPSTUB_ENABLE_HARDWARE_SOURCES 0
#define wWinMain DesktopStubUnusedEntryPoint
#include "../DesktopStub.cpp"
#undef wWinMain

#include <iostream>

static int checks = 0, failures = 0;
static void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}

int wmain()
{
    aip::caps::Config caps;
    g_contentCaps.restart = true;
    g_contentCaps.Refresh(caps);
    const auto capsText = g_contentCaps.Read();
    Check(!g_contentCaps.restart.load() &&
            capsText.primary == L"Caps indicator" &&
            capsText.secondary.find(L"excludes optional hardware sources") != std::wstring::npos,
        "no-hardware Caps provider preserves the tray restart interface and reports its omission");

    aip::content::AsusConfiguration asus;
    g_contentAsus.restart = true;
    g_contentAsus.Refresh(asus);
    const auto asusText = g_contentAsus.Read();
    Check(!g_contentAsus.restart.load() &&
            asusText.primary == L"ASUS indicators" &&
            asusText.secondary.find(L"excludes optional hardware sources") != std::wstring::npos,
        "no-hardware ASUS provider preserves the tray restart interface and reports its omission");

    g_contentCaps.Cancel();
    g_contentAsus.Cancel();
    g_contentCaps.Stop();
    g_contentAsus.Stop();
    Check(contentDialogs == 0, "no-hardware provider checks do not request UI interaction");

    std::cout << checks << " no-hardware host checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}

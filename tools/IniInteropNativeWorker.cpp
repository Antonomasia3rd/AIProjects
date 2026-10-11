#include "../dependencies/desktop_app_baseline.h"
#include "../dependencies/startup_shortcut.inc"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

// Worker for the managed interoperability fixture. Write modes accept only
// its new temporary-directory prefix. No Startup folder/COM/registry actions.
namespace {
bool FixturePath(const std::wstring& path) {
    std::wstring full, temporary;
    if (!aip::named_objects::NormalizeFileIdentity(path, full)) return false;
    wchar_t buffer[32768]{};
    DWORD count = GetTempPathW(_countof(buffer), buffer);
    if (!count || count >= _countof(buffer) || !aip::named_objects::NormalizeFileIdentity(buffer, temporary)) return false;
    const auto directory = std::filesystem::path(full).parent_path();
    const std::wstring prefix = L"AIPROJECTS-INIINTEROP-";
    return directory.parent_path().wstring() == temporary && directory.filename().wstring().compare(0, prefix.size(), prefix) == 0;
}
int Run(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring mode = argv[1], path = argv[2];
    if (mode == L"--name") {
        std::wstring identity, name;
        if (!aip::named_objects::NormalizeFileIdentity(path, identity) || !aip::named_objects::IniMutationName(path, name)) return 1;
        aip::WriteCommandLineText(L"IDENTITY=" + identity + L"\nNAME=" + name, false); return 0;
    }
    if (mode == L"--startup-name") {
        const auto identity = aip::NormalizeShortcutComparisonPath(path);
        std::wstring name;
        if (!aip::named_objects::CurrentUserScopedName(L"StartupShortcut", identity, name)) return 1;
        aip::WriteCommandLineText(L"IDENTITY=" + identity + L"\nNAME=" + name, false); return 0;
    }
    if (mode == L"--hash") {
        std::wstring hash;
        if (!aip::named_objects::StableIdentityHash(path, hash)) return 1;
        aip::WriteCommandLineText(hash, false); return 0;
    }
    if (!FixturePath(path)) return 2;
    if (mode == L"--probe") {
        int wait = 0;
        if (argc != 4 || !aip::ParseIntValueInRange(argv[3], 0, 1500, wait)) return 2;
        aip::IniWriteMutexGuard scope(path, static_cast<DWORD>(wait));
        if (!scope.IsLocked()) { aip::WriteCommandLineText(L"LOCK_UNAVAILABLE=" + std::to_wstring(scope.Error()), false); return 3; }
        return 0;
    }
    if (mode == L"--hold") {
        aip::IniWriteMutexGuard scope(path, 1500);
        if (!scope.IsLocked()) return 3;
        aip::WriteCommandLineText(L"READY", false);
        char released = 0;
        if (!std::cin.get(released)) return 4;
        return 0;
    }
    if (mode == L"--nested") {
        aip::IniWriteMutexGuard outer(path, 1500), inner(path, 0);
        return outer.IsLocked() && inner.IsLocked() ? 0 : 3;
    }
    if (mode == L"--write") {
        int count = 0;
        if (argc != 4 || !aip::ParseIntValueInRange(argv[3], 1, 500, count)) return 2;
        aip::IniConfigStore store(path, L"", 1500);
        for (int index = 0; index < count; ++index) {
            if (!store.MutateFresh([](std::wstring& text) {
                const auto document = aip::ParseIniDocument(text);
                std::wstring raw; int value = 0;
                if (aip::ReadIniValueFromDoc(document, L"Native", L"Count", raw) && !aip::ParseIntValue(raw, value)) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return aip::WriteIniValueToText(text, L"Native", L"Count", std::to_wstring(value + 1));
            })) return 5;
        }
        return 0;
    }
    return 2;
}
}
int wmain(int argc, wchar_t** argv) {
    std::atomic<bool> finished{false};
    std::thread deadline([&] {
        for (int tick = 0; tick < 450 && !finished; ++tick) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!finished) { std::cerr << "Native INI interop worker exceeded 45 seconds.\n"; std::_Exit(124); }
    });
    int result = 1;
    try { result = Run(argc, argv); }
    catch (...) { result = 1; }
    finished = true; deadline.join(); return result;
}

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
bool SameExistingDirectory(const std::wstring& first,const std::wstring& second) {
    struct DirectoryHandle { HANDLE value=INVALID_HANDLE_VALUE; explicit DirectoryHandle(const std::wstring& path) {
        value=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    } ~DirectoryHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);} } left(first),right(second);
    BY_HANDLE_FILE_INFORMATION a{},b{};
    return left.value!=INVALID_HANDLE_VALUE&&right.value!=INVALID_HANDLE_VALUE&&
        GetFileInformationByHandle(left.value,&a)&&GetFileInformationByHandle(right.value,&b)&&
        (a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&(b.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&
        (a.nFileIndexHigh||a.nFileIndexLow)&&a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&
        a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
}
bool FixturePath(const std::wstring& path) {
    std::wstring full, temporary;
    if (!aip::named_objects::NormalizeFileIdentity(path, full)) return false;
    wchar_t buffer[32768]{};
    DWORD count = GetTempPathW(_countof(buffer), buffer);
    if (!count || count >= _countof(buffer) || !aip::named_objects::NormalizeFileIdentity(buffer, temporary)) return false;
    const auto directory = std::filesystem::path(full).parent_path();
    const std::wstring prefix = L"AIPROJECTS-INIINTEROP-";
    const auto leaf=directory.filename().wstring();
    const auto attributes=GetFileAttributesW(directory.c_str());
    // .NET expands 8.3 spellings on some serviced runners. Match the actual
    // trusted temp directory, retaining a strict ordinary GUID fixture leaf.
    const bool allowed=leaf.size()==prefix.size()+32&&leaf.compare(0,prefix.size(),prefix)==0&&
        std::all_of(leaf.begin()+prefix.size(),leaf.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'A'&&c<=L'F');})&&
        attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
        SameExistingDirectory(directory.parent_path().wstring(),temporary);
    if(!allowed)aip::WriteCommandLineText(L"Fixture path rejected: parent="+directory.parent_path().wstring()+L"; native-temp="+temporary+L"; fixture="+directory.filename().wstring(),true);
    return allowed;
}
int Run(int argc, wchar_t** argv) {
    if (argc < 3) { aip::WriteCommandLineText(L"Native fixture requires a mode and value; argc="+std::to_wstring(argc),true); return 2; }
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

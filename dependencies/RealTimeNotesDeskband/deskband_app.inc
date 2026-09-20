#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0700

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <comcat.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <stdint.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../desktop_app_baseline.h"
#include "../dpapi.inc"
#include "../content_sources/notes.inc"

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllRegisterServer,PRIVATE")
#pragma comment(linker, "/EXPORT:DllUnregisterServer,PRIVATE")
#define RTN_COM_EXPORT STDAPI
#else
#define RTN_COM_EXPORT extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE
#endif

// {8D1C2A67-39F0-497E-8D3C-0D27A4A41C1F}
static const CLSID CLSID_RealTimeNotesDeskband =
{ 0x8d1c2a67, 0x39f0, 0x497e, { 0x8d, 0x3c, 0x0d, 0x27, 0xa4, 0xa4, 0x1c, 0x1f } };

static const wchar_t kBandName[] = L"Real Time Notes";
static const wchar_t kClassName[] = L"RealTimeNotesDeskbandWindow";
static const wchar_t kSettingsKey[] = L"Software\\RealTimeNotesDeskband";
static const wchar_t kAccountsKey[] = L"Software\\RealTimeNotesDeskband\\Accounts";
static const wchar_t kClassesClsidKey[] = L"Software\\Classes\\CLSID";
static const wchar_t kDeskBandCatid[] = L"{00021492-0000-0000-C000-000000000046}";
static const wchar_t kConfigWindowClassName[] = L"RealTimeNotesDeskbandConfigWindow";
static const wchar_t kSettingsWindowClassName[] = L"RealTimeNotesDeskbandSettingsWindow";
static const UINT_PTR kRefreshTimer = 1;
static const UINT kStatusReadyMessage = WM_APP + 0x343;
static const UINT kRefreshRequestedMessage = kStatusReadyMessage + 1;
static const UINT kMinRefreshSeconds = 30;
static const UINT kDefaultRefreshSeconds = 300;
static const UINT kMaxRefreshSeconds = 24 * 60 * 60;

HMODULE g_module = nullptr;
long g_objectCount = 0;
long g_lockCount = 0;
LONG g_legacyMigrationAttempted = 0;
volatile LONG g_windowGeneration = 0;

using aip::notes::ResourceKind;
using aip::notes::StateKind;
using NoteState = aip::notes::Snapshot;
using AppConfig = aip::notes::Account;
struct ResourceMetadata
{
    ResourceKind resource;
    const wchar_t* registryValue;
    const wchar_t* displayName;
    const wchar_t* configFile;
    const wchar_t* fullIcon;
    const wchar_t* notFullIcon;
    const wchar_t* errorIcon;
};

static const ResourceMetadata kResources[] = {
    {
        ResourceKind::Resin,
        L"resin",
        L"Resin",
        L"genshin_cookie.json",
        L"genshin\\resin_full.ico",
        L"genshin\\resin_not_full.ico",
        L"genshin\\resin_error.ico"
    },
    {
        ResourceKind::Stamina,
        L"stamina",
        L"Stamina",
        L"hsr_cookie.json",
        L"hsr\\stamina_full.ico",
        L"hsr\\stamina_not_full.ico",
        L"hsr\\stamina_error.ico"
    },
    {
        ResourceKind::Charge,
        L"charge",
        L"Charge",
        L"zzz_cookie.json",
        L"zzz\\charge_full.ico",
        L"zzz\\charge_not_full.ico",
        L"zzz\\charge_error.ico"
    },
};

static const ResourceMetadata& MetadataFor(ResourceKind resource)
{
    for (const auto& item : kResources)
    {
        if (item.resource == resource)
        {
            return item;
        }
    }
    return kResources[0];
}

static std::wstring GuidToString(REFGUID guid)
{
    wchar_t buffer[64]{};
    StringFromGUID2(guid, buffer, ARRAYSIZE(buffer));
    return buffer;
}

static std::wstring ModulePath()
{
    std::vector<wchar_t> path(512);
    for (;;)
    {
        DWORD length = GetModuleFileNameW(
            g_module,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return L"";
        }
        if (length < path.size())
        {
            return std::wstring(path.data(), length);
        }
        if (path.size() >= 32768)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return L"";
        }
        path.resize((std::min)(path.size() * 2, static_cast<size_t>(32768)));
    }
}

static std::wstring ParentDirectoryOf(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return L".";
    }
    return path.substr(0, slash);
}

static std::wstring BaseNameWithoutExtension(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t start = slash == std::wstring::npos ? 0 : slash + 1;
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < start)
    {
        dot = path.size();
    }
    std::wstring name = path.substr(start, dot - start);
    return name.empty() ? L"RealTimeNotesDeskband" : name;
}

static bool FileExists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirectoryExists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
    {
        return right;
    }
    if (left.back() == L'\\' || left.back() == L'/')
    {
        return left + right;
    }
    return left + L"\\" + right;
}

static std::wstring ModuleLocalPath(const wchar_t* extension)
{
    std::wstring module = ModulePath();
    return JoinPath(ParentDirectoryOf(module), BaseNameWithoutExtension(module) + extension);
}

static std::wstring TrimWide(std::wstring value)
{
    auto isNotSpace = [](wchar_t ch) { return iswspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
    return value;
}

static std::wstring AppIniPath()
{
    return ModuleLocalPath(L".ini");
}

static bool TryReadIniString(const wchar_t* section, const wchar_t* key, std::wstring& value)
{
    std::wstring path = AppIniPath();
    if (!aip::IniHasKey(path, section, key))
    {
        value.clear();
        return false;
    }
    value = TrimWide(aip::IniConfigStore(path).ReadRaw(section, key, L""));
    return true;
}

static bool TryReadIniDword(const wchar_t* section, const wchar_t* key, DWORD& value)
{
    std::wstring raw;
    if (!TryReadIniString(section, key, raw) || raw.empty())
    {
        return false;
    }

    errno = 0;
    wchar_t* end = nullptr;
    unsigned long long parsed = wcstoull(raw.c_str(), &end, 10);
    if (end == raw.c_str() ||
        !end ||
        *end != L'\0' ||
        errno == ERANGE ||
        parsed > MAXDWORD)
    {
        return false;
    }

    value = static_cast<DWORD>(parsed);
    return true;
}

static DWORD ReadIniDwordValue(const wchar_t* section, const wchar_t* key, DWORD fallback)
{
    DWORD value = fallback;
    return TryReadIniDword(section, key, value) ? value : fallback;
}

static bool WriteIniStringValue(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    return aip::IniConfigStore(AppIniPath(), L"", 5000).WriteRaw(section, key, value);
}

static bool WriteIniDwordValue(const wchar_t* section, const wchar_t* key, DWORD value)
{
    return WriteIniStringValue(section, key, std::to_wstring(value));
}

static bool DeleteIniSection(const wchar_t* section)
{
    return aip::IniConfigStore(AppIniPath(), L"", 5000).MutateFresh(
        [&](std::wstring& text) {
            return aip::RemoveIniSectionFromText(text, section);
        });
}

static void LogLine(const std::wstring& message)
{
    if (ReadIniDwordValue(L"Settings", L"LoggingEnabled", 1) == 0)
    {
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[64];
    wsprintfW(timestamp, L"%04u-%02u-%02u %02u:%02u:%02u  ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    (void)aip::AppendUtf16LineToFile(
        ModuleLocalPath(L".log"),
        std::wstring(timestamp) + message,
        false,
        5000);
}

static void LogLineNoThrow(const wchar_t* message) noexcept
{
    try
    {
        LogLine(message ? message : L"Unknown error.");
    }
    catch (...)
    {
        OutputDebugStringW(L"RealTimeNotesDeskband: logging failed while handling an exception.\n");
    }
}

static bool OpenShellTarget(HWND owner, const std::wstring& target)
{
    HINSTANCE result = ShellExecuteW(
        owner,
        L"open",
        target.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32)
    {
        return true;
    }

    LogLine(L"Could not open shell target: " + target);
    return false;
}

static UINT ClampRefreshSeconds(UINT seconds)
{
    return std::max(kMinRefreshSeconds, std::min(kMaxRefreshSeconds, seconds));
}

static std::wstring WindowText(HWND hwnd)
{
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0)
    {
        return L"";
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, &text[0], length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

static void ApplyGuiFont(HWND hwnd)
{
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

static bool HasAnyConfigFile(const std::wstring& directory)
{
    for (const auto& item : kResources)
    {
        if (FileExists(JoinPath(directory, item.configFile)))
        {
            return true;
        }
    }
    return false;
}

static std::wstring DefaultConfigDir(const std::wstring& installDir)
{
    if (HasAnyConfigFile(installDir))
    {
        return installDir;
    }

    std::wstring parent = ParentDirectoryOf(installDir);
    if (parent != installDir && HasAnyConfigFile(parent))
    {
        return parent;
    }

    std::wstring localReference = JoinPath(installDir, L"references\\genshin-real-time-notes-0.0.8");
    if (HasAnyConfigFile(localReference))
    {
        return localReference;
    }

    std::wstring parentReference = JoinPath(parent, L"references\\genshin-real-time-notes-0.0.8");
    if (parent != installDir && HasAnyConfigFile(parentReference))
    {
        return parentReference;
    }

    return installDir;
}

static bool HasAnyAssetFile(const std::wstring& assetDir)
{
    for (const auto& item : kResources)
    {
        if (FileExists(JoinPath(assetDir, item.notFullIcon)) || FileExists(JoinPath(assetDir, item.fullIcon)))
        {
            return true;
        }
    }
    return false;
}

static std::wstring DefaultAssetDir(const std::wstring& installDir)
{
    std::wstring parent = ParentDirectoryOf(installDir);
    std::wstring localReferenceAssets = JoinPath(installDir, L"references\\genshin-real-time-notes-0.0.8\\embedded\\assets");
    if (HasAnyAssetFile(localReferenceAssets))
    {
        return localReferenceAssets;
    }

    std::wstring parentReferenceAssets = JoinPath(parent, L"references\\genshin-real-time-notes-0.0.8\\embedded\\assets");
    if (parent != installDir && HasAnyAssetFile(parentReferenceAssets))
    {
        return parentReferenceAssets;
    }

    return localReferenceAssets;
}

static std::wstring Utf8ToWide(const std::string& text)
{
    std::wstring wide;
    return aip::TryUtf8ToWide(text, wide) ? wide : L"";
}

static std::string WideToUtf8(const std::wstring& text)
{
    std::string utf8;
    return aip::TryWideToUtf8(text, utf8) ? utf8 : "";
}

static std::wstring ReadRegString(HKEY root, const wchar_t* subkey, const wchar_t* valueName, const std::wstring& fallback)
{
    DWORD type = 0;
    DWORD bytes = 0;
    LONG result = RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (result != ERROR_SUCCESS || bytes < sizeof(wchar_t))
    {
        return fallback;
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    result = RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, &type, &value[0], &bytes);
    if (result != ERROR_SUCCESS)
    {
        return fallback;
    }
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }
    return value.empty() ? fallback : value;
}

static DWORD ReadRegDword(HKEY root, const wchar_t* subkey, const wchar_t* valueName, DWORD fallback)
{
    DWORD value = fallback;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(root, subkey, valueName, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS)
    {
        return value;
    }
    return fallback;
}

static bool TryReadRegDword(HKEY root, const wchar_t* subkey, const wchar_t* valueName, DWORD& value)
{
    DWORD bytes = sizeof(value);
    return RegGetValueW(root, subkey, valueName, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS;
}

static std::wstring ProtectedValueName(const wchar_t* valueName)
{
    return std::wstring(valueName) + L"Protected";
}

static std::wstring ProtectSecretString(const std::wstring& value)
{
    if (value.empty())
    {
        return L"";
    }

    try
    {
        return aip::ProtectSecretForCurrentUser(value);
    }
    catch (const std::exception& ex)
    {
        LogLine(L"Could not protect an account secret with DPAPI: " + Utf8ToWide(ex.what()));
        return L"";
    }
}

static bool UnprotectSecretString(const std::wstring& value, std::wstring& plain)
{
    plain.clear();
    try
    {
        plain = aip::UnprotectSecretForCurrentUser(
            value,
            aip::DpapiLegacyEncoding::Utf16LittleEndian);
        return true;
    }
    catch (const std::exception& ex)
    {
        LogLine(L"Could not unprotect an account secret with DPAPI: " + Utf8ToWide(ex.what()));
        return false;
    }
}

static std::wstring AccountKeyFor(ResourceKind resource)
{
    return std::wstring(kAccountsKey) + L"\\" + MetadataFor(resource).registryValue;
}

static std::wstring AccountSectionFor(ResourceKind resource)
{
    return L"Account." + std::wstring(MetadataFor(resource).registryValue);
}

static std::wstring ReadSettingString(const wchar_t* valueName, const std::wstring& fallback)
{
    std::wstring value;
    if (TryReadIniString(L"Settings", valueName, value))
    {
        return value.empty() ? fallback : value;
    }
    return fallback;
}

static DWORD ReadSettingDword(const wchar_t* valueName, DWORD fallback)
{
    DWORD value = fallback;
    if (TryReadIniDword(L"Settings", valueName, value))
    {
        return value;
    }
    return fallback;
}

static bool TryReadSettingDword(const wchar_t* valueName, DWORD& value)
{
    return TryReadIniDword(L"Settings", valueName, value);
}

static bool WriteSettingString(const wchar_t* valueName, const std::wstring& value)
{
    return WriteIniStringValue(L"Settings", valueName, value);
}

static bool WriteSettingDword(const wchar_t* valueName, DWORD value)
{
    return WriteIniDwordValue(L"Settings", valueName, value);
}

static std::wstring ReadAccountString(ResourceKind resource, const wchar_t* valueName, const wchar_t* fallbackValueName = nullptr)
{
    std::wstring section = AccountSectionFor(resource);
    std::wstring value;
    if (TryReadIniString(section.c_str(), valueName, value) && !value.empty())
    {
        return value;
    }
    if (fallbackValueName && TryReadIniString(section.c_str(), fallbackValueName, value) && !value.empty())
    {
        return value;
    }

    return L"";
}

static std::wstring ReadProtectedAccountString(ResourceKind resource, const wchar_t* valueName, const wchar_t* fallbackValueName = nullptr)
{
    std::wstring protectedName = ProtectedValueName(valueName);
    std::wstring section = AccountSectionFor(resource);
    std::wstring protectedValue;
    std::wstring plain;
    if (TryReadIniString(section.c_str(), protectedName.c_str(), protectedValue) &&
        UnprotectSecretString(protectedValue, plain))
    {
        return plain;
    }

    return ReadAccountString(resource, valueName, fallbackValueName);
}

static bool KeepLegacyPlaintextSecrets(ResourceKind resource)
{
    (void)resource;
    DWORD configured = 0;
    if (TryReadSettingDword(L"KeepLegacyPlaintextSecrets", configured))
    {
        return configured != 0;
    }

    return false;
}

static bool HasAccountConfig(ResourceKind resource)
{
    return !ReadAccountString(resource, L"UID", L"uid").empty() &&
        !ReadProtectedAccountString(resource, L"LTokenV2", L"ltoken_v2").empty() &&
        !ReadProtectedAccountString(resource, L"LTuidV2", L"ltuid_v2").empty();
}

static bool SaveAccountConfig(ResourceKind resource, const AppConfig& cfg)
{
    std::wstring section = AccountSectionFor(resource);
    bool keepLegacyPlaintext = KeepLegacyPlaintextSecrets(resource);
    std::wstring uid = Utf8ToWide(cfg.uid);
    std::wstring ltoken = Utf8ToWide(cfg.ltoken);
    std::wstring ltuid = Utf8ToWide(cfg.ltuid);
    std::wstring protectedLtoken = ProtectSecretString(ltoken);
    std::wstring protectedLtuid = ProtectSecretString(ltuid);
    if (uid.empty() ||
        ltoken.empty() ||
        ltuid.empty() ||
        protectedLtoken.empty() ||
        protectedLtuid.empty())
    {
        return false;
    }

    return aip::IniConfigStore(AppIniPath(), L"", 5000).MutateFresh(
        [&](std::wstring& text) {
            if (!aip::WriteIniValueToText(text, section.c_str(), L"UID", uid) ||
                !aip::WriteIniValueToText(text, section.c_str(), L"LTokenV2Protected", protectedLtoken) ||
                !aip::WriteIniValueToText(text, section.c_str(), L"LTuidV2Protected", protectedLtuid))
            {
                return false;
            }

            if (keepLegacyPlaintext)
            {
                if (!aip::WriteIniValueToText(text, section.c_str(), L"LTokenV2", ltoken) ||
                    !aip::WriteIniValueToText(text, section.c_str(), L"LTuidV2", ltuid))
                {
                    return false;
                }
            }
            else if (!aip::RemoveIniValueFromText(text, section.c_str(), L"LTokenV2") ||
                !aip::RemoveIniValueFromText(text, section.c_str(), L"LTuidV2") ||
                !aip::RemoveIniValueFromText(text, section.c_str(), L"ltoken_v2") ||
                !aip::RemoveIniValueFromText(text, section.c_str(), L"ltuid_v2"))
            {
                return false;
            }

            return cfg.refreshSeconds > 0
                ? aip::WriteIniValueToText(
                    text,
                    section.c_str(),
                    L"RefreshIntervalSeconds",
                    std::to_wstring(cfg.refreshSeconds))
                : aip::RemoveIniValueFromText(
                    text,
                    section.c_str(),
                    L"RefreshIntervalSeconds");
        });
}

static bool DeleteAccountConfig(ResourceKind resource)
{
    std::wstring section = AccountSectionFor(resource);
    if (!DeleteIniSection(section.c_str()))
    {
        return false;
    }
    LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, AccountKeyFor(resource).c_str());
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

static std::wstring ReadIniAccountStringOnly(ResourceKind resource, const wchar_t* valueName, const wchar_t* fallbackValueName = nullptr)
{
    std::wstring section = AccountSectionFor(resource);
    std::wstring value;
    if (TryReadIniString(section.c_str(), valueName, value) && !value.empty())
    {
        return value;
    }
    if (fallbackValueName && TryReadIniString(section.c_str(), fallbackValueName, value) && !value.empty())
    {
        return value;
    }
    return L"";
}

static std::wstring ReadIniProtectedAccountStringOnly(ResourceKind resource, const wchar_t* valueName, const wchar_t* fallbackValueName = nullptr)
{
    std::wstring section = AccountSectionFor(resource);
    std::wstring protectedName = ProtectedValueName(valueName);
    std::wstring protectedValue;
    std::wstring plain;
    if (TryReadIniString(section.c_str(), protectedName.c_str(), protectedValue) &&
        UnprotectSecretString(protectedValue, plain))
    {
        return plain;
    }
    return ReadIniAccountStringOnly(resource, valueName, fallbackValueName);
}

static bool HasIniAccountConfig(ResourceKind resource)
{
    return !ReadIniAccountStringOnly(resource, L"UID", L"uid").empty() &&
        !ReadIniProtectedAccountStringOnly(resource, L"LTokenV2", L"ltoken_v2").empty() &&
        !ReadIniProtectedAccountStringOnly(resource, L"LTuidV2", L"ltuid_v2").empty();
}

static std::wstring ReadLegacyAccountString(ResourceKind resource, const wchar_t* valueName, const wchar_t* fallbackValueName = nullptr)
{
    std::wstring key = AccountKeyFor(resource);
    std::wstring value = ReadRegString(HKEY_CURRENT_USER, key.c_str(), valueName, L"");
    if (value.empty() && fallbackValueName)
    {
        value = ReadRegString(HKEY_CURRENT_USER, key.c_str(), fallbackValueName, L"");
    }
    return value;
}

static std::wstring ReadLegacyProtectedAccountString(ResourceKind resource, const wchar_t* valueName, const wchar_t* fallbackValueName = nullptr)
{
    std::wstring key = AccountKeyFor(resource);
    std::wstring protectedName = ProtectedValueName(valueName);
    std::wstring protectedValue = ReadRegString(HKEY_CURRENT_USER, key.c_str(), protectedName.c_str(), L"");
    std::wstring plain;
    if (UnprotectSecretString(protectedValue, plain))
    {
        return plain;
    }
    return ReadLegacyAccountString(resource, valueName, fallbackValueName);
}

static bool LoadLegacyAccountConfig(ResourceKind resource, AppConfig& cfg)
{
    cfg = AppConfig{};
    cfg.uid = WideToUtf8(ReadLegacyAccountString(resource, L"UID", L"uid"));
    cfg.ltoken = WideToUtf8(ReadLegacyProtectedAccountString(resource, L"LTokenV2", L"ltoken_v2"));
    cfg.ltuid = WideToUtf8(ReadLegacyProtectedAccountString(resource, L"LTuidV2", L"ltuid_v2"));

    std::wstring key = AccountKeyFor(resource);
    DWORD refresh = ReadRegDword(HKEY_CURRENT_USER, key.c_str(), L"RefreshIntervalSeconds", 0);
    if (refresh == 0)
    {
        refresh = ReadRegDword(HKEY_CURRENT_USER, key.c_str(), L"refresh_interval", 0);
    }
    if (refresh > 0)
    {
        cfg.refreshSeconds = refresh;
    }

    return !cfg.uid.empty() && !cfg.ltoken.empty() && !cfg.ltuid.empty();
}

static bool DeleteMigratedLegacyAccountKey(ResourceKind resource)
{
    LONG result = RegDeleteTreeW(
        HKEY_CURRENT_USER,
        AccountKeyFor(resource).c_str());
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

static void MigrateLegacySettingString(const wchar_t* valueName)
{
    std::wstring existing;
    if (TryReadIniString(L"Settings", valueName, existing))
    {
        return;
    }
    std::wstring legacy = ReadRegString(HKEY_CURRENT_USER, kSettingsKey, valueName, L"");
    if (!legacy.empty())
    {
        WriteSettingString(valueName, legacy);
    }
}

static void MigrateLegacySettingDword(const wchar_t* valueName)
{
    DWORD existing = 0;
    if (TryReadIniDword(L"Settings", valueName, existing))
    {
        return;
    }
    DWORD legacy = 0;
    if (TryReadRegDword(HKEY_CURRENT_USER, kSettingsKey, valueName, legacy))
    {
        WriteSettingDword(valueName, legacy);
    }
}

static void MigrateLegacyRegistryToIniOnce()
{
    if (InterlockedExchange(&g_legacyMigrationAttempted, 1) != 0)
    {
        return;
    }

    MigrateLegacySettingString(L"Resource");
    MigrateLegacySettingString(L"ConfigDir");
    MigrateLegacySettingString(L"AssetDir");
    MigrateLegacySettingString(L"InstallDir");
    MigrateLegacySettingDword(L"RefreshIntervalSeconds");
    MigrateLegacySettingDword(L"KeepLegacyPlaintextSecrets");

    for (const auto& item : kResources)
    {
        if (HasIniAccountConfig(item.resource))
        {
            if (!DeleteMigratedLegacyAccountKey(item.resource))
            {
                LogLine(std::wstring(L"Protected INI account is valid, but the legacy registry account key could not be removed for ") + item.displayName);
            }
            continue;
        }

        AppConfig cfg;
        if (LoadLegacyAccountConfig(item.resource, cfg))
        {
            if (SaveAccountConfig(item.resource, cfg))
            {
                if (HasIniAccountConfig(item.resource) &&
                    DeleteMigratedLegacyAccountKey(item.resource))
                {
                    LogLine(std::wstring(L"Migrated legacy registry account settings and removed the plaintext registry source for ") + item.displayName);
                }
                else
                {
                    LogLine(std::wstring(L"Protected INI migration succeeded, but legacy registry cleanup failed for ") + item.displayName);
                }
            }
            else
            {
                LogLine(std::wstring(L"Could not migrate legacy registry account settings for ") + item.displayName);
            }
        }
    }
}

static ResourceKind ResourceFromString(const std::wstring& value)
{
    std::wstring lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    for (const auto& item : kResources)
    {
        if (lower == item.registryValue)
        {
            return item.resource;
        }
    }
    return ResourceKind::Resin;
}

static bool TryNormalizeResourceSetting(const std::wstring& value, std::wstring& normalized)
{
    normalized = TrimWide(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    if (normalized == L"auto")
    {
        return true;
    }
    for (const auto& item : kResources)
    {
        if (normalized == item.registryValue)
        {
            return true;
        }
    }
    normalized.clear();
    return false;
}

static ResourceKind ReadResourceSetting(const std::wstring& configDir)
{
    MigrateLegacyRegistryToIniOnce();
    std::wstring value = ReadSettingString(L"Resource", L"auto");
    std::wstring lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    if (lower != L"auto")
    {
        return ResourceFromString(lower);
    }

    for (const auto& item : kResources)
    {
        if (HasAccountConfig(item.resource))
        {
            return item.resource;
        }
    }

    for (const auto& item : kResources)
    {
        if (FileExists(JoinPath(configDir, item.configFile)))
        {
            return item.resource;
        }
    }
    return ResourceKind::Resin;
}

static std::string ReadFileUtf8(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return "";
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024)
    {
        CloseHandle(file);
        return "";
    }

    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size())
    {
        DWORD read = 0;
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 64 * 1024));
        if (!ReadFile(file, &data[offset], chunk, &read, nullptr) || read == 0)
        {
            ok = false;
            break;
        }
        offset += read;
    }
    CloseHandle(file);
    if (!ok)
    {
        return "";
    }
    return data;
}

using aip::notes::detail::JsonString;
using aip::notes::detail::JsonInt;

static bool LoadConfig(const std::wstring& path, AppConfig& cfg)
{
    cfg = AppConfig{};
    std::string json = ReadFileUtf8(path);
    if (json.empty())
    {
        return false;
    }

    JsonString(json, "uid", cfg.uid);
    JsonString(json, "ltoken_v2", cfg.ltoken);
    JsonString(json, "ltuid_v2", cfg.ltuid);

    int refresh = 0;
    if (JsonInt(json, "refresh_interval", refresh) && refresh > 0)
    {
        cfg.refreshSeconds = static_cast<UINT>(refresh);
    }

    return !cfg.uid.empty() && !cfg.ltoken.empty() && !cfg.ltuid.empty();
}

static bool LoadAccountConfig(ResourceKind resource, AppConfig& cfg)
{
    cfg = AppConfig{};
    std::wstring uid = ReadAccountString(resource, L"UID", L"uid");
    std::wstring ltoken = ReadProtectedAccountString(resource, L"LTokenV2", L"ltoken_v2");
    std::wstring ltuid = ReadProtectedAccountString(resource, L"LTuidV2", L"ltuid_v2");

    cfg.uid = WideToUtf8(uid);
    cfg.ltoken = WideToUtf8(ltoken);
    cfg.ltuid = WideToUtf8(ltuid);

    std::wstring section = AccountSectionFor(resource);
    DWORD refresh = ReadIniDwordValue(section.c_str(), L"RefreshIntervalSeconds", 0);
    if (refresh == 0)
    {
        refresh = ReadIniDwordValue(section.c_str(), L"refresh_interval", 0);
    }
    if (refresh > 0)
    {
        cfg.refreshSeconds = refresh;
    }

    return !cfg.uid.empty() && !cfg.ltoken.empty() && !cfg.ltuid.empty();
}

static UINT EffectiveRefreshSeconds(UINT configRefresh)
{
    DWORD configuredRefresh = ReadSettingDword(L"RefreshIntervalSeconds", 0);
    return ClampRefreshSeconds(configuredRefresh > 0 ? configuredRefresh : configRefresh);
}

static NoteState FetchState(const std::wstring& configDir, const std::function<bool()>& canceled = {})
{
    const ResourceKind resource = ReadResourceSetting(configDir);
    const auto& metadata = MetadataFor(resource);
    AppConfig account;
    if (!LoadAccountConfig(resource, account) && !LoadConfig(JoinPath(configDir, metadata.configFile), account))
    {
        const auto state = aip::notes::MakeError(resource, L"Login needed", L"Configure account", EffectiveRefreshSeconds(kDefaultRefreshSeconds));
        LogLine(state.tooltip);
        return state;
    }
    aip::notes::Configuration configuration;
    static_cast<aip::notes::Account&>(configuration) = std::move(account);
    configuration.resource = resource;
    configuration.refreshSeconds = EffectiveRefreshSeconds(configuration.refreshSeconds);
    configuration.canceled = canceled;
    auto state = aip::notes::Fetch(configuration);
    if (state.state == StateKind::Error) LogLine(state.tooltip);
    return state;
}
static bool ShouldUseLightTaskbarText()
{
    DWORD colorPrevalence = ReadRegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"ColorPrevalence", 0);
    if (colorPrevalence)
    {
        return true;
    }

    DWORD systemUsesLightTheme = ReadRegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", 1);
    return systemUsesLightTheme == 0;
}

static HFONT CreateBandFont(int dpi)
{
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        metrics.lfMessageFont.lfWeight = FW_NORMAL;
        metrics.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    return CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static bool BrowseForFileSystemPath(
    HWND owner,
    const wchar_t* title,
    bool pickFolder,
    const COMDLG_FILTERSPEC* filters,
    UINT filterCount,
    const wchar_t* defaultExtension,
    std::wstring& out)
{
    struct ComApartment
    {
        bool uninitialize = false;
        ~ComApartment()
        {
            if (uninitialize) {
                CoUninitialize();
            }
        }
    } apartment;

    try {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        apartment.uninitialize = SUCCEEDED(init);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
            return false;
        }

        IFileOpenDialog* rawDialog = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&rawDialog));
        if (FAILED(hr) || !rawDialog) {
            return false;
        }
        auto releaseDialog = [](IFileOpenDialog* value) {
            if (value) value->Release();
        };
        std::unique_ptr<IFileOpenDialog, decltype(releaseDialog)> dialog(rawDialog, releaseDialog);

        FILEOPENDIALOGOPTIONS options = {};
        if (FAILED(dialog->GetOptions(&options))) {
            return false;
        }
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        options |= pickFolder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
        if (FAILED(dialog->SetOptions(options)) ||
            (title && FAILED(dialog->SetTitle(title))) ||
            (filterCount != 0 && filters && FAILED(dialog->SetFileTypes(filterCount, filters))) ||
            (defaultExtension && FAILED(dialog->SetDefaultExtension(defaultExtension))) ||
            FAILED(dialog->Show(owner))) {
            return false;
        }

        IShellItem* rawItem = nullptr;
        if (FAILED(dialog->GetResult(&rawItem)) || !rawItem) {
            return false;
        }
        auto releaseItem = [](IShellItem* value) {
            if (value) value->Release();
        };
        std::unique_ptr<IShellItem, decltype(releaseItem)> item(rawItem, releaseItem);

        PWSTR rawPath = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || !rawPath) {
            return false;
        }
        auto freePath = [](wchar_t* value) {
            CoTaskMemFree(value);
        };
        std::unique_ptr<wchar_t, decltype(freePath)> path(rawPath, freePath);
        out.assign(path.get());
        return !out.empty();
    }
    catch (...) {
        LogLineNoThrow(L"File-system picker failed with a C++ exception.");
        return false;
    }
}

static bool BrowseForFolder(HWND owner, const wchar_t* title, std::wstring& out)
{
    return BrowseForFileSystemPath(owner, title, true, nullptr, 0, nullptr, out);
}

static bool OpenCookieJson(HWND owner, ResourceKind resource, AppConfig& cfg)
{
    const COMDLG_FILTERSPEC filters[] = {
        { L"Cookie JSON (*.json)", L"*.json" },
        { L"All files (*.*)", L"*.*" }
    };
    std::wstring fileName;
    if (!BrowseForFileSystemPath(
            owner,
            L"Import HoYoLAB cookie JSON",
            false,
            filters,
            ARRAYSIZE(filters),
            L"json",
            fileName)) {
        return false;
    }

    AppConfig imported;
    if (!LoadConfig(fileName, imported))
    {
        MessageBoxW(owner, L"The selected JSON does not contain uid, ltoken_v2, and ltuid_v2.", L"Import Cookie JSON", MB_OK | MB_ICONERROR);
        return false;
    }

    cfg = imported;
    if (!SaveAccountConfig(resource, cfg))
    {
        MessageBoxW(owner, L"The account could not be saved. Check the deskband log and INI file permissions.", L"Import Cookie JSON", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

struct AccountDialogState
{
    HWND hwnd = nullptr;
    HWND comboResource = nullptr;
    HWND editUid = nullptr;
    HWND editLToken = nullptr;
    HWND editLTuid = nullptr;
    HWND editRefresh = nullptr;
    ResourceKind initialResource = ResourceKind::Resin;
    bool selectAfterSave = true;
    bool saved = false;
};

static int DpiScale(HWND hwnd, int value)
{
    HDC hdc = GetDC(hwnd);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc)
    {
        ReleaseDC(hwnd, hdc);
    }
    return MulDiv(value, dpi > 0 ? dpi : 96, 96);
}

static void SetAccountDialogFields(AccountDialogState* state, ResourceKind resource)
{
    AppConfig cfg;
    std::wstring installDir = ReadSettingString(L"InstallDir", ParentDirectoryOf(ModulePath()));
    std::wstring configDir = ReadSettingString(L"ConfigDir", DefaultConfigDir(installDir));
    if (!LoadAccountConfig(resource, cfg))
    {
        LoadConfig(JoinPath(configDir, MetadataFor(resource).configFile), cfg);
    }

    SetWindowTextW(state->editUid, Utf8ToWide(cfg.uid).c_str());
    SetWindowTextW(state->editLToken, Utf8ToWide(cfg.ltoken).c_str());
    SetWindowTextW(state->editLTuid, Utf8ToWide(cfg.ltuid).c_str());
    std::wstring refresh = cfg.refreshSeconds > 0 ? std::to_wstring(ClampRefreshSeconds(cfg.refreshSeconds)) : L"";
    SetWindowTextW(state->editRefresh, refresh.c_str());
}

static ResourceKind AccountDialogSelectedResource(AccountDialogState* state)
{
    LRESULT selection = SendMessageW(state->comboResource, CB_GETCURSEL, 0, 0);
    if (selection >= 0 && selection < static_cast<LRESULT>(ARRAYSIZE(kResources)))
    {
        return kResources[selection].resource;
    }
    return state->initialResource;
}

static bool ParseRefreshEdit(HWND owner, const std::wstring& text, UINT& refresh)
{
    std::wstring trimmed = TrimWide(text);
    refresh = 0;
    if (trimmed.empty())
    {
        return true;
    }

    errno = 0;
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(trimmed.c_str(), &end, 10);
    while (end && *end && iswspace(*end) != 0)
    {
        ++end;
    }
    if (end == trimmed.c_str() ||
        (end && *end) ||
        errno == ERANGE ||
        parsed < kMinRefreshSeconds ||
        parsed > kMaxRefreshSeconds)
    {
        MessageBoxW(owner, L"Refresh interval must be blank or a number from 30 to 86400 seconds.", L"Configure Real-Time Notes", MB_OK | MB_ICONWARNING);
        return false;
    }

    refresh = static_cast<UINT>(parsed);
    return true;
}

static bool SaveAccountDialog(AccountDialogState* state)
{
    ResourceKind resource = AccountDialogSelectedResource(state);
    std::wstring uid = TrimWide(WindowText(state->editUid));
    std::wstring ltoken = TrimWide(WindowText(state->editLToken));
    std::wstring ltuid = TrimWide(WindowText(state->editLTuid));

    if (uid.empty() || ltoken.empty() || ltuid.empty())
    {
        MessageBoxW(state->hwnd, L"UID, ltoken_v2, and ltuid_v2 are required.", L"Configure Account", MB_OK | MB_ICONWARNING);
        return false;
    }

    UINT refresh = 0;
    if (!ParseRefreshEdit(state->hwnd, WindowText(state->editRefresh), refresh))
    {
        return false;
    }

    AppConfig cfg;
    cfg.uid = WideToUtf8(uid);
    cfg.ltoken = WideToUtf8(ltoken);
    cfg.ltuid = WideToUtf8(ltuid);
    cfg.refreshSeconds = refresh;
    if (cfg.uid.empty() || cfg.ltoken.empty() || cfg.ltuid.empty() ||
        !SaveAccountConfig(resource, cfg))
    {
        MessageBoxW(state->hwnd, L"The account could not be saved. Check the deskband log and INI file permissions.", L"Configure Account", MB_OK | MB_ICONERROR);
        return false;
    }
    if (state->selectAfterSave)
    {
        if (!WriteSettingString(L"Resource", MetadataFor(resource).registryValue))
        {
            MessageBoxW(state->hwnd, L"The account was saved, but the selected resource could not be updated.", L"Configure Account", MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    state->saved = true;
    DestroyWindow(state->hwnd);
    return true;
}

static HWND CreateDialogControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle, int x, int y, int w, int h, int id)
{
    HWND control = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        DpiScale(parent, x), DpiScale(parent, y), DpiScale(parent, w), DpiScale(parent, h),
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_module, nullptr);
    if (control)
    {
        ApplyGuiFont(control);
    }
    return control;
}

static bool CreateAccountDialogControls(AccountDialogState* state)
{
    HWND hwnd = state->hwnd;
    bool created = CreateDialogControl(hwnd, L"STATIC", L"Resource:", 0, 0, 14, 18, 95, 20, 0) != nullptr;
    state->comboResource = CreateDialogControl(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 118, 14, 290, 120, 101);
    created = state->comboResource != nullptr && created;
    int initialIndex = 0;
    for (int i = 0; i < static_cast<int>(ARRAYSIZE(kResources)); ++i)
    {
        SendMessageW(state->comboResource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kResources[i].displayName));
        if (kResources[i].resource == state->initialResource)
        {
            initialIndex = i;
        }
    }
    SendMessageW(state->comboResource, CB_SETCURSEL, initialIndex, 0);

    created = CreateDialogControl(hwnd, L"STATIC", L"UID:", 0, 0, 14, 52, 95, 20, 0) != nullptr && created;
    state->editUid = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 118, 48, 290, 24, 102);
    created = state->editUid != nullptr && created;
    created = CreateDialogControl(hwnd, L"STATIC", L"ltoken_v2:", 0, 0, 14, 84, 95, 20, 0) != nullptr && created;
    state->editLToken = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, WS_EX_CLIENTEDGE, 118, 80, 290, 24, 103);
    created = state->editLToken != nullptr && created;
    created = CreateDialogControl(hwnd, L"STATIC", L"ltuid_v2:", 0, 0, 14, 116, 95, 20, 0) != nullptr && created;
    state->editLTuid = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, WS_EX_CLIENTEDGE, 118, 112, 290, 24, 104);
    created = state->editLTuid != nullptr && created;
    created = CreateDialogControl(hwnd, L"STATIC", L"Refresh seconds:", 0, 0, 14, 148, 95, 20, 0) != nullptr && created;
    state->editRefresh = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 118, 144, 120, 24, 105);
    created = state->editRefresh != nullptr && created;

    created = CreateDialogControl(hwnd, L"BUTTON", L"Import JSON...", BS_PUSHBUTTON | WS_TABSTOP, 0, 14, 184, 116, 28, 106) != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Open HoYoLAB", BS_PUSHBUTTON | WS_TABSTOP, 0, 138, 184, 116, 28, 107) != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 262, 184, 70, 28, IDOK) != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, 338, 184, 70, 28, IDCANCEL) != nullptr && created;

    if (created)
    {
        SetAccountDialogFields(state, state->initialResource);
    }
    return created;
}

static LRESULT CALLBACK AccountDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    AccountDialogState* state = reinterpret_cast<AccountDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<AccountDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message)
    {
    case WM_CREATE:
        return state && CreateAccountDialogControls(state) ? 0 : -1;
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int notify = HIWORD(wParam);
        if (id == IDOK)
        {
            SaveAccountDialog(state);
            return 0;
        }
        if (id == IDCANCEL)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == 101 && notify == CBN_SELCHANGE)
        {
            SetAccountDialogFields(state, AccountDialogSelectedResource(state));
            return 0;
        }
        if (id == 106)
        {
            ResourceKind resource = AccountDialogSelectedResource(state);
            AppConfig cfg;
            if (OpenCookieJson(hwnd, resource, cfg))
            {
                SetWindowTextW(state->editUid, Utf8ToWide(cfg.uid).c_str());
                SetWindowTextW(state->editLToken, Utf8ToWide(cfg.ltoken).c_str());
                SetWindowTextW(state->editLTuid, Utf8ToWide(cfg.ltuid).c_str());
                SetWindowTextW(state->editRefresh, std::to_wstring(ClampRefreshSeconds(cfg.refreshSeconds)).c_str());
            }
            return 0;
        }
        if (id == 107)
        {
            if (!OpenShellTarget(hwnd, L"https://www.hoyolab.com/home"))
            {
                MessageBoxW(hwnd, L"Could not open the HoYoLAB sign-in page.", kBandName, MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static bool RegisterAccountDialogClass()
{
    static ATOM atom = 0;
    if (atom)
    {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = AccountDialogProc;
    wc.hInstance = g_module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kConfigWindowClassName;
    atom = RegisterClassW(&wc);
    if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        atom = 1;
    }
    return atom != 0;
}

static bool ShowAccountDialog(HWND owner, ResourceKind initialResource, bool selectAfterSave)
{
    if (!RegisterAccountDialogClass())
    {
        return false;
    }
    InitCommonControls();

    AccountDialogState state;
    state.initialResource = initialResource;
    state.selectAfterSave = selectAfterSave;

    HWND modalOwner = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kConfigWindowClassName,
        L"Configure Real-Time Notes Account",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        DpiScale(owner ? owner : GetDesktopWindow(), 440),
        DpiScale(owner ? owner : GetDesktopWindow(), 270),
        modalOwner,
        nullptr,
        g_module,
        &state);
    if (!hwnd)
    {
        return false;
    }

    bool ownerWasEnabled = false;
    if (modalOwner && IsWindowEnabled(modalOwner))
    {
        ownerWasEnabled = true;
        EnableWindow(modalOwner, FALSE);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    bool loopFailed = false;
    bool repostQuit = false;
    int quitCode = 0;
    while (IsWindow(hwnd))
    {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == -1)
        {
            loopFailed = true;
            LogLine(L"GetMessageW(account dialog) failed: " + std::to_wstring(GetLastError()));
            DestroyWindow(hwnd);
            break;
        }
        if (got == 0)
        {
            repostQuit = true;
            quitCode = static_cast<int>(msg.wParam);
            DestroyWindow(hwnd);
            break;
        }
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (ownerWasEnabled)
    {
        EnableWindow(modalOwner, TRUE);
        SetActiveWindow(modalOwner);
    }
    if (repostQuit)
    {
        PostQuitMessage(quitCode);
    }
    return state.saved && !loopFailed && !repostQuit;
}

struct GeneralSettingsProfile
{
    std::wstring resource = L"auto";
    UINT refreshSeconds = 0;
    bool loggingEnabled = true;
    bool keepLegacyPlaintextSecrets = false;
    std::wstring installDir;
    std::wstring configDir;
    std::wstring assetDir;
};

static GeneralSettingsProfile ReadGeneralSettingsProfile()
{
    GeneralSettingsProfile profile;
    profile.installDir = ReadSettingString(L"InstallDir", ParentDirectoryOf(ModulePath()));
    profile.configDir = ReadSettingString(L"ConfigDir", DefaultConfigDir(profile.installDir));
    profile.assetDir = ReadSettingString(L"AssetDir", DefaultAssetDir(profile.installDir));

    std::wstring configuredResource = ReadSettingString(L"Resource", L"auto");
    if (!TryNormalizeResourceSetting(configuredResource, profile.resource))
    {
        profile.resource = L"auto";
    }

    DWORD refresh = 0;
    if (TryReadSettingDword(L"RefreshIntervalSeconds", refresh) &&
        refresh >= kMinRefreshSeconds &&
        refresh <= kMaxRefreshSeconds)
    {
        profile.refreshSeconds = static_cast<UINT>(refresh);
    }
    profile.loggingEnabled = ReadSettingDword(L"LoggingEnabled", 1) != 0;
    profile.keepLegacyPlaintextSecrets = ReadSettingDword(L"KeepLegacyPlaintextSecrets", 0) != 0;
    return profile;
}

static bool SaveGeneralSettingsProfile(const GeneralSettingsProfile& profile)
{
    return aip::IniConfigStore(AppIniPath(), L"", 5000).MutateFresh(
        [&](std::wstring& text) {
            if (!aip::WriteIniValueToText(text, L"Settings", L"Resource", profile.resource) ||
                !aip::WriteIniValueToText(text, L"Settings", L"InstallDir", profile.installDir) ||
                !aip::WriteIniValueToText(text, L"Settings", L"ConfigDir", profile.configDir) ||
                !aip::WriteIniValueToText(text, L"Settings", L"AssetDir", profile.assetDir) ||
                !aip::WriteIniValueToText(text, L"Settings", L"LoggingEnabled", profile.loggingEnabled ? L"1" : L"0") ||
                !aip::WriteIniValueToText(text, L"Settings", L"KeepLegacyPlaintextSecrets", profile.keepLegacyPlaintextSecrets ? L"1" : L"0"))
            {
                return false;
            }

            return profile.refreshSeconds > 0
                ? aip::WriteIniValueToText(
                    text,
                    L"Settings",
                    L"RefreshIntervalSeconds",
                    std::to_wstring(profile.refreshSeconds))
                : aip::RemoveIniValueFromText(text, L"Settings", L"RefreshIntervalSeconds");
        });
}

struct SettingsDialogState
{
    HWND hwnd = nullptr;
    HWND comboResource = nullptr;
    HWND editRefresh = nullptr;
    HWND checkLogging = nullptr;
    HWND checkKeepLegacy = nullptr;
    HWND editInstallDir = nullptr;
    HWND editConfigDir = nullptr;
    HWND editAssetDir = nullptr;
    bool saved = false;
};

static int ResourceComboIndex(const std::wstring& resource)
{
    if (resource == L"auto")
    {
        return 0;
    }
    for (int i = 0; i < static_cast<int>(ARRAYSIZE(kResources)); ++i)
    {
        if (resource == kResources[i].registryValue)
        {
            return i + 1;
        }
    }
    return 0;
}

static std::wstring ResourceFromCombo(HWND combo)
{
    LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection > 0 && selection <= static_cast<LRESULT>(ARRAYSIZE(kResources)))
    {
        return kResources[selection - 1].registryValue;
    }
    return L"auto";
}

static ResourceKind AccountResourceFromSettingsDialog(SettingsDialogState* state)
{
    std::wstring resource = ResourceFromCombo(state->comboResource);
    if (resource != L"auto")
    {
        return ResourceFromString(resource);
    }
    return ReadResourceSetting(WindowText(state->editConfigDir));
}

static void PopulateSettingsDialog(SettingsDialogState* state)
{
    GeneralSettingsProfile profile = ReadGeneralSettingsProfile();
    SendMessageW(state->comboResource, CB_SETCURSEL, ResourceComboIndex(profile.resource), 0);
    SetWindowTextW(
        state->editRefresh,
        profile.refreshSeconds > 0 ? std::to_wstring(profile.refreshSeconds).c_str() : L"");
    SendMessageW(state->checkLogging, BM_SETCHECK, profile.loggingEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(state->checkKeepLegacy, BM_SETCHECK, profile.keepLegacyPlaintextSecrets ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(state->editInstallDir, profile.installDir.c_str());
    SetWindowTextW(state->editConfigDir, profile.configDir.c_str());
    SetWindowTextW(state->editAssetDir, profile.assetDir.c_str());
}

static bool SaveSettingsDialog(SettingsDialogState* state)
{
    GeneralSettingsProfile profile;
    profile.resource = ResourceFromCombo(state->comboResource);
    if (!ParseRefreshEdit(state->hwnd, WindowText(state->editRefresh), profile.refreshSeconds))
    {
        return false;
    }
    profile.loggingEnabled = SendMessageW(state->checkLogging, BM_GETCHECK, 0, 0) == BST_CHECKED;
    profile.keepLegacyPlaintextSecrets = SendMessageW(state->checkKeepLegacy, BM_GETCHECK, 0, 0) == BST_CHECKED;
    profile.installDir = TrimWide(WindowText(state->editInstallDir));
    profile.configDir = TrimWide(WindowText(state->editConfigDir));
    profile.assetDir = TrimWide(WindowText(state->editAssetDir));

    if (profile.installDir.empty() || profile.configDir.empty() || profile.assetDir.empty())
    {
        MessageBoxW(state->hwnd, L"Install, config, and asset directories cannot be blank.", L"Configure Real-Time Notes", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!DirectoryExists(profile.installDir))
    {
        MessageBoxW(state->hwnd, L"The install directory does not exist.", L"Configure Real-Time Notes", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!SaveGeneralSettingsProfile(profile))
    {
        MessageBoxW(state->hwnd, L"The settings could not be saved. Check the deskband log and INI file permissions.", L"Configure Real-Time Notes", MB_OK | MB_ICONERROR);
        return false;
    }

    state->saved = true;
    DestroyWindow(state->hwnd);
    return true;
}

static bool BrowseSettingsDirectory(SettingsDialogState* state, HWND edit, const wchar_t* title)
{
    std::wstring selected;
    if (!BrowseForFolder(state->hwnd, title, selected))
    {
        return false;
    }
    SetWindowTextW(edit, selected.c_str());
    return true;
}

static bool CreateSettingsDialogControls(SettingsDialogState* state)
{
    HWND hwnd = state->hwnd;
    bool created = CreateDialogControl(hwnd, L"STATIC", L"Resource:", 0, 0, 14, 18, 112, 20, 0) != nullptr;
    state->comboResource = CreateDialogControl(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 136, 14, 252, 150, 301);
    created = state->comboResource != nullptr && created;
    SendMessageW(state->comboResource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic"));
    for (const auto& item : kResources)
    {
        SendMessageW(state->comboResource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.displayName));
    }

    created = CreateDialogControl(hwnd, L"STATIC", L"Global refresh seconds:", 0, 0, 14, 52, 120, 20, 0) != nullptr && created;
    state->editRefresh = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 136, 48, 120, 24, 302);
    created = state->editRefresh != nullptr && created;
    created = CreateDialogControl(hwnd, L"STATIC", L"Blank uses the per-account/default interval (30-86400).", 0, 0, 266, 52, 330, 20, 0) != nullptr && created;

    state->checkLogging = CreateDialogControl(hwnd, L"BUTTON", L"Enable diagnostic logging", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 136, 82, 260, 22, 303);
    created = state->checkLogging != nullptr && created;
    state->checkKeepLegacy = CreateDialogControl(hwnd, L"BUTTON", L"Keep plaintext token copies for rollback compatibility", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 136, 108, 390, 22, 304);
    created = state->checkKeepLegacy != nullptr && created;

    created = CreateDialogControl(hwnd, L"STATIC", L"Install directory:", 0, 0, 14, 148, 112, 20, 0) != nullptr && created;
    state->editInstallDir = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 136, 144, 400, 24, 305);
    created = state->editInstallDir != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, 546, 143, 78, 26, 306) != nullptr && created;

    created = CreateDialogControl(hwnd, L"STATIC", L"Config directory:", 0, 0, 14, 184, 112, 20, 0) != nullptr && created;
    state->editConfigDir = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 136, 180, 400, 24, 307);
    created = state->editConfigDir != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, 546, 179, 78, 26, 308) != nullptr && created;

    created = CreateDialogControl(hwnd, L"STATIC", L"Asset directory:", 0, 0, 14, 220, 112, 20, 0) != nullptr && created;
    state->editAssetDir = CreateDialogControl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 136, 216, 400, 24, 309);
    created = state->editAssetDir != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, 546, 215, 78, 26, 310) != nullptr && created;

    created = CreateDialogControl(hwnd, L"BUTTON", L"Configure selected account...", BS_PUSHBUTTON | WS_TABSTOP, 0, 14, 270, 210, 28, 311) != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 472, 270, 72, 28, IDOK) != nullptr && created;
    created = CreateDialogControl(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, 552, 270, 72, 28, IDCANCEL) != nullptr && created;

    if (created)
    {
        PopulateSettingsDialog(state);
    }
    return created;
}

static LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    SettingsDialogState* state = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<SettingsDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message)
    {
    case WM_CREATE:
        return state && CreateSettingsDialogControls(state) ? 0 : -1;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            SaveSettingsDialog(state);
            return 0;
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        case 306:
            BrowseSettingsDirectory(state, state->editInstallDir, L"Select Real-Time Notes install directory");
            return 0;
        case 308:
            BrowseSettingsDirectory(state, state->editConfigDir, L"Select Real-Time Notes config directory");
            return 0;
        case 310:
            BrowseSettingsDirectory(state, state->editAssetDir, L"Select Real-Time Notes asset directory");
            return 0;
        case 311:
            ShowAccountDialog(hwnd, AccountResourceFromSettingsDialog(state), false);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static bool RegisterSettingsDialogClass()
{
    static ATOM atom = 0;
    if (atom)
    {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsDialogProc;
    wc.hInstance = g_module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsWindowClassName;
    atom = RegisterClassW(&wc);
    if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        atom = 1;
    }
    return atom != 0;
}

static bool ShowSettingsDialog(HWND owner)
{
    if (!RegisterSettingsDialogClass())
    {
        return false;
    }
    InitCommonControls();

    SettingsDialogState state;
    HWND modalOwner = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kSettingsWindowClassName,
        L"Configure Real-Time Notes",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        DpiScale(owner ? owner : GetDesktopWindow(), 654),
        DpiScale(owner ? owner : GetDesktopWindow(), 350),
        modalOwner,
        nullptr,
        g_module,
        &state);
    if (!hwnd)
    {
        return false;
    }

    bool ownerWasEnabled = false;
    if (modalOwner && IsWindowEnabled(modalOwner))
    {
        ownerWasEnabled = true;
        EnableWindow(modalOwner, FALSE);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    bool loopFailed = false;
    bool repostQuit = false;
    int quitCode = 0;
    while (IsWindow(hwnd))
    {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == -1)
        {
            loopFailed = true;
            break;
        }
        if (got == 0)
        {
            repostQuit = true;
            quitCode = static_cast<int>(msg.wParam);
            break;
        }
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (IsWindow(hwnd))
    {
        DestroyWindow(hwnd);
    }
    if (ownerWasEnabled && modalOwner && IsWindow(modalOwner))
    {
        EnableWindow(modalOwner, TRUE);
        SetActiveWindow(modalOwner);
    }
    if (repostQuit)
    {
        PostQuitMessage(quitCode);
    }
    return state.saved && !loopFailed && !repostQuit;
}

class RealTimeNotesBand final : public IDeskBand2, public IObjectWithSite, public IPersistStream, public IInputObject
{
public:
    RealTimeNotesBand() : m_ref(1)
    {
        InterlockedIncrement(&g_objectCount);
        InitializeCriticalSection(&m_stateLock);
        InitializeCriticalSection(&m_settingsLock);
        InitializeCriticalSection(&m_windowLock);
        ReloadSettings();
    }

    ~RealTimeNotesBand()
    {
        CloseDW(0);
        SetSite(nullptr);
        DeleteCriticalSection(&m_windowLock);
        DeleteCriticalSection(&m_settingsLock);
        DeleteCriticalSection(&m_stateLock);
        InterlockedDecrement(&g_objectCount);
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
        {
            return E_POINTER;
        }
        *ppv = nullptr;

        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDeskBand) || IsEqualIID(riid, IID_IDeskBand2) || IsEqualIID(riid, IID_IOleWindow) || IsEqualIID(riid, IID_IDockingWindow))
        {
            *ppv = static_cast<IDeskBand2*>(this);
        }
        else if (IsEqualIID(riid, IID_IObjectWithSite))
        {
            *ppv = static_cast<IObjectWithSite*>(this);
        }
        else if (IsEqualIID(riid, IID_IPersist) || IsEqualIID(riid, IID_IPersistStream))
        {
            *ppv = static_cast<IPersistStream*>(this);
        }
        else if (IsEqualIID(riid, IID_IInputObject))
        {
            *ppv = static_cast<IInputObject*>(this);
        }
        else
        {
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP GetWindow(HWND* hwnd) override
    {
        if (!hwnd)
        {
            return E_POINTER;
        }
        *hwnd = m_hwnd;
        return m_hwnd ? S_OK : E_FAIL;
    }

    IFACEMETHODIMP ContextSensitiveHelp(BOOL) override
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP ShowDW(BOOL show) override
    {
        if (m_hwnd)
        {
            ShowWindow(m_hwnd, show ? SW_SHOW : SW_HIDE);
        }
        return S_OK;
    }

    IFACEMETHODIMP CloseDW(DWORD) override
    {
        aip::CriticalSectionLock lock(m_windowLock);
        HWND hwnd = m_hwnd;
        if (hwnd)
        {
            KillTimer(hwnd, kRefreshTimer);
            if (m_tooltipHwnd)
            {
                DestroyWindow(m_tooltipHwnd);
                m_tooltipHwnd = nullptr;
            }
            DestroyWindow(hwnd);
            m_hwnd = nullptr;
            m_windowToken = 0;
        }
        return S_OK;
    }

    IFACEMETHODIMP ResizeBorderDW(LPCRECT, IUnknown*, BOOL) override
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetBandInfo(DWORD bandId, DWORD viewMode, DESKBANDINFO* info) override
    {
        if (!info)
        {
            return E_INVALIDARG;
        }

        m_bandId = bandId;
        m_viewMode = viewMode;
        SIZE desired = DesiredSize();

        if (info->dwMask & DBIM_MINSIZE)
        {
            info->ptMinSize.x = desired.cx;
            info->ptMinSize.y = desired.cy;
        }
        if (info->dwMask & DBIM_MAXSIZE)
        {
            info->ptMaxSize.x = desired.cx;
            info->ptMaxSize.y = desired.cy;
        }
        if (info->dwMask & DBIM_INTEGRAL)
        {
            info->ptIntegral.x = 1;
            info->ptIntegral.y = 1;
        }
        if (info->dwMask & DBIM_ACTUAL)
        {
            info->ptActual.x = desired.cx;
            info->ptActual.y = desired.cy;
        }
        if (info->dwMask & DBIM_TITLE)
        {
            std::wstring title = CurrentHoverText();
            lstrcpynW(info->wszTitle, title.c_str(), ARRAYSIZE(info->wszTitle));
        }
        if (info->dwMask & DBIM_MODEFLAGS)
        {
            info->dwModeFlags = DBIMF_NORMAL | DBIMF_VARIABLEHEIGHT;
        }
        if (info->dwMask & DBIM_BKCOLOR)
        {
            info->dwMask &= ~DBIM_BKCOLOR;
        }
        return S_OK;
    }

    IFACEMETHODIMP CanRenderComposited(BOOL* canRenderComposited) override
    {
        if (!canRenderComposited)
        {
            return E_POINTER;
        }
        *canRenderComposited = TRUE;
        return S_OK;
    }

    IFACEMETHODIMP SetCompositionState(BOOL compositionEnabled) override
    {
        m_compositionEnabled = compositionEnabled != FALSE;
        if (m_hwnd)
        {
            InvalidateRect(m_hwnd, nullptr, TRUE);
        }
        return S_OK;
    }

    IFACEMETHODIMP GetCompositionState(BOOL* compositionEnabled) override
    {
        if (!compositionEnabled)
        {
            return E_POINTER;
        }
        *compositionEnabled = m_compositionEnabled ? TRUE : FALSE;
        return S_OK;
    }

    IFACEMETHODIMP SetSite(IUnknown* site) override
    {
        if (!site)
        {
            CloseDW(0);
            if (m_inputSite)
            {
                m_inputSite->Release();
                m_inputSite = nullptr;
            }
            if (m_site)
            {
                m_site->Release();
                m_site = nullptr;
            }
            m_parent = nullptr;
            return S_OK;
        }

        IOleWindow* oleWindow = nullptr;
        HRESULT hr = site->QueryInterface(IID_IOleWindow, reinterpret_cast<void**>(&oleWindow));
        if (FAILED(hr))
        {
            return hr;
        }
        HWND newParent = nullptr;
        hr = oleWindow->GetWindow(&newParent);
        oleWindow->Release();
        if (FAILED(hr) || !newParent)
        {
            return FAILED(hr) ? hr : E_FAIL;
        }

        if (!RegisterWindowClass())
        {
            DWORD error = GetLastError();
            return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_CANNOT_MAKE : error);
        }

        IInputObjectSite* newInputSite = nullptr;
        site->QueryInterface(IID_IInputObjectSite, reinterpret_cast<void**>(&newInputSite));
        site->AddRef();

        CloseDW(0);
        if (m_inputSite)
        {
            m_inputSite->Release();
        }
        if (m_site)
        {
            m_site->Release();
        }
        m_site = site;
        m_inputSite = newInputSite;
        m_parent = newParent;

        m_hwnd = CreateWindowExW(
            0,
            kClassName,
            nullptr,
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            0,
            0,
            m_parent,
            nullptr,
            g_module,
            this);
        if (!m_hwnd)
        {
            DWORD error = GetLastError();
            if (m_inputSite)
            {
                m_inputSite->Release();
                m_inputSite = nullptr;
            }
            m_site->Release();
            m_site = nullptr;
            m_parent = nullptr;
            return HRESULT_FROM_WIN32(error);
        }

        if (!CreateTooltip())
        {
            LogLine(L"Could not create the deskband hover tooltip: " + std::to_wstring(GetLastError()));
        }

        BeginRefresh();
        return S_OK;
    }

    IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override
    {
        if (!ppv)
        {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (!m_site)
        {
            return E_FAIL;
        }
        return m_site->QueryInterface(riid, ppv);
    }

    IFACEMETHODIMP GetClassID(CLSID* clsid) override
    {
        if (!clsid)
        {
            return E_POINTER;
        }
        *clsid = CLSID_RealTimeNotesDeskband;
        return S_OK;
    }

    IFACEMETHODIMP IsDirty() override
    {
        return S_FALSE;
    }

    IFACEMETHODIMP Load(IStream*) override
    {
        return S_OK;
    }

    IFACEMETHODIMP Save(IStream*, BOOL) override
    {
        return S_OK;
    }

    IFACEMETHODIMP GetSizeMax(ULARGE_INTEGER* size) override
    {
        if (size)
        {
            size->QuadPart = 0;
        }
        return S_OK;
    }

    IFACEMETHODIMP UIActivateIO(BOOL activate, MSG*) override
    {
        if (activate && m_hwnd)
        {
            SetFocus(m_hwnd);
        }
        return S_OK;
    }

    IFACEMETHODIMP HasFocusIO() override
    {
        return m_hasFocus ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP TranslateAcceleratorIO(MSG*) override
    {
        return S_FALSE;
    }

private:
    static bool RegisterWindowClass()
    {
        static ATOM atom = 0;
        if (atom)
        {
            return true;
        }

        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = g_module;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        atom = RegisterClassW(&wc);
        if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        {
            atom = 1;
        }
        return atom != 0;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        RealTimeNotesBand* band = reinterpret_cast<RealTimeNotesBand*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            band = reinterpret_cast<RealTimeNotesBand*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(band));
            aip::CriticalSectionLock lock(band->m_windowLock);
            band->m_hwnd = hwnd;
            band->m_windowToken = static_cast<ULONG_PTR>(
                static_cast<ULONG>(InterlockedIncrement(&g_windowGeneration)));
        }

        if (!band)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_PAINT:
            band->OnPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wParam == kRefreshTimer)
            {
                band->BeginRefresh();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            band->BeginRefresh();
            return 0;
        case WM_CONTEXTMENU:
            band->ShowContextMenu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_NOTIFY:
            if (band->m_tooltipHwnd &&
                reinterpret_cast<NMHDR*>(lParam)->hwndFrom == band->m_tooltipHwnd &&
                reinterpret_cast<NMHDR*>(lParam)->code == TTN_GETDISPINFOW)
            {
                auto display = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                band->m_hoverText = band->CurrentHoverText();
                display->lpszText = const_cast<wchar_t*>(band->m_hoverText.c_str());
                return 0;
            }
            break;
        case WM_SETFOCUS:
            band->m_hasFocus = true;
            return 0;
        case WM_KILLFOCUS:
            band->m_hasFocus = false;
            return 0;
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case kStatusReadyMessage:
        {
            bool isCurrentWindow = false;
            {
                aip::CriticalSectionLock lock(band->m_windowLock);
                isCurrentWindow =
                    band->m_hwnd == hwnd &&
                    static_cast<ULONG_PTR>(wParam) == band->m_windowToken;
            }
            if (!isCurrentWindow)
            {
                return 0;
            }
            KillTimer(hwnd, kRefreshTimer);
            if (SetTimer(hwnd, kRefreshTimer, band->CurrentRefreshSeconds() * 1000, nullptr) == 0)
            {
                LogLine(L"Could not schedule the deskband refresh timer: " + std::to_wstring(GetLastError()));
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            band->NotifyBandInfoChanged();
            return 0;
        }
        case kRefreshRequestedMessage:
        {
            bool isCurrentWindow = false;
            {
                aip::CriticalSectionLock lock(band->m_windowLock);
                isCurrentWindow =
                    band->m_hwnd == hwnd &&
                    static_cast<ULONG_PTR>(wParam) == band->m_windowToken;
            }
            if (isCurrentWindow)
            {
                band->BeginRefresh();
            }
            return 0;
        }
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            {
                aip::CriticalSectionLock lock(band->m_windowLock);
                if (band->m_hwnd == hwnd)
                {
                    band->m_hwnd = nullptr;
                    band->m_windowToken = 0;
                }
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    struct BandSettings
    {
        std::wstring installDir;
        std::wstring configDir;
        std::wstring assetDir;
    };

    struct RefreshContext
    {
        RealTimeNotesBand* band;
        HWND hwnd;
        ULONG_PTR windowToken;
    };

    BandSettings CurrentSettings()
    {
        aip::CriticalSectionLock lock(m_settingsLock);
        BandSettings settings{ m_installDir, m_configDir, m_assetDir };
        return settings;
    }

    void BeginRefresh()
    {
        if (InterlockedCompareExchange(&m_refreshing, 1, 0) != 0)
        {
            return;
        }

        HWND hwnd = nullptr;
        ULONG_PTR windowToken = 0;
        {
            aip::CriticalSectionLock lock(m_windowLock);
            hwnd = m_hwnd;
            windowToken = m_windowToken;
        }
        if (!hwnd || windowToken == 0)
        {
            InterlockedExchange(&m_refreshing, 0);
            return;
        }

        auto context = new (std::nothrow) RefreshContext{ this, hwnd, windowToken };
        if (!context)
        {
            InterlockedExchange(&m_refreshing, 0);
            return;
        }

        AddRef();
        HANDLE thread = CreateThread(nullptr, 0, RefreshThread, context, 0, nullptr);
        if (!thread)
        {
            InterlockedExchange(&m_refreshing, 0);
            Release();
            delete context;
            return;
        }
        CloseHandle(thread);
    }

    static DWORD WINAPI RefreshThread(void* context)
    {
        auto refresh = static_cast<RefreshContext*>(context);
        auto band = refresh->band;
        bool published = false;
        try
        {
            BandSettings settings = band->CurrentSettings();
            NoteState state = FetchState(settings.configDir, [band, refresh] {
                aip::CriticalSectionLock lock(band->m_windowLock);
                return band->m_hwnd != refresh->hwnd || band->m_windowToken != refresh->windowToken;
            });

            aip::CriticalSectionLock windowLock(band->m_windowLock);
            if (band->m_hwnd == refresh->hwnd &&
                band->m_windowToken == refresh->windowToken &&
                IsWindow(refresh->hwnd))
            {
                {
                    aip::CriticalSectionLock stateLock(band->m_stateLock);
                    band->m_state = state;
                }
                published = PostMessageW(
                    refresh->hwnd,
                    kStatusReadyMessage,
                    static_cast<WPARAM>(refresh->windowToken),
                    0) != FALSE;
            }
        }
        catch (const std::exception&)
        {
            LogLineNoThrow(L"Deskband refresh worker failed with a C++ exception.");
        }
        catch (...)
        {
            LogLineNoThrow(L"Deskband refresh worker failed with an unknown exception.");
        }

        InterlockedExchange(&band->m_refreshing, 0);
        if (!published)
        {
            {
                aip::CriticalSectionLock lock(band->m_windowLock);
                if (band->m_hwnd &&
                    band->m_windowToken != 0 &&
                    (band->m_hwnd != refresh->hwnd || band->m_windowToken != refresh->windowToken) &&
                    !PostMessageW(
                        band->m_hwnd,
                        kRefreshRequestedMessage,
                        static_cast<WPARAM>(band->m_windowToken),
                        0))
                {
                    LogLineNoThrow(L"Could not request a refresh for the replacement deskband window.");
                }
            }
        }

        delete refresh;
        band->Release();
        return 0;
    }

    NoteState CurrentState()
    {
        aip::CriticalSectionLock lock(m_stateLock);
        NoteState copy = m_state;
        return copy;
    }

    std::wstring CurrentHoverText()
    {
        NoteState state = CurrentState();
        std::wstring text = TrimWide(state.tooltip);
        if (text.empty())
        {
            text = TrimWide(state.line1 + L" - " + state.line2);
        }
        return text.empty() ? std::wstring(kBandName) : text;
    }

    bool CreateTooltip()
    {
        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
        if (!InitCommonControlsEx(&controls))
        {
            return false;
        }

        m_tooltipHwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            m_hwnd,
            nullptr,
            g_module,
            nullptr);
        if (!m_tooltipHwnd)
        {
            return false;
        }

        SetWindowPos(m_tooltipHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(m_tooltipHwnd, TTM_SETMAXTIPWIDTH, 0, DpiScale(m_hwnd, 420));
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = m_hwnd;
        tool.uId = reinterpret_cast<UINT_PTR>(m_hwnd);
        tool.lpszText = LPSTR_TEXTCALLBACKW;
        if (!SendMessageW(m_tooltipHwnd, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)))
        {
            DWORD error = GetLastError();
            DestroyWindow(m_tooltipHwnd);
            m_tooltipHwnd = nullptr;
            SetLastError(error == ERROR_SUCCESS ? ERROR_CANNOT_MAKE : error);
            return false;
        }
        return true;
    }

    UINT CurrentRefreshSeconds()
    {
        NoteState state = CurrentState();
        return ClampRefreshSeconds(state.refreshSeconds);
    }

    int CurrentDpi() const
    {
        HDC hdc = m_hwnd ? GetDC(m_hwnd) : GetDC(nullptr);
        int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc)
        {
            ReleaseDC(m_hwnd, hdc);
        }
        return dpi > 0 ? dpi : 96;
    }

    SIZE DesiredSize()
    {
        NoteState state = CurrentState();
        int dpi = CurrentDpi();
        HDC hdc = GetDC(m_hwnd);
        if (!hdc)
        {
            return { MulDiv(132, dpi, 96), MulDiv(34, dpi, 96) };
        }

        HFONT font = CreateBandFont(dpi);
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, font));

        SIZE line1{};
        SIZE line2{};
        GetTextExtentPoint32W(hdc, state.line1.c_str(), static_cast<int>(state.line1.size()), &line1);
        GetTextExtentPoint32W(hdc, state.line2.c_str(), static_cast<int>(state.line2.size()), &line2);
        SelectObject(hdc, oldFont);
        DeleteObject(font);
        ReleaseDC(m_hwnd, hdc);

        int icon = MulDiv(24, dpi, 96);
        int margin = MulDiv(6, dpi, 96);
        int width = icon + margin * 4 + std::max(line1.cx, line2.cx);
        width = std::max(MulDiv(118, dpi, 96), width);
        int height = std::max(MulDiv(30, dpi, 96), icon + margin);
        return { width, height };
    }

    std::wstring IconPathFor(const NoteState& state)
    {
        BandSettings settings = CurrentSettings();
        const auto& metadata = MetadataFor(state.resource);
        std::wstring relative = metadata.notFullIcon;
        if (state.state == StateKind::Full)
        {
            relative = metadata.fullIcon;
        }
        else if (state.state == StateKind::Error)
        {
            relative = metadata.errorIcon;
        }

        std::wstring iconPath = JoinPath(settings.assetDir, relative);
        if (FileExists(iconPath))
        {
            return iconPath;
        }

        std::wstring legacyPath = JoinPath(settings.configDir, JoinPath(L"embedded\\assets", relative));
        if (FileExists(legacyPath))
        {
            return legacyPath;
        }

        legacyPath = JoinPath(settings.installDir, JoinPath(L"embedded\\assets", relative));
        if (FileExists(legacyPath))
        {
            return legacyPath;
        }

        return iconPath;
    }

    void DrawFallbackIcon(HDC hdc, const RECT& rc, const NoteState& state, int dpi)
    {
        COLORREF fill = RGB(73, 115, 220);
        if (state.resource == ResourceKind::Stamina)
        {
            fill = RGB(42, 120, 162);
        }
        else if (state.resource == ResourceKind::Charge)
        {
            fill = RGB(190, 130, 42);
        }
        if (state.state == StateKind::Error)
        {
            fill = RGB(192, 58, 58);
        }
        else if (state.state == StateKind::Full)
        {
            fill = RGB(54, 148, 80);
        }

        HBRUSH brush = CreateSolidBrush(fill);
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, brush));
        HPEN pen = CreatePen(PS_SOLID, MulDiv(1, dpi, 96), RGB(255, 255, 255));
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
        Ellipse(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);

        wchar_t label[2] = { L'R', 0 };
        if (state.state == StateKind::Error)
        {
            label[0] = L'!';
        }
        else if (state.resource == ResourceKind::Stamina)
        {
            label[0] = L'S';
        }
        else if (state.resource == ResourceKind::Charge)
        {
            label[0] = L'C';
        }

        HFONT font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, font));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT textRc = rc;
        DrawTextW(hdc, label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(font);
    }

    void OnPaint()
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        if (!hdc)
        {
            return;
        }

        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        if (FAILED(DrawThemeParentBackground(m_hwnd, hdc, &rc)))
        {
            FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        }

        NoteState state = CurrentState();
        int dpi = CurrentDpi();
        int margin = MulDiv(6, dpi, 96);
        int iconSize = MulDiv(24, dpi, 96);
        int iconY = rc.top + ((rc.bottom - rc.top) - iconSize) / 2;
        std::wstring iconPath = IconPathFor(state);
        HICON icon = reinterpret_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, iconSize, iconSize, LR_LOADFROMFILE));
        RECT iconRc{ rc.left + margin, iconY, rc.left + margin + iconSize, iconY + iconSize };
        if (icon)
        {
            DrawIconEx(hdc, iconRc.left, iconRc.top, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            DestroyIcon(icon);
        }
        else
        {
            DrawFallbackIcon(hdc, iconRc, state, dpi);
        }

        int textLeft = rc.left + margin * 2 + iconSize;
        RECT textRc{ textLeft, rc.top, rc.right - margin, rc.bottom };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ShouldUseLightTaskbarText() ? RGB(255, 255, 255) : RGB(32, 32, 32));

        HFONT font = CreateBandFont(dpi);
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, font));

        RECT line1Rc = textRc;
        line1Rc.bottom = rc.top + (rc.bottom - rc.top) / 2 + MulDiv(1, dpi, 96);
        DrawTextW(hdc, state.line1.c_str(), -1, &line1Rc, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT line2Rc = textRc;
        line2Rc.top = rc.top + (rc.bottom - rc.top) / 2 - MulDiv(1, dpi, 96);
        DrawTextW(hdc, state.line2.c_str(), -1, &line2Rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(hdc, oldFont);
        DeleteObject(font);
        EndPaint(m_hwnd, &ps);
    }

    void NotifyBandInfoChanged()
    {
        if (!m_site)
        {
            return;
        }

        IOleCommandTarget* commandTarget = nullptr;
        if (SUCCEEDED(m_site->QueryInterface(IID_IOleCommandTarget, reinterpret_cast<void**>(&commandTarget))))
        {
            VARIANTARG var{};
            VariantInit(&var);
            var.vt = VT_I4;
            var.lVal = static_cast<LONG>(m_bandId);
            commandTarget->Exec(&CGID_DeskBand, DBID_BANDINFOCHANGED, 0, &var, nullptr);
            commandTarget->Release();
        }
    }

    void ReloadSettings()
    {
        MigrateLegacyRegistryToIniOnce();
        std::wstring installDir = ReadSettingString(L"InstallDir", ParentDirectoryOf(ModulePath()));
        std::wstring configDir = ReadSettingString(L"ConfigDir", DefaultConfigDir(installDir));
        std::wstring assetDir = ReadSettingString(L"AssetDir", DefaultAssetDir(installDir));

        aip::CriticalSectionLock lock(m_settingsLock);
        m_installDir = installDir;
        m_configDir = configDir;
        m_assetDir = assetDir;
    }

    void SetLoadingState(ResourceKind resource, const wchar_t* detail)
    {
        UINT refreshSeconds = CurrentRefreshSeconds();
        {
            aip::CriticalSectionLock lock(m_stateLock);
            m_state.resource = resource;
            m_state.state = StateKind::Loading;
            m_state.line1 = L"Loading";
            m_state.line2 = detail && detail[0] ? detail : MetadataFor(resource).displayName;
            m_state.tooltip = L"Refreshing Real-Time Notes";
            m_state.menuLines.clear();
            m_state.refreshSeconds = refreshSeconds;
        }

        if (m_hwnd)
        {
            InvalidateRect(m_hwnd, nullptr, TRUE);
        }
        NotifyBandInfoChanged();
    }

    void BeginRefreshWithCurrentSettings()
    {
        ReloadSettings();
        BandSettings settings = CurrentSettings();
        ResourceKind resource = ReadResourceSetting(settings.configDir);
        SetLoadingState(resource, MetadataFor(resource).displayName);
        BeginRefresh();
    }

    void ConfigureAccount(ResourceKind resource)
    {
        if (ShowAccountDialog(m_hwnd, resource, true))
        {
            BeginRefreshWithCurrentSettings();
        }
    }

    void ImportCookie(ResourceKind resource)
    {
        AppConfig cfg;
        if (OpenCookieJson(m_hwnd, resource, cfg))
        {
            WriteSettingString(L"Resource", MetadataFor(resource).registryValue);
            BeginRefreshWithCurrentSettings();
        }
    }

    void ClearAccount(ResourceKind resource)
    {
        std::wstring message = L"Remove saved credentials for ";
        message += MetadataFor(resource).displayName;
        message += L"?";
        if (MessageBoxW(m_hwnd, message.c_str(), L"Clear Account", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
        {
            if (DeleteAccountConfig(resource))
            {
                BeginRefreshWithCurrentSettings();
            }
            else
            {
                MessageBoxW(m_hwnd, L"The saved account could not be removed.", L"Clear Account", MB_OK | MB_ICONERROR);
            }
        }
    }

    void OpenDirectory(const std::wstring& path)
    {
        if (!OpenShellTarget(m_hwnd, path))
        {
            MessageBoxW(m_hwnd, path.c_str(), L"Could not open directory", MB_OK | MB_ICONERROR);
        }
    }

    void ChangeDirectorySetting(const wchar_t* valueName, const wchar_t* title)
    {
        std::wstring directory;
        if (BrowseForFolder(m_hwnd, title, directory))
        {
            WriteSettingString(valueName, directory);
            BeginRefreshWithCurrentSettings();
        }
    }

    void ShowContextMenu(int x, int y)
    {
        if (x == -1 && y == -1)
        {
            RECT rc{};
            GetWindowRect(m_hwnd, &rc);
            x = rc.left;
            y = rc.bottom;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        NoteState state = CurrentState();
        constexpr UINT kCommandRefresh = 100;
        constexpr UINT kCommandConfigureSelected = 101;
        constexpr UINT kCommandImportSelected = 102;
        constexpr UINT kCommandOpenLogin = 103;
        constexpr UINT kCommandClearSelected = 104;
        constexpr UINT kCommandResourceAuto = 190;
        constexpr UINT kCommandResourceBase = 200;
        constexpr UINT kCommandConfigureBase = 220;
        constexpr UINT kCommandImportBase = 240;
        constexpr UINT kCommandClearBase = 260;
        constexpr UINT kCommandOpenConfigDir = 300;
        constexpr UINT kCommandOpenAssetDir = 301;
        constexpr UINT kCommandChangeConfigDir = 302;
        constexpr UINT kCommandChangeAssetDir = 303;
        constexpr UINT kCommandAbout = 304;
        constexpr UINT kCommandSettings = 305;

        std::wstring configuredResource = ReadSettingString(L"Resource", L"auto");
        std::transform(configuredResource.begin(), configuredResource.end(), configuredResource.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
        bool resourceAuto = configuredResource.empty() || configuredResource == L"auto";
        ResourceKind selectedResource = state.resource;

        std::wstring status = L"Status: ";
        status += state.tooltip.empty() ? (state.line1 + L" - " + state.line2) : state.tooltip;
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status.c_str());
        for (const auto& line : state.menuLines)
        {
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, line.c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandRefresh, L"Refresh now");
        AppendMenuW(menu, MF_STRING, kCommandConfigureSelected, L"Configure selected account...");
        AppendMenuW(menu, MF_STRING, kCommandImportSelected, L"Import cookie JSON for selected...");
        AppendMenuW(menu, MF_STRING, kCommandOpenLogin, L"Open HoYoLAB login page");
        AppendMenuW(menu, MF_STRING, kCommandClearSelected, L"Clear selected account...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        HMENU resourceMenu = CreatePopupMenu();
        if (resourceMenu)
        {
            AppendMenuW(resourceMenu, MF_STRING | (resourceAuto ? MF_CHECKED : 0), kCommandResourceAuto, L"Select automatically");
            AppendMenuW(resourceMenu, MF_SEPARATOR, 0, nullptr);
            for (const auto& item : kResources)
            {
                UINT flags = MF_STRING;
                if (!resourceAuto && selectedResource == item.resource)
                {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(resourceMenu, flags, kCommandResourceBase + static_cast<UINT>(item.resource), item.displayName);
            }
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(resourceMenu), L"Resource");
        }

        HMENU accountsMenu = CreatePopupMenu();
        if (accountsMenu)
        {
            for (const auto& item : kResources)
            {
                std::wstring configure = L"Configure ";
                configure += item.displayName;
                configure += L"...";
                AppendMenuW(accountsMenu, MF_STRING, kCommandConfigureBase + static_cast<UINT>(item.resource), configure.c_str());

                std::wstring import = L"Import ";
                import += item.displayName;
                import += L" JSON...";
                AppendMenuW(accountsMenu, MF_STRING, kCommandImportBase + static_cast<UINT>(item.resource), import.c_str());
            }
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(accountsMenu), L"Accounts");
        }

        HMENU clearMenu = CreatePopupMenu();
        if (clearMenu)
        {
            for (const auto& item : kResources)
            {
                AppendMenuW(clearMenu, MF_STRING, kCommandClearBase + static_cast<UINT>(item.resource), item.displayName);
            }
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(clearMenu), L"Clear Account");
        }

        HMENU advancedMenu = CreatePopupMenu();
        if (advancedMenu)
        {
            AppendMenuW(advancedMenu, MF_STRING, kCommandSettings, L"Settings...");
            AppendMenuW(advancedMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(advancedMenu, MF_STRING, kCommandOpenConfigDir, L"Open config directory");
            AppendMenuW(advancedMenu, MF_STRING, kCommandOpenAssetDir, L"Open asset directory");
            AppendMenuW(advancedMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(advancedMenu, MF_STRING, kCommandChangeConfigDir, L"Change config directory...");
            AppendMenuW(advancedMenu, MF_STRING, kCommandChangeAssetDir, L"Change asset directory...");
            AppendMenuW(advancedMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(advancedMenu, MF_STRING, kCommandAbout, L"About");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(advancedMenu), L"Advanced");
        }

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (const auto& item : kResources)
        {
            UINT flags = MF_STRING;
            if (!resourceAuto && selectedResource == item.resource)
            {
                flags |= MF_CHECKED;
            }
            AppendMenuW(menu, flags, kCommandResourceBase + static_cast<UINT>(item.resource), item.displayName);
        }

        SetForegroundWindow(m_hwnd);
        UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
        DestroyMenu(menu);

        if (command == kCommandRefresh)
        {
            BeginRefreshWithCurrentSettings();
        }
        else if (command == kCommandConfigureSelected)
        {
            ConfigureAccount(selectedResource);
        }
        else if (command == kCommandImportSelected)
        {
            ImportCookie(selectedResource);
        }
        else if (command == kCommandOpenLogin)
        {
            if (!OpenShellTarget(m_hwnd, L"https://www.hoyolab.com/home"))
            {
                MessageBoxW(m_hwnd, L"Could not open the HoYoLAB sign-in page.", kBandName, MB_OK | MB_ICONERROR);
            }
        }
        else if (command == kCommandClearSelected)
        {
            ClearAccount(selectedResource);
        }
        else if (command == kCommandResourceAuto)
        {
            WriteSettingString(L"Resource", L"auto");
            BeginRefreshWithCurrentSettings();
        }
        else if (command >= kCommandResourceBase && command < kCommandResourceBase + ARRAYSIZE(kResources))
        {
            ResourceKind resource = static_cast<ResourceKind>(command - kCommandResourceBase);
            WriteSettingString(L"Resource", MetadataFor(resource).registryValue);
            SetLoadingState(resource, MetadataFor(resource).displayName);
            BeginRefreshWithCurrentSettings();
        }
        else if (command >= kCommandConfigureBase && command < kCommandConfigureBase + ARRAYSIZE(kResources))
        {
            ConfigureAccount(static_cast<ResourceKind>(command - kCommandConfigureBase));
        }
        else if (command >= kCommandImportBase && command < kCommandImportBase + ARRAYSIZE(kResources))
        {
            ImportCookie(static_cast<ResourceKind>(command - kCommandImportBase));
        }
        else if (command >= kCommandClearBase && command < kCommandClearBase + ARRAYSIZE(kResources))
        {
            ClearAccount(static_cast<ResourceKind>(command - kCommandClearBase));
        }
        else if (command == kCommandOpenConfigDir)
        {
            ReloadSettings();
            OpenDirectory(CurrentSettings().configDir);
        }
        else if (command == kCommandOpenAssetDir)
        {
            ReloadSettings();
            OpenDirectory(CurrentSettings().assetDir);
        }
        else if (command == kCommandChangeConfigDir)
        {
            ChangeDirectorySetting(L"ConfigDir", L"Select Real-Time Notes config directory");
        }
        else if (command == kCommandChangeAssetDir)
        {
            ChangeDirectorySetting(L"AssetDir", L"Select Deskband asset directory");
        }
        else if (command == kCommandSettings)
        {
            if (ShowSettingsDialog(m_hwnd))
            {
                BeginRefreshWithCurrentSettings();
            }
        }
        else if (command == kCommandAbout)
        {
            MessageBoxW(m_hwnd, L"RealTimeNotesDeskband\nNative HoYoLAB Real-Time Notes deskband.", L"About Real Time Notes", MB_OK | MB_ICONINFORMATION);
        }
    }

    long m_ref = 1;
    IUnknown* m_site = nullptr;
    IInputObjectSite* m_inputSite = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_tooltipHwnd = nullptr;
    HWND m_parent = nullptr;
    DWORD m_bandId = 0;
    DWORD m_viewMode = 0;
    bool m_hasFocus = false;
    bool m_compositionEnabled = false;
    long m_refreshing = 0;
    CRITICAL_SECTION m_stateLock{};
    CRITICAL_SECTION m_settingsLock{};
    CRITICAL_SECTION m_windowLock{};
    ULONG_PTR m_windowToken = 0;
    NoteState m_state{};
    std::wstring m_hoverText;
    std::wstring m_installDir;
    std::wstring m_configDir;
    std::wstring m_assetDir;
};

class ClassFactory final : public IClassFactory
{
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
        {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (outer)
        {
            return CLASS_E_NOAGGREGATION;
        }

        auto band = new (std::nothrow) RealTimeNotesBand();
        if (!band)
        {
            return E_OUTOFMEMORY;
        }
        HRESULT hr = band->QueryInterface(riid, ppv);
        band->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
        {
            InterlockedIncrement(&g_lockCount);
        }
        else
        {
            InterlockedDecrement(&g_lockCount);
        }
        return S_OK;
    }

private:
    long m_ref = 1;
};

static HRESULT SetRegValueString(HKEY root, const std::wstring& subkey, const wchar_t* valueName, const std::wstring& value)
{
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(result);
    }
    result = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    LONG closeResult = RegCloseKey(key);
    if (result == ERROR_SUCCESS)
    {
        result = closeResult;
    }
    return HRESULT_FROM_WIN32(result);
}

static HRESULT EnsureRegKey(HKEY root, const std::wstring& subkey)
{
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS)
    {
        result = RegCloseKey(key);
    }
    return HRESULT_FROM_WIN32(result);
}

struct RegistryValueSnapshot
{
    HKEY root = nullptr;
    std::wstring subkey;
    std::wstring valueName;
    bool existed = false;
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
};

struct RegistryKeySnapshot
{
    HKEY root = nullptr;
    std::wstring subkey;
    bool existed = false;
};

struct RegistrationIniSnapshot
{
    bool existed = false;
    bool changed = false;
    std::wstring text;
};

static HRESULT CaptureRegistryKey(HKEY root, const std::wstring& subkey, RegistryKeySnapshot& snapshot)
{
    snapshot = RegistryKeySnapshot{ root, subkey, false };
    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
    {
        return S_OK;
    }
    if (result != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(result);
    }
    snapshot.existed = true;
    result = RegCloseKey(key);
    return HRESULT_FROM_WIN32(result);
}

static HRESULT CaptureRegistryValue(
    HKEY root,
    const std::wstring& subkey,
    const wchar_t* valueName,
    RegistryValueSnapshot& snapshot)
{
    snapshot = RegistryValueSnapshot{};
    snapshot.root = root;
    snapshot.subkey = subkey;
    snapshot.valueName = valueName ? valueName : L"";

    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
    {
        return S_OK;
    }
    if (result != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(result);
    }

    DWORD bytes = 0;
    result = RegQueryValueExW(
        key,
        snapshot.valueName.empty() ? nullptr : snapshot.valueName.c_str(),
        nullptr,
        &snapshot.type,
        nullptr,
        &bytes);
    if (result == ERROR_FILE_NOT_FOUND)
    {
        result = ERROR_SUCCESS;
    }
    else if (result == ERROR_SUCCESS)
    {
        snapshot.existed = true;
        snapshot.data.resize(bytes);
        if (bytes != 0)
        {
            result = RegQueryValueExW(
                key,
                snapshot.valueName.empty() ? nullptr : snapshot.valueName.c_str(),
                nullptr,
                &snapshot.type,
                snapshot.data.data(),
                &bytes);
            if (result == ERROR_SUCCESS)
            {
                snapshot.data.resize(bytes);
            }
        }
    }

    LONG closeResult = RegCloseKey(key);
    if (result == ERROR_SUCCESS)
    {
        result = closeResult;
    }
    return HRESULT_FROM_WIN32(result);
}

static HRESULT RestoreRegistryValue(const RegistryValueSnapshot& snapshot)
{
    if (snapshot.existed)
    {
        HKEY key = nullptr;
        LONG result = RegCreateKeyExW(
            snapshot.root,
            snapshot.subkey.c_str(),
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr);
        if (result == ERROR_SUCCESS)
        {
            result = RegSetValueExW(
                key,
                snapshot.valueName.empty() ? nullptr : snapshot.valueName.c_str(),
                0,
                snapshot.type,
                snapshot.data.empty() ? nullptr : snapshot.data.data(),
                static_cast<DWORD>(snapshot.data.size()));
            LONG closeResult = RegCloseKey(key);
            if (result == ERROR_SUCCESS)
            {
                result = closeResult;
            }
        }
        return HRESULT_FROM_WIN32(result);
    }

    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(snapshot.root, snapshot.subkey.c_str(), 0, KEY_SET_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
    {
        return S_OK;
    }
    if (result != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(result);
    }
    result = RegDeleteValueW(key, snapshot.valueName.empty() ? nullptr : snapshot.valueName.c_str());
    if (result == ERROR_FILE_NOT_FOUND)
    {
        result = ERROR_SUCCESS;
    }
    LONG closeResult = RegCloseKey(key);
    if (result == ERROR_SUCCESS)
    {
        result = closeResult;
    }
    return HRESULT_FROM_WIN32(result);
}

static bool EnsureIniDefault(
    std::wstring& text,
    const wchar_t* key,
    const std::wstring& value,
    bool overwrite,
    bool& changed)
{
    std::vector<aip::IniSectionData> document = aip::ParseIniDocument(text);
    if (!overwrite && aip::IniHasKeyInDoc(document, L"Settings", key))
    {
        return true;
    }
    if (!aip::WriteIniValueToText(text, L"Settings", key, value))
    {
        return false;
    }
    changed = true;
    return true;
}

static bool PrepareRegistrationIni(
    const std::wstring& installDir,
    const std::wstring& configDir,
    const std::wstring& assetDir,
    RegistrationIniSnapshot& snapshot)
{
    const std::wstring path = AppIniPath();
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND && error != ERROR_INVALID_NAME)
        {
            SetLastError(error);
            return false;
        }
        snapshot.existed = false;
    }
    else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    else
    {
        snapshot.existed = true;
    }

    if (!aip::LoadIniText(path, snapshot.text))
    {
        return false;
    }
    std::wstring updated = snapshot.text;
    bool changed = false;
    if (!EnsureIniDefault(updated, L"InstallDir", installDir, true, changed) ||
        !EnsureIniDefault(updated, L"ConfigDir", configDir, false, changed) ||
        !EnsureIniDefault(updated, L"AssetDir", assetDir, false, changed) ||
        !EnsureIniDefault(updated, L"Resource", L"auto", false, changed) ||
        !EnsureIniDefault(updated, L"LoggingEnabled", L"1", false, changed) ||
        !EnsureIniDefault(updated, L"KeepLegacyPlaintextSecrets", L"0", false, changed))
    {
        return false;
    }

    snapshot.changed = changed || !snapshot.existed;
    return !snapshot.changed || aip::WriteTextFileUtf8Bom(path, updated);
}

static HRESULT RollBackRegistration(
    const std::vector<RegistryValueSnapshot>& values,
    const std::vector<RegistryKeySnapshot>& keys,
    const RegistrationIniSnapshot& ini)
{
    HRESULT rollback = S_OK;
    for (auto item = values.rbegin(); item != values.rend(); ++item)
    {
        HRESULT current = RestoreRegistryValue(*item);
        if (FAILED(current) && SUCCEEDED(rollback))
        {
            rollback = current;
        }
    }
    for (auto item = keys.rbegin(); item != keys.rend(); ++item)
    {
        if (item->existed)
        {
            continue;
        }
        LONG result = RegDeleteKeyW(item->root, item->subkey.c_str());
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND && SUCCEEDED(rollback))
        {
            rollback = HRESULT_FROM_WIN32(result);
        }
    }

    if (ini.changed)
    {
        bool restored = ini.existed
            ? aip::WriteTextFileUtf8Bom(AppIniPath(), ini.text)
            : (DeleteFileW(AppIniPath().c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND);
        if (!restored && SUCCEEDED(rollback))
        {
            DWORD error = GetLastError();
            rollback = HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error);
        }
    }
    return rollback;
}

static HRESULT LastErrorAsHresult()
{
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(
        error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error);
}

static void DeleteCategoryCache()
{
    std::wstring base = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Discardable\\PostSetup\\Component Categories\\";
    RegDeleteTreeW(HKEY_CURRENT_USER, (base + kDeskBandCatid + L"\\Enum").c_str());
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

RTN_COM_EXPORT DllCanUnloadNow()
{
    return (g_objectCount == 0 && g_lockCount == 0) ? S_OK : S_FALSE;
}

RTN_COM_EXPORT DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{
    if (!IsEqualCLSID(clsid, CLSID_RealTimeNotesDeskband))
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto factory = new (std::nothrow) ClassFactory();
    if (!factory)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

static std::wstring AnsiCommandLineToWide(const char* commandLine)
{
    if (!commandLine || !commandLine[0])
    {
        return L"";
    }
    int count = MultiByteToWideChar(CP_ACP, 0, commandLine, -1, nullptr, 0);
    if (count <= 1)
    {
        return L"";
    }
    std::wstring result(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, commandLine, -1, &result[0], count) == 0)
    {
        return L"";
    }
    result.pop_back();
    return result;
}

static void ReportConfigurationMessage(HWND owner, const std::wstring& message, bool error)
{
    if (!aip::WriteCommandLineText(message, error))
    {
        MessageBoxW(owner, message.c_str(), L"Configure Real-Time Notes", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }
}

static bool TryParseBooleanSetting(const std::wstring& raw, bool& value)
{
    std::wstring normalized = TrimWide(raw);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    if (normalized == L"1" || normalized == L"true" || normalized == L"on" || normalized == L"yes")
    {
        value = true;
        return true;
    }
    if (normalized == L"0" || normalized == L"false" || normalized == L"off" || normalized == L"no")
    {
        value = false;
        return true;
    }
    return false;
}

static bool TryParseRefreshSetting(const std::wstring& raw, UINT& value)
{
    std::wstring normalized = TrimWide(raw);
    std::wstring lower = normalized;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    if (lower.empty() || lower == L"default" || lower == L"none")
    {
        value = 0;
        return true;
    }

    errno = 0;
    wchar_t* end = nullptr;
    unsigned long long parsed = wcstoull(normalized.c_str(), &end, 10);
    if (end == normalized.c_str() ||
        !end ||
        *end != L'\0' ||
        errno == ERANGE ||
        parsed < kMinRefreshSeconds ||
        parsed > kMaxRefreshSeconds)
    {
        return false;
    }
    value = static_cast<UINT>(parsed);
    return true;
}

static bool ApplyGeneralSetting(
    GeneralSettingsProfile& profile,
    const std::wstring& name,
    const std::wstring& value,
    std::wstring& error)
{
    if (_wcsicmp(name.c_str(), L"Resource") == 0)
    {
        if (!TryNormalizeResourceSetting(value, profile.resource))
        {
            error = L"Resource must be auto, resin, stamina, or charge.";
            return false;
        }
        return true;
    }
    if (_wcsicmp(name.c_str(), L"RefreshIntervalSeconds") == 0)
    {
        if (!TryParseRefreshSetting(value, profile.refreshSeconds))
        {
            error = L"RefreshIntervalSeconds must be default or a number from 30 to 86400.";
            return false;
        }
        return true;
    }
    if (_wcsicmp(name.c_str(), L"LoggingEnabled") == 0)
    {
        if (!TryParseBooleanSetting(value, profile.loggingEnabled))
        {
            error = L"LoggingEnabled must be 0/1, true/false, on/off, or yes/no.";
            return false;
        }
        return true;
    }
    if (_wcsicmp(name.c_str(), L"KeepLegacyPlaintextSecrets") == 0)
    {
        if (!TryParseBooleanSetting(value, profile.keepLegacyPlaintextSecrets))
        {
            error = L"KeepLegacyPlaintextSecrets must be 0/1, true/false, on/off, or yes/no.";
            return false;
        }
        return true;
    }

    std::wstring trimmed = TrimWide(value);
    if (trimmed.empty())
    {
        error = name + L" cannot be blank.";
        return false;
    }
    if (_wcsicmp(name.c_str(), L"InstallDir") == 0)
    {
        profile.installDir = trimmed;
        return true;
    }
    if (_wcsicmp(name.c_str(), L"ConfigDir") == 0)
    {
        profile.configDir = trimmed;
        return true;
    }
    if (_wcsicmp(name.c_str(), L"AssetDir") == 0)
    {
        profile.assetDir = trimmed;
        return true;
    }

    error = L"Unknown setting: " + name;
    return false;
}

static int RunConfigurationCommand(HWND owner, const std::wstring& rawCommandLine)
{
    std::wstring commandLine = L"RealTimeNotesDeskband " + rawCommandLine;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(commandLine.c_str(), &argc);
    if (!argv)
    {
        ReportConfigurationMessage(owner, L"The configuration command line could not be parsed.", true);
        return 2;
    }
    auto freeArguments = [](wchar_t** value) { LocalFree(value); };
    std::unique_ptr<wchar_t*, decltype(freeArguments)> arguments(argv, freeArguments);

    if (argc <= 1 || _wcsicmp(argv[1], L"--show") == 0)
    {
        ShowSettingsDialog(owner);
        return 0;
    }
    if (_wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"-h") == 0 || _wcsicmp(argv[1], L"/?") == 0)
    {
        ReportConfigurationMessage(
            owner,
            L"ConfigureDeskband.cmd [--show]\n"
            L"ConfigureDeskband.cmd --account <resin|stamina|charge>\n"
            L"ConfigureDeskband.cmd --set Name=Value [--set Name=Value ...]\n\n"
            L"Names: Resource, RefreshIntervalSeconds, LoggingEnabled, "
            L"KeepLegacyPlaintextSecrets, InstallDir, ConfigDir, AssetDir.",
            false);
        return 0;
    }
    if (_wcsicmp(argv[1], L"--account") == 0)
    {
        std::wstring resource;
        if (argc != 3 || !TryNormalizeResourceSetting(argv[2], resource) || resource == L"auto")
        {
            ReportConfigurationMessage(owner, L"--account requires resin, stamina, or charge.", true);
            return 2;
        }
        ShowAccountDialog(owner, ResourceFromString(resource), true);
        return 0;
    }

    GeneralSettingsProfile profile = ReadGeneralSettingsProfile();
    std::wstring error;
    for (int index = 1; index < argc; ++index)
    {
        if (_wcsicmp(argv[index], L"--set") != 0 || index + 1 >= argc)
        {
            ReportConfigurationMessage(owner, L"Expected --set Name=Value. Use --help for syntax.", true);
            return 2;
        }
        std::wstring assignment = argv[++index];
        size_t equals = assignment.find(L'=');
        if (equals == std::wstring::npos || equals == 0)
        {
            ReportConfigurationMessage(owner, L"Each --set argument must use Name=Value syntax.", true);
            return 2;
        }
        if (!ApplyGeneralSetting(profile, assignment.substr(0, equals), assignment.substr(equals + 1), error))
        {
            ReportConfigurationMessage(owner, error, true);
            return 2;
        }
    }

    if (!DirectoryExists(profile.installDir))
    {
        ReportConfigurationMessage(owner, L"InstallDir must name an existing directory.", true);
        return 2;
    }
    if (!SaveGeneralSettingsProfile(profile))
    {
        ReportConfigurationMessage(owner, L"The INI settings could not be saved.", true);
        return 1;
    }
    ReportConfigurationMessage(owner, L"Real-Time Notes settings updated.", false);
    return 0;
}

extern "C" __declspec(dllexport) void CALLBACK ConfigureDeskband(HWND hwnd, HINSTANCE, LPSTR commandLine, int)
{
    MigrateLegacyRegistryToIniOnce();
    int exitCode = RunConfigurationCommand(hwnd, AnsiCommandLineToWide(commandLine));
    // ConfigureDeskband is a rundll32 entry point: terminating this dedicated
    // host is the only way for its command wrapper to receive a meaningful
    // validation/persistence exit status from the required void callback ABI.
    ExitProcess(static_cast<UINT>(exitCode));
}

RTN_COM_EXPORT DllRegisterServer()
{
    MigrateLegacyRegistryToIniOnce();
    std::wstring clsid = GuidToString(CLSID_RealTimeNotesDeskband);
    std::wstring clsidKey = std::wstring(kClassesClsidKey) + L"\\" + clsid;
    std::wstring inprocKey = clsidKey + L"\\InprocServer32";
    std::wstring implementedCategoriesKey = clsidKey + L"\\Implemented Categories";
    std::wstring implementedDeskbandKey = implementedCategoriesKey + L"\\" + kDeskBandCatid;
    std::wstring componentCategoryKey = L"Software\\Classes\\Component Categories\\" + std::wstring(kDeskBandCatid);
    std::wstring modulePath = ModulePath();
    if (modulePath.empty())
    {
        return HRESULT_FROM_WIN32(
            GetLastError() == ERROR_SUCCESS
                ? ERROR_FILE_NOT_FOUND
                : GetLastError());
    }
    std::wstring installDir = ParentDirectoryOf(modulePath);
    std::wstring configDir = DefaultConfigDir(installDir);
    std::wstring assetDir = DefaultAssetDir(installDir);

    std::vector<RegistryKeySnapshot> keySnapshots;
    for (const std::wstring& key : {
        clsidKey,
        inprocKey,
        implementedCategoriesKey,
        implementedDeskbandKey,
        componentCategoryKey })
    {
        RegistryKeySnapshot snapshot;
        HRESULT hr = CaptureRegistryKey(HKEY_CURRENT_USER, key, snapshot);
        if (FAILED(hr))
        {
            return hr;
        }
        keySnapshots.push_back(std::move(snapshot));
    }

    struct RegistrationValueTarget
    {
        std::wstring subkey;
        const wchar_t* valueName;
    };
    const RegistrationValueTarget targets[] = {
        { clsidKey, nullptr },
        { inprocKey, nullptr },
        { inprocKey, L"ThreadingModel" },
        { componentCategoryKey, nullptr }
    };
    std::vector<RegistryValueSnapshot> valueSnapshots;
    for (const auto& target : targets)
    {
        RegistryValueSnapshot snapshot;
        HRESULT hr = CaptureRegistryValue(HKEY_CURRENT_USER, target.subkey, target.valueName, snapshot);
        if (FAILED(hr))
        {
            return hr;
        }
        valueSnapshots.push_back(std::move(snapshot));
    }

    aip::IniWriteMutexGuard iniGuard(AppIniPath(), 5000);
    if (!iniGuard.IsLocked())
    {
        return HRESULT_FROM_WIN32(iniGuard.Error());
    }

    RegistrationIniSnapshot iniSnapshot;
    if (!PrepareRegistrationIni(installDir, configDir, assetDir, iniSnapshot))
    {
        return LastErrorAsHresult();
    }

    auto failAndRollback = [&](HRESULT failure) {
        HRESULT rollback = RollBackRegistration(valueSnapshots, keySnapshots, iniSnapshot);
        if (FAILED(rollback))
        {
            LogLine(L"Registration failed and rollback was incomplete: " + std::to_wstring(static_cast<unsigned long>(rollback)));
        }
        return failure;
    };

    HRESULT hr = SetRegValueString(HKEY_CURRENT_USER, clsidKey, nullptr, kBandName);
    if (FAILED(hr)) return failAndRollback(hr);
    hr = SetRegValueString(HKEY_CURRENT_USER, componentCategoryKey, nullptr, L"Desk Band");
    if (FAILED(hr)) return failAndRollback(hr);
    hr = EnsureRegKey(HKEY_CURRENT_USER, implementedDeskbandKey);
    if (FAILED(hr)) return failAndRollback(hr);
    hr = SetRegValueString(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", L"Apartment");
    if (FAILED(hr)) return failAndRollback(hr);

    // The server path is the activation value. Write it only after every other
    // registration and INI operation has succeeded, so interrupted setup stays inert.
    hr = SetRegValueString(HKEY_CURRENT_USER, inprocKey, nullptr, modulePath);
    if (FAILED(hr)) return failAndRollback(hr);

    DeleteCategoryCache();
    return S_OK;
}

RTN_COM_EXPORT DllUnregisterServer()
{
    std::wstring clsid = GuidToString(CLSID_RealTimeNotesDeskband);
    std::wstring clsidKey = std::wstring(kClassesClsidKey) + L"\\" + clsid;
    LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, clsidKey.c_str());
    DeleteCategoryCache();
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND
        ? S_OK
        : HRESULT_FROM_WIN32(result);
}

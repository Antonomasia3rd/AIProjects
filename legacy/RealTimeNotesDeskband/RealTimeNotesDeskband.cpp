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
#include <vector>

#include "../../dependencies/desktop_app_baseline.h"

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "winhttp.lib")
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
static const UINT_PTR kRefreshTimer = 1;
static const UINT kStatusReadyMessage = WM_APP + 0x343;
static const UINT kRefreshRequestedMessage = kStatusReadyMessage + 1;
static const UINT kMinRefreshSeconds = 30;
static const UINT kDefaultRefreshSeconds = 300;
static const UINT kMaxRefreshSeconds = 24 * 60 * 60;
static const size_t kMaxHttpResponseBytes = 1024 * 1024;

HMODULE g_module = nullptr;
long g_objectCount = 0;
long g_lockCount = 0;
LONG g_legacyMigrationAttempted = 0;
volatile LONG g_windowGeneration = 0;

enum class ResourceKind
{
    Resin,
    Stamina,
    Charge
};

enum class StateKind
{
    Loading,
    Ok,
    Full,
    Error
};

struct NoteState
{
    ResourceKind resource = ResourceKind::Resin;
    StateKind state = StateKind::Loading;
    std::wstring line1 = L"Loading";
    std::wstring line2 = L"Real-Time Notes";
    std::wstring tooltip = L"Real-Time Notes";
    std::vector<std::wstring> menuLines;
    UINT refreshSeconds = kDefaultRefreshSeconds;
};

struct AppConfig
{
    std::string uid;
    std::string ltoken;
    std::string ltuid;
    UINT refreshSeconds = kDefaultRefreshSeconds;
};

struct ResourceMetadata
{
    ResourceKind resource;
    const wchar_t* registryValue;
    const wchar_t* displayName;
    const wchar_t* configFile;
    const wchar_t* fullIcon;
    const wchar_t* notFullIcon;
    const wchar_t* errorIcon;
    const wchar_t* host;
    const wchar_t* path;
};

static const ResourceMetadata kResources[] = {
    {
        ResourceKind::Resin,
        L"resin",
        L"Resin",
        L"genshin_cookie.json",
        L"genshin\\resin_full.ico",
        L"genshin\\resin_not_full.ico",
        L"genshin\\resin_error.ico",
        L"bbs-api-os.hoyolab.com",
        L"/game_record/genshin/api/dailyNote"
    },
    {
        ResourceKind::Stamina,
        L"stamina",
        L"Stamina",
        L"hsr_cookie.json",
        L"hsr\\stamina_full.ico",
        L"hsr\\stamina_not_full.ico",
        L"hsr\\stamina_error.ico",
        L"bbs-api-os.hoyolab.com",
        L"/game_record/hkrpg/api/note"
    },
    {
        ResourceKind::Charge,
        L"charge",
        L"Charge",
        L"zzz_cookie.json",
        L"zzz\\charge_full.ico",
        L"zzz\\charge_not_full.ico",
        L"zzz\\charge_error.ico",
        L"sg-act-nap-api.hoyolab.com",
        L"/event/game_record_zzz/api/zzz/note"
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

static std::wstring BytesToHexWide(const BYTE* bytes, DWORD byteCount)
{
    static const wchar_t* digits = L"0123456789abcdef";
    std::wstring result;
    result.reserve(static_cast<size_t>(byteCount) * 2);
    for (DWORD i = 0; i < byteCount; ++i)
    {
        BYTE value = bytes[i];
        result.push_back(digits[(value >> 4) & 0x0f]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

static bool HexWideToBytes(const std::wstring& hex, std::vector<BYTE>& bytes)
{
    auto hexValue = [](wchar_t ch) -> int {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    };

    if ((hex.size() % 2) != 0)
    {
        return false;
    }

    bytes.clear();
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        int hi = hexValue(hex[i]);
        int lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        bytes.push_back(static_cast<BYTE>((hi << 4) | lo));
    }
    return true;
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

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(value.data()));
    input.cbData = static_cast<DWORD>(value.size() * sizeof(wchar_t));

    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"RealTimeNotesDeskband", nullptr, nullptr, nullptr, 0, &output))
    {
        return L"";
    }

    std::wstring protectedValue = L"dpapi:" + BytesToHexWide(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return protectedValue;
}

static bool UnprotectSecretString(const std::wstring& value, std::wstring& plain)
{
    static const std::wstring prefix = L"dpapi:";
    if (value.size() <= prefix.size() ||
        _wcsnicmp(value.c_str(), prefix.c_str(), prefix.size()) != 0)
    {
        return false;
    }

    std::vector<BYTE> protectedBytes;
    if (!HexWideToBytes(value.substr(prefix.size()), protectedBytes) || protectedBytes.empty())
    {
        return false;
    }

    DATA_BLOB input{};
    input.pbData = protectedBytes.data();
    input.cbData = static_cast<DWORD>(protectedBytes.size());

    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
    {
        return false;
    }

    if ((output.cbData % sizeof(wchar_t)) != 0)
    {
        LocalFree(output.pbData);
        return false;
    }

    plain.assign(reinterpret_cast<wchar_t*>(output.pbData), output.cbData / sizeof(wchar_t));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
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
            continue;
        }

        AppConfig cfg;
        if (LoadLegacyAccountConfig(item.resource, cfg))
        {
            if (SaveAccountConfig(item.resource, cfg))
            {
                LogLine(std::wstring(L"Migrated legacy registry account settings for ") + item.displayName);
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

static int JsonHexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool JsonReadHexQuad(const std::string& json, size_t pos, uint32_t& value)
{
    if (pos + 4 > json.size())
    {
        return false;
    }

    value = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        int digit = JsonHexValue(json[pos + i]);
        if (digit < 0)
        {
            return false;
        }
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    return true;
}

static void JsonAppendUtf8(uint32_t codePoint, std::string& out)
{
    if (codePoint <= 0x7F)
    {
        out.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint <= 0x10FFFF)
    {
        out.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

static bool JsonDecodeStringAt(const std::string& json, size_t start, std::string& out, size_t& after)
{
    if (start >= json.size() || json[start] != '"')
    {
        return false;
    }

    std::string value;
    for (size_t pos = start + 1; pos < json.size(); ++pos)
    {
        unsigned char ch = static_cast<unsigned char>(json[pos]);
        if (ch == '"')
        {
            out = value;
            after = pos + 1;
            return true;
        }
        if (ch != '\\')
        {
            value.push_back(static_cast<char>(ch));
            continue;
        }

        if (++pos >= json.size())
        {
            return false;
        }

        char escaped = json[pos];
        switch (escaped)
        {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u':
        {
            uint32_t first = 0;
            if (!JsonReadHexQuad(json, pos + 1, first))
            {
                return false;
            }
            pos += 4;

            if (first >= 0xD800 && first <= 0xDBFF)
            {
                if (pos + 6 >= json.size() || json[pos + 1] != '\\' || json[pos + 2] != 'u')
                {
                    return false;
                }
                uint32_t second = 0;
                if (!JsonReadHexQuad(json, pos + 3, second) || second < 0xDC00 || second > 0xDFFF)
                {
                    return false;
                }
                pos += 6;
                JsonAppendUtf8(0x10000 + (((first - 0xD800) << 10) | (second - 0xDC00)), value);
            }
            else if (first >= 0xDC00 && first <= 0xDFFF)
            {
                return false;
            }
            else
            {
                JsonAppendUtf8(first, value);
            }
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

static size_t JsonSkipWhitespace(const std::string& json, size_t pos)
{
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
    {
        ++pos;
    }
    return pos;
}

static size_t FindJsonValue(const std::string& json, const std::string& key, size_t start = 0)
{
    for (size_t pos = start; pos < json.size();)
    {
        if (json[pos] != '"')
        {
            ++pos;
            continue;
        }

        std::string decodedKey;
        size_t afterString = 0;
        if (!JsonDecodeStringAt(json, pos, decodedKey, afterString))
        {
            return std::string::npos;
        }

        size_t colon = JsonSkipWhitespace(json, afterString);
        if (colon < json.size() && json[colon] == ':' && decodedKey == key)
        {
            return JsonSkipWhitespace(json, colon + 1);
        }

        pos = afterString;
    }
    return std::string::npos;
}

static bool JsonString(const std::string& json, const std::string& key, std::string& out)
{
    size_t pos = FindJsonValue(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '"')
    {
        return false;
    }

    size_t after = 0;
    return JsonDecodeStringAt(json, pos, out, after);
}

static bool JsonInt(const std::string& json, const std::string& key, int& out)
{
    size_t pos = FindJsonValue(json, key);
    if (pos == std::string::npos || pos >= json.size())
    {
        return false;
    }

    bool quoted = json[pos] == '"';
    if (quoted)
    {
        ++pos;
    }

    bool negative = false;
    if (pos < json.size() && json[pos] == '-')
    {
        negative = true;
        ++pos;
    }

    if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos])))
    {
        return false;
    }

    long long value = 0;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
    {
        int digit = json[pos] - '0';
        if (value > (static_cast<long long>(INT_MAX) + (negative ? 1LL : 0LL) - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
        ++pos;
    }
    if (negative)
    {
        value = -value;
        if (value < INT_MIN)
        {
            return false;
        }
    }

    if (quoted)
    {
        if (pos >= json.size() || json[pos] != '"')
        {
            return false;
        }
        ++pos;
    }
    pos = JsonSkipWhitespace(json, pos);
    if (pos < json.size() &&
        json[pos] != ',' &&
        json[pos] != '}' &&
        json[pos] != ']')
    {
        return false;
    }

    out = static_cast<int>(value);
    return true;
}

static bool ParseAsciiInt(const std::string& text, int& value)
{
    if (text.empty())
    {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    long long parsed = strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() ||
        !end ||
        *end != '\0' ||
        errno == ERANGE ||
        parsed < INT_MIN ||
        parsed > INT_MAX)
    {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

static bool JsonObject(const std::string& json, const std::string& key, std::string& out)
{
    size_t pos = FindJsonValue(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '{')
    {
        return false;
    }

    size_t start = pos;
    int depth = 0;
    bool inString = false;
    bool escaping = false;
    for (; pos < json.size(); ++pos)
    {
        char ch = json[pos];
        if (inString)
        {
            if (escaping)
            {
                escaping = false;
            }
            else if (ch == '\\')
            {
                escaping = true;
            }
            else if (ch == '"')
            {
                inString = false;
            }
            continue;
        }

        if (ch == '"')
        {
            inString = true;
        }
        else if (ch == '{')
        {
            ++depth;
        }
        else if (ch == '}')
        {
            --depth;
            if (depth == 0)
            {
                out.assign(json.data() + start, pos - start + 1);
                return true;
            }
        }
    }
    return false;
}

static bool JsonArray(const std::string& json, const std::string& key, std::string& out)
{
    size_t pos = FindJsonValue(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '[')
    {
        return false;
    }

    size_t start = pos;
    int depth = 0;
    bool inString = false;
    bool escaping = false;
    for (; pos < json.size(); ++pos)
    {
        char ch = json[pos];
        if (inString)
        {
            if (escaping)
            {
                escaping = false;
            }
            else if (ch == '\\')
            {
                escaping = true;
            }
            else if (ch == '"')
            {
                inString = false;
            }
            continue;
        }

        if (ch == '"')
        {
            inString = true;
        }
        else if (ch == '[')
        {
            ++depth;
        }
        else if (ch == ']')
        {
            --depth;
            if (depth == 0)
            {
                out.assign(json.data() + start, pos - start + 1);
                return true;
            }
        }
    }
    return false;
}

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

static std::string Md5Hex(const std::string& text)
{
    if (text.size() > MAXDWORD)
    {
        return "";
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE bytes[16]{};
    DWORD byteCount = sizeof(bytes);

    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        return "";
    }
    if (!CryptCreateHash(provider, CALG_MD5, 0, 0, &hash))
    {
        CryptReleaseContext(provider, 0);
        return "";
    }
    if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(text.data()), static_cast<DWORD>(text.size()), 0) ||
        !CryptGetHashParam(hash, HP_HASHVAL, bytes, &byteCount, 0))
    {
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return "";
    }

    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (BYTE b : bytes)
    {
        result.push_back(hex[b >> 4]);
        result.push_back(hex[b & 0x0F]);
    }

    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return result;
}

static std::string GenerateDS()
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char salt[] = "6s25p5ox5y14umn1p61aqyyvbvvl3lrt";

    std::random_device randomDevice;
    std::mt19937 generator(randomDevice());
    std::uniform_int_distribution<int> distribution(0, static_cast<int>(sizeof(alphabet) - 2));

    std::string random;
    random.reserve(6);
    for (int i = 0; i < 6; ++i)
    {
        random.push_back(alphabet[distribution(generator)]);
    }

    std::time_t now = std::time(nullptr);
    std::string source = "salt=" + std::string(salt) + "&t=" + std::to_string(static_cast<long long>(now)) + "&r=" + random;
    return std::to_string(static_cast<long long>(now)) + "," + random + "," + Md5Hex(source);
}

static bool HttpGet(const ResourceMetadata& metadata, const std::wstring& server, const std::string& uid, const AppConfig& cfg, std::string& response)
{
    std::wstring requestPath = std::wstring(metadata.path) + L"?server=" + server + L"&role_id=" + Utf8ToWide(uid);
    std::wstring cookie = L"ltoken_v2=" + Utf8ToWide(cfg.ltoken) + L"; ltuid_v2=" + Utf8ToWide(cfg.ltuid);
    std::wstring headers =
        L"Accept: application/json, text/plain, */*\r\n"
        L"Accept-Language: en-US,en;q=0.5\r\n"
        L"x-rpc-client_type: 5\r\n"
        L"x-rpc-app_version: 1.5.0\r\n"
        L"x-rpc-language: en-us\r\n"
        L"Origin: https://act.hoyolab.com\r\n"
        L"Referer: https://act.hoyolab.com/\r\n"
        L"Sec-Fetch-Dest: empty\r\n"
        L"Sec-Fetch-Mode: cors\r\n"
        L"Sec-Fetch-Site: same-site\r\n";
    headers += L"Cookie: " + cookie + L"\r\n";
    headers += L"DS: " + Utf8ToWide(GenerateDS()) + L"\r\n";

    HINTERNET session = WinHttpOpen(L"RealTimeNotesDeskband/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        return false;
    }
    if (!WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000))
    {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET connection = WinHttpConnect(session, metadata.host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection)
    {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    BOOL ok = WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr);

    if (ok)
    {
        DWORD status = 0;
        DWORD statusBytes = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX);
        ok = status >= 200 && status < 300;
    }

    if (ok)
    {
        response.clear();
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                ok = FALSE;
                break;
            }
            if (available == 0)
            {
                break;
            }
            if (response.size() + static_cast<size_t>(available) > kMaxHttpResponseBytes)
            {
                ok = FALSE;
                response.clear();
                break;
            }
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read))
            {
                ok = FALSE;
                break;
            }
            if (read == 0)
            {
                break;
            }
            response.append(buffer.data(), buffer.data() + read);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok == TRUE;
}

static bool GenshinServer(const std::string& uid, std::wstring& server)
{
    if (uid.empty())
    {
        return false;
    }
    switch (uid[0])
    {
    case '1':
    case '2': server = L"cn_gf01"; return true;
    case '5': server = L"cn_qd01"; return true;
    case '6': server = L"os_usa"; return true;
    case '7': server = L"os_euro"; return true;
    case '8': server = L"os_asia"; return true;
    case '9': server = L"os_cht"; return true;
    default: return false;
    }
}

static bool HsrServer(const std::string& uid, std::wstring& server)
{
    if (uid.empty())
    {
        return false;
    }
    switch (uid[0])
    {
    case '1':
    case '2': server = L"prod_gf_cn"; return true;
    case '5': server = L"prod_qd_cn"; return true;
    case '6': server = L"prod_official_usa"; return true;
    case '7': server = L"prod_official_eur"; return true;
    case '8': server = L"prod_official_asia"; return true;
    case '9': server = L"prod_official_cht"; return true;
    default: return false;
    }
}

static bool ZzzServer(const std::string& uid, std::wstring& server)
{
    if (uid.size() < 2)
    {
        return false;
    }
    switch (uid[1])
    {
    case '0': server = L"prod_gf_us"; return true;
    case '3': server = L"prod_gf_jp"; return true;
    case '5': server = L"prod_gf_eu"; return true;
    case '7': server = L"prod_gf_sg"; return true;
    default: return false;
    }
}

static std::wstring FormatDuration(int seconds)
{
    if (seconds <= 0)
    {
        return L"Full";
    }
    int hours = seconds / 3600;
    int minutes = (seconds / 60) - hours * 60;

    wchar_t buffer[64]{};
    if (hours > 0)
    {
        swprintf(buffer, ARRAYSIZE(buffer), L"%dh %dm", hours, minutes);
    }
    else
    {
        swprintf(buffer, ARRAYSIZE(buffer), L"%dm", minutes);
    }
    return buffer;
}

static std::wstring FormatCount(int current, int max)
{
    return std::to_wstring(current) + L"/" + std::to_wstring(max);
}

static std::wstring FormatCountWithRecovery(int current, int max, int seconds)
{
    std::wstring value = FormatCount(current, max);
    if (seconds > 0)
    {
        value += L" [";
        value += FormatDuration(seconds);
        value += L"]";
    }
    return value;
}

static std::wstring FormatMenuLine(const wchar_t* label, int current, int max)
{
    return std::wstring(label) + L": " + FormatCount(current, max);
}

static size_t CountSubstring(const std::string& text, const std::string& pattern)
{
    if (pattern.empty())
    {
        return 0;
    }

    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos)
    {
        ++count;
        pos += pattern.size();
    }
    return count;
}

static std::wstring ZzzSaleStateText(const std::string& saleState)
{
    if (saleState == "SaleStateDoing")
    {
        return L"Open";
    }
    if (saleState == "SaleStateNo")
    {
        return L"Closed";
    }
    if (saleState == "SaleStateDone")
    {
        return L"Done";
    }
    return L"ERROR";
}

static std::wstring ZzzCardStateText(const std::string& cardSign)
{
    if (cardSign == "CardSignNo")
    {
        return L"Incomplete";
    }
    if (cardSign == "CardSignDone")
    {
        return L"Done";
    }
    return L"ERROR";
}

static UINT EffectiveRefreshSeconds(UINT configRefresh)
{
    DWORD configuredRefresh = ReadSettingDword(L"RefreshIntervalSeconds", 0);
    UINT refresh = configuredRefresh > 0 ? configuredRefresh : configRefresh;
    return ClampRefreshSeconds(refresh);
}

static NoteState ErrorState(ResourceKind resource, const std::wstring& line1, const std::wstring& line2, UINT refreshSeconds = kDefaultRefreshSeconds)
{
    LogLine(line1 + L": " + line2);
    NoteState state;
    state.resource = resource;
    state.state = StateKind::Error;
    state.line1 = line1;
    state.line2 = line2;
    state.tooltip = line1 + L" - " + line2;
    state.refreshSeconds = EffectiveRefreshSeconds(refreshSeconds);
    return state;
}

static NoteState FetchState(const std::wstring& configDir)
{
    ResourceKind resource = ReadResourceSetting(configDir);
    const auto& metadata = MetadataFor(resource);

    AppConfig cfg;
    if (!LoadAccountConfig(resource, cfg) && !LoadConfig(JoinPath(configDir, metadata.configFile), cfg))
    {
        return ErrorState(resource, L"Login needed", L"Configure account");
    }

    std::wstring server;
    bool serverOk = false;
    if (resource == ResourceKind::Resin)
    {
        serverOk = GenshinServer(cfg.uid, server);
    }
    else if (resource == ResourceKind::Stamina)
    {
        serverOk = HsrServer(cfg.uid, server);
    }
    else
    {
        serverOk = ZzzServer(cfg.uid, server);
    }
    if (!serverOk)
    {
        return ErrorState(resource, L"Bad UID", Utf8ToWide(cfg.uid), cfg.refreshSeconds);
    }

    std::string body;
    if (!HttpGet(metadata, server, cfg.uid, cfg, body))
    {
        return ErrorState(resource, L"Network error", metadata.displayName, cfg.refreshSeconds);
    }

    int retcode = -1;
    JsonInt(body, "retcode", retcode);
    if (retcode != 0)
    {
        std::string message;
        JsonString(body, "message", message);
        return ErrorState(resource, L"API error", message.empty() ? std::to_wstring(retcode) : Utf8ToWide(message), cfg.refreshSeconds);
    }

    int current = 0;
    int max = 0;
    int seconds = 0;
    bool parsed = false;
    if (resource == ResourceKind::Resin)
    {
        std::string recovery;
        parsed = JsonInt(body, "current_resin", current) && JsonInt(body, "max_resin", max);
        if (JsonString(body, "resin_recovery_time", recovery))
        {
            ParseAsciiInt(recovery, seconds);
        }
    }
    else if (resource == ResourceKind::Stamina)
    {
        parsed = JsonInt(body, "current_stamina", current) && JsonInt(body, "max_stamina", max);
        JsonInt(body, "stamina_recover_time", seconds);
    }
    else
    {
        std::string energy;
        std::string progress;
        parsed = JsonObject(body, "energy", energy) &&
            JsonObject(energy, "progress", progress) &&
            JsonInt(progress, "current", current) &&
            JsonInt(progress, "max", max);
        JsonInt(energy, "restore", seconds);
    }

    if (!parsed || max <= 0)
    {
        return ErrorState(resource, L"Parse error", metadata.displayName, cfg.refreshSeconds);
    }

    NoteState state;
    state.resource = resource;
    state.state = current >= max ? StateKind::Full : StateKind::Ok;
    state.line1 = FormatCount(current, max);
    state.line2 = FormatDuration(seconds);
    state.tooltip = std::wstring(metadata.displayName) + L": " + state.line1 + L" - " + state.line2;
    state.refreshSeconds = EffectiveRefreshSeconds(cfg.refreshSeconds);

    if (resource == ResourceKind::Resin)
    {
        state.menuLines.push_back(L"Resin: " + FormatCountWithRecovery(current, max, seconds));

        int finishedTasks = 0;
        int totalTasks = 0;
        if (JsonInt(body, "finished_task_num", finishedTasks) && JsonInt(body, "total_task_num", totalTasks))
        {
            state.menuLines.push_back(FormatMenuLine(L"Commissions", finishedTasks, totalTasks));
        }

        std::string expeditions;
        int maxExpeditions = 0;
        if (JsonArray(body, "expeditions", expeditions) && JsonInt(body, "max_expedition_num", maxExpeditions))
        {
            state.menuLines.push_back(FormatMenuLine(L"Expeditions", static_cast<int>(CountSubstring(expeditions, "\"Finished\"")), maxExpeditions));
        }

        int currentHomeCoin = 0;
        int maxHomeCoin = 0;
        if (JsonInt(body, "current_home_coin", currentHomeCoin) && JsonInt(body, "max_home_coin", maxHomeCoin))
        {
            state.menuLines.push_back(FormatMenuLine(L"Realm", currentHomeCoin, maxHomeCoin));
        }

        int remainBosses = 0;
        int bossLimit = 0;
        if (JsonInt(body, "remain_resin_discount_num", remainBosses) && JsonInt(body, "resin_discount_num_limit", bossLimit))
        {
            state.menuLines.push_back(FormatMenuLine(L"Weekly Bosses", remainBosses, bossLimit));
        }
    }
    else if (resource == ResourceKind::Stamina)
    {
        state.menuLines.push_back(L"Stamina: " + FormatCountWithRecovery(current, max, seconds));

        int currentTrain = 0;
        int maxTrain = 0;
        if (JsonInt(body, "current_train_score", currentTrain) && JsonInt(body, "max_train_score", maxTrain))
        {
            state.menuLines.push_back(FormatMenuLine(L"Training", currentTrain, maxTrain));
        }

        std::string expeditions;
        int totalExpeditions = 0;
        if (JsonArray(body, "expeditions", expeditions) && JsonInt(body, "total_expedition_num", totalExpeditions))
        {
            state.menuLines.push_back(FormatMenuLine(L"Expeditions", static_cast<int>(CountSubstring(expeditions, "\"Finished\"")), totalExpeditions));
        }

        int reserve = 0;
        if (JsonInt(body, "current_reserve_stamina", reserve))
        {
            state.menuLines.push_back(FormatMenuLine(L"Reserve", reserve, 2400));
        }

        int cocoon = 0;
        int cocoonLimit = 0;
        if (JsonInt(body, "weekly_cocoon_cnt", cocoon) && JsonInt(body, "weekly_cocoon_limit", cocoonLimit))
        {
            state.menuLines.push_back(FormatMenuLine(L"Echo of War", cocoon, cocoonLimit));
        }
    }
    else
    {
        state.menuLines.push_back(L"Charge: " + FormatCountWithRecovery(current, max, seconds));

        std::string vitality;
        int currentVitality = 0;
        int maxVitality = 0;
        if (JsonObject(body, "vitality", vitality) && JsonInt(vitality, "current", currentVitality) && JsonInt(vitality, "max", maxVitality))
        {
            state.menuLines.push_back(FormatMenuLine(L"Engagement", currentVitality, maxVitality));
        }

        std::string cardSign;
        if (JsonString(body, "card_sign", cardSign))
        {
            state.menuLines.push_back(L"Scratch Card: " + ZzzCardStateText(cardSign));
        }

        std::string vhsSale;
        std::string saleState;
        if (JsonObject(body, "vhs_sale", vhsSale) && JsonString(vhsSale, "sale_state", saleState))
        {
            state.menuLines.push_back(L"Video Store: " + ZzzSaleStateText(saleState));
        }
    }
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
    if (end == trimmed.c_str() || (end && *end) || errno == ERANGE || parsed > kMaxRefreshSeconds)
    {
        MessageBoxW(owner, L"Refresh interval must be blank or a number from 30 to 86400 seconds.", L"Configure Account", MB_OK | MB_ICONWARNING);
        return false;
    }

    refresh = ClampRefreshSeconds(static_cast<UINT>(parsed));
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
            info->dwMask &= ~DBIM_TITLE;
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
            NoteState state = FetchState(settings.configDir);

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
        else if (command == kCommandAbout)
        {
            MessageBoxW(m_hwnd, L"RealTimeNotesDeskband\nNative HoYoLAB Real-Time Notes deskband.", L"About Real Time Notes", MB_OK | MB_ICONINFORMATION);
        }
    }

    long m_ref = 1;
    IUnknown* m_site = nullptr;
    IInputObjectSite* m_inputSite = nullptr;
    HWND m_hwnd = nullptr;
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

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    return (g_objectCount == 0 && g_lockCount == 0) ? S_OK : S_FALSE;
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
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

extern "C" __declspec(dllexport) void CALLBACK ConfigureDeskband(HWND hwnd, HINSTANCE, LPSTR, int)
{
    MigrateLegacyRegistryToIniOnce();
    std::wstring installDir = ReadSettingString(L"InstallDir", ParentDirectoryOf(ModulePath()));
    std::wstring configDir = ReadSettingString(L"ConfigDir", DefaultConfigDir(installDir));
    ShowAccountDialog(hwnd, ReadResourceSetting(configDir), true);
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE DllRegisterServer()
{
    MigrateLegacyRegistryToIniOnce();
    std::wstring clsid = GuidToString(CLSID_RealTimeNotesDeskband);
    std::wstring clsidKey = std::wstring(kClassesClsidKey) + L"\\" + clsid;
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

    HRESULT hr = SetRegValueString(HKEY_CURRENT_USER, clsidKey, nullptr, kBandName);
    if (FAILED(hr)) return hr;
    hr = SetRegValueString(HKEY_CURRENT_USER, clsidKey + L"\\InprocServer32", nullptr, modulePath);
    if (FAILED(hr)) return hr;
    hr = SetRegValueString(HKEY_CURRENT_USER, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    if (FAILED(hr)) return hr;
    hr = EnsureRegKey(HKEY_CURRENT_USER, clsidKey + L"\\Implemented Categories\\" + kDeskBandCatid);
    if (FAILED(hr)) return hr;
    hr = SetRegValueString(HKEY_CURRENT_USER, L"Software\\Classes\\Component Categories\\" + std::wstring(kDeskBandCatid), nullptr, L"Desk Band");
    if (FAILED(hr)) return hr;

    if (!WriteSettingString(L"InstallDir", installDir))
    {
        return LastErrorAsHresult();
    }
    if (ReadSettingString(L"ConfigDir", L"").empty())
    {
        if (!WriteSettingString(L"ConfigDir", configDir))
        {
            return LastErrorAsHresult();
        }
    }
    if (ReadSettingString(L"AssetDir", L"").empty())
    {
        if (!WriteSettingString(L"AssetDir", assetDir))
        {
            return LastErrorAsHresult();
        }
    }
    if (ReadSettingString(L"Resource", L"").empty())
    {
        if (!WriteSettingString(L"Resource", L"auto"))
        {
            return LastErrorAsHresult();
        }
    }
    DWORD loggingEnabled = 0;
    if (!TryReadIniDword(L"Settings", L"LoggingEnabled", loggingEnabled))
    {
        if (!WriteSettingDword(L"LoggingEnabled", 1))
        {
            return LastErrorAsHresult();
        }
    }

    DeleteCategoryCache();
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE DllUnregisterServer()
{
    std::wstring clsid = GuidToString(CLSID_RealTimeNotesDeskband);
    std::wstring clsidKey = std::wstring(kClassesClsidKey) + L"\\" + clsid;
    LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, clsidKey.c_str());
    DeleteCategoryCache();
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND
        ? S_OK
        : HRESULT_FROM_WIN32(result);
}

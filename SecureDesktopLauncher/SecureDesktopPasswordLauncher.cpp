#define UNICODE
#define _UNICODE

#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winver.h>
#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static const wchar_t* kConfigFileName = L"SecureDesktopPasswordLauncher.ini";
static const wchar_t* kPasswordKdfName = L"PBKDF2-SHA256";
static const DWORD kPasswordKdfIterations = 210000;
static const DWORD kPasswordKdfMaxIterations = 5000000;

struct GateConfig
{
    std::wstring configPath;
    std::wstring kdf;
    DWORD kdfIterations = kPasswordKdfIterations;
    std::wstring saltHex;
    std::wstring hashHex;
    std::wstring pbkdf2HashHex;
    bool keepLegacySha256Hash = false;
    DWORD maxAttempts = 3;
    DWORD lockoutSeconds = 30;
    std::wstring launchPath = L"C:\\Windows\\System32\\cmd.exe";
    std::wstring arguments;
    std::wstring workingDirectory = L"C:\\Windows\\System32";
    std::wstring desktop = L"WinSta0\\Winlogon";
    int showWindow = SW_SHOWNORMAL;
    bool startMinimized = true;
    bool topMost = false;
    DWORD autoLockMinutes = 0;
    bool trusted = true;
    std::wstring trustError;
};

struct PasswordDialogState
{
    std::wstring title;
    std::wstring prompt;
    bool confirm = false;
    bool accepted = false;
    std::wstring password;
    HWND edit1 = nullptr;
    HWND edit2 = nullptr;
};

struct LaunchedProcess
{
    HANDLE process = nullptr;
    HANDLE wait = nullptr;
    DWORD pid = 0;
};

struct ControlWindowState
{
    GateConfig config;
    HANDLE job = nullptr;
    std::vector<LaunchedProcess> processes;
    std::vector<HWND> trackedWindows;
    HWND hwnd = nullptr;
    HWND processCount = nullptr;
    std::vector<HWND> processRows;
    HWND quickLock = nullptr;
    HWND openProgram = nullptr;
    HWND exitButton = nullptr;
    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;
    bool destroyLargeIcon = false;
    bool destroySmallIcon = false;
    std::vector<HICON> rowIcons;
    bool lockInProgress = false;
    std::wstring targetFriendlyName;
    std::wstring targetDisplayName;
};

static const int kQuickLockButtonId = 2001;
static const int kOpenProgramButtonId = 2002;
static const int kExitButtonId = 2003;
static const UINT kProcessExitedMessage = WM_APP + 1;
static const UINT_PTR kAutoLockTimerId = 3001;

static std::wstring Trim(const std::wstring& value)
{
    size_t begin = 0;
    while (begin < value.size() && iswspace(value[begin])) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && iswspace(value[end - 1])) {
        --end;
    }

    return value.substr(begin, end - begin);
}

static std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

static std::wstring CurrentExePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return L"";
        }
        if (len < buffer.size() - 1) {
            return std::wstring(buffer.data(), len);
        }
        buffer.resize(buffer.size() * 2);
    }
}

static std::wstring DirectoryOf(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

static std::wstring BaseName(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

static std::wstring CombinePath(const std::wstring& left, const std::wstring& right)
{
    if (left.empty()) {
        return right;
    }
    wchar_t last = left[left.size() - 1];
    if (last == L'\\' || last == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

static std::wstring ParentDirectory(std::wstring path)
{
    while (!path.empty() && (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/')) {
        path.resize(path.size() - 1);
    }

    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : path.substr(0, pos);
}

static bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool DirectoryExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool EqualKnownSid(PSID sid, WELL_KNOWN_SID_TYPE type)
{
    BYTE buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD size = sizeof(buffer);
    return CreateWellKnownSid(type, nullptr, buffer, &size) && EqualSid(sid, buffer);
}

static bool EqualSidString(PSID sid, const wchar_t* sidString)
{
    PSID parsedSid = nullptr;
    if (!ConvertStringSidToSidW(sidString, &parsedSid))
    {
        return false;
    }

    bool equal = EqualSid(sid, parsedSid) != FALSE;
    LocalFree(parsedSid);
    return equal;
}

static bool IsTrustedSecurityPrincipal(PSID sid)
{
    return EqualKnownSid(sid, WinLocalSystemSid) ||
        EqualKnownSid(sid, WinBuiltinAdministratorsSid) ||
        EqualSidString(sid, L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464");
}

static bool IsWriteLikeAccess(DWORD mask)
{
    const DWORD writeMask =
        FILE_WRITE_DATA |
        FILE_APPEND_DATA |
        FILE_WRITE_EA |
        FILE_WRITE_ATTRIBUTES |
        DELETE |
        FILE_DELETE_CHILD |
        WRITE_DAC |
        WRITE_OWNER |
        GENERIC_WRITE |
        GENERIC_ALL;
    return (mask & writeMask) != 0;
}

static bool IsReplaceLikeAccess(DWORD mask)
{
    const DWORD replaceMask =
        DELETE |
        FILE_DELETE_CHILD |
        WRITE_DAC |
        WRITE_OWNER |
        GENERIC_ALL;
    return (mask & replaceMask) != 0;
}

static bool PathHasUntrustedAccessAce(const std::wstring& path, bool replaceOnly)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    PACL dacl = nullptr;
    DWORD error = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &dacl,
        nullptr,
        &sd);
    if (error != ERROR_SUCCESS || !dacl)
    {
        if (sd) LocalFree(sd);
        return true;
    }

    bool unsafe = false;
    for (DWORD i = 0; i < dacl->AceCount && !unsafe; ++i)
    {
        void* aceData = nullptr;
        if (!GetAce(dacl, i, &aceData) || !aceData)
        {
            unsafe = true;
            break;
        }

        ACE_HEADER* header = reinterpret_cast<ACE_HEADER*>(aceData);
        if ((header->AceFlags & INHERIT_ONLY_ACE) != 0 ||
            (header->AceType != ACCESS_ALLOWED_ACE_TYPE &&
             header->AceType != ACCESS_ALLOWED_CALLBACK_ACE_TYPE))
        {
            continue;
        }

        auto* ace = reinterpret_cast<ACCESS_ALLOWED_ACE*>(aceData);
        PSID sid = reinterpret_cast<PSID>(&ace->SidStart);
        bool unsafeAccess = replaceOnly ? IsReplaceLikeAccess(ace->Mask) : IsWriteLikeAccess(ace->Mask);
        if (unsafeAccess && !IsTrustedSecurityPrincipal(sid))
        {
            unsafe = true;
        }
    }

    if (sd) LocalFree(sd);
    return unsafe;
}

static bool PathOwnerIsTrusted(const std::wstring& path)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    PSID owner = nullptr;
    DWORD error = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION,
        &owner,
        nullptr,
        nullptr,
        nullptr,
        &sd);
    if (error != ERROR_SUCCESS || !owner)
    {
        if (sd) LocalFree(sd);
        return false;
    }

    bool trusted = IsTrustedSecurityPrincipal(owner);
    if (sd) LocalFree(sd);
    return trusted;
}

static bool PathSecurityIsTrusted(const std::wstring& path, bool replaceOnly = false)
{
    return PathOwnerIsTrusted(path) && !PathHasUntrustedAccessAce(path, replaceOnly);
}

static bool IsAbsoluteFileSystemPath(const std::wstring& path)
{
    std::wstring value = path;
    if (value.rfind(L"\\\\?\\", 0) == 0)
    {
        value.erase(0, 4);
    }

    return value.size() >= 3 &&
        iswalpha(value[0]) &&
        value[1] == L':' &&
        (value[2] == L'\\' || value[2] == L'/');
}

static bool NormalizeFullPath(const std::wstring& path, std::wstring& fullPath)
{
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
    {
        return false;
    }

    std::vector<wchar_t> buffer(needed + 1);
    DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size())
    {
        return false;
    }

    fullPath.assign(buffer.data(), written);
    return true;
}

static bool IsDriveRoot(const std::wstring& path)
{
    return path.size() == 3 &&
        iswalpha(path[0]) &&
        path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/');
}

static std::wstring TrimTrailingDirectorySeparators(std::wstring path)
{
    while (path.size() > 3 &&
        (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/'))
    {
        path.resize(path.size() - 1);
    }
    return path;
}

static bool IsUncShareRoot(const std::wstring& path)
{
    if (path.size() < 5 || path[0] != L'\\' || path[1] != L'\\')
    {
        return false;
    }

    size_t serverEnd = path.find_first_of(L"\\/", 2);
    if (serverEnd == std::wstring::npos || serverEnd + 1 >= path.size())
    {
        return false;
    }

    size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
    return shareEnd == std::wstring::npos;
}

static std::wstring ParentDirectoryForTrust(std::wstring path)
{
    path = TrimTrailingDirectorySeparators(path);
    if (path.empty() || IsDriveRoot(path) || IsUncShareRoot(path))
    {
        return L"";
    }

    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return L"";
    }
    if (pos == 2 && path[1] == L':')
    {
        return path.substr(0, 3);
    }
    return path.substr(0, pos);
}

static bool TrustedExistingAncestorDirectories(std::wstring path)
{
    path = TrimTrailingDirectorySeparators(path);
    bool strictCurrentDirectory = true;
    while (!path.empty())
    {
        if (!DirectoryExists(path) || !PathSecurityIsTrusted(path, !strictCurrentDirectory))
        {
            return false;
        }

        std::wstring parent = ParentDirectoryForTrust(path);
        if (parent.empty() || parent == path)
        {
            break;
        }
        path = parent;
        strictCurrentDirectory = false;
    }

    return true;
}

static bool TrustedExistingFilePath(const std::wstring& path, std::wstring& error, const wchar_t* label)
{
    if (!IsAbsoluteFileSystemPath(path))
    {
        error = std::wstring(label) + L" path is not a local absolute path: " + path;
        return false;
    }

    std::wstring fullPath;
    if (!NormalizeFullPath(path, fullPath))
    {
        error = std::wstring(label) + L" path could not be normalized: " + path;
        return false;
    }

    if (!FileExists(fullPath))
    {
        error = std::wstring(label) + L" file does not exist: " + path;
        return false;
    }

    if (!PathSecurityIsTrusted(fullPath))
    {
        error = std::wstring(label) + L" is not trusted. It must be owned by SYSTEM, Administrators, or TrustedInstaller and not writable by non-admin principals: " + fullPath;
        return false;
    }

    std::wstring dir = DirectoryOf(fullPath);
    if (dir.empty() || !TrustedExistingAncestorDirectories(dir))
    {
        error = std::wstring(label) + L" directory chain is not trusted: " + dir;
        return false;
    }

    return true;
}

static bool TrustedExistingDirectoryPath(const std::wstring& path, std::wstring& error, const wchar_t* label)
{
    if (!IsAbsoluteFileSystemPath(path))
    {
        error = std::wstring(label) + L" path is not a local absolute path: " + path;
        return false;
    }

    std::wstring fullPath;
    if (!NormalizeFullPath(path, fullPath))
    {
        error = std::wstring(label) + L" path could not be normalized: " + path;
        return false;
    }

    if (!TrustedExistingAncestorDirectories(fullPath))
    {
        error = std::wstring(label) + L" directory chain is not trusted: " + fullPath;
        return false;
    }

    return true;
}

static std::wstring FindConfigPath()
{
    std::wstring exeDir = DirectoryOf(CurrentExePath());
    std::wstring parent = ParentDirectory(exeDir);
    std::wstring inExeDir = CombinePath(exeDir, kConfigFileName);
    std::wstring inParent = parent.empty() ? L"" : CombinePath(parent, kConfigFileName);

    if (FileExists(inExeDir)) {
        return inExeDir;
    }
    if (!inParent.empty() && FileExists(inParent)) {
        return inParent;
    }
    return inExeDir;
}

static std::wstring ReadIniString(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& defaultValue)
{
    DWORD size = 512;
    for (;;) {
        std::vector<wchar_t> buffer(size);
        DWORD read = GetPrivateProfileStringW(
            section.c_str(),
            key.c_str(),
            defaultValue.c_str(),
            buffer.data(),
            size,
            path.c_str());

        if (read < size - 2 || size >= 32768) {
            return Trim(std::wstring(buffer.data(), read));
        }
        size *= 2;
    }
}

static bool ReadIniBool(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    bool defaultValue)
{
    std::wstring raw = ReadIniString(path, section, key, defaultValue ? L"1" : L"0");
    std::transform(raw.begin(), raw.end(), raw.begin(), towlower);
    return raw == L"1" || raw == L"true" || raw == L"yes" || raw == L"on";
}

static DWORD ReadIniDword(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    DWORD defaultValue)
{
    std::wstring raw = ReadIniString(path, section, key, L"");
    if (raw.empty()) {
        return defaultValue;
    }

    wchar_t* end = nullptr;
    unsigned long value = wcstoul(raw.c_str(), &end, 10);
    return end && *end == L'\0' ? static_cast<DWORD>(value) : defaultValue;
}

static int ReadIniInt(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    int defaultValue)
{
    std::wstring raw = ReadIniString(path, section, key, L"");
    if (raw.empty()) {
        return defaultValue;
    }

    wchar_t* end = nullptr;
    long value = wcstol(raw.c_str(), &end, 10);
    return end && *end == L'\0' ? static_cast<int>(value) : defaultValue;
}

static void MarkUntrusted(GateConfig& config, const std::wstring& error)
{
    config.trusted = false;
    config.trustError = error;
}

static GateConfig LoadConfig(bool enforceTrust = true)
{
    GateConfig config;
    std::wstring trustError;
    if (enforceTrust && !TrustedExistingFilePath(CurrentExePath(), trustError, L"Password launcher executable"))
    {
        MarkUntrusted(config, trustError);
        return config;
    }

    config.configPath = FindConfigPath();
    if (enforceTrust && !FileExists(config.configPath))
    {
        MarkUntrusted(config, L"Password launcher config file does not exist: " + config.configPath + L"\n\nRun SecureDesktopPasswordLauncher.exe set-password from a trusted install directory first.");
        return config;
    }
    if (enforceTrust && !TrustedExistingFilePath(config.configPath, trustError, L"Password launcher config"))
    {
        MarkUntrusted(config, trustError);
        return config;
    }

    config.kdf = ReadIniString(config.configPath, L"Security", L"Kdf", L"");
    config.kdfIterations = ReadIniDword(config.configPath, L"Security", L"Iterations", kPasswordKdfIterations);
    if (config.kdfIterations == 0) {
        config.kdfIterations = kPasswordKdfIterations;
    }
    if (config.kdfIterations > kPasswordKdfMaxIterations) {
        MarkUntrusted(config, L"Password KDF iteration count is too large: " + std::to_wstring(config.kdfIterations));
        return config;
    }
    config.saltHex = ReadIniString(config.configPath, L"Security", L"SaltHex", L"");
    config.hashHex = ReadIniString(config.configPath, L"Security", L"PasswordHashHex", L"");
    config.pbkdf2HashHex = ReadIniString(config.configPath, L"Security", L"PasswordPbkdf2HashHex", L"");
    config.keepLegacySha256Hash = ReadIniBool(config.configPath, L"Security", L"KeepLegacySha256Hash", false);
    config.maxAttempts = ReadIniDword(config.configPath, L"Security", L"MaxAttempts", 3);
    config.lockoutSeconds = ReadIniDword(config.configPath, L"Security", L"LockoutSeconds", 30);
    config.launchPath = ReadIniString(config.configPath, L"Launch", L"Path", config.launchPath);
    config.arguments = ReadIniString(config.configPath, L"Launch", L"Arguments", L"");
    config.workingDirectory = ReadIniString(config.configPath, L"Launch", L"WorkingDirectory", config.workingDirectory);
    config.desktop = ReadIniString(config.configPath, L"Launch", L"Desktop", config.desktop);
    config.showWindow = ReadIniInt(config.configPath, L"Launch", L"ShowWindow", SW_SHOWNORMAL);
    config.startMinimized = ReadIniBool(config.configPath, L"UI", L"StartMinimized", true);
    config.topMost = ReadIniBool(config.configPath, L"UI", L"TopMost", false);
    config.autoLockMinutes = ReadIniDword(config.configPath, L"UI", L"AutoLockMinutes", 0);
    if (enforceTrust &&
        (!TrustedExistingFilePath(config.launchPath, trustError, L"Launch target") ||
         (!config.workingDirectory.empty() && !TrustedExistingDirectoryPath(config.workingDirectory, trustError, L"Launch working directory"))))
    {
        MarkUntrusted(config, trustError);
        return config;
    }

    return config;
}

static std::wstring BytesToHex(const std::vector<BYTE>& bytes)
{
    static const wchar_t* digits = L"0123456789abcdef";
    std::wstring result;
    result.reserve(bytes.size() * 2);
    for (BYTE value : bytes) {
        result.push_back(digits[(value >> 4) & 0x0f]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

static bool HexToBytes(const std::wstring& hex, std::vector<BYTE>& bytes)
{
    auto hexValue = [](wchar_t ch) -> int {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    };

    if ((hex.size() % 2) != 0) {
        return false;
    }

    bytes.clear();
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexValue(hex[i]);
        int lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        bytes.push_back(static_cast<BYTE>((hi << 4) | lo));
    }
    return true;
}

static bool GenerateRandomBytes(std::vector<BYTE>& bytes)
{
    return BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

static bool HashPasswordLegacySha256(const std::vector<BYTE>& salt, const std::wstring& password, std::vector<BYTE>& hash)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    DWORD objectSize = 0;
    DWORD dataSize = 0;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }

    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &dataSize, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    std::vector<BYTE> object(objectSize);
    hash.assign(32, 0);

    if (BCryptCreateHash(alg, &hashHandle, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0) == 0 &&
        BCryptHashData(hashHandle, const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()), 0) == 0) {
        const BYTE* passwordBytes = reinterpret_cast<const BYTE*>(password.data());
        ULONG passwordByteCount = static_cast<ULONG>(password.size() * sizeof(wchar_t));
        if (BCryptHashData(hashHandle, const_cast<PUCHAR>(passwordBytes), passwordByteCount, 0) == 0 &&
            BCryptFinishHash(hashHandle, hash.data(), static_cast<ULONG>(hash.size()), 0) == 0) {
            ok = true;
        }
    }

    if (hashHandle) {
        BCryptDestroyHash(hashHandle);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static bool DerivePasswordHash(
    const std::vector<BYTE>& salt,
    const std::wstring& password,
    DWORD iterations,
    std::vector<BYTE>& hash)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    hash.assign(32, 0);

    if (iterations == 0 || BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) {
        return false;
    }

    const BYTE* passwordBytes = reinterpret_cast<const BYTE*>(password.data());
    ULONG passwordByteCount = static_cast<ULONG>(password.size() * sizeof(wchar_t));
    NTSTATUS status = BCryptDeriveKeyPBKDF2(
        alg,
        const_cast<PUCHAR>(passwordBytes),
        passwordByteCount,
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        iterations,
        hash.data(),
        static_cast<ULONG>(hash.size()),
        0);

    BCryptCloseAlgorithmProvider(alg, 0);
    return status == 0;
}

static bool ConstantTimeEquals(const std::vector<BYTE>& left, const std::vector<BYTE>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    BYTE diff = 0;
    for (size_t i = 0; i < left.size(); ++i) {
        diff |= left[i] ^ right[i];
    }
    return diff == 0;
}

static LRESULT CALLBACK PasswordWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<PasswordDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE: {
        state = reinterpret_cast<PasswordDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        HWND label = CreateWindowExW(0, L"STATIC", state->prompt.c_str(), WS_CHILD | WS_VISIBLE,
            16, 16, 340, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        state->edit1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
            16, 44, 340, 24, hwnd, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
        SendMessageW(state->edit1, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        int buttonY = 84;
        if (state->confirm) {
            HWND label2 = CreateWindowExW(0, L"STATIC", L"Confirm password:", WS_CHILD | WS_VISIBLE,
                16, 82, 340, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(label2, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            state->edit2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
                16, 110, 340, 24, hwnd, reinterpret_cast<HMENU>(1002), nullptr, nullptr);
            SendMessageW(state->edit2, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            buttonY = 150;
        }

        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            272, buttonY, 84, 28, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(state->edit1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && state) {
            wchar_t first[512] = {};
            wchar_t second[512] = {};
            GetWindowTextW(state->edit1, first, static_cast<int>(std::size(first)));
            if (state->confirm) {
                GetWindowTextW(state->edit2, second, static_cast<int>(std::size(second)));
                if (wcscmp(first, second) != 0) {
                    MessageBoxW(hwnd, L"The passwords do not match.", L"Password", MB_OK | MB_ICONERROR);
                    return 0;
                }
                if (first[0] == L'\0') {
                    MessageBoxW(hwnd, L"Password cannot be empty.", L"Password", MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            state->password = first;
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && state && state->edit1) {
            SetFocus(state->edit1);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowPasswordDialog(const std::wstring& title, const std::wstring& prompt, bool confirm, bool startMinimized, bool topMost, std::wstring& password)
{
    PasswordDialogState state;
    state.title = title;
    state.prompt = prompt;
    state.confirm = confirm;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"SecureDesktopPasswordLauncherWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = PasswordWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    int height = confirm ? 230 : 164;
    DWORD exStyle = WS_EX_APPWINDOW;
    if (topMost) {
        exStyle |= WS_EX_TOPMOST;
    }

    HWND hwnd = CreateWindowExW(
        exStyle,
        className,
        title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        390,
        height,
        nullptr,
        nullptr,
        instance,
        &state);

    if (!hwnd) {
        return false;
    }

    RECT rect = {};
    GetWindowRect(hwnd, &rect);
    int width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, topMost ? HWND_TOPMOST : HWND_NOTOPMOST, (screenX - width) / 2, (screenY - height) / 2, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(hwnd, startMinimized ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (state.accepted) {
        password = state.password;
    }
    return state.accepted;
}

static int SetPassword()
{
    GateConfig config = LoadConfig(false);
    std::wstring password;
    if (!ShowPasswordDialog(L"Set Secure Desktop Password", L"New password:", true, false, true, password)) {
        return 1;
    }

    std::vector<BYTE> salt(16);
    std::vector<BYTE> legacyHash;
    std::vector<BYTE> pbkdf2Hash;
    if (!GenerateRandomBytes(salt) ||
        !DerivePasswordHash(salt, password, kPasswordKdfIterations, pbkdf2Hash) ||
        (config.keepLegacySha256Hash && !HashPasswordLegacySha256(salt, password, legacyHash))) {
        MessageBoxW(nullptr, L"Failed to generate password hash.", L"Password", MB_OK | MB_ICONERROR);
        return 1;
    }

    WritePrivateProfileStringW(L"Security", L"Kdf", kPasswordKdfName, config.configPath.c_str());
    WritePrivateProfileStringW(L"Security", L"Iterations", std::to_wstring(kPasswordKdfIterations).c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Security", L"SaltHex", BytesToHex(salt).c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Security", L"KeepLegacySha256Hash", config.keepLegacySha256Hash ? L"1" : L"0", config.configPath.c_str());
    if (config.keepLegacySha256Hash) {
        WritePrivateProfileStringW(L"Security", L"PasswordHashHex", BytesToHex(legacyHash).c_str(), config.configPath.c_str());
    } else {
        WritePrivateProfileStringW(L"Security", L"PasswordHashHex", nullptr, config.configPath.c_str());
    }
    WritePrivateProfileStringW(L"Security", L"PasswordPbkdf2HashHex", BytesToHex(pbkdf2Hash).c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Security", L"MaxAttempts", std::to_wstring(config.maxAttempts).c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Security", L"LockoutSeconds", std::to_wstring(config.lockoutSeconds).c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Launch", L"Path", config.launchPath.c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Launch", L"Arguments", config.arguments.c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Launch", L"WorkingDirectory", config.workingDirectory.c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Launch", L"Desktop", config.desktop.c_str(), config.configPath.c_str());
    WritePrivateProfileStringW(L"Launch", L"ShowWindow", L"1", config.configPath.c_str());
    WritePrivateProfileStringW(L"UI", L"AutoLockMinutes", std::to_wstring(config.autoLockMinutes).c_str(), config.configPath.c_str());

    MessageBoxW(nullptr, config.configPath.c_str(), L"Password saved", MB_OK | MB_ICONINFORMATION);
    return 0;
}

static std::wstring FriendlyNameForImage(const std::wstring& imagePath);

static bool VerifyPassword(const GateConfig& config, bool startMinimized)
{
    std::vector<BYTE> salt;
    std::vector<BYTE> expectedHash;
    bool hasPbkdf2Hash = _wcsicmp(config.kdf.c_str(), kPasswordKdfName) == 0 &&
        HexToBytes(config.pbkdf2HashHex, expectedHash) &&
        !expectedHash.empty();
    if (!HexToBytes(config.saltHex, salt) ||
        (!hasPbkdf2Hash && (!HexToBytes(config.hashHex, expectedHash) || expectedHash.empty())) ||
        salt.empty()) {
        MessageBoxW(nullptr, L"Password is not configured. Run this program with the set-password argument first.",
            L"Secure Desktop Password Launcher", MB_OK | MB_ICONERROR);
        return false;
    }

    DWORD attempts = std::max<DWORD>(1, config.maxAttempts);
    std::wstring promptTitle = L"Launching " + FriendlyNameForImage(config.launchPath) + L"...";
    for (DWORD attempt = 0; attempt < attempts; ++attempt) {
        std::wstring password;
        if (!ShowPasswordDialog(promptTitle, L"Password:", false, startMinimized, config.topMost, password)) {
            return false;
        }

        std::vector<BYTE> actualHash;
        bool hashed = hasPbkdf2Hash
            ? DerivePasswordHash(salt, password, config.kdfIterations, actualHash)
            : HashPasswordLegacySha256(salt, password, actualHash);
        if (hashed && ConstantTimeEquals(actualHash, expectedHash)) {
            return true;
        }

        MessageBoxW(nullptr, L"Incorrect password.", L"Secure Desktop Password Prompt", MB_OK | MB_ICONERROR);
    }

    return false;
}

static void SleepAfterFailedUnlock(const GateConfig& config)
{
    DWORD seconds = config.lockoutSeconds;
    if (seconds == 0) {
        seconds = 1;
    }
    if (seconds > 3600) {
        seconds = 3600;
    }
    Sleep(seconds * 1000UL);
}

static bool ContainsWindow(const std::vector<HWND>& windows, HWND hwnd)
{
    return std::find(windows.begin(), windows.end(), hwnd) != windows.end();
}

static bool ContainsProcessId(const std::vector<DWORD>& pids, DWORD pid)
{
    return std::find(pids.begin(), pids.end(), pid) != pids.end();
}

static void AddProcessId(std::vector<DWORD>& pids, DWORD pid)
{
    if (pid != 0 && !ContainsProcessId(pids, pid)) {
        pids.push_back(pid);
    }
}

static void TrackWindow(ControlWindowState* state, HWND hwnd)
{
    if (!state || !hwnd || hwnd == state->hwnd || ContainsWindow(state->trackedWindows, hwnd)) {
        return;
    }

    state->trackedWindows.push_back(hwnd);
}

static std::vector<DWORD> QueryJobProcessIds(HANDLE job)
{
    std::vector<DWORD> pids;
    if (!job) {
        return pids;
    }

    DWORD capacity = 32;
    for (;;) {
        DWORD bytes = FIELD_OFFSET(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList) +
            capacity * sizeof(ULONG_PTR);
        std::vector<BYTE> buffer(bytes);
        auto* info = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());

        if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, info, bytes, nullptr)) {
            DWORD count = (std::min)(info->NumberOfProcessIdsInList, info->NumberOfAssignedProcesses);
            for (DWORD i = 0; i < count; ++i) {
                pids.push_back(static_cast<DWORD>(info->ProcessIdList[i]));
            }

            if (info->NumberOfProcessIdsInList >= info->NumberOfAssignedProcesses) {
                break;
            }

            capacity = info->NumberOfAssignedProcesses + 8;
            continue;
        }

        if (GetLastError() != ERROR_MORE_DATA) {
            break;
        }

        capacity *= 2;
    }

    return pids;
}

static std::vector<DWORD> QueryOwnedProcessIds(ControlWindowState* state)
{
    std::vector<DWORD> pids;
    if (!state) {
        return pids;
    }

    pids = QueryJobProcessIds(state->job);
    for (const LaunchedProcess& process : state->processes) {
        AddProcessId(pids, process.pid);
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return pids;
    }

    bool changed = true;
    while (changed) {
        changed = false;

        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (ContainsProcessId(pids, pe.th32ParentProcessID) &&
                    !ContainsProcessId(pids, pe.th32ProcessID)) {
                    pids.push_back(pe.th32ProcessID);
                    changed = true;
                }
            } while (Process32NextW(snap, &pe));
        }
    }

    CloseHandle(snap);
    return pids;
}

struct ProcessWindowContext
{
    ControlWindowState* state = nullptr;
    std::vector<DWORD>* pids = nullptr;
};

static BOOL CALLBACK TrackProcessWindowProc(HWND hwnd, LPARAM lParam)
{
    auto* context = reinterpret_cast<ProcessWindowContext*>(lParam);
    if (!context || !context->state || !context->pids || hwnd == context->state->hwnd) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0 && ContainsProcessId(*context->pids, pid)) {
        TrackWindow(context->state, hwnd);
    }

    return TRUE;
}

static void AddWindowsForTrackedProcesses(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    std::vector<DWORD> pids = QueryOwnedProcessIds(state);
    ProcessWindowContext context = {};
    context.state = state;
    context.pids = &pids;
    EnumWindows(TrackProcessWindowProc, reinterpret_cast<LPARAM>(&context));
}

static void HideTrackedWindows(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    AddWindowsForTrackedProcesses(state);
    std::vector<DWORD> ownedPids = QueryOwnedProcessIds(state);
    for (HWND hwnd : state->trackedWindows) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (IsWindow(hwnd) && hwnd != state->hwnd && ContainsProcessId(ownedPids, pid)) {
            ShowWindow(hwnd, SW_HIDE);
        }
    }
}

static void RestoreTrackedWindows(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    AddWindowsForTrackedProcesses(state);
    std::vector<DWORD> ownedPids = QueryOwnedProcessIds(state);
    int showWindow = state->config.showWindow == SW_HIDE ? SW_SHOWNORMAL : state->config.showWindow;
    for (HWND hwnd : state->trackedWindows) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (IsWindow(hwnd) && hwnd != state->hwnd && ContainsProcessId(ownedPids, pid)) {
            ShowWindow(hwnd, showWindow);
        }
    }
}

static std::wstring QueryProcessImagePath(HANDLE process)
{
    if (!process) {
        return L"";
    }

    std::vector<wchar_t> path(32768);
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
        return L"";
    }

    return std::wstring(path.data(), size);
}

static std::wstring StripExeExtension(const std::wstring& baseName)
{
    const std::wstring extension = L".exe";
    if (baseName.size() > extension.size() &&
        _wcsicmp(baseName.c_str() + baseName.size() - extension.size(), extension.c_str()) == 0) {
        return baseName.substr(0, baseName.size() - extension.size());
    }

    return baseName;
}

static std::wstring QueryFileDescription(const std::wstring& path)
{
    if (path.empty()) {
        return L"";
    }

    DWORD handle = 0;
    DWORD versionSize = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (versionSize == 0) {
        return L"";
    }

    std::vector<BYTE> data(versionSize);
    if (!GetFileVersionInfoW(path.c_str(), 0, versionSize, data.data())) {
        return L"";
    }

    struct LangAndCodePage
    {
        WORD language;
        WORD codePage;
    };

    LangAndCodePage* translations = nullptr;
    UINT translationsBytes = 0;
    if (VerQueryValueW(
            data.data(),
            L"\\VarFileInfo\\Translation",
            reinterpret_cast<LPVOID*>(&translations),
            &translationsBytes) &&
        translations &&
        translationsBytes >= sizeof(LangAndCodePage)) {
        UINT count = translationsBytes / sizeof(LangAndCodePage);
        for (UINT i = 0; i < count; ++i) {
            wchar_t subBlock[96] = {};
            swprintf_s(
                subBlock,
                L"\\StringFileInfo\\%04x%04x\\FileDescription",
                translations[i].language,
                translations[i].codePage);

            LPWSTR description = nullptr;
            UINT descriptionChars = 0;
            if (VerQueryValueW(
                    data.data(),
                    subBlock,
                    reinterpret_cast<LPVOID*>(&description),
                    &descriptionChars) &&
                description &&
                descriptionChars > 0) {
                return Trim(description);
            }
        }
    }

    LPWSTR description = nullptr;
    UINT descriptionChars = 0;
    if (VerQueryValueW(
            data.data(),
            L"\\StringFileInfo\\040904b0\\FileDescription",
            reinterpret_cast<LPVOID*>(&description),
            &descriptionChars) &&
        description &&
        descriptionChars > 0) {
        return Trim(description);
    }

    return L"";
}

static std::wstring FriendlyNameForImage(const std::wstring& imagePath)
{
    std::wstring baseName = BaseName(imagePath);
    if (baseName.empty()) {
        return L"Program";
    }

    std::wstring displayName;
    if (_wcsicmp(baseName.c_str(), L"cmd.exe") == 0) {
        displayName = L"Command Prompt";
    } else {
        displayName = QueryFileDescription(imagePath);
    }

    if (displayName.empty()) {
        displayName = StripExeExtension(baseName);
    }

    return displayName;
}

static std::wstring ImageDisplayName(const std::wstring& imagePath)
{
    std::wstring baseName = BaseName(imagePath);
    std::wstring friendlyName = FriendlyNameForImage(imagePath);
    return baseName.empty() ? friendlyName : friendlyName + L" (" + baseName + L")";
}

static std::wstring Ellipsize(const std::wstring& value, size_t maxChars)
{
    if (value.size() <= maxChars || maxChars < 4) {
        return value;
    }

    return value.substr(0, maxChars - 3) + L"...";
}

static HICON LoadTargetIcon(const std::wstring& path, bool useSmallIcon, bool* destroyRequired)
{
    if (destroyRequired) {
        *destroyRequired = false;
    }

    SHFILEINFOW info = {};
    UINT flags = SHGFI_ICON | (useSmallIcon ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), flags) && info.hIcon) {
        if (destroyRequired) {
            *destroyRequired = true;
        }
        return info.hIcon;
    }

    return LoadIconW(nullptr, IDI_APPLICATION);
}

struct DisplayProcess
{
    DWORD pid = 0;
    std::wstring imagePath;
};

static HANDLE FindTrackedProcessHandle(ControlWindowState* state, DWORD pid)
{
    if (!state) {
        return nullptr;
    }

    for (const LaunchedProcess& process : state->processes) {
        if (process.pid == pid) {
            return process.process;
        }
    }

    return nullptr;
}

static std::wstring QueryProcessImagePathByPid(ControlWindowState* state, DWORD pid)
{
    HANDLE tracked = FindTrackedProcessHandle(state, pid);
    if (tracked) {
        return QueryProcessImagePath(tracked);
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return L"";
    }

    std::wstring imagePath = QueryProcessImagePath(process);
    CloseHandle(process);
    return imagePath;
}

static std::vector<DisplayProcess> QueryDisplayedProcesses(ControlWindowState* state)
{
    std::vector<DisplayProcess> displayProcesses;
    if (!state) {
        return displayProcesses;
    }

    std::vector<DWORD> pids = QueryOwnedProcessIds(state);
    for (DWORD pid : pids) {
        DisplayProcess process;
        process.pid = pid;
        process.imagePath = QueryProcessImagePathByPid(state, pid);
        displayProcesses.push_back(process);
    }

    return displayProcesses;
}

static void PruneExitedProcesses(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    std::vector<LaunchedProcess> running;
    for (LaunchedProcess& process : state->processes) {
        if (!process.process) {
            continue;
        }

        DWORD wait = WaitForSingleObject(process.process, 0);
        if (wait == WAIT_TIMEOUT) {
            running.push_back(process);
        } else {
            if (process.wait) {
                UnregisterWaitEx(process.wait, INVALID_HANDLE_VALUE);
                process.wait = nullptr;
            }
            CloseHandle(process.process);
        }
    }

    state->processes.swap(running);
}

static std::wstring RunningProcessText(size_t count)
{
    if (count == 0) {
        return L"No running processes";
    }

    return std::to_wstring(count) + (count == 1 ? L" running process" : L" running processes");
}

static void DestroyProcessRows(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    for (HWND row : state->processRows) {
        if (IsWindow(row)) {
            DestroyWindow(row);
        }
    }
    state->processRows.clear();

    for (HICON icon : state->rowIcons) {
        if (icon) {
            DestroyIcon(icon);
        }
    }
    state->rowIcons.clear();
}

static void ResizeControlWindow(ControlWindowState* state, size_t rowCount)
{
    if (!state || !state->hwnd) {
        return;
    }

    size_t visibleRows = (std::max)(rowCount, static_cast<size_t>(1));
    int buttonY = 44 + static_cast<int>(visibleRows) * 54 + 18;

    if (state->quickLock) {
        MoveWindow(state->quickLock, 16, buttonY, 190, 30, TRUE);
    }
    if (state->openProgram) {
        MoveWindow(state->openProgram, 218, buttonY, 190, 30, TRUE);
    }
    if (state->exitButton) {
        MoveWindow(state->exitButton, 420, buttonY, 84, 30, TRUE);
    }

    RECT desired = { 0, 0, 620, buttonY + 48 };
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(state->hwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(state->hwnd, GWL_EXSTYLE));
    AdjustWindowRectEx(&desired, style, FALSE, exStyle);
    SetWindowPos(
        state->hwnd,
        nullptr,
        0,
        0,
        desired.right - desired.left,
        desired.bottom - desired.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void AddProcessRow(ControlWindowState* state, HFONT font, int y, const DisplayProcess& process)
{
    std::wstring imagePath = process.imagePath.empty() ? L"(unavailable)" : process.imagePath;

    HWND icon = CreateWindowExW(
        0,
        L"STATIC",
        nullptr,
        WS_CHILD | WS_VISIBLE | SS_ICON,
        16,
        y + 4,
        32,
        32,
        state->hwnd,
        nullptr,
        nullptr,
        nullptr);
    bool destroyIcon = false;
    HICON rowIcon = process.imagePath.empty()
        ? state->largeIcon
        : LoadTargetIcon(process.imagePath, false, &destroyIcon);
    if (icon && rowIcon) {
        SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(rowIcon), 0);
    }
    if (destroyIcon && rowIcon) {
        state->rowIcons.push_back(rowIcon);
    }

    std::wstring processName = process.imagePath.empty() ? L"Process" : ImageDisplayName(process.imagePath);
    std::wstring title = processName + L", PID " + std::to_wstring(process.pid);
    HWND titleControl = CreateWindowExW(
        0,
        L"STATIC",
        title.c_str(),
        WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
        60,
        y,
        520,
        20,
        state->hwnd,
        nullptr,
        nullptr,
        nullptr);

    std::wstring path = L"Path: " + imagePath;
    HWND pathControl = CreateWindowExW(
        0,
        L"STATIC",
        path.c_str(),
        WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
        60,
        y + 24,
        520,
        20,
        state->hwnd,
        nullptr,
        nullptr,
        nullptr);

    SendMessageW(titleControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(pathControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    state->processRows.push_back(icon);
    state->processRows.push_back(titleControl);
    state->processRows.push_back(pathControl);
}

static void AddEmptyProcessRow(ControlWindowState* state, HFONT font)
{
    HWND empty = CreateWindowExW(
        0,
        L"STATIC",
        L"No opened processes are currently running.",
        WS_CHILD | WS_VISIBLE,
        16,
        44,
        568,
        22,
        state->hwnd,
        nullptr,
        nullptr,
        nullptr);
    SendMessageW(empty, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    state->processRows.push_back(empty);
}

static void UpdateProcessListDisplay(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    PruneExitedProcesses(state);

    std::vector<DisplayProcess> displayProcesses = QueryDisplayedProcesses(state);
    size_t count = displayProcesses.size();
    std::wstring titleText = RunningProcessText(count);
    if (state->hwnd) {
        SetWindowTextW(state->hwnd, titleText.c_str());
    }

    if (state->processCount) {
        std::wstring countText = titleText + L":";
        SetWindowTextW(state->processCount, countText.c_str());
    }

    DestroyProcessRows(state);
    ResizeControlWindow(state, count);

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (count == 0) {
        AddEmptyProcessRow(state, font);
        return;
    }

    int y = 44;
    for (const DisplayProcess& process : displayProcesses) {
        AddProcessRow(state, font, y, process);
        y += 54;
    }
}

static HANDLE CreateKillOnCloseJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        return nullptr;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limitInfo = {};
    limitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limitInfo, sizeof(limitInfo))) {
        CloseHandle(job);
        return nullptr;
    }

    return job;
}

static void ReleaseTrackedProcessHandles(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    for (LaunchedProcess& process : state->processes) {
        if (process.wait) {
            UnregisterWaitEx(process.wait, INVALID_HANDLE_VALUE);
            process.wait = nullptr;
        }
        if (process.process) {
            CloseHandle(process.process);
            process.process = nullptr;
        }
    }
    state->processes.clear();
}

static bool ClearSpawnedProcesses(ControlWindowState* state)
{
    if (!state) {
        return false;
    }

    ReleaseTrackedProcessHandles(state);
    if (state->job) {
        CloseHandle(state->job);
        state->job = nullptr;
    }

    state->trackedWindows.clear();
    DestroyProcessRows(state);
    state->job = CreateKillOnCloseJob();
    return state->job != nullptr;
}

static void CleanupControlWindowState(ControlWindowState* state)
{
    if (!state) {
        return;
    }

    DestroyProcessRows(state);
    ReleaseTrackedProcessHandles(state);

    if (state->job) {
        CloseHandle(state->job);
        state->job = nullptr;
    }

    if (state->destroyLargeIcon && state->largeIcon) {
        DestroyIcon(state->largeIcon);
        state->largeIcon = nullptr;
    }
    if (state->destroySmallIcon && state->smallIcon) {
        DestroyIcon(state->smallIcon);
        state->smallIcon = nullptr;
    }
}

static void CALLBACK ProcessExitedCallback(PVOID context, BOOLEAN)
{
    HWND hwnd = reinterpret_cast<HWND>(context);
    if (hwnd) {
        PostMessageW(hwnd, kProcessExitedMessage, 0, 0);
    }
}

static bool LaunchConfiguredTarget(ControlWindowState* state)
{
    if (!state || !state->job) {
        return false;
    }

    std::wstring trustError;
    if (!TrustedExistingFilePath(state->config.launchPath, trustError, L"Launch target") ||
        (!state->config.workingDirectory.empty() && !TrustedExistingDirectoryPath(state->config.workingDirectory, trustError, L"Launch working directory")))
    {
        MessageBoxW(state->hwnd, trustError.c_str(), L"Secure Desktop Password Launcher", MB_OK | MB_ICONERROR);
        return false;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.lpDesktop = const_cast<LPWSTR>(state->config.desktop.c_str());
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = static_cast<WORD>(state->config.showWindow);

    PROCESS_INFORMATION pi = {};
    std::wstring commandLine = Quote(state->config.launchPath);
    if (!state->config.arguments.empty()) {
        commandLine += L" " + state->config.arguments;
    }

    DWORD creationFlags = CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED;
    BOOL ok = CreateProcessW(
        state->config.launchPath.c_str(),
        &commandLine[0],
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        nullptr,
        state->config.workingDirectory.empty() ? nullptr : state->config.workingDirectory.c_str(),
        &si,
        &pi);

    if (!ok) {
        MessageBoxW(state->hwnd, L"Failed to launch target program.", L"Secure Desktop Password Launcher", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!AssignProcessToJobObject(state->job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        MessageBoxW(state->hwnd, L"Failed to attach target program to the job object.", L"Secure Desktop Password Launcher", MB_OK | MB_ICONERROR);
        return false;
    }

    ResumeThread(pi.hThread);

    LaunchedProcess launched = {};
    launched.process = pi.hProcess;
    launched.pid = pi.dwProcessId;
    RegisterWaitForSingleObject(
        &launched.wait,
        launched.process,
        ProcessExitedCallback,
        state->hwnd,
        INFINITE,
        WT_EXECUTEONLYONCE);
    state->processes.push_back(launched);

    CloseHandle(pi.hThread);

    WaitForInputIdle(pi.hProcess, 1500);
    Sleep(300);
    AddWindowsForTrackedProcesses(state);
    UpdateProcessListDisplay(state);
    return true;
}

static void QuickLock(ControlWindowState* state)
{
    if (!state || state->lockInProgress) {
        return;
    }

    state->lockInProgress = true;
    HideTrackedWindows(state);
    if (state->hwnd) {
        ShowWindow(state->hwnd, SW_HIDE);
    }

    while (!VerifyPassword(state->config, false)) {
        SleepAfterFailedUnlock(state->config);
    }

    RestoreTrackedWindows(state);
    if (state->hwnd) {
        ShowWindow(state->hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(state->hwnd);
    }
    state->lockInProgress = false;
}

static void ResetToPasswordPrompt(ControlWindowState* state)
{
    if (!state || state->lockInProgress) {
        return;
    }

    state->lockInProgress = true;
    if (state->hwnd) {
        ShowWindow(state->hwnd, SW_HIDE);
    }

    if (!ClearSpawnedProcesses(state)) {
        MessageBoxW(state->hwnd, L"Failed to reset the process job.", L"Secure Desktop Password Launcher", MB_OK | MB_ICONERROR);
        if (state->hwnd) {
            ShowWindow(state->hwnd, SW_SHOWNORMAL);
        }
        state->lockInProgress = false;
        return;
    }

    while (!VerifyPassword(state->config, false)) {
        SleepAfterFailedUnlock(state->config);
    }

    if (state->hwnd) {
        ShowWindow(state->hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(state->hwnd);
    }

    state->lockInProgress = false;
    LaunchConfiguredTarget(state);
}

static bool ShouldAutoLock(ControlWindowState* state)
{
    if (!state || state->lockInProgress || state->config.autoLockMinutes == 0) {
        return false;
    }

    if (QueryDisplayedProcesses(state).empty()) {
        return false;
    }

    LASTINPUTINFO input = {};
    input.cbSize = sizeof(input);
    if (!GetLastInputInfo(&input)) {
        return false;
    }

    ULONGLONG timeoutMs64 = static_cast<ULONGLONG>(state->config.autoLockMinutes) * 60ULL * 1000ULL;
    DWORD timeoutMs = timeoutMs64 > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : static_cast<DWORD>(timeoutMs64);
    DWORD idleMs = GetTickCount() - input.dwTime;
    return idleMs >= timeoutMs;
}

static LRESULT CALLBACK ControlWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<ControlWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        state = reinterpret_cast<ControlWindowState*>(create->lpCreateParams);
        if (state) {
            state->hwnd = hwnd;
        }
        return TRUE;
    }
    case WM_CREATE: {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (state) {
            state->processCount = CreateWindowExW(
                0,
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                16,
                16,
                568,
                18,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            SendMessageW(state->processCount, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }

        std::wstring openText = L"Open " + Ellipsize(state ? state->targetFriendlyName : L"Program", 28);
        HWND quickLock = CreateWindowExW(0, L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            16, 116, 190, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQuickLockButtonId)), nullptr, nullptr);
        HWND openProgram = CreateWindowExW(0, L"BUTTON", openText.c_str(), WS_CHILD | WS_VISIBLE,
            218, 116, 190, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenProgramButtonId)), nullptr, nullptr);
        if (state) {
            state->quickLock = quickLock;
            state->openProgram = openProgram;
        }
        HWND exit = CreateWindowExW(0, L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE,
            420, 116, 84, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExitButtonId)), nullptr, nullptr);
        if (state) {
            state->exitButton = exit;
        }
        SendMessageW(quickLock, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(openProgram, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(exit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (state && state->config.autoLockMinutes > 0) {
            SetTimer(hwnd, kAutoLockTimerId, 10000, nullptr);
        }
        UpdateProcessListDisplay(state);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kQuickLockButtonId) {
            QuickLock(state);
            UpdateProcessListDisplay(state);
            return 0;
        }
        if (LOWORD(wParam) == kOpenProgramButtonId) {
            LaunchConfiguredTarget(state);
            UpdateProcessListDisplay(state);
            return 0;
        }
        if (LOWORD(wParam) == kExitButtonId) {
            ResetToPasswordPrompt(state);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == kAutoLockTimerId && ShouldAutoLock(state)) {
            QuickLock(state);
            UpdateProcessListDisplay(state);
            return 0;
        }
        break;
    case kProcessExitedMessage:
        UpdateProcessListDisplay(state);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kAutoLockTimerId);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int RunControlWindow(const GateConfig& config)
{
    ControlWindowState state;
    state.config = config;
    state.targetFriendlyName = FriendlyNameForImage(config.launchPath);
    state.targetDisplayName = ImageDisplayName(config.launchPath);
    state.largeIcon = LoadTargetIcon(config.launchPath, false, &state.destroyLargeIcon);
    state.smallIcon = LoadTargetIcon(config.launchPath, true, &state.destroySmallIcon);
    state.job = CreateKillOnCloseJob();
    if (!state.job) {
        return 1;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"SecureDesktopPasswordLauncherControlWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = ControlWndProc;
    wc.hInstance = instance;
    wc.hIcon = state.largeIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    DWORD exStyle = WS_EX_APPWINDOW;
    if (config.topMost) {
        exStyle |= WS_EX_TOPMOST;
    }

    std::wstring windowTitle = RunningProcessText(0);
    HWND hwnd = CreateWindowExW(
        exStyle,
        className,
        windowTitle.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        620,
        190,
        nullptr,
        nullptr,
        instance,
        &state);

    if (!hwnd) {
        CleanupControlWindowState(&state);
        return 1;
    }

    if (state.largeIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(state.largeIcon));
    }
    if (state.smallIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(state.smallIcon));
    }

    RECT rect = {};
    GetWindowRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, config.topMost ? HWND_TOPMOST : HWND_NOTOPMOST, (screenX - width) / 2, (screenY - height) / 2, 0, 0, SWP_NOSIZE);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    LaunchConfiguredTarget(&state);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CleanupControlWindowState(&state);
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1 && _wcsicmp(argv[1], L"set-password") == 0) {
        LocalFree(argv);
        return SetPassword();
    }
    if (argv) {
        LocalFree(argv);
    }

    GateConfig config = LoadConfig();
    if (!config.trusted) {
        MessageBoxW(nullptr, config.trustError.c_str(), L"Secure Desktop Password Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!VerifyPassword(config, config.startMinimized)) {
        return 1;
    }

    return RunControlWindow(config);
}





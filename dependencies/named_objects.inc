#ifndef AIP_DEPENDENCIES_NAMED_OBJECTS_INC
#define AIP_DEPENDENCIES_NAMED_OBJECTS_INC

// Shared kernel-object names. INI coordination is scoped to one normalized
// file across accounts/sessions; interactive Startup coordination is per user.
// This changes naming only. Standard object ACLs and access errors remain.
#include <windows.h>
#include <sddl.h>
#include <wincrypt.h>
#include <algorithm>
#include <string>
#include <vector>
#pragma comment(lib, "advapi32.lib")

namespace aip { namespace named_objects {
inline bool UpperInvariant(const std::wstring& value, std::wstring& result) {
    if (value.empty()) { result.clear(); return true; }
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!size) return false;
    std::wstring upper(static_cast<std::size_t>(size), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), static_cast<int>(value.size()), &upper[0], size, nullptr, nullptr, 0) != size) return false;
    result = std::move(upper); return true;
}
inline bool NormalizeFileIdentity(const std::wstring& path, std::wstring& result) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos) { SetLastError(ERROR_INVALID_NAME); return false; }
    auto input = path;
    std::replace(input.begin(), input.end(), L'/', L'\\');
    DWORD size = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (!size) return false;
    std::vector<wchar_t> buffer(size);
    DWORD length = GetFullPathNameW(input.c_str(), size, buffer.data(), nullptr);
    if (!length || length >= size) { if (length >= size) SetLastError(ERROR_INSUFFICIENT_BUFFER); return false; }
    std::wstring full(buffer.data(), length);
    while (!full.empty() && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
    return UpperInvariant(full, result);
}
inline bool StableIdentityHash(const std::wstring& value, std::wstring& result) {
    std::string bytes;
    if (!value.empty()) {
        int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (!size) return false;
        bytes.resize(static_cast<std::size_t>(size));
        if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &bytes[0], size, nullptr, nullptr) != size) return false;
    }
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    BYTE digest[32]{}; DWORD length = sizeof(digest);
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
    bool success = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    if (success && !bytes.empty()) success = CryptHashData(hash, reinterpret_cast<const BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()), 0) != FALSE;
    if (success) success = CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) != FALSE && length == sizeof(digest);
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!success) { SetLastError(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error); return false; }
    static constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring text;
    for (int index = 0; index < 8; ++index) { text += digits[digest[index] >> 4]; text += digits[digest[index] & 15]; }
    result = std::move(text); return true;
}
inline bool ValidComponent(const std::wstring& component) {
    return !component.empty() && std::all_of(component.begin(), component.end(), [](wchar_t character) {
        return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z') ||
            (character >= L'0' && character <= L'9') || character == L'.' || character == L'-' || character == L'_';
    });
}
inline bool MachineScopedName(const std::wstring& component, const std::wstring& identity, std::wstring& result) {
    if (!ValidComponent(component)) { SetLastError(ERROR_INVALID_NAME); return false; }
    std::wstring hash;
    if (!StableIdentityHash(identity, hash)) return false;
    result = L"Global\\AIProjects." + component + L"." + hash; return true;
}
inline bool CurrentUserIdentity(std::wstring& result) {
    HANDLE token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
        if (GetLastError() != ERROR_NO_TOKEN || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (!size) { DWORD error = GetLastError(); CloseHandle(token); SetLastError(error); return false; }
    std::vector<BYTE> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        DWORD error = GetLastError(); CloseHandle(token); SetLastError(error); return false;
    }
    LPWSTR sid = nullptr;
    bool success = ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid) != FALSE;
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    if (!success) { SetLastError(error); return false; }
    std::wstring upper;
    success = UpperInvariant(sid, upper);
    error = success ? ERROR_SUCCESS : GetLastError();
    LocalFree(sid);
    if (!success) { SetLastError(error); return false; }
    result = L"SID:" + upper; return true;
}
inline bool CurrentUserScopedName(const std::wstring& component, const std::wstring& identity, std::wstring& result) {
    if (!ValidComponent(component)) { SetLastError(ERROR_INVALID_NAME); return false; }
    std::wstring user, userHash, identityHash;
    if (!CurrentUserIdentity(user) || !StableIdentityHash(user, userHash) || !StableIdentityHash(identity, identityHash)) return false;
    result = L"Global\\AIProjects." + component + L"." + userHash + L"." + identityHash; return true;
}
inline bool IniMutationName(const std::wstring& path, std::wstring& result) {
    std::wstring identity;
    return NormalizeFileIdentity(path, identity) && MachineScopedName(L"ConfigIni", identity, result);
}
} }
#endif

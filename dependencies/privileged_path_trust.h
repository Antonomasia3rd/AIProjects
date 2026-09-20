#ifndef AIP_DEPENDENCIES_PRIVILEGED_PATH_TRUST_H
#define AIP_DEPENDENCIES_PRIVILEGED_PATH_TRUST_H

#include "desktop_app_baseline.h"

#include <aclapi.h>
#include <shlobj.h>

#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace aip
{

enum class PrivilegedRootPolicy
{
    ProgramFilesOnly,
    WindowsOrProgramFiles,
};

struct PrivilegedPathPin
{
    // Retaining every component prevents ordinary name-based replacement after
    // validation. This cannot revoke dangerous handles opened before a
    // deployment was secured, so callers must begin from a protected state.
    std::vector<UniqueKernelHandle> handles;
    std::wstring finalPath;
};

namespace privileged_path_detail
{

inline const wchar_t* SafeLabel(const wchar_t* label)
{
    return label && *label ? label : L"privileged path";
}

inline std::wstring NormalizePath(std::wstring path)
{
    if (path.rfind(L"\\\\?\\", 0) == 0)
    {
        path.erase(0, 4);
    }
    for (wchar_t& ch : path)
    {
        if (ch == L'/')
        {
            ch = L'\\';
        }
    }
    return path;
}

inline bool IsAbsoluteFileSystemPath(const std::wstring& path)
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

inline bool HasAlternateDataStream(const std::wstring& path)
{
    return path.find(L':', 2) != std::wstring::npos;
}

inline bool NormalizeFullPath(const std::wstring& path, std::wstring& fullPath)
{
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
    {
        return false;
    }
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1);
    DWORD written = GetFullPathNameW(
        path.c_str(),
        static_cast<DWORD>(buffer.size()),
        buffer.data(),
        nullptr);
    if (written == 0 || written >= buffer.size())
    {
        return false;
    }
    fullPath.assign(buffer.data(), written);
    return true;
}

inline std::wstring TrimTrailingDirectorySeparators(std::wstring path)
{
    while (path.size() > 3 &&
        (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/'))
    {
        path.resize(path.size() - 1);
    }
    return path;
}

inline bool IsPathAtOrBelow(const std::wstring& path, const std::wstring& root)
{
    if (path.size() < root.size() ||
        _wcsnicmp(path.c_str(), root.c_str(), root.size()) != 0)
    {
        return false;
    }
    return path.size() == root.size() || path[root.size()] == L'\\';
}

inline bool FinalPathForHandle(HANDLE handle, std::wstring& finalPath)
{
    DWORD needed = GetFinalPathNameByHandleW(
        handle,
        nullptr,
        0,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (needed == 0)
    {
        return false;
    }
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1);
    DWORD written = GetFinalPathNameByHandleW(
        handle,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written >= buffer.size())
    {
        return false;
    }
    finalPath = NormalizePath(std::wstring(buffer.data(), written));
    return IsAbsoluteFileSystemPath(finalPath) &&
        !HasAlternateDataStream(finalPath);
}

inline bool CanonicalExistingDirectory(
    const std::wstring& path,
    std::wstring& canonicalPath)
{
    std::wstring fullPath;
    if (!IsAbsoluteFileSystemPath(path) ||
        HasAlternateDataStream(path) ||
        !NormalizeFullPath(path, fullPath))
    {
        return false;
    }

    fullPath = TrimTrailingDirectorySeparators(NormalizePath(fullPath));
    DWORD attributes = GetFileAttributesW(fullPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return false;
    }

    UniqueKernelHandle directory(CreateFileW(
        fullPath.c_str(),
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr));
    if (!directory)
    {
        return false;
    }

    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(directory.Get(), &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        !FinalPathForHandle(directory.Get(), canonicalPath))
    {
        return false;
    }
    canonicalPath = TrimTrailingDirectorySeparators(canonicalPath);
    return true;
}

inline void AddTrustedRoot(
    std::vector<std::wstring>& roots,
    const std::wstring& path)
{
    std::wstring canonical;
    if (!CanonicalExistingDirectory(path, canonical))
    {
        return;
    }
    for (const std::wstring& existing : roots)
    {
        if (_wcsicmp(existing.c_str(), canonical.c_str()) == 0)
        {
            return;
        }
    }
    roots.push_back(std::move(canonical));
}

inline void AddKnownFolderRoot(
    std::vector<std::wstring>& roots,
    REFKNOWNFOLDERID folderId)
{
    PWSTR path = nullptr;
    HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path);
    if (SUCCEEDED(result) && path && *path)
    {
        AddTrustedRoot(roots, path);
    }
    CoTaskMemFree(path);
}

inline std::vector<std::wstring> TrustedPrivilegedRoots(
    PrivilegedRootPolicy policy)
{
    std::vector<std::wstring> roots;
    AddKnownFolderRoot(roots, FOLDERID_ProgramFiles);
    AddKnownFolderRoot(roots, FOLDERID_ProgramFilesX86);
    AddKnownFolderRoot(roots, FOLDERID_ProgramFilesX64);

    if (policy == PrivilegedRootPolicy::WindowsOrProgramFiles)
    {
        std::vector<wchar_t> windowsBuffer(32768);
        UINT windowsLength = GetWindowsDirectoryW(
            windowsBuffer.data(),
            static_cast<UINT>(windowsBuffer.size()));
        if (windowsLength > 0 && windowsLength < windowsBuffer.size())
        {
            AddTrustedRoot(
                roots,
                std::wstring(windowsBuffer.data(), windowsLength));
        }
    }
    return roots;
}

inline bool FindTrustedRootForPath(
    const std::wstring& path,
    PrivilegedRootPolicy policy,
    std::wstring& root)
{
    root.clear();
    for (const std::wstring& candidate : TrustedPrivilegedRoots(policy))
    {
        if (IsPathAtOrBelow(path, candidate) && candidate.size() > root.size())
        {
            root = candidate;
        }
    }
    return !root.empty();
}

inline void AddWellKnownSid(
    std::vector<std::vector<BYTE>>& sids,
    WELL_KNOWN_SID_TYPE type)
{
    std::vector<BYTE> sid(SECURITY_MAX_SID_SIZE);
    DWORD sidSize = static_cast<DWORD>(sid.size());
    if (CreateWellKnownSid(type, nullptr, sid.data(), &sidSize))
    {
        sid.resize(sidSize);
        sids.push_back(std::move(sid));
    }
}

inline std::vector<std::vector<BYTE>> BuildTrustedWriterSids()
{
    std::vector<std::vector<BYTE>> sids;
    AddWellKnownSid(sids, WinLocalSystemSid);
    AddWellKnownSid(sids, WinBuiltinAdministratorsSid);

    DWORD sidSize = 0;
    DWORD domainSize = 0;
    SID_NAME_USE sidUse = SidTypeUnknown;
    LookupAccountNameW(
        nullptr,
        L"NT SERVICE\\TrustedInstaller",
        nullptr,
        &sidSize,
        nullptr,
        &domainSize,
        &sidUse);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && sidSize > 0)
    {
        std::vector<BYTE> sid(sidSize);
        std::vector<wchar_t> domain(domainSize == 0 ? 1 : domainSize);
        if (LookupAccountNameW(
            nullptr,
            L"NT SERVICE\\TrustedInstaller",
            sid.data(),
            &sidSize,
            domain.data(),
            &domainSize,
            &sidUse))
        {
            sid.resize(sidSize);
            sids.push_back(std::move(sid));
        }
    }
    return sids;
}

inline bool IsTrustedPrivilegedWriterSid(PSID sid)
{
    if (!sid || !IsValidSid(sid))
    {
        return false;
    }
    static const std::vector<std::vector<BYTE>> trustedSids =
        BuildTrustedWriterSids();
    for (const std::vector<BYTE>& trusted : trustedSids)
    {
        if (!trusted.empty() &&
            EqualSid(sid, const_cast<BYTE*>(trusted.data())))
        {
            return true;
        }
    }
    return false;
}

inline PSID SidFromAllowedAce(void* ace)
{
    if (!ace)
    {
        return nullptr;
    }

    ACE_HEADER* header = static_cast<ACE_HEADER*>(ace);
    BYTE* bytes = static_cast<BYTE*>(ace);
    const size_t simpleSidOffset = sizeof(ACE_HEADER) + sizeof(ACCESS_MASK);
    if (header->AceSize < simpleSidOffset + sizeof(DWORD))
    {
        return nullptr;
    }

    size_t sidOffset = 0;
    switch (header->AceType)
    {
    case ACCESS_ALLOWED_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
        sidOffset = simpleSidOffset;
        break;

    case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
    {
        const size_t flagsOffset = simpleSidOffset;
        if (header->AceSize < flagsOffset + sizeof(DWORD))
        {
            return nullptr;
        }
        DWORD flags = 0;
        std::memcpy(&flags, bytes + flagsOffset, sizeof(flags));
        sidOffset = flagsOffset + sizeof(DWORD);
        if ((flags & ACE_OBJECT_TYPE_PRESENT) != 0)
        {
            sidOffset += sizeof(GUID);
        }
        if ((flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) != 0)
        {
            sidOffset += sizeof(GUID);
        }
        break;
    }

    default:
        return nullptr;
    }

    constexpr size_t kSidHeaderBytes = 8;
    if (header->AceSize < sidOffset + kSidHeaderBytes)
    {
        return nullptr;
    }
    BYTE* sidBytes = bytes + sidOffset;
    DWORD required = GetSidLengthRequired(sidBytes[1]);
    if (required < kSidHeaderBytes || required > header->AceSize - sidOffset)
    {
        return nullptr;
    }
    PSID sid = sidBytes;
    return IsValidSid(sid) ? sid : nullptr;
}

inline bool AceCanGrantAccess(BYTE type)
{
    return type == ACCESS_ALLOWED_ACE_TYPE ||
        type == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
        type == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
        type == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE ||
        type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE;
}

inline bool HasDangerousWriteAccess(ACCESS_MASK mask, ACCESS_MASK dangerous)
{
    GENERIC_MAPPING mapping = {
        FILE_GENERIC_READ,
        FILE_GENERIC_WRITE,
        FILE_GENERIC_EXECUTE,
        FILE_ALL_ACCESS
    };
    MapGenericMask(&mask, &mapping);
    return (mask & dangerous) != 0;
}

inline bool HasProtectedOwnerAndDacl(
    HANDLE handle,
    const std::wstring& path,
    const wchar_t* label,
    ACCESS_MASK dangerous,
    std::wstring& error)
{
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD status = GetSecurityInfo(
        handle,
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner,
        nullptr,
        &dacl,
        nullptr,
        &descriptor);
    if (status != ERROR_SUCCESS)
    {
        error = std::wstring(L"Could not inspect ") + SafeLabel(label) +
            L" security: " + path + L" (error " +
            std::to_wstring(status) + L")";
        return false;
    }

    bool safe = true;
    if (!IsTrustedPrivilegedWriterSid(owner))
    {
        error = std::wstring(L"Refusing user-owned or unrecognized-owner ") +
            SafeLabel(label) + L": " + path;
        safe = false;
    }
    else if (!dacl)
    {
        error = std::wstring(L"Refusing ") + SafeLabel(label) +
            L" with an unrestricted DACL: " + path;
        safe = false;
    }
    else
    {
        ACL_SIZE_INFORMATION info = {};
        if (!GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation))
        {
            error = std::wstring(L"Could not inspect ") + SafeLabel(label) +
                L" DACL: " + path;
            safe = false;
        }
        else
        {
            for (DWORD index = 0; safe && index < info.AceCount; ++index)
            {
                void* rawAce = nullptr;
                if (!GetAce(dacl, index, &rawAce) || !rawAce)
                {
                    error = std::wstring(L"Could not inspect ") +
                        SafeLabel(label) + L" DACL entry: " + path;
                    safe = false;
                    break;
                }

                ACE_HEADER* header = static_cast<ACE_HEADER*>(rawAce);
                if ((header->AceFlags & INHERIT_ONLY_ACE) != 0 ||
                    !AceCanGrantAccess(header->AceType))
                {
                    continue;
                }
                if (header->AceSize <
                    sizeof(ACE_HEADER) + sizeof(ACCESS_MASK))
                {
                    error = std::wstring(L"Refusing malformed ") +
                        SafeLabel(label) + L" DACL: " + path;
                    safe = false;
                    break;
                }

                ACCESS_MASK mask = 0;
                std::memcpy(
                    &mask,
                    static_cast<BYTE*>(rawAce) + sizeof(ACE_HEADER),
                    sizeof(mask));
                if (!HasDangerousWriteAccess(mask, dangerous))
                {
                    continue;
                }

                PSID writer = SidFromAllowedAce(rawAce);
                if (!IsTrustedPrivilegedWriterSid(writer))
                {
                    error = std::wstring(L"Refusing ") + SafeLabel(label) +
                        L" writable by a non-privileged principal: " + path;
                    safe = false;
                }
            }
        }
    }

    LocalFree(descriptor);
    return safe;
}

inline bool OpenPinnedProtectedPath(
    const std::wstring& path,
    bool expectDirectory,
    PrivilegedRootPolicy policy,
    const wchar_t* label,
    PrivilegedPathPin& result,
    std::wstring& error)
{
    result.handles.clear();
    result.finalPath.clear();
    error.clear();

    std::wstring fullPath;
    if (!IsAbsoluteFileSystemPath(path) || HasAlternateDataStream(path))
    {
        error = std::wstring(SafeLabel(label)) +
            L" must use a local absolute path without alternate streams: " +
            path;
        return false;
    }
    if (!NormalizeFullPath(path, fullPath))
    {
        error = std::wstring(L"Could not normalize ") + SafeLabel(label) +
            L" path: " + path;
        return false;
    }
    fullPath = NormalizePath(fullPath);
    if (expectDirectory)
    {
        fullPath = TrimTrailingDirectorySeparators(fullPath);
    }

    std::wstring trustedRoot;
    if (!FindTrustedRootForPath(fullPath, policy, trustedRoot))
    {
        error = std::wstring(L"Refusing ") + SafeLabel(label) +
            (policy == PrivilegedRootPolicy::ProgramFilesOnly
                ? L" outside the Program Files directories: "
                : L" outside the Windows or Program Files directories: ") +
            fullPath;
        return false;
    }

    std::wstring current = fullPath.substr(0, 3);
    size_t cursor = 3;
    for (;;)
    {
        bool isLeaf = _wcsicmp(current.c_str(), fullPath.c_str()) == 0;
        bool isDirectory = !isLeaf || expectDirectory;
        DWORD access = FILE_READ_ATTRIBUTES | READ_CONTROL;
        if (isLeaf && !isDirectory)
        {
            access |= GENERIC_READ;
        }
        DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT |
            (isDirectory
                ? FILE_FLAG_BACKUP_SEMANTICS
                : static_cast<DWORD>(0));
        UniqueKernelHandle pin(CreateFileW(
            current.c_str(),
            access,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            flags,
            nullptr));
        if (!pin)
        {
            error = std::wstring(L"Could not pin ") + SafeLabel(label) +
                L" path component: " + current + L" (error " +
                std::to_wstring(GetLastError()) + L")";
            return false;
        }

        BY_HANDLE_FILE_INFORMATION info = {};
        if (!GetFileInformationByHandle(pin.Get(), &info) ||
            (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) !=
                isDirectory))
        {
            error = std::wstring(L"Refusing wrong-type ") + SafeLabel(label) +
                L" path component: " + current;
            return false;
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            error = std::wstring(L"Refusing reparse-point ") +
                SafeLabel(label) + L" path component: " + current;
            return false;
        }

        std::wstring componentFinalPath;
        if (!FinalPathForHandle(pin.Get(), componentFinalPath) ||
            _wcsicmp(
                TrimTrailingDirectorySeparators(componentFinalPath).c_str(),
                TrimTrailingDirectorySeparators(current).c_str()) != 0)
        {
            error = std::wstring(L"Refusing redirected or non-canonical ") +
                SafeLabel(label) + L" path component: " + current;
            return false;
        }

        ACCESS_MASK dangerous = DELETE | WRITE_DAC | WRITE_OWNER |
            FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;
        if (isDirectory)
        {
            dangerous |= FILE_DELETE_CHILD;
            if (IsPathAtOrBelow(current, trustedRoot))
            {
                // Sibling DLLs, plug-ins, and sidecars are part of the
                // privileged-code boundary once the trusted root is reached.
                dangerous |= FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
            }
        }
        if (isLeaf && !isDirectory)
        {
            dangerous |= FILE_WRITE_DATA | FILE_APPEND_DATA;
        }
        if (!HasProtectedOwnerAndDacl(
            pin.Get(),
            current,
            label,
            dangerous,
            error))
        {
            return false;
        }

        result.handles.push_back(std::move(pin));
        if (isLeaf)
        {
            result.finalPath = std::move(componentFinalPath);
            break;
        }

        while (cursor < fullPath.size() && fullPath[cursor] == L'\\')
        {
            ++cursor;
        }
        size_t separator = fullPath.find(L'\\', cursor);
        cursor = separator == std::wstring::npos
            ? fullPath.size()
            : separator;
        current = fullPath.substr(0, cursor);
    }

    if (!IsPathAtOrBelow(result.finalPath, trustedRoot))
    {
        error = std::wstring(L"Refusing ") + SafeLabel(label) +
            L" whose final path leaves the trusted root: " + result.finalPath;
        result.handles.clear();
        result.finalPath.clear();
        return false;
    }
    return true;
}

} // namespace privileged_path_detail

inline bool OpenPinnedProtectedFile(
    const std::wstring& path,
    PrivilegedRootPolicy policy,
    const wchar_t* label,
    PrivilegedPathPin& result,
    std::wstring& error)
{
    return privileged_path_detail::OpenPinnedProtectedPath(
        path,
        false,
        policy,
        label,
        result,
        error);
}

inline bool OpenPinnedProtectedDirectory(
    const std::wstring& path,
    PrivilegedRootPolicy policy,
    const wchar_t* label,
    PrivilegedPathPin& result,
    std::wstring& error)
{
    return privileged_path_detail::OpenPinnedProtectedPath(
        path,
        true,
        policy,
        label,
        result,
        error);
}

} // namespace aip

#endif

#pragma once
// Worker-only native inventory plus an injected portable policy. This module
// neither owns gallery pools nor deletes previously published thumbnails.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>

namespace aip { namespace gallery {
inline constexpr unsigned InventoryThumbnailVersion = 1;
using InventoryCancel = std::function<bool()>;
struct InventoryOrientation { unsigned clockwise = 0; bool horizontal = false, vertical = false; };
inline InventoryOrientation InventoryExifOrientation(unsigned tag) {
    switch (tag) {
    case 2: return {0, true, false};
    case 3: return {180, false, false};
    case 4: return {0, false, true};
    case 5: return {90, true, false}; // Rotate first, then reflect: transpose.
    case 6: return {90, false, false};
    case 7: return {90, false, true}; // Rotate first, then reflect: transverse.
    case 8: return {270, false, false};
    default: return {};
    }
}
struct InventoryOptions {
    std::size_t maxFiles = 4096, maxFileBytes = 64u * 1024u * 1024u, maxChangedFiles = 32, maxDiagnostics = 16;
    unsigned maxSourceDimension = 16384, thumbnailLongEdge = 620;
    std::uint64_t maxSourcePixels = 64u * 1024u * 1024u, maxScanMilliseconds = 2500;
};
struct InventoryImage { std::wstring id, thumbnailPath; };
struct InventoryEntry { std::wstring path; std::uint64_t size = 0, modified = 0; };
struct InventoryFile {
    std::wstring canonicalPath, fileIdentity, id, thumbnailPath, diagnostic;
    std::uint64_t size = 0, modified = 0;
    bool ready = false, permanentFailure = false;
};
struct InventoryIndex {
    std::wstring folderKey, optionsKey, listingKey, cacheDirectory;
    unsigned thumbnailLongEdge = 0, thumbnailFormatVersion = 0;
    std::size_t cursor = 0;
    std::map<std::wstring, InventoryFile> files;
};
struct InventoryResult {
    std::vector<InventoryImage> images;
    InventoryIndex index;
    std::vector<std::wstring> diagnostics;
    bool complete = false, cancelled = false;
    std::size_t skippedFiles = 0, deferredFiles = 0, hashedFiles = 0, decodedFiles = 0;
};
// Callback errors must be generic operation descriptions, never credentials,
// directory listings or private source paths. Native callbacks below obey this.
struct InventoryCallbacks {
    std::function<bool(const std::wstring&, InventoryFile&, std::wstring&)> metadata;
    std::function<bool(const std::wstring&, const InventoryFile&, std::vector<std::uint8_t>&, std::wstring&)> read;
    std::function<std::wstring(const std::vector<std::uint8_t>&)> hash;
    std::function<bool(const std::vector<std::uint8_t>&, const std::wstring&, std::wstring&, bool&)> thumbnail;
    std::function<bool(const std::wstring&)> exists;
    std::function<std::uint64_t()> now;
};
namespace inventory_detail {
inline bool ValidOptions(const InventoryOptions& options) {
    return options.maxFiles && options.maxFiles <= 65536 && options.maxFileBytes && options.maxFileBytes <= 256u * 1024u * 1024u &&
        options.maxChangedFiles && options.maxScanMilliseconds && options.maxScanMilliseconds <= 60000 &&
        options.thumbnailLongEdge && options.thumbnailLongEdge <= 620 && options.maxSourceDimension && options.maxSourceDimension <= 32768 &&
        options.maxSourcePixels && options.maxSourcePixels <= 256ull * 1024u * 1024u && options.maxDiagnostics <= 64;
}
inline std::wstring Hex(std::uint64_t value) {
    static const wchar_t digits[] = L"0123456789abcdef";
    std::wstring result(16, L'0');
    for (int index = 15; index >= 0; --index) { result[static_cast<std::size_t>(index)] = digits[value & 15]; value >>= 4; }
    return result;
}
inline void Mix(std::uint64_t& hash, const std::wstring& value) {
    for (const auto unit : value) { hash ^= static_cast<std::uint32_t>(unit); hash *= 1099511628211ull; }
    hash ^= 0xff; hash *= 1099511628211ull;
}
inline bool Digest(const std::wstring& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f');
    });
}
inline std::wstring IdentityKey(const InventoryFile& file) {
    return file.fileIdentity.empty() ? std::wstring{} : file.fileIdentity + L":" + Hex(file.size) + L":" + Hex(file.modified);
}
inline bool Same(const InventoryFile& left, const InventoryFile& right) {
    return left.canonicalPath == right.canonicalPath && left.fileIdentity == right.fileIdentity &&
        left.size == right.size && left.modified == right.modified;
}
inline std::wstring ThumbnailPath(const std::wstring& directory, const std::wstring& digest, unsigned edge) {
    return directory + (directory.empty() || directory.back() == L'/' || directory.back() == L'\\' ? L"" : L"\\") +
        L"Gallery-v" + std::to_wstring(InventoryThumbnailVersion) + L"-" + digest + L"-" + std::to_wstring(edge) + L".png";
}
}

inline InventoryResult ScanGalleryInventoryInjected(std::vector<InventoryEntry> entries, bool listingComplete,
    const std::wstring& folderKey, const std::wstring& cacheDirectory, const InventoryIndex& previous,
    const InventoryCancel& cancel, const InventoryCallbacks& callbacks, const InventoryOptions& options = {}) {
    InventoryResult result;
    const auto diagnostic = [&](const std::wstring& message) {
        if (!message.empty() && result.diagnostics.size() < options.maxDiagnostics &&
            std::find(result.diagnostics.begin(), result.diagnostics.end(), message) == result.diagnostics.end())
            result.diagnostics.push_back(message.substr(0, 256));
    };
    if (!inventory_detail::ValidOptions(options) || folderKey.empty() || cacheDirectory.empty() ||
        !callbacks.metadata || !callbacks.read || !callbacks.hash || !callbacks.thumbnail || !callbacks.exists || !callbacks.now) {
        diagnostic(L"The gallery inventory options or callbacks are invalid."); return result;
    }
    const auto optionKey = std::to_wstring(options.maxFileBytes) + L":" + std::to_wstring(options.maxSourceDimension) + L":" +
        std::to_wstring(options.maxSourcePixels) + L":" + std::to_wstring(options.thumbnailLongEdge);
    const bool samePolicy = previous.optionsKey == optionKey;
    const bool sameThumbnails = previous.cacheDirectory == cacheDirectory && previous.thumbnailLongEdge == options.thumbnailLongEdge &&
        previous.thumbnailFormatVersion == InventoryThumbnailVersion;
    if (previous.folderKey == folderKey) result.index = previous;
    result.index.folderKey = folderKey; result.index.optionsKey = optionKey;
    result.index.cacheDirectory = cacheDirectory; result.index.thumbnailLongEdge = options.thumbnailLongEdge;
    result.index.thumbnailFormatVersion = InventoryThumbnailVersion;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    if (entries.size() > options.maxFiles) { entries.resize(options.maxFiles); listingComplete = false; }
    std::set<std::wstring> present;
    for (const auto& entry : entries) present.insert(entry.path);
    // This is an in-memory metadata cache, not the durable ownership ledger.
    // Dropping absent cache entries never deletes an immutable thumbnail.
    for (auto item = result.index.files.begin(); item != result.index.files.end();)
        if (!present.count(item->first)) item = result.index.files.erase(item); else ++item;
    if (!listingComplete) diagnostic(L"The gallery listing reached a file, entry, or depth limit, or could not be completed.");
    std::uint64_t listingHash = 14695981039346656037ull;
    for (const auto& entry : entries) { inventory_detail::Mix(listingHash, entry.path); listingHash ^= entry.size; listingHash *= 1099511628211ull; listingHash ^= entry.modified; }
    const auto listingKey = inventory_detail::Hex(listingHash);
    if (!samePolicy || !sameThumbnails || result.index.listingKey != listingKey || result.index.cursor >= entries.size()) result.index.cursor = 0;
    result.index.listingKey = listingKey;
    const auto started = callbacks.now();
    std::size_t changed = 0;
    std::map<std::wstring, InventoryFile> aliases;
    for (const auto& pair : result.index.files) if (pair.second.ready && !pair.second.id.empty()) {
        const auto key = inventory_detail::IdentityKey(pair.second);
        if (!key.empty()) aliases[key] = pair.second;
    }
    std::size_t cursor = result.index.cursor;
    for (; cursor < entries.size(); ++cursor) {
        if (cancel && cancel()) { result.cancelled = true; break; }
        const auto now = callbacks.now();
        if (now < started || now - started >= options.maxScanMilliseconds) break;
        const auto& entry = entries[cursor];
        InventoryFile current; std::wstring issue;
        if (!callbacks.metadata(entry.path, current, issue)) {
            auto& invalid = result.index.files[entry.path]; invalid.ready = false; invalid.permanentFailure = false;
            invalid.diagnostic = L"A gallery file could not be inspected."; diagnostic(invalid.diagnostic); ++result.skippedFiles; continue;
        }
        const auto old = result.index.files.find(entry.path);
        const bool unchanged = old != result.index.files.end() && inventory_detail::Same(old->second, current);
        if (current.size == 0 || current.size > options.maxFileBytes) {
            if (unchanged) current.id = old->second.id;
            current.permanentFailure = true; current.diagnostic = L"An empty gallery file or encoded-size limit was skipped.";
            result.index.files[entry.path] = current; diagnostic(current.diagnostic); ++result.skippedFiles; continue;
        }
        if (unchanged && samePolicy && old->second.permanentFailure) { diagnostic(old->second.diagnostic); ++result.skippedFiles; continue; }
        const auto targetFor = [&](const InventoryFile& file) {
            return file.id.compare(0, 7, L"sha256:") == 0 ? inventory_detail::ThumbnailPath(cacheDirectory, file.id.substr(7), options.thumbnailLongEdge) : std::wstring{};
        };
        if (unchanged && old->second.ready && callbacks.exists(targetFor(old->second))) {
            if (old->second.thumbnailPath != targetFor(old->second)) old->second.thumbnailPath = targetFor(old->second);
            continue;
        }
        const auto identity = inventory_detail::IdentityKey(current);
        const auto alias = aliases.find(identity);
        if (!identity.empty() && alias != aliases.end() && callbacks.exists(targetFor(alias->second))) {
            current.id = alias->second.id; current.thumbnailPath = targetFor(alias->second); current.ready = true;
            result.index.files[entry.path] = std::move(current); continue;
        }
        if (changed >= options.maxChangedFiles) break;
        ++changed;
        std::vector<std::uint8_t> encoded;
        if (!callbacks.read(entry.path, current, encoded, issue) || encoded.size() != current.size) {
            const auto afterRead = callbacks.now();
            if ((cancel && cancel()) || afterRead < started || afterRead - started >= options.maxScanMilliseconds) {
                result.cancelled = cancel && cancel(); break;
            }
            if (unchanged) { current.id = old->second.id; current.thumbnailPath = old->second.thumbnailPath; }
            current.diagnostic = L"A gallery file changed during its read or could not be read.";
            result.index.files[entry.path] = current; diagnostic(current.diagnostic); ++result.skippedFiles; continue;
        }
        // Missing thumbnails can be regenerated without hashing an unchanged
        // file again. Hardlink identity can likewise reuse an earlier digest.
        std::wstring digest;
        if (unchanged && old->second.id.compare(0, 7, L"sha256:") == 0) digest = old->second.id.substr(7);
        else if (!identity.empty() && alias != aliases.end() && alias->second.id.compare(0, 7, L"sha256:") == 0) digest = alias->second.id.substr(7);
        else { ++result.hashedFiles; digest = callbacks.hash(encoded); }
        if (!inventory_detail::Digest(digest)) {
            const auto afterHash = callbacks.now();
            if ((cancel && cancel()) || afterHash < started || afterHash - started >= options.maxScanMilliseconds) {
                result.cancelled = cancel && cancel(); break;
            }
            current.diagnostic = L"A gallery file's encoded content could not be identified.";
            result.index.files[entry.path] = current; diagnostic(current.diagnostic); ++result.skippedFiles; continue;
        }
        current.id = L"sha256:" + digest;
        current.thumbnailPath = inventory_detail::ThumbnailPath(cacheDirectory, digest, options.thumbnailLongEdge);
        bool permanent = false;
        bool thumbReady = callbacks.exists(current.thumbnailPath);
        if (!thumbReady) { ++result.decodedFiles; thumbReady = callbacks.thumbnail(encoded, current.thumbnailPath, issue, permanent); }
        if (!thumbReady) {
            current.permanentFailure = permanent; current.diagnostic = permanent ?
                L"A corrupt, unsupported or oversized decoded gallery image was skipped." : L"A gallery thumbnail could not be written before its deadline.";
            result.index.files[entry.path] = current;
            const auto afterDecode = callbacks.now();
            if ((cancel && cancel()) || afterDecode < started || afterDecode - started >= options.maxScanMilliseconds) {
                result.cancelled = cancel && cancel(); break;
            }
            diagnostic(current.diagnostic); ++result.skippedFiles; continue;
        }
        current.ready = true;
        result.index.files[entry.path] = current;
        if (!identity.empty()) aliases[identity] = std::move(current);
    }
    result.index.cursor = cursor;
    result.deferredFiles = entries.size() - cursor;
    result.cancelled = result.cancelled || (cancel && cancel());
    result.complete = listingComplete && cursor == entries.size() && !result.cancelled;
    if (result.complete) result.index.cursor = 0;
    std::set<std::wstring> emitted;
    for (const auto& entry : entries) {
        const auto found = result.index.files.find(entry.path);
        if (found != result.index.files.end() && found->second.ready && emitted.insert(found->second.id).second)
            result.images.push_back({found->second.id, found->second.thumbnailPath});
    }
    if (result.cancelled) diagnostic(L"Gallery inventory scanning was canceled.");
    else if (result.deferredFiles) diagnostic(L"Gallery inventory scanning will continue in another bounded pass.");
    return result;
}
} }

#if defined(_WIN32) && !defined(AIP_GALLERY_INVENTORY_PURE)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <propidl.h>
#include <wincrypt.h>
#include <wrl/client.h>
#include <shlobj.h>
#include <atomic>
#include <chrono>
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Windowscodecs.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
namespace aip { namespace gallery { namespace inventory_native {
using Microsoft::WRL::ComPtr;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct FindHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    void Close() { if (value != INVALID_HANDLE_VALUE) { FindClose(value); value = INVALID_HANDLE_VALUE; } }
    ~FindHandle() { Close(); }
};
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
inline std::uint64_t Time(FILETIME value) { return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
inline std::wstring Absolute(const std::wstring& path) {
    if (path.empty()) return {};
    const auto count = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!count || count > 32768) return {};
    std::vector<wchar_t> buffer(count);
    const auto copied = GetFullPathNameW(path.c_str(), count, buffer.data(), nullptr);
    return copied && copied < count ? std::wstring(buffer.data(), copied) : std::wstring{};
}
inline std::wstring DirectoryPath(const std::wstring& path) {
    Handle directory;
    directory.value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory.value == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(directory.value, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    const auto count = GetFinalPathNameByHandleW(directory.value, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!count || count > 32768) return {};
    std::vector<wchar_t> buffer(count + 1);
    const auto copied = GetFinalPathNameByHandleW(directory.value, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
    return copied && copied < buffer.size() ? std::wstring(buffer.data(), copied) : std::wstring{};
}
inline bool Metadata(HANDLE file, InventoryFile& result) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    const auto needed = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!needed || needed > 32768) return false;
    std::vector<wchar_t> path(needed + 1);
    const auto copied = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED);
    if (!copied || copied >= path.size()) return false;
    result.canonicalPath.assign(path.data(), copied);
    const auto fileIndex = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    result.fileIdentity = fileIndex ? inventory_detail::Hex(info.dwVolumeSerialNumber) + inventory_detail::Hex(fileIndex) : std::wstring{};
#if _WIN32_WINNT >= 0x0602
    FILE_ID_INFO extended{};
    if (GetFileInformationByHandleEx(file, FileIdInfo, &extended, sizeof(extended))) {
        const bool nonzero = std::any_of(std::begin(extended.FileId.Identifier), std::end(extended.FileId.Identifier), [](BYTE byte) { return byte != 0; });
        result.fileIdentity.clear();
        if (nonzero) {
            result.fileIdentity = inventory_detail::Hex(extended.VolumeSerialNumber);
            static const wchar_t digits[] = L"0123456789abcdef";
            for (const auto byte : extended.FileId.Identifier) { result.fileIdentity += digits[byte >> 4]; result.fileIdentity += digits[byte & 15]; }
        }
    }
#endif
    result.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    result.modified = Time(info.ftLastWriteTime); return true;
}
inline bool Metadata(const std::wstring& path, InventoryFile& result, std::wstring&) {
    Handle file; file.value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return file.value != INVALID_HANDLE_VALUE && Metadata(file.value, result);
}
inline std::wstring Hash(const std::vector<std::uint8_t>& bytes, const InventoryCancel& cancel = {},
    std::uint64_t deadline = UINT64_MAX) {
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0; BYTE digest[32]{}; DWORD count = sizeof(digest);
    if (bytes.size() > MAXDWORD || !CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return {};
    bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    for (std::size_t offset = 0; ok && offset < bytes.size();) {
        if ((cancel && cancel()) || GetTickCount64() >= deadline) { ok = false; break; }
        const auto size = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset));
        ok = CryptHashData(hash, bytes.data() + offset, size, 0) != FALSE; offset += size;
    }
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &count, 0) != FALSE;
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ok || count != sizeof(digest)) return {};
    static const wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
inline bool Extension(std::wstring path) {
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    path.erase(0, dot);
    for (auto& c : path) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return path == L".png" || path == L".jpg" || path == L".jpeg" || path == L".bmp" || path == L".gif" || path == L".tif" || path == L".tiff";
}
inline bool Exists(const std::wstring& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
inline InventoryOrientation Orientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (!frame || FAILED(frame->GetMetadataQueryReader(&reader))) return {};
    for (const auto* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}", L"/eXIf/ifd/{ushort=274}", L"/eXIf/{ushort=274}"}) {
        PROPVARIANT value{};
        const auto status = reader->GetMetadataByName(query, &value);
        unsigned tag = 0;
        if (SUCCEEDED(status)) {
            if (value.vt == VT_UI2) tag = value.uiVal;
            else if (value.vt == VT_UI4) tag = value.ulVal;
        }
        PropVariantClear(&value);
        if (tag >= 1 && tag <= 8) return InventoryExifOrientation(tag);
    }
    return {};
}
inline bool WritePng(const std::vector<std::uint8_t>& encoded, const std::wstring& destination, const InventoryOptions& options,
    const InventoryCancel& cancel, std::uint64_t deadline, std::wstring&, bool& permanent) {
    const auto continuing = [&] { return GetTickCount64() < deadline && (!cancel || !cancel()); };
    permanent = false;
    if (!continuing()) return false;
    Apartment apartment;
    if (FAILED(apartment.result)) return false;
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> input;
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> source;
    HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(status)) return false;
    if (SUCCEEDED(status)) status = factory->CreateStream(&input);
    if (SUCCEEDED(status)) status = input->InitializeFromMemory(const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
    if (SUCCEEDED(status)) status = factory->CreateDecoderFromStream(input.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    GUID format{};
    if (SUCCEEDED(status)) status = decoder->GetContainerFormat(&format);
    if (SUCCEEDED(status) && format != GUID_ContainerFormatPng && format != GUID_ContainerFormatJpeg &&
        format != GUID_ContainerFormatBmp && format != GUID_ContainerFormatGif && format != GUID_ContainerFormatTiff) status = WINCODEC_ERR_BADIMAGE;
    if (SUCCEEDED(status)) status = decoder->GetFrame(0, &source);
    UINT width = 0, height = 0;
    if (SUCCEEDED(status)) status = source->GetSize(&width, &height);
    if (FAILED(status) || !width || !height || width > options.maxSourceDimension || height > options.maxSourceDimension ||
        static_cast<std::uint64_t>(width) * height > options.maxSourcePixels) { permanent = true; return false; }
    if (!continuing()) return false;
    const auto orientation = Orientation(source.Get());
    ComPtr<IWICBitmapFlipRotator> rotation, reflection;
    IWICBitmapSource* oriented = source.Get();
    if (orientation.clockwise) {
        status = factory->CreateBitmapFlipRotator(&rotation);
        const auto transform = orientation.clockwise == 90 ? WICBitmapTransformRotate90 :
            orientation.clockwise == 180 ? WICBitmapTransformRotate180 : WICBitmapTransformRotate270;
        if (SUCCEEDED(status)) status = rotation->Initialize(oriented, transform);
        oriented = rotation.Get();
    }
    if (SUCCEEDED(status) && (orientation.horizontal || orientation.vertical)) {
        status = factory->CreateBitmapFlipRotator(&reflection);
        if (SUCCEEDED(status)) status = reflection->Initialize(oriented,
            orientation.horizontal ? WICBitmapTransformFlipHorizontal : WICBitmapTransformFlipVertical);
        oriented = reflection.Get();
    }
    if (SUCCEEDED(status)) status = oriented->GetSize(&width, &height);
    if (FAILED(status) || !width || !height) { permanent = true; return false; }
    const auto longest = std::max(width, height);
    const auto outWidth = longest > options.thumbnailLongEdge ? std::max(1u, static_cast<UINT>(static_cast<std::uint64_t>(width) * options.thumbnailLongEdge / longest)) : width;
    const auto outHeight = longest > options.thumbnailLongEdge ? std::max(1u, static_cast<UINT>(static_cast<std::uint64_t>(height) * options.thumbnailLongEdge / longest)) : height;
    ComPtr<IWICBitmapScaler> scaler; ComPtr<IWICFormatConverter> premultiplied, converter;
    status = factory->CreateFormatConverter(&premultiplied);
    if (SUCCEEDED(status)) status = premultiplied->Initialize(oriented, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    IWICBitmapSource* pixels = premultiplied.Get();
    if (SUCCEEDED(status) && (outWidth != width || outHeight != height)) {
        status = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(status)) status = scaler->Initialize(pixels, outWidth, outHeight, WICBitmapInterpolationModeFant);
        pixels = scaler.Get();
    }
    if (SUCCEEDED(status)) status = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(status)) status = converter->Initialize(pixels, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (FAILED(status)) { permanent = status != E_OUTOFMEMORY; return false; }
    ComPtr<IStream> output; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
    if (SUCCEEDED(status) && continuing()) status = CreateStreamOnHGlobal(nullptr, TRUE, &output);
    else if (SUCCEEDED(status)) return false;
    if (SUCCEEDED(status)) status = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(status)) status = encoder->Initialize(output.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(status)) status = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(status)) status = frame->Initialize(properties.Get());
    if (SUCCEEDED(status)) status = frame->SetSize(outWidth, outHeight);
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(status)) status = frame->SetPixelFormat(&pixelFormat);
    if (SUCCEEDED(status) && pixelFormat != GUID_WICPixelFormat32bppBGRA) status = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(status) && continuing()) status = frame->WriteSource(converter.Get(), nullptr);
    else if (SUCCEEDED(status)) return false;
    if (SUCCEEDED(status)) status = frame->Commit();
    if (SUCCEEDED(status)) status = encoder->Commit();
    if (FAILED(status) || !continuing()) return false;
    STATSTG info{};
    if (FAILED(output->Stat(&info, STATFLAG_NONAME)) || !info.cbSize.QuadPart || info.cbSize.QuadPart > 4u * 1024u * 1024u) return false;
    LARGE_INTEGER start{};
    if (FAILED(output->Seek(start, STREAM_SEEK_SET, nullptr))) return false;
    std::vector<BYTE> bytes(static_cast<std::size_t>(info.cbSize.QuadPart)); ULONG copied = 0;
    if (FAILED(output->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &copied)) || copied != bytes.size()) return false;
    static std::atomic<unsigned> sequence{0};
    const auto stage = destination + L".stage-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence);
    bool owned = false, saved = false;
    {
        Handle file; file.value = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file.value != INVALID_HANDLE_VALUE) {
            owned = true; DWORD written = 0;
            saved = WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                written == bytes.size() && FlushFileBuffers(file.value);
        }
    }
    if (!owned) return false;
    if (saved && continuing() && MoveFileExW(stage.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    // Only this invocation's CREATE_NEW stage is ever deleted. Existing
    // immutable PNGs, including older notification assets, are never purged.
    DeleteFileW(stage.c_str());
    return saved && continuing() && Exists(destination);
}
} // inventory_native

inline InventoryResult ScanGalleryInventory(const std::wstring& folder, const std::wstring& cacheDirectory,
    const InventoryIndex& previous, const InventoryCancel& cancel = {}, const InventoryOptions& options = {}) {
    if (!inventory_detail::ValidOptions(options)) {
        InventoryResult result; result.diagnostics.push_back(L"The gallery inventory options are invalid."); return result;
    }
    if (cancel && cancel()) { InventoryResult result; result.index = previous; result.cancelled = true; return result; }
    const auto requestedSource = inventory_native::Absolute(folder), requestedCache = inventory_native::Absolute(cacheDirectory);
    const auto source = inventory_native::DirectoryPath(requestedSource);
    const auto cache = requestedCache;
    if (source.empty() || cache.empty() || _wcsicmp(source.c_str(), cache.c_str()) == 0) {
        InventoryResult result; result.diagnostics.push_back(L"The source and thumbnail-cache directories must be valid and distinct."); return result;
    }
    const auto created = SHCreateDirectoryExW(nullptr, cache.c_str(), nullptr);
    if (created != ERROR_SUCCESS && created != ERROR_FILE_EXISTS && created != ERROR_ALREADY_EXISTS) {
        InventoryResult result; result.diagnostics.push_back(L"The gallery thumbnail cache could not be created."); return result;
    }
    const auto canonicalCache = inventory_native::DirectoryPath(cache);
    if (canonicalCache.empty() || _wcsicmp(source.c_str(), canonicalCache.c_str()) == 0) {
        InventoryResult result; result.diagnostics.push_back(L"The thumbnail cache must be a separate directory from the gallery source."); return result;
    }
    const auto started = GetTickCount64(), deadline = started + options.maxScanMilliseconds;
    std::vector<InventoryEntry> entries; bool complete = true;
    // Windows' Web folder and ordinary albums commonly contain subfolders.
    // One bounded breadth-first listing follows real directories only; links
    // and junctions are excluded, and depth/entry limits still bound races.
    constexpr unsigned maximumDepth = 32;
    std::vector<std::pair<std::wstring, unsigned>> directories{{source, 0}};
    std::size_t directoryEntries = 0;
    bool exhausted = false;
    for (std::size_t directoryIndex = 0; directoryIndex < directories.size() && !exhausted; ++directoryIndex) {
        if ((cancel && cancel()) || GetTickCount64() >= deadline) { complete = false; break; }
        const auto directory = directories[directoryIndex];
        const auto prefix = directory.first + (directory.first.back() == L'\\' || directory.first.back() == L'/' ? L"" : L"\\");
        WIN32_FIND_DATAW data{}; inventory_native::FindHandle find;
        find.value = FindFirstFileExW((prefix + L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
        if (find.value == INVALID_HANDLE_VALUE) { if (GetLastError() != ERROR_FILE_NOT_FOUND) complete = false; continue; }
        do {
            if ((cancel && cancel()) || ++directoryEntries > options.maxFiles * 4 || GetTickCount64() >= deadline) {
                complete = false; exhausted = true; break;
            }
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            const auto path = prefix + data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || _wcsicmp(path.c_str(), canonicalCache.c_str()) == 0) continue;
                if (directory.second >= maximumDepth) { complete = false; continue; }
                directories.emplace_back(path, directory.second + 1); continue;
            }
            if (!inventory_native::Extension(data.cFileName)) continue;
            if (entries.size() >= options.maxFiles) { complete = false; exhausted = true; break; }
            entries.push_back({path, (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
                inventory_native::Time(data.ftLastWriteTime)});
        } while (FindNextFileW(find.value, &data));
        const auto enumerationError = GetLastError();
        if (!exhausted && enumerationError != ERROR_NO_MORE_FILES) complete = false;
    }
    const auto afterListing = GetTickCount64();
    if (afterListing >= deadline) {
        InventoryResult result; result.index = previous; result.cancelled = cancel && cancel();
        result.diagnostics.push_back(L"The gallery listing exceeded its bounded scan interval."); return result;
    }
    InventoryCallbacks callbacks;
    callbacks.metadata = [](const auto& path, auto& stamp, auto& error) { return inventory_native::Metadata(path, stamp, error); };
    callbacks.read = [&](const auto& path, const InventoryFile& expected, auto& bytes, auto&) {
        if ((cancel && cancel()) || GetTickCount64() >= deadline || expected.size > options.maxFileBytes) return false;
        inventory_native::Handle file;
        // Excluding FILE_SHARE_WRITE ensures the hashed bytes and decoder's
        // input are one coherent read, while rename/delete can still proceed.
        file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        InventoryFile actual;
        if (file.value == INVALID_HANDLE_VALUE || !inventory_native::Metadata(file.value, actual) || !inventory_detail::Same(expected, actual)) return false;
        bytes.resize(static_cast<std::size_t>(actual.size));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            if ((cancel && cancel()) || GetTickCount64() >= deadline) return false;
            DWORD read = 0; const auto amount = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset));
            if (!ReadFile(file.value, bytes.data() + offset, amount, &read, nullptr) || read != amount) return false;
            offset += read;
        }
        return true;
    };
    callbacks.hash = [&](const auto& bytes) { return inventory_native::Hash(bytes, cancel, deadline); };
    callbacks.thumbnail = [&](const auto& bytes, const auto& path, auto& error, bool& permanent) {
        return inventory_native::WritePng(bytes, path, options, cancel, deadline, error, permanent);
    };
    callbacks.exists = inventory_native::Exists;
    callbacks.now = [] { return GetTickCount64(); };
    auto remaining = options; remaining.maxScanMilliseconds = deadline - afterListing;
    auto result = ScanGalleryInventoryInjected(std::move(entries), complete, source, cache, previous, cancel, callbacks, remaining);
    if (cancel && cancel()) { result.cancelled = true; result.complete = false; }
    return result;
}
} }
#endif

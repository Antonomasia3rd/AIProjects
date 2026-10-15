#pragma once

// Shared, bounded SMTC artwork decoding and profile-scoped temporary storage.
// Call on a worker. A snapshot lease keeps its immutable BMP alive; releasing
// the last lease removes it. No executable/package directory needs to be writable.
#include <windows.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <wrl/client.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace aip { namespace content {
inline constexpr std::size_t SmtcMaximumArtworkBytes = 10u * 1024u * 1024u;
inline constexpr UINT SmtcMaximumArtworkDimension = 8192;
inline constexpr std::uint64_t SmtcMaximumArtworkPixels = 16u * 1024u * 1024u;
inline constexpr UINT SmtcCachedArtworkDimension = 1024;
using SmtcCancel = std::function<bool()>;

namespace smtc_artwork_detail {
using Clock = std::chrono::steady_clock;
inline bool Continue(Clock::time_point deadline, const SmtcCancel& canceled) {
    return Clock::now() < deadline && (!canceled || !canceled());
}
inline bool AllowedDimensions(UINT width, UINT height) {
    return width && height && width <= SmtcMaximumArtworkDimension && height <= SmtcMaximumArtworkDimension &&
        static_cast<std::uint64_t>(width) * height <= SmtcMaximumArtworkPixels;
}
inline std::wstring Hash(const BYTE* data, std::size_t size) {
    if (size > MAXDWORD) return {};
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    BYTE digest[32]{}; DWORD length = sizeof(digest);
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return {};
    const bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
        CryptHashData(hash, data, static_cast<DWORD>(size), 0) &&
        CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0);
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ok || length != sizeof(digest)) return {};
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    for (BYTE byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
struct Directory {
    std::wstring path, identity;
    explicit Directory(std::wstring value) : identity(std::move(value)) {}
    ~Directory() { if (!path.empty()) RemoveDirectoryW(path.c_str()); }
    bool Prepare() {
        if (!path.empty()) return true;
        wchar_t temporary[32768]{};
        const auto count = GetTempPathW(_countof(temporary), temporary);
        if (!count || count >= _countof(temporary)) return false;
        const auto key = Hash(reinterpret_cast<const BYTE*>(identity.data()), identity.size() * sizeof(wchar_t));
        if (key.empty()) return false;
        static std::atomic<unsigned long> sequence{0};
        auto candidate = std::wstring(temporary) + L"AIProjects-SMTC-" + key + L"-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence);
        if (!CreateDirectoryW(candidate.c_str(), nullptr)) return false;
        path = std::move(candidate); return true;
    }
};
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

inline bool Decode(const std::vector<std::uint8_t>& encoded, std::vector<BYTE>& bitmap,
    UINT& width, UINT& height, std::wstring& error, Clock::time_point deadline, const SmtcCancel& canceled) {
    using Microsoft::WRL::ComPtr;
    bitmap.clear(); width = 0; height = 0;
    if (encoded.empty() || encoded.size() > SmtcMaximumArtworkBytes) {
        error = L"The media artwork is empty or exceeds the encoded byte limit."; return false;
    }
    Apartment apartment;
    if (FAILED(apartment.result)) { error = L"Artwork decoding requires a multithreaded worker."; return false; }
    if (!Continue(deadline, canceled)) { error = L"Artwork decoding timed out or was canceled."; return false; }
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(status)) status = factory->CreateStream(&stream);
    if (SUCCEEDED(status)) status = stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
    if (SUCCEEDED(status)) status = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    GUID container{};
    if (SUCCEEDED(status)) status = decoder->GetContainerFormat(&container);
    if (SUCCEEDED(status) && container != GUID_ContainerFormatPng && container != GUID_ContainerFormatJpeg &&
        container != GUID_ContainerFormatBmp && container != GUID_ContainerFormatGif) status = WINCODEC_ERR_BADIMAGE;
    if (SUCCEEDED(status)) status = decoder->GetFrame(0, &frame);
    UINT originalWidth = 0, originalHeight = 0;
    if (SUCCEEDED(status)) status = frame->GetSize(&originalWidth, &originalHeight);
    if (FAILED(status) || !originalWidth || !originalHeight) { error = L"The media artwork is not a supported image."; return false; }
    if (!AllowedDimensions(originalWidth, originalHeight)) {
        error = L"The media artwork exceeds the decoded image size limit."; return false;
    }
    if (!Continue(deadline, canceled)) { error = L"Artwork decoding timed out or was canceled."; return false; }
    width = originalWidth; height = originalHeight;
    if (width > SmtcCachedArtworkDimension || height > SmtcCachedArtworkDimension) {
        const auto largest = (std::max)(width, height);
        width = (std::max)(1u, static_cast<UINT>(static_cast<std::uint64_t>(width) * SmtcCachedArtworkDimension / largest));
        height = (std::max)(1u, static_cast<UINT>(static_cast<std::uint64_t>(height) * SmtcCachedArtworkDimension / largest));
    }
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    IWICBitmapSource* source = frame.Get();
    if (width != originalWidth || height != originalHeight) {
        status = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(status)) status = scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant);
        source = scaler.Get();
    }
    if (SUCCEEDED(status)) status = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(status)) status = converter->Initialize(source, GUID_WICPixelFormat24bppBGR,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    const UINT stride = (width * 3u + 3u) & ~3u;
    const UINT pixelBytes = stride * height;
    // At most 1024 x 1024 x 24 bits are allocated/copied, regardless of source size.
    bitmap.assign(54u + pixelBytes, 0);
    if (SUCCEEDED(status) && Continue(deadline, canceled))
        status = converter->CopyPixels(nullptr, stride, pixelBytes, bitmap.data() + 54);
    else if (SUCCEEDED(status)) status = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    if (FAILED(status) || !Continue(deadline, canceled)) { bitmap.clear(); error = L"Artwork decoding failed, timed out, or was canceled."; return false; }
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfSize = static_cast<DWORD>(bitmap.size()); file.bfOffBits = 54;
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height); info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = pixelBytes;
    memcpy(bitmap.data(), &file, sizeof(file)); memcpy(bitmap.data() + 14, &info, sizeof(info));
    return true;
}
} // namespace smtc_artwork_detail

struct SmtcArtworkFile {
    std::wstring path, hash;
    UINT width = 0, height = 0;
    std::shared_ptr<smtc_artwork_detail::Directory> directory;
    ~SmtcArtworkFile() { if (!path.empty()) DeleteFileW(path.c_str()); }
};

class SmtcArtworkCache {
    std::shared_ptr<smtc_artwork_detail::Directory> directory_;
    std::mutex mutex_;
    std::map<std::wstring, std::weak_ptr<const SmtcArtworkFile>> files_;
public:
    SmtcArtworkCache(const std::wstring& product, const std::wstring& absoluteProfile)
        : directory_(std::make_shared<smtc_artwork_detail::Directory>(product + L"\n" + absoluteProfile)) {}
    std::shared_ptr<const SmtcArtworkFile> Store(const std::vector<std::uint8_t>& bytes,
        std::wstring& error, smtc_artwork_detail::Clock::time_point deadline, const SmtcCancel& canceled = {}, bool retainEncoded = false) {
        error.clear();
        if (bytes.empty()) return {};
        if (bytes.size() > SmtcMaximumArtworkBytes) { error = L"The media artwork exceeds the encoded byte limit."; return {}; }
        if (!smtc_artwork_detail::Continue(deadline, canceled)) { error = L"Artwork storage timed out or was canceled."; return {}; }
        const auto hash = smtc_artwork_detail::Hash(bytes.data(), bytes.size());
        if (hash.empty()) { error = L"Could not identify the media artwork."; return {}; }
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iterator = files_.begin(); iterator != files_.end();)
            if (iterator->second.expired()) iterator = files_.erase(iterator); else ++iterator;
        const auto cacheKey = hash + (retainEncoded ? L":encoded" : L":bitmap");
        const auto found = files_.find(cacheKey);
        if (found != files_.end()) if (auto asset = found->second.lock()) return asset;
        std::vector<BYTE> bitmap;
        UINT width = 0, height = 0;
        if (!smtc_artwork_detail::Decode(bytes, bitmap, width, height, error, deadline, canceled)) return {};
        if (retainEncoded && (width > 256 || height > 256)) { error = L"The application icon exceeds its decoded dimension limit."; return {}; }
        if (!directory_->Prepare()) { error = L"Could not create the profile's temporary artwork cache."; return {}; }
        auto asset = std::make_shared<SmtcArtworkFile>();
        const auto path = directory_->path + L"\\" + hash + (retainEncoded ? L"-icon.img" : L".bmp");
        const auto temporary = path + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) { error = L"Could not create the media artwork file."; return {}; }
        DWORD written = 0;
        const auto& output = retainEncoded ? bytes : bitmap;
        const bool saved = WriteFile(file, output.data(), static_cast<DWORD>(output.size()), &written, nullptr) && written == output.size();
        CloseHandle(file);
        const bool complete = saved && smtc_artwork_detail::Continue(deadline, canceled) &&
            MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!complete) { DeleteFileW(temporary.c_str()); error = L"Could not finish storing the media artwork."; return {}; }
        asset->path = path; asset->hash = hash; asset->width = width; asset->height = height; asset->directory = directory_;
        files_[cacheKey] = asset;
        return asset;
    }
};
} } // namespace aip::content

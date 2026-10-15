// Windows codec/filesystem fixture. Creates only named synthetic files beneath
// one fresh temporary directory; never accepts a user photo/config directory.
#define NOMINMAX
#include "../dependencies/gallery_inventory.inc"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <iterator>
namespace {
using namespace aip::gallery;
using Microsoft::WRL::ComPtr;
int checks = 0;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
struct Fixture {
    std::wstring root, source, cache;
    std::vector<std::wstring> ownedFiles, ownedDirectories;
    bool ownsRoot = false;
    Fixture() {
        wchar_t base[32768]{};
        const auto count = GetTempPathW(static_cast<DWORD>(std::size(base)), base);
        if (!count || count >= std::size(base)) throw std::runtime_error("private temporary directory unavailable");
        root = std::wstring(base) + L"GalleryInventoryFixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        Check(CreateDirectoryW(root.c_str(), nullptr) != FALSE, "create a fresh private fixture root"); ownsRoot = true;
        source = root + L"\\source"; cache = root + L"\\cache";
        if (!CreateDirectoryW(source.c_str(), nullptr) || !CreateDirectoryW(cache.c_str(), nullptr)) {
            Cleanup(); throw std::runtime_error("private fixture child directory creation failed");
        }
    }
    bool Cleanup() {
        if (!ownsRoot) return true;
        bool ok = true;
        for (const auto& file : ownedFiles) if (!DeleteFileW(file.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) ok = false;
        for (auto directory = ownedDirectories.rbegin(); directory != ownedDirectories.rend(); ++directory)
            if (!RemoveDirectoryW(directory->c_str()) && GetLastError() != ERROR_PATH_NOT_FOUND && GetLastError() != ERROR_FILE_NOT_FOUND) ok = false;
        if (!RemoveDirectoryW(cache.c_str()) && GetLastError() != ERROR_PATH_NOT_FOUND && GetLastError() != ERROR_FILE_NOT_FOUND) ok = false;
        if (!RemoveDirectoryW(source.c_str()) && GetLastError() != ERROR_PATH_NOT_FOUND && GetLastError() != ERROR_FILE_NOT_FOUND) ok = false;
        if (!RemoveDirectoryW(root.c_str())) ok = false;
        if (ok) ownsRoot = false;
        return ok;
    }
    ~Fixture() { Cleanup(); }
    std::wstring Write(const wchar_t* name, const std::vector<std::uint8_t>& bytes) {
        const auto path = source + L"\\" + name;
        inventory_native::Handle file;
        file.value = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("private synthetic file could not be created");
        ownedFiles.push_back(path);
        DWORD written = 0;
        Check(WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size(),
            "write only in-memory synthetic image bytes");
        return path;
    }
    void Keep(const InventoryResult& result) {
        for (const auto& image : result.images) {
            Check(image.thumbnailPath.compare(0, cache.size(), cache) == 0 && image.thumbnailPath[cache.size()] == L'\\',
                "native helper returns cache images only inside the owned private fixture directory");
            if (std::find(ownedFiles.begin(), ownedFiles.end(), image.thumbnailPath) == ownedFiles.end()) ownedFiles.push_back(image.thumbnailPath);
        }
    }
};
std::vector<std::uint8_t> AlphaPng() {
    inventory_native::Apartment apartment;
    Check(SUCCEEDED(apartment.result), "synthetic image encoder owns its MTA lifetime");
    ComPtr<IWICImagingFactory> factory; ComPtr<IStream> stream; ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) &&
        SUCCEEDED(frame->Initialize(properties.Get())) && SUCCEEDED(frame->SetSize(1, 1)), "prepare an in-memory one-pixel alpha PNG");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    BYTE pixel[4] = {24, 48, 96, 128};
    Check(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA &&
        SUCCEEDED(frame->WritePixels(1, 4, 4, pixel)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),
        "encode a known semitransparent pixel without reading artwork");
    STATSTG info{}; LARGE_INTEGER zero{};
    Check(SUCCEEDED(stream->Stat(&info, STATFLAG_NONAME)) && info.cbSize.QuadPart && info.cbSize.QuadPart < 4096 &&
        SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)), "bound the synthetic PNG stream");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(info.cbSize.QuadPart)); ULONG read = 0;
    Check(SUCCEEDED(stream->Read(result.data(), static_cast<ULONG>(result.size()), &read)) && read == result.size(), "copy only the generated PNG bytes");
    return result;
}
std::vector<std::uint8_t> LandscapeBmp() {
    constexpr UINT width = 620, height = 310, stride = ((width * 3 + 3) & ~3u);
    std::vector<std::uint8_t> result(54 + stride * height, 80);
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = 54; file.bfSize = static_cast<DWORD>(result.size());
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width); info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = stride * height;
    memcpy(result.data(), &file, sizeof(file)); memcpy(result.data() + 14, &info, sizeof(info)); return result;
}
std::vector<std::uint8_t> OrientedImage(bool jpeg, unsigned orientation) {
    inventory_native::Apartment apartment;
    Check(SUCCEEDED(apartment.result), "orientation fixture encoder owns its MTA lifetime");
    ComPtr<IWICImagingFactory> factory; ComPtr<IStream> stream; ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties; ComPtr<IWICMetadataQueryWriter> metadata;
    const UINT width = jpeg ? 40u : 3u, height = jpeg ? 20u : 2u, components = jpeg ? 3u : 4u;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
        SUCCEEDED(factory->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatTiff, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) &&
        SUCCEEDED(frame->Initialize(properties.Get())) && SUCCEEDED(frame->SetSize(width, height)), "prepare only a synthetic tagged JPEG/TIFF stream");
    WICPixelFormatGUID format = jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
    Check(SUCCEEDED(frame->SetPixelFormat(&format)) && format == (jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA) &&
        SUCCEEDED(frame->GetMetadataQueryWriter(&metadata)), "orientation fixture supports explicit pixel and metadata formats");
    PROPVARIANT tag{}; tag.vt = VT_UI2; tag.uiVal = static_cast<USHORT>(orientation);
    Check(SUCCEEDED(metadata->SetMetadataByName(jpeg ? L"/app1/ifd/{ushort=274}" : L"/ifd/{ushort=274}", &tag)),
        "write the actual EXIF orientation tag into the synthetic file");
    const BYTE palette[6][4] = {{0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255},
        {0, 255, 255, 255}, {255, 0, 255, 255}, {255, 255, 0, 255}};
    std::vector<BYTE> pixels(static_cast<std::size_t>(width) * height * components);
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        auto* target = pixels.data() + (static_cast<std::size_t>(y) * width + x) * components;
        if (!jpeg) memcpy(target, palette[y * width + x], components);
        else {
            const BYTE colors[4][3] = {{0, 0, 255}, {0, 255, 0}, {255, 0, 0}, {255, 255, 255}};
            memcpy(target, colors[(y >= height / 2 ? 2 : 0) + (x >= width / 2 ? 1 : 0)], components);
        }
    }
    Check(SUCCEEDED(frame->WritePixels(height, width * components, static_cast<UINT>(pixels.size()), pixels.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "encode only the synthetic asymmetric orientation pixels");
    STATSTG info{}; LARGE_INTEGER zero{};
    Check(SUCCEEDED(stream->Stat(&info, STATFLAG_NONAME)) && info.cbSize.QuadPart && info.cbSize.QuadPart < 1024 * 1024 &&
        SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)), "orientation fixture encoded size remains bounded");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(info.cbSize.QuadPart)); ULONG read = 0;
    Check(SUCCEEDED(stream->Read(result.data(), static_cast<ULONG>(result.size()), &read)) && read == result.size(), "read only generated orientation fixture bytes");
    return result;
}
void CheckOrientationPixels(const std::wstring& path, unsigned orientation, bool jpeg) {
    inventory_native::Apartment apartment;
    Check(SUCCEEDED(apartment.result), "orientation readback owns its MTA lifetime");
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)),
        "read only orientation pixels from generated private thumbnails");
    if (!jpeg) {
        BYTE pixels[24]{};
        Check(SUCCEEDED(converter->CopyPixels(nullptr, 8, 24, pixels)), "read the lossless two-by-three orientation result");
        const char* expected = orientation == 5 ? "ADBECF" : "FCEBDA";
        const BYTE palette[6][4] = {{0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255},
            {0, 255, 255, 255}, {255, 0, 255, 255}, {255, 255, 0, 255}};
        bool match = true;
        for (unsigned pixel = 0; pixel < 6; ++pixel) match = match && memcmp(pixels + pixel * 4, palette[expected[pixel] - 'A'], 4) == 0;
        Check(match, "transpose/transverse use the correct separate rotation and reflection order");
    } else {
        const WICRect samples[4] = {{3, 3, 1, 1}, {16, 3, 1, 1}, {3, 36, 1, 1}, {16, 36, 1, 1}};
        BYTE colors[4][4]{}; bool read = true;
        for (unsigned index = 0; index < 4; ++index) read = SUCCEEDED(converter->CopyPixels(&samples[index], 4, 4, colors[index])) && read;
        Check(read && colors[0][0] > 160 && colors[0][1] < 100 && colors[0][2] < 100 &&
            colors[1][2] > 160 && colors[1][0] < 100 && colors[1][1] < 100 &&
            colors[2][0] > 160 && colors[2][1] > 160 && colors[2][2] > 160 &&
            colors[3][1] > 160 && colors[3][0] < 100 && colors[3][2] < 100,
            "portrait EXIF JPEGs are rotated clockwise with the correct corner colors");
    }
}
void CheckPng(const std::wstring& path, UINT expectedWidth, UINT expectedHeight, bool alpha) {
    inventory_native::Apartment apartment;
    Check(SUCCEEDED(apartment.result), "thumbnail readback owns its MTA lifetime");
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)), "read only a generated private thumbnail");
    GUID format{}; UINT width = 0, height = 0;
    Check(SUCCEEDED(decoder->GetContainerFormat(&format)) && format == GUID_ContainerFormatPng &&
        SUCCEEDED(frame->GetSize(&width, &height)) && width == expectedWidth && height == expectedHeight,
        "native thumbnails are PNGs with aspect-preserving bounded dimensions");
    if (alpha) {
        ComPtr<IWICFormatConverter> converter; BYTE pixel[4]{}; WICRect bounds{0, 0, 1, 1};
        Check(SUCCEEDED(factory->CreateFormatConverter(&converter)) && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) && SUCCEEDED(converter->CopyPixels(&bounds, 4, 4, pixel)) && pixel[3] == 128,
            "the alpha channel survives gallery thumbnail publication");
    }
}
}
int main() {
    try {
        Fixture fixture;
        Check(inventory_native::Hash({'a', 'b', 'c'}) == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "native encoded identities use SHA-256 rather than a pathname or noncryptographic checksum");
        const auto alpha = AlphaPng();
        const auto alphaPath = fixture.Write(L"alpha.png", alpha); fixture.Write(L"copy.png", alpha);
        const auto hardlink = fixture.source + L"\\alias.png";
        if (CreateHardLinkW(hardlink.c_str(), alphaPath.c_str(), nullptr)) {
            fixture.ownedFiles.push_back(hardlink);
            InventoryFile original, alias; std::wstring error;
            Check(inventory_native::Metadata(alphaPath, original, error) && inventory_native::Metadata(hardlink, alias, error) &&
                !original.fileIdentity.empty() && original.fileIdentity == alias.fileIdentity,
                "native hardlink aliases expose the same file identity when the fixture filesystem supports them");
        }
        const auto landscape = fixture.Write(L"landscape.bmp", LandscapeBmp());
        fixture.Write(L"corrupt.png", {1, 2, 3}); fixture.Write(L"ignored.txt", {4, 5, 6});
        const auto portrait = OrientedImage(true, 6), transpose = OrientedImage(false, 5), transverse = OrientedImage(false, 7);
        fixture.Write(L"portrait.jpg", portrait); fixture.Write(L"transpose.tiff", transpose); fixture.Write(L"transverse.tiff", transverse);
        InventoryOptions options; options.thumbnailLongEdge = 310; options.maxScanMilliseconds = 5000;
        const auto first = ScanGalleryInventory(fixture.source, fixture.cache, {}, {}, options);
        fixture.Keep(first);
        Check(first.complete && !first.cancelled && first.images.size() == 5 && first.hashedFiles == 7 && first.decodedFiles == 6,
            "native scanning deduplicates exact copies and skips corrupt or unsupported synthetic files");
        const auto alphaId = L"sha256:" + inventory_native::Hash(alpha);
        const auto portraitId = L"sha256:" + inventory_native::Hash(portrait), transposeId = L"sha256:" + inventory_native::Hash(transpose),
            transverseId = L"sha256:" + inventory_native::Hash(transverse);
        std::wstring oldLandscape;
        for (const auto& image : first.images) {
            if (image.id == alphaId) CheckPng(image.thumbnailPath, 1, 1, true);
            else if (image.id == portraitId) { CheckPng(image.thumbnailPath, 20, 40, false); CheckOrientationPixels(image.thumbnailPath, 6, true); }
            else if (image.id == transposeId || image.id == transverseId) {
                CheckPng(image.thumbnailPath, 2, 3, false); CheckOrientationPixels(image.thumbnailPath, image.id == transposeId ? 5 : 7, false);
            }
            else { CheckPng(image.thumbnailPath, 310, 155, false); oldLandscape = image.thumbnailPath; }
        }
        const auto unchanged = ScanGalleryInventory(fixture.source, fixture.cache, first.index, {}, options);
        fixture.Keep(unchanged);
        Check(unchanged.complete && unchanged.hashedFiles == 0 && unchanged.decodedFiles == 0,
            "unchanged native inventories perform metadata checks without hashing or decoding again");
        Check(DeleteFileW(landscape.c_str()) != FALSE, "remove only the explicitly created synthetic source BMP");
        const auto removed = ScanGalleryInventory(fixture.source, fixture.cache, unchanged.index, {}, options);
        fixture.Keep(removed);
        Check(removed.complete && removed.images.size() == 4 && inventory_native::Exists(oldLandscape),
            "deleting a source never purges an older thumbnail that a notification could still reference");
        const auto untouched = fixture.root + L"\\cancel-cache";
        const auto canceled = ScanGalleryInventory(fixture.source, untouched, removed.index, [] { return true; }, options);
        Check(canceled.cancelled && !canceled.complete && GetFileAttributesW(untouched.c_str()) == INVALID_FILE_ATTRIBUTES,
            "early cancellation creates no cache and starts no source inspection");
        const auto nestedDirectory = fixture.source + L"\\nested";
        Check(CreateDirectoryW(nestedDirectory.c_str(), nullptr) != FALSE, "create only an owned synthetic album subdirectory");
        fixture.ownedDirectories.push_back(nestedDirectory);
        auto nestedBytes = LandscapeBmp(); nestedBytes[54] = 200;
        fixture.Write(L"nested\\deep.bmp", nestedBytes);
        const auto loop = nestedDirectory + L"\\back-to-source";
        // Optional Developer Mode support: no elevation, junction machinery or
        // external destination. An unavailable link leaves the core case valid.
        const bool linked = CreateSymbolicLinkW(loop.c_str(), fixture.source.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2) != 0;
        if (linked) fixture.ownedDirectories.push_back(loop);
        const auto recursive = ScanGalleryInventory(fixture.source, fixture.cache, removed.index, {}, options);
        fixture.Keep(recursive);
        const auto nestedId = L"sha256:" + inventory_native::Hash(nestedBytes);
        Check(recursive.complete && recursive.images.size() == 5 &&
            std::any_of(recursive.images.begin(), recursive.images.end(), [&](const auto& image) { return image.id == nestedId; }) &&
            std::any_of(recursive.index.files.begin(), recursive.index.files.end(), [](const auto& item) { return item.first.find(L"\\nested\\deep.bmp") != std::wstring::npos; }),
            "default recursive inventory includes a generated image below the selected folder without rereading user photos");
        if (linked) Check(recursive.index.files.size() <= removed.index.files.size() + 1,
            "a directory symbolic link back to the source is excluded rather than recursively revisited");
        Check(fixture.Cleanup(), "remove only named owned fixture files and their empty private directories");
        std::cout << checks << " native synthetic gallery inventory checks passed; no user photos or app integration\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1; }
}

// Compile the donor adapter but never enter its app/package/session paths.
#define wWinMain NowPlayingUnusedEntryPoint
#include "../NowPlayingTile.cpp"
#undef wWinMain
#include <iostream>

int wmain()
{
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
    };
    wchar_t temporary[32768]{};
    const auto count = GetTempPathW(_countof(temporary), temporary);
    if (!count || count >= _countof(temporary)) return 2;
    const auto directory = std::wstring(temporary) + L"NowPlayingArtworkFixture-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) return 2;
    g_exeDir = directory;
    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &input, nullptr) != Ok) { RemoveDirectoryW(directory.c_str()); return 2; }
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42; file.bfSize = 58; file.bfOffBits = 54;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = 1; info.biHeight = -1;
    info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = 4;
    std::vector<std::uint8_t> bytes(58, 96);
    memcpy(bytes.data(), &file, sizeof(file)); memcpy(bytes.data() + 14, &info, sizeof(info));
    const auto first = directory + L"\\tile-artwork-fixture-first.jpg";
    const auto second = directory + L"\\tile-artwork-fixture-second.jpg";
    std::wstring error;
    check(SaveSharedArtwork(bytes, first, steady_clock::now() + seconds(2), error),
        "the donor saves shared-decoded fixture pixels atomically");
    {
        Image image(first.c_str());
        GUID format{};
        check(image.GetLastStatus() == Ok && image.GetWidth() == 1 && image.GetHeight() == 1 &&
            image.GetRawFormat(&format) == Ok && format == ImageFormatJPEG,
            "the retained notification artifact preserves valid JPEG content");
    }
    check(!SaveSharedArtwork(bytes, second, steady_clock::now() - milliseconds(1), error) && !error.empty() && !FileExists(second),
        "an expired donor deadline writes no notification image");
    check(!SaveSharedArtwork({1, 2, 3}, second, steady_clock::now() + seconds(2), error) && !error.empty() && !FileExists(second),
        "malformed images retain the donor's no-artwork fallback");
    bytes[54] = 192;
    check(SaveSharedArtwork(bytes, second, steady_clock::now() + seconds(2), error), "a second bounded artwork update completes");
    CleanupOldArtwork(second);
    check(!FileExists(first) && FileExists(second), "donor cleanup preserves only the selected fixture notification image");
    GdiplusShutdown(token);
    DeleteFileW(second.c_str());
    check(RemoveDirectoryW(directory.c_str()) != FALSE, "donor fixture leaves no image or temporary sibling behind");
    std::cout << checks << " inert donor artwork checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}

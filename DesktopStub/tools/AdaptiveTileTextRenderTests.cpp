// Compile the actual host without entering its lifecycle. All inputs and
// snapshots are synthetic; no playback/provider/package/Startup API is called.
#define DESKTOPSTUB_INERT_HARDWARE
#define wWinMain DesktopStubUnusedEntryPoint
#include "../DesktopStub.cpp"
#undef wWinMain
#include <iostream>
static int checks = 0, failures = 0;
static void Check(bool value, const char* label) { ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
static std::vector<ARGB> Pixels(Bitmap& bitmap)
{
    std::vector<ARGB> result(bitmap.GetWidth() * bitmap.GetHeight());
    Rect bounds(0, 0, bitmap.GetWidth(), bitmap.GetHeight()); BitmapData data{};
    if (bitmap.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) { Check(false, "bitmap can be read"); return {}; }
    for (UINT y = 0; y < bitmap.GetHeight(); ++y)
        memcpy(result.data() + y * bitmap.GetWidth(), static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
            bitmap.GetWidth() * sizeof(ARGB));
    Check(bitmap.UnlockBits(&data) == Ok, "bitmap read lock closes");
    return result;
}
static std::vector<RectF> Regions(Bitmap& bitmap, tiletext::TileSize size, const tiletext::TileTextInputs& input, double scale)
{
    Graphics metrics(&bitmap);
    auto captionFamily = CreateSystemTileFontFamily(L"Segoe UI");
    auto titleFamily = CreateSystemTileFontFamily(L"Segoe UI Semilight");
    Font caption(captionFamily.get(), static_cast<REAL>(tiletext::AdaptiveCaptionEm * scale), FontStyleRegular, UnitPixel);
    Font title(titleFamily.get(), static_cast<REAL>(tiletext::AdaptiveTitleEm * scale), FontStyleRegular, UnitPixel);
    std::vector<RectF> result;
    for (const auto& region : tiletext::ComputeAdaptiveTileTextLayout(size, input,
            caption.GetHeight(&metrics) / scale, title.GetHeight(&metrics) / scale))
        result.push_back(ScaleTileRect(static_cast<REAL>(region.x), static_cast<REAL>(region.y),
            static_cast<REAL>(region.width), static_cast<REAL>(region.height), scale));
    return result;
}
static void Inspect(Bitmap& bitmap, const std::vector<RectF>& regions, ARGB background)
{
    const auto pixels = Pixels(bitmap);
    if (pixels.empty()) return;
    std::vector<unsigned> ink(regions.size()); unsigned outside = 0, border = 0;
    for (UINT y = 0; y < bitmap.GetHeight(); ++y) for (UINT x = 0; x < bitmap.GetWidth(); ++x) {
        const auto pixel = pixels[y * bitmap.GetWidth() + x];
        if (pixel == background) continue;
        bool inside = false;
        for (std::size_t i = 0; i < regions.size(); ++i) {
            const auto& r = regions[i];
            if (x >= r.X && y >= r.Y && x < r.GetRight() && y < r.GetBottom()) { inside = true; ++ink[i]; }
        }
        if (!inside) ++outside;
        if (!x || !y || x + 1 == bitmap.GetWidth() || y + 1 == bitmap.GetHeight()) ++border;
    }
    Check(outside == 0, "all pixels outside adaptive text regions retain the exact source background");
    Check(border == 0, "all border pixels retain source color and alpha without a plate or band");
    for (unsigned count : ink) Check(count > 0, "every selected adaptive field has visible glyph pixels");
}
int wmain(int argc, wchar_t** argv)
{
    struct Deadline {
        std::atomic<bool> done{false};
        std::thread thread{[this] {
            const auto expires = steady_clock::now() + seconds(45);
            while (!done && steady_clock::now() < expires) std::this_thread::sleep_for(milliseconds(50));
            if (!done) std::_Exit(124);
        }};
        ~Deadline() { done = true; thread.join(); }
    } deadline;
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(_countof(temporary), temporary)) return 2;
    const auto directory = std::wstring(temporary) + L"DesktopStubAdaptiveTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) return 2;
    const auto output = argc > 1 ? aip::MakeAbsolutePath(argv[1]) : directory;
    DWORD error = 0;
    if (!EnsureParentDirectoryForFile(output + L"\\sample.png", &error)) return 2;
    g_iniPath = directory + L"\\fixture.ini"; g_exePath = directory + L"\\AdaptiveFixture.exe";
    g_logPath = directory + L"\\fixture.log";
    g_logging = g_console = g_notificationsEnabled = false;
    const auto profile = L"[TileText]\r\nEnabled=1\r\nApplyToMediumTile=1\r\nApplyToWideTile=1\r\nApplyToLargeTile=1\r\n"
        L"Text=AAAA\r\nSecondaryText=IIII\r\nBadgeText=7\r\n";
    std::wstring loaded;
    Check(WriteUtf8BomTextFile(g_iniPath, profile) && ReadIniFileForMutation(loaded), "private adaptive fixture profile is initialized");
    g_manifestCompatibilityTarget = static_cast<int>(ManifestCompatibilityTarget::Windows10);
    g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Adaptive);
    g_liveTileUpdateMode = static_cast<int>(LiveTileUpdateMode::Registration);
    g_renderContent = nullptr;
    Check(ShouldUseAdaptiveStaticTextFallback(), "legacy default text uses selected Windows 10 adaptive fallback");
    aip::content::Snapshot snapshot; snapshot.active = true; snapshot.textMode = aip::content::TextMode::Auto;
    g_renderContent = &snapshot;
    Check(ShouldUseAdaptiveStaticTextFallback(), "composed automatic text uses selected adaptive fallback");
    snapshot.textMode = aip::content::TextMode::Overlay;
    Check(ShouldUseAdaptiveStaticTextFallback(), "Overlay forces bitmap delivery while preserving the selected adaptive layout");
    snapshot.textMode = aip::content::TextMode::Off;
    Check(!ShouldUseAdaptiveStaticTextFallback(), "Off excludes all static text layout selection");
    snapshot.textMode = aip::content::TextMode::Auto;
    g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Windows81Preset);
    Check(!ShouldUseAdaptiveStaticTextFallback(), "explicit Windows81Preset keeps its established static geometry");
    g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Adaptive);
    for (const auto target : {ManifestCompatibilityTarget::Windows8, ManifestCompatibilityTarget::Windows81}) {
        g_manifestCompatibilityTarget = static_cast<int>(target);
        Check(!ShouldUseAdaptiveStaticTextFallback(), "older Windows targets retain preset bitmap fallback");
    }
    g_manifestCompatibilityTarget = static_cast<int>(ManifestCompatibilityTarget::Windows10);
    {
        ScopedGdiPlus gdi;
        if (!gdi.ok()) return 2;
        const wchar_t* names[] = {L"MediumTile", L"WideTile", L"LargeTile"};
        const int widths[] = {150, 310, 310}, heights[] = {150, 150, 310};
        const auto raster = [&](int size, double requestedScale, Color background) {
            const int width = static_cast<int>(std::lround(widths[size] * requestedScale));
            const int height = static_cast<int>(std::lround(heights[size] * requestedScale));
            auto bitmap = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
            { Graphics graphics(bitmap.get()); Check(graphics.Clear(background) == Ok, "synthetic source canvas is initialized"); }
            Check(ApplyTileTextOverlay(bitmap.get(), names[size], widths[size], heights[size]), "production adaptive static branch renders successfully");
            return bitmap;
        };
        for (bool transparent : {false, true}) for (int size = 0; size < 3; ++size)
        for (double requestedScale : {0.8, 1.0, 1.25, 1.5, 1.8, 2.0, 4.0}) for (unsigned mask = 0; mask < 8; ++mask) {
            snapshot.backgroundId = transparent ? aip::content::BackgroundId::Transparent : aip::content::BackgroundId::Image;
            snapshot.textMode = aip::content::TextMode::Auto;
            snapshot.text = {mask & 1 ? L"AAAA" : L"", mask & 2 ? L"IIII\nMMMM" : L"", mask & 4 ? L"7" : L""};
            const Color background = transparent ? Color(0, 0, 0, 0) : Color(255, 21, 63, 117);
            auto bitmap = raster(size, requestedScale, background);
            const double scale = std::min(static_cast<double>(bitmap->GetWidth()) / widths[size], static_cast<double>(bitmap->GetHeight()) / heights[size]);
            const tiletext::TileTextInputs input{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
            const auto regions = Regions(*bitmap, static_cast<tiletext::TileSize>(size), input, scale);
            Inspect(*bitmap, regions, background.GetValue());
            snapshot.textMode = aip::content::TextMode::Overlay;
            auto overlay = raster(size, requestedScale, background);
            Check(Pixels(*overlay) == Pixels(*bitmap), "Auto fallback and Overlay have exact identical fields/layout/background for the selected adaptive template");
            Inspect(*overlay, regions, background.GetValue());
            snapshot.textMode = aip::content::TextMode::Auto;
            if (mask == 7 && (requestedScale == 1 || requestedScale == 2)) {
                const auto path = output + L"\\adaptive-" + std::to_wstring(size) + L"-" +
                    std::to_wstring(static_cast<int>(requestedScale * 100)) + (transparent ? L"-alpha.png" : L"-image.png");
                Check(SavePNG(bitmap.get(), path.c_str()), "adaptive sample PNG is saved for visual review");
                Bitmap reloaded(path.c_str());
                Check(reloaded.GetLastStatus() == Ok && Pixels(reloaded) == Pixels(*bitmap), "PNG encoding preserves exact adaptive pixels and alpha");
                if (output == directory) DeleteFileW(path.c_str());
            }
        }
        for (int size = 0; size < 3; ++size) {
            snapshot.backgroundId = aip::content::BackgroundId::Image;
            snapshot.textMode = aip::content::TextMode::Auto;
            snapshot.text = {L"AAAA", L"IIII", L"7"};
            const Color background(255, 21, 63, 117);
            const auto baseline = Pixels(*raster(size, 1, background));
            snapshot.text.secondary = L"MMMM";
            const auto changed = Pixels(*raster(size, 1, background));
            Check(baseline != changed, "adaptive secondary text changes every supported tile size even when primary and badge are present");
            snapshot.textMode = aip::content::TextMode::Off;
            auto hidden = raster(size, 1, background);
            Inspect(*hidden, {}, background.GetValue());
        }
        snapshot.textMode = aip::content::TextMode::Auto;
        snapshot.text = {L"AAAA", L"IIII", L"7"};
        std::vector<LiveTileUpdateAsset> assets;
        for (const auto& asset : g_liveTileAssets) {
            const auto relative = std::wstring(asset.file);
            Check(EnsureParentDirectoryForFile(directory + L"\\" + relative, &error), "native XML fixture asset directory is created");
            Bitmap bitmap(asset.w, asset.h, PixelFormat32bppARGB);
            { Graphics graphics(&bitmap); graphics.Clear(Color(255, 21, 63, 117)); }
            Check(SavePNG(&bitmap, (directory + L"\\" + relative).c_str()), "native XML fixture image is valid");
            assets.push_back({asset.name, asset.file, asset.binding, relative, asset.w, asset.h});
        }
        ManifestDisplayInfo manifest;
        for (unsigned mask = 0; mask < 8; ++mask) {
            snapshot.text = {mask & 1 ? L"PRIMARY_MARKER" : L"", mask & 2 ? L"SECONDARY_MARKER" : L"", mask & 4 ? L"BADGE_MARKER" : L""};
            const auto xml = BuildLiveTileXml(directory, manifest, assets);
            Check(!xml.empty(), "native XML remains valid for every adaptive field mask");
            const wchar_t* markers[] = {L"PRIMARY_MARKER", L"SECONDARY_MARKER", L"BADGE_MARKER"};
            for (int field = 0; field < 3; ++field) Check((xml.find(markers[field]) != std::wstring::npos) == (((mask >> field) & 1) != 0),
                "native XML field selection agrees with the portable adaptive fallback");
            Check((xml.find(L"hint-weight=\"3\"") != std::wstring::npos) == ((mask & 4) && (mask & 3)), "native grouping matches the fallback column policy");
            snapshot.textMode = aip::content::TextMode::Overlay;
            const auto overlayXml = BuildLiveTileXml(directory, manifest, assets);
            Check(!overlayXml.empty() && overlayXml.find(L"<image ") != std::wstring::npos &&
                overlayXml.find(L"<text ") == std::wstring::npos && overlayXml.find(L"hint-overlay=\"0\"") != std::wstring::npos,
                "forced adaptive bitmap overlay uses image-only XML with no duplicated text or darkening");
            snapshot.textMode = aip::content::TextMode::Auto;
        }
        for (const auto& asset : assets) DeleteFileW((directory + L"\\" + asset.file).c_str());
        RemoveDirectoryW((directory + L"\\Assets").c_str());
    }
    g_renderContent = nullptr;
    DeleteFileW(g_iniPath.c_str()); DeleteFileW(g_logPath.c_str()); RemoveDirectoryW(directory.c_str());
    std::cout << checks << " adaptive render/host checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}

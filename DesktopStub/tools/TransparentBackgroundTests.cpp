// Real host composition, raster and notification XML, without entering the
// product lifecycle. No provider, package, Startup, or notification API runs.
#define DESKTOPSTUB_INERT_HARDWARE
#define wWinMain DesktopStubUnusedEntryPoint
#include "../DesktopStub.cpp"
#undef wWinMain
#include <iostream>

static int checks = 0, failures = 0;
static void Check(bool condition, const char* label)
{
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
static bool WriteFixture(const std::wstring& text)
{
    std::wstring loaded;
    return WriteUtf8BomTextFile(g_iniPath, text) && ReadIniFileForMutation(loaded) && loaded == text;
}
static bool FindMenuCommand(HMENU menu, UINT command, std::wstring& label, UINT& state)
{
    for (int position = 0; position < GetMenuItemCount(menu); ++position) {
        wchar_t text[512]{};
        MENUITEMINFOW item{}; item.cbSize = sizeof(item);
        item.fMask = MIIM_SUBMENU | MIIM_ID | MIIM_STATE | MIIM_STRING;
        item.dwTypeData = text; item.cch = _countof(text);
        if (!GetMenuItemInfoW(menu, position, TRUE, &item)) continue;
        if (item.hSubMenu && FindMenuCommand(item.hSubMenu, command, label, state)) return true;
        if (!item.hSubMenu && item.wID == command) { label = text; state = item.fState; return true; }
    }
    return false;
}
struct RenderSnapshotScope {
    const aip::content::Snapshot* previous = g_renderContent;
    explicit RenderSnapshotScope(const aip::content::Snapshot& snapshot) { g_renderContent = &snapshot; }
    ~RenderSnapshotScope() { g_renderContent = previous; }
};
static void InspectAlpha(Bitmap& bitmap, bool expectText, tiletext::TileSize size, double scale)
{
    std::vector<RectF> permitted;
    if (ShouldUseAdaptiveStaticTextFallback()) {
        Graphics metrics(&bitmap);
        auto captionFamily = CreateSystemTileFontFamily(L"Segoe UI");
        auto titleFamily = CreateSystemTileFontFamily(L"Segoe UI Semilight");
        Font caption(captionFamily.get(), static_cast<REAL>(tiletext::AdaptiveCaptionEm * scale), FontStyleRegular, UnitPixel);
        Font title(titleFamily.get(), static_cast<REAL>(tiletext::AdaptiveTitleEm * scale), FontStyleRegular, UnitPixel);
        for (const auto& region : tiletext::ComputeAdaptiveTileTextLayout(size, {true, true, true},
                caption.GetHeight(&metrics) / scale, title.GetHeight(&metrics) / scale))
            permitted.emplace_back(static_cast<REAL>(region.x * scale), static_cast<REAL>(region.y * scale),
                static_cast<REAL>(region.width * scale), static_cast<REAL>(region.height * scale));
    } else {
        for (const auto& region : tiletext::ComputeTileTextLayout(size, {true, true, true}))
            permitted.emplace_back(static_cast<REAL>(region.x * scale), static_cast<REAL>(region.y * scale),
                static_cast<REAL>(region.width * scale), static_cast<REAL>(region.height * scale));
    }
    Rect bounds(0, 0, bitmap.GetWidth(), bitmap.GetHeight());
    BitmapData data{};
    const bool locked = bitmap.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppARGB, &data) == Ok;
    Check(locked, "raster can be inspected as RGBA");
    if (!locked) return;
    unsigned ink = 0, unexpectedForeground = 0, transparent = 0, outsideLayout = 0, borderInk = 0;
    for (UINT y = 0; y < bitmap.GetHeight(); ++y) {
        const auto* row = static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        for (UINT x = 0; x < bitmap.GetWidth(); ++x) {
            const auto* pixel = row + x * 4;
            if (!pixel[3]) ++transparent;
            else {
                ++ink;
                // GDI+ converts between straight and premultiplied alpha.
                // White antialiased edges can round to 254 in all RGB channels;
                // fully opaque glyph cores remain exactly white.
                const bool whiteCore = pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255;
                const bool roundedWhiteEdge = pixel[3] < 255 && pixel[0] >= 254 &&
                    pixel[0] == pixel[1] && pixel[1] == pixel[2];
                if (!whiteCore && !roundedWhiteEdge) ++unexpectedForeground;
                const bool inside = std::any_of(permitted.begin(), permitted.end(), [&](const auto& region) {
                    return x >= region.X && y >= region.Y && x < region.GetRight() && y < region.GetBottom();
                });
                if (!inside) ++outsideLayout;
                if (!x || !y || x + 1 == bitmap.GetWidth() || y + 1 == bitmap.GetHeight()) ++borderInk;
            }
        }
    }
    bitmap.UnlockBits(&data);
    Check(transparent != 0, "tile retains fully transparent pixels around content");
    Check((ink != 0) == expectText, "text delivery controls visible raster ink");
    Check(unexpectedForeground == 0, "visible pixels are white glyph cores or grayscale white antialiased edges");
    Check(outsideLayout == 0, "every pixel outside selected text layout regions has exactly zero alpha");
    Check(borderInk == 0, "every tile border pixel has exactly zero alpha, excluding rectangular fills and bands");
    Color corner;
    Check(bitmap.GetPixel(0, 0, &corner) == Ok && corner.GetA() == 0, "outer tile corner stays transparent");
}
int wmain(int argc, wchar_t** argv)
{
    struct Deadline {
        std::atomic<bool> done{false};
        std::thread thread{[this] {
            const auto expires = steady_clock::now() + seconds(30);
            while (!done && steady_clock::now() < expires) std::this_thread::sleep_for(milliseconds(50));
            if (!done) std::_Exit(124);
        }};
        ~Deadline() { done = true; thread.join(); }
    } deadline;
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(_countof(temporary), temporary)) return 2;
    const auto directory = std::wstring(temporary) + L"DesktopStubTransparentTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) return 2;
    const auto output = argc > 1 ? aip::MakeAbsolutePath(argv[1]) : directory;
    DWORD error = 0;
    if (!EnsureParentDirectoryForFile(output + L"\\sample.png", &error)) return 2;
    g_iniPath = directory + L"\\content.ini";
    g_exePath = directory + L"\\TransparentFixture.exe"; // Private synthetic product temp directory.
    g_logPath = directory + L"\\fixture.log";
    g_logging = g_notificationsEnabled = g_console = false;
    g_keepGdiPlusLoaded = false;
    auto profile = BuildInitialIniTemplate();
    // This fixture explicitly covers legacy numbered-content configuration;
    // typed primary/fallback behavior is covered by the host defaults fixture.
    aip::RemoveIniSectionFromText(profile,L"Tiles");
    aip::RemoveIniSectionFromText(profile,L"Tile.desktop");
    for (const aip::IniSetting& setting : std::vector<aip::IniSetting>{
        {L"Content", L"Enabled", L"1"}, {L"Content", L"Count", L"1"},
        {L"Content.1", L"Background", L"Transparent"}, {L"Content.1", L"TextSources", L"CustomText"},
        {L"Content.1", L"Text", L"Test & title"}, {L"Content.1", L"SecondaryText", L"Second line"},
        {L"Content.1", L"BadgeText", L"42"}, {L"Settings", L"ManifestBackgroundColor", L"#123456"}})
        if (!aip::WriteIniValueToText(profile, setting.section.c_str(), setting.key.c_str(), setting.value)) return 2;
    Check(WriteFixture(profile), "temporary profile is initialized without product lifecycle");
    LoadUiStrings();
    aip::content::Configuration configuration; std::wstring issue;
    Check(ReadContentConfiguration(configuration, issue) && configuration.entries[0].background == L"Transparent",
        "direct INI accepts the canonical transparent background");
    std::wstring canonical = L" transparent ";
    Check(aip::content::NormalizeSetting(L"Content.1", L"Background", canonical, issue) && canonical == L"Transparent",
        "CLI and tray shared normalization canonicalizes transparency");
    const auto* descriptor = aip::content::FindBackgroundDescriptor(L"Transparent");
    Check(descriptor && descriptor->id == aip::content::BackgroundId::Transparent, "shared catalog resolves transparent mode");
    if (!descriptor) return 2;
    const UINT choice = static_cast<UINT>(descriptor - aip::content::Backgrounds);
    const UINT command = ID_CONTENT_ENTRY_OPTIONS + 16 + choice;
    HMENU menu = CreatePopupMenu();
    AppendContentEngineMenu(menu);
    std::wstring label; UINT state = 0;
    Check(FindMenuCommand(menu, command, label, state) && label == L"Nothing / transparent" && (state & MFS_CHECKED),
        "tray exposes the same selected background with a clear label");
    DestroyMenu(menu);
    Check(DispatchContentEngineCommand(nullptr, ID_CONTENT_ENTRY_OPTIONS + 16 + 3) &&
        IniReadS(L"Content.1", L"Background", L"") == L"None", "existing solid-color tray command retains None compatibility");
    Check(DispatchContentEngineCommand(nullptr, command) && IniReadS(L"Content.1", L"Background", L"") == L"Transparent",
        "tray saves transparency to the same INI key");
    aip::content::Entry entry; entry.background = L"Transparent";
    const auto sourcePath = ContentBackgroundPath(entry);
    Check(!sourcePath.empty() && sourcePath.find(L"ContentTransparent-") != std::wstring::npos,
        "transparent source is synthesized in the private product temporary directory");
    const auto before = FileFingerprint(sourcePath);
    Check(ContentBackgroundPath(entry) == sourcePath && FileFingerprint(sourcePath) == before,
        "unchanged refresh does not rewrite the transparent source");
    entry.background = L"None";
    const auto solidPath = ContentBackgroundPath(entry);
    {
        ScopedGdiPlus gdi;
        if (!gdi.ok()) return 2;
        Bitmap source(sourcePath.c_str()); Color pixel;
        Check(source.GetLastStatus() == Ok && source.GetWidth() == 1 && source.GetHeight() == 1 &&
            source.GetPixel(0, 0, &pixel) == Ok && pixel.GetA() == 0, "synthetic PNG decodes to an alpha-zero RGBA pixel");
        Bitmap solid(solidPath.c_str());
        Check(solid.GetLastStatus() == Ok && solid.GetPixel(0, 0, &pixel) == Ok && pixel.GetA() == 255 &&
            pixel.GetR() == 0x12 && pixel.GetG() == 0x34 && pixel.GetB() == 0x56,
            "None still supplies the opaque configured manifest color");
        aip::content::Snapshot snapshot;
        snapshot.active = true; snapshot.backgroundPath = sourcePath;
        snapshot.backgroundId = aip::content::BackgroundId::Transparent;
        snapshot.text = {L"Title & content", L"Second line", L"42"};
        RenderSnapshotScope scope(snapshot);
        Check(ContentHasTransparentBackground() && ContentCanvasBackgroundColor().GetA() == 0,
            "immutable snapshot selects transparent canvas behavior");
        std::unique_ptr<Bitmap> prepared; Bitmap* renderSource = nullptr; FitMode fit = FitMode::Tile;
        Check(PrepareWallpaperRenderSource(&source, fit, prepared, renderSource) && fit == FitMode::Fill &&
            renderSource == &source && !prepared, "blank background bypasses desktop Tile and Span preparation");
        const std::wstring transparentKey = BuildGenerationStateKey(sourcePath);
        snapshot.backgroundId = aip::content::BackgroundId::None;
        Check(ContentCanvasBackgroundColor().GetA() == 255 && BuildGenerationStateKey(sourcePath) != transparentKey,
            "cache identity includes background intent even with an identical source path");
        snapshot.backgroundId = aip::content::BackgroundId::Transparent;
        const wchar_t* names[] = {L"MediumTile", L"WideTile", L"LargeTile"};
        const int widths[] = {150, 310, 310}, heights[] = {150, 150, 310};
        for (const auto mode : {aip::content::TextMode::Auto, aip::content::TextMode::Overlay, aip::content::TextMode::Off}) {
            snapshot.textMode = mode;
            for (int scale : {100, 200}) for (int size = 0; size < 3; ++size) {
                std::unique_ptr<Bitmap> bitmap(ResizeBitmapToMode(&source, widths[size] * scale / 100,
                    heights[size] * scale / 100, FitMode::Fill, ContentCanvasBackgroundColor()));
                Check(bitmap && ApplyTileTextOverlay(bitmap.get(), names[size], widths[size], heights[size]),
                    "real transparent asset resize and text composition succeed");
                if (!bitmap) continue;
                InspectAlpha(*bitmap, mode != aip::content::TextMode::Off, static_cast<tiletext::TileSize>(size), scale / 100.0);
                const auto path = output + L"\\transparent-" + std::to_wstring(size) + L"-" +
                    std::to_wstring(static_cast<int>(mode)) + L"-" + std::to_wstring(scale) + L".png";
                Check(SavePNG(bitmap.get(), path.c_str()), "transparent raster sample is saved for visual review");
                Bitmap reloaded(path.c_str());
                Check(reloaded.GetLastStatus() == Ok, "saved PNG reopens without losing alpha support");
                InspectAlpha(reloaded, mode != aip::content::TextMode::Off, static_cast<tiletext::TileSize>(size), scale / 100.0);
                if (output == directory) DeleteFileW(path.c_str());
            }
        }
        std::vector<LiveTileUpdateAsset> assets;
        const wchar_t* liveNames[] = {L"LiveMediumTile", L"LiveWideTile", L"LiveLargeTile"};
        const wchar_t* bindings[] = {L"TileMedium", L"TileWide", L"TileLarge"};
        for (int size = 0; size < 3; ++size) {
            const auto file = L"tile-" + std::to_wstring(size) + L".png";
            std::unique_ptr<Bitmap> bitmap(ResizeBitmapToMode(&source, widths[size], heights[size], FitMode::Fill, ContentCanvasBackgroundColor()));
            Check(bitmap && SavePNG(bitmap.get(), (directory + L"\\" + file).c_str()), "native XML alpha-image fixture is valid");
            assets.push_back({liveNames[size], g_liveTileAssets[size].file, bindings[size], file, widths[size], heights[size]});
        }
        ManifestDisplayInfo manifest;
        g_manifestCompatibilityTarget = static_cast<int>(ManifestCompatibilityTarget::Windows10);
        g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Adaptive);
        snapshot.textMode = aip::content::TextMode::Auto;
        const auto native = BuildLiveTileXml(directory, manifest, assets);
        Check(!native.empty() && native.find(L"<text ") != std::wstring::npos && native.find(L"<image ") == std::wstring::npos &&
            native.find(L"Title &amp; content") != std::wstring::npos, "Windows 10 adaptive automatic text emits no app background image");
        Check(!ShouldBakeTileTextIntoAssets(true) && ShouldBakeTileTextIntoAssets(false),
            "automatic text stays native in Windows 10 and is baked for static tiles");
        auto absentImages = assets;
        for (auto& asset : absentImages) asset.file = L"missing.png";
        Check(!BuildLiveTileXml(directory, manifest, absentImages).empty(), "native text-only XML does not depend on PNG files");
        snapshot.textMode = aip::content::TextMode::Overlay;
        const auto overlay = BuildLiveTileXml(directory, manifest, assets);
        Check(!overlay.empty() && overlay.find(L"<image ") != std::wstring::npos && overlay.find(L"<text ") == std::wstring::npos &&
            overlay.find(L"hint-overlay=\"0\"") != std::wstring::npos && ShouldBakeTileTextIntoAssets(true),
            "explicit overlay uses the alpha image without duplicated native text or shell darkening");
        snapshot.textMode = aip::content::TextMode::Off;
        const auto hidden = BuildLiveTileXml(directory, manifest, assets);
        Check(!hidden.empty() && hidden.find(L"<image ") != std::wstring::npos && hidden.find(L"<text ") == std::wstring::npos,
            "empty transparent content retains a valid alpha-image binding");
        snapshot.textMode = aip::content::TextMode::Auto;
        g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Windows81Preset);
        const auto preset = BuildLiveTileXml(directory, manifest, assets);
        Check(preset.find(L"TileSquare150x150Block") != std::wstring::npos && preset.find(L"TileSquare310x310BlockAndText02") != std::wstring::npos &&
            preset.find(L"<image ") != std::wstring::npos, "explicit Windows 8.1 preset remains selected with transparent image fallback");
        for (const auto target : {ManifestCompatibilityTarget::Windows8, ManifestCompatibilityTarget::Windows81}) {
            g_manifestCompatibilityTarget = static_cast<int>(target);
            const auto old = BuildLiveTileXml(directory, manifest, assets);
            Check(!old.empty() && old.find(L"<image ") != std::wstring::npos && old.find(L"<text ") == std::wstring::npos &&
                ShouldBakeTileTextIntoAssets(true), "older target uses alpha images with baked text rather than unsupported adaptive XML");
        }
        for (const auto& asset : assets) DeleteFileW((directory + L"\\" + asset.file).c_str());
    }
    DeleteFileW(sourcePath.c_str()); DeleteFileW(solidPath.c_str());
    RemoveDirectoryW(GetLiveWallpaperSnapshotDirectory().c_str());
    DeleteFileW(g_iniPath.c_str()); DeleteFileW(g_logPath.c_str()); RemoveDirectoryW(directory.c_str());
    std::cout << checks << " transparent background checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}

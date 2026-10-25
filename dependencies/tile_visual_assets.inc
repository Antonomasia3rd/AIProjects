#ifndef AIP_TILE_VISUAL_ASSETS_INC
#define AIP_TILE_VISUAL_ASSETS_INC
#include "tile_visual_assets.h"
#include <gdiplus.h>
#include <mutex>
#include <memory>
#include <algorithm>
#include <map>
#include <set>

namespace aip { namespace tile_visual {
inline const wchar_t* FontFamilyFor(FontStyle style) {
    return style == FontStyle::Fluent ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
}
inline bool InstalledGlyph(FontStyle style, wchar_t character) {
    const auto dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    LOGFONTW description{}; description.lfHeight = -64;
    wcscpy_s(description.lfFaceName, FontFamilyFor(style));
    const auto font = CreateFontIndirectW(&description);
    const auto previous = font ? SelectObject(dc, font) : nullptr;
    bool available = false;
    if (previous && previous != HGDI_ERROR) {
        wchar_t actual[LF_FACESIZE]{}; WORD glyph = 0xffff;
        available = GetTextFaceW(dc, LF_FACESIZE, actual) > 0 && _wcsicmp(actual, description.lfFaceName) == 0 &&
            GetGlyphIndicesW(dc, &character, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && glyph != 0 && glyph != 0xffff;
        SelectObject(dc, previous);
    }
    if (font) DeleteObject(font);
    DeleteDC(dc); return available;
}
inline FontStyle FontForGlyph(Glyph glyph) {
    static std::mutex mutex; static std::map<Glyph, FontStyle> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = cache.find(glyph);
    if (found != cache.end()) return found->second;
    const auto selected = SelectFont(glyph, InstalledGlyph);
    cache.emplace(glyph, selected); return selected;
}
inline bool DrawNativeTopicGlyph(Gdiplus::Graphics& graphics, const NativeGlyph& native,
        FontStyle style, Gdiplus::Color ink) {
    using namespace Gdiplus;
    FontFamily family(FontFamilyFor(style));
    if (family.GetLastStatus() != Ok || !family.IsStyleAvailable(FontStyleRegular)) return false;
    GraphicsPath combined;
    const auto add = [&](wchar_t glyph, const RectF& target) {
        GraphicsPath path;
        if (path.AddString(&glyph, 1, &family, FontStyleRegular, 100.0f, PointF(0, 0), StringFormat::GenericTypographic()) != Ok) return false;
        RectF actual;
        if (path.GetBounds(&actual) != Ok || actual.Width <= 0 || actual.Height <= 0) return false;
        const REAL scale = (std::min)(target.Width / actual.Width, target.Height / actual.Height);
        Matrix transform(scale, 0, 0, scale,
            target.X + (target.Width - actual.Width * scale) / 2 - actual.X * scale,
            target.Y + (target.Height - actual.Height * scale) / 2 - actual.Y * scale);
        return path.Transform(&transform) == Ok && combined.AddPath(&path, FALSE) == Ok;
    };
    if (!add(native.primary, native.secondary ? RectF(9, 20, 35, 60) : RectF(12, 12, 76, 76)) ||
        (native.secondary && !add(native.secondary, RectF(57, 20, 35, 60)))) return false;
    SolidBrush brush(ink);
    if (graphics.FillPath(&brush, &combined) != Ok) return false;
    if (native.slash) { Pen slash(ink, 6); slash.SetStartCap(LineCapRound); slash.SetEndCap(LineCapRound); return graphics.DrawLine(&slash, 16.0f, 84.0f, 84.0f, 16.0f) == Ok; }
    return true;
}
// The caller already owns GDI+ when drawing an inline glyph. Geometry is
// normalized and kept inside the supplied square bounds. Missing system-font
// glyphs use the repo's vector fallback rather than font substitution/tofu.
inline bool DrawTileTopicGlyph(Gdiplus::Graphics& graphics, const std::wstring& key,
        const Gdiplus::RectF& bounds, Gdiplus::Color ink = Gdiplus::Color(255, 255, 255, 255)) {
    const auto* topic = FindTopic(key);
    if (!topic || bounds.Width <= 0 || bounds.Height <= 0) return false;
    using namespace Gdiplus;
    const auto saved = graphics.Save();
    const REAL side = (std::min)(bounds.Width, bounds.Height);
    graphics.TranslateTransform(bounds.X + (bounds.Width - side) / 2, bounds.Y + (bounds.Height - side) / 2);
    graphics.ScaleTransform(side / 100, side / 100);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    const auto font = FontForGlyph(topic->glyph);
    if (font != FontStyle::Vector && DrawNativeTopicGlyph(graphics, NativeGlyphFor(topic->glyph), font, ink)) {
        graphics.Restore(saved); return true;
    }
    Pen pen(ink, 7); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    SolidBrush brush(ink);
    const auto line = [&](REAL x1, REAL y1, REAL x2, REAL y2) { graphics.DrawLine(&pen, x1, y1, x2, y2); };
    const auto triangle = [&](bool reverse, REAL offset) {
        PointF points[3] = {{reverse ? 68.0f + offset : 32.0f + offset, 23},
            {reverse ? 68.0f + offset : 32.0f + offset, 77}, {reverse ? 27.0f + offset : 73.0f + offset, 50}};
        graphics.FillPolygon(&brush, points, 3);
    };
    switch (topic->glyph) {
    case Glyph::Desktop:
        graphics.DrawRectangle(&pen,12.0f,18.0f,76.0f,52.0f);line(50,70,50,83);line(32,83,68,83);
        graphics.FillRectangle(&brush,21.0f,27.0f,27.0f,20.0f);line(58,53,76,53);break;
    case Glyph::Clock:
        graphics.DrawEllipse(&pen,15.0f,15.0f,70.0f,70.0f);line(50,50,50,28);line(50,50,67,59);break;
    case Glyph::WorldClock: {
        // Independent vector interpretation of the installed world's clock:
        // a clock above/left of a globe, with a transparent separation between
        // them. No Microsoft bitmap or font outline is copied into the app.
        pen.SetWidth(5);
        GraphicsPath globeSeparation;globeSeparation.AddEllipse(36.0f,36.0f,56.0f,56.0f);
        const auto clockState=graphics.Save();
        graphics.SetClip(&globeSeparation,CombineModeExclude);
        graphics.DrawEllipse(&pen,10.0f,10.0f,46.0f,46.0f);
        line(33,33,33,18);line(33,33,45,33);
        graphics.Restore(clockState);
        graphics.DrawEllipse(&pen,40.0f,40.0f,48.0f,48.0f);
        graphics.DrawEllipse(&pen,54.0f,40.0f,20.0f,48.0f);
        line(40,64,88,64);
        graphics.DrawArc(&pen,40.0f,48.0f,48.0f,16.0f,0.0f,180.0f);
        graphics.DrawArc(&pen,40.0f,64.0f,48.0f,16.0f,180.0f,180.0f);
        break;
    }
    case Glyph::Calendar:
        graphics.DrawRectangle(&pen,18.0f,25.0f,64.0f,58.0f);line(18,41,82,41);line(32,16,32,31);line(68,16,68,31);
        for(REAL x:{33.0f,50.0f,67.0f})for(REAL y:{55.0f,70.0f})graphics.FillRectangle(&brush,x-3.0f,y-3.0f,6.0f,6.0f);break;
    case Glyph::Prayer: {
        GraphicsPath crescent;
        crescent.AddBezier(73, 15, 32, -1, 4, 29, 15, 65);
        crescent.AddBezier(15, 65, 25, 94, 66, 99, 87, 64);
        crescent.AddBezier(87, 64, 40, 82, 26, 32, 73, 15);
        crescent.CloseFigure();
        graphics.FillPath(&brush, &crescent);
        graphics.FillEllipse(&brush, 68.0f, 22.0f, 9.0f, 9.0f); break;
    }
    case Glyph::Photo: {
        graphics.DrawRectangle(&pen, 16.0f, 20.0f, 68.0f, 60.0f);
        graphics.FillEllipse(&brush, 57.0f, 30.0f, 12.0f, 12.0f);
        PointF mountain[3] = {{22, 72}, {43, 45}, {65, 72}};
        graphics.FillPolygon(&brush, mountain, 3);
        PointF hill[3] = {{48, 72}, {65, 53}, {78, 72}};
        graphics.FillPolygon(&brush, hill, 3); break;
    }
    case Glyph::Music:
        line(40, 24, 40, 70); line(72, 16, 72, 62); line(40, 24, 72, 16); line(40, 34, 72, 26);
        graphics.FillEllipse(&brush, 18.0f, 61.0f, 26.0f, 19.0f); graphics.FillEllipse(&brush, 50.0f, 53.0f, 26.0f, 19.0f); break;
    case Glyph::Feed:
        graphics.FillEllipse(&brush, 18.0f, 69.0f, 13.0f, 13.0f);
        graphics.DrawArc(&pen, -18.0f, 39.0f, 95.0f, 95.0f, 270, 90);
        graphics.DrawArc(&pen, -39.0f, 18.0f, 137.0f, 137.0f, 270, 90); break;
    case Glyph::Game: case Glyph::Genshin: case Glyph::StarRail: case Glyph::Zenless: {
        GraphicsPath controller;
        controller.AddBezier(24, 33, 12, 35, 9, 68, 15, 75);
        controller.AddBezier(15, 75, 22, 83, 31, 62, 38, 62);
        controller.AddLine(38, 62, 62, 62);
        controller.AddBezier(62, 62, 69, 62, 78, 83, 85, 75);
        controller.AddBezier(85, 75, 91, 68, 88, 35, 76, 33);
        controller.AddLine(76, 33, 24, 33); controller.CloseFigure();
        graphics.DrawPath(&pen, &controller); line(23, 49, 37, 49); line(30, 42, 30, 56);
        graphics.FillEllipse(&brush, 68.0f, 45.0f, 6.0f, 6.0f); graphics.FillEllipse(&brush, 77.0f, 53.0f, 6.0f, 6.0f);
        pen.SetWidth(4);
        if (topic->glyph == Glyph::Genshin) { line(55, 20, 43, 20); line(43, 20, 43, 8); line(43, 8, 55, 8); line(55, 14, 49, 14); line(55, 14, 55, 20); }
        else if (topic->glyph == Glyph::StarRail) { line(43, 8, 43, 20); line(55, 8, 55, 20); line(43, 14, 55, 14); }
        else if(topic->glyph==Glyph::Zenless) { line(43, 8, 55, 8); line(55, 8, 43, 20); line(43, 20, 55, 20); }
        break;
    }
    case Glyph::Chat: {
        GraphicsPath bubble; bubble.AddLine(22, 24, 78, 24); bubble.AddArc(70, 24, 16, 16, 270, 90);
        bubble.AddLine(86, 32, 86, 61); bubble.AddArc(70, 53, 16, 16, 0, 90);
        bubble.AddLine(78, 69, 45, 69); bubble.AddLine(45, 69, 26, 83); bubble.AddLine(26, 83, 26, 69);
        bubble.AddArc(14, 53, 16, 16, 90, 90); bubble.AddLine(14, 61, 14, 32); bubble.AddArc(14, 24, 16, 16, 180, 90); bubble.CloseFigure();
        graphics.DrawPath(&pen, &bubble);
        for (REAL x : {32.0f, 47.0f, 62.0f}) graphics.FillEllipse(&brush, x, 42.0f, 7.0f, 7.0f); break;
    }
    case Glyph::Play: triangle(false, 0); break;
    case Glyph::Pause: graphics.FillRectangle(&brush, 28.0f, 24.0f, 15.0f, 52.0f); graphics.FillRectangle(&brush, 57.0f, 24.0f, 15.0f, 52.0f); break;
    case Glyph::Toggle: { PointF play[3] = {{19, 24}, {19, 76}, {55, 50}}; graphics.FillPolygon(&brush, play, 3);
        graphics.FillRectangle(&brush, 63.0f, 24.0f, 8.0f, 52.0f); graphics.FillRectangle(&brush, 77.0f, 24.0f, 8.0f, 52.0f); break; }
    case Glyph::Previous: triangle(true, 0); line(21, 25, 21, 75); break;
    case Glyph::Next: triangle(false, 0); line(79, 25, 79, 75); break;
    case Glyph::Repeat: case Glyph::RepeatOff: case Glyph::RepeatTrack:
        line(24, 34, 76, 34); line(76, 34, 76, 53); line(76, 34, 65, 23); line(76, 34, 65, 45);
        line(76, 66, 24, 66); line(24, 66, 24, 47); line(24, 66, 35, 55); line(24, 66, 35, 77);
        if (topic->glyph == Glyph::RepeatOff) line(16, 84, 84, 16);
        if (topic->glyph == Glyph::RepeatTrack) { pen.SetWidth(4); line(48, 44, 53, 40); line(53, 40, 53, 58); } break;
    case Glyph::Shuffle: case Glyph::ShuffleOff:
        line(20, 27, 80, 73); line(20, 73, 80, 27); line(80, 27, 64, 26); line(80, 27, 77, 43);
        line(80, 73, 64, 74); line(80, 73, 77, 57);
        if (topic->glyph == Glyph::ShuffleOff) line(16, 84, 84, 16); break;
    case Glyph::VolumeDown: case Glyph::VolumeUp: {
        PointF speaker[6] = {{18, 39}, {32, 39}, {50, 24}, {50, 76}, {32, 61}, {18, 61}};
        graphics.FillPolygon(&brush, speaker, 6); line(66, 50, 85, 50);
        if (topic->glyph == Glyph::VolumeUp) line(75, 40, 75, 60); break;
    }
    case Glyph::Stop: graphics.FillRectangle(&brush, 24.0f, 24.0f, 52.0f, 52.0f); break;
    case Glyph::Unknown:
        graphics.DrawArc(&pen, 31.0f, 19.0f, 38.0f, 36.0f, 190, 270); line(50, 55, 50, 63);
        graphics.FillEllipse(&brush, 46.0f, 75.0f, 8.0f, 8.0f); break;
    case Glyph::Resin: {
        GraphicsPath drop; drop.AddBezier(50, 12, 45, 29, 24, 47, 24, 62);
        drop.AddBezier(24, 62, 24, 94, 76, 94, 76, 62); drop.AddBezier(76, 62, 76, 47, 55, 29, 50, 12);
        drop.CloseFigure(); graphics.DrawPath(&pen, &drop); break;
    }
    case Glyph::Stamina: case Glyph::Charge: case Glyph::Battery:
        graphics.DrawRectangle(&pen, 15.0f, 30.0f, 65.0f, 40.0f); graphics.FillRectangle(&brush, 83.0f, 42.0f, 7.0f, 16.0f);
        if (topic->glyph == Glyph::Stamina||topic->glyph==Glyph::Battery) { for (REAL x : {25.0f, 40.0f, 55.0f}) graphics.FillRectangle(&brush, x, 40.0f, 8.0f, 20.0f); }
        else { PointF bolt[6] = {{51, 36}, {34, 53}, {45, 53}, {41, 65}, {60, 46}, {49, 46}}; graphics.FillPolygon(&brush, bolt, 6); }
        break;
    }
    const auto result = graphics.GetLastStatus(); graphics.Restore(saved); return result == Ok;
}
inline bool DrawCornerTopicGlyph(Gdiplus::Graphics& destination,const std::wstring& topic,unsigned width,unsigned height,Gdiplus::Color ink){
    using namespace Gdiplus;Bitmap source(128,128,PixelFormat32bppARGB);
    {Graphics graphics(&source);graphics.Clear(Color(0,0,0,0));if(source.GetLastStatus()!=Ok||!DrawTileTopicGlyph(graphics,topic,RectF(0,0,128,128),ink))return false;}
    Rect area(0,0,128,128);BitmapData pixels{};if(source.LockBits(&area,ImageLockModeRead,PixelFormat32bppARGB,&pixels)!=Ok)return false;
    INT left=128,top=128,right=-1,bottom=-1;for(INT y=0;y<128;++y){const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const BYTE*>(pixels.Scan0)+static_cast<std::ptrdiff_t>(y)*pixels.Stride);
        for(INT x=0;x<128;++x)if(row[x]>>24){left=(std::min)(left,x);top=(std::min)(top,y);right=(std::max)(right,x);bottom=(std::max)(bottom,y);}}
    if(source.UnlockBits(&pixels)!=Ok||right<left||bottom<top||width<=2||height<=2)return false;
    const REAL factor=(std::min)(static_cast<REAL>(width-2)/(right-left+1),static_cast<REAL>(height-2)/(bottom-top+1));
    const REAL drawnWidth=(right-left+1)*factor,drawnHeight=(bottom-top+1)*factor;const auto saved=destination.Save();
    destination.SetClip(Rect(1,1,static_cast<INT>(width-2),static_cast<INT>(height-2)));destination.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    const auto status=destination.DrawImage(&source,RectF((width-drawnWidth)*.5f,(height-drawnHeight)*.5f,drawnWidth,drawnHeight),static_cast<REAL>(left),static_cast<REAL>(top),static_cast<REAL>(right-left+1),static_cast<REAL>(bottom-top+1),UnitPixel);
    destination.Restore(saved);return status==Ok;
}

inline bool EnsureTileTopicAssets(const std::wstring& key, const std::wstring& directory,
        Assets& result, std::wstring& error, AssetBackground background = {}) {
    result = {}; error.clear();
    const auto* topic = FindTopic(key);
    if (!topic || directory.empty()) { error = L"Unknown tile artwork topic or empty package directory."; return false; }
    static std::mutex mutex; std::lock_guard<std::mutex> lock(mutex);
    const auto plan = AssetPlan(*topic, FontForGlyph(topic->glyph), background);
    const auto cachedKey = directory + L"/" + plan.front().relative;
    static std::set<std::wstring> ensured;
    const auto publish = [&] { result = {plan[0].relative, plan[1].relative, plan[2].relative, plan[3].relative, plan[4].relative, plan[5].relative}; };
    if (ensured.count(cachedKey)) {
        bool present = true;
        for (const auto& asset : plan) if (!aip::FileExists(directory + L"\\" + asset.relative)) { present = false; break; }
        if (present) { publish(); return true; }
        ensured.erase(cachedKey);
    }
    using namespace Gdiplus;
    struct Session { ULONG_PTR token = 0; ~Session() { if (token) GdiplusShutdown(token); } } session;
    GdiplusStartupInput startup;
    if (GdiplusStartup(&session.token, &startup, nullptr) != Ok) { error = L"Could not initialize tile artwork rendering."; return false; }
    UINT count = 0, bytes = 0;
    if (GetImageEncodersSize(&count, &bytes) != Ok || !bytes) { error = L"PNG encoder unavailable."; return false; }
    auto storage = std::make_unique<BYTE[]>(bytes);
    auto* codecs = reinterpret_cast<ImageCodecInfo*>(storage.get());
    if (GetImageEncoders(count, bytes, codecs) != Ok) { error = L"Could not enumerate PNG encoders."; return false; }
    CLSID png{}; bool found = false;
    for (UINT i = 0; i < count; ++i) if (wcscmp(codecs[i].MimeType, L"image/png") == 0) { png = codecs[i].Clsid; found = true; break; }
    if (!found) { error = L"PNG encoder unavailable."; return false; }
    for (const auto& asset : plan) {
        auto path = directory + L"\\" + asset.relative;
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (aip::FileExists(path)) {
            Bitmap old(path.c_str());
            if (old.GetLastStatus() == Ok && old.GetWidth() == asset.width && old.GetHeight() == asset.height) continue;
        }
        aip::EnsureDirectory(path.substr(0, path.find_last_of(L'\\')));
        Bitmap bitmap(static_cast<INT>(asset.width), static_cast<INT>(asset.height), PixelFormat32bppARGB);
        Graphics graphics(&bitmap);
        if (bitmap.GetLastStatus() != Ok || graphics.GetLastStatus() != Ok) { error = L"Could not create tile artwork bitmap."; return false; }
        const BYTE r = static_cast<BYTE>(background.rgb >> 16), g = static_cast<BYTE>(background.rgb >> 8), b = static_cast<BYTE>(background.rgb);
        // Clear paints every pixel exactly, including the boundary. An
        // antialiased rectangle leaves partially transparent edge pixels.
        graphics.Clear(asset.transparent ? Color(0, 0, 0, 0) : Color(255, r, g, b));
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        const REAL width = static_cast<REAL>(asset.width), height = static_cast<REAL>(asset.height);
        const REAL side = (std::min)(width, height) * .64f;
        const auto ink = DarkArtworkInk(background) ? Color(255, 20, 20, 20) : Color(255, 255, 255, 255);
        const bool corner=asset.width==asset.height&&(asset.width==44||asset.width==30);
        if (!(corner?DrawCornerTopicGlyph(graphics,topic->key,asset.width,asset.height,ink):
            DrawTileTopicGlyph(graphics, topic->key, RectF((width - side) / 2, (height - side) / 2, side, side), ink))) {
            error = L"Could not draw tile topic glyph."; return false;
        }
        const auto temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId());
        const auto status = bitmap.Save(temporary.c_str(), &png, nullptr);
        if (status != Ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); error = L"Could not save tile topic artwork: " + path; return false;
        }
    }
    ensured.insert(cachedKey); publish();
    return true;
}
} }
#endif

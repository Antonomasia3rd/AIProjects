#ifndef AIP_TILE_VISUAL_ASSETS_INC
#define AIP_TILE_VISUAL_ASSETS_INC
#include "tile_visual_assets.h"
#include <gdiplus.h>
#include <mutex>
#include <memory>
#include <algorithm>

namespace aip { namespace tile_visual {
// The caller already owns GDI+ when drawing an inline glyph. Geometry is
// normalized, font-independent and kept inside the supplied square bounds.
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
    Pen pen(ink, 7); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    SolidBrush brush(ink);
    const auto line = [&](REAL x1, REAL y1, REAL x2, REAL y2) { graphics.DrawLine(&pen, x1, y1, x2, y2); };
    const auto triangle = [&](bool reverse, REAL offset) {
        PointF points[3] = {{reverse ? 68.0f + offset : 32.0f + offset, 23},
            {reverse ? 68.0f + offset : 32.0f + offset, 77}, {reverse ? 27.0f + offset : 73.0f + offset, 50}};
        graphics.FillPolygon(&brush, points, 3);
    };
    switch (topic->glyph) {
    case Glyph::Music:
        line(40, 24, 40, 70); line(72, 16, 72, 62); line(40, 24, 72, 16); line(40, 34, 72, 26);
        graphics.FillEllipse(&brush, 18.0f, 61.0f, 26.0f, 19.0f); graphics.FillEllipse(&brush, 50.0f, 53.0f, 26.0f, 19.0f); break;
    case Glyph::Feed:
        graphics.FillEllipse(&brush, 18.0f, 69.0f, 13.0f, 13.0f);
        graphics.DrawArc(&pen, -18.0f, 39.0f, 95.0f, 95.0f, 270, 90);
        graphics.DrawArc(&pen, -39.0f, 18.0f, 137.0f, 137.0f, 270, 90); break;
    case Glyph::Genshin: case Glyph::StarRail: case Glyph::Zenless: {
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
        else { line(43, 8, 55, 8); line(55, 8, 43, 20); line(43, 20, 55, 20); }
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
    case Glyph::Repeat:
        line(24, 34, 76, 34); line(76, 34, 76, 53); line(76, 34, 65, 23); line(76, 34, 65, 45);
        line(76, 66, 24, 66); line(24, 66, 24, 47); line(24, 66, 35, 55); line(24, 66, 35, 77); break;
    case Glyph::Shuffle:
        line(20, 27, 80, 73); line(20, 73, 80, 27); line(80, 27, 64, 26); line(80, 27, 77, 43);
        line(80, 73, 64, 74); line(80, 73, 77, 57); break;
    case Glyph::VolumeDown: case Glyph::VolumeUp: {
        PointF speaker[6] = {{18, 39}, {32, 39}, {50, 24}, {50, 76}, {32, 61}, {18, 61}};
        graphics.FillPolygon(&brush, speaker, 6); line(66, 50, 85, 50);
        if (topic->glyph == Glyph::VolumeUp) line(75, 40, 75, 60); break;
    }
    }
    const auto result = graphics.GetLastStatus(); graphics.Restore(saved); return result == Ok;
}

inline bool EnsureTileTopicAssets(const std::wstring& key, const std::wstring& directory,
        Assets& result, std::wstring& error) {
    result = {}; error.clear();
    const auto* topic = FindTopic(key);
    if (!topic || directory.empty()) { error = L"Unknown tile artwork topic or empty package directory."; return false; }
    static std::mutex mutex; std::lock_guard<std::mutex> lock(mutex);
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
    const auto plan = AssetPlan(*topic);
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
        graphics.Clear(Color(0, 0, 0, 0)); graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        const BYTE r = static_cast<BYTE>(topic->rgb >> 16), g = static_cast<BYTE>(topic->rgb >> 8), b = static_cast<BYTE>(topic->rgb);
        const REAL width = static_cast<REAL>(asset.width), height = static_cast<REAL>(asset.height);
        if (!asset.transparent) {
            LinearGradientBrush background(PointF(0, 0), PointF(width, height), Color(255, r, g, b),
                Color(255, static_cast<BYTE>(r * .66), static_cast<BYTE>(g * .66), static_cast<BYTE>(b * .66)));
            graphics.FillRectangle(&background, 0.0f, 0.0f, width, height);
            SolidBrush halo(Color(20, 255, 255, 255));
            const REAL haloSize = (std::min)(width, height) * .8f;
            graphics.FillEllipse(&halo, (width - haloSize) / 2, (height - haloSize) / 2, haloSize, haloSize);
        } else {
            SolidBrush badge(Color(255, r, g, b));
            graphics.FillEllipse(&badge, width * .06f, height * .06f, width * .88f, height * .88f);
        }
        const REAL side = (std::min)(width, height) * .64f;
        if (!DrawTileTopicGlyph(graphics, topic->key, RectF((width - side) / 2, (height - side) / 2, side, side))) {
            error = L"Could not draw tile topic glyph."; return false;
        }
        const auto temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId());
        const auto status = bitmap.Save(temporary.c_str(), &png, nullptr);
        if (status != Ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); error = L"Could not save tile topic artwork: " + path; return false;
        }
    }
    result = {plan[0].relative, plan[1].relative, plan[2].relative, plan[3].relative, plan[4].relative};
    return true;
}
} }
#endif

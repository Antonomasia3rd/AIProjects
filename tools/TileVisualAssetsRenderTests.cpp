// Bounded offscreen artwork fixture: Windows font/GDI+/temporary PNGs only.
// No package, pin/unpin, notification, provider, profile or player API calls.
#include "../dependencies/desktop_app_baseline.h"
#include <objidl.h>
#include <propidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include "../dependencies/tile_visual_assets.inc"
#include <iostream>
#include <stdexcept>

static int checks = 0;
static void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
int main() {
    try {
        using namespace Gdiplus;
        using namespace aip::tile_visual;
        wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{};
        Check(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"aiv", 0, unique), "isolated artwork directory can be reserved");
        Check(DeleteFileW(unique) && CreateDirectoryW(unique, nullptr), "reserved test file becomes a fresh fixture directory");
        const std::wstring directory = unique;
        struct Session { ULONG_PTR token = 0; ~Session() { if (token) GdiplusShutdown(token); } } session;
        GdiplusStartupInput startup;
        Check(GdiplusStartup(&session.token, &startup, nullptr) == Ok, "offscreen GDI+ session starts");
        constexpr unsigned columns = 5, cell = 112;
        const auto count = static_cast<unsigned>(sizeof(Topics) / sizeof(Topics[0]));
        Bitmap sheet(static_cast<INT>(columns * cell), static_cast<INT>(((count + columns - 1) / columns) * cell), PixelFormat32bppARGB);
        Graphics contact(&sheet); contact.Clear(Color(255, 36, 36, 36));
        unsigned index = 0;
        for (const auto& topic : Topics) {
            Assets assets; std::wstring error;
            Check(EnsureTileTopicAssets(topic.key, directory, assets, error) && error.empty(), "topic PNG set generates in isolated directory");
            const auto style = FontForGlyph(topic.glyph);
            const auto plan = AssetPlan(topic, style);
            for (const auto& asset : plan) {
                const auto path = directory + L"\\" + asset.relative;
                Bitmap bitmap(path.c_str());
                Check(bitmap.GetLastStatus() == Ok && bitmap.GetWidth() == asset.width && bitmap.GetHeight() == asset.height,
                    "generated PNG has the exact planned dimensions");
                Color corner;
                Check(bitmap.GetPixel(0, 0, &corner) == Ok && corner.GetA() == 0,
                    "all default source/action/status logos have no colored plate");
                Color margin;
                Check(bitmap.GetPixel(asset.width / 2, 1, &margin) == Ok && margin.GetA() == 0,
                    "default fallback artwork has no colored halo or circular badge at its margins");
            }
            Bitmap glyph(96, 96, PixelFormat32bppARGB);
            {
                Graphics canvas(&glyph); canvas.Clear(Color(0, 0, 0, 0));
                Check(DrawTileTopicGlyph(canvas, topic.key, RectF(12, 12, 72, 72)), "offscreen topic glyph draws without fallback boxes");
            }
            unsigned visible = 0, margin = 0;
            for (UINT y = 0; y < 96; ++y) for (UINT x = 0; x < 96; ++x) {
                Color pixel; Check(glyph.GetPixel(x, y, &pixel) == Ok, "glyph pixel can be read");
                if (pixel.GetA()) { ++visible; if (x < 6 || y < 6 || x >= 90 || y >= 90) ++margin; }
            }
            Check(visible > 25 && margin == 0, "every glyph is visible and contained in its requested icon region");
            contact.DrawImage(&glyph, static_cast<INT>((index % columns) * cell + 8), static_cast<INT>((index / columns) * cell + 8));
            ++index;
            const auto existing = assets;
            Check(EnsureTileTopicAssets(topic.key, directory, assets, error) && SameLogos(assets, existing), "cached assets preserve their stable versioned paths");
            std::wcout << topic.key << L": " << (style == aip::tile_visual::FontStyle::Fluent ? L"Segoe Fluent Icons" :
                style == aip::tile_visual::FontStyle::Mdl2 ? L"Segoe MDL2 Assets" : L"vector fallback") << L'\n';
        }
        for (const std::uint32_t color : {0x123456u, 0xffffffu}) {
            Assets colored; std::wstring error;
            Check(EnsureTileTopicAssets(L"smtc", directory, colored, error, {true, color}), "explicit Color fallback generates independently");
            Bitmap medium((directory + L"\\" + colored.medium).c_str()); Color corner;
            Check(medium.GetPixel(0, 0, &corner) == Ok && corner.GetValue() == (0xff000000u | color),
                "Color is an explicit solid RGB plate without a gradient/halo");
            Bitmap logo((directory + L"\\" + colored.logo44).c_str());
            Check(logo.GetPixel(0, 0, &corner) == Ok && corner.GetA() == 0, "branding icons remain transparent even for a color tile");
        }
        // Resolve an encoder only for the contact sheet. All asset writes above
        // exercised the actual shared PNG-generation implementation.
        UINT encoderCount = 0, encoderBytes = 0;
        Check(GetImageEncodersSize(&encoderCount, &encoderBytes) == Ok && encoderBytes, "PNG encoder metadata available");
        auto storage = std::make_unique<BYTE[]>(encoderBytes);
        auto* encoders = reinterpret_cast<ImageCodecInfo*>(storage.get());
        Check(GetImageEncoders(encoderCount, encoderBytes, encoders) == Ok, "PNG encoders enumerate");
        bool saved = false;
        const auto output = directory + L"\\TopicContactSheet.png";
        for (UINT i = 0; i < encoderCount; ++i) if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            contact.Flush(); saved = sheet.Save(output.c_str(), &encoders[i].Clsid, nullptr) == Ok; break;
        }
        Check(saved, "topic contact sheet saved for visual review");
        std::wcout << L"Artwork artifacts: " << directory << L"\nContact sheet: " << output << L'\n';
        std::cout << "Offscreen tile artwork: " << checks << " checks passed; no shell or provider integration invoked\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

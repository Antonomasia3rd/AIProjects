// Portable tests of the actual preset XML generator, with host inputs supplied
// in memory. No Windows API, app startup, or file configuration is involved.
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <set>
#include <string>
#include "../../dependencies/DesktopStub/tile_text_layout.h"

static std::wstring fields[3];
static bool applies = true;
static bool TileTextAppliesToTileName(const wchar_t*) { return applies; }
static std::wstring TileTextXmlText() { return fields[0]; }
static std::wstring TileTextXmlSecondaryText() { return fields[1]; }
static std::wstring TileTextXmlBadgeText() { return fields[2]; }
static std::wstring IntToWString(int number) { return std::to_wstring(number); }
// Field markers and the test URI contain no XML special characters. Escaping
// belongs to the shared XML utility suite; this harness tests template routing.
static std::wstring XmlEscape(const std::wstring& text) { return text; }
static bool IEquals(const std::wstring& left, const std::wstring& right)
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
}
#include "../../dependencies/DesktopStub/ga_live_tile_templates.inc"

static int checks = 0, failures = 0;
static void Check(bool ok, const std::string& name)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL - %s\n", name.c_str()); }
}
static bool Contains(const std::wstring& xml, const std::wstring& value)
{
    return xml.find(value) != std::wstring::npos;
}
static void TestMatrix()
{
    const wchar_t* bindings[] = { L"TileMedium", L"TileWide", L"TileLarge" };
    const wchar_t* assets[] = { L"LiveMediumTile", L"LiveWideTile", L"LiveLargeTile" };
    const tiletext::TileSize sizes[] = { tiletext::TileSize::Medium, tiletext::TileSize::Wide, tiletext::TileSize::Large };
    for (int size = 0; size < 3; ++size)
    for (unsigned mask = 0; mask <= 7; ++mask)
    for (bool enabled : { false, true })
    for (bool withImage : { false, true })
    {
        applies = enabled;
        fields[0] = mask & 1 ? L"PRIMARY_MARKER" : L"";
        fields[1] = mask & 2 ? L"SECONDARY_MARKER" : L"";
        fields[2] = mask & 4 ? L"BADGE_MARKER" : L"";
        const unsigned active = enabled ? mask : 0;
        const bool needsImage = size == 2 || (size == 1 && !(active & 4)) || active == 0;
        const bool expectedSuccess = !needsImage || withImage;
        std::wstring xml = L"PREFIX";
        const bool success = AppendWindows81PresetLiveTileBinding(xml, bindings[size], assets[size],
            withImage ? L"ms-appx:///Assets/image.png" : L"");
        const std::string label = "size=" + std::to_string(size) + " mask=" + std::to_string(mask) +
            " enabled=" + std::to_string(enabled) + " image=" + std::to_string(withImage);
        Check(success == expectedSuccess, label + " requires an image only when the template uses one");
        if (!success)
        {
            Check(xml == L"PREFIX", label + " failure does not append partial XML");
            continue;
        }
        Check(Contains(xml, L"</binding>"), label + " closes its binding");
        Check(Contains(xml, L"<image ") == needsImage, label + " emits exactly the expected image slot");
        Check(Contains(xml, L" fallback=") == (size != 2), label + " Windows 8 fallback matches supported sizes");
        tiletext::TileTextInputs inputs{ (active & 1) != 0, (active & 2) != 0, (active & 4) != 0 };
        std::set<int> drawnFields;
        for (const auto& region : tiletext::ComputeTileTextLayout(sizes[size], inputs)) drawnFields.insert(region.sourceField);
        const wchar_t* markers[] = { L"PRIMARY_MARKER", L"SECONDARY_MARKER", L"BADGE_MARKER" };
        for (int field = 0; field < 3; ++field)
            Check(Contains(xml, markers[field]) == (drawnFields.count(field) != 0),
                label + " native template and static overlay agree on field " + std::to_string(field));
    }

    applies = true;
    fields[0] = L"PRIMARY_MARKER";
    fields[1] = L"SECONDARY_MARKER";
    fields[2] = L"BADGE_MARKER";
    std::wstring wide;
    Check(AppendWindows81PresetLiveTileBinding(wide, L"tilewide", L"LiveWideTile", L"image.png"), "binding names are case insensitive");
    Check(Contains(wide, L"<text id=\"1\">PRIMARY_MARKER</text>") &&
        Contains(wide, L"<text id=\"2\">BADGE_MARKER</text>") &&
        Contains(wide, L"<text id=\"3\">SECONDARY_MARKER</text>"), "WideBlockAndText02 uses documented left/block/caption IDs");
    std::wstring large;
    Check(AppendWindows81PresetLiveTileBinding(large, L"TileLarge", L"LiveLargeTile", L"image.png"), "large block XML succeeds");
    Check(Contains(large, L"<text id=\"1\">BADGE_MARKER</text>") &&
        Contains(large, L"<text id=\"2\">PRIMARY_MARKER</text>") &&
        Contains(large, L"<text id=\"3\">SECONDARY_MARKER</text>"), "LargeBlockAndText02 uses documented block/header/header IDs");
    for (const wchar_t* name : { L"", L"TileSmall", static_cast<const wchar_t*>(nullptr) })
    {
        std::wstring xml = L"PREFIX";
        Check(!AppendWindows81PresetLiveTileBinding(xml, name, nullptr, L"image.png") && xml == L"PREFIX",
            "unsupported binding leaves XML unchanged");
    }
}
int main()
{
    TestMatrix();
    std::printf("TileText preset XML: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

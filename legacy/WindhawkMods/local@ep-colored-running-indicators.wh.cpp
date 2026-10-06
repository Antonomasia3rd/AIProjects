// ==WindhawkMod==
// @id              ep-colored-running-indicators
// @name            Classic taskbar colored running indicators
// @description     Restores and customizes per-app colored running indicators on ExplorerPatcher classic taskbars and native Windows 10/Server classic taskbars (17763, 19041-19045, 20348).
// @version         0.10
// @author          ChatGPT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -luser32 -lgdi32
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- behavior:
  - enabled: true
    $name: Enable colored running indicators
    $description: Master switch. When disabled, the taskbar's native indicator color is left unchanged.
  - source: icon
    $name: Color source
    $description: Select where the replacement color comes from.
    $options:
    - icon: App icon (taskbar-derived)
    - fixed: Fixed custom color
    - native: Native taskbar source
  - strength: 100
    $name: Replacement strength (%)
    $description: Mix the selected color with the taskbar's native source color. 0 = native, 100 = full selected color.
  - fallback: native
    $name: If icon color is unavailable/rejected
    $description: Used only when the App icon source can't provide an acceptable color.
    $options:
    - native: Keep the native taskbar color
    - fixed: Use the fixed custom color
  - fixedColor: "#4CC2FF"
    $name: Fixed/fallback color
    $description: RGB hex color, for example #4CC2FF or 4CC2FF.

- iconProcessing:
  - profile: faithful
    $name: Color profile
    $description: Presets adjust the icon color before the taskbar performs its own native theme transform. Choose Custom to use the controls below.
    $options:
    - faithful: Faithful (unchanged; recommended/default)
    - subtle: Subtle
    - vivid: Vivid
    - pastel: Pastel
    - neon: Neon
    - dark: Dark
    - grayscale: Grayscale
    - custom: Custom
  - hueShift: 0
    $name: Hue shift (degrees)
    $description: Custom profile only. Range -180 to 180.
  - saturation: 100
    $name: Saturation (%)
    $description: Custom profile only. 100 = unchanged, 0 = grayscale, up to 300 = very saturated.
  - brightness: 100
    $name: Brightness (%)
    $description: Custom profile only. 100 = unchanged.
  - contrast: 100
    $name: Contrast (%)
    $description: Custom profile only. 100 = unchanged.
  - minSaturation: 0
    $name: Minimum saturation (%)
    $description: After profile processing, raise saturation to at least this amount.
  - maxSaturation: 100
    $name: Maximum saturation (%)
    $description: After profile processing, cap saturation at this amount.
  - minBrightness: 0
    $name: Minimum brightness (%)
    $description: After profile processing, lift colors darker than this value.
  - maxBrightness: 100
    $name: Maximum brightness (%)
    $description: After profile processing, cap colors brighter than this value.
  - invert: false
    $name: Invert RGB
    $description: Invert the selected color before tint/palette processing.

- filter:
  - minRawSaturation: 0
    $name: Reject icon colors below saturation (%)
    $description: Icon source only. If the original icon-derived color is less saturated than this, use the configured fallback.
  - minRawBrightness: 0
    $name: Reject icon colors below brightness (%)
    $description: Icon source only. Reject very dark icon colors below this threshold.
  - maxRawBrightness: 100
    $name: Reject icon colors above brightness (%)
    $description: Icon source only. Reject very bright icon colors above this threshold.

- tint:
  - amount: 0
    $name: Tint amount (%)
    $description: Mix the processed color toward the tint color. 0 = off, 100 = tint color.
  - color: "#FFFFFF"
    $name: Tint color
    $description: RGB hex color used by Tint amount.

- palette:
  - enabled: false
    $name: Snap toward a custom palette
    $description: Finds the nearest palette color and blends toward it.
  - colors: "#E74856,#FF8C00,#FFB900,#7FBA00,#00B7C3,#0078D7,#8764B8,#E3008C"
    $name: Palette colors
    $description: Up to 32 RGB hex colors separated by commas, semicolons, or spaces.
  - strength: 100
    $name: Palette strength (%)
    $description: 0 = no palette effect, 100 = exact nearest palette color.

- finalOutput:
  - enabled: false
    $name: Advanced post-transform adjustments (ExplorerPatcher only)
    $description: ExplorerPatcher backend only. Adjust its final ARGB result after the native theme transform. The native Windows 10 backend ignores finalOutput.
  - hueShift: 0
    $name: Final hue shift (degrees)
    $description: Advanced. Range -180 to 180.
  - saturation: 100
    $name: Final saturation (%)
    $description: Advanced. 100 = unchanged.
  - brightness: 100
    $name: Final brightness (%)
    $description: Advanced. 100 = unchanged.
  - contrast: 100
    $name: Final contrast (%)
    $description: Advanced. 100 = unchanged.
  - opacity: 100
    $name: Final alpha multiplier (%)
    $description: ExplorerPatcher backend only. Multiplies the alpha produced by ExplorerPatcher. 100 = unchanged.

- usability:
  - refreshOnSettingsChange: true
    $name: Refresh taskbar after changing settings
    $description: Invalidates Explorer taskbar windows so new settings appear without waiting for a hover/repaint.
  - logEverySubstitution: false
    $name: Verbose color logging
    $description: Log every substituted color. Keep off for normal use.
*/
// ==/WindhawkModSettings==

//
// Hardened / compatibility-oriented build.
//
// Design change from v0.6:
// ------------------------
// v0.6 successfully restored the colors, but it still rediscovered and called
// ExplorerPatcher's private indicator renderer itself. That meant knowing
// BUTTONRENDERINFO geometry, task-list mode fields, renderer resources, and the
// private renderer's ABI.
//
// v0.7/v0.8 remove all of those dependencies.
//
// Instead, v0.7 lets CTaskBtnGroup::_DrawBar run its complete native code. The
// one thing it hooks is the small color-transform helper that _DrawBar itself
// calls in the ordinary running-indicator path. When that exact call site is
// reached, the hook substitutes ExplorerPatcher's icon-derived hot-track color
// for the default source color. ExplorerPatcher then performs its own theme
// transformation and its own indicator rendering exactly as normal.
//
// Benefits:
//   * no BUTTONRENDERINFO field offsets
//   * no BUTTONRENDERINFOSTATES field offsets
//   * no task-list mode/flag offsets
//   * no private renderer signature
//   * no private drawing-resource offset
//   * progress/attention/special states remain native automatically because
//     those branches don't make the ordinary color-transform call
//   * horizontal/vertical geometry stays entirely inside ExplorerPatcher
//
// The transform hook is restricted by the exact return address of the direct
// call inside _DrawBar, so other users of the same helper are left untouched.
//
// ABI support:
//   Legacy:
//     CTaskBtnGroup::_DrawBar(HDC, BUTTONRENDERINFO const&,
//                             BUTTONRENDERINFOSTATES const&, ITaskItem*)
//
//   Modern (confirmed in ep_taskbar.ge.dll):
//     CTaskBtnGroup::_DrawBar(HDC, BUTTONRENDERINFO const&,
//                             BUTTONRENDERINFOSTATES const&)
//
// For the modern ABI, _GetStatesFromRenderInfo is used only to associate the
// ITaskItem* with its renderInfo pointer. A small per-thread ring avoids relying
// on "the last item" if several buttons are prepared before drawing.
//
// Safety / future compatibility:
//   * any x64 module named ep_taskbar.*.dll is considered
//   * required exported C++ methods/vtables must exist
//   * private member offsets are discovered from the exported constructor
//   * the transform call site is discovered from _DrawBar and must be unique
//   * CTaskBtnGroup and CTaskListWnd objects are checked against exported
//     vtables before private EP methods are called
//   * ITaskGroup/ITaskItem virtual slots are discovered from EP's own exported
//     implementations and checked before the lazy color getter is entered
//   * an unknown layout fails closed: native monochrome indicators remain
//
// This source intentionally targets x86-64 only. ExplorerPatcher also supports
// ARM64, but its machine-code patterns are different and require a separate
// backend.
//
// Backends:
//   * ExplorerPatcher ep_taskbar.*.dll: hardened v0.8 transform-call backend.
//   * Native classic taskbar: 17763, 19041-19045 and Server 2022/20348.
//     The 19041-19045 path is crash-dump/stress validated; 17763/20348 use
//     statically validated branch variants and remain runtime-test candidates.
// ExplorerPatcher takes priority if present. Unknown native builds fail closed.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <windhawk_api.h>

namespace {

using DrawBarOld_t = void(__fastcall*)(
    void* pThis,
    HDC hdc,
    const void* renderInfo,
    const void* renderInfoStates,
    void* taskItem);

using DrawBarNew_t = void(__fastcall*)(
    void* pThis,
    HDC hdc,
    const void* renderInfo,
    const void* renderInfoStates);

using GetStatesFromRenderInfo_t = void(__fastcall*)(
    void* pThis,
    const void* renderInfo,
    bool unknownBool,
    void* taskItem,
    void* renderInfoStates);

using GetGroup_t = void* (__fastcall*)(
    void* taskBtnGroupInterfaceThis);

using GetHotTrackColor_t = HRESULT(__fastcall*)(
    void* taskListWnd,
    void* taskGroup,
    void* taskItem,
    DWORD* color);

// _DrawBar's ordinary running-indicator path calls this helper with:
//
//   ECX = source color (EP uses 0x00RRGGBB for icon-derived colors)
//   EDX = theme-dependent parameter
//   R8D = theme-dependent parameter
//   EAX = transformed ARGB
//
// v0.8 changes only ECX at the exact _DrawBar call site.
using TransformIndicatorColor_t = DWORD(__fastcall*)(
    DWORD sourceColor,
    DWORD parameter1,
    DWORD parameter2);

using LoadLibraryExW_t = HMODULE(WINAPI*)(
    LPCWSTR lpLibFileName,
    HANDLE hFile,
    DWORD dwFlags);

enum class ColorSource {
    Icon,
    Fixed,
    Native,
};

enum class FallbackMode {
    Native,
    Fixed,
};

enum class ColorProfile {
    Faithful,
    Subtle,
    Vivid,
    Pastel,
    Neon,
    Dark,
    Grayscale,
    Custom,
};

struct Settings {
    bool enabled = true;
    ColorSource source = ColorSource::Icon;
    int strength = 100;
    FallbackMode fallback = FallbackMode::Native;
    DWORD fixedColor = 0x004CC2FF;

    ColorProfile profile = ColorProfile::Faithful;
    int hueShift = 0;
    int saturation = 100;
    int brightness = 100;
    int contrast = 100;
    int minSaturation = 0;
    int maxSaturation = 100;
    int minBrightness = 0;
    int maxBrightness = 100;
    bool invert = false;

    int filterMinRawSaturation = 0;
    int filterMinRawBrightness = 0;
    int filterMaxRawBrightness = 100;

    int tintAmount = 0;
    DWORD tintColor = 0x00FFFFFF;

    bool paletteEnabled = false;
    DWORD palette[32] = {};
    int paletteCount = 0;
    int paletteStrength = 100;

    bool finalOutputEnabled = false;
    int finalHueShift = 0;
    int finalSaturation = 100;
    int finalBrightness = 100;
    int finalContrast = 100;
    int finalOpacity = 100;

    bool refreshOnSettingsChange = true;
    bool logEverySubstitution = false;
};

SRWLOCK g_settingsLock = SRWLOCK_INIT;
Settings g_settings;

struct RgbColor {
    double r;
    double g;
    double b;
};

struct HslColor {
    double h;
    double s;
    double l;
};

double Clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

int ClampSetting(
    int value,
    int minimum,
    int maximum) {

    return std::clamp(value, minimum, maximum);
}

RgbColor SourceColorToRgb(DWORD color) {
    return {
        ((color >> 16) & 0xFF) / 255.0,
        ((color >> 8) & 0xFF) / 255.0,
        (color & 0xFF) / 255.0,
    };
}

DWORD RgbToSourceColor(
    const RgbColor& color) {

    const BYTE r = static_cast<BYTE>(
        std::lround(Clamp01(color.r) * 255.0));
    const BYTE g = static_cast<BYTE>(
        std::lround(Clamp01(color.g) * 255.0));
    const BYTE b = static_cast<BYTE>(
        std::lround(Clamp01(color.b) * 255.0));

    return (static_cast<DWORD>(r) << 16) |
           (static_cast<DWORD>(g) << 8) |
           static_cast<DWORD>(b);
}

RgbColor ArgbToRgb(DWORD color) {
    return {
        ((color >> 16) & 0xFF) / 255.0,
        ((color >> 8) & 0xFF) / 255.0,
        (color & 0xFF) / 255.0,
    };
}

DWORD RgbToArgb(
    const RgbColor& color,
    BYTE alpha) {

    const DWORD r = static_cast<DWORD>(
        std::lround(Clamp01(color.r) * 255.0));
    const DWORD g = static_cast<DWORD>(
        std::lround(Clamp01(color.g) * 255.0));
    const DWORD b = static_cast<DWORD>(
        std::lround(Clamp01(color.b) * 255.0));

    return (static_cast<DWORD>(alpha) << 24) |
           (r << 16) |
           (g << 8) |
           b;
}

HslColor RgbToHsl(
    const RgbColor& color) {

    const double maximum =
        std::max({color.r, color.g, color.b});
    const double minimum =
        std::min({color.r, color.g, color.b});
    const double delta = maximum - minimum;

    HslColor result = {};
    result.l = (maximum + minimum) / 2.0;

    if (delta <= 1e-9) {
        result.h = 0.0;
        result.s = 0.0;
        return result;
    }

    result.s =
        result.l > 0.5
            ? delta / (2.0 - maximum - minimum)
            : delta / (maximum + minimum);

    if (maximum == color.r) {
        result.h =
            (color.g - color.b) / delta +
            (color.g < color.b ? 6.0 : 0.0);
    } else if (maximum == color.g) {
        result.h =
            (color.b - color.r) / delta + 2.0;
    } else {
        result.h =
            (color.r - color.g) / delta + 4.0;
    }

    result.h /= 6.0;
    return result;
}

double HueToRgb(
    double p,
    double q,
    double t) {

    if (t < 0.0) {
        t += 1.0;
    }

    if (t > 1.0) {
        t -= 1.0;
    }

    if (t < 1.0 / 6.0) {
        return p + (q - p) * 6.0 * t;
    }

    if (t < 1.0 / 2.0) {
        return q;
    }

    if (t < 2.0 / 3.0) {
        return p +
            (q - p) *
                (2.0 / 3.0 - t) *
                6.0;
    }

    return p;
}

RgbColor HslToRgb(
    const HslColor& color) {

    if (color.s <= 1e-9) {
        return {
            color.l,
            color.l,
            color.l,
        };
    }

    const double q =
        color.l < 0.5
            ? color.l * (1.0 + color.s)
            : color.l +
                  color.s -
                  color.l * color.s;
    const double p =
        2.0 * color.l - q;

    return {
        HueToRgb(p, q, color.h + 1.0 / 3.0),
        HueToRgb(p, q, color.h),
        HueToRgb(p, q, color.h - 1.0 / 3.0),
    };
}

RgbColor BlendRgb(
    const RgbColor& first,
    const RgbColor& second,
    double secondAmount) {

    const double t = Clamp01(secondAmount);
    const double a = 1.0 - t;

    return {
        first.r * a + second.r * t,
        first.g * a + second.g * t,
        first.b * a + second.b * t,
    };
}

double HsvSaturation(
    const RgbColor& color) {

    const double maximum =
        std::max({color.r, color.g, color.b});
    const double minimum =
        std::min({color.r, color.g, color.b});

    if (maximum <= 1e-9) {
        return 0.0;
    }

    return (maximum - minimum) / maximum;
}

double HsvBrightness(
    const RgbColor& color) {

    return std::max({
        color.r,
        color.g,
        color.b,
    });
}

RgbColor ApplyHueSaturation(
    RgbColor color,
    int hueShiftDegrees,
    int saturationPercent) {

    HslColor hsl = RgbToHsl(color);

    hsl.h = std::fmod(
        hsl.h +
            hueShiftDegrees / 360.0,
        1.0);

    if (hsl.h < 0.0) {
        hsl.h += 1.0;
    }

    hsl.s = Clamp01(
        hsl.s *
        (saturationPercent / 100.0));

    return HslToRgb(hsl);
}

RgbColor ApplyBrightnessContrast(
    RgbColor color,
    int brightnessPercent,
    int contrastPercent) {

    const double brightness =
        brightnessPercent / 100.0;
    const double contrast =
        contrastPercent / 100.0;

    auto adjust = [&](double channel) {
        channel =
            (channel - 0.5) *
                contrast +
            0.5;
        channel *= brightness;
        return Clamp01(channel);
    };

    color.r = adjust(color.r);
    color.g = adjust(color.g);
    color.b = adjust(color.b);

    return color;
}

RgbColor ClampSaturation(
    RgbColor color,
    int minimumPercent,
    int maximumPercent) {

    HslColor hsl = RgbToHsl(color);

    const double minimum =
        minimumPercent / 100.0;
    const double maximum =
        maximumPercent / 100.0;

    hsl.s = std::clamp(
        hsl.s,
        minimum,
        maximum);

    return HslToRgb(hsl);
}

RgbColor ClampBrightness(
    RgbColor color,
    int minimumPercent,
    int maximumPercent) {

    const double current =
        HsvBrightness(color);
    const double minimum =
        minimumPercent / 100.0;
    const double maximum =
        maximumPercent / 100.0;
    const double target =
        std::clamp(
            current,
            minimum,
            maximum);

    if (current <= 1e-9) {
        if (target > 0.0) {
            return {
                target,
                target,
                target,
            };
        }

        return color;
    }

    const double scale = target / current;

    color.r = Clamp01(color.r * scale);
    color.g = Clamp01(color.g * scale);
    color.b = Clamp01(color.b * scale);

    return color;
}

void GetProfileAdjustments(
    const Settings& settings,
    int* hue,
    int* saturation,
    int* brightness,
    int* contrast) {

    *hue = 0;
    *saturation = 100;
    *brightness = 100;
    *contrast = 100;

    switch (settings.profile) {
        case ColorProfile::Faithful:
            break;

        case ColorProfile::Subtle:
            *saturation = 80;
            *contrast = 90;
            break;

        case ColorProfile::Vivid:
            *saturation = 135;
            *brightness = 105;
            *contrast = 110;
            break;

        case ColorProfile::Pastel:
            *saturation = 70;
            *brightness = 120;
            *contrast = 80;
            break;

        case ColorProfile::Neon:
            *saturation = 175;
            *brightness = 115;
            *contrast = 125;
            break;

        case ColorProfile::Dark:
            *saturation = 110;
            *brightness = 75;
            *contrast = 110;
            break;

        case ColorProfile::Grayscale:
            *saturation = 0;
            break;

        case ColorProfile::Custom:
            *hue = settings.hueShift;
            *saturation = settings.saturation;
            *brightness = settings.brightness;
            *contrast = settings.contrast;
            break;
    }
}

RgbColor ProcessIconColor(
    RgbColor color,
    const Settings& settings) {

    int hue = 0;
    int saturation = 100;
    int brightness = 100;
    int contrast = 100;

    GetProfileAdjustments(
        settings,
        &hue,
        &saturation,
        &brightness,
        &contrast);

    color = ApplyHueSaturation(
        color,
        hue,
        saturation);

    color = ApplyBrightnessContrast(
        color,
        brightness,
        contrast);

    color = ClampSaturation(
        color,
        settings.minSaturation,
        settings.maxSaturation);

    color = ClampBrightness(
        color,
        settings.minBrightness,
        settings.maxBrightness);

    if (settings.invert) {
        color.r = 1.0 - color.r;
        color.g = 1.0 - color.g;
        color.b = 1.0 - color.b;
    }

    return color;
}

RgbColor ApplyTintAndPalette(
    RgbColor color,
    const Settings& settings) {

    if (settings.tintAmount > 0) {
        color = BlendRgb(
            color,
            SourceColorToRgb(
                settings.tintColor),
            settings.tintAmount / 100.0);
    }

    if (settings.paletteEnabled &&
        settings.paletteCount > 0 &&
        settings.paletteStrength > 0) {

        int nearestIndex = 0;
        double nearestDistance =
            1.0e100;

        for (int i = 0;
             i < settings.paletteCount;
             ++i) {

            const RgbColor candidate =
                SourceColorToRgb(
                    settings.palette[i]);

            const double dr =
                color.r - candidate.r;
            const double dg =
                color.g - candidate.g;
            const double db =
                color.b - candidate.b;

            // Cheap perceptual weighting. The palette operation is optional
            // and happens only on taskbar repaints, so this is plenty precise.
            const double distance =
                dr * dr * 0.299 +
                dg * dg * 0.587 +
                db * db * 0.114;

            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = i;
            }
        }

        color = BlendRgb(
            color,
            SourceColorToRgb(
                settings.palette[
                    nearestIndex]),
            settings.paletteStrength /
                100.0);
    }

    return color;
}

DWORD ApplyFinalOutputAdjustments(
    DWORD transformed,
    const Settings& settings) {

    if (!settings.finalOutputEnabled) {
        return transformed;
    }

    RgbColor color =
        ArgbToRgb(transformed);

    color = ApplyHueSaturation(
        color,
        settings.finalHueShift,
        settings.finalSaturation);

    color = ApplyBrightnessContrast(
        color,
        settings.finalBrightness,
        settings.finalContrast);

    const BYTE originalAlpha =
        static_cast<BYTE>(
            (transformed >> 24) & 0xFF);

    const BYTE adjustedAlpha =
        static_cast<BYTE>(
            std::lround(
                originalAlpha *
                (settings.finalOpacity /
                 100.0)));

    return RgbToArgb(
        color,
        adjustedAlpha);
}

std::wstring GetStringSettingCopy(
    PCWSTR name) {

    PCWSTR value =
        Wh_GetStringSetting(name);

    std::wstring result =
        value ? value : L"";

    if (value) {
        Wh_FreeStringSetting(value);
    }

    return result;
}

bool EqualsI(
    const std::wstring& value,
    PCWSTR expected) {

    return _wcsicmp(
               value.c_str(),
               expected) == 0;
}

bool ParseHexColor(
    std::wstring text,
    DWORD* colorOut) {

    if (!colorOut) {
        return false;
    }

    text.erase(
        std::remove_if(
            text.begin(),
            text.end(),
            [](wchar_t ch) {
                return !!iswspace(ch);
            }),
        text.end());

    if (!text.empty() &&
        text.front() == L'#') {
        text.erase(text.begin());
    } else if (text.size() >= 2 &&
               text[0] == L'0' &&
               (text[1] == L'x' ||
                text[1] == L'X')) {
        text.erase(0, 2);
    }

    if (text.size() != 6) {
        return false;
    }

    wchar_t* end = nullptr;
    const unsigned long value =
        wcstoul(
            text.c_str(),
            &end,
            16);

    if (!end ||
        *end != L'\0') {
        return false;
    }

    const BYTE r =
        static_cast<BYTE>(
            (value >> 16) & 0xFF);
    const BYTE g =
        static_cast<BYTE>(
            (value >> 8) & 0xFF);
    const BYTE b =
        static_cast<BYTE>(
            value & 0xFF);

    *colorOut =
        (static_cast<DWORD>(r) << 16) |
        (static_cast<DWORD>(g) << 8) |
        static_cast<DWORD>(b);
    return true;
}

void ParsePalette(
    const std::wstring& text,
    Settings* settings) {

    settings->paletteCount = 0;

    std::wstring token;

    auto flushToken = [&]() {
        if (token.empty() ||
            settings->paletteCount >= 32) {
            token.clear();
            return;
        }

        DWORD color = 0;

        if (ParseHexColor(
                token,
                &color)) {
            settings->palette[
                settings->paletteCount++] =
                    color;
        }

        token.clear();
    };

    for (wchar_t ch : text) {
        if (ch == L',' ||
            ch == L';' ||
            iswspace(ch)) {
            flushToken();
        } else {
            token.push_back(ch);
        }
    }

    flushToken();
}

Settings LoadSettingsSnapshot() {
    Settings settings;

    const std::wstring source =
        GetStringSettingCopy(
            L"behavior.source");

    const std::wstring profile =
        GetStringSettingCopy(
            L"iconProcessing.profile");

    // When replacing an older local-mod source, Windhawk can briefly expose
    // no values for settings that didn't exist in the previous schema.
    // Treat missing primary comboboxes as "use all built-in defaults" rather
    // than turning numeric defaults into zeroes.
    if (source.empty() ||
        profile.empty()) {
        return settings;
    }

    settings.enabled =
        Wh_GetIntSetting(
            L"behavior.enabled") != 0;

    if (EqualsI(source, L"fixed")) {
        settings.source =
            ColorSource::Fixed;
    } else if (EqualsI(
                   source,
                   L"native")) {
        settings.source =
            ColorSource::Native;
    } else {
        settings.source =
            ColorSource::Icon;
    }

    settings.strength =
        ClampSetting(
            Wh_GetIntSetting(
                L"behavior.strength"),
            0,
            100);

    const std::wstring fallback =
        GetStringSettingCopy(
            L"behavior.fallback");

    settings.fallback =
        EqualsI(fallback, L"fixed")
            ? FallbackMode::Fixed
            : FallbackMode::Native;

    DWORD parsedColor = 0;

    if (ParseHexColor(
            GetStringSettingCopy(
                L"behavior.fixedColor"),
            &parsedColor)) {
        settings.fixedColor =
            parsedColor;
    }

    if (EqualsI(profile, L"subtle")) {
        settings.profile =
            ColorProfile::Subtle;
    } else if (EqualsI(
                   profile,
                   L"vivid")) {
        settings.profile =
            ColorProfile::Vivid;
    } else if (EqualsI(
                   profile,
                   L"pastel")) {
        settings.profile =
            ColorProfile::Pastel;
    } else if (EqualsI(
                   profile,
                   L"neon")) {
        settings.profile =
            ColorProfile::Neon;
    } else if (EqualsI(
                   profile,
                   L"dark")) {
        settings.profile =
            ColorProfile::Dark;
    } else if (EqualsI(
                   profile,
                   L"grayscale")) {
        settings.profile =
            ColorProfile::Grayscale;
    } else if (EqualsI(
                   profile,
                   L"custom")) {
        settings.profile =
            ColorProfile::Custom;
    } else {
        settings.profile =
            ColorProfile::Faithful;
    }

    settings.hueShift =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.hueShift"),
            -180,
            180);

    settings.saturation =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.saturation"),
            0,
            300);

    settings.brightness =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.brightness"),
            0,
            300);

    settings.contrast =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.contrast"),
            0,
            300);

    settings.minSaturation =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.minSaturation"),
            0,
            100);

    settings.maxSaturation =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.maxSaturation"),
            0,
            100);

    if (settings.minSaturation >
        settings.maxSaturation) {
        std::swap(
            settings.minSaturation,
            settings.maxSaturation);
    }

    settings.minBrightness =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.minBrightness"),
            0,
            100);

    settings.maxBrightness =
        ClampSetting(
            Wh_GetIntSetting(
                L"iconProcessing.maxBrightness"),
            0,
            100);

    if (settings.minBrightness >
        settings.maxBrightness) {
        std::swap(
            settings.minBrightness,
            settings.maxBrightness);
    }

    settings.invert =
        Wh_GetIntSetting(
            L"iconProcessing.invert") != 0;

    settings.filterMinRawSaturation =
        ClampSetting(
            Wh_GetIntSetting(
                L"filter.minRawSaturation"),
            0,
            100);

    settings.filterMinRawBrightness =
        ClampSetting(
            Wh_GetIntSetting(
                L"filter.minRawBrightness"),
            0,
            100);

    settings.filterMaxRawBrightness =
        ClampSetting(
            Wh_GetIntSetting(
                L"filter.maxRawBrightness"),
            0,
            100);

    if (settings.filterMinRawBrightness >
        settings.filterMaxRawBrightness) {
        std::swap(
            settings.filterMinRawBrightness,
            settings.filterMaxRawBrightness);
    }

    settings.tintAmount =
        ClampSetting(
            Wh_GetIntSetting(
                L"tint.amount"),
            0,
            100);

    if (ParseHexColor(
            GetStringSettingCopy(
                L"tint.color"),
            &parsedColor)) {
        settings.tintColor =
            parsedColor;
    }

    settings.paletteEnabled =
        Wh_GetIntSetting(
            L"palette.enabled") != 0;

    settings.paletteStrength =
        ClampSetting(
            Wh_GetIntSetting(
                L"palette.strength"),
            0,
            100);

    ParsePalette(
        GetStringSettingCopy(
            L"palette.colors"),
        &settings);

    settings.finalOutputEnabled =
        Wh_GetIntSetting(
            L"finalOutput.enabled") != 0;

    settings.finalHueShift =
        ClampSetting(
            Wh_GetIntSetting(
                L"finalOutput.hueShift"),
            -180,
            180);

    settings.finalSaturation =
        ClampSetting(
            Wh_GetIntSetting(
                L"finalOutput.saturation"),
            0,
            300);

    settings.finalBrightness =
        ClampSetting(
            Wh_GetIntSetting(
                L"finalOutput.brightness"),
            0,
            300);

    settings.finalContrast =
        ClampSetting(
            Wh_GetIntSetting(
                L"finalOutput.contrast"),
            0,
            300);

    settings.finalOpacity =
        ClampSetting(
            Wh_GetIntSetting(
                L"finalOutput.opacity"),
            0,
            100);

    settings.refreshOnSettingsChange =
        Wh_GetIntSetting(
            L"usability.refreshOnSettingsChange") != 0;

    settings.logEverySubstitution =
        Wh_GetIntSetting(
            L"usability.logEverySubstitution") != 0;

    return settings;
}

void LoadSettings() {
    Settings settings =
        LoadSettingsSnapshot();

    AcquireSRWLockExclusive(
        &g_settingsLock);
    g_settings = settings;
    ReleaseSRWLockExclusive(
        &g_settingsLock);

    Wh_Log(
        L"Settings loaded: enabled=%d source=%d strength=%d "
        L"profile=%d palette=%d/%d final=%d.",
        settings.enabled,
        static_cast<int>(settings.source),
        settings.strength,
        static_cast<int>(settings.profile),
        settings.paletteEnabled,
        settings.paletteCount,
        settings.finalOutputEnabled);
}

Settings GetSettingsCopy() {
    Settings settings;

    AcquireSRWLockShared(
        &g_settingsLock);
    settings = g_settings;
    ReleaseSRWLockShared(
        &g_settingsLock);

    return settings;
}

BOOL CALLBACK RefreshTaskbarWindowProc(
    HWND hwnd,
    LPARAM) {

    wchar_t className[128] = {};

    if (!GetClassNameW(
            hwnd,
            className,
            ARRAYSIZE(className))) {
        return TRUE;
    }

    if (_wcsicmp(
            className,
            L"Shell_TrayWnd") == 0 ||
        _wcsicmp(
            className,
            L"Shell_SecondaryTrayWnd") == 0) {

        RedrawWindow(
            hwnd,
            nullptr,
            nullptr,
            RDW_INVALIDATE |
                RDW_ALLCHILDREN);
    }

    return TRUE;
}

void RefreshTaskbars() {
    EnumWindows(
        RefreshTaskbarWindowProc,
        0);
}

DrawBarOld_t g_DrawBarOld_Original = nullptr;
DrawBarNew_t g_DrawBarNew_Original = nullptr;
GetStatesFromRenderInfo_t g_GetStatesFromRenderInfo_Original = nullptr;
GetGroup_t g_GetGroup = nullptr;
GetHotTrackColor_t g_GetHotTrackColor = nullptr;
TransformIndicatorColor_t g_TransformIndicatorColor_Original = nullptr;
LoadLibraryExW_t g_LoadLibraryExW_Original = nullptr;

SIZE_T g_taskListWndOffset = 0;
SIZE_T g_taskBtnGroupInterfaceOffset = 0;
SIZE_T g_taskGroupHotTrackSlot = 0;
SIZE_T g_taskItemHotTrackSlot = 0;

void* g_expectedTaskBtnGroupInterfaceVtable = nullptr;
void* g_expectedTaskListWndPrimaryVtable = nullptr;
void* g_expectedTaskGroupInterfaceVtable = nullptr;
void* g_expectedTaskGroupHotTrackMethod = nullptr;

// Only a transform invocation returning to this instruction is eligible for
// substitution. It is discovered from the selected _DrawBar implementation.
const void* g_transformReturnAddress = nullptr;

std::atomic<bool> g_epHooked = false;
std::atomic<bool> g_epModulePresent = false;
std::atomic<bool> g_validationWarningLogged = false;
std::atomic<bool> g_firstSubstitutionLogged = false;

// New _DrawBar ABI does not receive ITaskItem*. _GetStatesFromRenderInfo does.
// Keep a few recent renderInfo -> taskItem associations per UI thread instead
// of assuming that the last prepared item is always the next one painted.
struct PendingTaskItem {
    const void* renderInfo;
    void* taskItem;
};

constexpr unsigned kPendingTaskItemCount = 8;
thread_local PendingTaskItem
    g_pendingTaskItems[kPendingTaskItemCount] = {};
thread_local unsigned g_pendingTaskItemWriteIndex = 0;

// Nested painting is unlikely, but a stack makes the transform hook correct if
// one _DrawBar invocation causes another one synchronously.
struct DrawBarContext {
    void* taskBtnGroup;
    void* taskItem;
    bool hasTaskItemMapping;
    bool consumed;
    bool inColorLookup;
};

constexpr unsigned kDrawBarContextCount = 16;
thread_local DrawBarContext
    g_drawBarContexts[kDrawBarContextCount] = {};
thread_local unsigned g_drawBarContextDepth = 0;

// If nesting exceeds the fixed stack, suppress substitution until the overflow
// frame(s) unwind. This prevents an outer context from leaking into an inner
// draw.
thread_local unsigned g_drawBarContextOverflowDepth = 0;

// Exported member names observed in both supplied builds where applicable.
constexpr char kDrawBarOldExport[] =
    "?_DrawBar@CTaskBtnGroup@@AEAAXPEAUHDC__@@AEBUBUTTONRENDERINFO@@"
    "AEBUBUTTONRENDERINFOSTATES@@PEAUITaskItem@@@Z";

constexpr char kDrawBarNewExport[] =
    "?_DrawBar@CTaskBtnGroup@@AEAAXPEAUHDC__@@AEBUBUTTONRENDERINFO@@"
    "AEBUBUTTONRENDERINFOSTATES@@@Z";

constexpr char kGetStatesFromRenderInfoExport[] =
    "?_GetStatesFromRenderInfo@CTaskBtnGroup@@AEAAXAEBUBUTTONRENDERINFO@@"
    "_NPEAUITaskItem@@PEAUBUTTONRENDERINFOSTATES@@@Z";

constexpr char kGetGroupExport[] =
    "?GetGroup@CTaskBtnGroup@@UEAAPEAUITaskGroup@@XZ";

constexpr char kGetHotTrackColorExport[] =
    "?GetHotTrackColor_7@CTaskListWnd@@IEAAJPEAUITaskGroup@@"
    "PEAUITaskItem@@PEAK@Z";

constexpr char kTaskGroupGetHotTrackColorExport[] =
    "?GetHotTrackColor_7@CTaskGroup@@UEAAJPEAUITaskItem@@PEAK@Z";

constexpr char kTaskBtnGroupConstructorExport[] =
    "??0CTaskBtnGroup@@AEAA@PEAVCTaskListWnd@@PEAUITaskGroup@@@Z";

constexpr char kTaskBtnGroupITaskBtnGroupVtableExport[] =
    "??_7CTaskBtnGroup@@6BITaskBtnGroup@@@";

constexpr char kTaskListWndPrimaryVtableExport[] =
    "??_7CTaskListWnd@@6BCImpWndProc@@@";

constexpr char kTaskGroupITaskGroupVtableExport[] =
    "??_7CTaskGroup@@6BITaskGroup@@@";

bool StartsWithI(const wchar_t* str, const wchar_t* prefix) {
    if (!str || !prefix) {
        return false;
    }

    while (*prefix) {
        if (!*str || towlower(*str) != towlower(*prefix)) {
            return false;
        }

        ++str;
        ++prefix;
    }

    return true;
}

bool EndsWithI(const wchar_t* str, const wchar_t* suffix) {
    if (!str || !suffix) {
        return false;
    }

    SIZE_T strLength = 0;
    SIZE_T suffixLength = 0;

    while (str[strLength]) {
        ++strLength;
    }

    while (suffix[suffixLength]) {
        ++suffixLength;
    }

    if (suffixLength > strLength) {
        return false;
    }

    const wchar_t* tail =
        str + strLength - suffixLength;

    for (SIZE_T i = 0; i < suffixLength; ++i) {
        if (towlower(tail[i]) !=
            towlower(suffix[i])) {
            return false;
        }
    }

    return true;
}

const wchar_t* BaseNameFromPath(const wchar_t* path) {
    if (!path) {
        return L"";
    }

    const wchar_t* base = path;

    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            base = p + 1;
        }
    }

    return base;
}

bool IsExplorerPatcherTaskbarModule(HMODULE module) {
    if (!module) {
        return false;
    }

    wchar_t path[MAX_PATH];
    const DWORD len =
        GetModuleFileNameW(module, path, ARRAYSIZE(path));

    if (!len || len >= ARRAYSIZE(path)) {
        return false;
    }

    const wchar_t* base = BaseNameFromPath(path);

    return StartsWithI(base, L"ep_taskbar.") &&
           EndsWithI(base, L".dll");
}

HMODULE FindLoadedExplorerPatcherTaskbarModule() {
    const DWORD pid = GetCurrentProcessId();

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        pid);

    if (snapshot == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    HMODULE result = nullptr;

    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (IsExplorerPatcherTaskbarModule(
                    entry.hModule)) {
                result = entry.hModule;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

bool IsReadableMemory(const void* address, SIZE_T bytes) {
    if (!address || bytes == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};

    if (!VirtualQuery(address, &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD) ||
        (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }

    const uintptr_t start =
        reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = start + bytes;

    if (end < start) {
        return false;
    }

    const uintptr_t regionStart =
        reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t regionEnd =
        regionStart + mbi.RegionSize;

    return start >= regionStart &&
           end <= regionEnd;
}

bool IsExecutableAddress(const void* address) {
    if (!address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};

    if (!VirtualQuery(address, &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD) ||
        (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }

    const DWORD protection = mbi.Protect & 0xFF;

    return protection == PAGE_EXECUTE ||
           protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

bool ReadInterfaceSlot(
    void* object,
    SIZE_T slotOffset,
    void** vtableOut,
    void** targetOut) {

    if (!object ||
        !IsReadableMemory(object, sizeof(void*))) {
        return false;
    }

    void* vtable =
        *reinterpret_cast<void**>(object);

    if (!vtable ||
        !IsReadableMemory(
            static_cast<unsigned char*>(vtable) +
                slotOffset,
            sizeof(void*))) {
        return false;
    }

    void* target =
        *reinterpret_cast<void**>(
            static_cast<unsigned char*>(vtable) +
            slotOffset);

    if (!target || !IsExecutableAddress(target)) {
        return false;
    }

    if (vtableOut) {
        *vtableOut = vtable;
    }

    if (targetOut) {
        *targetOut = target;
    }

    return true;
}

bool IsFunctionInVtable(
    void* vtable,
    const void* function,
    SIZE_T maxSlots = 128) {

    if (!vtable || !function) {
        return false;
    }

    for (SIZE_T slot = 0; slot < maxSlots; ++slot) {
        auto* entryAddress =
            static_cast<unsigned char*>(vtable) +
            slot * sizeof(void*);

        if (!IsReadableMemory(
                entryAddress,
                sizeof(void*))) {
            return false;
        }

        void* entry =
            *reinterpret_cast<void**>(entryAddress);

        if (entry == function) {
            return true;
        }

        // Don't scan indefinitely into unrelated data.
        if (slot > 8 &&
            entry &&
            !IsExecutableAddress(entry)) {
            break;
        }
    }

    return false;
}

bool DiscoverTaskListWndOffset(
    const unsigned char* constructor,
    SIZE_T* offset) {

    if (!constructor || !offset) {
        return false;
    }

    // x64 constructor argument #2 is CTaskListWnd* in RDX.
    // Both supplied DLLs use:
    //     48 89 51 28       mov [rcx+28h], rdx

    for (SIZE_T i = 0; i + 4 <= 128; ++i) {
        if (constructor[i] == 0x48 &&
            constructor[i + 1] == 0x89 &&
            constructor[i + 2] == 0x51) {

            const SIZE_T candidate =
                constructor[i + 3];

            if (candidate >= 0x20 &&
                candidate <= 0x200 &&
                (candidate % sizeof(void*)) == 0) {
                *offset = candidate;
                return true;
            }
        }
    }

    // Alternate disp32 encoding:
    //     48 89 91 xx xx xx xx
    for (SIZE_T i = 0; i + 7 <= 128; ++i) {
        if (constructor[i] == 0x48 &&
            constructor[i + 1] == 0x89 &&
            constructor[i + 2] == 0x91) {

            const LONG candidateSigned =
                *reinterpret_cast<const LONG*>(
                    constructor + i + 3);

            if (candidateSigned >= 0x20 &&
                candidateSigned <= 0x200 &&
                (candidateSigned %
                 static_cast<LONG>(
                     sizeof(void*))) == 0) {
                *offset =
                    static_cast<SIZE_T>(
                        candidateSigned);
                return true;
            }
        }
    }

    return false;
}

bool DiscoverTaskBtnGroupInterfaceOffset(
    const unsigned char* constructor,
    const void* expectedVtable,
    SIZE_T* offset) {

    if (!constructor ||
        !expectedVtable ||
        !offset) {
        return false;
    }

    // Find a RIP-relative LEA of the exported ITaskBtnGroup vtable followed
    // shortly by storing that pointer into the CTaskBtnGroup object.
    //
    // Both supplied DLLs resolve this to +0x10.

    for (SIZE_T i = 0; i + 7 <= 128; ++i) {
        if (constructor[i] != 0x48 ||
            constructor[i + 1] != 0x8D ||
            constructor[i + 2] != 0x05) {
            continue;
        }

        const LONG rel =
            *reinterpret_cast<const LONG*>(
                constructor + i + 3);

        const auto* target =
            constructor + i + 7 + rel;

        if (target != expectedVtable) {
            continue;
        }

        const SIZE_T searchEnd =
            (i + 24 < 128)
                ? i + 24
                : 128;

        for (SIZE_T j = i + 7;
             j + 4 <= searchEnd;
             ++j) {

            // mov [rcx+disp8], rax
            if (constructor[j] == 0x48 &&
                constructor[j + 1] == 0x89 &&
                constructor[j + 2] == 0x41) {

                const SIZE_T candidate =
                    constructor[j + 3];

                if (candidate <= 0x200 &&
                    (candidate %
                     sizeof(void*)) == 0) {
                    *offset = candidate;
                    return true;
                }
            }

            // mov [rbx+disp8], rax
            if (constructor[j] == 0x48 &&
                constructor[j + 1] == 0x89 &&
                constructor[j + 2] == 0x43) {

                const SIZE_T candidate =
                    constructor[j + 3];

                if (candidate <= 0x200 &&
                    (candidate %
                     sizeof(void*)) == 0) {
                    *offset = candidate;
                    return true;
                }
            }

            // mov [rcx+disp32], rax
            if (j + 7 <= searchEnd &&
                constructor[j] == 0x48 &&
                constructor[j + 1] == 0x89 &&
                constructor[j + 2] == 0x81) {

                const LONG candidateSigned =
                    *reinterpret_cast<const LONG*>(
                        constructor + j + 3);

                if (candidateSigned >= 0 &&
                    candidateSigned <= 0x200 &&
                    (candidateSigned %
                     static_cast<LONG>(
                         sizeof(void*))) == 0) {
                    *offset =
                        static_cast<SIZE_T>(
                            candidateSigned);
                    return true;
                }
            }

            // mov [rbx+disp32], rax
            if (j + 7 <= searchEnd &&
                constructor[j] == 0x48 &&
                constructor[j + 1] == 0x89 &&
                constructor[j + 2] == 0x83) {

                const LONG candidateSigned =
                    *reinterpret_cast<const LONG*>(
                        constructor + j + 3);

                if (candidateSigned >= 0 &&
                    candidateSigned <= 0x200 &&
                    (candidateSigned %
                     static_cast<LONG>(
                         sizeof(void*))) == 0) {
                    *offset =
                        static_cast<SIZE_T>(
                            candidateSigned);
                    return true;
                }
            }
        }
    }

    return false;
}

bool DiscoverTaskGroupHotTrackSlot(
    const unsigned char* lazyGetter,
    SIZE_T* slotOffset) {

    if (!lazyGetter || !slotOffset) {
        return false;
    }

    // CTaskListWnd::GetHotTrackColor_7 makes an indirect virtual call through
    // ITaskGroup. Both supplied DLLs use slot +0x178.
    for (SIZE_T i = 0; i + 6 <= 128; ++i) {
        if (lazyGetter[i] == 0xFF &&
            lazyGetter[i + 1] == 0x90) {

            const LONG candidateSigned =
                *reinterpret_cast<const LONG*>(
                    lazyGetter + i + 2);

            if (candidateSigned >= 0 &&
                candidateSigned <= 0x800 &&
                (candidateSigned %
                 static_cast<LONG>(
                     sizeof(void*))) == 0) {
                *slotOffset =
                    static_cast<SIZE_T>(
                        candidateSigned);
                return true;
            }
        }
    }

    return false;
}

bool DiscoverTaskItemHotTrackSlot(
    const unsigned char* taskGroupGetter,
    SIZE_T* slotOffset) {

    if (!taskGroupGetter || !slotOffset) {
        return false;
    }

    // CTaskGroup::GetHotTrackColor_7 dispatches through ITaskItem when an item
    // is supplied. Both supplied DLLs use slot +0x208.
    for (SIZE_T i = 0; i + 7 <= 96; ++i) {
        if (taskGroupGetter[i] == 0x48 &&
            taskGroupGetter[i + 1] == 0xFF &&
            taskGroupGetter[i + 2] == 0xA0) {

            const LONG candidateSigned =
                *reinterpret_cast<const LONG*>(
                    taskGroupGetter + i + 3);

            if (candidateSigned >= 0 &&
                candidateSigned <= 0x800 &&
                (candidateSigned %
                 static_cast<LONG>(
                     sizeof(void*))) == 0) {
                *slotOffset =
                    static_cast<SIZE_T>(
                        candidateSigned);
                return true;
            }
        }
    }

    for (SIZE_T i = 0; i + 6 <= 96; ++i) {
        if (taskGroupGetter[i] == 0xFF &&
            taskGroupGetter[i + 1] == 0xA0) {

            const LONG candidateSigned =
                *reinterpret_cast<const LONG*>(
                    taskGroupGetter + i + 2);

            if (candidateSigned >= 0 &&
                candidateSigned <= 0x800 &&
                (candidateSigned %
                 static_cast<LONG>(
                     sizeof(void*))) == 0) {
                *slotOffset =
                    static_cast<SIZE_T>(
                        candidateSigned);
                return true;
            }
        }
    }

    return false;
}

const unsigned char* ResolveRelativeCallTarget(
    const unsigned char* callInstruction) {

    if (!callInstruction ||
        callInstruction[0] != 0xE8) {
        return nullptr;
    }

    const int32_t rel =
        *reinterpret_cast<const int32_t*>(
            callInstruction + 1);

    return callInstruction + 5 + rel;
}

bool DiscoverTransformCallSite(
    const unsigned char* drawBar,
    TransformIndicatorColor_t* transformOut,
    const void** returnAddressOut) {

    if (!drawBar ||
        !transformOut ||
        !returnAddressOut) {
        return false;
    }

    const unsigned char* selectedCall = nullptr;
    const unsigned char* selectedTarget = nullptr;
    unsigned candidateCount = 0;

    // Both supplied _DrawBar implementations contain exactly one:
    //
    //     E8 xx xx xx xx       call TransformIndicatorColor
    //     44 8B C8             mov r9d, eax
    //
    // in the normal running-indicator color path.
    //
    // Requiring uniqueness is intentional: a future build with an ambiguous
    // layout is safer left untouched.
    for (SIZE_T i = 0; i + 8 <= 0x240; ++i) {
        if (drawBar[i] != 0xE8 ||
            drawBar[i + 5] != 0x44 ||
            drawBar[i + 6] != 0x8B ||
            drawBar[i + 7] != 0xC8) {
            continue;
        }

        const unsigned char* target =
            ResolveRelativeCallTarget(
                drawBar + i);

        if (!target ||
            !IsExecutableAddress(target)) {
            continue;
        }

        ++candidateCount;
        selectedCall = drawBar + i;
        selectedTarget = target;
    }

    if (candidateCount != 1 ||
        !selectedCall ||
        !selectedTarget) {
        return false;
    }

    *transformOut =
        reinterpret_cast<
            TransformIndicatorColor_t>(
                const_cast<unsigned char*>(
                    selectedTarget));

    *returnAddressOut = selectedCall + 5;

    return true;
}

void RecordPendingTaskItem(
    const void* renderInfo,
    void* taskItem) {

    if (!renderInfo) {
        return;
    }

    PendingTaskItem& slot =
        g_pendingTaskItems[
            g_pendingTaskItemWriteIndex %
            kPendingTaskItemCount];

    slot.renderInfo = renderInfo;
    slot.taskItem = taskItem;

    ++g_pendingTaskItemWriteIndex;
}

bool ConsumePendingTaskItem(
    const void* renderInfo,
    void** taskItemOut) {

    if (!renderInfo || !taskItemOut) {
        return false;
    }

    for (unsigned distance = 0;
         distance < kPendingTaskItemCount;
         ++distance) {

        const unsigned index =
            (g_pendingTaskItemWriteIndex +
             kPendingTaskItemCount - 1 -
             distance) %
            kPendingTaskItemCount;

        PendingTaskItem& slot =
            g_pendingTaskItems[index];

        if (slot.renderInfo == renderInfo) {
            *taskItemOut = slot.taskItem;
            slot.renderInfo = nullptr;
            slot.taskItem = nullptr;
            return true;
        }
    }

    return false;
}

void EnterDrawBarContext(
    void* taskBtnGroup,
    void* taskItem,
    bool hasTaskItemMapping) {

    if (g_drawBarContextDepth >=
        kDrawBarContextCount) {
        ++g_drawBarContextOverflowDepth;
        return;
    }

    DrawBarContext& context =
        g_drawBarContexts[
            g_drawBarContextDepth++];

    context.taskBtnGroup = taskBtnGroup;
    context.taskItem = taskItem;
    context.hasTaskItemMapping = hasTaskItemMapping;
    context.consumed = false;
    context.inColorLookup = false;
}

void LeaveDrawBarContext() {
    if (g_drawBarContextOverflowDepth) {
        --g_drawBarContextOverflowDepth;
        return;
    }

    if (!g_drawBarContextDepth) {
        return;
    }

    DrawBarContext& context =
        g_drawBarContexts[
            --g_drawBarContextDepth];

    context = {};
}

DrawBarContext* CurrentDrawBarContext() {
    if (g_drawBarContextOverflowDepth ||
        !g_drawBarContextDepth) {
        return nullptr;
    }

    return &g_drawBarContexts[
        g_drawBarContextDepth - 1];
}

bool ValidateTaskBtnGroupRawObject(
    void* pThis) {

    if (!pThis ||
        !g_taskBtnGroupInterfaceOffset ||
        !g_expectedTaskBtnGroupInterfaceVtable) {
        return false;
    }

    auto* interfaceAddress =
        static_cast<unsigned char*>(pThis) +
        g_taskBtnGroupInterfaceOffset;

    if (!IsReadableMemory(
            interfaceAddress,
            sizeof(void*))) {
        return false;
    }

    void* actualVtable =
        *reinterpret_cast<void**>(
            interfaceAddress);

    return actualVtable ==
        g_expectedTaskBtnGroupInterfaceVtable;
}

bool ValidateTaskListWndRawObject(
    void* taskListWnd) {

    if (!taskListWnd ||
        !g_expectedTaskListWndPrimaryVtable ||
        !IsReadableMemory(
            taskListWnd,
            sizeof(void*))) {
        return false;
    }

    void* actualVtable =
        *reinterpret_cast<void**>(
            taskListWnd);

    return actualVtable ==
        g_expectedTaskListWndPrimaryVtable;
}

void LogValidationFailureOnce() {
    bool expected = false;

    if (g_validationWarningLogged
            .compare_exchange_strong(
                expected,
                true)) {
        Wh_Log(
            L"Pointer/interface validation failed; "
            L"leaving this indicator unchanged.");
    }
}

bool TryGetIconHotTrackColor(
    void* pThis,
    void* taskItem,
    DWORD* colorOut) {

    if (!pThis ||
        !colorOut ||
        !g_GetGroup ||
        !g_GetHotTrackColor ||
        !g_taskListWndOffset ||
        !g_taskBtnGroupInterfaceOffset ||
        !g_taskGroupHotTrackSlot ||
        !g_taskItemHotTrackSlot) {
        return false;
    }

    if (!ValidateTaskBtnGroupRawObject(
            pThis)) {
        LogValidationFailureOnce();
        return false;
    }

    auto* raw =
        static_cast<unsigned char*>(pThis);

    auto* taskListWndField =
        raw + g_taskListWndOffset;

    if (!IsReadableMemory(
            taskListWndField,
            sizeof(void*))) {
        LogValidationFailureOnce();
        return false;
    }

    void* taskListWnd =
        *reinterpret_cast<void**>(
            taskListWndField);

    if (!ValidateTaskListWndRawObject(
            taskListWnd)) {
        LogValidationFailureOnce();
        return false;
    }

    void* taskBtnGroupInterface =
        raw + g_taskBtnGroupInterfaceOffset;

    // GetGroup is an ITaskBtnGroup virtual implementation. The adjusted
    // subobject pointer is the ABI-correct `this`.
    void* taskGroup =
        g_GetGroup(
            taskBtnGroupInterface);

    if (!taskGroup) {
        return false;
    }

    void* taskGroupVtable = nullptr;
    void* taskGroupSlotTarget = nullptr;

    if (!ReadInterfaceSlot(
            taskGroup,
            g_taskGroupHotTrackSlot,
            &taskGroupVtable,
            &taskGroupSlotTarget)) {
        LogValidationFailureOnce();
        return false;
    }

    // For the ordinary concrete CTaskGroup object, require the exact exported
    // hot-track method at the slot discovered from CTaskListWnd.
    if (g_expectedTaskGroupInterfaceVtable &&
        g_expectedTaskGroupHotTrackMethod &&
        taskGroupVtable ==
            g_expectedTaskGroupInterfaceVtable &&
        taskGroupSlotTarget !=
            g_expectedTaskGroupHotTrackMethod) {
        LogValidationFailureOnce();
        return false;
    }

    // nullptr is a legitimate grouped/glommed request. Validate the
    // ITaskItem virtual slot only when an item is supplied.
    if (taskItem &&
        !ReadInterfaceSlot(
            taskItem,
            g_taskItemHotTrackSlot,
            nullptr,
            nullptr)) {
        LogValidationFailureOnce();
        return false;
    }

    DWORD color = 0;

    const HRESULT hr =
        g_GetHotTrackColor(
            taskListWnd,
            taskGroup,
            taskItem,
            &color);

    if (hr != S_OK) {
        return false;
    }

    *colorOut = color & 0x00FFFFFF;
    return true;
}

DWORD __fastcall TransformIndicatorColor_Hook(
    DWORD sourceColor,
    DWORD parameter1,
    DWORD parameter2) {

    const DWORD nativeSourceColor =
        sourceColor;

    DrawBarContext* context =
        CurrentDrawBarContext();

    const void* returnAddress =
        __builtin_return_address(0);

    const bool targetCall =
        context &&
        context->hasTaskItemMapping &&
        returnAddress ==
            g_transformReturnAddress &&
        !context->consumed &&
        !context->inColorLookup;

    Settings settings =
        GetSettingsCopy();

    bool substituted = false;
    bool usedIconColor = false;
    bool usedFallbackColor = false;
    DWORD rawIconColor = 0;
    DWORD selectedSourceColor = 0;

    if (targetCall) {
        // Exactly one ordinary indicator transform is expected per _DrawBar.
        // Consume before any call back into ExplorerPatcher so recursion can't
        // accidentally re-enter the customization path.
        context->consumed = true;

        if (settings.enabled &&
            settings.source !=
                ColorSource::Native &&
            settings.strength > 0) {

            bool haveSelectedColor = false;

            if (settings.source ==
                ColorSource::Fixed) {

                selectedSourceColor =
                    settings.fixedColor;
                haveSelectedColor = true;
            } else {
                context->inColorLookup = true;

                const bool gotIconColor =
                    TryGetIconHotTrackColor(
                        context->taskBtnGroup,
                        context->taskItem,
                        &rawIconColor);

                context->inColorLookup = false;

                if (gotIconColor) {
                    const RgbColor rawRgb =
                        SourceColorToRgb(
                            rawIconColor);

                    const int rawSaturation =
                        static_cast<int>(
                            std::lround(
                                HsvSaturation(
                                    rawRgb) *
                                100.0));

                    const int rawBrightness =
                        static_cast<int>(
                            std::lround(
                                HsvBrightness(
                                    rawRgb) *
                                100.0));

                    if (rawSaturation >=
                            settings
                                .filterMinRawSaturation &&
                        rawBrightness >=
                            settings
                                .filterMinRawBrightness &&
                        rawBrightness <=
                            settings
                                .filterMaxRawBrightness) {

                        RgbColor processed =
                            ProcessIconColor(
                                rawRgb,
                                settings);

                        selectedSourceColor =
                            RgbToSourceColor(
                                processed);

                        haveSelectedColor = true;
                        usedIconColor = true;
                    }
                }

                if (!haveSelectedColor &&
                    settings.fallback ==
                        FallbackMode::Fixed) {

                    selectedSourceColor =
                        settings.fixedColor;
                    haveSelectedColor = true;
                    usedFallbackColor = true;
                }
            }

            if (haveSelectedColor) {
                RgbColor selected =
                    SourceColorToRgb(
                        selectedSourceColor);

                selected =
                    ApplyTintAndPalette(
                        selected,
                        settings);

                const RgbColor nativeRgb =
                    SourceColorToRgb(
                        nativeSourceColor &
                        0x00FFFFFF);

                const RgbColor blended =
                    BlendRgb(
                        nativeRgb,
                        selected,
                        settings.strength /
                            100.0);

                // ExplorerPatcher's historic icon-color path supplies a
                // 0x00RRGGBB value here. Keep the high byte clear for custom
                // colors too, and let EP construct the final ARGB itself.
                sourceColor =
                    RgbToSourceColor(
                        blended);

                substituted = true;
            }
        }
    }

    DWORD transformed =
        g_TransformIndicatorColor_Original(
            sourceColor,
            parameter1,
            parameter2);

    if (targetCall &&
        settings.enabled &&
        settings.finalOutputEnabled) {

        transformed =
            ApplyFinalOutputAdjustments(
                transformed,
                settings);
    }

    if (targetCall &&
        settings.enabled &&
        (substituted ||
         settings.finalOutputEnabled)) {

        bool shouldLog =
            settings.logEverySubstitution;

        if (!shouldLog) {
            bool expected = false;

            shouldLog =
                g_firstSubstitutionLogged
                    .compare_exchange_strong(
                        expected,
                        true);
        }

        if (shouldLog) {
            Wh_Log(
                L"Indicator color: native=0x%08X rawIcon=0x%08X "
                L"selected=0x%08X source=0x%08X final=0x%08X "
                L"icon=%d fallback=%d strength=%d.",
                nativeSourceColor,
                rawIconColor,
                selectedSourceColor,
                sourceColor,
                transformed,
                usedIconColor,
                usedFallbackColor,
                settings.strength);
        }
    }

    return transformed;
}

void __fastcall DrawBarOld_Hook(
    void* pThis,
    HDC hdc,
    const void* renderInfo,
    const void* renderInfoStates,
    void* taskItem) {

    EnterDrawBarContext(
        pThis,
        taskItem,
        true);

    g_DrawBarOld_Original(
        pThis,
        hdc,
        renderInfo,
        renderInfoStates,
        taskItem);

    LeaveDrawBarContext();
}

void __fastcall GetStatesFromRenderInfo_Hook(
    void* pThis,
    const void* renderInfo,
    bool unknownBool,
    void* taskItem,
    void* renderInfoStates) {

    g_GetStatesFromRenderInfo_Original(
        pThis,
        renderInfo,
        unknownBool,
        taskItem,
        renderInfoStates);

    RecordPendingTaskItem(
        renderInfo,
        taskItem);
}

void __fastcall DrawBarNew_Hook(
    void* pThis,
    HDC hdc,
    const void* renderInfo,
    const void* renderInfoStates) {

    void* taskItem = nullptr;

    const bool hasTaskItemMapping =
        ConsumePendingTaskItem(
            renderInfo,
            &taskItem);

    EnterDrawBarContext(
        pThis,
        taskItem,
        hasTaskItemMapping);

    g_DrawBarNew_Original(
        pThis,
        hdc,
        renderInfo,
        renderInfoStates);

    LeaveDrawBarContext();
}

bool HookExplorerPatcherTaskbar(
    HMODULE module) {

    if (!module) {
        return false;
    }

    if (g_epHooked.load()) {
        return true;
    }

    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(
        module,
        modulePath,
        ARRAYSIZE(modulePath));

    Wh_Log(
        L"Inspecting ExplorerPatcher taskbar module: %s",
        modulePath[0]
            ? modulePath
            : L"(unknown path)");

    auto* constructor =
        reinterpret_cast<
            const unsigned char*>(
                GetProcAddress(
                    module,
                    kTaskBtnGroupConstructorExport));

    auto getGroup =
        reinterpret_cast<GetGroup_t>(
            GetProcAddress(
                module,
                kGetGroupExport));

    auto getHotTrackColor =
        reinterpret_cast<
            GetHotTrackColor_t>(
                GetProcAddress(
                    module,
                    kGetHotTrackColorExport));

    auto* taskGroupGetHotTrackColor =
        reinterpret_cast<
            const unsigned char*>(
                GetProcAddress(
                    module,
                    kTaskGroupGetHotTrackColorExport));

    auto* taskBtnGroupInterfaceVtable =
        reinterpret_cast<void*>(
            GetProcAddress(
                module,
                kTaskBtnGroupITaskBtnGroupVtableExport));

    auto* taskListWndPrimaryVtable =
        reinterpret_cast<void*>(
            GetProcAddress(
                module,
                kTaskListWndPrimaryVtableExport));

    auto* taskGroupInterfaceVtable =
        reinterpret_cast<void*>(
            GetProcAddress(
                module,
                kTaskGroupITaskGroupVtableExport));

    if (!constructor ||
        !getGroup ||
        !getHotTrackColor ||
        !taskGroupGetHotTrackColor ||
        !taskBtnGroupInterfaceVtable ||
        !taskListWndPrimaryVtable) {
        Wh_Log(
            L"Required EP exports aren't present; "
            L"leaving this build untouched.");
        return false;
    }

    SIZE_T taskListWndOffset = 0;

    if (!DiscoverTaskListWndOffset(
            constructor,
            &taskListWndOffset)) {
        Wh_Log(
            L"Couldn't safely discover the "
            L"CTaskListWnd member offset.");
        return false;
    }

    SIZE_T taskBtnGroupInterfaceOffset = 0;

    if (!DiscoverTaskBtnGroupInterfaceOffset(
            constructor,
            taskBtnGroupInterfaceVtable,
            &taskBtnGroupInterfaceOffset)) {
        Wh_Log(
            L"Couldn't safely discover the "
            L"ITaskBtnGroup subobject offset.");
        return false;
    }

    if (!IsFunctionInVtable(
            taskBtnGroupInterfaceVtable,
            reinterpret_cast<void*>(
                getGroup))) {
        Wh_Log(
            L"Exported GetGroup isn't present in the "
            L"exported ITaskBtnGroup vtable.");
        return false;
    }

    SIZE_T taskGroupHotTrackSlot = 0;

    if (!DiscoverTaskGroupHotTrackSlot(
            reinterpret_cast<
                const unsigned char*>(
                    getHotTrackColor),
            &taskGroupHotTrackSlot)) {
        Wh_Log(
            L"Couldn't safely discover the "
            L"ITaskGroup hot-track virtual slot.");
        return false;
    }

    SIZE_T taskItemHotTrackSlot = 0;

    if (!DiscoverTaskItemHotTrackSlot(
            taskGroupGetHotTrackColor,
            &taskItemHotTrackSlot)) {
        Wh_Log(
            L"Couldn't safely discover the "
            L"ITaskItem hot-track virtual slot.");
        return false;
    }

    void* expectedTaskGroupHotTrackMethod =
        reinterpret_cast<void*>(
            const_cast<unsigned char*>(
                taskGroupGetHotTrackColor));

    if (taskGroupInterfaceVtable) {
        auto* slotAddress =
            static_cast<unsigned char*>(
                taskGroupInterfaceVtable) +
            taskGroupHotTrackSlot;

        if (!IsReadableMemory(
                slotAddress,
                sizeof(void*)) ||
            *reinterpret_cast<void**>(
                slotAddress) !=
                expectedTaskGroupHotTrackMethod) {
            Wh_Log(
                L"Concrete CTaskGroup vtable doesn't "
                L"match the discovered hot-track slot.");
            return false;
        }
    }

    auto drawBarOld =
        reinterpret_cast<DrawBarOld_t>(
            GetProcAddress(
                module,
                kDrawBarOldExport));

    auto drawBarNew =
        reinterpret_cast<DrawBarNew_t>(
            GetProcAddress(
                module,
                kDrawBarNewExport));

    if (!drawBarOld && !drawBarNew) {
        Wh_Log(
            L"Neither recognized _DrawBar ABI is exported.");
        return false;
    }

    // Prefer modern if a transitional build ever exposes both.
    const unsigned char* drawBarForDiscovery =
        reinterpret_cast<
            const unsigned char*>(
                drawBarNew
                    ? reinterpret_cast<void*>(
                          drawBarNew)
                    : reinterpret_cast<void*>(
                          drawBarOld));

    TransformIndicatorColor_t
        transformIndicatorColor = nullptr;
    const void* transformReturnAddress = nullptr;

    if (!DiscoverTransformCallSite(
            drawBarForDiscovery,
            &transformIndicatorColor,
            &transformReturnAddress)) {
        Wh_Log(
            L"Couldn't uniquely identify EP's native "
            L"running-indicator color-transform call.");
        return false;
    }

    GetStatesFromRenderInfo_t
        getStatesFromRenderInfo = nullptr;

    if (drawBarNew) {
        getStatesFromRenderInfo =
            reinterpret_cast<
                GetStatesFromRenderInfo_t>(
                    GetProcAddress(
                        module,
                        kGetStatesFromRenderInfoExport));

        if (!getStatesFromRenderInfo) {
            Wh_Log(
                L"Modern _DrawBar ABI found, but "
                L"_GetStatesFromRenderInfo is missing.");
            return false;
        }
    }

    // Publish only after all static/runtime checks have succeeded.
    g_GetGroup = getGroup;
    g_GetHotTrackColor = getHotTrackColor;

    g_taskListWndOffset =
        taskListWndOffset;
    g_taskBtnGroupInterfaceOffset =
        taskBtnGroupInterfaceOffset;
    g_taskGroupHotTrackSlot =
        taskGroupHotTrackSlot;
    g_taskItemHotTrackSlot =
        taskItemHotTrackSlot;

    g_expectedTaskBtnGroupInterfaceVtable =
        taskBtnGroupInterfaceVtable;
    g_expectedTaskListWndPrimaryVtable =
        taskListWndPrimaryVtable;
    g_expectedTaskGroupInterfaceVtable =
        taskGroupInterfaceVtable;
    g_expectedTaskGroupHotTrackMethod =
        expectedTaskGroupHotTrackMethod;

    g_transformReturnAddress =
        transformReturnAddress;

    Wh_Log(
        L"Discovered CTaskListWnd member offset: 0x%Ix",
        g_taskListWndOffset);
    Wh_Log(
        L"Discovered ITaskBtnGroup subobject offset: 0x%Ix",
        g_taskBtnGroupInterfaceOffset);
    Wh_Log(
        L"Discovered ITaskGroup hot-track slot: 0x%Ix",
        g_taskGroupHotTrackSlot);
    Wh_Log(
        L"Discovered ITaskItem hot-track slot: 0x%Ix",
        g_taskItemHotTrackSlot);
    Wh_Log(
        L"Discovered native transform=%p exact call-return=%p",
        reinterpret_cast<void*>(
            transformIndicatorColor),
        g_transformReturnAddress);

    bool ok = true;

    // Hook the transform before _DrawBar. Hook application is batched until
    // Wh_ModInit returns (or Wh_ApplyHookOperations is called for late loads).
    ok &= !!Wh_SetFunctionHook(
        reinterpret_cast<void*>(
            transformIndicatorColor),
        reinterpret_cast<void*>(
            TransformIndicatorColor_Hook),
        reinterpret_cast<void**>(
            &g_TransformIndicatorColor_Original));

    if (drawBarNew) {
        ok &= !!Wh_SetFunctionHook(
            reinterpret_cast<void*>(
                getStatesFromRenderInfo),
            reinterpret_cast<void*>(
                GetStatesFromRenderInfo_Hook),
            reinterpret_cast<void**>(
                &g_GetStatesFromRenderInfo_Original));

        ok &= !!Wh_SetFunctionHook(
            reinterpret_cast<void*>(
                drawBarNew),
            reinterpret_cast<void*>(
                DrawBarNew_Hook),
            reinterpret_cast<void**>(
                &g_DrawBarNew_Original));

        if (ok) {
            Wh_Log(
                L"ExplorerPatcher colored indicators: "
                L"hooked hardened modern ABI.");
        }
    } else {
        ok &= !!Wh_SetFunctionHook(
            reinterpret_cast<void*>(
                drawBarOld),
            reinterpret_cast<void*>(
                DrawBarOld_Hook),
            reinterpret_cast<void**>(
                &g_DrawBarOld_Original));

        if (ok) {
            Wh_Log(
                L"ExplorerPatcher colored indicators: "
                L"hooked hardened legacy ABI.");
        }
    }

    if (!ok) {
        Wh_Log(
            L"Failed to install one or more EP hooks.");
        return false;
    }

    g_epHooked.store(true);
    return true;
}


namespace NativeWin10 {

std::atomic<bool> g_hooked = false;
std::atomic<bool> g_finalOutputWarningLogged = false;

enum class BuildFamily {
    None,
    Win10_17763,
    Win10_19041,
    Server_20348,
};

enum class ColorSourceHookKind {
    None,
    ImmersiveImpl,
    UxThemeOrdinal121,
};

BuildFamily g_buildFamily = BuildFamily::None;
ColorSourceHookKind g_colorSourceHookKind = ColorSourceHookKind::None;
DWORD g_nativeBuild = 0;
void* g_colorSourceTarget = nullptr;



// ---------------------------------------------------------------------------
// Resolved native signatures.
// ---------------------------------------------------------------------------

using GetStatesFromRenderInfo_t = void(__fastcall*)(
    void* pThis,
    const void* renderInfo,
    bool unknownBool,
    void* taskItem,
    void* renderInfoStates);

using DrawBar_t = void(__fastcall*)(
    void* pThis,
    HDC hdc,
    const void* renderInfo,
    const void* renderInfoStates);

using TaskBandGetIconId_t = HRESULT(__fastcall*)(
    void* taskBandInterfaceThis,
    void* taskGroup,
    void* taskItem,
    int flags,
    int* iconId);

using TaskBandGetImageList_t = HRESULT(__fastcall*)(
    void* taskBandInterfaceThis,
    UINT dpi,
    HIMAGELIST* imageList);

using TaskBtnGroupGetGroup_t = void*(__fastcall*)(
    void* iTaskBtnGroupThis);

using TaskGroupSetIconId_t = HRESULT(__fastcall*)(
    void* taskGroup,
    void* taskItem,
    int iconId);

using TaskListTaskDestroyed_t = HRESULT(__fastcall*)(
    void* pThis,
    void* taskGroup,
    void* taskItem,
    int flags);

using GetColorFromPreferenceImpl_t = DWORD(__fastcall*)(
    const void* preference,
    int colorType,
    bool unknownBool,
    int highContrastCacheMode);

using ImageListGetIcon_t = HICON(WINAPI*)(
    HIMAGELIST imageList,
    int index,
    UINT flags);

GetStatesFromRenderInfo_t
    g_GetStatesFromRenderInfo_Original = nullptr;
DrawBar_t
    g_DrawBar_Original = nullptr;
TaskGroupSetIconId_t
    g_TaskGroupSetIconId_Original = nullptr;
TaskListTaskDestroyed_t
    g_TaskListTaskDestroyed_Original = nullptr;
GetColorFromPreferenceImpl_t
    g_ColorSource_Original = nullptr;

TaskBandGetIconId_t
    g_TaskBandGetIconId = nullptr;
TaskBandGetImageList_t
    g_TaskBandGetImageList = nullptr;
TaskBtnGroupGetGroup_t
    g_TaskBtnGroupGetGroup = nullptr;

ImageListGetIcon_t
    g_ImageListGetIcon = nullptr;

struct Symbols {
    void* getStatesFromRenderInfo = nullptr;
    void* drawBar = nullptr;
    void* taskBandGetIconId = nullptr;
    void* taskBandGetImageList = nullptr;
    void* taskBtnGroupGetGroup = nullptr;
    void* taskBtnGroupITaskBtnGroupVtable = nullptr;
    void* taskListGetIconId = nullptr;
    void* taskGroupSetIconId = nullptr;
    void* taskListTaskDestroyed = nullptr;
    void* taskListGetHWND = nullptr;
    void* getColorFromPreferenceImpl = nullptr;
} g_symbols;

void* g_exactColorCallReturn = nullptr;

int g_taskListMemberOffset = -1;

// CTaskListWnd is a multiple-inheritance object. The pointer stored by
// CTaskBtnGroup is the complete/native object view, while public virtual
// implementations such as GetIconId/GetHWND are reached through embedded
// interface subobjects. The offsets below are discovered from the live object
// using exact vtable/member invariants before any call is made.
std::atomic<long> g_taskListIconSubobjectOffset{-1};
std::atomic<long> g_taskListHwndSubobjectOffset{-1};

// -1 = not discovered yet, -2 = discovery failed.
std::atomic<long>
    g_iTaskBtnGroupSubobjectOffset{-1};

std::atomic<bool> g_loggedFirstResolve{false};
std::atomic<bool> g_loggedFirstThemeFallback{false};
std::atomic<bool> g_loggedFirstReset{false};

// ---------------------------------------------------------------------------
// Basic safety.
// ---------------------------------------------------------------------------

bool Contains(
    PCWSTR text,
    PCWSTR needle) {

    return text &&
           needle &&
           wcsstr(text, needle);
}

bool IsReadableMemory(
    const void* address,
    SIZE_T bytes) {

    if (!address || !bytes) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};

    if (!VirtualQuery(
            address,
            &mbi,
            sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD) ||
        (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }

    uintptr_t begin =
        reinterpret_cast<uintptr_t>(
            address);
    uintptr_t end =
        begin + bytes;

    if (end < begin) {
        return false;
    }

    uintptr_t regionBegin =
        reinterpret_cast<uintptr_t>(
            mbi.BaseAddress);
    uintptr_t regionEnd =
        regionBegin +
        mbi.RegionSize;

    return begin >= regionBegin &&
           end <= regionEnd;
}

bool IsExecutableAddress(
    const void* address) {

    if (!address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};

    if (!VirtualQuery(
            address,
            &mbi,
            sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD) ||
        (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }

    DWORD p = mbi.Protect & 0xFF;

    return p == PAGE_EXECUTE ||
           p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE ||
           p == PAGE_EXECUTE_WRITECOPY;
}

bool IsApplicableBuild() {
    using RtlGetVersion_t =
        LONG(WINAPI*)(OSVERSIONINFOW*);

    HMODULE ntdll =
        GetModuleHandleW(L"ntdll.dll");

    if (!ntdll) {
        return false;
    }

    auto rtlGetVersion =
        reinterpret_cast<
            RtlGetVersion_t>(
                GetProcAddress(
                    ntdll,
                    "RtlGetVersion"));

    if (!rtlGetVersion) {
        return false;
    }

    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize =
        sizeof(version);

    if (rtlGetVersion(&version) != 0) {
        return false;
    }

    g_nativeBuild =
        version.dwBuildNumber;
    g_buildFamily =
        BuildFamily::None;

    if (version.dwMajorVersion == 10) {
        if (version.dwBuildNumber == 17763) {
            g_buildFamily =
                BuildFamily::Win10_17763;
        } else if (version.dwBuildNumber >= 19041 &&
                   version.dwBuildNumber <= 19045) {
            g_buildFamily =
                BuildFamily::Win10_19041;
        } else if (version.dwBuildNumber == 20348) {
            g_buildFamily =
                BuildFamily::Server_20348;
        }
    }

    const wchar_t* familyName = L"unsupported";

    switch (g_buildFamily) {
        case BuildFamily::Win10_17763:
            familyName = L"17763";
            break;
        case BuildFamily::Win10_19041:
            familyName = L"19041-19045";
            break;
        case BuildFamily::Server_20348:
            familyName = L"20348";
            break;
        default:
            break;
    }

    Wh_Log(
        L"Windows version: %u.%u build %u; native family=%s",
        version.dwMajorVersion,
        version.dwMinorVersion,
        version.dwBuildNumber,
        familyName);

    return
        g_buildFamily !=
            BuildFamily::None;
}

// ---------------------------------------------------------------------------
// Symbols and ABI validation.
// ---------------------------------------------------------------------------

bool FindExplorerSymbols(
    HMODULE explorer) {

    WH_FIND_SYMBOL_OPTIONS options = {};
    options.optionsSize = sizeof(options);
    options.symbolServer = nullptr;
    options.noUndecoratedSymbols = FALSE;

    WH_FIND_SYMBOL data = {};

    HANDLE search =
        Wh_FindFirstSymbol(
            explorer,
            &options,
            &data);

    if (!search) {
        Wh_Log(
            L"Wh_FindFirstSymbol failed.");
        return false;
    }

    bool ambiguousStates = false;
    bool ambiguousDrawBar = false;
    bool ambiguousBandGetIconId = false;
    bool ambiguousBandGetImageList = false;
    bool ambiguousGetGroup = false;
    bool ambiguousBtnGroupVtable = false;
    bool ambiguousListGetIconId = false;
    bool ambiguousGroupSetIconId = false;
    bool ambiguousTaskDestroyed = false;
    bool ambiguousGetHWND = false;
    bool ambiguousColor = false;

    auto assignUnique = [](
        void*& target,
        void* candidate,
        bool& ambiguous) {

        if (!target) {
            target = candidate;
        } else if (target != candidate) {
            ambiguous = true;
        }
    };

    do {
        PCWSTR symbol = data.symbol;

        if (!symbol || !*symbol) {
            symbol =
                data.symbolDecorated;
        }

        if (!symbol || !*symbol ||
            Contains(symbol, L"[thunk]")) {
            continue;
        }

        if (Contains(
                symbol,
                L"CTaskBtnGroup::_GetStatesFromRenderInfo(")) {
            assignUnique(
                g_symbols.getStatesFromRenderInfo,
                data.address,
                ambiguousStates);
        } else if (Contains(
                       symbol,
                       L"CTaskBtnGroup::_DrawBar(")) {
            assignUnique(
                g_symbols.drawBar,
                data.address,
                ambiguousDrawBar);
        } else if (Contains(
                       symbol,
                       L"CTaskBand::GetIconId(")) {
            assignUnique(
                g_symbols.taskBandGetIconId,
                data.address,
                ambiguousBandGetIconId);
        } else if (Contains(
                       symbol,
                       L"CTaskBand::GetImageList(")) {
            assignUnique(
                g_symbols.taskBandGetImageList,
                data.address,
                ambiguousBandGetImageList);
        } else if (Contains(
                       symbol,
                       L"CTaskBtnGroup::GetGroup(void)")) {
            assignUnique(
                g_symbols.taskBtnGroupGetGroup,
                data.address,
                ambiguousGetGroup);
        } else if (Contains(
                       symbol,
                       L"CTaskBtnGroup::") &&
                   Contains(
                       symbol,
                       L"vftable") &&
                   Contains(
                       symbol,
                       L"ITaskBtnGroup")) {
            assignUnique(
                g_symbols.taskBtnGroupITaskBtnGroupVtable,
                data.address,
                ambiguousBtnGroupVtable);
        } else if (Contains(
                       symbol,
                       L"CTaskListWnd::GetIconId(")) {
            assignUnique(
                g_symbols.taskListGetIconId,
                data.address,
                ambiguousListGetIconId);
        } else if (Contains(
                       symbol,
                       L"CTaskGroup::SetIconId(")) {
            assignUnique(
                g_symbols.taskGroupSetIconId,
                data.address,
                ambiguousGroupSetIconId);
        } else if (Contains(
                       symbol,
                       L"CTaskListWnd::TaskDestroyed(")) {
            assignUnique(
                g_symbols.taskListTaskDestroyed,
                data.address,
                ambiguousTaskDestroyed);
        } else if (Contains(
                       symbol,
                       L"CTaskListWnd::GetHWND(void)")) {
            assignUnique(
                g_symbols.taskListGetHWND,
                data.address,
                ambiguousGetHWND);
        } else if (Contains(
                       symbol,
                       L"CImmersiveColorImpl::GetColorFromPreferenceImpl(")) {
            assignUnique(
                g_symbols.getColorFromPreferenceImpl,
                data.address,
                ambiguousColor);
        }
    } while (
        Wh_FindNextSymbol(
            search,
            &data));

    Wh_FindCloseSymbol(search);

    if (ambiguousStates ||
        ambiguousDrawBar ||
        ambiguousBandGetIconId ||
        ambiguousBandGetImageList ||
        ambiguousGetGroup ||
        ambiguousBtnGroupVtable ||
        ambiguousListGetIconId ||
        ambiguousGroupSetIconId ||
        ambiguousTaskDestroyed ||
        ambiguousGetHWND ||
        ambiguousColor) {

        Wh_Log(
            L"One or more required Explorer symbols "
            L"were ambiguous.");
        return false;
    }

    Wh_Log(
        L"Resolved parity symbols: states=%p drawBar=%p "
        L"bandGetIconId=%p bandGetImageList=%p "
        L"getGroup=%p btnGroupVtable=%p "
        L"listGetIconId=%p groupSetIconId=%p "
        L"taskDestroyed=%p getHWND=%p immersiveColor=%p",
        g_symbols.getStatesFromRenderInfo,
        g_symbols.drawBar,
        g_symbols.taskBandGetIconId,
        g_symbols.taskBandGetImageList,
        g_symbols.taskBtnGroupGetGroup,
        g_symbols.taskBtnGroupITaskBtnGroupVtable,
        g_symbols.taskListGetIconId,
        g_symbols.taskGroupSetIconId,
        g_symbols.taskListTaskDestroyed,
        g_symbols.taskListGetHWND,
        g_symbols.getColorFromPreferenceImpl);

    return
        g_symbols.getStatesFromRenderInfo &&
        g_symbols.drawBar &&
        g_symbols.taskBandGetIconId &&
        g_symbols.taskBandGetImageList &&
        g_symbols.taskBtnGroupGetGroup &&
        g_symbols.taskBtnGroupITaskBtnGroupVtable &&
        g_symbols.taskListGetIconId &&
        g_symbols.taskGroupSetIconId &&
        g_symbols.taskListTaskDestroyed &&
        g_symbols.taskListGetHWND;
}

bool ValidateGetGroupAbi() {
    auto* p =
        static_cast<const BYTE*>(
            g_symbols.taskBtnGroupGetGroup);

    if (!IsReadableMemory(p, 5)) {
        return false;
    }

    // 48 8B 41 xx    mov rax,[rcx+xx]
    // C3             ret
    return p[0] == 0x48 &&
           p[1] == 0x8B &&
           p[2] == 0x41 &&
           p[4] == 0xC3;
}

bool ValidateTaskBandGetImageListAbi() {
    auto* p =
        static_cast<const BYTE*>(
            g_symbols.taskBandGetImageList);

    if (!IsReadableMemory(p, 14)) {
        return false;
    }

    // 19041/20348:
    //   49 83 20 00          and qword ptr [r8],0
    //   48 8B 89 xx...       mov rcx,[rcx+...]
    const bool modern =
        p[0] == 0x49 &&
        p[1] == 0x83 &&
        p[2] == 0x20 &&
        p[3] == 0x00 &&
        p[4] == 0x48 &&
        p[5] == 0x8B &&
        p[6] == 0x89;

    // 17763:
    //   49 C7 00 00 00 00 00 mov qword ptr [r8],0
    //   48 8B 89 xx...        mov rcx,[rcx+...]
    const bool legacy17763 =
        p[0] == 0x49 &&
        p[1] == 0xC7 &&
        p[2] == 0x00 &&
        p[3] == 0x00 &&
        p[4] == 0x00 &&
        p[5] == 0x00 &&
        p[6] == 0x00 &&
        p[7] == 0x48 &&
        p[8] == 0x8B &&
        p[9] == 0x89;

    return modern || legacy17763;
}

bool DiscoverTaskListMemberOffset() {
    auto* start =
        static_cast<const BYTE*>(
            g_symbols.drawBar);

    if (!IsReadableMemory(
            start,
            0xB0)) {
        return false;
    }

    int foundOffset = -1;
    unsigned count = 0;

    // 17763 / 19041-family:
    //   mov rcx,[rcx+disp8]
    //   mov edx,[rcx+1C4h]
    for (SIZE_T i = 0;
         i + 16 <= 0xB0;
         ++i) {

        if (start[i + 0] == 0x48 &&
            start[i + 1] == 0x8B &&
            start[i + 2] == 0x49) {

            for (SIZE_T j = i + 4;
                 j + 6 <= i + 18 &&
                 j + 6 <= 0xB0;
                 ++j) {

                if (start[j + 0] == 0x8B &&
                    start[j + 1] == 0x91 &&
                    start[j + 2] == 0xC4 &&
                    start[j + 3] == 0x01 &&
                    start[j + 4] == 0x00 &&
                    start[j + 5] == 0x00) {

                    foundOffset =
                        static_cast<int>(
                            start[i + 3]);
                    ++count;
                    break;
                }
            }
        }

        // 20348:
        //   mov r10,[rcx+disp8]
        //   mov edx,[r10+1C4h]
        if (start[i + 0] == 0x4C &&
            start[i + 1] == 0x8B &&
            start[i + 2] == 0x51) {

            for (SIZE_T j = i + 4;
                 j + 7 <= i + 18 &&
                 j + 7 <= 0xB0;
                 ++j) {

                if (start[j + 0] == 0x41 &&
                    start[j + 1] == 0x8B &&
                    start[j + 2] == 0x92 &&
                    start[j + 3] == 0xC4 &&
                    start[j + 4] == 0x01 &&
                    start[j + 5] == 0x00 &&
                    start[j + 6] == 0x00) {

                    foundOffset =
                        static_cast<int>(
                            start[i + 3]);
                    ++count;
                    break;
                }
            }
        }
    }

    if (count != 1 ||
        foundOffset < 0) {
        Wh_Log(
            L"Couldn't uniquely discover native CTaskListWnd member "
            L"offset (matches=%u).",
            count);
        return false;
    }

    g_taskListMemberOffset =
        foundOffset;

    Wh_Log(
        L"Discovered native CTaskListWnd member offset: 0x%X",
        g_taskListMemberOffset);

    return true;
}

bool VtableContainsExact(
    void* vtable,
    void* target,
    SIZE_T maxBytes = 0x300) {

    if (!vtable ||
        !target ||
        !IsReadableMemory(
            vtable,
            sizeof(void*))) {
        return false;
    }

    auto* bytes =
        static_cast<BYTE*>(
            vtable);

    for (SIZE_T offset = 0;
         offset + sizeof(void*) <=
             maxBytes;
         offset += sizeof(void*)) {

        auto* slot =
            bytes + offset;

        if (!IsReadableMemory(
                slot,
                sizeof(void*))) {
            break;
        }

        if (*reinterpret_cast<void**>(
                slot) == target) {
            return true;
        }
    }

    return false;
}

bool ValidateTaskBandInterface(
    void* taskBand) {

    if (!taskBand ||
        !IsReadableMemory(
            taskBand,
            sizeof(void*))) {
        return false;
    }

    void* vtable =
        *reinterpret_cast<void**>(
            taskBand);

    if (!vtable ||
        !IsReadableMemory(
            static_cast<BYTE*>(
                vtable) + 0x28,
            sizeof(void*))) {
        return false;
    }

    // Native CTaskListWnd::GetIconId dispatches slot +0x20 and
    // CTaskBand::GetImageList is the next slot (+0x28) on the validated
    // 19041-family object. Requiring both exact public-symbol targets makes
    // the discovery fail closed instead of guessing from layout alone.
    void* getIconId =
        *reinterpret_cast<void**>(
            static_cast<BYTE*>(
                vtable) + 0x20);

    void* getImageList =
        *reinterpret_cast<void**>(
            static_cast<BYTE*>(
                vtable) + 0x28);

    return
        getIconId ==
            g_symbols.taskBandGetIconId &&
        getImageList ==
            g_symbols.taskBandGetImageList;
}

bool DiscoverTaskListSubobjects(
    void* taskList) {

    if (!taskList) {
        return false;
    }

    long iconKnown =
        g_taskListIconSubobjectOffset.load(
            std::memory_order_acquire);

    long hwndKnown =
        g_taskListHwndSubobjectOffset.load(
            std::memory_order_acquire);

    if (iconKnown >= 0 &&
        hwndKnown >= 0) {
        return true;
    }

    int iconCandidate = -1;
    unsigned iconMatches = 0;

    int hwndCandidate = -1;
    unsigned hwndMatches = 0;

    // The 19045 dump shows the relevant CTaskListWnd interface subobjects in
    // the first 0x60 bytes. Scan a little farther but require exact vtable
    // membership and member invariants.
    for (int offset = 0;
         offset <= 0x80;
         offset +=
             static_cast<int>(
                 sizeof(void*))) {

        auto* subobject =
            static_cast<BYTE*>(
                taskList) +
            offset;

        if (!IsReadableMemory(
                subobject,
                sizeof(void*))) {
            continue;
        }

        void* vtable =
            *reinterpret_cast<void**>(
                subobject);

        if (!vtable) {
            continue;
        }

        // GetIconId implementation starts with:
        //   mov rcx,[rcx+30h]
        // For the correct interface-this, that member must be the actual
        // CTaskBand interface whose slots +20/+28 are the exact symbols.
        if (VtableContainsExact(
                vtable,
                g_symbols.taskListGetIconId)) {

            auto* bandAddress =
                subobject + 0x30;

            if (IsReadableMemory(
                    bandAddress,
                    sizeof(void*))) {

                void* taskBand =
                    *reinterpret_cast<void**>(
                        bandAddress);

                if (ValidateTaskBandInterface(
                        taskBand)) {
                    iconCandidate =
                        offset;
                    ++iconMatches;
                }
            }
        }

        // GetHWND implementation is `mov rax,[rcx-20h]; ret`. Don't call it.
        // Validate the correct interface subobject by vtable membership plus
        // the field it would return being a real HWND.
        if (offset >= 0x20 &&
            VtableContainsExact(
                vtable,
                g_symbols.taskListGetHWND)) {

            auto* hwndAddress =
                subobject - 0x20;

            if (IsReadableMemory(
                    hwndAddress,
                    sizeof(HWND))) {

                HWND hwnd =
                    *reinterpret_cast<HWND*>(
                        hwndAddress);

                if (hwnd &&
                    IsWindow(hwnd)) {
                    hwndCandidate =
                        offset;
                    ++hwndMatches;
                }
            }
        }
    }

    if (iconMatches != 1 ||
        hwndMatches != 1) {

        Wh_Log(
            L"Couldn't uniquely discover CTaskListWnd subobjects "
            L"(iconMatches=%u hwndMatches=%u).",
            iconMatches,
            hwndMatches);

        return false;
    }

    long expected = -1;

    g_taskListIconSubobjectOffset
        .compare_exchange_strong(
            expected,
            iconCandidate,
            std::memory_order_acq_rel);

    expected = -1;

    g_taskListHwndSubobjectOffset
        .compare_exchange_strong(
            expected,
            hwndCandidate,
            std::memory_order_acq_rel);

    Wh_Log(
        L"Discovered crash-safe CTaskListWnd subobjects: "
        L"GetIconId-this=+0x%X GetHWND-this=+0x%X",
        iconCandidate,
        hwndCandidate);

    return true;
}

void* GetValidatedTaskBand(
    void* taskList) {

    if (!taskList ||
        !DiscoverTaskListSubobjects(
            taskList)) {
        return nullptr;
    }

    long offset =
        g_taskListIconSubobjectOffset.load(
            std::memory_order_acquire);

    if (offset < 0) {
        return nullptr;
    }

    auto* subobject =
        static_cast<BYTE*>(
            taskList) +
        offset;

    auto* bandAddress =
        subobject + 0x30;

    if (!IsReadableMemory(
            bandAddress,
            sizeof(void*))) {
        return nullptr;
    }

    void* taskBand =
        *reinterpret_cast<void**>(
            bandAddress);

    return
        ValidateTaskBandInterface(
            taskBand)
            ? taskBand
            : nullptr;
}

HWND GetTaskListHwnd(
    void* taskList) {

    if (!taskList ||
        !DiscoverTaskListSubobjects(
            taskList)) {
        return nullptr;
    }

    long offset =
        g_taskListHwndSubobjectOffset.load(
            std::memory_order_acquire);

    if (offset < 0x20) {
        return nullptr;
    }

    auto* hwndAddress =
        static_cast<BYTE*>(
            taskList) +
        offset -
        0x20;

    if (!IsReadableMemory(
            hwndAddress,
            sizeof(HWND))) {
        return nullptr;
    }

    HWND hwnd =
        *reinterpret_cast<HWND*>(
            hwndAddress);

    return
        hwnd &&
        IsWindow(hwnd)
            ? hwnd
            : nullptr;
}

bool HasOrdinaryColorCallContext(
    const BYTE* start,
    SIZE_T callOffset) {

    bool sawR9One = false;
    bool sawR8Zero = false;

    const SIZE_T windowStart =
        callOffset > 0x20
            ? callOffset - 0x20
            : 0;

    for (SIZE_T j = windowStart;
         j < callOffset;
         ++j) {

        if (j + 6 <= callOffset &&
            start[j + 0] == 0x41 &&
            start[j + 1] == 0xB9 &&
            start[j + 2] == 0x01 &&
            start[j + 3] == 0x00 &&
            start[j + 4] == 0x00 &&
            start[j + 5] == 0x00) {
            sawR9One = true;
        }

        if (j + 3 <= callOffset &&
            start[j + 0] == 0x45 &&
            start[j + 1] == 0x33 &&
            start[j + 2] == 0xC0) {
            sawR8Zero = true;
        }
    }

    return sawR9One && sawR8Zero;
}

bool Has17763OrdinaryColorCallContext(
    const BYTE* start,
    SIZE_T callOffset) {

    // The supplied 17763.1911 build uses:
    //   mov r9d,r13d   ; r13d is the function's constant 1
    //   xor r8d,r8d
    //   mov edx,ebx
    //   lea rcx,[rsp+78h]
    //   call qword ptr [UxTheme ordinal 121 IAT]
    //
    // Require the distinctive register setup near the exact ordinal target.
    bool sawR9FromR13 = false;
    bool sawR8Zero = false;
    bool sawEdxFromEbx = false;

    const SIZE_T windowStart =
        callOffset > 0x18
            ? callOffset - 0x18
            : 0;

    for (SIZE_T j = windowStart;
         j < callOffset;
         ++j) {

        if (j + 3 <= callOffset &&
            start[j + 0] == 0x45 &&
            start[j + 1] == 0x8B &&
            start[j + 2] == 0xCD) {
            sawR9FromR13 = true;
        }

        if (j + 3 <= callOffset &&
            start[j + 0] == 0x45 &&
            start[j + 1] == 0x33 &&
            start[j + 2] == 0xC0) {
            sawR8Zero = true;
        }

        if (j + 2 <= callOffset &&
            start[j + 0] == 0x8B &&
            start[j + 1] == 0xD3) {
            sawEdxFromEbx = true;
        }
    }

    return
        sawR9FromR13 &&
        sawR8Zero &&
        sawEdxFromEbx;
}

bool DiscoverImmersiveImplColorCallsite() {
    auto* start =
        static_cast<BYTE*>(
            g_symbols.drawBar);

    auto* target =
        static_cast<BYTE*>(
            g_symbols.getColorFromPreferenceImpl);

    if (!start ||
        !target ||
        !IsExecutableAddress(start) ||
        !IsExecutableAddress(target) ||
        !IsReadableMemory(
            start,
            0x240)) {
        return false;
    }

    BYTE* match = nullptr;
    unsigned count = 0;

    for (SIZE_T i = 0;
         i + 5 <= 0x240;
         ++i) {

        if (start[i] != 0xE8) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(
            &displacement,
            start + i + 1,
            sizeof(displacement));

        BYTE* destination =
            start + i + 5 +
            displacement;

        if (destination != target ||
            !HasOrdinaryColorCallContext(
                start,
                i)) {
            continue;
        }

        match = start + i;
        ++count;
    }

    if (count != 1 ||
        !match) {
        Wh_Log(
            L"Expected exactly one ordinary direct _DrawBar color call; "
            L"found %u.",
            count);
        return false;
    }

    g_exactColorCallReturn =
        match + 5;
    g_colorSourceTarget =
        target;
    g_colorSourceHookKind =
        ColorSourceHookKind::ImmersiveImpl;

    Wh_Log(
        L"Validated ordinary _DrawBar direct color call=%p "
        L"exact return=%p target=%p.",
        match,
        g_exactColorCallReturn,
        target);

    return true;
}

bool Discover17763UxThemeColorCallsite() {
    HMODULE uxTheme =
        GetModuleHandleW(
            L"uxtheme.dll");

    if (!uxTheme) {
        uxTheme =
            LoadLibraryW(
                L"uxtheme.dll");
    }

    if (!uxTheme) {
        return false;
    }

    void* target =
        reinterpret_cast<void*>(
            GetProcAddress(
                uxTheme,
                MAKEINTRESOURCEA(121)));

    auto* start =
        static_cast<BYTE*>(
            g_symbols.drawBar);

    if (!target ||
        !start ||
        !IsExecutableAddress(target) ||
        !IsReadableMemory(
            start,
            0x240)) {
        return false;
    }

    BYTE* match = nullptr;
    unsigned count = 0;

    for (SIZE_T i = 0;
         i + 7 <= 0x240;
         ++i) {

        // call qword ptr [rip+disp32]
        if (start[i + 0] != 0x48 ||
            start[i + 1] != 0xFF ||
            start[i + 2] != 0x15) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(
            &displacement,
            start + i + 3,
            sizeof(displacement));

        BYTE* iatSlot =
            start + i + 7 +
            displacement;

        if (!IsReadableMemory(
                iatSlot,
                sizeof(void*))) {
            continue;
        }

        void* destination =
            *reinterpret_cast<void**>(
                iatSlot);

        if (destination != target ||
            !Has17763OrdinaryColorCallContext(
                start,
                i)) {
            continue;
        }

        match = start + i;
        ++count;
    }

    if (count != 1 ||
        !match) {
        Wh_Log(
            L"Expected exactly one ordinary 17763 UxTheme ordinal-121 "
            L"call; found %u.",
            count);
        return false;
    }

    g_exactColorCallReturn =
        match + 7;
    g_colorSourceTarget =
        target;
    g_colorSourceHookKind =
        ColorSourceHookKind::UxThemeOrdinal121;

    Wh_Log(
        L"Validated 17763 ordinary UxTheme ordinal-121 color call=%p "
        L"exact return=%p target=%p.",
        match,
        g_exactColorCallReturn,
        target);

    return true;
}

bool DiscoverExactColorCallsite() {
    if (g_buildFamily ==
        BuildFamily::Win10_17763) {
        return
            Discover17763UxThemeColorCallsite();
    }

    if (!g_symbols.getColorFromPreferenceImpl) {
        Wh_Log(
            L"CImmersiveColorImpl::GetColorFromPreferenceImpl symbol "
            L"is required for this native build family.");
        return false;
    }

    return
        DiscoverImmersiveImplColorCallsite();
}

bool ResolveImageListExport() {
    HMODULE comctl32 =
        GetModuleHandleW(
            L"comctl32.dll");

    if (!comctl32) {
        comctl32 =
            LoadLibraryW(
                L"comctl32.dll");
    }

    if (!comctl32) {
        return false;
    }

    g_ImageListGetIcon =
        reinterpret_cast<
            ImageListGetIcon_t>(
                GetProcAddress(
                    comctl32,
                    "ImageList_GetIcon"));

    return
        g_ImageListGetIcon != nullptr;
}

// ---------------------------------------------------------------------------
// ITaskBtnGroup subobject discovery.
// ---------------------------------------------------------------------------

bool GetTaskGroupAndTaskList(
    void* rawTaskBtnGroup,
    void** taskGroupOut,
    void** taskListOut) {

    if (!rawTaskBtnGroup ||
        !taskGroupOut ||
        !taskListOut ||
        g_taskListMemberOffset < 0) {
        return false;
    }

    long offset =
        g_iTaskBtnGroupSubobjectOffset.load(
            std::memory_order_acquire);

    if (offset == -1) {
        int candidate = -1;
        unsigned matches = 0;

        for (int current = 0;
             current <= 0x50;
             current +=
                 static_cast<int>(
                     sizeof(void*))) {

            auto* address =
                static_cast<BYTE*>(
                    rawTaskBtnGroup) +
                current;

            if (!IsReadableMemory(
                    address,
                    sizeof(void*))) {
                continue;
            }

            void* vtable =
                *reinterpret_cast<void**>(
                    address);

            if (vtable ==
                g_symbols.taskBtnGroupITaskBtnGroupVtable) {
                candidate = current;
                ++matches;
            }
        }

        long resolved =
            matches == 1
                ? candidate
                : -2;

        long expected = -1;

        g_iTaskBtnGroupSubobjectOffset
            .compare_exchange_strong(
                expected,
                resolved,
                std::memory_order_acq_rel);

        offset =
            g_iTaskBtnGroupSubobjectOffset.load(
                std::memory_order_acquire);

        if (offset >= 0) {
            Wh_Log(
                L"Discovered ITaskBtnGroup subobject "
                L"offset: 0x%lX",
                offset);
        } else {
            Wh_Log(
                L"Couldn't uniquely discover "
                L"ITaskBtnGroup subobject offset.");
        }
    }

    if (offset < 0) {
        return false;
    }

    auto* iTaskBtnGroupThis =
        static_cast<BYTE*>(
            rawTaskBtnGroup) +
        offset;

    if (!IsReadableMemory(
            iTaskBtnGroupThis,
            sizeof(void*)) ||
        *reinterpret_cast<void**>(
            iTaskBtnGroupThis) !=
            g_symbols.taskBtnGroupITaskBtnGroupVtable) {
        return false;
    }

    void* taskGroup =
        g_TaskBtnGroupGetGroup(
            iTaskBtnGroupThis);

    auto* taskListAddress =
        static_cast<BYTE*>(
            rawTaskBtnGroup) +
        g_taskListMemberOffset;

    if (!IsReadableMemory(
            taskListAddress,
            sizeof(void*))) {
        return false;
    }

    void* taskList =
        *reinterpret_cast<void**>(
            taskListAddress);

    if (!taskGroup ||
        !taskList) {
        return false;
    }

    *taskGroupOut =
        taskGroup;
    *taskListOut =
        taskList;

    return true;
}

// ---------------------------------------------------------------------------
// renderInfo -> ITaskItem association.
//
// Consume-on-read is deliberate: if _DrawBar is ever reached without a fresh
// _GetStatesFromRenderInfo call, an old mapping cannot be mistaken for the
// current draw. A consumed null item is distinct from "not found"; null is the
// valid grouped/glommed case.
// ---------------------------------------------------------------------------

struct RenderItemEntry {
    const void* renderInfo = nullptr;
    void* taskItem = nullptr;
};

constexpr unsigned
    kRenderItemEntries = 8;

thread_local RenderItemEntry
    g_renderItems[kRenderItemEntries] = {};

thread_local unsigned
    g_renderItemWrite = 0;

void RememberRenderItem(
    const void* renderInfo,
    void* taskItem) {

    if (!renderInfo) {
        return;
    }

    RenderItemEntry& entry =
        g_renderItems[
            g_renderItemWrite %
            kRenderItemEntries];

    entry.renderInfo =
        renderInfo;
    entry.taskItem =
        taskItem;

    ++g_renderItemWrite;
}

bool ConsumeRenderItem(
    const void* renderInfo,
    void** taskItemOut) {

    if (!renderInfo ||
        !taskItemOut) {
        return false;
    }

    for (unsigned distance = 0;
         distance <
             kRenderItemEntries;
         ++distance) {

        unsigned index =
            (g_renderItemWrite +
             kRenderItemEntries -
             1 -
             distance) %
            kRenderItemEntries;

        RenderItemEntry& entry =
            g_renderItems[index];

        if (entry.renderInfo ==
            renderInfo) {

            *taskItemOut =
                entry.taskItem;

            entry.renderInfo =
                nullptr;
            entry.taskItem =
                nullptr;

            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// ExplorerPatcher-compatible hot-track cache state.
//
// EP stores this state inside CTaskGroup / ITaskItem. We keep an external cache
// keyed by the same logical identity: (ITaskGroup, ITaskItem). item==nullptr
// is the group-level cache.
// ---------------------------------------------------------------------------

enum class HotColorState : BYTE {
    Uninitialized = 0,
    Value = 1,
    NoDominantColor = 2,
};

struct HotColorEntry {
    HotColorEntry* next = nullptr;
    void* taskGroup = nullptr;
    void* taskItem = nullptr;
    DWORD colorRef = 0;
    HotColorState state =
        HotColorState::Uninitialized;
};

// ExplorerPatcher stores the hot color directly in CTaskGroup / ITaskItem, so
// it has no arbitrary entry-count limit. The Windhawk backend can't extend
// those native objects safely, therefore it mirrors the same logical state in
// a small process-heap linked list. Allocation failure simply leaves the color
// uncached; the current draw still succeeds and a later draw can retry.
SRWLOCK
    g_hotColorLock = SRWLOCK_INIT;

HotColorEntry*
    g_hotColorHead = nullptr;

HotColorEntry* FindHotColorEntryLocked(
    void* taskGroup,
    void* taskItem) {

    if (!taskGroup) {
        return nullptr;
    }

    for (HotColorEntry* entry =
             g_hotColorHead;
         entry;
         entry = entry->next) {

        if (entry->taskGroup ==
                taskGroup &&
            entry->taskItem ==
                taskItem) {
            return entry;
        }
    }

    return nullptr;
}

bool ReadHotColorState(
    void* taskGroup,
    void* taskItem,
    HotColorState* stateOut,
    DWORD* colorRefOut) {

    if (!taskGroup ||
        !stateOut ||
        !colorRefOut) {
        return false;
    }

    bool found = false;

    AcquireSRWLockShared(
        &g_hotColorLock);

    HotColorEntry* entry =
        FindHotColorEntryLocked(
            taskGroup,
            taskItem);

    if (entry) {
        *stateOut =
            entry->state;
        *colorRefOut =
            entry->colorRef;
        found = true;
    }

    ReleaseSRWLockShared(
        &g_hotColorLock);

    return found;
}

// Returns false only when a new entry was needed and allocation failed.
// Updating an existing entry never allocates.
bool StoreHotColorState(
    void* taskGroup,
    void* taskItem,
    HotColorState state,
    DWORD colorRef) {

    if (!taskGroup) {
        return false;
    }

    AcquireSRWLockExclusive(
        &g_hotColorLock);

    HotColorEntry* entry =
        FindHotColorEntryLocked(
            taskGroup,
            taskItem);

    if (!entry) {
        entry =
            static_cast<HotColorEntry*>(
                HeapAlloc(
                    GetProcessHeap(),
                    HEAP_ZERO_MEMORY,
                    sizeof(HotColorEntry)));

        if (!entry) {
            ReleaseSRWLockExclusive(
                &g_hotColorLock);
            return false;
        }

        entry->taskGroup =
            taskGroup;
        entry->taskItem =
            taskItem;
        entry->next =
            g_hotColorHead;
        g_hotColorHead =
            entry;
    }

    entry->state =
        state;
    entry->colorRef =
        colorRef;

    ReleaseSRWLockExclusive(
        &g_hotColorLock);

    return true;
}

void ResetHotColor(
    void* taskGroup,
    void* taskItem) {

    if (!taskGroup) {
        return;
    }

    AcquireSRWLockExclusive(
        &g_hotColorLock);

    HotColorEntry** link =
        &g_hotColorHead;

    while (*link) {
        HotColorEntry* entry =
            *link;

        if (entry->taskGroup ==
                taskGroup &&
            entry->taskItem ==
                taskItem) {

            *link =
                entry->next;

            HeapFree(
                GetProcessHeap(),
                0,
                entry);
            break;
        }

        link =
            &entry->next;
    }

    ReleaseSRWLockExclusive(
        &g_hotColorLock);
}

void RemoveDestroyedHotColors(
    void* taskGroup,
    void* taskItem) {

    if (!taskGroup &&
        !taskItem) {
        return;
    }

    AcquireSRWLockExclusive(
        &g_hotColorLock);

    HotColorEntry** link =
        &g_hotColorHead;

    while (*link) {
        HotColorEntry* entry =
            *link;

        const bool itemMatch =
            taskItem &&
            entry->taskItem ==
                taskItem;

        const bool groupLevelMatch =
            taskGroup &&
            entry->taskGroup ==
                taskGroup &&
            entry->taskItem ==
                nullptr;

        if (itemMatch ||
            groupLevelMatch) {

            *link =
                entry->next;

            HeapFree(
                GetProcessHeap(),
                0,
                entry);
            continue;
        }

        link =
            &entry->next;
    }

    ReleaseSRWLockExclusive(
        &g_hotColorLock);
}

void FreeAllHotColors() {
    AcquireSRWLockExclusive(
        &g_hotColorLock);

    HotColorEntry* entry =
        g_hotColorHead;
    g_hotColorHead =
        nullptr;

    while (entry) {
        HotColorEntry* next =
            entry->next;

        HeapFree(
            GetProcessHeap(),
            0,
            entry);

        entry = next;
    }

    ReleaseSRWLockExclusive(
        &g_hotColorLock);
}

// ---------------------------------------------------------------------------
// Exact ExplorerPatcher dominant-color extractor.
// ---------------------------------------------------------------------------

bool ExtractEpHotTrackColor(
    HICON icon,
    DWORD* colorRefOut) {

    if (!icon ||
        !colorRefOut) {
        return false;
    }

    int width =
        GetSystemMetrics(
            SM_CXICON);
    int height =
        GetSystemMetrics(
            SM_CYICON);

    if (width <= 0 ||
        height <= 0 ||
        width > 256 ||
        height > 256) {
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth =
        width;
    bmi.bmiHeader.biHeight =
        -height;
    bmi.bmiHeader.biPlanes =
        1;
    bmi.bmiHeader.biBitCount =
        32;
    bmi.bmiHeader.biCompression =
        BI_RGB;

    HDC screen =
        GetDC(nullptr);

    if (!screen) {
        return false;
    }

    HDC memory =
        CreateCompatibleDC(
            screen);

    void* pixels = nullptr;

    HBITMAP bitmap =
        CreateDIBSection(
            screen,
            &bmi,
            DIB_RGB_COLORS,
            &pixels,
            nullptr,
            0);

    ReleaseDC(
        nullptr,
        screen);

    if (!memory ||
        !bitmap ||
        !pixels) {

        if (bitmap) {
            DeleteObject(bitmap);
        }

        if (memory) {
            DeleteDC(memory);
        }

        return false;
    }

    HGDIOBJ oldBitmap =
        SelectObject(
            memory,
            bitmap);

    ZeroMemory(
        pixels,
        static_cast<SIZE_T>(
            width) *
        static_cast<SIZE_T>(
            height) *
        4);

    BOOL drawn =
        DrawIconEx(
            memory,
            0,
            0,
            icon,
            width,
            height,
            0,
            nullptr,
            DI_NORMAL);

    struct Bucket {
        uint32_t count = 0;
        uint64_t b = 0;
        uint64_t g = 0;
        uint64_t r = 0;
    };

    Bucket buckets[27] = {};
    uint32_t total = 0;

    if (drawn) {
        BYTE* bytes =
            static_cast<BYTE*>(
                pixels);

        SIZE_T pixelCount =
            static_cast<SIZE_T>(
                width) *
            static_cast<SIZE_T>(
                height);

        for (SIZE_T i = 0;
             i < pixelCount;
             ++i) {

            BYTE* pixel =
                bytes + i * 4;

            BYTE b = pixel[0];
            BYTE g = pixel[1];
            BYTE r = pixel[2];
            BYTE a = pixel[3];

            if (a == 0) {
                continue;
            }

            // EP un-premultiplies all nonzero-alpha pixels. Skipping the
            // division at alpha==255 is mathematically identical.
            if (a != 255) {
                b = static_cast<BYTE>(
                    (static_cast<unsigned>(
                         b) *
                     255u) /
                    a);

                g = static_cast<BYTE>(
                    (static_cast<unsigned>(
                         g) *
                     255u) /
                    a);

                r = static_cast<BYTE>(
                    (static_cast<unsigned>(
                         r) *
                     255u) /
                    a);
            }

            BYTE maxChannel = r;
            if (g > maxChannel) {
                maxChannel = g;
            }
            if (b > maxChannel) {
                maxChannel = b;
            }

            BYTE minChannel = r;
            if (g < minChannel) {
                minChannel = g;
            }
            if (b < minChannel) {
                minChannel = b;
            }

            // EP: (max - min) > 0x30.
            if (static_cast<unsigned>(
                    maxChannel) -
                    static_cast<unsigned>(
                        minChannel) <=
                0x30u) {
                continue;
            }

            // Exact 3-way bucket boundaries emitted by EP's multiply-high
            // sequence: 0..85, 86..171, 172..255.
            unsigned rBin =
                static_cast<unsigned>(
                    r) /
                86u;

            unsigned gBin =
                static_cast<unsigned>(
                    g) /
                86u;

            unsigned bBin =
                static_cast<unsigned>(
                    b) /
                86u;

            if (rBin > 2) rBin = 2;
            if (gBin > 2) gBin = 2;
            if (bBin > 2) bBin = 2;

            unsigned bucketIndex =
                9u * rBin +
                3u * gBin +
                bBin;

            Bucket& bucket =
                buckets[
                    bucketIndex];

            ++bucket.count;
            bucket.b += b;
            bucket.g += g;
            bucket.r += r;

            ++total;
        }
    }

    bool success = false;

    if (total) {
        const Bucket* best = nullptr;

        // First bucket wins ties, matching EP's strict-greater scan.
        for (const Bucket& bucket :
             buckets) {

            if (bucket.count &&
                (!best ||
                 bucket.count >
                     best->count)) {
                best = &bucket;
            }
        }

        if (best &&
            best->count) {

            int share =
                MulDiv(
                    static_cast<int>(
                        best->count),
                    100,
                    static_cast<int>(
                        total));

            if (share >= 7) {
                BYTE b =
                    static_cast<BYTE>(
                        best->b /
                        best->count);

                BYTE g =
                    static_cast<BYTE>(
                        best->g /
                        best->count);

                BYTE r =
                    static_cast<BYTE>(
                        best->r /
                        best->count);

                // EP returns COLORREF order: 0x00BBGGRR.
                *colorRefOut =
                    static_cast<DWORD>(r) |
                    (static_cast<DWORD>(g)
                     << 8) |
                    (static_cast<DWORD>(b)
                     << 16);

                success = true;
            }
        }
    }

    SelectObject(
        memory,
        oldBitmap);

    DeleteObject(bitmap);
    DeleteDC(memory);

    return success;
}

// ---------------------------------------------------------------------------
// EP's "no dominant icon color" fallback.
//
// Old EP does NOT invent a neutral color. It reads:
//   GetThemeColor(taskListTheme, part 5, state 0, TMT_HOTTRACKING=1627)
// and globally caches the result. If the task-list theme handle is null or the
// query fails, it uses COLORREF 0x00FFFF00.
//
// v0.7.2 uses the native CTaskListWnd's actual HWND and GetWindowTheme(), so
// vertical/small-icon/composited TaskBand2 variants use the theme handle which
// Windows has already associated with that specific task-list window instead
// of reopening a guessed base class.
// ---------------------------------------------------------------------------

INIT_ONCE
    g_fallbackInitOnce =
        INIT_ONCE_STATIC_INIT;

DWORD
    g_epFallbackColorRef =
        0x00FFFF00;

struct FallbackInitContext {
    HWND taskListHwnd = nullptr;
};

BOOL CALLBACK InitEpFallbackColor(
    PINIT_ONCE,
    PVOID parameter,
    PVOID*) {

    auto* context =
        static_cast<
            FallbackInitContext*>(
                parameter);

    if (!context ||
        !context->taskListHwnd) {
        return TRUE;
    }

    HMODULE uxTheme =
        GetModuleHandleW(
            L"uxtheme.dll");

    if (!uxTheme) {
        uxTheme =
            LoadLibraryW(
                L"uxtheme.dll");
    }

    if (!uxTheme) {
        return TRUE;
    }

    using GetWindowTheme_t =
        HANDLE(WINAPI*)(HWND);

    using GetThemeColor_t =
        HRESULT(WINAPI*)(
            HANDLE,
            int,
            int,
            int,
            COLORREF*);

    auto getWindowTheme =
        reinterpret_cast<
            GetWindowTheme_t>(
                GetProcAddress(
                    uxTheme,
                    "GetWindowTheme"));

    auto getThemeColor =
        reinterpret_cast<
            GetThemeColor_t>(
                GetProcAddress(
                    uxTheme,
                    "GetThemeColor"));

    if (getWindowTheme &&
        getThemeColor) {

        HANDLE theme =
            getWindowTheme(
                context->taskListHwnd);

        if (theme) {
            COLORREF color = 0;

            constexpr int
                kTmtHotTracking =
                    1627;

            if (SUCCEEDED(
                    getThemeColor(
                        theme,
                        5,
                        0,
                        kTmtHotTracking,
                        &color))) {

                g_epFallbackColorRef =
                    color;
            }
        }
    }

    return TRUE;
}

DWORD GetEpFallbackColorRef(
    HWND taskListHwnd) {

    FallbackInitContext context = {};
    context.taskListHwnd =
        taskListHwnd;

    InitOnceExecuteOnce(
        &g_fallbackInitOnce,
        InitEpFallbackColor,
        &context,
        nullptr);

    return g_epFallbackColorRef;
}

// ---------------------------------------------------------------------------
// Synchronous same-paint hot-track resolution.
// ---------------------------------------------------------------------------

enum class ResolveResult {
    InfrastructureFailure,
    Color,
    NoDominantColor,
};

UINT GetTaskListDpi(
    void* taskList,
    HDC hdc) {

    HWND hwnd =
        GetTaskListHwnd(
            taskList);

    if (hwnd) {
        UINT dpi =
            GetDpiForWindow(hwnd);

        if (dpi > 0 &&
            dpi <= 960) {
            return dpi;
        }
    }

    // Safe fallback used by the known-good v0.6 path.
    int dpi =
        hdc
            ? GetDeviceCaps(
                  hdc,
                  LOGPIXELSX)
            : 0;

    if (dpi <= 0 ||
        dpi > 960) {
        dpi = 96;
    }

    return static_cast<UINT>(
        dpi);
}

ResolveResult ResolveHotTrackColor(
    HDC hdc,
    void* taskGroup,
    void* taskList,
    void* taskItem,
    DWORD* colorRefOut) {

    if (!taskGroup ||
        !taskList ||
        !colorRefOut ||
        !g_TaskBandGetIconId ||
        !g_TaskBandGetImageList ||
        !g_ImageListGetIcon) {
        return
            ResolveResult::
                InfrastructureFailure;
    }

    void* taskBand =
        GetValidatedTaskBand(
            taskList);

    if (!taskBand) {
        return
            ResolveResult::
                InfrastructureFailure;
    }

    int iconId = -2;

    HRESULT hr =
        g_TaskBandGetIconId(
            taskBand,
            taskGroup,
            taskItem,
            0,
            &iconId);

    if (FAILED(hr) ||
        iconId < 0) {
        return
            ResolveResult::
                InfrastructureFailure;
    }

    UINT dpi =
        GetTaskListDpi(
            taskList,
            hdc);

    if (!dpi) {
        return
            ResolveResult::
                InfrastructureFailure;
    }

    HIMAGELIST imageList =
        nullptr;

    hr =
        g_TaskBandGetImageList(
            taskBand,
            dpi,
            &imageList);

    if (FAILED(hr) ||
        !imageList) {
        return
            ResolveResult::
                InfrastructureFailure;
    }

    constexpr UINT
        kEpImageListGetIconFlags =
            0x8001;

    HICON icon =
        g_ImageListGetIcon(
            imageList,
            iconId,
            kEpImageListGetIconFlags);

    if (!icon) {
        return
            ResolveResult::
                InfrastructureFailure;
    }

    DWORD colorRef = 0;

    bool extracted =
        ExtractEpHotTrackColor(
            icon,
            &colorRef);

    DestroyIcon(icon);

    if (!extracted) {
        return
            ResolveResult::
                NoDominantColor;
    }

    *colorRefOut =
        colorRef;

    return
        ResolveResult::Color;
}

bool GetOrResolveHotTrackColor(
    HDC hdc,
    void* taskGroup,
    void* taskList,
    void* taskItem,
    DWORD* colorRefOut) {

    if (!taskGroup ||
        !colorRefOut) {
        return false;
    }

    HotColorState state =
        HotColorState::
            Uninitialized;
    DWORD cached = 0;

    if (ReadHotColorState(
            taskGroup,
            taskItem,
            &state,
            &cached)) {

        if (state ==
            HotColorState::Value) {
            *colorRefOut =
                cached;
            return true;
        }

        if (state ==
            HotColorState::
                NoDominantColor) {

            HWND hwnd =
                GetTaskListHwnd(
                    taskList);

            *colorRefOut =
                GetEpFallbackColorRef(
                    hwnd);
            return true;
        }
    }

    DWORD resolved = 0;

    ResolveResult result =
        ResolveHotTrackColor(
            hdc,
            taskGroup,
            taskList,
            taskItem,
            &resolved);

    if (result ==
        ResolveResult::Color) {

        (void)StoreHotColorState(
            taskGroup,
            taskItem,
            HotColorState::Value,
            resolved);

        *colorRefOut =
            resolved;

        bool expected = false;

        if (g_loggedFirstResolve
                .compare_exchange_strong(
                    expected,
                    true)) {

            DWORD visualRgb =
                ((resolved &
                  0x0000FF) << 16) |
                (resolved &
                 0x00FF00) |
                ((resolved &
                  0xFF0000) >> 16);

            Wh_Log(
                L"First same-paint EP-style color "
                L"resolved: group=%p item=%p "
                L"visualRGB=#%06X colorRef=0x%06X",
                taskGroup,
                taskItem,
                visualRgb,
                resolved);
        }

        return true;
    }

    if (result ==
        ResolveResult::
            NoDominantColor) {

        (void)StoreHotColorState(
            taskGroup,
            taskItem,
            HotColorState::
                NoDominantColor,
            0);

        HWND hwnd =
            GetTaskListHwnd(
                taskList);

        DWORD fallback =
            GetEpFallbackColorRef(
                hwnd);

        *colorRefOut =
            fallback;

        bool expected = false;

        if (g_loggedFirstThemeFallback
                .compare_exchange_strong(
                    expected,
                    true)) {

            DWORD visualRgb =
                ((fallback &
                  0x0000FF) << 16) |
                (fallback &
                 0x00FF00) |
                ((fallback &
                  0xFF0000) >> 16);

            Wh_Log(
                L"First EP no-dominant-color "
                L"fallback: visualRGB=#%06X "
                L"colorRef=0x%06X",
                visualRgb,
                fallback);
        }

        return true;
    }

    // Infrastructure failure: EP leaves the caller's already initialized
    // native source color untouched.
    return false;
}

// ---------------------------------------------------------------------------
// Short _DrawBar context for exact source-color substitution.
// ---------------------------------------------------------------------------

struct DrawFrame {
    DWORD colorRef = 0;
    bool haveColor = false;
};

constexpr unsigned
    kDrawStackDepth = 8;

thread_local DrawFrame
    g_drawStack[kDrawStackDepth] = {};

thread_local unsigned
    g_drawDepth = 0;

thread_local bool
    g_drawOverflow = false;

DrawFrame* CurrentDrawFrame() {
    if (g_drawDepth == 0 ||
        g_drawDepth >
            kDrawStackDepth) {
        return nullptr;
    }

    return
        &g_drawStack[
            g_drawDepth - 1];
}

// ---------------------------------------------------------------------------
// Hooks.
// ---------------------------------------------------------------------------

void __fastcall
GetStatesFromRenderInfo_Hook(
    void* pThis,
    const void* renderInfo,
    bool unknownBool,
    void* taskItem,
    void* renderInfoStates) {

    g_GetStatesFromRenderInfo_Original(
        pThis,
        renderInfo,
        unknownBool,
        taskItem,
        renderInfoStates);

    if (g_epModulePresent.load()) {
        return;
    }

    RememberRenderItem(
        renderInfo,
        taskItem);
}

HRESULT __fastcall
TaskGroupSetIconId_Hook(
    void* taskGroup,
    void* taskItem,
    int iconId) {

    HRESULT hr =
        g_TaskGroupSetIconId_Original(
            taskGroup,
            taskItem,
            iconId);

    if (g_epModulePresent.load()) {
        return hr;
    }

    // ExplorerPatcher's CTaskGroup::SetIconId resets its hot-track value to
    // 0xFF000000 for every icon ID except -2 (the "not loaded yet" sentinel).
    if (iconId != -2) {
        ResetHotColor(
            taskGroup,
            taskItem);

        bool expected = false;

        if (g_loggedFirstReset
                .compare_exchange_strong(
                    expected,
                    true)) {

            Wh_Log(
                L"First EP-style hot-color reset: "
                L"group=%p item=%p iconId=%d",
                taskGroup,
                taskItem,
                iconId);
        }
    }

    return hr;
}

HRESULT __fastcall
TaskListTaskDestroyed_Hook(
    void* pThis,
    void* taskGroup,
    void* taskItem,
    int flags) {

    if (!g_epModulePresent.load()) {
        RemoveDestroyedHotColors(
            taskGroup,
            taskItem);
    }

    return
        g_TaskListTaskDestroyed_Original(
            pThis,
            taskGroup,
            taskItem,
            flags);
}

void __fastcall
DrawBar_Hook(
    void* pThis,
    HDC hdc,
    const void* renderInfo,
    const void* renderInfoStates) {

    if (g_epModulePresent.load()) {
        g_DrawBar_Original(
            pThis,
            hdc,
            renderInfo,
            renderInfoStates);
        return;
    }


    if (g_drawDepth >=
        kDrawStackDepth) {

        bool oldOverflow =
            g_drawOverflow;
        g_drawOverflow = true;

        g_DrawBar_Original(
            pThis,
            hdc,
            renderInfo,
            renderInfoStates);

        g_drawOverflow =
            oldOverflow;
        return;
    }

    DrawFrame& frame =
        g_drawStack[
            g_drawDepth++];

    frame = {};

    void* taskItem = nullptr;

    const Settings settings =
        GetSettingsCopy();

    if (settings.enabled &&
        settings.source == ColorSource::Icon &&
        settings.strength > 0 &&
        ConsumeRenderItem(
            renderInfo,
            &taskItem)) {

        void* taskGroup = nullptr;
        void* taskList = nullptr;

        if (GetTaskGroupAndTaskList(
                pThis,
                &taskGroup,
                &taskList)) {

            DWORD colorRef = 0;

            if (GetOrResolveHotTrackColor(
                    hdc,
                    taskGroup,
                    taskList,
                    taskItem,
                    &colorRef)) {

                frame.colorRef = colorRef;
                frame.haveColor = true;
            }
        }
    } else {
        void* ignored = nullptr;
        ConsumeRenderItem(
            renderInfo,
            &ignored);
    }

    g_DrawBar_Original(
        pThis,
        hdc,
        renderInfo,
        renderInfoStates);

    --g_drawDepth;
}

DWORD ColorRefToSourceRgb(DWORD colorRef) {
    const DWORD r = colorRef & 0xFF;
    const DWORD g = (colorRef >> 8) & 0xFF;
    const DWORD b = (colorRef >> 16) & 0xFF;
    return (r << 16) | (g << 8) | b;
}

DWORD SourceRgbToColorRef(DWORD sourceRgb) {
    const DWORD r = (sourceRgb >> 16) & 0xFF;
    const DWORD g = (sourceRgb >> 8) & 0xFF;
    const DWORD b = sourceRgb & 0xFF;
    return r | (g << 8) | (b << 16);
}

DWORD __fastcall
GetColorFromPreferenceImpl_Hook(
    const void* preference,
    int colorType,
    bool unknownBool,
    int highContrastCacheMode) {

    DWORD nativeColor =
        g_ColorSource_Original(
            preference,
            colorType,
            unknownBool,
            highContrastCacheMode);

    if (g_epModulePresent.load() ||
        g_drawOverflow ||
        __builtin_return_address(0) !=
            g_exactColorCallReturn) {
        return nativeColor;
    }

    const Settings settings =
        GetSettingsCopy();

    if (!settings.enabled) {
        return nativeColor;
    }

    if (settings.finalOutputEnabled) {
        bool expected = false;
        if (g_finalOutputWarningLogged.compare_exchange_strong(
                expected,
                true)) {
            Wh_Log(
                L"Native classic-taskbar backend: finalOutput.* is "
                L"ExplorerPatcher-only and is ignored.");
        }
    }

    if (settings.source == ColorSource::Native ||
        settings.strength <= 0) {
        return nativeColor;
    }

    DrawFrame* frame = CurrentDrawFrame();
    bool haveSelectedColor = false;
    bool usedIconColor = false;
    bool usedFallbackColor = false;
    DWORD rawIconColor = 0;
    DWORD selectedSourceColor = 0;

    if (settings.source == ColorSource::Fixed) {
        selectedSourceColor = settings.fixedColor;
        haveSelectedColor = true;
    } else if (frame && frame->haveColor) {
        rawIconColor =
            ColorRefToSourceRgb(frame->colorRef);

        const RgbColor rawRgb =
            SourceColorToRgb(rawIconColor);

        const int rawSaturation =
            static_cast<int>(std::lround(
                HsvSaturation(rawRgb) * 100.0));
        const int rawBrightness =
            static_cast<int>(std::lround(
                HsvBrightness(rawRgb) * 100.0));

        if (rawSaturation >= settings.filterMinRawSaturation &&
            rawBrightness >= settings.filterMinRawBrightness &&
            rawBrightness <= settings.filterMaxRawBrightness) {

            RgbColor processed =
                ProcessIconColor(rawRgb, settings);
            selectedSourceColor =
                RgbToSourceColor(processed);
            haveSelectedColor = true;
            usedIconColor = true;
        }
    }

    if (!haveSelectedColor &&
        settings.source == ColorSource::Icon &&
        settings.fallback == FallbackMode::Fixed) {
        selectedSourceColor = settings.fixedColor;
        haveSelectedColor = true;
        usedFallbackColor = true;
    }

    if (!haveSelectedColor) {
        return nativeColor;
    }

    RgbColor selected =
        SourceColorToRgb(selectedSourceColor);
    selected =
        ApplyTintAndPalette(selected, settings);

    const DWORD nativeSourceRgb =
        ColorRefToSourceRgb(
            nativeColor & 0x00FFFFFF);
    const RgbColor nativeRgb =
        SourceColorToRgb(nativeSourceRgb);
    const RgbColor blended =
        BlendRgb(
            nativeRgb,
            selected,
            settings.strength / 100.0);

    const DWORD blendedSourceRgb =
        RgbToSourceColor(blended);
    const DWORD replacement =
        (nativeColor & 0xFF000000) |
        SourceRgbToColorRef(blendedSourceRgb);

    bool shouldLog = settings.logEverySubstitution;
    if (!shouldLog) {
        bool expected = false;
        shouldLog =
            g_firstSubstitutionLogged.compare_exchange_strong(
                expected,
                true);
    }

    if (shouldLog) {
        Wh_Log(
            L"Indicator color (native classic): native=0x%08X "
            L"rawIcon=0x%06X selected=0x%06X replacement=0x%08X "
            L"icon=%d fallback=%d strength=%d.",
            nativeColor,
            rawIconColor,
            selectedSourceColor,
            replacement,
            usedIconColor,
            usedFallbackColor,
            settings.strength);
    }

    return replacement;
}



enum class InitResult { NotApplicable, Hooked, Failed };

InitResult Initialize() {
    if (g_epModulePresent.load()) {
        return InitResult::NotApplicable;
    }

    Wh_Log(L"Inspecting native classic taskbar backend.");

    if (!IsApplicableBuild()) {
        return InitResult::NotApplicable;
    }

    HMODULE explorer = GetModuleHandleW(nullptr);
    if (!explorer) return InitResult::Failed;

    if (!FindExplorerSymbols(explorer)) {
        Wh_Log(L"Required Microsoft public symbols weren't resolved; native backend failed closed.");
        return InitResult::Failed;
    }

    if (!ValidateGetGroupAbi() ||
        !ValidateTaskBandGetImageListAbi() ||
        !DiscoverTaskListMemberOffset() ||
        !DiscoverExactColorCallsite() ||
        !ResolveImageListExport()) {
        Wh_Log(L"One or more native ABI/callsite validations failed; native backend failed closed.");
        return InitResult::Failed;
    }

    g_TaskBandGetIconId = reinterpret_cast<TaskBandGetIconId_t>(g_symbols.taskBandGetIconId);
    g_TaskBandGetImageList = reinterpret_cast<TaskBandGetImageList_t>(g_symbols.taskBandGetImageList);
    g_TaskBtnGroupGetGroup = reinterpret_cast<TaskBtnGroupGetGroup_t>(g_symbols.taskBtnGroupGetGroup);

    bool ok = true;
    ok &= !!Wh_SetFunctionHook(g_symbols.getStatesFromRenderInfo,
        reinterpret_cast<void*>(GetStatesFromRenderInfo_Hook),
        reinterpret_cast<void**>(&g_GetStatesFromRenderInfo_Original));
    ok &= !!Wh_SetFunctionHook(g_symbols.drawBar,
        reinterpret_cast<void*>(DrawBar_Hook),
        reinterpret_cast<void**>(&g_DrawBar_Original));
    ok &= !!Wh_SetFunctionHook(g_symbols.taskGroupSetIconId,
        reinterpret_cast<void*>(TaskGroupSetIconId_Hook),
        reinterpret_cast<void**>(&g_TaskGroupSetIconId_Original));
    ok &= !!Wh_SetFunctionHook(g_symbols.taskListTaskDestroyed,
        reinterpret_cast<void*>(TaskListTaskDestroyed_Hook),
        reinterpret_cast<void**>(&g_TaskListTaskDestroyed_Original));
    ok &= !!Wh_SetFunctionHook(g_colorSourceTarget,
        reinterpret_cast<void*>(GetColorFromPreferenceImpl_Hook),
        reinterpret_cast<void**>(&g_ColorSource_Original));

    if (!ok) {
        Wh_Log(L"One or more native classic-taskbar hooks failed.");
        return InitResult::Failed;
    }

    g_hooked.store(true);

    const wchar_t* familyName = L"unknown";
    switch (g_buildFamily) {
        case BuildFamily::Win10_17763:
            familyName = L"17763";
            break;
        case BuildFamily::Win10_19041:
            familyName = L"19041-19045";
            break;
        case BuildFamily::Server_20348:
            familyName = L"20348";
            break;
        default:
            break;
    }

    Wh_Log(
        L"Native classic-taskbar colored-indicator backend hooked "
        L"(family=%s colorHook=%d).",
        familyName,
        static_cast<int>(
            g_colorSourceHookKind));

    if (g_buildFamily == BuildFamily::Win10_17763 ||
        g_buildFamily == BuildFamily::Server_20348) {
        Wh_Log(
            L"Native family %s is statically validated against the supplied "
            L"Explorer/PDB pair but has not yet been runtime stress-tested.",
            familyName);
    }

    return InitResult::Hooked;
}

bool IsHooked() { return g_hooked.load(); }
void SettingsChanged() { g_finalOutputWarningLogged.store(false); }
void Uninitialize() { FreeAllHotColors(); }

} // namespace NativeWin10

HMODULE WINAPI LoadLibraryExW_Hook(
    LPCWSTR lpLibFileName,
    HANDLE hFile,
    DWORD dwFlags) {

    HMODULE module =
        g_LoadLibraryExW_Original(
            lpLibFileName,
            hFile,
            dwFlags);

    if (module &&
        IsExplorerPatcherTaskbarModule(
            module)) {

        g_epModulePresent.store(true);

        if (!g_epHooked.load() &&
            HookExplorerPatcherTaskbar(
                module)) {
            Wh_ApplyHookOperations();
        }
    }

    return module;
}

}  // namespace

BOOL Wh_ModInit() {
    LoadSettings();

    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    if (!kernelBase) {
        Wh_Log(L"kernelbase.dll not found.");
        return FALSE;
    }

    auto loadLibraryExW = reinterpret_cast<LoadLibraryExW_t>(
        GetProcAddress(kernelBase, "LoadLibraryExW"));
    if (!loadLibraryExW) {
        Wh_Log(L"LoadLibraryExW not found.");
        return FALSE;
    }

    if (!Wh_SetFunctionHook(
            reinterpret_cast<void*>(loadLibraryExW),
            reinterpret_cast<void*>(LoadLibraryExW_Hook),
            reinterpret_cast<void**>(&g_LoadLibraryExW_Original))) {
        Wh_Log(L"Failed to hook LoadLibraryExW.");
        return FALSE;
    }

    if (HMODULE module = FindLoadedExplorerPatcherTaskbarModule()) {
        g_epModulePresent.store(true);
        if (!HookExplorerPatcherTaskbar(module)) {
            return FALSE;
        }
        Wh_Log(L"Active backend: ExplorerPatcher.");
        return TRUE;
    }

    const auto nativeResult = NativeWin10::Initialize();
    if (nativeResult == NativeWin10::InitResult::Failed) {
        return FALSE;
    }

    if (nativeResult == NativeWin10::InitResult::Hooked) {
        Wh_Log(L"Active backend: native classic taskbar.");
    } else {
        Wh_Log(L"No supported native backend active in this explorer.exe process; waiting for an ExplorerPatcher taskbar late load.");
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_epHooked.load()) {
        if (HMODULE module = FindLoadedExplorerPatcherTaskbarModule()) {
            g_epModulePresent.store(true);
            if (HookExplorerPatcherTaskbar(module)) {
                Wh_ApplyHookOperations();
                Wh_Log(L"Active backend switched to ExplorerPatcher.");
            }
        }
    }
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    g_firstSubstitutionLogged.store(false);
    NativeWin10::SettingsChanged();

    const Settings settings = GetSettingsCopy();
    if (settings.refreshOnSettingsChange &&
        (g_epHooked.load() || NativeWin10::IsHooked())) {
        RefreshTaskbars();
    }
}

void Wh_ModUninit() {
    NativeWin10::Uninitialize();
}

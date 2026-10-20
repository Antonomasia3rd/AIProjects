#pragma once
// Pure geometry for a WinRT pin flyout. Native callers supply physical monitor
// work-area/client-origin coordinates and convert once into the owner's DIPs.
namespace aip { namespace content {
struct SecondaryPinArea { double left = 0, top = 0, right = 0, bottom = 0; };
struct SecondaryPinAnchor { float x = 0, y = 0, width = 1, height = 1; };
inline SecondaryPinAnchor CenteredSecondaryPinAnchor(const SecondaryPinArea& work,
    double clientOriginX, double clientOriginY, unsigned dpi) {
    const double scale = 96.0 / (dpi ? dpi : 96);
    const auto centerX = work.left + (work.right - work.left) / 2;
    const auto centerY = work.top + (work.bottom - work.top) / 2;
    return {static_cast<float>((centerX - clientOriginX) * scale),
        static_cast<float>((centerY - clientOriginY) * scale), 1, 1};
}
} }

#pragma once
#include "../asusblink/service.h"
#include "../content_engine.h"

namespace aip { namespace content {
inline Text AsusContent(const aip::asus::Snapshot& snapshot) {
    Text text;
    text.primary = snapshot.faulted ? L"ASUS source error" : snapshot.hardwareEnabled ? L"ASUS indicators" : L"ASUS preview";
    text.secondary = snapshot.error.empty() ? snapshot.status : snapshot.error;
    if (snapshot.pattern.micState >= 0) AppendBounded(text.secondary, L"Mic LED requested: " + std::to_wstring(snapshot.pattern.micState));
    if (snapshot.pattern.keyboardState >= 0) AppendBounded(text.secondary, L"Keyboard requested: " + std::to_wstring(snapshot.pattern.keyboardState));
    // Keep state/error text in the medium tile's body, not a large counter.
    return text;
}
} }

#pragma once
#include "../DiscordRPC/service.h"
#include "../content_engine.h"

namespace aip { namespace content {
// Presentation adapter over an in-process service snapshot. It does not read
// another application's files, spawn DiscordRPC, or create a second transport.
inline Text DiscordContent(const aip::discord::Snapshot& snapshot) {
    Text text;
    text.primary = snapshot.details.empty() ? snapshot.name : snapshot.details;
    if (text.primary.empty()) text.primary = L"Discord presence";
    // A nonempty badge selects the medium counter template, which has no
    // secondary region. Status belongs in normal text so state/errors survive.
    if (snapshot.phase == aip::discord::Phase::Preview) text.secondary = L"Preview";
    else if (snapshot.phase == aip::discord::Phase::Error) text.secondary = L"Error";
    else if (snapshot.phase == aip::discord::Phase::Active) text.secondary = L"Sending";
    else if (snapshot.phase == aip::discord::Phase::Stopping) text.secondary = L"Stopping";
    else if (!snapshot.running) text.secondary = L"Stopped";
    else text.secondary = L"Loading";
    if (!snapshot.error.empty()) AppendBounded(text.secondary, snapshot.error);
    AppendBounded(text.secondary, snapshot.state);
    return text;
}
} }

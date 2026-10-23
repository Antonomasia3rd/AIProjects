#pragma once
// Shared with the original standalone DiscordRPC defaults. These are template
// expressions, not preview/sample data; the service resolves them from the
// current foreground window and machine counters without sending by default.
namespace aip { namespace discord {
inline constexpr const wchar_t* DefaultActivityName = L"a game";
inline constexpr const wchar_t* DefaultDetailsTemplate = L"{win_title}";
inline constexpr const wchar_t* DefaultStateTemplate = L"CPU: {cpu}, RAM: {ram_used}/{ram_total} ({ram_pct})";
} }

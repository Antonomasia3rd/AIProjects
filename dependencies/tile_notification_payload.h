#pragma once

// Private host/helper transport metadata, never an attribute in Windows tile
// XML. Raw legacy XML retains the single-notification behavior byte-for-byte.
// Pure C++17: no WinRT, files, clocks, Startup, package or notification calls.
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

namespace aip { namespace tile_notification {
inline constexpr std::size_t MaximumXmlUtf8Bytes = 64u * 1024u;
inline constexpr std::size_t MaximumHeaderCharacters = 128u;
inline constexpr wchar_t Marker[] = L"AIP-TILE-NOTIFICATION/1\t";
inline constexpr wchar_t SecondaryMarker[] = L"AIP-TILE-NOTIFICATION/2\t";

struct Options {
    bool queued = false;
    std::wstring tag;
    bool reset = false;
    std::wstring secondaryTileId{};
};
struct Payload { Options options; std::wstring xml; };

inline bool ValidOptions(const Options& options, std::wstring& error)
{
    if (options.queued != !options.tag.empty() || options.tag.size() > 16u) {
        error = L"Queued tile notifications require a tag of 1-16 alphanumeric characters; single notifications have no tag.";
        return false;
    }
    for (wchar_t value : options.tag) {
        if (!((value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z') ||
            (value >= L'0' && value <= L'9'))) {
            error = L"The private tile notification tag must contain only ASCII letters and digits.";
            return false;
        }
    }
    if (!options.secondaryTileId.empty()) {
        const auto alphanumeric = [](wchar_t value) {
            return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z') ||
                (value >= L'0' && value <= L'9');
        };
        if (options.secondaryTileId.size() > 64u || !alphanumeric(options.secondaryTileId.front())) {
            error = L"A secondary tile ID must start with a letter/digit and contain at most 64 characters.";
            return false;
        }
        for (wchar_t value : options.secondaryTileId) {
            if (!alphanumeric(value) && value != L'.' && value != L'_') {
                error = L"A secondary tile ID supports only ASCII letters, digits, periods and underscores.";
                return false;
            }
        }
    }
    return true;
}

inline bool ValidXmlText(const std::wstring& xml, std::wstring& error)
{
    if (xml.empty() || xml.size() >= MaximumXmlUtf8Bytes) {
        error = L"Tile notification XML must be nonempty and smaller than 64 KiB in UTF-8.";
        return false;
    }
    std::size_t start = 0;
    if (xml[start] == 0xfeff) ++start;
    while (start < xml.size() && (xml[start] == L' ' || xml[start] == L'\t' || xml[start] == L'\r' || xml[start] == L'\n')) ++start;
    if (start == xml.size() || xml[start] != L'<') {
        error = L"The private tile notification body must contain XML.";
        return false;
    }
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < xml.size(); ++index) {
        unsigned long point = static_cast<unsigned long>(xml[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (point >= 0xd800 && point <= 0xdbff) {
                if (++index == xml.size()) { error = L"Tile XML contains an incomplete Unicode character."; return false; }
                const auto low = static_cast<unsigned long>(xml[index]);
                if (low < 0xdc00 || low > 0xdfff) { error = L"Tile XML contains an invalid Unicode character."; return false; }
                point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
            }
        }
        if ((point < 0x20 && point != 9 && point != 10 && point != 13) ||
            (point >= 0xd800 && point <= 0xdfff) || point == 0xfffe || point == 0xffff || point > 0x10ffff) {
            error = L"Tile XML contains a character unsupported by XML.";
            return false;
        }
        bytes += point <= 0x7f ? 1u : point <= 0x7ff ? 2u : point <= 0xffff ? 3u : 4u;
        if (bytes >= MaximumXmlUtf8Bytes) {
            error = L"Tile notification XML must be smaller than 64 KiB in UTF-8.";
            return false;
        }
    }
    return true;
}

inline bool Encode(const std::wstring& xml, const Options& options, std::wstring& transport, std::wstring& error)
{
    transport.clear(); error.clear();
    if (!ValidOptions(options, error) || !ValidXmlText(xml, error)) return false;
    if (!options.queued && !options.reset && options.secondaryTileId.empty()) { transport = xml; return true; }
    transport = options.secondaryTileId.empty() ? Marker : SecondaryMarker;
    transport += options.queued ? L'1' : L'0';
    transport += L'\t'; transport += options.reset ? L'1' : L'0';
    transport += L'\t'; transport += options.tag;
    if (!options.secondaryTileId.empty()) { transport += L'\t'; transport += options.secondaryTileId; }
    transport += L"\r\n";
    transport += xml;
    return true;
}

inline bool Decode(const std::wstring& transport, Payload& payload, std::wstring& error)
{
    payload = {}; error.clear();
    if (transport.empty() || transport.size() >= MaximumXmlUtf8Bytes + MaximumHeaderCharacters) {
        error = L"Tile notification transport is empty or exceeds its bounded size.";
        return false;
    }
    std::size_t offset = transport.front() == 0xfeff ? 1u : 0u;
    const std::wstring marker = Marker;
    const std::wstring secondaryMarker = SecondaryMarker;
    Payload candidate;
    const bool secondary = transport.compare(offset, secondaryMarker.size(), secondaryMarker) == 0;
    if (secondary || transport.compare(offset, marker.size(), marker) == 0) {
        const auto markerSize = secondary ? secondaryMarker.size() : marker.size();
        const auto end = transport.find(L"\r\n", offset + markerSize);
        if (end == std::wstring::npos || end - offset > MaximumHeaderCharacters) {
            error = L"The private tile notification header is incomplete or oversized.";
            return false;
        }
        const auto fields = transport.substr(offset + markerSize, end - offset - markerSize);
        if (fields.size() < 4u || (fields[0] != L'0' && fields[0] != L'1') || fields[1] != L'\t' ||
            (fields[2] != L'0' && fields[2] != L'1') || fields[3] != L'\t') {
            error = L"The private tile notification header contains invalid flags.";
            return false;
        }
        candidate.options.queued = fields[0] == L'1';
        candidate.options.reset = fields[2] == L'1';
        if (secondary) {
            const auto targetSeparator = fields.find(L'\t', 4);
            if (targetSeparator == std::wstring::npos || targetSeparator + 1 == fields.size()) {
                error = L"The secondary tile notification header requires a target ID.";
                return false;
            }
            candidate.options.tag = fields.substr(4, targetSeparator - 4);
            candidate.options.secondaryTileId = fields.substr(targetSeparator + 1);
        } else candidate.options.tag = fields.substr(4);
        if (!ValidOptions(candidate.options, error)) return false;
        candidate.xml = transport.substr(end + 2);
    } else candidate.xml = transport.substr(offset);
    if (!ValidXmlText(candidate.xml, error)) return false;
    payload = std::move(candidate);
    return true;
}

// Validation and XML/notification/tag construction precede updater creation.
// A malformed transport or loader/factory exception cannot clear an old queue.
// Windows chooses queue rotation; the caller explicitly requests layout resets.
template<class LoadXml, class MakeNotification, class SetTag, class MakeUpdater>
bool Deliver(const std::wstring& transport, LoadXml loadXml, MakeNotification makeNotification,
    SetTag setTag, MakeUpdater makeUpdater, std::wstring& error)
{
    Payload payload;
    if (!Decode(transport, payload, error)) return false;
    if constexpr (!std::is_invocable_v<MakeUpdater, const std::wstring&>) {
        if (!payload.options.secondaryTileId.empty()) {
            error = L"This notification consumer does not support secondary tile targeting.";
            return false;
        }
    }
    auto document = loadXml(payload.xml);
    auto notification = makeNotification(document);
    if (payload.options.queued) setTag(notification, payload.options.tag);
    auto updater = [&] {
        if constexpr (std::is_invocable_v<MakeUpdater, const std::wstring&>) return makeUpdater(payload.options.secondaryTileId);
        else return makeUpdater();
    }();
    updater.EnableNotificationQueue(payload.options.queued);
    if (payload.options.reset) updater.Clear();
    updater.Update(notification);
    return true;
}
} }

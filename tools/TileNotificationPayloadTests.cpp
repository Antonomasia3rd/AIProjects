#include "../dependencies/tile_notification_payload.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace notification = aip::tile_notification;
static int checks = 0;
static void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

struct FakeNotification { std::wstring xml, tag; };
struct FakeState {
    bool queued = false;
    int loads = 0, factories = 0, updaterFactories = 0, clears = 0, updates = 0;
    bool rejectXml = false, rejectNotification = false, rejectTag = false;
    std::wstring updaterTarget;
    std::vector<std::string> events;
    std::vector<FakeNotification> notifications;
};
struct FakeUpdater {
    FakeState& state;
    void EnableNotificationQueue(bool enabled) { state.queued = enabled; state.events.push_back(enabled ? "queue=1" : "queue=0"); }
    void Clear() { ++state.clears; state.notifications.clear(); state.events.push_back("clear"); }
    void Update(const FakeNotification& notification) {
        ++state.updates; state.events.push_back("update");
        if (!state.queued) state.notifications.clear();
        else state.notifications.erase(std::remove_if(state.notifications.begin(), state.notifications.end(),
            [&](const auto& old) { return old.tag == notification.tag; }), state.notifications.end());
        state.notifications.push_back(notification);
        if (state.notifications.size() > 5) state.notifications.erase(state.notifications.begin());
    }
};
static bool Deliver(const std::wstring& transport, FakeState& state, std::wstring& error) {
    return notification::Deliver(transport,
        [&](const std::wstring& xml) {
            ++state.loads; state.events.push_back("load");
            if (state.rejectXml) throw std::runtime_error("fake XML parse rejected");
            return xml;
        },
        [&](const std::wstring& xml) {
            ++state.factories; state.events.push_back("notification");
            if (state.rejectNotification) throw std::runtime_error("fake notification rejected");
            return FakeNotification{xml, {}};
        },
        [&](FakeNotification& value, const std::wstring& tag) {
            state.events.push_back("tag");
            if (state.rejectTag) throw std::runtime_error("fake tag rejected");
            value.tag = tag;
        },
        [&](const std::wstring& target) { ++state.updaterFactories; state.updaterTarget = target; state.events.push_back("updater"); return FakeUpdater{state}; }, error);
}
static std::wstring Encode(const std::wstring& xml, bool queued, const std::wstring& tag, bool reset) {
    std::wstring transport, error;
    Check(notification::Encode(xml, {queued, tag, reset}, transport, error) && error.empty(), "encode expected valid payload");
    return transport;
}
static void TestLegacyAndRoundTrips() {
    const std::wstring xml = L"<?xml version=\"1.0\"?><tile><visual/></tile>";
    const auto raw = Encode(xml, false, {}, false);
    notification::Payload payload;
    std::wstring error;
    Check(raw == xml && notification::Decode(raw, payload, error) && payload.xml == xml &&
        !payload.options.queued && !payload.options.reset && payload.options.tag.empty(), "legacy raw XML is unchanged and decodes to single-notification defaults");
    Check(notification::Decode(L"\xfeff" + raw, payload, error) && payload.xml == raw, "one transport BOM is accepted without altering the XML body");
    for (int index = 0; index < 256; ++index) {
        const bool queued = index % 2 != 0, reset = index % 7 == 0;
        const std::wstring tag = queued ? L"Content" + std::to_wstring(index) : L"";
        const auto transport = Encode(xml, queued, tag, reset);
        Check(notification::Decode(transport, payload, error) && payload.xml == xml &&
            payload.options.queued == queued && payload.options.reset == reset && payload.options.tag == tag,
            "canonical queue/reset/tag payload round-trips exactly");
        Check((!queued && !reset) == (transport == xml), "only nondefault private options require a wrapper");
    }
    const auto finalTag = Encode(xml, true, L"1234567890ABCDEF", false);
    Check(notification::Decode(finalTag, payload, error) && payload.options.tag.size() == 16, "sixteen-character stable tag remains intact");
}
static void TestMalformed() {
    const std::wstring prefix = notification::Marker;
    const std::wstring xml = L"<tile/>";
    const std::vector<std::wstring> malformed = {
        L"", L"   ", L"not xml", L"AIP-TILE-NOTIFICATION/2\t1\t0\tTag\r\n" + xml,
        prefix + L"1\t0\tTag\n" + xml, prefix + L"2\t0\tTag\r\n" + xml,
        prefix + L"1\t2\tTag\r\n" + xml, prefix + L"1\t0\r\n" + xml,
        prefix + L"1\t0\t\r\n" + xml, prefix + L"0\t0\tTag\r\n" + xml,
        prefix + L"1\t0\tHas-Dash\r\n" + xml, prefix + L"1\t0\tHas_Tab\r\n" + xml,
        prefix + L"1\t0\t1234567890ABCDEFG\r\n" + xml,
        prefix + L"1\t0\tTag\tduplicate\r\n" + xml, prefix + L"1\t1\tTag\r\n",
        prefix + L"1\t1\tTag\r\nsecond private header", std::wstring(L"<tile>\0</tile>", 14),
        L"<tile>\x01</tile>", L"<tile>\xd800</tile>", L"<tile>\xdc00</tile>"
    };
    for (const auto& raw : malformed) {
        notification::Payload payload{{true, L"old", true}, L"old"};
        std::wstring error;
        Check(!notification::Decode(raw, payload, error) && !error.empty() && payload.xml.empty() &&
            payload.options.tag.empty() && !payload.options.queued && !payload.options.reset,
            "malformed transport is rejected without retaining old decoded state");
        FakeState state;
        Check(!Deliver(raw, state, error) && state.events.empty() && state.updates == 0 && state.clears == 0,
            "malformed payload cannot parse XML, construct an updater, clear or update");
    }
    for (const auto& options : std::vector<notification::Options>{{true, {}, false}, {false, L"Tag", false},
        {true, L"space tag", false}, {true, L"Asia\u65e5", false}, {true, L"1234567890ABCDEFG", false}}) {
        std::wstring result = L"old", error;
        Check(!notification::Encode(xml, options, result, error) && result.empty() && !error.empty(), "invalid options never publish a partial private header");
    }
}
static void TestXmlBoundsAndUnicode() {
    const std::wstring prefix = L"<tile>", suffix = L"</tile>";
    std::wstring error, transport;
    const std::wstring boundary = prefix + std::wstring(notification::MaximumXmlUtf8Bytes - 1 - prefix.size() - suffix.size(), L'a') + suffix;
    Check(notification::Encode(boundary, {true, L"Bound", false}, transport, error), "maximum supported ASCII XML plus metadata remains within the XML-specific bound");
    notification::Payload payload;
    Check(notification::Decode(transport, payload, error) && payload.xml == boundary, "bounded wrapper does not incorrectly count metadata against valid XML length");
    Check(!notification::Encode(boundary + L'a', {}, transport, error), "XML at exactly 64KiB is rejected");
    Check(!notification::Encode(prefix + std::wstring(22000, L'\u65e5') + suffix, {}, transport, error), "UTF-8 XML byte bound applies to Unicode rather than wchar count");
    Check(notification::Encode(L"<tile>\u65e5\U0001f600</tile>", {true, L"Unicode", false}, transport, error) &&
        notification::Decode(transport, payload, error), "valid CJK and supplementary Unicode text round-trips in native and portable wchar layouts");
}
static void TestDeliveryAndReset() {
    FakeState state;
    std::wstring error;
    Check(Deliver(Encode(L"<tile>one</tile>", true, L"Weather", true), state, error) && state.clears == 1 && state.updates == 1 && state.queued,
        "initial native layout reset enables queue and clears exactly once before update");
    Check(state.events == std::vector<std::string>{"load", "notification", "tag", "updater", "queue=1", "clear", "update"}, "metadata/XML/notification/tag preparation precedes updater side effects");
    Check(Deliver(Encode(L"<tile>two</tile>", true, L"Media", false), state, error) && state.clears == 1 && state.notifications.size() == 2,
        "adding another native slot preserves the first notification without clearing");
    Check(Deliver(Encode(L"<tile>latest one</tile>", true, L"Weather", false), state, error) && state.clears == 1 && state.notifications.size() == 2 &&
        std::count_if(state.notifications.begin(), state.notifications.end(), [](const auto& value) { return value.tag == L"Weather" && value.xml == L"<tile>latest one</tile>"; }) == 1,
        "same semantic tag replaces a slot without clearing the queue");
    Check(Deliver(Encode(L"<tile>single</tile>", false, {}, true), state, error) && !state.queued && state.clears == 2 && state.notifications.size() == 1,
        "an explicit departure/reset carries unqueued metadata and clears only on that request");
    FakeState legacy;
    Check(Deliver(L"<tile/>", legacy, error) && legacy.clears == 0 && !legacy.queued &&
        legacy.events == std::vector<std::string>{"load", "notification", "updater", "queue=0", "update"}, "raw XML keeps existing single-update behavior without Tag or Clear");
    for (int stage = 0; stage < 3; ++stage) {
        FakeState failed;
        failed.rejectXml = stage == 0; failed.rejectNotification = stage == 1; failed.rejectTag = stage == 2;
        bool rejected = false;
        try { Deliver(Encode(L"<tile/>", true, L"Rejected", true), failed, error); }
        catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && failed.updaterFactories == 0 && failed.clears == 0 && failed.updates == 0,
            "XML, notification and tag construction failures precede every updater operation");
    }
}
static void TestSecondaryTargets() {
    const std::wstring xml = L"<tile><visual/></tile>";
    std::wstring error, transport;
    notification::Payload payload;
    for (const auto* target : {L"AipSource_smtc", L"AipSource_rss", L"AipSource_genshin", L"AipSource_hsr", L"AipSource_zzz", L"AipSource_discord",
        L"AipAction_toggle", L"AipAction_play", L"AipAction_pause", L"AipAction_previous", L"AipAction_next",
        L"AipAction_repeat", L"AipAction_shuffle", L"AipAction_volume_down", L"AipAction_volume_up",
        L"AipSource_rss-0123456789abcdef", L"AipSource_prayertimes-second-city", L"AipSource_news-one"}) {
        Check(notification::Encode(xml, {false, {}, false, target}, transport, error) &&
            transport.find(notification::SecondaryMarker) == 0 && notification::Decode(transport, payload, error) &&
            payload.xml == xml && payload.options.secondaryTileId == target && !payload.options.queued,
            "secondary source target uses strict version-two metadata and unchanged XML body");
        FakeState state;
        Check(Deliver(transport, state, error) && state.updaterTarget == target && state.clears == 0,
            "secondary source uses its own updater without falling back to the primary tile");
    }
    Check(notification::Encode(xml, {true, L"Source", true, L"AipSource_smtc"}, transport, error), "secondary queue/reset/tag metadata encodes together");
    FakeState queued;
    Check(Deliver(transport, queued, error) && queued.queued && queued.clears == 1 && queued.updaterTarget == L"AipSource_smtc",
        "secondary queue reset targets only the chosen tile updater");
    int legacyCalls = 0;
    Check(!notification::Deliver(transport,
        [&](const auto& body) { ++legacyCalls; return body; },
        [&](const auto& body) { ++legacyCalls; return FakeNotification{body, {}}; },
        [&](auto&, const auto&) { ++legacyCalls; },
        [&] { ++legacyCalls; return FakeUpdater{queued}; }, error) && legacyCalls == 0 && !error.empty(),
        "a legacy no-target factory rejects a secondary payload before any call instead of updating primary");
    FakeState primary;
    Check(Deliver(xml, primary, error) && primary.updaterTarget.empty(), "raw legacy XML still selects the primary updater");
    const std::wstring prefix = notification::SecondaryMarker;
    for (const auto& bad : std::vector<std::wstring>{
        prefix + L"0\t0\t\r\n" + xml, prefix + L"0\t0\t\t\r\n" + xml,
        prefix + L"0\t0\t\t_bad\r\n" + xml, prefix + L"0\t0\t\tbad target\r\n" + xml,
        prefix + L"0\t0\t\tAipSource_rss/invalid\r\n" + xml,
        prefix + L"0\t0\t\tAipSource_rss:invalid\r\n" + xml,
        prefix + L"0\t0\t\tAipSource_rss\x1f" L"invalid\r\n" + xml,
        prefix + L"0\t0\t\tAipSource_smtc\textra\r\n" + xml,
        prefix + L"0\t0\t\t" + std::wstring(65, L'a') + L"\r\n" + xml }) {
        FakeState state;
        Check(!Deliver(bad, state, error) && state.events.empty(), "malformed secondary target cannot load, clear or update either tile");
    }
    Check(notification::Encode(xml, {false, {}, false, std::wstring(64, L'a')}, transport, error) &&
        notification::Decode(transport, payload, error) && payload.options.secondaryTileId.size() == 64,
        "maximum Windows secondary tile ID fits its bounded private header");
    const auto maximumInstance = L"AipSource_" + std::wstring(53,L'r') + L"-";
    Check(notification::Encode(xml, {false, {}, false, maximumInstance}, transport, error) &&
        notification::Decode(transport,payload,error) && payload.options.secondaryTileId == maximumInstance,
        "maximum-length independent tile ID preserves a supported hyphen through the actual payload encoder and decoder");
    for (const auto& invalid : std::vector<std::wstring>{L"AipSource_rss?invalid",L"AipSource_rss/invalid",L"AipSource_rss\ninvalid",
        L"-AipSource_rss",L"AipSource_" + std::wstring(55,L'r')})
        Check(!notification::Encode(xml,{false,{},false,invalid},transport,error) && transport.empty() && !error.empty(),
            "independent tile hyphen support retains target punctuation, control, first-character and length rejection");
}
int main() {
    try {
        TestLegacyAndRoundTrips(); TestMalformed(); TestXmlBoundsAndUnicode(); TestDeliveryAndReset(); TestSecondaryTargets();
        std::cout << "Tile notification payload: " << checks << " checks passed; no Windows APIs invoked\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

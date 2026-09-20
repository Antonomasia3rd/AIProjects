// Behavioral tests of the real portable content policy. No Windows headers,
// network, timers, configuration files, or shell registration are involved.
#include "../dependencies/content_engine.h"
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

using namespace aip::content;
namespace {
int checks = 0;
void Require(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
struct Profile {
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> values;
    void Set(std::wstring section, std::wstring key, std::wstring value) {
        values[{Lower(section), Lower(key)}] = std::move(value);
    }
    std::wstring operator()(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) const {
        const auto found = values.find({Lower(section), Lower(key)});
        return found == values.end() ? std::wstring(fallback) : found->second;
    }
};
bool Same(const Configuration& a, const Configuration& b) {
    if (a.enabled != b.enabled || a.cycle != b.cycle || a.cycleSeconds != b.cycleSeconds ||
        a.refreshSeconds != b.refreshSeconds || a.textMode != b.textMode || a.entries.size() != b.entries.size()) return false;
    if (a.caps.actionsEnabled != b.caps.actionsEnabled || a.caps.target != b.caps.target || a.caps.intervalMs != b.caps.intervalMs ||
        a.asus.hardwareEnabled != b.asus.hardwareEnabled || a.asus.readDiskActivity != b.asus.readDiskActivity ||
        a.asus.paused != b.asus.paused || a.asus.profilePath != b.asus.profilePath || a.asus.settings != b.asus.settings) return false;
    if (a.discord.send != b.discord.send || a.discord.clearOnStop != b.discord.clearOnStop ||
        a.discord.refreshSeconds != b.discord.refreshSeconds || a.discord.timeoutMs != b.discord.timeoutMs ||
        a.discord.profilePath != b.discord.profilePath || a.discord.clientId != b.discord.clientId ||
        a.discord.name != b.discord.name || a.discord.details != b.discord.details || a.discord.state != b.discord.state) return false;
    if (a.notes.refreshSeconds != b.notes.refreshSeconds || a.notes.timeoutSeconds != b.notes.timeoutSeconds ||
        a.notes.maximumBytes != b.notes.maximumBytes) return false;
    for (std::size_t i = 0; i < a.notes.accounts.size(); ++i)
        if (a.notes.accounts[i].uid != b.notes.accounts[i].uid || a.notes.accounts[i].token != b.notes.accounts[i].token ||
            a.notes.accounts[i].userId != b.notes.accounts[i].userId) return false;
    for (std::size_t i = 0; i < a.entries.size(); ++i) {
        const auto& x = a.entries[i]; const auto& y = b.entries[i];
        if (x.enabled != y.enabled || x.name != y.name || x.background != y.background ||
            x.imagePath != y.imagePath || x.sources != y.sources || x.custom.primary != y.custom.primary ||
            x.custom.secondary != y.custom.secondary || x.custom.badge != y.custom.badge || x.rssItem != y.rssItem || x.notesResource != y.notesResource) return false;
    }
    return true;
}
Configuration Read(const Profile& p) {
    Configuration cfg;
    std::wstring error;
    if (!ReadConfiguration(p, cfg, error)) throw std::runtime_error("valid profile was rejected");
    Require(error.empty(), "successful configuration clears its error");
    return cfg;
}
void TestParsingAndReload() {
    auto defaults = Read(Profile{});
    Require(!defaults.enabled && !defaults.cycle && defaults.entries.size() == 1,
        "first-run composition is disabled with one entry");
    Require(defaults.cycleSeconds == 10 && defaults.refreshSeconds == 2 && defaults.textMode == TextMode::Auto,
        "default timing and presentation remain stable");
    Profile profile;
    profile.Set(L"Content", L"Enabled", L"YES");
    profile.Set(L"Content", L"Count", L"2");
    profile.Set(L"Content", L"TextMode", L"overlay");
    profile.Set(L"Content.1", L"TextSources", L"customtext, smtc");
    profile.Set(L"Content.1", L"Text", L"preserved custom text");
    profile.Set(L"Content.2", L"Enabled", L"false");
    profile.Set(L"Content.2", L"Background", L"Image");
    auto good = Read(profile);
    Require(good.enabled && good.textMode == TextMode::Overlay && !good.entries[1].enabled,
        "disabled image entry may be staged without an image path");
    Require(good.entries[0].sources == std::vector<std::wstring>({L"CustomText", L"SMTC"}),
        "provider names are canonicalized in declared layering order");

    const std::vector<std::pair<std::wstring, std::wstring>> badRoot = {
        {L"Enabled", L"maybe"}, {L"Count", L"0"}, {L"Count", L"33"},
        {L"CycleSeconds", L"0"}, {L"CycleSeconds", L"86401"},
        {L"RefreshSeconds", L"-1"}, {L"RefreshSeconds", L"1.5"},
        {L"RefreshSeconds", L"184467440737095516160"}, {L"TextMode", L"LiveTileOnly"}
    };
    for (const auto& bad : badRoot) {
        Profile invalid = profile; invalid.Set(L"Content", bad.first, bad.second);
        auto retained = good; std::wstring error;
        Require(!ReadConfiguration(invalid, retained, error), "invalid root bound/type must reject reload");
        Require(!error.empty() && Same(retained, good), "failed reload is transactional across every field");
    }
    for (const auto& invalidSources : {L"CustomText,customtext", L"SMTC,Unknown", L",SMTC", L"SMTC,", L"RSSFeed,,CapsLock"}) {
        Profile invalid = profile; invalid.Set(L"Content.1", L"TextSources", invalidSources);
        auto retained = good; std::wstring error;
        Require(!ReadConfiguration(invalid, retained, error) && Same(retained, good),
            "duplicate, unknown and empty provider names reject without replacing a good profile");
    }
    Profile missingImage = profile; missingImage.Set(L"Content.2", L"Enabled", L"true");
    auto retained = good; std::wstring error;
    Require(!ReadConfiguration(missingImage, retained, error) && Same(retained, good),
        "enabling an unfinished image entry fails transactionally");
    Require(error.find(L"Content.2.ImagePath") != std::wstring::npos, "missing image diagnostics identify the entry");
    Profile maximum;
    maximum.Set(L"Content", L"Count", L"32");
    maximum.Set(L"Content", L"CycleSeconds", L"86400");
    maximum.Set(L"Content", L"RefreshSeconds", L"86400");
    auto full = Read(maximum);
    Require(full.entries.size() == 32 && full.cycleSeconds == 86400 && full.refreshSeconds == 86400,
        "inclusive maximum bounds are accepted");
    for (const auto& section : {L"Content.0", L"Content.33", L"Content.01", L"Content.x"}) {
        std::wstring value = L"1";
        Require(!NormalizeSetting(section, L"Enabled", value, error), "invalid entry section cannot be set by CLI");
    }
    std::wstring tooLong(MaxText + 1, L'x');
    Require(!NormalizeSetting(L"Content.1", L"Text", tooLong, error), "oversized configured text is rejected");
    std::wstring nulText(L"a\0b", 3);
    Require(!NormalizeSetting(L"Content.1", L"Text", nulText, error), "embedded NUL cannot be configured");
}
void TestSettingParity() {
    struct Setting { const wchar_t* section; const wchar_t* key; const wchar_t* value; const wchar_t* canonical; };
    const Setting settings[] = {
        {L"CapsBlink", L"HardwareEnabled", L"false", L"0"},
        {L"CapsBlink", L"KeyboardTargetPath", L"\\device\\keyboardclass42", L"\\Device\\KeyboardClass42"},
        {L"CapsBlink", L"BlinkIntervalMs", L" 0050 ", L"50"},
        {L"AsusBlink", L"HardwareEnabled", L"yes", L"1"},
        {L"AsusBlink", L"ReadDiskActivity", L"OFF", L"0"},
        {L"AsusBlink", L"Paused", L"on", L"1"},
        {L"AsusBlink", L"MicState", L"0,1", L"0,1"},
        {L"AsusBlink", L"MicInterval", L"1.5s", L"1.5s"},
        {L"Content", L"Enabled", L" ON ", L"1"}, {L"Content", L"CycleEnabled", L" no ", L"0"},
        {L"Content", L"Count", L" 02 ", L"2"}, {L"Content", L"CycleSeconds", L"00012", L"12"},
        {L"Content", L"RefreshSeconds", L" 5 ", L"5"}, {L"Content", L"TextMode", L" oVeRlAy ", L"Overlay"},
        {L"Content.1", L"Enabled", L"disabled", L"0"},
        {L"Content.1", L"Background", L" liveWALLpaper ", L"LiveWallpaper"},
        {L"Content.1", L"TextSources", L" capslock,SMTC , rssfeed, customtext ", L"CapsLock,SMTC,RssFeed,CustomText"},
        {L"Content.1", L"Name", L"  My tile  ", L"  My tile  "},
        {L"Content.1", L"ImagePath", L"C:\\my images\\img0.jpg", L"C:\\my images\\img0.jpg"},
        {L"Content.1", L"Text", L" first ", L" first "},
        {L"Content.1", L"SecondaryText", L"second", L"second"}, {L"Content.1", L"BadgeText", L"7", L"7"}
    };
    for (const auto& setting : settings) {
        std::wstring cliValue = setting.value, error;
        Require(NormalizeSetting(setting.section, setting.key, cliValue, error), "valid CLI setting normalizes");
        Require(cliValue == setting.canonical, "CLI setting uses expected canonical representation");
        Profile ini, cli;
        ini.Set(setting.section, setting.key, setting.value);
        cli.Set(setting.section, setting.key, cliValue);
        Require(Same(Read(ini), Read(cli)), "direct INI and normalized CLI setting produce the same configuration");
    }
}
Configuration Cycling() {
    Configuration cfg; cfg.enabled = true; cfg.cycle = true; cfg.cycleSeconds = 10;
    cfg.entries.resize(4); cfg.entries[1].enabled = false; return cfg;
}
void TestCycle() {
    auto cfg = Cycling(); Cycle cycle;
    Require(cycle.Select(cfg, 100) == 0 && cycle.Select(cfg, 10099) == 0, "first frame holds for a complete interval");
    Require(cycle.Select(cfg, 10100) == 2, "exact boundary advances past disabled entries");
    Require(cycle.Select(cfg, 1000000) == 3 && cycle.Select(cfg, 1000000) == 3,
        "long suspension advances once and does not replay missed cycles");
    Require(cycle.Select(cfg, 1010000) == 0, "cycle wraps to first enabled entry");
    cfg.cycle = false;
    Require(cycle.Select(cfg, 1010001) == 0 && cycle.Select(cfg, 2000000) == 0, "paused cycling remains on the first content");
    cfg.cycle = true;
    Require(cycle.Select(cfg, 2000000) == 0 && cycle.Select(cfg, 2009999) == 0 && cycle.Select(cfg, 2010000) == 2,
        "resumed cycling starts a fresh full interval");
    cfg.entries[0].enabled = false;
    Require(cycle.Select(cfg, 2010001) == 2, "editing the enabled set restarts on its first entry");
    for (auto& entry : cfg.entries) entry.enabled = false;
    Require(cycle.Select(cfg, 2010002) == std::numeric_limits<std::size_t>::max(), "all-disabled configuration selects no entry");
    cfg.entries[3].enabled = true;
    Require(cycle.Select(cfg, 2010003) == 3 && cycle.Select(cfg, 5000000) == 3, "single enabled entry never cycles away");
    cfg = Cycling(); Cycle rollover;
    const auto end = std::numeric_limits<std::uint64_t>::max();
    Require(rollover.Select(cfg, end - 5000) == 0 && rollover.Select(cfg, end) == 0,
        "clock values near uint64 maximum do not overflow interval comparison");
    Require(rollover.Select(cfg, 0) == 0 && rollover.Select(cfg, 9999) == 0 && rollover.Select(cfg, 10000) == 2,
        "unsigned clock rollover restarts timing without a catch-up burst");
    cfg.cycleSeconds = 86400; Cycle day;
    Require(day.Select(cfg, 0) == 0 && day.Select(cfg, 86399999) == 0 && day.Select(cfg, 86400000) == 2,
        "maximum interval uses milliseconds without overflow");
}
void TestTextAndWarnings() {
    auto hardwareDefaults = Read(Profile{});
    Require(!hardwareDefaults.caps.actionsEnabled && !hardwareDefaults.asus.hardwareEnabled && !hardwareDefaults.asus.readDiskActivity,
        "hardware and disk sampling require separate explicit settings");
    for (const auto& item : std::vector<std::pair<std::wstring, std::wstring>>{{L"KeyboardTargetPath", L"\\Device\\Harddisk0"}, {L"BlinkIntervalMs", L"49"}}) {
        auto bad = item.second; std::wstring issue;
        Require(!NormalizeSetting(L"CapsBlink", item.first, bad, issue), "invalid Caps settings use engine-compatible validation");
    }
    Profile hardwareProfile;
    hardwareProfile.Set(L"AsusBlink", L"MicState", L"2");
    auto unchanged = hardwareDefaults; std::wstring hardwareError;
    Require(!ReadConfiguration(hardwareProfile, unchanged, hardwareError) && Same(unchanged, hardwareDefaults),
        "invalid ASUS state cannot partially replace the prior complete configuration");
    Profile discord;
    discord.Set(L"Content.1", L"TextSources", L"DiscordRPC");
    discord.Set(L"Discord", L"Details", L"A preview");
    auto discordConfig = Read(discord);
    Require(!discordConfig.discord.send && discordConfig.discord.details == L"A preview", "Discord defaults to configurable preview without sending");
    std::wstring badClient = L"invalid", discordError;
    Require(!NormalizeSetting(L"Discord", L"ClientId", badClient, discordError), "Discord sender ID is typed consistently");
    Profile notes;
    notes.Set(L"Content.1", L"TextSources", L"Notes");
    notes.Set(L"Content.1", L"NotesResource", L"charge");
    notes.Set(L"Notes.Charge", L"UID", L"10000001");
    notes.Set(L"Notes.Charge", L"LToken", L"fixture-only");
    auto notesConfig = Read(notes);
    Require(notesConfig.entries[0].notesResource == 2 && notesConfig.notes.accounts[2].uid == L"10000001", "notes game/account selection remains distinct");
    std::wstring invalidCookie = L"fixture;other=value", notesError;
    Require(!NormalizeSetting(L"Notes.Resin", L"LToken", invalidCookie, notesError) && notesError.find(L"fixture") == std::wstring::npos,
        "invalid cookie fields fail without echoing credential text");
    Require(SensitiveSetting(L"Notes.Resin", L"LToken") && SensitiveSetting(L"Notes.Charge", L"LTUid"), "cookie settings are redacted on every game");
    Profile headline;
    headline.Set(L"Content.1", L"RssItem", L"20");
    Require(Read(headline).entries[0].rssItem == 20, "per-content RSS item is independent of source position");
    std::wstring badItem = L"21", itemError;
    Require(!NormalizeSetting(L"Content.1", L"RssItem", badItem, itemError), "RSS item upper bound is validated");
    auto before = Cycling(), after = before;
    after.entries[0].custom.primary = L"New title";
    Require(TimingChangeWarning(before, after, false).empty(), "text edits do not repeatedly ask to approve unchanged cycle risk");
    after.cycleSeconds = 1;
    Require(!TimingChangeWarning(before, after, false).empty(), "faster registration cycling still warns");
    before.enabled = false;
    Require(!TimingChangeWarning(before, after, false).empty(), "enabling risky cycling warns");
    Text result{L"Background", L"", L"1"}; Overlay(result, Text{L"RSS", L"Status", L"2"});
    Require(result.primary == L"Background\nRSS" && result.secondary == L"Status" && result.badge == L"1\n2",
        "text overlays preserve field mapping and source order");
    std::wstring empty; AppendBounded(empty, L"");
    Require(empty.empty(), "empty provider text introduces no separator");
    std::wstring full(MaxText, L'x'); AppendBounded(full, L"ignored");
    Require(full == std::wstring(MaxText, L'x'), "already-full output remains unchanged");
    std::wstring pair; pair += static_cast<wchar_t>(0xD83D); pair += static_cast<wchar_t>(0xDE80);
    std::wstring clipped; AppendBounded(clipped, std::wstring(MaxText - 1, L'x') + pair);
    Require(clipped.size() == MaxText - 1 && clipped.back() == L'x', "UTF-16 clipping never leaves a dangling high surrogate");
    std::wstring exact; AppendBounded(exact, std::wstring(MaxText - 2, L'x') + pair);
    Require(exact.size() == MaxText && exact.substr(MaxText - 2) == pair, "complete UTF-16 pair fits at exact capacity");
    std::wstring layered(MaxText - 2, L'x'); AppendBounded(layered, pair);
    Require(layered.size() <= MaxText && layered.back() != static_cast<wchar_t>(0xD83D), "separator-aware clipping also preserves UTF-16 boundaries");
    if constexpr (sizeof(wchar_t) > 2) {
        std::wstring utf32; AppendBounded(utf32, std::wstring(MaxText - 1, L'x') + static_cast<wchar_t>(0x1F680));
        Require(utf32.size() == MaxText && utf32.back() == static_cast<wchar_t>(0x1F680), "UTF-32 platform preserves a complete non-BMP character");
    }
    auto cfg = Cycling();
    Require(!TimingWarning(cfg, false).empty(), "registration cycling warns about its rendering/registration cost");
    cfg.cycleSeconds = 9;
    Require(!TimingWarning(cfg, true).empty() && TimingWarning(cfg, true).find(L"not a guaranteed") != std::wstring::npos,
        "fast Live Tile warning clearly identifies its advisory threshold");
    cfg.cycleSeconds = 10; Require(TimingWarning(cfg, true).empty(), "ten-second native cycle does not trigger fast-cycle warning");
    cfg.cycle = false; Require(TimingWarning(cfg, false).empty(), "disabled cycling does not warn");
    cfg.cycle = true; for (auto& entry : cfg.entries) entry.enabled = false; cfg.entries[0].enabled = true;
    Require(TimingWarning(cfg, false).empty(), "one active entry does not warn about nonexistent cycling");
}
void TestRandomSchedule() {
    auto cfg = Cycling(); cfg.entries.resize(MaxEntries);
    Cycle cycle; std::uint64_t now = 1234, random = 0x38a7f93b;
    std::size_t last = cycle.Select(cfg, now);
    for (int i = 0; i < 100000; ++i) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        if (i % 37 == 0) cfg.entries[static_cast<std::size_t>(random % MaxEntries)].enabled = (random & 256) != 0;
        if (i % 53 == 0) cfg.cycle = (random & 512) != 0;
        if (i % 71 == 0) cfg.cycleSeconds = static_cast<int>(random % 86400) + 1;
        now += random % 100000000;
        const auto selected = cycle.Select(cfg, now);
        const auto first = std::find_if(cfg.entries.begin(), cfg.entries.end(), [](const Entry& e) { return e.enabled; });
        if (first == cfg.entries.end()) {
            Require(selected == std::numeric_limits<std::size_t>::max(), "stress: no selection when every entry is disabled");
        } else {
            Require(selected < cfg.entries.size() && cfg.entries[selected].enabled, "stress: selected entry is in bounds and enabled");
            if (!cfg.cycle) Require(selected == static_cast<std::size_t>(first - cfg.entries.begin()), "stress: paused engine selects first enabled entry");
        }
        Require(cycle.Select(cfg, now) == selected, "stress: repeated poll cannot consume another pending cycle");
        if (i % 37 != 0 && i % 53 != 0 && i % 71 != 0 && selected != last && last < cfg.entries.size()) {
            auto expected = (last + 1) % cfg.entries.size();
            while (!cfg.entries[expected].enabled) expected = (expected + 1) % cfg.entries.size();
            Require(selected == expected, "stress: a late poll advances exactly one enabled entry");
        }
        last = selected;
    }
}
}
int main() {
    try {
        TestParsingAndReload(); TestSettingParity(); TestCycle(); TestTextAndWarnings(); TestRandomSchedule();
        std::cout << "Content engine tests passed (" << checks << " assertions; 100000 randomized schedule iterations).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Content engine test failed after " << checks << " assertions: " << error.what() << "\n";
        return 1;
    }
}

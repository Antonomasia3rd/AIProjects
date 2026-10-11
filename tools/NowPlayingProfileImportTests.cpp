#define DESKTOPSTUB_INERT_HARDWARE
#define AIP_ASUS_NO_NATIVE_BACKEND
#include "../dependencies/content_sources/now_playing_profile.inc"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

// No SMTC, artwork, Startup, registry, package or device implementation is
// included. Only synthetic text/bytes and a newly created temporary folder
// are used. The internal deadline is shorter than the runner's 60-second bound.
namespace {
int checks = 0;
void Check(bool condition, const char* label) {
    if (!condition) throw std::runtime_error(label);
    ++checks; std::cout << "ok - " << label << '\n';
}
using aip::now_playing_profile::ImportPlan;
using aip::now_playing_profile::ImportProfileText;
using aip::now_playing_profile::ImportProfileFile;
using aip::now_playing_profile::Layout;
std::wstring Value(const std::vector<aip::IniSetting>& batch, const wchar_t* section, const wchar_t* key,
    const wchar_t* fallback = L"") {
    for (auto setting = batch.rbegin(); setting != batch.rend(); ++setting)
        if (aip::IniEquals(setting->section, section) && aip::IniEquals(setting->key, key)) return setting->value;
    return fallback;
}
bool Has(const std::vector<aip::IniSetting>& batch, const wchar_t* section, const wchar_t* key) {
    for (const auto& setting : batch)
        if (aip::IniEquals(setting.section, section) && aip::IniEquals(setting.key, key)) return true;
    return false;
}
bool Diagnostic(const ImportPlan& plan, const wchar_t* fragment) {
    for (const auto& issue : plan.diagnostics) if (issue.find(fragment) != std::wstring::npos) return true;
    return false;
}
aip::content::Configuration Read(const std::vector<aip::IniSetting>& batch) {
    aip::content::Configuration result; std::wstring error;
    Check(aip::content::ReadConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        return Value(batch, section, key, fallback);
    }, result, error) && error.empty(), "every projected content batch passes the existing host model parser");
    return result;
}
void Layouts() {
    ImportPlan plan; std::wstring error;
    for (const auto* name : {L"Text", L"Artwork", L"Combined", L"Cycle"}) {
        Check(ImportProfileText(L"[Settings]\nTileLayout=" + std::wstring(name) + L"\n", plan, error) && error.empty(),
            "each native donor layout imports successfully");
        const auto cfg = Read(plan.settings);
        const bool cycle = std::wstring(name) == L"Cycle";
        Check(cfg.enabled && cfg.cycle == cycle && cfg.entries.size() == (cycle ? 3u : 1u) && cfg.refreshSeconds == 2,
            "layout plan enables exactly its intended entries and preserves donor polling defaults");
        if (std::wstring(name) == L"Text") {
            Check(cfg.textMode == aip::content::TextMode::Auto && cfg.entries[0].background == L"None" &&
                cfg.entries[0].sources == std::vector<std::wstring>{L"SMTC"}, "Text maps to SMTC text over a solid background with Auto text");
        } else if (std::wstring(name) == L"Artwork") {
            Check(cfg.textMode == aip::content::TextMode::Auto && cfg.entries[0].background == L"SMTCArtwork" &&
                cfg.entries[0].sources.empty(), "Artwork maps to SMTC artwork with Auto available for the host's missing-art text fallback");
        } else if (std::wstring(name) == L"Combined") {
            Check(cfg.textMode == aip::content::TextMode::Overlay && cfg.entries[0].background == L"SMTCArtwork" &&
                cfg.entries[0].sources == std::vector<std::wstring>{L"SMTC"}, "Combined maps to SMTC text over SMTC artwork using Overlay");
        } else {
            Check(cfg.textMode == aip::content::TextMode::Overlay && cfg.entries[0].background == L"None" &&
                cfg.entries[1].background == L"SMTCArtwork" && cfg.entries[2].background == L"SMTCArtwork" &&
                cfg.entries[0].sources == std::vector<std::wstring>{L"SMTC"} && cfg.entries[1].sources.empty() &&
                cfg.entries[2].sources == std::vector<std::wstring>{L"SMTC"}, "Cycle preserves the Text, Artwork, Combined variant order");
            Check(Diagnostic(plan, L"Windows controlled") && Diagnostic(plan, L"TextMode is global"),
                "Cycle describes donor queue timing and the host's global text-renderer difference");
        }
        Check(!Has(plan.settings, L"Content", L"CycleSeconds") && Diagnostic(plan, L"not mapped to source refresh or cycle timing"),
            "donor delivery refresh never becomes a host cycle or source-refresh preference");
        Check(Value(plan.settings, L"Settings", L"TrayIcon") == L"1" && Value(plan.settings, L"Settings", L"RunAtStartup") == L"0" &&
            Value(plan.settings, L"Settings", L"ShowMenuAsDropdown") == L"1" && !Has(plan.settings, L"Settings", L"RunAtStartupPackaged"),
            "donor defaults project to the three native host preference equivalents");
    }
}
void DialectAndPreferences() {
    ImportPlan plan; std::wstring error;
    const std::wstring text = L"\xfeff[\"sEtTiNgS\"] ; donor\nTile-Layout='cOmBiNeD' # chosen\n"
        L"UPDATE_INTERVAL_SECONDS=+007\nTile_Refresh_Seconds=00300\nShow-Tray-Icon=off\n"
        L"Run-At-Startup=enabled\nSHOW_MENU_AS_DROPDOWN=no\nWidgetFont=Comic Sans MS\nWidgetColor=#123456\n"
        L"[Widget]\nWidth=500\nArtworkPath='D:\\synthetic\\image.jpg'\n";
    Check(ImportProfileText(text, plan, error), "native quote/comment/BOM dialect and separator aliases import case-insensitively");
    Check(plan.snapshot.layout == Layout::Combined && plan.snapshot.updateIntervalSeconds == 7 && plan.snapshot.tileRefreshSeconds == 300 &&
        !plan.snapshot.showTrayIcon && plan.snapshot.runAtStartup && !plan.snapshot.showMenuAsDropdown,
        "typed snapshot preserves all six normalized donor preferences");
    Check(Value(plan.settings, L"Content", L"RefreshSeconds") == L"7" && Value(plan.settings, L"Settings", L"TrayIcon") == L"0" &&
        Value(plan.settings, L"Settings", L"RunAtStartup") == L"1" && Value(plan.settings, L"Settings", L"ShowMenuAsDropdown") == L"0",
        "polling, tray, native Startup and menu preferences have explicit host mappings");
    Check(plan.snapshot.unsupportedSettings.size() == 4 && Diagnostic(plan, L"WidgetFont") && Diagnostic(plan, L"WidgetColor") &&
        Diagnostic(plan, L"[Widget]") && Diagnostic(plan, L"normal Startup transaction") &&
        !Has(plan.settings, L"Content.1", L"ImagePathFromWidget") && Value(plan.settings, L"Content.1", L"ImagePath").empty(),
        "unsupported widget preferences remain in the snapshot with diagnostics and create no invented host fields");
    Check(ImportProfileText(L"[Settings]\nTileLayout=Artwork\n[SETTINGS]\nTILELAYOUT=Text\nUpdateIntervalSeconds=8\n", plan, error) &&
        plan.snapshot.layout == Layout::Text && plan.snapshot.updateIntervalSeconds == 8,
        "shared parser repeated-section and repeated-key effective values remain intact");
    Check(ImportProfileText(L"[Settings]\nTileLayout=combined\nTile-Layout=COMBINED\n", plan, error) && plan.snapshot.layout == Layout::Combined,
        "equivalent semantic aliases agree after canonical normalization");
    Check(ImportProfileText(L"[Settings]\n", plan, error) && plan.snapshot.layout == Layout::Text && plan.settings.size() == 16,
        "an empty native settings section imports donor defaults as one complete typed batch");
}
void Rejection() {
    ImportPlan plan; std::wstring error;
    const std::vector<std::wstring> invalid = {
        L"", L"[Other]\nTileLayout=Combined\n", L"[Settings\nTileLayout=Text\n", L"[]\nTileLayout=Text\n",
        L"TileLayout=Text\n[Settings]\n", L"[Settings]\nRunAtStartup\n", L"[Settings]\n=Combined\n",
        L"[Settings]\nTileLayout=unknown\n", L"[Settings]\nUpdateIntervalSeconds=0\n",
        L"[Settings]\nUpdateIntervalSeconds=61\n", L"[Settings]\nTileRefreshSeconds=301\n",
        L"[Settings]\nTileRefreshSeconds=2x\n", L"[Settings]\nShowTrayIcon=maybe\n",
        L"[Settings]\nRunAtStartup=perhaps\n", L"[Settings]\nShowMenuAsDropdown=2\n",
        L"[Settings]\nTileLayout=Text\nTile-Layout=Artwork\n",
        L"[Settings]\nUpdateIntervalSeconds=2\nUpdate_Interval_Seconds=3\n",
        std::wstring(L"[Settings]\nWidgetText=") + wchar_t(0xd800),
        std::wstring(L"[Settings]\nWidgetText=") + wchar_t(0xdc00),
        std::wstring(L"[Settings]\nTileLayout=Text\n") + wchar_t(0)
    };
    for (const auto& text : invalid) {
        Check(ImportProfileText(L"[Settings]\nTileLayout=Cycle\nRunAtStartup=true\n", plan, error), "rejection fixture starts with a complete successful plan");
        Check(!ImportProfileText(text, plan, error) && !error.empty() && plan.settings.empty() && plan.diagnostics.empty() &&
            plan.snapshot.unsupportedSettings.empty(), "malformed profiles return no stale or partial output");
    }
}
void ConfigurationProjection() {
    ImportPlan plan; std::wstring error;
    aip::now_playing_profile::ConfigurationProjection projected;
    for (const auto* preference : {L"true", L"false"}) {
        Check(ImportProfileText(L"[Settings]\nTileLayout=Combined\nRunAtStartup=" + std::wstring(preference) + L"\n", plan, error) &&
            aip::now_playing_profile::ProjectConfigurationOnly(plan, projected, error),
            "configuration-only projection accepts either imported Startup preference");
        Check(!Has(projected.settings, L"Settings", L"RunAtStartup") &&
            !Has(projected.settings, L"Settings", L"RunAtStartupPackaged") &&
            projected.donorRunAtStartup == (std::wstring(preference) == L"true") &&
            Has(plan.settings, L"Settings", L"RunAtStartup"),
            "safe projection omits every Startup transaction trigger and keeps full plan metadata");
        bool visible = false;
        for (const auto& issue : projected.diagnostics)
            if (issue.find(L"Donor RunAtStartup=" + std::wstring(preference)) != std::wstring::npos && issue.find(L"deferred") != std::wstring::npos) visible = true;
        Check(visible && Value(projected.settings, L"Content", L"TextMode") == L"Overlay" &&
            Value(projected.settings, L"Settings", L"TrayIcon") == L"1", "deferred Startup choice is explicit while content and tray preferences still project");
        Read(projected.settings);
    }
    Check(!aip::now_playing_profile::ProjectConfigurationOnly({}, projected, error) && !error.empty() &&
        projected.settings.empty() && projected.diagnostics.empty(), "a missing successful plan cannot return a stale configuration projection");
    std::wstring destination = L"; existing host\r\n[Settings]\r\nRunAtStartup=1\r\nKeep=unchanged\r\n"
        L"[Content]\r\nCount=4\r\nCycleSeconds=17\r\n[Content.1]\r\nText=obsolete\r\n"
        L"[CONTENT.4]\r\nBackground=Image\r\nImagePath=stale.png\r\n[Content.01]\r\nText=legacy\r\n"
        L"[Notes.Resin]\r\nUID=123456789\r\n[Unrelated]\r\nImagePath=keep.png\r\n";
    Check(aip::now_playing_profile::RemoveContentEntriesFromText(destination) && destination.find(L"obsolete") == std::wstring::npos &&
        destination.find(L"stale.png") == std::wstring::npos && destination.find(L"legacy") == std::wstring::npos &&
        destination.find(L"UID=123456789") != std::wstring::npos && destination.find(L"ImagePath=keep.png") != std::wstring::npos,
        "entry replacement removes old content sections and preserves unrelated native sections");
    Check(ImportProfileText(L"[Settings]\nTileLayout=Artwork\nRunAtStartup=false\n", plan, error) &&
        aip::now_playing_profile::ProjectConfigurationOnly(plan, projected, error), "replacement fixture builds its startup-free imported batch");
    for (const auto& setting : projected.settings)
        Check(aip::WriteIniValueToText(destination, setting.section.c_str(), setting.key.c_str(), setting.value),
            "replacement candidate uses the native text writer");
    const auto document = aip::ParseIniDocument(destination);
    std::wstring startup, cadence;
    Check(aip::ReadIniValueFromDoc(document, L"Settings", L"RunAtStartup", startup) && startup == L"1" &&
        aip::ReadIniValueFromDoc(document, L"Content", L"CycleSeconds", cadence) && cadence == L"17" &&
        !aip::IniHasKeyInDoc(document, L"Content.4", L"ImagePath") &&
        !aip::IniHasKeyInDoc(document, L"Content.01", L"Text"),
        "prospective replacement retains current host Startup and cycle cadence without dormant old entries");
}
struct Fixture {
    std::filesystem::path directory;
    Fixture() {
        const auto parent = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            directory = parent / (L"AIProjects-NowPlayingProfileImport-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
            if (std::filesystem::create_directory(directory)) return;
        }
        throw std::runtime_error("Could not create a unique fixture directory.");
    }
    ~Fixture() {
        std::error_code error;
        if (directory.parent_path() == std::filesystem::temp_directory_path()) std::filesystem::remove_all(directory, error);
    }
};
std::vector<BYTE> Bytes(const std::wstring& path) {
    std::vector<BYTE> bytes;
    if (!aip::ReadWholeFileBytes(path, bytes, aip::kMaxIniFileBytes + 2)) throw std::runtime_error("Could not read synthetic bytes.");
    return bytes;
}
void WriteBytes(const std::wstring& path, const std::vector<BYTE>& bytes) {
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not create synthetic bytes.");
    DWORD written = 0;
    const auto size = static_cast<DWORD>(bytes.size());
    const bool saved = WriteFile(file, bytes.data(), size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (!saved) throw std::runtime_error("Could not save synthetic bytes.");
}
void FilesAndExplicitOverrides() {
    Fixture fixture; ImportPlan plan; std::wstring error;
    const auto path = (fixture.directory / L"donor.ini").wstring();
    const std::wstring text = L"# synthetic donor\r\n[Settings]\r\nTileLayout=Cycle\r\nUpdateIntervalSeconds=6\r\n"
        L"TileRefreshSeconds=240\r\nRunAtStartup=true\r\nWidgetTitle=音楽\r\n";
    Check(aip::WriteTextFileUtf8Bom(path, text), "synthetic donor file uses the shared native UTF-8 writer");
    const auto original = Bytes(path);
    Check(ImportProfileFile(path, plan, error) && Bytes(path) == original && plan.snapshot.tileRefreshSeconds == 240,
        "explicit file import produces the typed snapshot without changing source bytes");
    Check(plan.snapshot.unsupportedSettings.size() == 1 && plan.snapshot.unsupportedSettings[0].value == L"音楽",
        "native decoding preserves unsupported Unicode preferences in the snapshot");
    // The host must fold its normalized explicit arguments after this imported
    // batch. ReadConfiguration's reader models that ordering without invoking
    // a host command, writing a host profile, or creating a Startup shortcut.
    auto overridden = plan.settings;
    overridden.push_back({L"CONTENT", L"RefreshSeconds", L"11"});
    overridden.push_back({L"Content", L"CycleSeconds", L"19"});
    overridden.push_back({L"Content", L"TextMode", L"Auto"});
    overridden.push_back({L"Settings", L"RunAtStartup", L"0"});
    const auto cfg = Read(overridden);
    Check(cfg.refreshSeconds == 11 && cfg.cycleSeconds == 19 && cfg.textMode == aip::content::TextMode::Auto &&
        Value(overridden, L"Settings", L"RunAtStartup") == L"0" && plan.snapshot.runAtStartup &&
        Value(plan.settings, L"Content", L"RefreshSeconds") == L"6" && Bytes(path) == original,
        "later explicit host overrides win while the imported plan and donor source stay unchanged");
    std::vector<BYTE> utf16{0xff, 0xfe};
    for (const auto ch : text) { utf16.push_back(static_cast<BYTE>(ch & 0xff)); utf16.push_back(static_cast<BYTE>(ch >> 8)); }
    WriteBytes(path, utf16);
    Check(ImportProfileFile(path, plan, error) && Bytes(path) == utf16 && plan.snapshot.updateIntervalSeconds == 6,
        "shared native BOM-marked UTF-16 decoding keeps the source unchanged");
    for (const auto& broken : std::vector<std::vector<BYTE>>{{0xef, 0xbb, 0xbf, 0xc0, 0xaf}, {0xff, 0xfe, 0x41},
        {0xff, 0xfe, 0x00, 0xd8}, {0xff, 0xfe, 0x00, 0x00}, std::vector<BYTE>(aip::kMaxIniFileBytes + 1, 'x')}) {
        WriteBytes(path, broken);
        plan.settings.push_back({L"Settings", L"RunAtStartup", L"1"});
        Check(!ImportProfileFile(path, plan, error) && !error.empty() && plan.settings.empty() && plan.diagnostics.empty() && Bytes(path) == broken,
            "bad encoding or oversized file returns no partial output and never repairs the source");
    }
    plan.settings.push_back({L"Content", L"Enabled", L"1"});
    Check(!ImportProfileFile((fixture.directory / L"missing.ini").wstring(), plan, error) && !error.empty() && plan.settings.empty(),
        "a missing source file fails without returning a stale import plan");
}
}
int main() {
    std::atomic<bool> finished{false};
    std::thread deadline([&] {
        for (int tick = 0; tick < 450 && !finished; ++tick) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!finished) { std::cerr << "NowPlayingTile import fixture exceeded 45 seconds.\n"; std::_Exit(124); }
    });
    int status = 0;
    try { Layouts(); DialectAndPreferences(); Rejection(); ConfigurationProjection(); FilesAndExplicitOverrides(); }
    catch (const std::exception& ex) { std::cerr << "FAIL: " << ex.what() << '\n'; status = 1; }
    finished = true; deadline.join();
    if (!status) std::cout << "NowPlayingTile profile import: " << checks << " inert checks passed.\n";
    return status;
}

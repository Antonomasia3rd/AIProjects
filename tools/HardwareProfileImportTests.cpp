#define DESKTOPSTUB_INERT_HARDWARE
#define AIP_ASUS_NO_NATIVE_BACKEND
#include "../dependencies/hardware/hardware_profile_import.inc"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

// This fixture does not link ASUS service/native_backend or Caps Windows I/O.
// It uses pure engine parsers and files inside one new temporary directory.
namespace {
int checks = 0;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    ++checks; std::cout << "ok - " << message << '\n';
}
struct Fixture {
    std::filesystem::path path;
    Fixture() {
        const auto parent = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = parent / (L"AIProjects-HardwareProfileImport-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("Could not create a new fixture directory.");
    }
    ~Fixture() {
        std::error_code error;
        if (path.parent_path() == std::filesystem::temp_directory_path()) std::filesystem::remove_all(path, error);
    }
};
using namespace aip::hardware_profile;
std::vector<BYTE> Bytes(const std::wstring& path) {
    std::vector<BYTE> bytes;
    if (!aip::ReadWholeFileBytes(path, bytes, aip::kMaxIniFileBytes + 1024)) throw std::runtime_error("Fixture bytes could not be read.");
    return bytes;
}
void Caps() {
    CapsProfile result; std::wstring error;
    Check(ParseCapsProfileText(L"[Settings]\nKeyboardTargetPath=\\Device\\KeyboardClass9\nBlinkIntervalMs=+0500\n" +
        std::wstring(L"Run-At-Startup=yes\nShow-Tray=off\nShow-Menu-As-Dropdown=0\n"), result, error), "Caps donor aliases and integer spelling import");
    Check(result.configuration.target == L"\\Device\\KeyboardClass9" && result.configuration.intervalMs == 500 &&
        !result.configuration.actionsEnabled, "Caps typed result preserves device/interval while hardware actions remain disabled");
    auto mapped = ToHostSettings(result);
    Check(mapped == Settings{{L"CapsBlink.KeyboardTargetPath", L"\\Device\\KeyboardClass9"}, {L"CapsBlink.BlinkIntervalMs", L"500"}} &&
        result.ignoredPreferences.at(L"RunAtStartup") == L"true" && result.ignoredPreferences.at(L"ShowTrayIcon") == L"false",
        "Caps host projection contains exactly native host keys and excludes donor integration preferences");
    Check(ParseCapsProfileText(L"[\"Settings\"] ; header\nKeyboardTargetPath=\"\\Device\\KeyboardClass01\" ; target\n" +
        std::wstring(L"blink-interval-ms=050\nBlinkIntervalMs=100\n[settings]\nBlinkIntervalMs=200\n"), result, error) &&
        result.configuration.target == L"\\Device\\KeyboardClass01" && result.configuration.intervalMs == 200,
        "Caps shared quote/comment dialect, repeated sections and effective alias ordering import");
    const auto before = ToHostSettings(result);
    for (const auto* invalid : {L"[Settings]\nKeyboardTargetPath=wrong\n", L"[Settings]\nBlinkIntervalMs=49\n",
        L"[Settings]\nBlinkIntervalMs=86400001\n", L"[Settings]\nBlinkIntervalMs=500x\n", L"[Settings]\nUnknown=1\n"})
        Check(!ParseCapsProfileText(invalid, result, error) && !error.empty() && ToHostSettings(result) == before,
            "invalid Caps replacement leaves the complete prior typed result untouched");
    Check(ParseCapsProfileText(L"[Other]\nRunAtStartup=true\n", result, error) && result.configuration.intervalMs == 500 &&
        !result.configuration.actionsEnabled, "Caps defaults are applied without reading unrelated sections or activating hardware");
}
void Asus() {
    AsusProfile result; std::wstring error;
    const std::wstring text = L"; source comment\n[Options]\nmic-state=0,1\nmic-interval=100,0.5S\n"
        L"keyboard-state=129\nerror-retry=2\nno-tray=\nerror-log=old.log\nrun-startup=1\n"
        L"[Settings]\nMic_State=off\nKeyboardDuration=2s\nErrorAction=LOG,Pause,log\nshow-tray=true\n";
    Check(ParseAsusProfileText(text, result, error), "ASUS modern aliases override legacy Options while unshadowed legacy values remain");
    Check(result.settings.at(L"MicState") == L"off" && result.settings.at(L"MicInterval") == L"100,0.5s" &&
        result.settings.at(L"KeyboardState") == L"129" && result.configuration.events.size() == 1 &&
        result.configuration.events[0].durationMs == 2000 && result.configuration.retries == 2 &&
        result.configuration.errorAction == aip::asus::ErrorAction::Pause, "ASUS typed configuration uses the native parser and canonical pattern values");
    Check(result.ignoredPreferences.at(L"ShowTrayIcon") == L"true" && result.ignoredPreferences.at(L"RunAtStartup") == L"true" &&
        result.ignoredPreferences.at(L"ErrorLog") == L"old.log", "modern tray preference masks legacy no-tray and other donor preferences remain informational");
    const auto mapped = ToHostSettings(result);
    Check(mapped.size() == 8 && mapped.count(L"AsusBlink.KeyboardState") && !mapped.count(L"AsusBlink.HardwareEnabled") &&
        !mapped.count(L"AsusBlink.ReadDiskActivity") && !mapped.count(L"AsusBlink.RunAtStartup") && !mapped.count(L"AsusBlink.ErrorLog"),
        "ASUS host projection imports pattern fields while omitting all activation, disk and integration flags");
    Check(ParseAsusProfileText(L"[Options]\nmic-state=bad\nno-tray=bad\nerror-retry=999\n[Settings]\nMicState=1\nShowTrayIcon=0\nErrorRetry=4\n", result, error) &&
        result.settings.at(L"MicState") == L"1" && result.configuration.retries == 4 && result.ignoredPreferences.at(L"ShowTrayIcon") == L"false",
        "valid modern settings mask obsolete invalid legacy aliases before validation");
    Check(ParseAsusProfileText(L"[Options]\nno-tray=\nShowTray=true\n", result, error) && result.ignoredPreferences.at(L"ShowTrayIcon") == L"true",
        "legacy explicit ShowTray wins over the present empty inverse no-tray flag");
    Check(ParseAsusProfileText(L"[Options]\nno-tray=\n", result, error) && result.ignoredPreferences.at(L"ShowTrayIcon") == L"false",
        "an empty legacy no-tray flag retains its original enabled meaning");
    Check(ParseAsusProfileText(L"[Options]\nmic-state=1\nMicState=0\n", result, error) && result.settings.at(L"MicState") == L"0",
        "legacy semantic aliases retain donor last-alias precedence");
    Check(ParseAsusProfileText(L"[Settings]\nMicState=0\n[settings]\nMICSTATE=1\n", result, error) && result.settings.at(L"MicState") == L"1",
        "repeated native section/key assignments have one effective value");
    const auto before = ToHostSettings(result);
    for (const auto* invalid : {L"[Settings]\nMicState=bad\n", L"[Settings]\nMicState=1\nMic_State=0\n",
        L"[Settings]\nEvent1000MicState=1\n", L"[Options]\nunknown-setting=1\n", L"[Settings]\nno-tray=true\n",
        L"[Settings]\nEvent2HddInterval=0.1ms\n", L"[Settings]\nEvent2MicState=2\n", L"[Settings]\nErrorAction=restart\n"})
        Check(!ParseAsusProfileText(invalid, result, error) && !error.empty() && ToHostSettings(result) == before,
            "invalid ASUS replacement preserves prior output rather than publishing a partial import");
    Check(ParseAsusProfileText(L"[Settings]\nEvent001MicState=1\nEvent1MicState=0\nEvent00HddState=128,129\n"
        L"Event00HddInterval=1s\nEvent00HddDuration=0\nEvent999KeyboardState=255\n", result, error),
        "advanced numbered mic, HDD and keyboard fields import");
    Check(result.settings.count(L"Event001MicState") && result.settings.count(L"Event1MicState") && result.settings.count(L"Event00HddState") &&
        result.configuration.events.size() == 4, "leading-zero event IDs remain distinct identities in maps and native configurations");
    const auto zero = std::find_if(result.configuration.events.begin(), result.configuration.events.end(), [](const aip::asus::Event& event) { return event.name == L"event00"; });
    Check(zero != result.configuration.events.end() && zero->priority == 0 && zero->hddActivity && zero->device == aip::asus::Device::Keyboard &&
        zero->intervalsMs == std::vector<std::uint64_t>{1000}, "numbered HDD metadata is preserved without querying any physical disk");
    aip::asus::PatternEngine engine;
    Check(engine.Reset(result.configuration, error), "imported typed ASUS configuration can initialize the pure pattern engine");
    Check(ToHostSettings(result).count(L"AsusBlink.Event001MicState") == 1, "advanced host map retains event number spelling for later integration");
}
void Files(const Fixture& fixture) {
    std::wstring error;
    const auto capsPath = (fixture.path / L"caps.ini").wstring();
    const auto asusPath = (fixture.path / L"asus.ini").wstring();
    Check(aip::WriteTextFileUtf8Bom(capsPath, L"; preserve source exactly\r\n[Settings]\r\n" +
        aip::FormatIniAssignment(L"KeyboardTargetPath", L"\\Device\\KeyboardClass2") + L"\r\nBlinkIntervalMs=750\r\n") &&
        aip::WriteTextFileUtf8Bom(asusPath, L"; keep comments\r\n[Options]\r\nmic-state=1\r\n[Settings]\r\nErrorRetry=7\r\n"),
        "synthetic donor files use the shared INI writer only inside the fixture directory");
    const auto capsBytes = Bytes(capsPath), asusBytes = Bytes(asusPath);
    CapsProfile caps; AsusProfile asus;
    Check(ReadCapsProfile(capsPath, caps, error) && ReadAsusProfile(asusPath, asus, error) &&
        caps.configuration.target == L"\\Device\\KeyboardClass2" && asus.configuration.retries == 7, "bounded native reader imports both synthetic donor files");
    Check(Bytes(capsPath) == capsBytes && Bytes(asusPath) == asusBytes && std::distance(std::filesystem::directory_iterator(fixture.path), std::filesystem::directory_iterator()) == 2,
        "import leaves source bytes, comments and directory contents exactly unchanged");
    const auto priorCaps = ToHostSettings(caps), priorAsus = ToHostSettings(asus);
    Check(!ReadCapsProfile((fixture.path / L"missing.ini").wstring(), caps, error) && ToHostSettings(caps) == priorCaps &&
        !ReadAsusProfile((fixture.path / L"missing.ini").wstring(), asus, error) && ToHostSettings(asus) == priorAsus &&
        !std::filesystem::exists(fixture.path / L"missing.ini"), "missing donor reads fail without creating files or replacing prior results");
    Check(aip::WriteTextFileUtf8Bom(asusPath, L"; invalid replacement\n[Settings]\nEvent001MicState=2\n"), "invalid synthetic replacement fixture writes successfully");
    const auto invalidBytes = Bytes(asusPath);
    Check(!ReadAsusProfile(asusPath, asus, error) && ToHostSettings(asus) == priorAsus && Bytes(asusPath) == invalidBytes,
        "invalid file replacement does not return partial output or alter the invalid source");
    const auto unicodePath = (fixture.path / L"unicode.ini").wstring();
    const auto unicodeText = L"; UTF-16 donor comment\r\n[Settings]\r\nMicState=1\r\nErrorLog=亚洲.log\r\n";
    {
        HANDLE file = CreateFileW(unicodePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("UTF-16 fixture file could not be created.");
        const wchar_t bom = 0xFEFF; DWORD written = 0;
        bool saved = WriteFile(file, &bom, sizeof(bom), &written, nullptr) != FALSE && written == sizeof(bom);
        const auto size = static_cast<DWORD>(wcslen(unicodeText) * sizeof(wchar_t));
        saved = saved && WriteFile(file, unicodeText, size, &written, nullptr) != FALSE && written == size;
        CloseHandle(file); Check(saved, "synthetic BOM-marked UTF-16 profile writes successfully");
    }
    const auto unicodeBytes = Bytes(unicodePath);
    Check(ReadAsusProfile(unicodePath, asus, error) && asus.ignoredPreferences.at(L"ErrorLog") == L"亚洲.log" && Bytes(unicodePath) == unicodeBytes,
        "existing native encoding support reads UTF-16 and leaves its bytes unchanged");
}
}
int main() {
    std::atomic<bool> finished{false};
    std::thread deadline([&] {
        for (int tick = 0; tick < 450 && !finished; ++tick) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!finished) { std::cerr << "Hardware import fixture exceeded 45 seconds.\n"; std::_Exit(124); }
    });
    int status = 0;
    try {
        Fixture fixture; Caps(); Asus(); Files(fixture);
        std::cout << "Hardware profile import checks passed: " << checks << '\n';
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; status = 1; }
    finished = true; deadline.join(); return status;
}

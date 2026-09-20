// Compile the product implementation, but NEVER invoke its entry point. These
// tests create only an isolated INI and invisible, untracked popup-menu objects.
#define wWinMain ADBControllerUncalledProductEntryPoint
#include "../../../dependencies/ADBController/adb_controller_app.inc"
#undef wWinMain
#include <iostream>
#include <stdexcept>

namespace {
int failures = 0;
void Check(bool condition, const char* name)
{
    std::cout << (condition ? "ok - " : "FAIL - ") << name << '\n';
    if (!condition) ++failures;
}
UINT MenuState(HMENU menu, UINT id)
{
    for (int index = 0; index < GetMenuItemCount(menu); ++index)
    {
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_ID | MIIM_STATE | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &item)) continue;
        if (item.hSubMenu)
        {
            const auto state = MenuState(item.hSubMenu, id);
            if (state != UINT_MAX) return state;
        }
        else if (item.wID == id) return item.fState;
    }
    return UINT_MAX;
}
void TestConfigurationAndMenus()
{
    ApplicationSettings settings;
    std::wstring error;
    Check(ParseApplicationSettings(L"[Settings]\nTheme=Auto\n", settings, error) &&
        !settings.runAtStartup && settings.showTrayIcon && settings.showMenuAsDropdown && settings.loggingEnabled,
        "ADB resident defaults keep Startup off and the foreground/tray available");
    CommandLineOptions options;
    Check(ParseCommandLine(options, error,
        L"ADBController.exe --startup --no-tray --menu-flat --no-logging --theme=dark --configure-only") &&
        options.settings.size() == 5 && options.configureOnly,
        "CLI aliases produce persistent canonical settings without running the application");
    Check(ParseApplicationSettings(L"[Settings]\nRunAtStartup=yes\nShowTrayIcon=off\n"
        L"ShowMenuAsDropdown=false\nLoggingEnabled=0\nTheme=Dark\n", settings, error) &&
        settings.runAtStartup && !settings.showTrayIcon && !settings.showMenuAsDropdown && !settings.loggingEnabled && settings.theme == ThemeMode::Dark,
        "INI booleans and appearance use the CLI validation rules");
    Check(!ParseApplicationSettings(L"[Settings]\nRunAtStartup=maybe\n", settings, error) &&
        !ParseApplicationSettings(L"[Settings]\nTheme=sepia\n", settings, error),
        "invalid persistent booleans and themes fail instead of silently using different values");
    CommandLineOptions invalid;
    Check(!ParseCommandLine(invalid, error, L"ADBController.exe --set Settings.RunAtStartup=maybe --configure-only"),
        "invalid generic Startup setting is rejected before any persistence");

    for (bool dropdown : {false, true})
    {
        g_settings = {};
        g_settings.showMenuAsDropdown = dropdown;
        g_settings.runAtStartup = true;
        g_settings.loggingEnabled = false;
        HMENU menu = BuildConfigurationMenu();
        bool mapped = menu != nullptr;
        for (UINT id : {IdRunAtStartup, IdShowTrayIcon, IdShowMenuAsDropdown, IdLoggingEnabled,
            IdChooseAdbPath, IdThemeAuto, IdThemeLight, IdThemeDark, IdAddDevice, IdEditDevice,
            IdRemoveDevice, IdEditConfiguration, IdReloadConfiguration, IdShowController, IdExit})
            mapped = mapped && MenuState(menu, id) != UINT_MAX;
        Check(mapped && (MenuState(menu, IdRunAtStartup) & MFS_CHECKED) &&
            !(MenuState(menu, IdLoggingEnabled) & MFS_CHECKED),
            dropdown ? "dropdown menu exposes every configuration command and current checks"
                : "flat menu exposes every configuration command and current checks");
        if (menu) DestroyMenu(menu);
    }
    g_busy = true;
    HMENU busyMenu = BuildConfigurationMenu();
    Check((MenuState(busyMenu, IdRunAtStartup) & MFS_DISABLED) && (MenuState(busyMenu, IdReloadConfiguration) & MFS_DISABLED),
        "menu configuration mutation is disabled while a copied ADB request is active");
    if (busyMenu) DestroyMenu(busyMenu);
    Check(TrayTooltip().find(L"Running ADB command") != std::wstring::npos,
        "tray tooltip includes product and current busy state");
    Check(WindowProcedure(nullptr, kMessageActivateExisting, static_cast<WPARAM>(InstanceRequest::Reload), 0) == aip::INSTANCE_REQUEST_HANDLED && g_reloadRequested,
        "a running command defers and acknowledges an external configuration reload");
    g_busy = false;
    g_reloadRequested = false;
    Check(TrayActivationFor(MAKELPARAM(NIN_KEYSELECT, 1)) == TrayActivation::Menu &&
        TrayActivationFor(MAKELPARAM(WM_CONTEXTMENU, 1)) == TrayActivation::Menu &&
        TrayActivationFor(MAKELPARAM(NIN_SELECT, 1)) == TrayActivation::Controller,
        "version-4 mouse and keyboard notifications map to menu or foreground activation");
    Check(!TrayTooltip().empty() && TrayTooltip().size() <= 127,
        "idle tray tooltip is visible and respects the shared shell limit");
}

struct FakeStartup {
    bool installed = false, failLaunch = false, failSave = false;
    std::vector<std::wstring> operations;
    bool Commit(bool desired, const SettingsMutator& mutate, std::wstring& error)
    {
        aip::IniTextSnapshot previous;
        if (!aip::CaptureIniTextSnapshot(g_config->Path(), previous)) return false;
        std::wstring prospective = previous.text;
        if (!mutate(prospective)) { error = L"Invalid prospective profile"; return false; }
        bool previousConfigured = false;
        if (!aip::ReadIniBooleanFromText(previous.text, L"Settings", L"RunAtStartup", false, previousConfigured)) return false;
        return aip::ExecuteCrashConsistentLaunchConfigState(desired, previousConfigured,
            [&](bool enabled, std::wstring& failure) {
                operations.push_back(enabled ? L"launch-on" : L"launch-off");
                if (failLaunch && enabled) { failure = L"Fake Startup failure"; return false; }
                installed = enabled;
                return true;
            },
            [&](std::wstring& failure) {
                operations.push_back(desired ? L"save-on" : L"save-off");
                if (failSave) { failure = L"Fake save failure"; return false; }
                return aip::WriteTextFileUtf8Bom(g_config->Path(), prospective);
            },
            [&](std::wstring&) { return aip::RestoreIniTextSnapshot(g_config->Path(), previous); }, &error);
    }
};

void TestInertStartupTransactions()
{
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(ARRAYSIZE(temporary), temporary)) throw std::runtime_error("No temporary path");
    const std::wstring directory = std::wstring(temporary) + L"AIProjects.ADB.Inert." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) throw std::runtime_error("Could not create isolated fixture");
    const std::wstring path = directory + L"\\settings.ini";
    try
    {
        g_paths = aip::BuildSidecarPathsFromExecutable(aip::GetCurrentExecutablePath(), L"ADBController", path);
        g_config = std::make_unique<aip::IniConfigStore>(path, kConfigHeader, 1000);
        const std::wstring original = L"[Settings]\nRunAtStartup=0\nTheme=Auto\n[Devices]\nFakeTV=example.invalid\n";
        if (!aip::WriteTextFileUtf8Bom(path, original)) throw std::runtime_error("Could not write fixture");
        FakeStartup fake;
        StartupCommit backend = [&](bool desired, const SettingsMutator& mutate, std::wstring& error) { return fake.Commit(desired, mutate, error); };
        CommandLineOptions options;
        std::wstring error;
        ParseCommandLine(options, error, L"ADBController.exe --startup --no-logging --configure-only");
        Check(ApplyCommandLineConfiguration(options, error, backend) && fake.installed &&
            fake.operations == std::vector<std::wstring>{L"launch-on", L"save-on"},
            "Startup enable uses the shared direction-safe transaction through a fake launch backend");
        ApplicationSettings loaded;
        Check(ReadApplicationSettings(loaded, error) && loaded.runAtStartup && !loaded.loggingEnabled,
            "Startup and companion CLI settings commit as one INI batch");
        options = {};
        ParseCommandLine(options, error, L"ADBController.exe --no-startup --configure-only");
        fake.operations.clear();
        Check(ApplyCommandLineConfiguration(options, error, backend) && !fake.installed &&
            fake.operations == std::vector<std::wstring>{L"save-off", L"launch-off"},
            "Startup disable persists false before removing the fake launch path");
        options = {};
        ParseCommandLine(options, error, L"ADBController.exe --startup --theme=Light --configure-only");
        std::wstring before;
        aip::LoadIniText(path, before);
        fake.failLaunch = true;
        Check(!ApplyCommandLineConfiguration(options, error, backend) && !fake.installed &&
            ReadApplicationSettings(loaded, error) && !loaded.runAtStartup && loaded.theme == ThemeMode::Auto,
            "failed Startup creation leaves every requested setting unchanged");
        fake.failLaunch = false;
        fake.failSave = true;
        Check(!ApplyCommandLineConfiguration(options, error, backend) && !fake.installed &&
            ReadApplicationSettings(loaded, error) && !loaded.runAtStartup,
            "failed atomic INI persistence rolls back the fake Startup path");
        fake.failSave = false;
        options.settings.push_back({L"Settings", L"ShowTrayIcon", L"invalid"});
        fake.operations.clear();
        Check(!ApplyCommandLineConfiguration(options, error, backend) && fake.operations.empty(),
            "invalid companion settings never reach the Startup backend");
        const auto spec = StartupSpec();
        Check(spec.identityPath == path && spec.arguments == L"--ini " + aip::QuoteCommandLineArg(path) &&
            spec.executablePath == g_paths.exePath && spec.workingDirectory == g_paths.exeDir,
            "Startup launch metadata selects the effective INI without capturing ADB actions or flags");
        g_config.reset();
    }
    catch (...) { g_config.reset(); DeleteFileW(path.c_str()); RemoveDirectoryW(directory.c_str()); throw; }
    DeleteFileW(path.c_str());
    Check(RemoveDirectoryW(directory.c_str()) != FALSE, "inert fixture cleans all files without any Startup-folder mutation");
}
}
int main()
{
    try { TestConfigurationAndMenus(); TestInertStartupTransactions(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; ++failures; }
    std::cout << (failures ? "ADB inert tests failed.\n" : "ADB inert tests passed.\n");
    return failures ? 1 : 0;
}

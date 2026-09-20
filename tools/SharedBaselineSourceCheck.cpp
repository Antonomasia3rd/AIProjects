#define SOURCE_CHECK_LABEL "Shared baseline"
#include "source_check_common.h"
#include <utility>

int main()
{
    try
    {
        const std::string appPaths = ReadAll("dependencies/app_paths.inc");
        const std::string baselineApp = ReadAll("dependencies/baseline_app.h");
        const std::string commandLine = ReadAll("dependencies/command_line.inc");
        const std::string sharedCore = ReadAll("dependencies/core.inc");
        const std::string configIni = ReadAll("dependencies/config_ini.inc");
        const std::string dpapi = ReadAll("dependencies/dpapi.inc");
        const std::string logging = ReadAll("dependencies/logging.inc");
        const std::string tray = ReadAll("dependencies/tray.inc");
        const std::string startupShortcut = ReadAll("dependencies/startup_shortcut.inc");
        const std::string packagedStartup = ReadAll("dependencies/packaged_startup.inc");
        const std::string powershellRunner = ReadAll("dependencies/powershell_runner.inc");
        const std::string managedNamedObjects = ReadAll("dependencies/managed_named_objects.cs");
        const std::string managedIni = ReadAll("dependencies/managed_ini.cs");
        const std::string managedStartup = ReadAll("dependencies/managed_startup_shortcut.cs");
        const std::string managedTray = ReadAll("dependencies/managed_tray.cs");
        const std::string releaseVersion = ReadAll("dependencies/release_version.inc");
        const std::string releaseVersionResource = ReadAll("dependencies/release_version_resource.rc.inc");
        const std::string releaseVersionResolver = ReadAll("dependencies/resolve_release_version.ps1");
        const std::string baselineHeader = ReadAll("dependencies/desktop_app_baseline.h");
        const std::string dependenciesReadme = ReadAll("dependencies/README.md");
        const std::string sharedTests = ReadAll("tools/SharedBaselineTests.cpp");
        const std::string sharedTestScript = ReadAll("tools/TestSharedBaseline.cmd");
        const std::string desktopSourceTestScript = ReadAll("DesktopStub/TestDesktopStubSource.cmd");
        const std::string discordSourceTestScript = ReadAll("DiscordRPC/TestDiscordRPCSource.cmd");
        const std::string secureSourceTestScript = ReadAll("legacy/SecureDesktopLauncher/TestSecureDesktopLauncherSource.cmd");
        const std::string discordMain = ReadAll("dependencies/DiscordRPC/drpc_environment.inc") + ReadAll("DiscordRPC/DiscordRPC.cpp");
        const std::string discordCore = ReadAll("dependencies/DiscordRPC/drpc_core.inc");
        const std::string desktopMain = ReadAll("DesktopStub/DesktopStub.cpp");
        const std::string asusBlink = ReadAll("dependencies/asusblink/asusblink_app.cs");
        const std::string asusOverlay = ReadAll("legacy/asusblink/asusblink.cs");
        const std::string asusProductBuild = ReadAll("legacy/asusblink/BuildAsusBlink.cmd");
        const std::string capsBlink = ReadAll("dependencies/capsblink/capsblink_app.cs");
        const std::string capsOverlay = ReadAll("legacy/capsblink/capsblink.cs");
        const std::string capsProductBuild = ReadAll("legacy/capsblink/BuildCapsBlink.cmd");
        const std::string dnsAutoUpdate = ReadAll("dependencies/DNSAutoUpdate/dns_auto_update_app.cs");
        const std::string dnsOverlay = ReadAll("legacy/DNSAutoUpdate/DNSAutoUpdate.cs");
        const std::string dnsBuild = ReadAll("legacy/DNSAutoUpdate/BuildDNSAutoUpdate.cmd");
        const std::string windowsBuild = ReadAll(".github/scripts/build-windows.cmd");
        const auto buildSection = [&windowsBuild](const std::string& start, const std::string& end)
        {
            const size_t first = windowsBuild.find(start);
            const size_t last = windowsBuild.find(end, first == std::string::npos ? 0 : first + start.size());
            if (first == std::string::npos || last == std::string::npos || last <= first)
                throw std::runtime_error("missing Windows build section: " + start);
            return windowsBuild.substr(first, last - first);
        };
        const std::string asusBuild = buildSection("\n:BuildAsusBlink\n", "\n:BuildCapsBlink\n");
        const std::string capsBuild = buildSection("\n:BuildCapsBlink\n", "\n:BuildYourPhoneHideBanner\n");

        RequireContains(
            "supported aggregate include is documented",
            "dependencies/README.md",
            dependenciesReadme,
            "desktop_app_baseline.h` from product translation units. That aggregate");
        RequireContains(
            "individual dependency modules are not promised standalone",
            "dependencies/README.md",
            dependenciesReadme,
            "not guaranteed to be standalone");
        RequireContains(
            "DesktopStub INI dialect is documented",
            "dependencies/README.md",
            dependenciesReadme,
            "write assignments as `\"Name\" = \"Value\"`");

        RequireContains(
            "baseline aggregate owns include order",
            "dependencies/desktop_app_baseline.h",
            baselineHeader,
            "#include \"baseline_app.h\"");
        RequireOrderedContains(
            "baseline aggregate includes shared modules in dependency order",
            "dependencies/desktop_app_baseline.h",
            baselineHeader,
            {
                "#include \"core.inc\"",
                "#include \"app_paths.inc\"",
                "#include \"logging.inc\"",
                "#include \"config_ini.inc\"",
                "#include \"command_line.inc\"",
                "#include \"tray.inc\""
            });

        RequireContains(
            "INI writer emits DesktopStub quoted assignment syntax",
            "dependencies/config_ini.inc",
            configIni,
            "return QuoteIniString(key) + L\" = \" + QuoteIniString(value);");
        RequireContains(
            "shared command-line dependency provides baseline primitives",
            "dependencies/command_line.inc",
            commandLine,
            "ParseIniSetSpec");
        RequireContains(
            "shared command-line dependency provides strict option-value extraction",
            "dependencies/command_line.inc",
            commandLine,
            "TakeCommandLineValue");
        RequireContains(
            "shared command-line dependency provides strict integer parsing",
            "dependencies/command_line.inc",
            commandLine,
            "ParseIntValueInRange");
        RequireContains(
            "shared command-line dependency handles parent-console output",
            "dependencies/command_line.inc",
            commandLine,
            "AttachConsole(ATTACH_PARENT_PROCESS)");
        RequireContains(
            "shared command-line dependency loops console writes",
            "dependencies/command_line.inc",
            commandLine,
            "while (offset < output.size())");
        RequireContains(
            "shared command-line dependency binds runtime console streams",
            "dependencies/command_line.inc",
            commandLine,
            "BindStandardConsoleStreams");
        RequireContains(
            "shared logger supports runtime console mirroring",
            "dependencies/logging.inc",
            logging,
            "options.consoleEnabled");
        RequireContains(
            "shared logger can replay recent lines after console allocation",
            "dependencies/logging.inc",
            logging,
            "ReplayRecentToConsole");
        RequireContains(
            "shared command-line integer parser checks overflow",
            "dependencies/command_line.inc",
            commandLine,
            "errno == ERANGE");
        RequireContains(
            "shared command-line integer parser uses wide long long parsing",
            "dependencies/command_line.inc",
            commandLine,
            "wcstoll");
        RequireContains(
            "shared PowerShell runner validates an absolute executable path",
            "dependencies/powershell_runner.inc",
            powershellRunner,
            "IsLocalAbsoluteFilePath(options.powerShellExe)");
        RequireContains(
            "shared PowerShell runner pins CreateProcess to the selected executable",
            "dependencies/powershell_runner.inc",
            powershellRunner,
            "CreateProcessW(options.powerShellExe.c_str()");
        RequireNotContains(
            "shared PowerShell runner never relies on first-token executable resolution",
            "dependencies/powershell_runner.inc",
            powershellRunner,
            "CreateProcessW(nullptr");
        RequireContains(
            "shared tray dependency provides baseline menu primitives",
            "dependencies/tray.inc",
            tray,
            "AppendTrayMenuItem");
        RequireContains(
            "shared tray registration couples version 4 with hover tooltip support",
            "dependencies/tray.inc",
            tray,
            "candidate.uFlags |= NIF_SHOWTIP;");
        RequireContains(
            "shared tray dependency owns notification-area registration",
            "dependencies/tray.inc",
            tray,
            "RegisterTrayIcon");
        RequireContains(
            "shared tray dependency owns tooltip updates",
            "dependencies/tray.inc",
            tray,
            "ModifyTrayIconTooltip");
        RequireOrderedContains(
            "shared tray dependency supplies a nonblank hover fallback",
            "dependencies/tray.inc",
            tray,
            {
                "NormalizeTrayTooltip",
                "visibleText(tooltip)",
                "visibleText(fallbackTooltip)",
                "L\"Application\""
            });
        RequireContains(
            "shared tray fallback has runtime regression coverage",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "tray tooltip normalization rejects blank hover text");
        RequireContains(
            "shared tray dependency owns notification-area removal",
            "dependencies/tray.inc",
            tray,
            "UnregisterTrayIcon");
        RequireOrderedContains(
            "shared tray header fixes the DesktopStub root ordering contract",
            "dependencies/tray.inc",
            tray,
            {
                "AppendBaselineTrayMenuHeader",
                "dropdownCommand",
                "primaryCommand",
                "versionText",
                "MF_SEPARATOR"
            });
        RequireContains(
            "shared tray header has a runtime content regression test",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "baseline tray header preserves dropdown, primary action, version, separator order");
        RequireContains(
            "shared tray dependency provides submenu primitives",
            "dependencies/tray.inc",
            tray,
            "BeginTrayNestedMenu");
        RequireContains(
            "shared tray balloon helper reports notification-area failures",
            "dependencies/tray.inc",
            tray,
            "Shell_NotifyIconW(NIM_MODIFY, &notification)");
        RequireContains(
            "shared startup helper resolves only the per-user Startup folder",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "SHGetKnownFolderPath(\n        FOLDERID_Startup");
        RequireContains(
            "shared startup helper writes ShellLink shortcuts",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "CLSID_ShellLink");
        RequireContains(
            "shared startup helper validates shortcut working directories",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "GetWorkingDirectory");
        RequireContains(
            "shared startup helper atomically replaces its shortcut",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH");
        RequireContains(
            "shared startup helper uses a stable caller-provided profile identity",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "identityPath");
        RequireContains(
            "shared startup helper isolates short concurrent temporary links",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "BuildStartupShortcutTemporaryPath");
        RequireContains(
            "shared startup helper serializes cross-process shortcut mutation",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "AIProjects.StartupShortcut.");
        RequireContains(
            "shared startup desired-state operation locks across query and mutation",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "StartupShortcutMutationLock transactionLock");
        RequireContains(
            "shared startup desired-state operation exposes previous state",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "SetStartupShortcutDesiredAtPath");
        RequireContains(
            "shared startup helper centralizes INI-coupled commit",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "CommitStartupShortcutIniState");
        RequireContains(
            "shared startup helper holds the INI lock across coupled commit",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "IniWriteMutexGuard iniLock");
        RequireContains(
            "shared startup helper reconciles prior config and launch state",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "Could not reconcile Startup before the INI transaction");
        RequireContains(
            "shared config helper owns direction-safe launch/config sequencing",
            "dependencies/config_ini.inc",
            configIni,
            "ExecuteCrashConsistentLaunchConfigState");
        RequireOrderedContains(
            "shared config helper installs before persisting enabled state",
            "dependencies/config_ini.inc",
            configIni,
            {
                "if (desired)",
                "launch(true, L\"Could not enable Startup\"",
                "std::forward<PersistenceMutation>(persistDesired)"
            });
        RequireOrderedContains(
            "shared config helper persists disabled state before removal",
            "dependencies/config_ini.inc",
            configIni,
            {
                "if (!persist(",
                "if (launch(false, L\"Could not disable Startup\""
            });
        RequireContains(
            "shared startup helper rejects targetless ShellLinks",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "result == S_FALSE || target[0] == L'\\0'");
        RequireContains(
            "shared startup helper refuses cross-target replacement",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "Refusing to replace a same-named Startup shortcut that points to another executable.");
        RequireContains(
            "shared startup helper refuses cross-target deletion",
            "dependencies/startup_shortcut.inc",
            startupShortcut,
            "Refusing to remove a same-named Startup shortcut that points to another executable.");
        RequireContains(
            "shared startup helper has a ShellLink round-trip test",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup shortcut ShellLink round trip");
        RequireContains(
            "shared startup helper tests concurrent replacement",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup shortcut replacement is safe under same-process concurrency");
        RequireContains(
            "shared startup helper tests cross-target deletion refusal",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup shortcut install and removal refuse a same-name foreign target");
        RequireContains(
            "shared startup helper tests stale-directory cleanup",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup shortcut removal works after its working directory disappears");
        RequireContains(
            "shared startup helper tests targetless links",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup shortcut loader rejects a targetless ShellLink");
        RequireContains(
            "shared startup helper tests desired-state transactions",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup shortcut desired-state transaction reports and changes previous state atomically");
        RequireContains(
            "shared startup helper tests crash-consistent enable ordering",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup/config transaction installs before persisting enabled state");
        RequireContains(
            "shared startup helper tests crash-consistent disable ordering",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup/config transaction persists disabled state before removal");
        RequireContains(
            "shared startup helper tests direction-safe rollback",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "Startup/config transaction restores launch before enabled config rollback");
        RequireContains(
            "shared packaged startup helper uses the manifest StartupTask API",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "Windows::ApplicationModel::StartupTask::GetAsync");
        RequireContains(
            "shared packaged startup helper enables through its bounded await",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "AwaitPackagedStartupOperation(task.RequestEnableAsync())");
        RequireContains(
            "shared packaged startup await has a finite default deadline",
            "dependencies/packaged_startup.inc", packagedStartup, "std::chrono::milliseconds(5000)");
        RequireContains(
            "shared packaged startup await uses the timeout",
            "dependencies/packaged_startup.inc", packagedStartup, "operation.wait_for(timeout)");
        RequireContains(
            "shared packaged startup await requests cancellation on timeout",
            "dependencies/packaged_startup.inc", packagedStartup, "operation.Cancel()");
        RequireNotContains(
            "shared packaged startup helper has no unbounded async get",
            "dependencies/packaged_startup.inc", packagedStartup, ".get()");
        RequireContains(
            "shared packaged startup helper disables through StartupTask",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "task.Disable()");
        RequireContains(
            "shared packaged startup helper keeps blocking WinRT work off an STA caller",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "winrt::init_apartment(winrt::apartment_type::multi_threaded)");
        RequireContains(
            "shared packaged startup helper serializes cross-process mutation",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "AIProjects.PackagedStartup.");
        RequireContains(
            "shared packaged startup helper centralizes INI-coupled commit",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "CommitPackagedStartupTaskIniState");
        RequireContains(
            "shared packaged startup helper uses direction-safe sequencing",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "ExecuteCrashConsistentLaunchConfigState");
        RequireContains(
            "shared packaged startup helper lets enforced matching state repair INI",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "if (desired != previousStartup.enabled)");
        RequireContains(
            "shared packaged startup helper requires package identity",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "CurrentProcessHasPackageIdentity()");
        RequireContains(
            "shared packaged startup helper preserves explicit user disables",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "PackagedStartupState::DisabledByUser");
        RequireContains(
            "shared packaged startup helper preserves administrator policy",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "The administrator policy cannot be overridden by this app.");
        RequireNotContains(
            "shared packaged startup helper never writes the Startup folder",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "FOLDERID_Startup");
        RequireNotContains(
            "shared packaged startup helper never writes the Run registry key",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "HKEY_CURRENT_USER");
        RequireNotContains(
            "shared packaged startup helper never creates scheduled tasks",
            "dependencies/packaged_startup.inc",
            packagedStartup,
            "TaskScheduler");
        RequireContains(
            "shared packaged startup contract is documented",
            "dependencies/README.md",
            dependenciesReadme,
            "Windows.ApplicationModel.StartupTask");
        RequireContains(
            "shared managed named objects coordinate across Windows sessions",
            "dependencies/managed_named_objects.cs",
            managedNamedObjects,
            "@\"Global\\AIProjects.\"");
        RequireContains(
            "shared managed named objects isolate per-user controls by SID",
            "dependencies/managed_named_objects.cs",
            managedNamedObjects,
            "WindowsIdentity.GetCurrent()");
        RequireContains(
            "shared managed INI helper serializes cross-process and cross-session mutation",
            "dependencies/managed_ini.cs",
            managedIni,
            "ManagedNamedObjects.CurrentUserScopedName(");
        RequireContains(
            "shared managed INI helper bounds sidecar reads",
            "dependencies/managed_ini.cs",
            managedIni,
            "INI file exceeds the ");
        RequireContains(
            "shared managed INI helper enforces its bound while consuming bytes",
            "dependencies/managed_ini.cs",
            managedIni,
            "if (read > limit - total)");
        RequireContains(
            "shared managed INI helper rejects malformed encoded input",
            "dependencies/managed_ini.cs",
            managedIni,
            "new UTF8Encoding(false, true)");
        RequireContains(
            "shared managed INI helper atomically replaces batches",
            "dependencies/managed_ini.cs",
            managedIni,
            "File.Replace(temporaryPath, spec.FilePath, null)");
        RequireContains(
            "shared managed INI helper collapses duplicate assignments",
            "dependencies/managed_ini.cs",
            managedIni,
            "lines.RemoveAt(matches[i])");
        RequireContains(
            "shared managed INI helper serializes reads with replacement",
            "dependencies/managed_ini.cs",
            managedIni,
            "List<string> lines = ReadLinesUnderLock(spec)");
        RequireContains(
            "shared managed INI helper rejects non-round-tripping keys",
            "dependencies/managed_ini.cs",
            managedIni,
            "key.Equals(key.Trim(), StringComparison.Ordinal)");
        RequireContains(
            "shared managed INI helper checks the committed byte limit",
            "dependencies/managed_ini.cs",
            managedIni,
            "GuardFileSize(spec, temporaryPath)");
        RequireContains(
            "shared managed startup helper targets the per-user Startup folder",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "Environment.GetFolderPath(Environment.SpecialFolder.Startup)");
        RequireContains(
            "shared managed startup helper serializes cross-process and cross-session mutation",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "ManagedNamedObjects.CurrentUserScopedName(");
        RequireContains(
            "shared managed startup helper validates all ShellLink launch metadata",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "MatchesExactLaunch");
        RequireContains(
            "shared managed startup helper can recognize owned legacy targets for migration",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "public static bool HasOwnedTarget(");
        RequireContains(
            "shared managed startup helper refuses foreign replacement",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "Refusing to replace a same-named Startup shortcut that points to another executable.");
        RequireContains(
            "shared managed startup helper atomically replaces owned links",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "File.Replace(temporary, destination, null)");
        RequireContains(
            "shared managed startup helper binds under-lock mutations to the requested profile",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "The Startup shortcut mutation lock belongs to another profile.");
        RequireContains(
            "shared managed startup helper centralizes the INI-coupled transaction",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "public static bool CommitIniCoupledState(");
        RequireContains(
            "shared managed startup helper documents direction-safe crash recovery",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "Disabling uses the");
        RequireContains(
            "shared managed startup helper restores enabled configuration only after its launch path",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "never restore INI=true until a launchable shortcut exists");
        RequireContains(
            "shared managed startup helper stages outside the enumerated Startup folder",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            ".AIProjects-StartupStaging");
        RequireContains(
            "shared managed startup helper propagates legacy cleanup failure",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "return RemoveOwnedLegacyShortcuts(spec, out error)");
        RequireNotContains(
            "shared managed startup helper never writes the Run registry key",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "HKEY_CURRENT_USER");
        RequireNotContains(
            "shared managed startup helper never creates scheduled tasks",
            "dependencies/managed_startup_shortcut.cs",
            managedStartup,
            "TaskScheduler");
        RequireContains(
            "shared managed tray helper owns notification-icon construction",
            "dependencies/managed_tray.cs",
            managedTray,
            "CreateNotifyIcon");
        RequireContains(
            "shared managed tray helper owns the WinForms tooltip limit",
            "dependencies/managed_tray.cs",
            managedTray,
            "NotifyIconTooltipLimit = 63");
        RequireContains(
            "shared managed tray helper owns the product/version header",
            "dependencies/managed_tray.cs",
            managedTray,
            "AppendHeader");
        RequireContains(
            "shared managed tray helper owns validated text prompts",
            "dependencies/managed_tray.cs",
            managedTray,
            "TryPromptText");
        RequireContains(
            "asusblink project source is a metadata overlay",
            "legacy/asusblink/asusblink.cs",
            asusOverlay,
            "implementation is compiled from");
        RequireContains(
            "asusblink consumes the shared managed startup lifecycle",
            "dependencies/asusblink/asusblink_app.cs",
            asusBlink,
            "ManagedStartupShortcut.CommitIniCoupledState");
        RequireContains(
            "asusblink consumes the shared managed INI lifecycle",
            "dependencies/asusblink/asusblink_app.cs",
            asusBlink,
            "ManagedIniFile.SaveSectionBatch");
        RequireContains(
            "asusblink consumes the shared managed tray lifecycle",
            "dependencies/asusblink/asusblink_app.cs",
            asusBlink,
            "ManagedTrayBaseline.CreateNotifyIcon");
        RequireContains(
            "Windows build delegates asusblink to its product build",
            ".github/scripts/build-windows.cmd :BuildAsusBlink",
            asusBuild,
            "BuildAsusBlink.cmd");
        RequireContains(
            "asusblink build compiles shared named-object scoping",
            "legacy/asusblink/BuildAsusBlink.cmd",
            asusProductBuild,
            "dependencies\\managed_named_objects.cs");
        RequireContains(
            "asusblink build compiles the shared managed startup source",
            "legacy/asusblink/BuildAsusBlink.cmd",
            asusProductBuild,
            "dependencies\\managed_startup_shortcut.cs");
        RequireContains(
            "asusblink build compiles the shared managed tray source",
            "legacy/asusblink/BuildAsusBlink.cmd",
            asusProductBuild,
            "dependencies\\managed_tray.cs");
        RequireContains(
            "asusblink build compiles the shared managed INI source",
            "legacy/asusblink/BuildAsusBlink.cmd",
            asusProductBuild,
            "dependencies\\managed_ini.cs");
        RequireContains(
            "asusblink build compiles the product dependency overlay",
            "legacy/asusblink/BuildAsusBlink.cmd",
            asusProductBuild,
            "dependencies\\asusblink\\asusblink_app.cs");
        RequireContains(
            "asusblink build compiles the project metadata overlay",
            "legacy/asusblink/BuildAsusBlink.cmd",
            asusProductBuild,
            "%ROOT%asusblink.cs");
        RequireContains(
            "capsblink project source is a metadata overlay",
            "legacy/capsblink/capsblink.cs",
            capsOverlay,
            "implementation is compiled from");
        RequireContains(
            "capsblink consumes the shared managed startup lifecycle",
            "dependencies/capsblink/capsblink_app.cs",
            capsBlink,
            "ManagedStartupShortcut.CommitIniCoupledState");
        RequireContains(
            "capsblink consumes the shared managed INI lifecycle",
            "dependencies/capsblink/capsblink_app.cs",
            capsBlink,
            "ManagedIniFile.LoadSection");
        RequireContains(
            "capsblink consumes shared managed tray construction",
            "dependencies/capsblink/capsblink_app.cs",
            capsBlink,
            "ManagedTrayBaseline.CreateNotifyIcon");
        RequireContains(
            "capsblink exposes typed INI CLI and tray settings",
            "dependencies/capsblink/capsblink_app.cs",
            capsBlink,
            "PersistSettingsAtomically");
        RequireContains(
            "Windows build delegates capsblink to its product build",
            ".github/scripts/build-windows.cmd :BuildCapsBlink",
            capsBuild,
            "BuildCapsBlink.cmd");
        RequireContains(
            "capsblink uses a GUI subsystem binary so Startup is console-free",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "/target:winexe");
        RequireContains(
            "capsblink build treats compiler warnings as errors",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "/warnaserror+");
        RequireContains(
            "capsblink build compiles shared named-object scoping",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "dependencies\\managed_named_objects.cs");
        RequireContains(
            "capsblink build compiles the shared managed INI source",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "dependencies\\managed_ini.cs");
        RequireContains(
            "capsblink build compiles the shared managed Startup source",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "dependencies\\managed_startup_shortcut.cs");
        RequireContains(
            "capsblink build compiles the shared managed tray source",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "dependencies\\managed_tray.cs");
        RequireContains(
            "capsblink build compiles the product dependency overlay",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "dependencies\\capsblink\\capsblink_app.cs");
        RequireContains(
            "capsblink build compiles the project metadata overlay",
            "legacy/capsblink/BuildCapsBlink.cmd",
            capsProductBuild,
            "%ROOT%capsblink.cs");
        RequireContains(
            "capsblink uses cross-session profile and device controls",
            "dependencies/capsblink/capsblink_app.cs",
            capsBlink,
            "ManagedNamedObjects.MachineScopedName(");
        RequireContains(
            "capsblink reloads UI-only settings without reopening its device",
            "dependencies/capsblink/capsblink_app.cs",
            capsBlink,
            "Applied reloaded settings without reopening the keyboard device");
        RequireContains(
            "capsblink fixes logically-on indicator synchronization",
            "dependencies/capsblink/capsblink_app.cs",
            capsBlink,
            "indicators.LEDflags | Flags.KEYBOARD_CAPS_LOCK_ON");
        RequireContains(
            "DNSAutoUpdate project source is a metadata overlay",
            "legacy/DNSAutoUpdate/DNSAutoUpdate.cs",
            dnsOverlay,
            "implementation is compiled from");
        RequireContains(
            "DNSAutoUpdate consumes the shared managed INI lifecycle",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "ManagedIniFile.SaveSectionBatch");
        RequireContains(
            "DNSAutoUpdate consumes the shared managed Startup lifecycle",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "ManagedStartupShortcut.CommitIniCoupledState");
        RequireContains(
            "DNSAutoUpdate consumes the shared managed tray lifecycle",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "ManagedTrayBaseline.CreateNotifyIcon");
        RequireContains(
            "DNSAutoUpdate first-run mutation is disabled",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "{ EnabledKey, \"false\" }");
        RequireContains(
            "DNSAutoUpdate first-run profile does not implicitly own the root",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "{ NoRootRecordKey, \"true\" }");
        RequireOrderedContains(
            "DNSAutoUpdate verifies additions before stale-record removal",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            {
                "Adding and verifying replacements before any deletion.",
                "Removing stale IP "
            });
        RequireContains(
            "DNSAutoUpdate requires stable automatic discovery before deletion",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "consecutiveObservations >= 2");
        RequireContains(
            "DNSAutoUpdate uses strict dotted-decimal parsing",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "parts.Length != 4");
        RequireNotContains(
            "DNSAutoUpdate no longer treats generic localized text as missing DNS records",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "not found|does not exist");
        RequireContains(
            "DNSAutoUpdate owns a zone for the resident lifetime",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "EnsureZoneOwnership(settings.ZoneName");
        RequireContains(
            "DNSAutoUpdate process waits are cancellable",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "p.WaitForExit(250)");
        RequireNotContains(
            "DNSAutoUpdate never creates scheduled tasks",
            "dependencies/DNSAutoUpdate/dns_auto_update_app.cs",
            dnsAutoUpdate,
            "TaskScheduler");
        RequireContains(
            "DNSAutoUpdate build compiles its product dependency overlay",
            "legacy/DNSAutoUpdate/BuildDNSAutoUpdate.cmd",
            dnsBuild,
            "dependencies\\DNSAutoUpdate\\dns_auto_update_app.cs");
        RequireContains(
            "DNSAutoUpdate build compiles the shared named-object source",
            "legacy/DNSAutoUpdate/BuildDNSAutoUpdate.cmd",
            dnsBuild,
            "dependencies\\managed_named_objects.cs");
        RequireContains(
            "DNSAutoUpdate build compiles the shared managed INI source",
            "legacy/DNSAutoUpdate/BuildDNSAutoUpdate.cmd",
            dnsBuild,
            "dependencies\\managed_ini.cs");
        RequireContains(
            "DNSAutoUpdate build compiles the shared managed Startup source",
            "legacy/DNSAutoUpdate/BuildDNSAutoUpdate.cmd",
            dnsBuild,
            "dependencies\\managed_startup_shortcut.cs");
        RequireContains(
            "DNSAutoUpdate build compiles the shared managed tray source",
            "legacy/DNSAutoUpdate/BuildDNSAutoUpdate.cmd",
            dnsBuild,
            "dependencies\\managed_tray.cs");
        RequireContains(
            "shared release helper formats tag and four-part version",
            "dependencies/release_version.inc",
            releaseVersion,
            "ReleaseVersionDisplayText");
        RequireContains(
            "shared release resource keeps file and product versions aligned",
            "dependencies/release_version_resource.rc.inc",
            releaseVersionResource,
            "PRODUCTVERSION AIP_VERSION_COMMA");
        RequireContains(
            "shared release resolver accepts explicit CI version overrides",
            "dependencies/resolve_release_version.ps1",
            releaseVersionResolver,
            "VersionEnvironment");
        RequireContains(
            "shared release resolver rejects zero major versions",
            "dependencies/resolve_release_version.ps1",
            releaseVersionResolver,
            "$versionParts[0] -lt 1");
        RequireContains(
            "shared application baseline exposes reusable subsystem contracts",
            "dependencies/baseline_app.h",
            baselineApp,
            "BuildInstanceIdentity");
        RequireContains(
            "shared application baseline supports acknowledged instance requests",
            "dependencies/baseline_app.h",
            baselineApp,
            "SendMessageTimeoutW");
        RequireContains(
            "shared application baseline requires an explicit handled result",
            "dependencies/baseline_app.h",
            baselineApp,
            "static_cast<LRESULT>(result) == INSTANCE_REQUEST_HANDLED");
        RequireContains(
            "shared application baseline exposes resident shutdown state",
            "dependencies/baseline_app.h",
            baselineApp,
            "ResidentShutdownState");
        RequireContains(
            "shared application baseline exposes stable hash helper",
            "dependencies/baseline_app.h",
            baselineApp,
            "StableHashHex64");
        RequireContains(
            "shared application baseline exposes path-scoped instance identity",
            "dependencies/baseline_app.h",
            baselineApp,
            "BuildPathScopedInstanceIdentity");
        RequireContains(
            "shared config dependency exposes an explicit config store",
            "dependencies/config_ini.inc",
            configIni,
            "class IniConfigStore");
        RequireContains(
            "shared config store supports fresh mutations",
            "dependencies/config_ini.inc",
            configIni,
            "MutateFresh");
        RequireContains(
            "shared config dependency supports value removal",
            "dependencies/config_ini.inc",
            configIni,
            "RemoveIniValueFromText");
        RequireContains(
            "shared config dependency supports section removal",
            "dependencies/config_ini.inc",
            configIni,
            "RemoveIniSectionFromText");

        RequireContains(
            "app path helper derives sidecar paths",
            "dependencies/app_paths.inc",
            appPaths,
            "BuildSidecarPathsFromExecutable");
        RequireContains(
            "app path helper supports configured INI overrides",
            "dependencies/app_paths.inc",
            appPaths,
            "configOverride");
        RequireContains(
            "app path helper exposes strict configured INI resolution",
            "dependencies/app_paths.inc",
            appPaths,
            "TryBuildSidecarPathsFromExecutable");
        RequireContains(
            "app path helper validates configured INI file paths",
            "dependencies/app_paths.inc",
            appPaths,
            "TryResolveConfigFilePath");
        RequireContains(
            "app path helper rejects directory config paths",
            "dependencies/app_paths.inc",
            appPaths,
            "FILE_ATTRIBUTE_DIRECTORY");
        RequireContains(
            "app path helper rejects reserved device config paths",
            "dependencies/app_paths.inc",
            appPaths,
            "IsReservedWindowsDeviceBaseName");
        RequireContains(
            "app path helper rejects alternate data stream config paths",
            "dependencies/app_paths.inc",
            appPaths,
            "ConfigFileNameContainsColon");
        RequireContains(
            "app path helper rejects invalid Windows config filename characters",
            "dependencies/app_paths.inc",
            appPaths,
            "ConfigFileNameHasInvalidCharacters");
        RequireContains(
            "app path helper documents unchecked raw builder",
            "dependencies/app_paths.inc",
            appPaths,
            "unchecked path builder");
        RequireContains(
            "app path helper exposes safe current-process path builder",
            "dependencies/app_paths.inc",
            appPaths,
            "TryBuildCurrentProcessSidecarPaths");
        RequireContains(
            "safe current-process path builder validates user config overrides",
            "dependencies/app_paths.inc",
            appPaths,
            "GetCurrentExecutablePath(),");
        RequireContains(
            "app path helper exposes executable-side log paths",
            "dependencies/app_paths.inc",
            appPaths,
            "BuildExecutableSidecarLogPath");
        RequireContains(
            "app path helper exposes explicit default log path policy",
            "dependencies/app_paths.inc",
            appPaths,
            "DefaultLogPathPolicy");
        RequireContains(
            "shared core exposes strict absolute path helper",
            "dependencies/core.inc",
            sharedCore,
            "TryMakeAbsolutePath");
        RequireContains(
            "shared core exposes exception-safe Win32 critical-section locking",
            "dependencies/core.inc",
            sharedCore,
            "class CriticalSectionLock");
        RequireContains(
            "shared critical-section lock is non-copyable",
            "dependencies/core.inc",
            sharedCore,
            "CriticalSectionLock(const CriticalSectionLock&) = delete;");
        RequireContains(
            "shared core exposes move-only kernel handle ownership",
            "dependencies/core.inc",
            sharedCore,
            "class UniqueKernelHandle");
        RequireContains(
            "shared kernel handle owner rejects invalid handles",
            "dependencies/core.inc",
            sharedCore,
            "handle != nullptr && handle != INVALID_HANDLE_VALUE");
        RequireContains(
            "shared core exposes growable temporary-directory lookup",
            "dependencies/core.inc",
            sharedCore,
            "GetTemporaryDirectoryPath");
        RequireContains(
            "shared core exposes growable system-directory lookup",
            "dependencies/core.inc",
            sharedCore,
            "GetSystemDirectoryPath");
        RequireContains(
            "shared core exposes checked UTF-8 conversion",
            "dependencies/core.inc",
            sharedCore,
            "TryWideToUtf8");
        RequireContains(
            "shared core exposes checked UTF-8 decoding",
            "dependencies/core.inc",
            sharedCore,
            "TryUtf8ToWide");
        RequireContains(
            "shared UTF conversion wrappers document empty-on-failure behavior",
            "dependencies/core.inc",
            sharedCore,
            "empty output and conversion failure must be distinguished");
        RequireContains(
            "shared UTF-8 encoder verifies exact second conversion length",
            "dependencies/core.inc",
            sharedCore,
            "if (written != length)");
        RequireContains(
            "shared config decoder verifies exact second decode length",
            "dependencies/config_ini.inc",
            configIni,
            "if (written != need)");
        RequireContains(
            "shared INI reader bounds sidecar allocation",
            "dependencies/config_ini.inc",
            configIni,
            "kMaxIniFileBytes");
        RequireContains(
            "shared INI loader preserves read and decode failures",
            "dependencies/config_ini.inc",
            configIni,
            "SetLastError(readError == ERROR_SUCCESS ? ERROR_READ_FAULT : readError)");
        RequireContains(
            "shared JSON decoding rejects invalid UTF-8",
            "dependencies/core.inc",
            sharedCore,
            "return TryUtf8ToWide(utf8, value);");
        RequireContains(
            "shared JSON decoding rejects raw control characters",
            "dependencies/core.inc",
            sharedCore,
            "uch < 0x20");
        RequireContains(
            "shared JSON string scanner rejects raw control characters",
            "dependencies/core.inc",
            sharedCore,
            "static_cast<unsigned char>(ch) < 0x20");
        RequireContains(
            "shared JSON string scanner rejects invalid escapes",
            "dependencies/core.inc",
            sharedCore,
            "JsonEscapeEnd(json, i, next)");
        RequireContains(
            "shared JSON scanner validates primitive tokens",
            "dependencies/core.inc",
            sharedCore,
            "JsonPrimitiveTokenIsValid");
        RequireContains(
            "shared JSON primitive scanner rejects malformed values",
            "dependencies/core.inc",
            sharedCore,
            "JsonPrimitiveValueEnd");
        RequireContains(
            "shared JSON lookup decodes object keys",
            "dependencies/core.inc",
            sharedCore,
            "DecodeJsonStringUtf8Range(json, pos, stringEnd, decodedKey)");
        RequireContains(
            "shared JSON top-level scanner tracks comma/member state",
            "dependencies/core.inc",
            sharedCore,
            "bool firstMember = true;");
        RequireContains(
            "shared JSON helper exposes try-extract variant",
            "dependencies/core.inc",
            sharedCore,
            "TryExtractJsonStringValue");
        RequireContains(
            "shared core exposes looped file writes",
            "dependencies/core.inc",
            sharedCore,
            "WriteAllBytes");
        RequireContains(
            "shared core exposes lossy UTF-8 conversion for JSON",
            "dependencies/core.inc",
            sharedCore,
            "WideToUtf8Lossy");
        RequireContains(
            "app path helper uses growable module path lookup",
            "dependencies/app_paths.inc",
            appPaths,
            "GetCurrentExecutablePath");
        RequireContains(
            "app path helper does not use fixed module path buffer",
            "dependencies/app_paths.inc",
            appPaths,
            "buffer.resize(buffer.size() * 2)");
        RequireContains(
            "shared logging helper exposes a reusable recent log buffer",
            "dependencies/logging.inc",
            logging,
            "class RecentLogBuffer");
        RequireContains(
            "shared logging helper keeps a bounded recent buffer",
            "dependencies/logging.inc",
            logging,
            "class Utf8Logger");
        RequireContains(
            "shared logging helper writes UTF-8 sidecar log lines",
            "dependencies/logging.inc",
            logging,
            "AppendUtf8LineToFile");
        RequireContains(
            "shared logging helper line appends reuse raw append primitive",
            "dependencies/logging.inc",
            logging,
            "return AppendUtf8TextToFile(filePath, line + L\"\\r\\n\", writeUtf8Bom, lockWaitMs);");
        RequireContains(
            "shared logging helper preserves synchronized UTF-16 compatibility logs",
            "dependencies/logging.inc",
            logging,
            "AppendUtf16LineToFile");
        RequireContains(
            "shared UTF-16 compatibility logger writes complete records",
            "dependencies/logging.inc",
            logging,
            "text.size() * sizeof(wchar_t)");
        RequireContains(
            "shared logging helper writes UTF-8 BOM for new files",
            "dependencies/logging.inc",
            logging,
            "{ 0xEF, 0xBB, 0xBF }");
        RequireContains(
            "shared logging helper uses cross-process append locking",
            "dependencies/logging.inc",
            logging,
            "LockFileEx");
        RequireContains(
            "shared logging helper opens append handle with write access for locking",
            "dependencies/logging.inc",
            logging,
            "FILE_APPEND_DATA | FILE_WRITE_DATA | SYNCHRONIZE");
        RequireContains(
            "shared logging helper retries transient sharing violations while opening append files",
            "dependencies/logging.inc",
            logging,
            "OpenAppendLogFileWithRetry");
        RequireContains(
            "shared logging helper limits append-open retries with the configured wait",
            "dependencies/logging.inc",
            logging,
            "error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION");
        RequireContains(
            "shared logging helper seeks to EOF after acquiring the append lock",
            "dependencies/logging.inc",
            logging,
            "SetFilePointerEx(file, end, nullptr, FILE_END)");
        RequireContains(
            "shared logging helper loops on partial writes",
            "dependencies/logging.inc",
            logging,
            "WriteAllBytes");
        RequireContains(
            "shared logging helper propagates delayed close failures",
            "dependencies/logging.inc",
            logging,
            "if (!CloseHandle(file) && ok)");
        RequireContains(
            "shared config writer loops on partial writes",
            "dependencies/config_ini.inc",
            configIni,
            "WriteAllBytes(file");
        RequireContains(
            "shared config writer uses checked UTF-8 conversion",
            "dependencies/config_ini.inc",
            configIni,
            "TryWideToUtf8(text, utf8)");
        RequireContains(
            "shared JSON escaping avoids strict conversion data loss",
            "dependencies/core.inc",
            sharedCore,
            "WideToUtf8Lossy(value)");
        RequireContains(
            "shared logging helper exposes bounded append lock wait",
            "dependencies/logging.inc",
            logging,
            "DWORD lockWaitMs = 5000");
        RequireContains(
            "shared JSON field scanner is documented as not a full parser",
            "dependencies/core.inc",
            sharedCore,
            "not a full JSON parser");
        RequireNotContains(
            "shared JSON field scanner does not keep duplicate malformed-string checks",
            "dependencies/core.inc",
            sharedCore,
            "if (stringEnd == std::string::npos)\n        if (stringEnd == std::string::npos)");
        RequireContains(
            "shared logging helper passes bounded lock wait to file appends",
            "dependencies/logging.inc",
            logging,
            "AppendUtf8LineToFile(filePath, line, writeUtf8Bom, lockWaitMs)");
        RequireContains(
            "shared logging helper treats UTF-8 conversion failure as write failure",
            "dependencies/logging.inc",
            logging,
            "TryWideToUtf8(text, utf8)");
        RequireContains(
            "shared logging helper exposes file write failure state",
            "dependencies/logging.inc",
            logging,
            "LastFileWriteFailed");
        RequireContains(
            "shared logging helper reports write failures to recent log",
            "dependencies/logging.inc",
            logging,
            "Log file write failed");
        RequireContains(
            "shared config mutex uses stable hash helper",
            "dependencies/config_ini.inc",
            configIni,
            "StableHashHex64(MakeAbsolutePath(path))");
        RequireContains(
            "shared config mutex supports bounded waits",
            "dependencies/config_ini.inc",
            configIni,
            "WaitForSingleObject(handle_, waitMs)");
        RequireNotContains(
            "shared config dependency does not keep a private mutex hash",
            "dependencies/config_ini.inc",
            configIni,
            std::string("Ini") + "MutexHash");
        RequireNotContains(
            "shared baseline does not expose environment-backed config primitives",
            "dependencies/core.inc",
            sharedCore,
            std::string("Read") + "EnvironmentString");

        RequireContains(
            "INI parser preserves unknown backslash escapes",
            "dependencies/config_ini.inc",
            configIni,
            "out.push_back(ch);");
        RequireContains(
            "INI parser preserves raw Windows-path backslashes by default",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI parser preserves raw Windows path backslashes");
        RequireContains(
            "shared DPAPI helper uses checked UTF-8 encoding",
            "dependencies/dpapi.inc",
            dpapi,
            "TryWideToUtf8(secret, utf8.value)");
        RequireContains(
            "shared DPAPI helper validates Win32 plaintext blob lengths",
            "dependencies/dpapi.inc",
            dpapi,
            "utf8.value.size() > static_cast<size_t>(MAXDWORD) - envelopeSize");
        RequireContains(
            "shared DPAPI helper validates Win32 encrypted blob lengths",
            "dependencies/dpapi.inc",
            dpapi,
            "cipherBytes.size() > MAXDWORD");
        RequireContains(
            "shared DPAPI helper rejects invalid decrypted UTF-8",
            "dependencies/dpapi.inc",
            dpapi,
            "TryUtf8ToWide(utf8.value, result)");
        RequireContains(
            "shared DPAPI writes an explicit versioned encoding",
            "dependencies/dpapi.inc",
            dpapi,
            "kDpapiV1Utf8Prefix = L\"dpapi:v1:utf8:\"");
        RequireContains(
            "shared DPAPI protect guards empty encrypted buffers",
            "dependencies/dpapi.inc",
            dpapi,
            "cipher.cbData == 0 || cipher.pbData == nullptr");
        RequireContains(
            "shared DPAPI requires callers to identify legacy encoding",
            "dependencies/dpapi.inc",
            dpapi,
            "DpapiLegacyEncoding legacyEncoding");
        RequireContains(
            "shared DPAPI supports deskband legacy UTF-16LE values",
            "dependencies/dpapi.inc",
            dpapi,
            "DpapiLegacyEncoding::Utf16LittleEndian");
        RequireContains(
            "shared DPAPI authenticates the new encoding inside ciphertext",
            "dependencies/dpapi.inc",
            dpapi,
            "kV1Utf8Envelope");
        RequireContains(
            "shared DPAPI wipes decrypted Win32 buffers",
            "dependencies/dpapi.inc",
            dpapi,
            "SecureZeroMemory(decrypted.pbData, decrypted.cbData)");
        RequireContains(
            "shared DPAPI forbids unexpected credential UI",
            "dependencies/dpapi.inc",
            dpapi,
            "CRYPTPROTECT_UI_FORBIDDEN");

        RequireContains(
            "shared tests cover bounded INI waits",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI write mutex supports bounded waits");
        RequireContains(
            "shared tests cover INI value removal",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI value removal preserves neighboring entries");
        RequireContains(
            "shared tests cover INI section removal",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI section removal preserves following sections");
        RequireContains(
            "shared tests cover strict integer overflow rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "integer parser rejects junk, overflow, and out-of-range values");
        RequireContains(
            "shared tests cover directory config path rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "config path helper rejects existing directories");
        RequireContains(
            "shared tests cover reserved config path rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "config path helper rejects reserved Windows device names");
        RequireContains(
            "shared tests cover alternate data stream config path rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "config path helper rejects alternate data stream names");
        RequireContains(
            "shared tests cover invalid config filename character rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "config path helper rejects invalid Windows filename characters");
        RequireContains(
            "shared tests cover invalid JSON primitive rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON lookup rejects invalid primitive tokens before later fields");
        RequireContains(
            "shared tests cover invalid JSON UTF-8 rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON string decoding rejects invalid UTF-8 bytes");
        RequireContains(
            "shared tests cover raw JSON control-character rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON string scanning rejects unescaped control characters");
        RequireContains(
            "shared tests cover invalid JSON escape rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON string scanning rejects invalid escape sequences");
        RequireContains(
            "shared tests cover escaped JSON object keys",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON lookup decodes escaped object keys");
        RequireContains(
            "shared tests cover JSON try-extract helper",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON try-extract distinguishes empty strings from missing or invalid fields");
        RequireContains(
            "shared tests cover DPAPI invalid UTF-16 rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI protect rejects invalid UTF-16 before encrypting");
        RequireContains(
            "shared tests cover versioned DPAPI empty-secret round trip",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI writes versioned UTF-8 values and round-trips an empty secret");
        RequireContains(
            "shared tests cover DiscordRPC legacy DPAPI compatibility",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI explicitly decodes DiscordRPC legacy UTF-8 values");
        RequireContains(
            "shared tests cover versioned Unicode independent of legacy policy",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI versioned encoding ignores the legacy policy and round-trips Unicode");
        RequireContains(
            "shared tests cover deskband legacy DPAPI compatibility",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI explicitly decodes deskband legacy UTF-16LE values");
        RequireContains(
            "shared tests reject unknown DPAPI versions",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI rejects unknown serialization versions before decrypting");
        RequireContains(
            "shared tests reject relabelled DPAPI envelopes",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI rejects a versioned envelope relabelled as legacy data");
        RequireContains(
            "shared baseline test script reports test exit code",
            "tools/TestSharedBaseline.cmd",
            sharedTestScript,
            "SharedBaselineTests exit code");
        for (const auto& script : std::vector<std::pair<std::string, std::string>>{
                 { "tools/TestSharedBaseline.cmd", sharedTestScript },
                 { "DesktopStub/TestDesktopStubSource.cmd", desktopSourceTestScript },
                 { "DiscordRPC/TestDiscordRPCSource.cmd", discordSourceTestScript },
                 { "legacy/SecureDesktopLauncher/TestSecureDesktopLauncherSource.cmd", secureSourceTestScript } })
        {
            RequireOrderedContains(
                "native test launcher prefers vcvars before a PATH compiler fallback: " + script.first,
                script.first,
                script.second,
                {
                    "if defined VCVARS",
                    "where cl.exe",
                    "goto HaveCompiler"
                });
        }
        RequireContains(
            "shared tests cover UTF-8 conversion failure",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger fails non-empty text it cannot encode");
        RequireContains(
            "shared tests cover INI writer UTF-8 conversion failure",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI file writer fails non-empty text it cannot encode");
        RequireContains(
            "shared tests cover oversized INI rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI reader rejects oversized sidecar files before allocation");
        RequireContains(
            "shared tests cover JSON invalid UTF-16 behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON escaping does not silently empty invalid UTF-16");
        RequireContains(
            "shared tests cover explicit default log policy",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "sidecar paths expose explicit executable-side default log policy");
        RequireContains(
            "INI tests lock escaped Windows path write style",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI writer uses DesktopStub quoted assignment and escaped path style");
        RequireContains(
            "INI tests lock app-level template escape separation",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI parser keeps app-level template escapes raw");
        RequireContains(
            "shared tests lock sidecar app path behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "sidecar paths derive default INI and log from executable name");
        RequireContains(
            "shared tests lock executable-side log path behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "sidecar paths can preserve executable-side default log behavior");
        RequireContains(
            "shared tests lock bounded logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger keeps bounded recent lines");
        RequireContains(
            "shared tests lock reusable recent log behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared recent log buffer preserves DesktopStub tray-log behavior");
        RequireContains(
            "shared tests lock concurrent logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger allows concurrent appenders");
        RequireContains(
            "shared tests lock transient sharing-violation recovery",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger retries transient reader sharing violations");
        RequireContains(
            "shared tests lock UTF-8 BOM logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger writes BOM for new log files");
        RequireContains(
            "shared tests lock UTF-16 compatibility logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-16 compatibility logger appends complete lines");
        RequireContains(
            "shared tests lock logging failure reporting behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger reports file write failures once");
        RequireContains(
            "shared tests lock logger failure-state reset behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger resets failure state when target changes");
        RequireContains(
            "shared tests lock logger failure reporting after recovery",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger reports a new failure after recovery");
        RequireContains(
            "shared tests lock bounded logger append wait behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger uses bounded append lock wait");
        RequireContains(
            "shared tests lock strict absolute path behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "strict absolute path helper rejects empty paths");
        RequireContains(
            "shared tests lock system-directory path behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "system directory helper returns an existing directory");
        RequireContains(
            "shared tests lock path-scoped identity hashing",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "single-instance identity supports shared path-scoped hashing");

        RequireContains(
            "DiscordRPC help contract allows read-only configured templates",
            "DiscordRPC/DiscordRPC.cpp",
            discordMain,
            "it may read an existing configured");
        RequireContains(
            "DiscordRPC consumes the aggregate desktop baseline",
            "DiscordRPC/DiscordRPC.cpp",
            discordMain,
            "../desktop_app_baseline.h");
        RequireContains(
            "DiscordRPC consumes the optional shared startup helper",
            "DiscordRPC/DiscordRPC.cpp",
            discordMain,
            "../startup_shortcut.inc");
        RequireContains(
            "DesktopStub consumes the aggregate desktop baseline",
            "DesktopStub/DesktopStub.cpp",
            desktopMain,
            "dependencies\\desktop_app_baseline.h");
        RequireContains(
            "DiscordRPC uses the shared INI store",
            "dependencies/DiscordRPC/drpc_core.inc",
            discordCore,
            "aip::IniConfigStore");
        RequireNotContains(
            "DiscordRPC does not use Win32 profile INI APIs",
            "dependencies/DiscordRPC/drpc_core.inc",
            discordCore,
            "GetPrivateProfileStringW");
        RequireNotContains(
            "DiscordRPC does not use Win32 profile INI writers",
            "dependencies/DiscordRPC/drpc_core.inc",
            discordCore,
            "WritePrivateProfileStringW");

        std::cout << "Shared baseline source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;

static class LegacyUtilitiesTests
{
    static int failures;

    static bool allowStartupIntegration;
    static int Main(string[] args)
    {
        if (args.Length == 0 || !Directory.Exists(args[0]) ||
            args.Skip(1).Any(arg => arg != "--allow-startup-integration")) return 2;
        allowStartupIntegration = args.Skip(1).Any(arg => arg == "--allow-startup-integration");
        string repositoryRoot =
            args.Length > 0 ? Path.GetFullPath(args[0]) : Directory.GetCurrentDirectory();
        TestDnsRecordParsing();
        TestDnsSafetyDefaults(repositoryRoot);
        TestStrictArgumentParsing();
        TestPhotoCollageAtomicOutput(repositoryRoot);
        TestTaskXmlHardening(repositoryRoot);
        TestCapsBlinkIdentity(repositoryRoot);
        TestAsusBlinkGuardrails(repositoryRoot);
        TestManagedNamedObjectScoping();
        TestManagedLoggingLifecycle();
        TestManagedIniLifecycle();
        TestManagedIniDialect(repositoryRoot);
        TestManagedStartupShortcutLifecycle();
        TestAlwaysUiAccessSource(repositoryRoot);

        if (failures != 0)
        {
            Console.Error.WriteLine("Legacy utility tests failed: " + failures);
            return 1;
        }

        Console.WriteLine("Legacy utility tests passed.");
        return 0;
    }

    static void TestDnsRecordParsing()
    {
        MethodInfo parse = PrivateMethod(typeof(DNSAutoUpdate), "ParseExactARecords");
        string sample =
            "Server address 10.0.0.53\r\n" +
            "@ 3600 A 192.168.1.10\r\n" +
            "unrelated 172.16.0.1\r\n" +
            "app 300 A 192.168.1.11\r\n";
        var values = ((IEnumerable)parse.Invoke(null, new object[] { sample }))
            .Cast<object>()
            .Select(value => value.ToString())
            .ToArray();
        Check(
            values.SequenceEqual(new[] { "192.168.1.10", "192.168.1.11" }),
            "DNS parser accepts only IPv4 values from A-record lines");
    }

    static void TestStrictArgumentParsing()
    {
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(DNSAutoUpdate),
                    "ParseCommandLine",
                    new object[] { new[] { "--zone-name", "--once" } });
            },
            "DNS parser rejects a missing option value");

        CheckThrows<ArgumentOutOfRangeException>(
            delegate
            {
                InvokePrivate(
                    typeof(PhotoCollage),
                    "ParseArgs",
                    new object[] { new[] { "--cols", "0" } });
            },
            "PhotoCollage rejects out-of-range numeric options");

        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(TaskSchedulerMigration),
                    "ParseArgs",
                    new object[] { new[] { "--old-sid", "not-a-sid", "--new-user", "User" } });
            },
            "TaskSchedulerMigration validates the source SID");
    }

    static void TestPhotoCollageAtomicOutput(string repositoryRoot)
    {
        string informationalOutput;
        int helpResult = InvokeMainAndCapture(
            typeof(PhotoCollage),
            new[] { "--help", "--definitely-invalid" },
            out informationalOutput);
        Check(
            helpResult == 0 && informationalOutput.Contains("Usage:"),
            "PhotoCollage help short-circuits invalid companion arguments");
        int versionResult = InvokeMainAndCapture(
            typeof(PhotoCollage),
            new[] { "--version", "--definitely-invalid" },
            out informationalOutput);
        Check(
            versionResult == 0 && informationalOutput.Contains("PhotoCollage"),
            "PhotoCollage version short-circuits invalid companion arguments");

        string root = Path.Combine(
            Path.GetTempPath(),
            "AIProjects-LegacyUtilitiesTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            string corrupt = Path.Combine(root, "000-corrupt.png");
            string first = Path.Combine(root, "first.png");
            string second = Path.Combine(root, "second.png");
            File.WriteAllText(corrupt, "not an image");
            using (var bitmap = new Bitmap(8, 6))
            {
                bitmap.SetPixel(0, 0, Color.Red);
                bitmap.Save(first);
                bitmap.SetPixel(0, 0, Color.Blue);
                bitmap.Save(second);
            }

            Type optionsType = typeof(PhotoCollage).GetNestedType(
                "Options",
                BindingFlags.NonPublic);
            object options = Activator.CreateInstance(optionsType, true);
            SetField(options, "InputFolder", root);
            string output = Path.Combine(root, "collage.png");
            SetField(options, "OutputFile", output);
            SetField(options, "Cols", 2);
            SetField(options, "MaxImages", 2);
            SetField(options, "JpegQuality", 80L);
            SetField(options, "MaxCanvasMegapixels", 10L);
            SetField(options, "LogFile", Path.Combine(root, "test.log"));

            InvokePrivate(typeof(PhotoCollage), "Run", new[] { options });
            using (Image image = Image.FromFile(output))
                Check(image.Width == 16 && image.Height == 6, "PhotoCollage writes the expected canvas");
            Check(
                File.ReadAllText(Path.Combine(root, "test.log")).Contains(
                    "Skipped unreadable image"),
                "PhotoCollage skips a corrupt supported-extension file and continues");

            File.WriteAllText(output, "old output");
            InvokePrivate(typeof(PhotoCollage), "Run", new[] { options });
            using (Image image = Image.FromFile(output))
                Check(image.Width == 16 && image.Height == 6, "PhotoCollage atomically replaces an existing output");

            bool temporaryFileLeftBehind = Directory
                .EnumerateFiles(root, ".*.tmp", SearchOption.TopDirectoryOnly)
                .Any();
            Check(!temporaryFileLeftBehind, "PhotoCollage removes temporary output files");

            string source = File.ReadAllText(Path.Combine(
                repositoryRoot,
                "dependencies",
                "PhotoCollage",
                "photo_collage_app.cs"));
            Check(
                source.Contains("ManagedLogFile.AppendLine(") &&
                !source.Contains("File.AppendAllText("),
                "PhotoCollage delegates sidecar logging to the shared managed logger");
        }
        finally
        {
            try
            {
                Directory.Delete(root, true);
            }
            catch
            {
            }
        }
    }

    static void TestTaskXmlHardening(string repositoryRoot)
    {
        CheckThrows<System.Xml.XmlException>(
            delegate
            {
                InvokePrivate(
                    typeof(TaskSchedulerMigration),
                    "LoadTaskXml",
                    new object[] { "<!DOCTYPE Task [<!ENTITY x SYSTEM \"file:///missing\">]><Task>&x;</Task>" });
            },
            "TaskSchedulerMigration rejects task XML DTDs");

        string path = Path.Combine(
            repositoryRoot,
            "dependencies",
            "TaskSchedulerMigration",
            "task_scheduler_migration_app.cs");
        string source = File.ReadAllText(path);
        Check(
            source.Contains("taskCollection.Item(i)") &&
            source.Contains("folderCollection.Item(i)") &&
            source.Contains("ReleaseComObject(taskCollection)") &&
            source.Contains("ReleaseComObject(folderCollection)"),
            "TaskSchedulerMigration releases COM collections and avoids COM foreach enumerators");
        Check(
            source.Contains("foreach (object folder in folders)") &&
            !source.Contains("ReleaseComObject(taskRef.Folder)"),
            "TaskSchedulerMigration releases each retained task folder once");
        Check(
            source.Contains("Task folders/items that could not be enumerated") &&
            source.Contains("enumerationFailures == 0"),
            "TaskSchedulerMigration reports partial enumeration as a nonzero result");
    }

    static void TestCapsBlinkIdentity(string repositoryRoot)
    {
        string first = (string)InvokePrivate(
            typeof(CapsLockLight),
            "StableHash",
            new object[] { "\\Device\\KeyboardClass0" });
        string second = (string)InvokePrivate(
            typeof(CapsLockLight),
            "StableHash",
            new object[] { "\\device\\keyboardclass0" });
        Check(first == second && first.Length == 8, "capsblink uses a stable case-insensitive device identity");

        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(CapsLockLight),
                    "ParseCommandLine",
                    new object[] { new[] { "--unknown" } });
            },
            "capsblink rejects unknown command-line arguments");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(CapsLockLight),
                    "ParseCommandLine",
                    new object[] { new[] { "--blink-interval-ms", "49" } });
            },
            "capsblink rejects out-of-range command-line timing");

        object parsed = InvokePrivate(
            typeof(CapsLockLight),
            "ParseCommandLine",
            new object[]
            {
                new[]
                {
                    "--startup",
                    "--flat-menu",
                    "--set", "Settings.BlinkIntervalMs=750"
                }
            });
        var parsedSettings = (IDictionary)parsed.GetType().GetField("Settings").GetValue(parsed);
        Check(
            (string)parsedSettings["RunAtStartup"] == "true" &&
            (string)parsedSettings["ShowMenuAsDropdown"] == "false" &&
            (string)parsedSettings["BlinkIntervalMs"] == "750",
            "capsblink aliases and generic setter share canonical persistent settings");

        string source = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "dependencies",
            "capsblink",
            "capsblink_app.cs"));
        int parsePosition = source.IndexOf("commandLine = ParseCommandLine(args);", StringComparison.Ordinal);
        int persistPosition = source.IndexOf("PersistSettingsAtomically(commandLine.Settings)", StringComparison.Ordinal);
        int hardwarePosition = source.IndexOf("HardwareWorker(initialSettings)", StringComparison.Ordinal);
        Check(
            parsePosition >= 0 && persistPosition >= 0 && hardwarePosition >= 0 &&
            parsePosition < persistPosition && persistPosition < hardwarePosition,
            "capsblink validates command-line syntax before INI and hardware mutation");
        Check(
            source.Contains("WaitHandle.WaitAny(controls, settings.BlinkIntervalMs)") &&
            source.Contains("localReloadEvent") &&
            source.Contains("externalExitEvent") &&
            source.Contains("RequestExit();"),
            "capsblink interrupts long waits for console, tray, and CLI control");
        Check(
            source.Contains("BlinkIntervalKey + \" must be an integer from 50 through 86400000.\"") &&
            source.Contains("throw new InvalidDataException(\"Invalid [Settings] \"") &&
            source.Contains("Unknown [Settings] key"),
            "capsblink strictly validates typed INI settings");
        Check(
            source.Contains("EntryPoint = \"DefineDosDeviceW\"") &&
            source.Contains("EntryPoint = \"CreateFileW\"") &&
            source.Contains("CharSet = CharSet.Unicode"),
            "capsblink binds string Win32 APIs to explicit Unicode entry points");
        Check(
            source.Contains("ManagedStartupShortcut.CommitIniCoupledState(") &&
            source.Contains("ManagedTrayBaseline.CreateNotifyIcon(") &&
            source.Contains("ManagedTrayBaseline.TryPromptText(") &&
            source.Contains("ShowMenuAsDropdown"),
            "capsblink consumes shared managed Startup and tray dependencies for every setting surface");
        Check(
            source.Contains("ManagedLogFile.AppendLine(") &&
            !source.Contains("File.AppendAllText("),
            "capsblink delegates sidecar logging and error reporting to the shared managed logger");
        Check(
            source.Contains("capsLockOn") &&
            source.Contains("indicators.LEDflags | Flags.KEYBOARD_CAPS_LOCK_ON") &&
            source.Contains("SynchronizeCapsIndicator(device"),
            "capsblink forces a logically-on Caps Lock indicator back on and restores it at cleanup");
    }

    static void TestAsusBlinkGuardrails(string repositoryRoot)
    {
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseArgs",
                    new object[] { new[] { "--mic-state", "--no-tray" } });
            },
            "asusblink rejects a missing option value");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseArgs",
                    new object[] { new[] { "--unknown", "1" } });
            },
            "asusblink rejects unknown options");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseTimeMs",
                    new object[] { "not-a-time", "test-time" });
            },
            "asusblink rejects malformed time values");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseTimeMs",
                    new object[] { "0.5ms", "test-time" });
            },
            "asusblink rejects sub-millisecond values instead of truncating to an infinite duration");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseArgs",
                    new object[] { new[] { "--set", "Options.run-at-startup=maybe" } });
            },
            "asusblink rejects invalid typed persistent booleans");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseArgs",
                    new object[] { new[] { "--set", "Settings.ShowTrayIcon=" } });
            },
            "asusblink rejects empty canonical booleans consistently with other managed residents");

        object parsedAliases = InvokePrivate(
            typeof(Program),
            "ParseArgs",
            new object[]
            {
                new[]
                {
                    "--startup",
                    "--flat-menu",
                    "--set", "Options.error-retry=5"
                }
            });
        var persistentAliases = (IDictionary)parsedAliases.GetType()
            .GetField("PersistentOptions")
            .GetValue(parsedAliases);
        Check(
            (string)persistentAliases["RunAtStartup"] == "true" &&
            (string)persistentAliases["ShowMenuAsDropdown"] == "false" &&
            (string)persistentAliases["ErrorRetry"] == "5",
            "asusblink command-line aliases share canonical persistent settings");

        var defaults = (IDictionary)InvokePrivate(
            typeof(Program),
            "DefaultSettings",
            new object[0]);
        Check(
            (string)defaults["MicInterval"] == "0" &&
            (string)defaults["MicDuration"] == "once" &&
            (string)defaults["KeyboardInterval"] == "0" &&
            (string)defaults["KeyboardDuration"] == "once",
            "asusblink canonical defaults preserve legacy one-cycle event behavior");
        Check(
            (string)InvokePrivate(
                typeof(Program),
                "CanonicalizeOptionValue",
                new object[] { "mic-duration", "ONCE" }) == "once",
            "asusblink exposes one-cycle duration consistently through typed settings");
        var invalidProfile = (IDictionary)InvokePrivate(
            typeof(Program),
            "DefaultSettings",
            new object[0]);
        invalidProfile["ErrorAction"] = "pause";
        invalidProfile["ShowTrayIcon"] = "false";
        CheckThrows<InvalidDataException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "BuildRuntimeProfile",
                    new object[] { invalidProfile });
            },
            "asusblink rejects cross-setting combinations before they can become a runtime profile");

        string path = Path.Combine(
            repositoryRoot,
            "dependencies",
            "asusblink",
            "asusblink_app.cs");
        string source = File.ReadAllText(path);
        string startupSource = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "dependencies",
            "managed_startup_shortcut.cs"));
        string managedIniSource = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "dependencies",
            "managed_ini.cs"));
        string traySource = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "dependencies",
            "managed_tray.cs"));
        string namedObjectsSource = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "dependencies",
            "managed_named_objects.cs"));
        string managedLoggingSource = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "dependencies",
            "managed_logging.cs"));
        string windowsBuild = File.ReadAllText(Path.Combine(
            repositoryRoot,
            ".github",
            "scripts",
            "build-windows.cmd"));
        string asusProductBuild = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "legacy",
            "asusblink",
            "BuildAsusBlink.cmd"));
        int asusBuildStart = windowsBuild.IndexOf(
            "\n:BuildAsusBlink",
            StringComparison.OrdinalIgnoreCase);
        int asusBuildEnd = windowsBuild.IndexOf(
            "\n:BuildCapsBlink",
            Math.Max(0, asusBuildStart + 1),
            StringComparison.OrdinalIgnoreCase);
        string asusBuildSection = asusBuildStart >= 0 && asusBuildEnd > asusBuildStart
            ? windowsBuild.Substring(asusBuildStart, asusBuildEnd - asusBuildStart)
            : "";
        int parsePosition = source.IndexOf(
            "commandLine = ParseArgs(args);",
            StringComparison.Ordinal);
        int loadPosition = source.IndexOf(
            "RuntimeProfile initialProfile = LoadRuntimeProfile();",
            StringComparison.Ordinal);
        Check(
            parsePosition >= 0 && loadPosition >= 0 && parsePosition < loadPosition,
            "asusblink validates command-line syntax before creating/loading the INI");
        Check(
            source.Contains("ManagedIniFile.EnsureExists(iniFile)") &&
            managedIniSource.Contains("Guid.NewGuid().ToString(\"N\") + \".tmp\"") &&
            managedIniSource.Contains("File.Move(temporaryPath, spec.FilePath)") &&
            managedIniSource.Contains("ManagedNamedObjects.CurrentUserScopedName("),
            "asusblink creates its default INI atomically under cross-session startup races");
        Check(
            !source.Contains("Environment.Exit(") &&
            !source.Contains("Environment.TickCount"),
            "asusblink uses cooperative shutdown and monotonic duration timing");
        Check(
            source.Contains("WaitForRunningTasks(Timeout.Infinite)") &&
            source.Contains("lock (acpiLock)"),
            "asusblink drains workers before synchronized ACPI cleanup");
        Check(
            source.Contains("static bool CreateTray()") &&
            source.Contains("Application.ExitThread()"),
            "asusblink reports tray creation failure and exits its message loop cooperatively");
        Check(
            source.Contains("ManagedStartupShortcut.IsInstalled(startupShortcut") &&
            source.Contains("ManagedStartupShortcut.SetDesiredState(") &&
            startupSource.Contains("Environment.SpecialFolder.Startup") &&
            startupSource.Contains("MatchesExactLaunch") &&
            startupSource.Contains("StableIdentityHash(identity)"),
            "asusblink consumes the shared profile-scoped Startup shortcut lifecycle");
        Check(
            source.Contains("static readonly string startupArguments = \"\"") &&
            source.Contains("IdentityPath = iniPath") &&
            source.Contains("ManagedStartupShortcut.HasOwnedTarget(") &&
            startupSource.Contains("WorkingDirectory = ReadShortcutProperty") &&
            startupSource.Contains("String.Equals(info.Arguments ?? \"\", spec.Arguments ?? \"\"") &&
            startupSource.Contains("Refusing to replace a same-named Startup shortcut") &&
            startupSource.Contains("File.Replace(temporary, destination, null)"),
            "asusblink Startup shortcut is INI-scoped, metadata-validated, target-safe, and atomically replaced");
        Check(
            source.Contains("PersistSettingsAtomically(commandLine.Settings)") &&
            source.Contains("SaveIniOptions(settings)") &&
            source.Contains("CommitIniCoupledState(") &&
            source.Contains("previousSettings") &&
            source.Contains("delegate(out string rollbackError)"),
            "asusblink commits command-line settings through the shared crash-consistent Startup transaction");
        Check(
            source.Contains("StartupKey + \"=false\"") &&
            source.Contains("ShowTrayKey + \"=true\"") &&
            source.Contains("DropdownKey + \"=true\"") &&
            source.Contains("settings[StartupKey]") &&
            source.Contains("settings[DropdownKey]"),
            "asusblink common INI settings are exposed through CLI and tray");
        Check(
            !startupSource.Contains("HKEY_CURRENT_USER") &&
            !startupSource.Contains("TaskScheduler") &&
            startupSource.Contains("Environment.SpecialFolder.Startup"),
            "shared managed Startup lifecycle uses only the per-user Startup folder");
        Check(
            source.Contains("SaveIniOption(ErrorLogKey, iniValue)") &&
            source.Contains("ManagedIniFile.SaveSectionBatch(iniFile, settings, out error)") &&
            managedIniSource.Contains("File.Replace(temporaryPath, spec.FilePath, null)") &&
            source.Contains("Log path changed and saved to:"),
            "asusblink tray log selection persists through the same INI used on restart");
        Check(
            source.Contains("ManagedLogFile.AppendLine(") &&
            !source.Contains("File.AppendAllText(") &&
            managedLoggingSource.Contains("ManagedNamedObjects.MachineScopedName(") &&
            managedLoggingSource.Contains("FileShare.Read"),
            "asusblink delegates cross-process UTF-8 sidecar logging to the shared managed logger");
        Check(
            source.Contains("ManagedTrayBaseline.CreateNotifyIcon(") &&
            source.Contains("ManagedTrayBaseline.AppendHeader(") &&
            traySource.Contains("NotifyIconTooltipLimit = 63") &&
            traySource.Contains("text.Substring(0, NotifyIconTooltipLimit)"),
            "asusblink consumes the shared managed tray header and length-safe hover tooltip lifecycle");
        Check(
            source.Contains("ProfileMutexName()") &&
            source.Contains("ReloadEventName()") &&
            source.Contains("ExitEventName()") &&
            source.Contains("ReloadRuntimeProfile()") &&
            source.Contains("RunResidentLoop()") &&
            source.Contains("ManagedStartupShortcut.StableIdentityHash(iniPath)") &&
            source.Contains("ManagedNamedObjects.CurrentUserScopedName(") &&
            namedObjectsSource.Contains("@\"Global\\AIProjects.\"") &&
            source.Contains("new MenuItem(\"Reload settings\""),
            "asusblink supports user-scoped, cross-session profile reload and exit control");
        Check(
            source.Contains("BuildRuntimeProfile(prospective)") &&
            source.Contains("ReadEffectiveIniOptions(out ignoredLegacy)") &&
            source.Contains("Duplicate semantic [Settings] key:"),
            "asusblink validates the complete prospective profile before persistent mutation");
        Check(
            asusBuildSection.Contains("BuildAsusBlink.cmd") &&
            asusProductBuild.Contains("/warnaserror+") &&
            asusProductBuild.Contains("dependencies\\managed_named_objects.cs") &&
            asusProductBuild.Contains("dependencies\\managed_logging.cs") &&
            asusProductBuild.Contains("dependencies\\managed_ini.cs") &&
            asusProductBuild.Contains("dependencies\\managed_startup_shortcut.cs") &&
            asusProductBuild.Contains("dependencies\\managed_tray.cs") &&
            asusProductBuild.Contains("dependencies\\asusblink\\asusblink_app.cs") &&
            asusProductBuild.Contains("%ROOT%asusblink.cs"),
            "asusblink build guardrails pin every shared dependency inside its own build section");
        Check(
            AIProjects.Dependencies.ManagedTrayBaseline.NormalizeTooltip(
                new string('x', 80),
                "fallback").Length == 63,
            "shared managed tray tooltip enforces the WinForms length limit at runtime");
        Check(
            AIProjects.Dependencies.ManagedTrayBaseline.NormalizeTooltip("\0 state", "Product") == "state" &&
            AIProjects.Dependencies.ManagedTrayBaseline.NormalizeTooltip("\0", " Product ") == "Product",
            "shared managed tooltip sanitizes embedded NULs and trims its fallback");
        Check(
            AIProjects.Dependencies.ManagedTrayBaseline.NormalizeTooltip(
                new string('x', 62) + "\uD83D\uDE00", "Product") == new string('x', 62),
            "shared managed tooltip truncation preserves complete Unicode pairs");
        Check(
            source.Contains("if (bytesReturned < sizeof(int))") &&
            source.Contains("ACPI returned a truncated response"),
            "asusblink rejects truncated ACPI firmware responses");
    }

    static void TestAlwaysUiAccessSource(string repositoryRoot)
    {
        string path = Path.Combine(
            repositoryRoot,
            "legacy",
            "WindhawkMods",
            "local@always-uiaccess.wh.cpp");
        string source = File.ReadAllText(path);
        Check(
            source.Contains("bytesRead != sizeof(*request)") &&
            source.Contains("ValidateUIAccessRequest(request.get(), &response)") &&
            source.Contains("RequestEnvironmentIsValid(request)"),
            "Always UIAccess validates the complete pipe request");
        Check(
            source.Contains("Requested path does not match the target process image.") &&
            source.Contains("Target process is not a child of the requesting process."),
            "Always UIAccess binds token patches to the real child image");
        Check(
            source.Contains("windowsDirectoryBuffer.resize(windowsDirectoryLength)") &&
            !source.Contains("WCHAR windowsDirectory[MAX_PATH]"),
            "Always UIAccess native System32 validation supports long Windows paths");
        Check(
            source.Contains("CopyEnvironmentToRequest(") &&
            !source.Contains("CreateEnvironmentBlock"),
            "Always UIAccess forwards the caller environment instead of inheriting the service environment");
        Check(
            source.Contains("PipeReadWithStop(") &&
            source.Contains("PipeWriteWithStop(") &&
            source.Contains("CancelIoEx(pipe, overlapped)"),
            "Always UIAccess uses cancellable overlapped pipe I/O");
        Check(
            source.Contains("CloseDuplicatedHandleInClient(") &&
            source.Contains("response.processHandle"),
            "Always UIAccess cleans up duplicated client handles on failed delivery");
        Check(
            source.Contains("BrokerCanPreserveCreateProcessRequest(") &&
            source.Contains("actualCreationFlags |= CREATE_SUSPENDED") &&
            source.Contains("ResumeThread(lpProcessInformation->hThread)"),
            "Always UIAccess preserves unsupported CreateProcess semantics through suspended in-process creation");
        Check(
            source.Contains("WaitForSingleObject(g_pipeThread, INFINITE)") &&
            source.Contains("WaitForSingleObject(g_autoTopmostThread, INFINITE)"),
            "Always UIAccess drains worker threads before unload");
    }

    static void TestDnsSafetyDefaults(string repositoryRoot)
    {
        var defaults = (IDictionary)InvokePrivate(
            typeof(DNSAutoUpdate),
            "DefaultSettingValues",
            new object[0]);
        Check(
            defaults["Enabled"].ToString() == "false" &&
            defaults["WhatIf"].ToString() == "true" &&
            defaults["NoRootRecord"].ToString() == "true",
            "DNS first-run defaults are inert and do not implicitly own the zone root");

        object options = InvokePrivate(
            typeof(DNSAutoUpdate),
            "OptionsFromValues",
            new object[] { defaults });
        var managed = ((IEnumerable)InvokePrivate(
            typeof(DNSAutoUpdate),
            "BuildManagedRecordNames",
            new[] { options })).Cast<object>().ToArray();
        Check(
            managed.Length == 0,
            "DNS disabled default profile has no implicit managed owner names");

        string source = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "dependencies",
            "DNSAutoUpdate",
            "dns_auto_update_app.cs"));
        Check(
            source.Contains("ManagedLogFile.AppendLine(") &&
            !source.Contains("RotateLogIfNeeded(") &&
            !source.Contains("File.AppendAllText("),
            "DNSAutoUpdate delegates locked rotation and append to the shared managed logger");

        CheckDnsIpNormalization("192.168.1.10", true, "192.168.1.10");
        CheckDnsIpNormalization("192.168.1", false, "");
        CheckDnsIpNormalization("0xC0A80101", false, "");
        CheckDnsIpNormalization("224.0.0.1", false, "");
        CheckDnsIpNormalization("127.0.0.1", false, "");
    }

    static void CheckDnsIpNormalization(string input, bool expected, string normalized)
    {
        object[] invocation = { input, null };
        bool actual = (bool)InvokePrivate(
            typeof(DNSAutoUpdate),
            "TryNormalizeUsableIPv4",
            invocation);
        Check(
            actual == expected && (invocation[1] ?? "").ToString() == normalized,
            "DNS IPv4 validation is strict for " + input);
    }

    static void TestManagedStartupShortcutLifecycle()
    {
        var enableOrder = new List<string>();
        string coupledError;
        bool enabled = AIProjects.Dependencies.ManagedStartupShortcut
            .ExecuteCrashConsistentCoupledState(
                true,
                false,
                delegate(bool state, out string mutationError)
                {
                    mutationError = null;
                    enableOrder.Add("shortcut=" + state.ToString());
                    return true;
                },
                delegate(out string persistenceError)
                {
                    persistenceError = null;
                    enableOrder.Add("persist");
                    return true;
                },
                delegate(out string restoreError)
                {
                    restoreError = null;
                    enableOrder.Add("restore");
                    return true;
                },
                out coupledError);
        Check(
            enabled && String.IsNullOrEmpty(coupledError) &&
            enableOrder.SequenceEqual(new[] { "shortcut=True", "persist" }),
            "shared Startup/INI transaction installs before persisting enabled state");

        var disableOrder = new List<string>();
        bool disabled = AIProjects.Dependencies.ManagedStartupShortcut
            .ExecuteCrashConsistentCoupledState(
                false,
                true,
                delegate(bool state, out string mutationError)
                {
                    mutationError = null;
                    disableOrder.Add("shortcut=" + state.ToString());
                    return true;
                },
                delegate(out string persistenceError)
                {
                    persistenceError = null;
                    disableOrder.Add("persist");
                    return true;
                },
                delegate(out string restoreError)
                {
                    restoreError = null;
                    disableOrder.Add("restore");
                    return true;
                },
                out coupledError);
        Check(
            disabled && String.IsNullOrEmpty(coupledError) &&
            disableOrder.SequenceEqual(new[] { "persist", "shortcut=False" }),
            "shared Startup/INI transaction persists disabled state before removing its launch path");

        var enableFailureOrder = new List<string>();
        bool rejectedEnable = AIProjects.Dependencies.ManagedStartupShortcut
            .ExecuteCrashConsistentCoupledState(
                true,
                false,
                delegate(bool state, out string mutationError)
                {
                    mutationError = null;
                    enableFailureOrder.Add("shortcut=" + state.ToString());
                    return true;
                },
                delegate(out string persistenceError)
                {
                    persistenceError = "injected persistence failure";
                    enableFailureOrder.Add("persist");
                    return false;
                },
                delegate(out string restoreError)
                {
                    restoreError = null;
                    enableFailureOrder.Add("restore");
                    return true;
                },
                out coupledError);
        Check(
            !rejectedEnable && !String.IsNullOrEmpty(coupledError) &&
            enableFailureOrder.SequenceEqual(
                new[] { "shortcut=True", "persist", "shortcut=False" }),
            "shared Startup/INI transaction rolls back an enabled shortcut after an atomic INI failure");

        var disableFailureOrder = new List<string>();
        bool rejectedDisable = AIProjects.Dependencies.ManagedStartupShortcut
            .ExecuteCrashConsistentCoupledState(
                false,
                true,
                delegate(bool state, out string mutationError)
                {
                    disableFailureOrder.Add("shortcut=" + state.ToString());
                    if (!state)
                    {
                        mutationError = "injected shortcut failure";
                        return false;
                    }
                    mutationError = null;
                    return true;
                },
                delegate(out string persistenceError)
                {
                    persistenceError = null;
                    disableFailureOrder.Add("persist");
                    return true;
                },
                delegate(out string restoreError)
                {
                    restoreError = null;
                    disableFailureOrder.Add("restore");
                    return true;
                },
                out coupledError);
        Check(
            !rejectedDisable && !String.IsNullOrEmpty(coupledError) &&
            disableFailureOrder.SequenceEqual(
                new[] { "persist", "shortcut=False", "shortcut=True", "restore" }),
            "shared Startup/INI transaction recreates the launch path before restoring enabled configuration");

        if (!allowStartupIntegration)
        {
            Console.WriteLine("skip - real Startup-folder integration (explicit --allow-startup-integration only); injected transaction tests remain enabled");
            return;
        }
        string unique = "AIProjectsManagedStartupTest-" + Guid.NewGuid().ToString("N");
        string executable = Assembly.GetExecutingAssembly().Location;
        var spec = new AIProjects.Dependencies.ManagedStartupShortcutSpec
        {
            ProductName = unique,
            ExecutablePath = executable,
            WorkingDirectory = Path.GetDirectoryName(executable),
            Arguments = "--managed-startup-test",
            IdentityPath = Path.Combine(Path.GetTempPath(), unique + ".ini"),
            LegacyFileName = unique + "-legacy.lnk"
        };

        bool previous;
        string error;
        try
        {
            bool persistenceObservedInstalled = false;
            bool installed = AIProjects.Dependencies.ManagedStartupShortcut.CommitIniCoupledState(
                spec,
                true,
                delegate(out bool configuredStartup, out string snapshotError)
                {
                    configuredStartup = false;
                    snapshotError = null;
                    return true;
                },
                delegate(out string persistenceError)
                {
                    persistenceError = null;
                    persistenceObservedInstalled =
                        AIProjects.Dependencies.ManagedStartupShortcut.IsInstalled(spec);
                    return true;
                },
                delegate(out string restoreError)
                {
                    restoreError = null;
                    return true;
                },
                out error);
            Check(
                installed && persistenceObservedInstalled &&
                AIProjects.Dependencies.ManagedStartupShortcut.IsInstalled(spec),
                "shared managed Startup/INI transaction holds the profile lock and installs exact launch metadata first");

            var migratedSpec = new AIProjects.Dependencies.ManagedStartupShortcutSpec
            {
                ProductName = spec.ProductName,
                ExecutablePath = spec.ExecutablePath,
                WorkingDirectory = spec.WorkingDirectory,
                Arguments = "",
                IdentityPath = spec.IdentityPath,
                LegacyFileName = spec.LegacyFileName
            };
            string migrationQueryError;
            Check(
                !AIProjects.Dependencies.ManagedStartupShortcut.IsInstalled(migratedSpec) &&
                AIProjects.Dependencies.ManagedStartupShortcut.HasOwnedTarget(
                    migratedSpec,
                    out migrationQueryError) &&
                String.IsNullOrEmpty(migrationQueryError),
                "shared managed Startup migration recognizes an owned target with stale arguments");

            var foreignSpec = new AIProjects.Dependencies.ManagedStartupShortcutSpec
            {
                ProductName = spec.ProductName,
                ExecutablePath = Path.Combine(Environment.SystemDirectory, "cmd.exe"),
                WorkingDirectory = Environment.SystemDirectory,
                Arguments = "",
                IdentityPath = spec.IdentityPath,
                LegacyFileName = spec.LegacyFileName
            };
            bool foreignPrevious;
            string foreignError;
            bool replaced = AIProjects.Dependencies.ManagedStartupShortcut.SetDesiredState(
                foreignSpec,
                true,
                out foreignPrevious,
                out foreignError);
            Check(
                !replaced &&
                !String.IsNullOrEmpty(foreignError) &&
                AIProjects.Dependencies.ManagedStartupShortcut.IsInstalled(spec),
                "shared managed Startup shortcut refuses a same-name foreign target");

            var otherIdentitySpec = new AIProjects.Dependencies.ManagedStartupShortcutSpec
            {
                ProductName = unique + "-other",
                ExecutablePath = executable,
                WorkingDirectory = Path.GetDirectoryName(executable),
                Arguments = "",
                IdentityPath = Path.Combine(Path.GetTempPath(), unique + "-other.ini")
            };
            using (AIProjects.Dependencies.ManagedStartupShortcutMutationLock wrongLock =
                AIProjects.Dependencies.ManagedStartupShortcut.AcquireMutationLock(spec))
            {
                string wrongLockError;
                bool usedWrongLock = AIProjects.Dependencies.ManagedStartupShortcut.SetDesiredStateUnderLock(
                    otherIdentitySpec,
                    true,
                    wrongLock,
                    out wrongLockError);
                Check(
                    !usedWrongLock &&
                    !String.IsNullOrEmpty(wrongLockError) &&
                    !AIProjects.Dependencies.ManagedStartupShortcut.IsInstalled(otherIdentitySpec),
                    "shared managed Startup shortcut rejects a lock from another profile");
            }

            bool removed = AIProjects.Dependencies.ManagedStartupShortcut.SetDesiredState(
                spec,
                false,
                out previous,
                out error);
            Check(
                removed && previous &&
                !AIProjects.Dependencies.ManagedStartupShortcut.IsInstalled(spec),
                "shared managed Startup shortcut removes its owned entry");
        }
        finally
        {
            AIProjects.Dependencies.ManagedStartupShortcut.SetDesiredState(
                spec,
                false,
                out previous,
                out error);
        }
    }

    static void TestManagedIniLifecycle()
    {
        string root = Path.Combine(
            Path.GetTempPath(),
            "AIProjectsManagedIniTest-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "settings.ini");
        var spec = new AIProjects.Dependencies.ManagedIniFileSpec
        {
            FilePath = path,
            SectionName = "Options",
            DefaultContents =
                "[Options]" + Environment.NewLine +
                "; preserve this comment" + Environment.NewLine +
                "Value=old" + Environment.NewLine +
                "Value=stale" + Environment.NewLine +
                "Other=keep" + Environment.NewLine
        };
        try
        {
            AIProjects.Dependencies.ManagedIniFile.EnsureExists(spec);
            Dictionary<string, string> initial =
                AIProjects.Dependencies.ManagedIniFile.LoadSection(spec);
            Check(
                initial["Value"] == "stale" && initial["Other"] == "keep",
                "shared managed INI loader reads the effective section values");

            string error;
            bool saved = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                spec,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    { "Value", "new" },
                    { "Added", "yes" }
                },
                out error);
            Dictionary<string, string> updated =
                AIProjects.Dependencies.ManagedIniFile.LoadSection(spec);
            string text = File.ReadAllText(path);
            Check(
                saved && updated["Value"] == "new" && updated["Added"] == "yes" &&
                text.Contains("; preserve this comment") &&
                text.Split(new[] { "\"Value\" =" }, StringSplitOptions.None).Length == 2,
                "shared managed INI batch preserves comments and removes duplicate keys atomically");

            Exception concurrentFailure = null;
            Thread writer = new Thread(new ThreadStart(delegate
            {
                try
                {
                    for (int i = 0; i < 50; ++i)
                    {
                        string concurrentError;
                        if (!AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                            spec,
                            new Dictionary<string, string>
                            {
                                { "Concurrent", i.ToString() }
                            },
                            out concurrentError))
                            throw new IOException(concurrentError);
                    }
                }
                catch (Exception ex)
                {
                    concurrentFailure = ex;
                }
            }));
            writer.Start();
            try
            {
                for (int i = 0; i < 50; ++i)
                    AIProjects.Dependencies.ManagedIniFile.LoadSection(spec);
            }
            catch (Exception ex)
            {
                concurrentFailure = ex;
            }
            writer.Join();
            Check(
                concurrentFailure == null,
                "shared managed INI serializes readers with atomic replacement");

            string beforeInvalid = File.ReadAllText(path);
            bool invalidUnicodeSaved = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                spec,
                new Dictionary<string, string> { { "Unicode", "\uD800" } },
                out error);
            Check(
                !invalidUnicodeSaved && !String.IsNullOrEmpty(error) &&
                    File.ReadAllText(path) == beforeInvalid,
                "shared managed INI rejects invalid Unicode without replacing existing settings");
            bool invalidSaved = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                spec,
                new Dictionary<string, string> { { "Bad=Key", "value" } },
                out error);
            Check(
                !invalidSaved && File.ReadAllText(path) == beforeInvalid,
                "shared managed INI rejects an invalid batch before mutation");

            bool paddedSaved = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                spec,
                new Dictionary<string, string> { { " Padded", "value" } },
                out error);
            bool commentSaved = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                spec,
                new Dictionary<string, string> { { ";Comment", "value" } },
                out error);
            Check(
                !paddedSaved && !commentSaved && File.ReadAllText(path) == beforeInvalid,
                "shared managed INI rejects keys that cannot round-trip");

            File.AppendAllText(path, "broken target line" + Environment.NewLine);
            string malformed = File.ReadAllText(path);
            bool repairedSilently = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                spec,
                new Dictionary<string, string> { { "Added", "again" } },
                out error);
            Check(
                !repairedSilently && File.ReadAllText(path) == malformed,
                "shared managed INI refuses to mutate a malformed target section");

            File.WriteAllText(path, beforeInvalid);
            spec.MaximumBytes = 1024;
            bool oversizedSaved = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(
                spec,
                new Dictionary<string, string> { { "Large", new string('x', 2048) } },
                out error);
            Check(
                !oversizedSaved && File.ReadAllText(path) == beforeInvalid,
                "shared managed INI rejects a resulting file over its byte limit");

            byte[] invalidPrefix = Encoding.ASCII.GetBytes("[Options]\r\nValue=");
            byte[] invalidUtf8 = new byte[invalidPrefix.Length + 2];
            Buffer.BlockCopy(invalidPrefix, 0, invalidUtf8, 0, invalidPrefix.Length);
            invalidUtf8[invalidPrefix.Length] = 0xC3;
            invalidUtf8[invalidPrefix.Length + 1] = 0x28;
            File.WriteAllBytes(path, invalidUtf8);
            CheckThrows<DecoderFallbackException>(
                delegate { AIProjects.Dependencies.ManagedIniFile.LoadSection(spec); },
                "shared managed INI rejects malformed UTF-8 instead of replacing bytes");

            string invalidDefaultPath = Path.Combine(root, "invalid-default.ini");
            var invalidDefaultSpec = new AIProjects.Dependencies.ManagedIniFileSpec
            {
                FilePath = invalidDefaultPath,
                SectionName = " Options ",
                DefaultContents = "[ Options ]" + Environment.NewLine + "Value=x"
            };
            CheckThrows<ArgumentException>(
                delegate { AIProjects.Dependencies.ManagedIniFile.EnsureExists(invalidDefaultSpec); },
                "shared managed INI rejects a non-canonical section specification");
            Check(
                !File.Exists(invalidDefaultPath),
                "invalid managed INI specifications have no filesystem side effect");
            invalidDefaultSpec.SectionName = "Options";
            invalidDefaultSpec.DefaultContents = "[Options]\r\nValue=\uD800";
            CheckThrows<EncoderFallbackException>(
                delegate { AIProjects.Dependencies.ManagedIniFile.EnsureExists(invalidDefaultSpec); },
                "shared managed INI rejects default text that cannot be encoded losslessly");
            Check(
                !File.Exists(invalidDefaultPath) && Directory.GetFiles(root, "*.tmp").Length == 0,
                "failed managed INI creation removes its temporary file");
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }

    static void TestManagedIniDialect(string repositoryRoot)
    {
        string directory = Path.Combine(Path.GetTempPath(), "AIProjectsIniDialect-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var spec = new AIProjects.Dependencies.ManagedIniFileSpec {
            FilePath = Path.Combine(directory, "settings.ini"),
            SectionName = "Options",
            DefaultContents = File.ReadAllText(Path.Combine(repositoryRoot, "tools", "fixtures", "ini-dialect.txt"))
        };
        try
        {
            var expected = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (string line in File.ReadAllLines(Path.Combine(repositoryRoot, "tools", "fixtures", "ini-dialect.expected")))
            {
                int separator = line.IndexOf('\t');
                if (separator < 1) throw new InvalidDataException("Malformed INI dialect expectation.");
                expected.Add(line.Substring(0, separator), line.Substring(separator + 1));
            }
            var loaded = AIProjects.Dependencies.ManagedIniFile.LoadSection(spec);
            Check(loaded.Count == expected.Count && expected.All(pair =>
                loaded.ContainsKey(pair.Key) && loaded[pair.Key] == pair.Value),
                "managed INI reads the same quote/comment/Unicode/path fixture as the native DesktopStub parser");

            expected["Duplicate"] = "new value";
            string error;
            bool saved = AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(spec, expected, out error);
            loaded = AIProjects.Dependencies.ManagedIniFile.LoadSection(spec);
            string written = File.ReadAllText(spec.FilePath);
            Check(saved && loaded.Count == expected.Count && expected.All(pair =>
                loaded.ContainsKey(pair.Key) && loaded[pair.Key] == pair.Value),
                "managed quoted INI writes round-trip significant spaces, comment characters, escapes, and Unicode");
            Check(written.Contains("\"QuotedPath\" = \"C:\\\\Users\\\\Amiya\\\\Desktop\\\\app.log\"") &&
                written.Contains("Value=unrelated") && written.Contains("; One input shared") &&
                written.Split(new[] { "\"Duplicate\" =" }, StringSplitOptions.None).Length == 2,
                "managed INI mutation matches native quoting and preserves unrelated sections while deduplicating quoted keys");

            // UTF-16 files remain readable and become canonical UTF-8 on edit.
            File.WriteAllText(spec.FilePath, written, Encoding.Unicode);
            loaded = AIProjects.Dependencies.ManagedIniFile.LoadSection(spec);
            Check(loaded["Unicode"] == expected["Unicode"] &&
                AIProjects.Dependencies.ManagedIniFile.SaveSectionBatch(spec,
                    new Dictionary<string, string> { { "Empty", "" } }, out error) &&
                File.ReadAllBytes(spec.FilePath).Take(3).SequenceEqual(new byte[] { 0xEF, 0xBB, 0xBF }),
                "managed INI migrates a UTF-16 quoted profile to UTF-8 BOM without data loss");
        }
        finally
        {
            try { Directory.Delete(directory, true); } catch { }
        }
    }

    static void TestManagedNamedObjectScoping()
    {
        string first = AIProjects.Dependencies.ManagedNamedObjects.CurrentUserScopedName(
            "Test.Profile",
            "identity-one");
        string second = AIProjects.Dependencies.ManagedNamedObjects.CurrentUserScopedName(
            "Test.Profile",
            "identity-one");
        string other = AIProjects.Dependencies.ManagedNamedObjects.CurrentUserScopedName(
            "Test.Profile",
            "identity-two");
        string machine = AIProjects.Dependencies.ManagedNamedObjects.MachineScopedName(
            "Test.Device",
            "device-zero");
        Check(
            first == second && first.StartsWith(@"Global\AIProjects.Test.Profile.", StringComparison.Ordinal) &&
            first != other && machine.StartsWith(@"Global\AIProjects.Test.Device.", StringComparison.Ordinal),
            "shared managed named objects are stable, identity-specific, and cross-session scoped");
        CheckThrows<ArgumentException>(
            delegate
            {
                AIProjects.Dependencies.ManagedNamedObjects.CurrentUserScopedName(
                    "bad\\component",
                    "identity");
            },
            "shared managed named objects reject namespace injection");
    }

    static void TestManagedLoggingLifecycle()
    {
        string root = Path.Combine(
            Path.GetTempPath(),
            "AIProjects-ManagedLoggingTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            string path = Path.Combine(root, "shared.log");
            var spec = new AIProjects.Dependencies.ManagedLogFileSpec
            {
                FilePath = path,
                LockWaitMilliseconds = 5000,
                WriteUtf8Bom = true
            };
            string error;
            bool first = AIProjects.Dependencies.ManagedLogFile.AppendLine(
                spec,
                "alpha",
                out error);
            bool second = AIProjects.Dependencies.ManagedLogFile.AppendLine(
                spec,
                "unicode-\u03b2",
                out error);
            byte[] initialBytes = File.ReadAllBytes(path);
            int bomCount = 0;
            for (int i = 0; i + 2 < initialBytes.Length; ++i)
            {
                if (initialBytes[i] == 0xEF && initialBytes[i + 1] == 0xBB &&
                    initialBytes[i + 2] == 0xBF)
                    ++bomCount;
            }
            string initialText = new UTF8Encoding(true, true).GetString(initialBytes);
            Check(
                first && second && String.IsNullOrEmpty(error) && bomCount == 1 &&
                initialText.Contains("alpha") && initialText.Contains("unicode-\u03b2"),
                "shared managed logger writes strict UTF-8 with one BOM across appends");

            bool sharedReaderAppend;
            using (var reader = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            {
                sharedReaderAppend = AIProjects.Dependencies.ManagedLogFile.AppendLine(
                    spec,
                    "while-reader-open",
                    out error);
            }
            Check(
                sharedReaderAppend,
                "shared managed logger permits diagnostic readers while appending");

            Exception concurrentFailure = null;
            var writers = new List<Thread>();
            for (int writerIndex = 0; writerIndex < 4; ++writerIndex)
            {
                int capturedWriter = writerIndex;
                var writer = new Thread(new ThreadStart(delegate
                {
                    try
                    {
                        for (int lineIndex = 0; lineIndex < 25; ++lineIndex)
                        {
                            string appendError;
                            if (!AIProjects.Dependencies.ManagedLogFile.AppendLine(
                                spec,
                                "writer-" + capturedWriter + "-" + lineIndex,
                                out appendError))
                                throw new IOException(appendError);
                        }
                    }
                    catch (Exception ex)
                    {
                        concurrentFailure = ex;
                    }
                }));
                writers.Add(writer);
                writer.Start();
            }
            foreach (Thread writer in writers)
                writer.Join();
            int writerLines = File.ReadAllLines(path, Encoding.UTF8)
                .Count(line => line.StartsWith("writer-", StringComparison.Ordinal));
            Check(
                concurrentFailure == null && writerLines == 100,
                "shared managed logger serializes concurrent writers without losing lines");

            string rotatingPath = Path.Combine(root, "rotating.log");
            var rotating = new AIProjects.Dependencies.ManagedLogFileSpec
            {
                FilePath = rotatingPath,
                LockWaitMilliseconds = 5000,
                MaximumBytes = 50,
                RetentionCount = 2,
                WriteUtf8Bom = true
            };
            bool rotateFirst = AIProjects.Dependencies.ManagedLogFile.AppendLine(
                rotating,
                new string('a', 32),
                out error);
            bool rotateSecond = AIProjects.Dependencies.ManagedLogFile.AppendLine(
                rotating,
                new string('b', 32),
                out error);
            Check(
                rotateFirst && rotateSecond && File.Exists(rotatingPath + ".1") &&
                File.ReadAllText(rotatingPath + ".1", Encoding.UTF8).Contains(new string('a', 32)) &&
                File.ReadAllText(rotatingPath, Encoding.UTF8).Contains(new string('b', 32)),
                "shared managed logger rotates under the same path-scoped mutation lock");

            string lockedPath = Path.Combine(root, "locked.log");
            string lockName = AIProjects.Dependencies.ManagedNamedObjects.MachineScopedName(
                "ManagedLog",
                Path.GetFullPath(lockedPath).ToUpperInvariant());
            bool timedOut = false;
            string timeoutError = null;
            using (var held = new Mutex(true, lockName))
            {
                var contender = new Thread(new ThreadStart(delegate
                {
                    timedOut = !AIProjects.Dependencies.ManagedLogFile.AppendLine(
                        new AIProjects.Dependencies.ManagedLogFileSpec
                        {
                            FilePath = lockedPath,
                            LockWaitMilliseconds = 0
                        },
                        "blocked",
                        out timeoutError);
                }));
                contender.Start();
                contender.Join();
                held.ReleaseMutex();
            }
            Check(
                timedOut && timeoutError != null && timeoutError.Contains("Timed out") &&
                !File.Exists(lockedPath),
                "shared managed logger reports bounded lock contention without writing");
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }

    static MethodInfo PrivateMethod(Type type, string name)
    {
        MethodInfo method = type.GetMethod(
            name,
            BindingFlags.NonPublic | BindingFlags.Static);
        if (method == null)
            throw new MissingMethodException(type.FullName, name);
        return method;
    }

    static int InvokeMainAndCapture(Type type, string[] arguments, out string output)
    {
        TextWriter oldOut = Console.Out;
        TextWriter oldError = Console.Error;
        var captured = new StringWriter();
        try
        {
            Console.SetOut(captured);
            Console.SetError(captured);
            return (int)PrivateMethod(type, "Main").Invoke(
                null,
                new object[] { arguments });
        }
        finally
        {
            Console.SetOut(oldOut);
            Console.SetError(oldError);
            output = captured.ToString();
        }
    }

    static object InvokePrivate(Type type, string name, object[] arguments)
    {
        try
        {
            return PrivateMethod(type, name).Invoke(null, arguments);
        }
        catch (TargetInvocationException ex)
        {
            throw ex.InnerException ?? ex;
        }
    }

    static void SetField(object target, string name, object value)
    {
        FieldInfo field = target.GetType().GetField(
            name,
            BindingFlags.Public | BindingFlags.Instance);
        if (field == null)
            throw new MissingFieldException(target.GetType().FullName, name);
        field.SetValue(target, value);
    }

    static void CheckThrows<T>(Action action, string name) where T : Exception
    {
        try
        {
            action();
            Check(false, name);
        }
        catch (T)
        {
            Check(true, name);
        }
    }

    static void Check(bool condition, string name)
    {
        if (condition)
        {
            Console.WriteLine("ok - " + name);
            return;
        }

        Console.Error.WriteLine("not ok - " + name);
        failures++;
    }
}

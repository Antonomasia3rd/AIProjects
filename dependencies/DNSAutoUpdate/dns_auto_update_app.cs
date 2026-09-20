using AIProjects.Dependencies;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

static class DNSAutoUpdate
{
    const string SettingsSection = "Settings";
    const string ZoneNameKey = "ZoneName";
    const string SubFolderKey = "SubFolder";
    const string ManagedRecordNameKey = "ManagedRecordName";
    const string NoRootRecordKey = "NoRootRecord";
    const string LogFileKey = "LogFile";
    const string MaxLogMegabytesKey = "MaxLogMegabytes";
    const string LogRetentionCountKey = "LogRetentionCount";
    const string SleepSecondsKey = "SleepSeconds";
    const string IncludeInterfaceAliasKey = "IncludeInterfaceAlias";
    const string ExcludeInterfaceAliasKey = "ExcludeInterfaceAlias";
    const string IncludeIPAddressKey = "IncludeIPAddress";
    const string IncludeUnpreferredKey = "IncludeUnpreferred";
    const string WhatIfKey = "WhatIf";
    const string ConfirmKey = "Confirm";
    const string EnabledKey = "Enabled";
    const string StartupKey = "RunAtStartup";
    const string ShowTrayKey = "ShowTrayIcon";
    const string DropdownKey = "ShowMenuAsDropdown";
    const string DefaultZoneName = "server.local";
    const string DefaultExcludedInterfaces = "Loopback*,vEthernet*,VMware*,VirtualBox*,Bluetooth*";

    static readonly string executablePath = GetExecutablePath();
    static readonly string executableDirectory = GetExecutableDirectory();
    static readonly string executableBaseName = GetExecutableBaseName();
    static readonly string iniPath = Path.Combine(executableDirectory, executableBaseName + ".ini");
    static readonly string profileIdentity = ManagedStartupShortcut.StableIdentityHash(iniPath);
    static readonly ManagedStartupShortcutSpec startupShortcut = BuildStartupShortcutSpec();
    static readonly ManagedIniFileSpec iniFile = BuildIniFileSpec();
    static readonly object settingsLock = new object();
    static readonly object statusLock = new object();

    static EventWaitHandle reloadEvent;
    static EventWaitHandle exitEvent;
    static EventWaitHandle runNowEvent;
    static Mutex profileMutex;
    static bool ownsProfileMutex;
    static Thread workerThread;
    static Exception workerFailure;
    static Options activeSettings;
    static int activeSettingsVersion;
    static int appliedSettingsVersion = -1;
    static string statusText = "Starting";
    static bool paused;
    static NotifyIcon tray;
    static System.Windows.Forms.Timer uiTimer;
    static MenuItem statusItem;
    static MenuItem pauseItem;
    static readonly Dictionary<string, MenuItem> settingMenuItems =
        new Dictionary<string, MenuItem>(StringComparer.OrdinalIgnoreCase);
    static bool appliedDropdown;

    sealed class Options
    {
        public string ZoneName;
        public List<string> SubFolder;
        public List<string> ManagedRecordName;
        public bool NoRootRecord;
        public string LogFile;
        public int MaxLogMegabytes;
        public int LogRetentionCount;
        public int SleepSeconds;
        public List<string> IncludeInterfaceAlias;
        public List<string> ExcludeInterfaceAlias;
        public List<string> IncludeIPAddress;
        public bool IncludeUnpreferred;
        public bool WhatIf;
        public bool Confirm;
        public bool Enabled;
        public bool RunAtStartup;
        public bool ShowTrayIcon;
        public bool ShowMenuAsDropdown;
        public bool ResidentMode;
    }

    sealed class ParsedCommandLine
    {
        public readonly Dictionary<string, string> Settings =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public bool ConfigureOnly;
        public bool Once;
        public bool ReloadRequested;
        public bool ExitRequested;
        public bool RunNowRequested;
    }

    [STAThread]
    static int Main(string[] args)
    {
        if (HasOption(args, IsHelpOption))
        {
            Usage();
            return 0;
        }
        if (HasOption(args, IsVersionOption))
        {
            Console.WriteLine(executableBaseName + " " + ProductVersion());
            return 0;
        }

        ParsedCommandLine commandLine;
        try
        {
            commandLine = ParseCommandLine(args);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR: " + ex.Message);
            Usage();
            return 2;
        }

        if (commandLine.ExitRequested)
            return SignalExistingControl(ExitEventName(), "exit") ? 0 : 1;
        if (commandLine.ReloadRequested)
            return SignalExistingControl(ReloadEventName(), "reload") ? 0 : 1;
        if (commandLine.RunNowRequested)
            return SignalExistingControl(RunNowEventName(), "run a cycle now") ? 0 : 1;

        try
        {
            return RunApplication(commandLine);
        }
        catch (Exception ex)
        {
            Diagnostic("Fatal error: " + ex);
            Console.Error.WriteLine("ERROR: " + ex.Message);
            return 1;
        }
        finally
        {
            RequestExit();
            DisposeUi();
            DrainWorker();
            DisposeControlObjects();
        }
    }

    static int RunApplication(ParsedCommandLine commandLine)
    {
        bool iniExisted = File.Exists(iniPath);
        if (!iniExisted && !commandLine.Settings.ContainsKey(StartupKey))
        {
            string startupQueryError;
            if (ManagedStartupShortcut.IsInstalled(startupShortcut, out startupQueryError))
                commandLine.Settings[StartupKey] = "true";
            else if (!String.IsNullOrEmpty(startupQueryError))
                throw new IOException("Could not inspect the existing Startup shortcut: " + startupQueryError);
        }

        if (!PersistSettingsAtomically(commandLine.Settings))
            throw new IOException("Could not persist the command-line setting batch.");

        Options initialSettings = LoadRuntimeSettings();
        ReconcileStartup(initialSettings.RunAtStartup);
        if (commandLine.ConfigureOnly)
        {
            Console.WriteLine("Settings saved to " + iniPath + ".");
            return 0;
        }

        InitializeControlObjects();
        if (!AcquireProfileMutex())
        {
            if (commandLine.Settings.Count != 0)
            {
                reloadEvent.Set();
                if (commandLine.Once)
                    runNowEvent.Set();
                Console.WriteLine(commandLine.Once
                    ? "Settings saved; the resident instance was asked to reload and run a cycle."
                    : "Settings saved; the resident instance was asked to reload.");
                return 0;
            }
            Console.Error.WriteLine("ERROR: Another " + executableBaseName +
                " instance is already using " + iniPath + ".");
            return 1;
        }

        if (commandLine.Once)
        {
            initialSettings.ResidentMode = false;
            return RunOneShot(initialSettings);
        }

        Console.CancelKeyPress += delegate(object sender, ConsoleCancelEventArgs e)
        {
            e.Cancel = true;
            RequestExit();
        };
        AppDomain.CurrentDomain.ProcessExit += delegate { RequestExit(); };

        initialSettings.ResidentMode = true;
        PublishSettings(initialSettings);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        ApplyUiSettings(initialSettings);

        workerThread = new Thread(new ThreadStart(delegate { ResidentWorker(initialSettings); }));
        workerThread.Name = "DNSAutoUpdate worker";
        workerThread.IsBackground = true;
        workerThread.Start();

        uiTimer = new System.Windows.Forms.Timer();
        uiTimer.Interval = 250;
        uiTimer.Tick += delegate { OnUiTimer(); };
        uiTimer.Start();
        Application.Run();

        RequestExit();
        DrainWorker();
        if (workerFailure != null)
            throw new InvalidOperationException("The DNS update worker failed.", workerFailure);
        return 0;
    }

    static ParsedCommandLine ParseCommandLine(string[] args)
    {
        var parsed = new ParsedCommandLine();
        for (int i = 0; i < args.Length; ++i)
        {
            string argument = args[i];
            if (argument == null || argument.Length < 2 ||
                (argument[0] != '-' && !argument.Equals("/?", StringComparison.Ordinal)))
                throw new ArgumentException("Unknown argument: " + argument);

            int prefix = argument.StartsWith("--", StringComparison.Ordinal) ? 2 : 1;
            string token = argument.Substring(prefix);
            int equals = token.IndexOf('=');
            string name = equals >= 0 ? token.Substring(0, equals) : token;
            string inlineValue = equals >= 0 ? token.Substring(equals + 1) : null;
            string key;
            string flagValue;

            if (inlineValue == null && TryMapFlag(name, out key, out flagValue))
            {
                parsed.Settings[key] = flagValue;
                continue;
            }

            if (name.Equals("once", StringComparison.OrdinalIgnoreCase))
            {
                RejectInlineValue(name, inlineValue);
                parsed.Once = true;
                continue;
            }
            if (name.Equals("configure-only", StringComparison.OrdinalIgnoreCase) ||
                name.Equals("reload", StringComparison.OrdinalIgnoreCase) ||
                name.Equals("exit", StringComparison.OrdinalIgnoreCase) ||
                name.Equals("run-now", StringComparison.OrdinalIgnoreCase))
            {
                RejectInlineValue(name, inlineValue);
                if (name.Equals("configure-only", StringComparison.OrdinalIgnoreCase))
                    parsed.ConfigureOnly = true;
                else if (name.Equals("reload", StringComparison.OrdinalIgnoreCase))
                    parsed.ReloadRequested = true;
                else if (name.Equals("exit", StringComparison.OrdinalIgnoreCase))
                    parsed.ExitRequested = true;
                else
                    parsed.RunNowRequested = true;
                continue;
            }

            string value;
            if (name.Equals("set", StringComparison.OrdinalIgnoreCase))
            {
                value = inlineValue ?? RequireNextValue(args, ref i, "--set");
                int assignment = value.IndexOf('=');
                if (assignment <= 0)
                    throw new ArgumentException("--set requires Settings.Key=Value.");
                string rawKey = value.Substring(0, assignment).Trim();
                string rawValue = value.Substring(assignment + 1);
                int sectionSeparator = rawKey.IndexOf('.');
                if (sectionSeparator >= 0)
                {
                    string section = rawKey.Substring(0, sectionSeparator);
                    if (!section.Equals(SettingsSection, StringComparison.OrdinalIgnoreCase))
                        throw new ArgumentException("--set supports only the [Settings] section.");
                    rawKey = rawKey.Substring(sectionSeparator + 1);
                }
                key = CanonicalSettingName(rawKey);
                if (key == null)
                    throw new ArgumentException("Unknown setting: " + rawKey);
                parsed.Settings[key] = NormalizeSettingValue(key, rawValue);
                continue;
            }

            key = DirectOptionSetting(name);
            if (key == null)
                throw new ArgumentException("Unknown option: " + argument);
            value = inlineValue ?? RequireNextValue(args, ref i, argument);
            parsed.Settings[key] = NormalizeSettingValue(key, value);
        }

        int controlActions = (parsed.ReloadRequested ? 1 : 0) +
            (parsed.ExitRequested ? 1 : 0) + (parsed.RunNowRequested ? 1 : 0);
        if (controlActions > 1)
            throw new ArgumentException("--reload, --run-now, and --exit are mutually exclusive.");
        if (parsed.ConfigureOnly && (controlActions != 0 || parsed.Once))
            throw new ArgumentException("--configure-only cannot be combined with resident or one-shot actions.");
        if (parsed.Once && controlActions != 0)
            throw new ArgumentException("--once cannot be combined with resident-control actions.");
        if (controlActions != 0 && parsed.Settings.Count != 0)
            throw new ArgumentException("Resident-control actions cannot be combined with setting changes.");
        return parsed;
    }

    static bool TryMapFlag(string name, out string key, out string value)
    {
        key = null;
        value = null;
        if (name.Equals("NoRootRecord", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("no-root-record", StringComparison.OrdinalIgnoreCase))
        {
            key = NoRootRecordKey;
            value = "true";
        }
        else if (name.Equals("root-record", StringComparison.OrdinalIgnoreCase))
        {
            key = NoRootRecordKey;
            value = "false";
        }
        else if (name.Equals("IncludeUnpreferred", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("include-unpreferred", StringComparison.OrdinalIgnoreCase))
        {
            key = IncludeUnpreferredKey;
            value = "true";
        }
        else if (name.Equals("preferred-only", StringComparison.OrdinalIgnoreCase))
        {
            key = IncludeUnpreferredKey;
            value = "false";
        }
        else if (name.Equals("WhatIf", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("what-if", StringComparison.OrdinalIgnoreCase))
        {
            key = WhatIfKey;
            value = "true";
        }
        else if (name.Equals("apply", StringComparison.OrdinalIgnoreCase))
        {
            key = WhatIfKey;
            value = "false";
        }
        else if (name.Equals("Confirm", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("confirm", StringComparison.OrdinalIgnoreCase))
        {
            key = ConfirmKey;
            value = "true";
        }
        else if (name.Equals("no-confirm", StringComparison.OrdinalIgnoreCase))
        {
            key = ConfirmKey;
            value = "false";
        }
        else if (name.Equals("enabled", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("enable", StringComparison.OrdinalIgnoreCase))
        {
            key = EnabledKey;
            value = "true";
        }
        else if (name.Equals("disabled", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("disable", StringComparison.OrdinalIgnoreCase))
        {
            key = EnabledKey;
            value = "false";
        }
        else if (name.Equals("startup", StringComparison.OrdinalIgnoreCase))
        {
            key = StartupKey;
            value = "true";
        }
        else if (name.Equals("no-startup", StringComparison.OrdinalIgnoreCase))
        {
            key = StartupKey;
            value = "false";
        }
        else if (name.Equals("tray", StringComparison.OrdinalIgnoreCase))
        {
            key = ShowTrayKey;
            value = "true";
        }
        else if (name.Equals("no-tray", StringComparison.OrdinalIgnoreCase))
        {
            key = ShowTrayKey;
            value = "false";
        }
        else if (name.Equals("dropdown", StringComparison.OrdinalIgnoreCase))
        {
            key = DropdownKey;
            value = "true";
        }
        else if (name.Equals("flat-menu", StringComparison.OrdinalIgnoreCase))
        {
            key = DropdownKey;
            value = "false";
        }
        return key != null;
    }

    static string DirectOptionSetting(string name)
    {
        string key = CanonicalSettingName(name);
        return key;
    }

    static string CanonicalSettingName(string value)
    {
        if (MatchesSetting(value, ZoneNameKey, "zone-name")) return ZoneNameKey;
        if (MatchesSetting(value, SubFolderKey, "sub-folder")) return SubFolderKey;
        if (MatchesSetting(value, ManagedRecordNameKey, "managed-record-name")) return ManagedRecordNameKey;
        if (MatchesSetting(value, NoRootRecordKey, "no-root-record")) return NoRootRecordKey;
        if (MatchesSetting(value, LogFileKey, "log-file")) return LogFileKey;
        if (MatchesSetting(value, MaxLogMegabytesKey, "max-log-megabytes")) return MaxLogMegabytesKey;
        if (MatchesSetting(value, LogRetentionCountKey, "log-retention-count")) return LogRetentionCountKey;
        if (MatchesSetting(value, SleepSecondsKey, "sleep-seconds")) return SleepSecondsKey;
        if (MatchesSetting(value, IncludeInterfaceAliasKey, "include-interface-alias")) return IncludeInterfaceAliasKey;
        if (MatchesSetting(value, ExcludeInterfaceAliasKey, "exclude-interface-alias")) return ExcludeInterfaceAliasKey;
        if (MatchesSetting(value, IncludeIPAddressKey, "include-ip-address")) return IncludeIPAddressKey;
        if (MatchesSetting(value, IncludeUnpreferredKey, "include-unpreferred")) return IncludeUnpreferredKey;
        if (MatchesSetting(value, WhatIfKey, "what-if")) return WhatIfKey;
        if (MatchesSetting(value, ConfirmKey, "confirm")) return ConfirmKey;
        if (MatchesSetting(value, EnabledKey, "enabled")) return EnabledKey;
        if (MatchesSetting(value, StartupKey, "run-at-startup")) return StartupKey;
        if (MatchesSetting(value, ShowTrayKey, "show-tray-icon") ||
            String.Equals(value, "show-tray", StringComparison.OrdinalIgnoreCase)) return ShowTrayKey;
        if (MatchesSetting(value, DropdownKey, "show-menu-as-dropdown")) return DropdownKey;
        return null;
    }

    static bool MatchesSetting(string value, string canonical, string alias)
    {
        return String.Equals(value, canonical, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, alias, StringComparison.OrdinalIgnoreCase);
    }

    static void Usage()
    {
        Console.WriteLine("Usage: " + executableBaseName + ".exe [settings | action]");
        Console.WriteLine();
        Console.WriteLine("Persistent DNS settings (legacy -PascalName aliases are retained):");
        Console.WriteLine("  --zone-name ZONE              --sub-folder NAME[,NAME]");
        Console.WriteLine("  --managed-record-name NAME[,NAME]");
        Console.WriteLine("  --no-root-record | --root-record");
        Console.WriteLine("  --log-file PATH               --max-log-megabytes N");
        Console.WriteLine("  --log-retention-count N       --sleep-seconds N");
        Console.WriteLine("  --include-interface-alias PATTERN[,PATTERN]");
        Console.WriteLine("  --exclude-interface-alias PATTERN[,PATTERN]");
        Console.WriteLine("  --include-ip-address IP[,IP]");
        Console.WriteLine("  --include-unpreferred | --preferred-only");
        Console.WriteLine("  --enabled | --disabled        --what-if | --apply");
        Console.WriteLine("  --confirm | --no-confirm");
        Console.WriteLine();
        Console.WriteLine("Persistent app settings:");
        Console.WriteLine("  --startup | --no-startup      --tray | --no-tray");
        Console.WriteLine("  --dropdown | --flat-menu      --set Settings.Key=Value");
        Console.WriteLine("  --configure-only              Save without reading or changing DNS");
        Console.WriteLine();
        Console.WriteLine("Actions: --once | --run-now | --reload | --exit");
        Console.WriteLine("Information: --help | --version");
        Console.WriteLine("Configuration: " + iniPath);
        Console.WriteLine("Startup: per-user shell:startup shortcut only");
        Console.WriteLine("Requires dnscmd.exe and permission to update the DNS zone.");
    }

    static string NormalizeSettingValue(string key, string value)
    {
        if (value == null || value.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0)
            throw new ArgumentException(key + " contains an invalid value.");
        string trimmed = value.Trim();
        if (key.Equals(ZoneNameKey, StringComparison.OrdinalIgnoreCase))
        {
            ValidateDnsToken(trimmed, ZoneNameKey, false);
            return trimmed;
        }
        if (key.Equals(SubFolderKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(ManagedRecordNameKey, StringComparison.OrdinalIgnoreCase))
        {
            bool allowRoot = true;
            return NormalizeListValue(trimmed, delegate(string item)
            {
                ValidateDnsToken(item, key, allowRoot);
            });
        }
        if (key.Equals(LogFileKey, StringComparison.OrdinalIgnoreCase))
        {
            if (trimmed.Length == 0 || trimmed.Length > 32767 ||
                trimmed.IndexOfAny(Path.GetInvalidPathChars()) >= 0)
                throw new ArgumentException(LogFileKey + " must be a non-empty valid path.");
            return trimmed;
        }
        if (key.Equals(MaxLogMegabytesKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeInteger(trimmed, key, 0, 1048576);
        if (key.Equals(LogRetentionCountKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeInteger(trimmed, key, 0, 100);
        if (key.Equals(SleepSecondsKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeInteger(trimmed, key, 1, 86400);
        if (key.Equals(IncludeInterfaceAliasKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(ExcludeInterfaceAliasKey, StringComparison.OrdinalIgnoreCase))
        {
            return NormalizeListValue(trimmed, delegate(string item)
            {
                if (item.Length > 1024)
                    throw new ArgumentException(key + " contains an overlong wildcard pattern.");
            });
        }
        if (key.Equals(IncludeIPAddressKey, StringComparison.OrdinalIgnoreCase))
        {
            return NormalizeListValue(trimmed, delegate(string item)
            {
                string normalized;
                if (!TryNormalizeUsableIPv4(item, out normalized))
                    throw new ArgumentException("Invalid or unusable IPv4 address for " + key + ": " + item);
            }, true);
        }
        if (key.Equals(NoRootRecordKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(IncludeUnpreferredKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(WhatIfKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(ConfirmKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(EnabledKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(StartupKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(ShowTrayKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(DropdownKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeBoolean(trimmed, key);
        throw new ArgumentException("Unknown setting: " + key);
    }

    static string NormalizeInteger(string value, string key, int minimum, int maximum)
    {
        int number;
        if (!Int32.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out number) ||
            number < minimum || number > maximum)
            throw new ArgumentException(key + " must be an integer from " + minimum + " through " + maximum + ".");
        return number.ToString(CultureInfo.InvariantCulture);
    }

    static string NormalizeBoolean(string value, string key)
    {
        if (value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("on", StringComparison.OrdinalIgnoreCase) || value == "1")
            return "true";
        if (value.Equals("false", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("no", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("off", StringComparison.OrdinalIgnoreCase) || value == "0")
            return "false";
        throw new ArgumentException(key + " must be true or false.");
    }

    static string NormalizeListValue(string value, Action<string> validate)
    {
        return NormalizeListValue(value, validate, false);
    }

    static string NormalizeListValue(string value, Action<string> validate, bool normalizeIp)
    {
        var normalized = new List<string>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string raw in (value ?? "").Split(','))
        {
            string item = raw.Trim();
            if (item.Length == 0)
                continue;
            validate(item);
            if (normalizeIp)
            {
                string ip;
                TryNormalizeUsableIPv4(item, out ip);
                item = ip;
            }
            if (seen.Add(item))
                normalized.Add(item);
        }
        return String.Join(",", normalized);
    }

    static Dictionary<string, string> DefaultSettingValues()
    {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { ZoneNameKey, DefaultZoneName },
            { SubFolderKey, "" },
            { ManagedRecordNameKey, "" },
            { NoRootRecordKey, "true" },
            { LogFileKey, executableBaseName + ".log" },
            { MaxLogMegabytesKey, "10" },
            { LogRetentionCountKey, "5" },
            { SleepSecondsKey, "20" },
            { IncludeInterfaceAliasKey, "" },
            { ExcludeInterfaceAliasKey, DefaultExcludedInterfaces },
            { IncludeIPAddressKey, "" },
            { IncludeUnpreferredKey, "false" },
            { WhatIfKey, "true" },
            { ConfirmKey, "false" },
            { EnabledKey, "false" },
            { StartupKey, "false" },
            { ShowTrayKey, "true" },
            { DropdownKey, "true" }
        };
    }

    static Options LoadRuntimeSettings()
    {
        return OptionsFromValues(LoadEffectiveSettingValues());
    }

    static Dictionary<string, string> LoadEffectiveSettingValues()
    {
        Dictionary<string, string> values = DefaultSettingValues();
        foreach (KeyValuePair<string, string> rawSetting in ManagedIniFile.LoadSection(iniFile))
        {
            string key = CanonicalSettingName(rawSetting.Key);
            if (key == null)
                throw new InvalidDataException("Unknown [Settings] key: " + rawSetting.Key);
            try
            {
                values[key] = NormalizeSettingValue(key, rawSetting.Value);
            }
            catch (ArgumentException ex)
            {
                throw new InvalidDataException(
                    "Invalid [Settings] " + rawSetting.Key + ": " + ex.Message,
                    ex);
            }
        }
        return values;
    }

    static Options OptionsFromValues(IDictionary<string, string> values)
    {
        var options = new Options
        {
            ZoneName = values[ZoneNameKey],
            SubFolder = SplitList(values[SubFolderKey]),
            ManagedRecordName = SplitList(values[ManagedRecordNameKey]),
            NoRootRecord = IsTrue(values[NoRootRecordKey]),
            LogFile = ResolveModuleLocalPath(values[LogFileKey]),
            MaxLogMegabytes = Int32.Parse(values[MaxLogMegabytesKey], CultureInfo.InvariantCulture),
            LogRetentionCount = Int32.Parse(values[LogRetentionCountKey], CultureInfo.InvariantCulture),
            SleepSeconds = Int32.Parse(values[SleepSecondsKey], CultureInfo.InvariantCulture),
            IncludeInterfaceAlias = SplitList(values[IncludeInterfaceAliasKey]),
            ExcludeInterfaceAlias = SplitList(values[ExcludeInterfaceAliasKey]),
            IncludeIPAddress = SplitList(values[IncludeIPAddressKey]),
            IncludeUnpreferred = IsTrue(values[IncludeUnpreferredKey]),
            WhatIf = IsTrue(values[WhatIfKey]),
            Confirm = IsTrue(values[ConfirmKey]),
            Enabled = IsTrue(values[EnabledKey]),
            RunAtStartup = IsTrue(values[StartupKey]),
            ShowTrayIcon = IsTrue(values[ShowTrayKey]),
            ShowMenuAsDropdown = IsTrue(values[DropdownKey])
        };
        if (options.Enabled)
        {
            ValidateAndBuildManagedRecordNames(options);
            if (!options.WhatIf && options.IncludeIPAddress.Count == 0 &&
                options.IncludeInterfaceAlias.Count == 0)
                throw new InvalidDataException(
                    "Enabled apply mode requires an explicit IncludeIPAddress or IncludeInterfaceAlias allowlist. Use * only when every eligible interface is intentionally authoritative.");
        }
        return options;
    }

    static List<string> SplitList(string value)
    {
        return (value ?? "").Split(',')
            .Select(item => item.Trim())
            .Where(item => item.Length != 0)
            .ToList();
    }

    static bool IsTrue(string value)
    {
        return String.Equals(value, "true", StringComparison.Ordinal);
    }

    static string ResolveModuleLocalPath(string value)
    {
        string path = value;
        if (!Path.IsPathRooted(path))
            path = Path.Combine(executableDirectory, path);
        return Path.GetFullPath(path);
    }

    static bool PersistSettingsAtomically(IDictionary<string, string> settings)
    {
        if (settings == null || settings.Count == 0)
        {
            ManagedIniFile.EnsureExists(iniFile);
            return true;
        }

        Dictionary<string, string> normalized;
        try
        {
            normalized = NormalizeSettingBatch(settings);
            ValidateEffectiveSettingBatch(normalized);
        }
        catch (Exception ex)
        {
            Diagnostic("Could not validate setting batch: " + ex.Message);
            return false;
        }

        string startupValue;
        if (!normalized.TryGetValue(StartupKey, out startupValue))
            return SaveIniOptions(normalized);

        bool desired = IsTrue(startupValue);
        Dictionary<string, string> previousSettings = null;
        string transactionError;
        bool committed = ManagedStartupShortcut.CommitIniCoupledState(
            startupShortcut,
            desired,
            delegate(out bool previousConfigured, out string snapshotError)
            {
                previousConfigured = false;
                snapshotError = null;
                try
                {
                    Dictionary<string, string> effective = LoadEffectiveSettingValues();
                    previousConfigured = IsTrue(effective[StartupKey]);
                    previousSettings = new Dictionary<string, string>(
                        StringComparer.OrdinalIgnoreCase);
                    foreach (KeyValuePair<string, string> setting in normalized)
                        previousSettings[setting.Key] = effective[setting.Key];
                    return true;
                }
                catch (Exception ex)
                {
                    snapshotError = "Could not capture the previous settings: " + ex.Message;
                    return false;
                }
            },
            delegate(out string persistenceError)
            {
                return SaveIniOptions(normalized, out persistenceError);
            },
            delegate(out string rollbackError)
            {
                return SaveIniOptions(previousSettings, out rollbackError);
            },
            out transactionError);
        if (!committed)
            Diagnostic("Could not commit Startup/INI transaction: " +
                (transactionError ?? "unknown error"));
        return committed;
    }

    static void ValidateEffectiveSettingBatch(IDictionary<string, string> settings)
    {
        Dictionary<string, string> effective = File.Exists(iniPath)
            ? LoadEffectiveSettingValues()
            : DefaultSettingValues();
        foreach (KeyValuePair<string, string> setting in settings)
            effective[setting.Key] = setting.Value;
        OptionsFromValues(effective);
    }

    static Dictionary<string, string> NormalizeSettingBatch(
        IDictionary<string, string> settings)
    {
        var normalized = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (KeyValuePair<string, string> setting in settings)
        {
            string key = CanonicalSettingName(setting.Key);
            if (key == null)
                throw new ArgumentException("Unknown setting: " + setting.Key);
            normalized[key] = NormalizeSettingValue(key, setting.Value);
        }
        return normalized;
    }

    static bool SaveIniOptions(IDictionary<string, string> settings)
    {
        string error;
        return SaveIniOptions(settings, out error);
    }

    static bool SaveIniOptions(
        IDictionary<string, string> settings,
        out string error)
    {
        bool saved = ManagedIniFile.SaveSectionBatch(iniFile, settings, out error);
        if (!saved && !String.IsNullOrEmpty(error))
            Diagnostic(error);
        return saved;
    }

    static void ReconcileStartup(bool desired)
    {
        bool previous;
        string error;
        if (!ManagedStartupShortcut.SetDesiredState(
            startupShortcut, desired, out previous, out error))
            throw new IOException("Could not reconcile Startup: " + (error ?? "unknown error"));
    }

    static List<string> ValidateAndBuildManagedRecordNames(Options options)
    {
        List<string> managed = BuildManagedRecordNames(options);
        if (managed.Count == 0)
            throw new InvalidOperationException(
                "No managed DNS owner names were selected. Disable NoRootRecord, set SubFolder, or set ManagedRecordName.");
        ValidateDnsToken(options.ZoneName, ZoneNameKey, false);
        foreach (string recordName in managed)
            ValidateDnsToken(recordName, "managed record name", true);
        return managed;
    }

    static ManagedStartupShortcutSpec BuildStartupShortcutSpec()
    {
        return new ManagedStartupShortcutSpec
        {
            ProductName = executableBaseName,
            ExecutablePath = executablePath,
            WorkingDirectory = executableDirectory,
            Arguments = "",
            IdentityPath = iniPath,
            LegacyFileName = executableBaseName + ".lnk",
            Log = Diagnostic
        };
    }

    static ManagedIniFileSpec BuildIniFileSpec()
    {
        Dictionary<string, string> defaults = DefaultSettingValues();
        var text = new StringBuilder();
        text.AppendLine("[Settings]");
        text.AppendLine("; Every setting is available through INI, command line, and tray.");
        text.AppendLine("; WhatIf defaults to true so a first launch cannot change DNS silently.");
        foreach (string key in new[]
        {
            ZoneNameKey, SubFolderKey, ManagedRecordNameKey, NoRootRecordKey,
            LogFileKey, MaxLogMegabytesKey, LogRetentionCountKey, SleepSecondsKey,
            IncludeInterfaceAliasKey, ExcludeInterfaceAliasKey, IncludeIPAddressKey,
            IncludeUnpreferredKey, WhatIfKey, ConfirmKey, EnabledKey, StartupKey, ShowTrayKey,
            DropdownKey
        })
            text.AppendLine(key + "=" + defaults[key]);
        return new ManagedIniFileSpec
        {
            FilePath = iniPath,
            SectionName = SettingsSection,
            DefaultContents = text.ToString(),
            Log = Diagnostic
        };
    }

    static string GetExecutablePath()
    {
        Assembly entry = Assembly.GetEntryAssembly();
        string location = entry == null ? null : entry.Location;
        if (String.IsNullOrEmpty(location))
            location = Process.GetCurrentProcess().MainModule.FileName;
        return Path.GetFullPath(location);
    }

    static string GetExecutableDirectory()
    {
        string directory = Path.GetDirectoryName(executablePath);
        return String.IsNullOrEmpty(directory) ? AppDomain.CurrentDomain.BaseDirectory : directory;
    }

    static string GetExecutableBaseName()
    {
        string name = Path.GetFileNameWithoutExtension(executablePath);
        return String.IsNullOrEmpty(name) ? "DNSAutoUpdate" : name;
    }

    static string ProductVersion()
    {
        Version version = Assembly.GetExecutingAssembly().GetName().Version;
        return version == null ? "1.0.0.0" : version.ToString();
    }

    static string RequireNextValue(string[] args, ref int index, string option)
    {
        if (index + 1 >= args.Length ||
            (args[index + 1] != null && args[index + 1].StartsWith("-", StringComparison.Ordinal)))
            throw new ArgumentException("Missing value for " + option + ".");
        return args[++index];
    }

    static void RejectInlineValue(string option, string value)
    {
        if (value != null)
            throw new ArgumentException("--" + option + " does not accept a value.");
    }

    static bool IsHelpOption(string value)
    {
        return String.Equals(value, "--help", StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "-h", StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "/?", StringComparison.OrdinalIgnoreCase);
    }

    static bool IsVersionOption(string value)
    {
        return String.Equals(value, "--version", StringComparison.OrdinalIgnoreCase);
    }

    static bool HasOption(string[] args, Func<string, bool> predicate)
    {
        return args != null && args.Any(predicate);
    }

    static void Diagnostic(string message)
    {
        try { Console.Error.WriteLine(message); }
        catch { }
    }

    static void InitializeControlObjects()
    {
        reloadEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ReloadEventName());
        exitEvent = new EventWaitHandle(false, EventResetMode.ManualReset, ExitEventName());
        runNowEvent = new EventWaitHandle(false, EventResetMode.AutoReset, RunNowEventName());
        profileMutex = new Mutex(false, ProfileMutexName());
    }

    static bool AcquireProfileMutex()
    {
        try
        {
            ownsProfileMutex = profileMutex.WaitOne(0);
        }
        catch (AbandonedMutexException)
        {
            ownsProfileMutex = true;
        }
        return ownsProfileMutex;
    }

    static string ProfileMutexName()
    {
        return @"Local\AIProjects.DNSAutoUpdate.Profile." + profileIdentity;
    }

    static string ReloadEventName()
    {
        return @"Local\AIProjects.DNSAutoUpdate.Reload." + profileIdentity;
    }

    static string ExitEventName()
    {
        return @"Local\AIProjects.DNSAutoUpdate.Exit." + profileIdentity;
    }

    static string RunNowEventName()
    {
        return @"Local\AIProjects.DNSAutoUpdate.RunNow." + profileIdentity;
    }

    static bool SignalExistingControl(string name, string action)
    {
        try
        {
            using (EventWaitHandle control = EventWaitHandle.OpenExisting(name))
            {
                control.Set();
                Console.WriteLine("The resident instance was asked to " + action + ".");
                return true;
            }
        }
        catch (WaitHandleCannotBeOpenedException)
        {
            Console.Error.WriteLine("ERROR: No resident " + executableBaseName +
                " instance is running for " + iniPath + ".");
            return false;
        }
    }

    static void RequestReload()
    {
        if (reloadEvent != null)
            reloadEvent.Set();
    }

    static void RequestRunNow()
    {
        if (runNowEvent != null)
            runNowEvent.Set();
    }

    static void RequestExit()
    {
        if (exitEvent != null)
            exitEvent.Set();
    }

    static bool ExitRequested()
    {
        return exitEvent != null && exitEvent.WaitOne(0);
    }

    static void DisposeControlObjects()
    {
        if (ownsProfileMutex && profileMutex != null)
        {
            try { profileMutex.ReleaseMutex(); }
            catch (ApplicationException) { }
            ownsProfileMutex = false;
        }
        if (profileMutex != null)
        {
            profileMutex.Dispose();
            profileMutex = null;
        }
        if (reloadEvent != null)
        {
            reloadEvent.Dispose();
            reloadEvent = null;
        }
        if (exitEvent != null)
        {
            exitEvent.Dispose();
            exitEvent = null;
        }
        if (runNowEvent != null)
        {
            runNowEvent.Dispose();
            runNowEvent = null;
        }
    }

    sealed class AddressObservation
    {
        string previousSignature;
        int consecutiveObservations;

        public bool Observe(List<string> addresses)
        {
            string signature = String.Join("\n", addresses);
            if (String.Equals(signature, previousSignature, StringComparison.Ordinal))
                ++consecutiveObservations;
            else
            {
                previousSignature = signature;
                consecutiveObservations = 1;
            }
            return consecutiveObservations >= 2;
        }

        public void Reset()
        {
            previousSignature = null;
            consecutiveObservations = 0;
        }
    }

    static void ResidentWorker(Options initialSettings)
    {
        Options settings = initialSettings;
        var observation = new AddressObservation();
        bool forcedCycle = false;
        Mutex ownedZoneMutex = null;
        string ownedZone = null;
        try
        {
            LogStart(settings, settings.Enabled
                ? ValidateAndBuildManagedRecordNames(settings)
                : BuildManagedRecordNames(settings),
                "Resident DNS updater starting");
            while (!ExitRequested())
            {
                if (!settings.Enabled)
                {
                    ReleaseZoneOwnership(ref ownedZoneMutex, ref ownedZone);
                    SetStatus("Disabled - configure Enabled to begin");
                }
                else if (IsPaused() && !forcedCycle)
                {
                    SetStatus("Paused");
                }
                else
                {
                    forcedCycle = false;
                    List<string> managed = ValidateAndBuildManagedRecordNames(settings);
                    if (!EnsureZoneOwnership(settings.ZoneName, ref ownedZoneMutex, ref ownedZone))
                    {
                        SetStatus("Zone owned by another local profile");
                        Log(settings, "ERROR: Another local DNSAutoUpdate profile owns zone " +
                            settings.ZoneName + "; no DNS operation was attempted.");
                    }
                    else
                    {
                        SetStatus(settings.WhatIf ? "Previewing DNS cycle" : "Running DNS cycle");
                        bool succeeded;
                        try
                        {
                            succeeded = RunCycleSerialized(settings, managed, observation);
                        }
                        catch (OperationCanceledException)
                        {
                            if (ExitRequested())
                                break;
                            throw;
                        }
                        catch (Exception ex)
                        {
                            Log(settings, "ERROR: DNS cycle failed safely and will be retried: " + ex.Message);
                            succeeded = false;
                        }
                        SetStatus(succeeded
                            ? (settings.WhatIf ? "Preview cycle complete" : "Cycle complete")
                            : "Cycle incomplete - see log");
                    }
                }

                int timeout = settings.Enabled && !IsPaused()
                    ? checked(settings.SleepSeconds * 1000)
                    : Timeout.Infinite;
                int signaled = WaitHandle.WaitAny(
                    new WaitHandle[] { exitEvent, reloadEvent, runNowEvent },
                    timeout);
                if (signaled == 0)
                    break;
                if (signaled == 1)
                {
                    try
                    {
                        Options reloaded = LoadRuntimeSettings();
                        reloaded.ResidentMode = true;
                        ReconcileStartup(reloaded.RunAtStartup);
                        settings = reloaded;
                        observation.Reset();
                        PublishSettings(settings);
                        Log(settings, "Settings reloaded from " + iniPath + ".");
                    }
                    catch (Exception ex)
                    {
                        Log(settings, "ERROR: Could not reload settings; retaining the last valid profile: " + ex.Message);
                        SetStatus("Reload failed - using prior settings");
                    }
                    continue;
                }
                if (signaled == 2)
                {
                    forcedCycle = true;
                    continue;
                }
                // WaitHandle.WaitTimeout means the normal polling interval elapsed.
            }
        }
        catch (OperationCanceledException)
        {
            if (!ExitRequested())
                throw;
        }
        catch (Exception ex)
        {
            workerFailure = ex;
            Diagnostic("DNS worker error: " + ex);
            SetStatus("Worker failed");
            RequestExit();
        }
        finally
        {
            ReleaseZoneOwnership(ref ownedZoneMutex, ref ownedZone);
        }
    }

    static bool EnsureZoneOwnership(
        string zone,
        ref Mutex ownedMutex,
        ref string ownedZone)
    {
        if (ownedMutex != null && String.Equals(ownedZone, zone, StringComparison.OrdinalIgnoreCase))
            return true;
        ReleaseZoneOwnership(ref ownedMutex, ref ownedZone);
        var candidate = new Mutex(false,
            @"Global\AIProjects.DNSAutoUpdate.Zone." + StableTextHash(zone));
        bool owns = false;
        try
        {
            try { owns = candidate.WaitOne(0); }
            catch (AbandonedMutexException) { owns = true; }
            if (!owns)
            {
                candidate.Dispose();
                return false;
            }
            ownedMutex = candidate;
            ownedZone = zone;
            return true;
        }
        catch
        {
            if (owns)
            {
                try { candidate.ReleaseMutex(); }
                catch { }
            }
            candidate.Dispose();
            throw;
        }
    }

    static void ReleaseZoneOwnership(ref Mutex ownedMutex, ref string ownedZone)
    {
        Mutex releasing = ownedMutex;
        ownedMutex = null;
        ownedZone = null;
        if (releasing == null)
            return;
        try { releasing.ReleaseMutex(); }
        catch (ApplicationException) { }
        releasing.Dispose();
    }

    static bool RunCycleSerialized(
        Options options,
        List<string> managed,
        AddressObservation observation)
    {
        string mutexName = @"Global\AIProjects.DNSAutoUpdate.Zone." +
            StableTextHash(options.ZoneName);
        using (var zoneMutex = new Mutex(false, mutexName))
        {
            bool owns = false;
            Stopwatch wait = Stopwatch.StartNew();
            try
            {
                while (!owns && wait.ElapsedMilliseconds < 10000)
                {
                    try { owns = zoneMutex.WaitOne(250); }
                    catch (AbandonedMutexException) { owns = true; }
                    if (ExitRequested())
                        throw new OperationCanceledException();
                }
                if (!owns)
                {
                    Log(options, "ERROR: Timed out waiting for exclusive ownership of DNS zone " + options.ZoneName + ".");
                    return false;
                }
                return RunCycle(options, managed, observation);
            }
            finally
            {
                if (owns)
                {
                    try { zoneMutex.ReleaseMutex(); }
                    catch (ApplicationException) { }
                }
            }
        }
    }

    static string StableTextHash(string value)
    {
        using (SHA256 sha = SHA256.Create())
        {
            byte[] bytes = sha.ComputeHash(Encoding.UTF8.GetBytes((value ?? "").Trim().ToUpperInvariant()));
            var result = new StringBuilder(16);
            for (int i = 0; i < 8; ++i)
                result.Append(bytes[i].ToString("X2", CultureInfo.InvariantCulture));
            return result.ToString();
        }
    }

    static void LogStart(Options options, List<string> managed, string heading)
    {
        Log(options, "=============================================");
        Log(options, " " + heading + " ");
        Log(options, " Zone: " + options.ZoneName);
        Log(options, " Managed exact A record owner names: " + String.Join(", ", managed));
        Log(options, " Mode: " + (options.Enabled
            ? (options.WhatIf ? "enabled preview (WhatIf)" : "enabled apply")
            : "disabled"));
        Log(options, " Records outside this allowlist are ignored.");
        Log(options, "=============================================");
    }

    static void PublishSettings(Options settings)
    {
        lock (settingsLock)
        {
            activeSettings = settings;
            ++activeSettingsVersion;
        }
    }

    static Options GetPublishedSettings(out int version)
    {
        lock (settingsLock)
        {
            version = activeSettingsVersion;
            return activeSettings;
        }
    }

    static void SetStatus(string value)
    {
        lock (statusLock)
            statusText = value;
    }

    static string GetStatus()
    {
        lock (statusLock)
            return statusText;
    }

    static bool IsPaused()
    {
        lock (statusLock)
            return paused;
    }

    static void DrainWorker()
    {
        Thread draining = workerThread;
        if (draining == null || draining == Thread.CurrentThread)
            return;
        if (!draining.Join(70000))
            Diagnostic("Timed out waiting for the DNS worker to stop.");
        workerThread = null;
    }

    static void OnUiTimer()
    {
        if (ExitRequested())
        {
            Application.ExitThread();
            return;
        }
        int version;
        Options settings = GetPublishedSettings(out version);
        if (settings != null && version != appliedSettingsVersion)
        {
            ApplyUiSettings(settings);
            appliedSettingsVersion = version;
        }
        else if (settings != null && tray != null)
            RefreshTray(settings);
    }

    static void ApplyUiSettings(Options settings)
    {
        if (!settings.ShowTrayIcon)
        {
            DisposeTray();
            return;
        }
        if (tray == null || appliedDropdown != settings.ShowMenuAsDropdown)
            CreateTray(settings);
        else
            RefreshTray(settings);
    }

    static void CreateTray(Options settings)
    {
        DisposeTray();
        ContextMenu menu = new ContextMenu();
        try
        {
            ManagedTrayBaseline.AppendHeader(menu, executableBaseName, "Version " + ProductVersion());
            statusItem = new MenuItem() { Enabled = false };
            menu.MenuItems.Add(statusItem);
            menu.MenuItems.Add("-");
            menu.MenuItems.Add(new MenuItem("Run cycle now", delegate { RequestRunNow(); }));
            pauseItem = new MenuItem("Pause");
            pauseItem.Click += delegate
            {
                bool nowPaused;
                lock (statusLock)
                {
                    paused = !paused;
                    nowPaused = paused;
                }
                if (nowPaused)
                    RequestReload();
                else
                    RequestRunNow();
            };
            menu.MenuItems.Add(pauseItem);
            menu.MenuItems.Add("-");

            var productSettings = new List<MenuItem>();
            AddBooleanSettingItem(productSettings, EnabledKey, "Enabled");
            AddTextSettingItem(productSettings, ZoneNameKey);
            AddTextSettingItem(productSettings, ManagedRecordNameKey);
            AddTextSettingItem(productSettings, SubFolderKey);
            AddBooleanSettingItem(productSettings, NoRootRecordKey, "Exclude zone root (@)");
            AddTextSettingItem(productSettings, IncludeIPAddressKey);
            AddTextSettingItem(productSettings, IncludeInterfaceAliasKey);
            AddTextSettingItem(productSettings, ExcludeInterfaceAliasKey);
            AddBooleanSettingItem(productSettings, IncludeUnpreferredKey, "Include unpreferred addresses");
            AddTextSettingItem(productSettings, SleepSecondsKey);
            AddBooleanSettingItem(productSettings, WhatIfKey, "WhatIf preview mode");
            AddBooleanSettingItem(productSettings, ConfirmKey, "Confirm each change");
            AddTextSettingItem(productSettings, LogFileKey);
            AddTextSettingItem(productSettings, MaxLogMegabytesKey);
            AddTextSettingItem(productSettings, LogRetentionCountKey);
            productSettings.Add(new MenuItem("-"));
            AddBooleanSettingItem(productSettings, StartupKey, "Run at startup");
            AddBooleanSettingItem(productSettings, ShowTrayKey, "Show tray icon");
            AddBooleanSettingItem(productSettings, DropdownKey, "Show menu as dropdown");

            if (settings.ShowMenuAsDropdown)
                menu.MenuItems.Add(new MenuItem("Settings", productSettings.ToArray()));
            else
            {
                foreach (MenuItem item in productSettings)
                    menu.MenuItems.Add(item);
            }

            menu.MenuItems.Add("-");
            menu.MenuItems.Add(new MenuItem("Reload settings", delegate { RequestReload(); }));
            menu.MenuItems.Add(new MenuItem("Open configuration", delegate { OpenPath(iniPath, false); }));
            menu.MenuItems.Add(new MenuItem("Open log", delegate
            {
                int ignored;
                Options current = GetPublishedSettings(out ignored);
                OpenPath(current == null ? null : current.LogFile, true);
            }));
            menu.MenuItems.Add("-");
            menu.MenuItems.Add(new MenuItem("Exit", delegate
            {
                RequestExit();
                Application.ExitThread();
            }));

            tray = ManagedTrayBaseline.CreateNotifyIcon(
                SystemIcons.Application,
                BuildTooltip(settings),
                executableBaseName,
                menu);
            menu = null;
            appliedDropdown = settings.ShowMenuAsDropdown;
            RefreshTray(settings);
        }
        finally
        {
            if (menu != null)
                menu.Dispose();
        }
    }

    static void AddTextSettingItem(List<MenuItem> items, string key)
    {
        MenuItem item = new MenuItem();
        item.Click += delegate
        {
            int ignored;
            Options current = GetPublishedSettings(out ignored);
            if (current != null)
                PromptForSetting(key, SerializeSetting(current, key));
        };
        settingMenuItems[key] = item;
        items.Add(item);
    }

    static void AddBooleanSettingItem(List<MenuItem> items, string key, string label)
    {
        MenuItem item = new MenuItem(label);
        item.Click += delegate
        {
            int ignored;
            Options current = GetPublishedSettings(out ignored);
            if (current != null)
                PersistTraySetting(key, (!GetBooleanSetting(current, key)).ToString());
        };
        settingMenuItems[key] = item;
        items.Add(item);
    }

    static void RefreshTray(Options settings)
    {
        if (tray == null)
            return;
        string status = GetStatus();
        ManagedTrayBaseline.UpdateTooltip(tray, BuildTooltip(settings), executableBaseName);
        if (statusItem != null)
            statusItem.Text = "Status: " + status;
        if (pauseItem != null)
        {
            bool isPaused;
            lock (statusLock)
                isPaused = paused;
            pauseItem.Checked = isPaused;
            pauseItem.Text = isPaused ? "Resume" : "Pause";
        }

        foreach (KeyValuePair<string, MenuItem> entry in settingMenuItems)
        {
            if (IsBooleanSetting(entry.Key))
                entry.Value.Checked = GetBooleanSetting(settings, entry.Key);
            else
                entry.Value.Text = SettingLabel(entry.Key) + ": " + DisplayValue(SerializeSetting(settings, entry.Key));
        }
        MenuItem startupItem;
        if (settingMenuItems.TryGetValue(StartupKey, out startupItem))
        {
            string error;
            startupItem.Checked = ManagedStartupShortcut.IsInstalled(startupShortcut, out error);
            if (!String.IsNullOrEmpty(error))
                Diagnostic("Could not refresh Startup menu state: " + error);
        }
    }

    static string BuildTooltip(Options settings)
    {
        return executableBaseName + " - " + settings.ZoneName + " - " + GetStatus();
    }

    static string SettingLabel(string key)
    {
        if (key == ZoneNameKey) return "Zone";
        if (key == ManagedRecordNameKey) return "Managed names";
        if (key == SubFolderKey) return "Legacy subfolders";
        if (key == IncludeIPAddressKey) return "Explicit IPv4 addresses";
        if (key == IncludeInterfaceAliasKey) return "Included interfaces";
        if (key == ExcludeInterfaceAliasKey) return "Excluded interfaces";
        if (key == SleepSecondsKey) return "Polling interval (seconds)";
        if (key == LogFileKey) return "Log file";
        if (key == MaxLogMegabytesKey) return "Maximum log MiB";
        if (key == LogRetentionCountKey) return "Rotated logs retained";
        return key;
    }

    static string DisplayValue(string value)
    {
        if (String.IsNullOrEmpty(value))
            return "(empty)";
        return value.Length <= 48 ? value : value.Substring(0, 45) + "...";
    }

    static void PromptForSetting(string key, string currentValue)
    {
        string value;
        if (!ManagedTrayBaseline.TryPromptText(
            executableBaseName,
            "Enter " + SettingLabel(key) + ":",
            currentValue,
            out value))
            return;
        try
        {
            value = NormalizeSettingValue(key, value);
        }
        catch (Exception ex)
        {
            ShowTrayError(ex.Message);
            return;
        }
        PersistTraySetting(key, value);
    }

    static bool PersistTraySetting(string key, string value)
    {
        var settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { key, value }
        };
        if (!PersistSettingsAtomically(settings))
        {
            ShowTrayError("Could not save " + key + " to " + iniPath + ".");
            return false;
        }
        if (key.Equals(ShowTrayKey, StringComparison.OrdinalIgnoreCase) &&
            String.Equals(value, "false", StringComparison.OrdinalIgnoreCase) && tray != null)
            tray.Visible = false;
        RequestReload();
        return true;
    }

    static bool IsBooleanSetting(string key)
    {
        return key == EnabledKey || key == NoRootRecordKey ||
            key == IncludeUnpreferredKey || key == WhatIfKey || key == ConfirmKey ||
            key == StartupKey || key == ShowTrayKey || key == DropdownKey;
    }

    static bool GetBooleanSetting(Options settings, string key)
    {
        if (key == EnabledKey) return settings.Enabled;
        if (key == NoRootRecordKey) return settings.NoRootRecord;
        if (key == IncludeUnpreferredKey) return settings.IncludeUnpreferred;
        if (key == WhatIfKey) return settings.WhatIf;
        if (key == ConfirmKey) return settings.Confirm;
        if (key == StartupKey) return settings.RunAtStartup;
        if (key == ShowTrayKey) return settings.ShowTrayIcon;
        if (key == DropdownKey) return settings.ShowMenuAsDropdown;
        throw new ArgumentException("Not a boolean setting: " + key);
    }

    static string SerializeSetting(Options settings, string key)
    {
        if (key == ZoneNameKey) return settings.ZoneName;
        if (key == SubFolderKey) return String.Join(",", settings.SubFolder);
        if (key == ManagedRecordNameKey) return String.Join(",", settings.ManagedRecordName);
        if (key == LogFileKey)
        {
            if (settings.LogFile.StartsWith(executableDirectory + Path.DirectorySeparatorChar,
                StringComparison.OrdinalIgnoreCase))
                return settings.LogFile.Substring(executableDirectory.Length + 1);
            return settings.LogFile;
        }
        if (key == MaxLogMegabytesKey) return settings.MaxLogMegabytes.ToString(CultureInfo.InvariantCulture);
        if (key == LogRetentionCountKey) return settings.LogRetentionCount.ToString(CultureInfo.InvariantCulture);
        if (key == SleepSecondsKey) return settings.SleepSeconds.ToString(CultureInfo.InvariantCulture);
        if (key == IncludeInterfaceAliasKey) return String.Join(",", settings.IncludeInterfaceAlias);
        if (key == ExcludeInterfaceAliasKey) return String.Join(",", settings.ExcludeInterfaceAlias);
        if (key == IncludeIPAddressKey) return String.Join(",", settings.IncludeIPAddress);
        if (IsBooleanSetting(key)) return GetBooleanSetting(settings, key) ? "true" : "false";
        throw new ArgumentException("Unknown setting: " + key);
    }

    static void OpenPath(string path, bool createLog)
    {
        try
        {
            if (String.IsNullOrWhiteSpace(path))
                throw new InvalidOperationException("The path is unavailable.");
            if (createLog && !File.Exists(path))
            {
                int ignored;
                Options settings = GetPublishedSettings(out ignored);
                if (settings != null)
                    Log(settings, "Log opened from tray.");
            }
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            ShowTrayError("Could not open " + path + ": " + ex.Message);
        }
    }

    static void ShowTrayError(string message)
    {
        MessageBox.Show(message, executableBaseName, MessageBoxButtons.OK, MessageBoxIcon.Error);
    }

    static void DisposeTray()
    {
        if (tray != null && tray.ContextMenu != null)
            tray.ContextMenu.Dispose();
        ManagedTrayBaseline.DisposeNotifyIcon(ref tray);
        settingMenuItems.Clear();
        statusItem = null;
        pauseItem = null;
    }

    static void DisposeUi()
    {
        if (uiTimer != null)
        {
            uiTimer.Stop();
            uiTimer.Dispose();
            uiTimer = null;
        }
        DisposeTray();
    }

    static int RunOneShot(Options o)
    {
        if (!o.Enabled)
        {
            Console.Error.WriteLine("ERROR: DNS updates are disabled. Persist Enabled=true or pass --enabled.");
            return 3;
        }
        List<string> managed = ValidateAndBuildManagedRecordNames(o);
        LogStart(o, managed, "One-shot DNS update starting");
        SetStatus("Running one-shot cycle");
        bool succeeded;
        try
        {
            succeeded = RunCycleSerialized(o, managed, new AddressObservation());
        }
        catch (Exception ex)
        {
            Log(o, "ERROR: One-shot DNS cycle failed: " + ex.Message);
            succeeded = false;
        }
        SetStatus(succeeded ? "One-shot cycle succeeded" : "One-shot cycle failed");
        return succeeded ? 0 : 3;
    }

    static bool RunCycle(
        Options o,
        List<string> managed,
        AddressObservation observation)
    {
        var serverIps = GetEligibleServerIPv4(o);
        if (serverIps.Count == 0)
        {
            Log(o, "No eligible server IPv4 addresses detected; skipping DNS changes this cycle.");
            return false;
        }

        bool succeeded = true;
        bool stableAutomaticObservation = observation.Observe(serverIps);
        bool removalsAllowed = o.IncludeIPAddress.Count != 0 || stableAutomaticObservation;
        if (!Log(o, "Eligible server IPs detected: " + String.Join(", ", serverIps)))
        {
            Console.Error.WriteLine("ERROR: DNS changes are blocked because the audit log is not writable.");
            return false;
        }
        if (!removalsAllowed)
            Log(o, "Safety hold: stale-record deletion is deferred until the same automatically discovered address set is observed in two consecutive cycles.");
        foreach (string recordName in managed)
            succeeded = SyncARecords(o, recordName, serverIps, removalsAllowed) && succeeded;
        return succeeded;
    }

    static List<string> BuildManagedRecordNames(Options o)
    {
        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        if (o.ManagedRecordName.Count > 0)
        {
            foreach (string name in o.ManagedRecordName.Select(NormalizeRecordName).Where(s => s.Length > 0))
                set.Add(name);
        }
        else
        {
            if (!o.NoRootRecord)
                set.Add("@");
            foreach (string name in o.SubFolder.Select(NormalizeRecordName).Where(s => s.Length > 0 && s != "@"))
                set.Add(name);
        }
        return set.ToList();
    }

    static bool SyncARecords(
        Options o,
        string recordName,
        List<string> serverIps,
        bool removalsAllowed)
    {
        bool succeeded = true;
        List<string> existing;
        try
        {
            existing = GetExactARecords(o, recordName);
        }
        catch (Exception ex)
        {
            Log(o, "ERROR reading A records for '" + recordName + "'; skipping this owner name this cycle: " + ex.Message);
            return false;
        }

        Log(o, "Found " + existing.Count + " A records for '" + recordName + "'. Adding and verifying replacements before any deletion.");
        foreach (string ip in serverIps)
        {
            if (!existing.Contains(ip))
            {
                Log(o, "   Adding missing IP " + ip);
                if (ShouldApply(o, o.ZoneName + "/" + recordName + " " + ip, "Add missing A record"))
                {
                    try
                    {
                        succeeded = RunDnsCmd(o, "/RecordAdd " + Quote(o.ZoneName) + " " +
                            Quote(recordName) + " A " + Quote(ip), "   Added successfully.") && succeeded;
                    }
                    catch (Exception ex)
                    {
                        Log(o, "   ERROR: add result is uncertain and will be re-read: " + ex.Message);
                        succeeded = false;
                    }
                }
                else
                {
                    Log(o, "   Add skipped.");
                    if (!o.WhatIf)
                        succeeded = false;
                }
            }
        }

        try
        {
            existing = GetExactARecords(o, recordName);
        }
        catch (Exception ex)
        {
            Log(o, "ERROR re-reading A records for '" + recordName +
                "' after additions; no stale records will be removed: " + ex.Message);
            return false;
        }

        bool verifiedReplacementExists = existing.Any(ip => serverIps.Contains(ip));
        foreach (string ip in existing.Where(ip => !serverIps.Contains(ip)).ToList())
        {
            if (!removalsAllowed)
            {
                Log(o, "   Stale IP " + ip + " retained during the address-stability safety hold.");
                succeeded = false;
                continue;
            }
            if (!o.WhatIf && !verifiedReplacementExists)
            {
                Log(o, "   ERROR: stale IP " + ip +
                    " retained because no desired replacement was verified present.");
                succeeded = false;
                continue;
            }

            Log(o, "   Removing stale IP " + ip);
            if (ShouldApply(o, o.ZoneName + "/" + recordName + " " + ip,
                "Remove stale A record"))
            {
                try
                {
                    succeeded = RunDnsCmd(o, "/RecordDelete " + Quote(o.ZoneName) + " " +
                        Quote(recordName) + " A " + Quote(ip) + " /f",
                        "   Removed successfully.") && succeeded;
                }
                catch (Exception ex)
                {
                    Log(o, "   ERROR: removal result is uncertain and will be re-read: " + ex.Message);
                    succeeded = false;
                }
            }
            else
            {
                Log(o, "   Removal skipped.");
                if (!o.WhatIf)
                    succeeded = false;
            }
        }

        if (!o.WhatIf)
        {
            try
            {
                List<string> finalRecords = GetExactARecords(o, recordName);
                if (!finalRecords.SequenceEqual(serverIps, StringComparer.OrdinalIgnoreCase))
                {
                    Log(o, "ERROR: final A-record verification for '" + recordName +
                        "' did not match the desired set. Found: " + String.Join(", ", finalRecords));
                    succeeded = false;
                }
            }
            catch (Exception ex)
            {
                Log(o, "ERROR: final A-record verification failed for '" +
                    recordName + "': " + ex.Message);
                succeeded = false;
            }
        }
        return succeeded;
    }

    static List<string> GetExactARecords(Options o, string recordName)
    {
        var result = RunProcess("dnscmd.exe", ". /EnumRecords " + Quote(o.ZoneName) + " " + Quote(recordName) + " /Type A", 60000);
        string combined = result.Output + "\n" + result.Error;
        if (result.ExitCode != 0)
        {
            if (Regex.IsMatch(
                combined,
                @"(?i)\bDNS_ERROR_(?:RECORD|NAME)_DOES_NOT_EXIST\b|\b(?:9701|9714)\b"))
                return new List<string>();
            throw new InvalidOperationException(combined.Trim());
        }

        return ParseExactARecords(result.Output);
    }

    static List<string> ParseExactARecords(string output)
    {
        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        using (var reader = new StringReader(output ?? ""))
        {
            string line;
            while ((line = reader.ReadLine()) != null)
            {
                if (!Regex.IsMatch(line, @"(?:^|\s)A(?:\s|$)", RegexOptions.IgnoreCase))
                    continue;
                foreach (Match m in Regex.Matches(line, @"\b(?:\d{1,3}\.){3}\d{1,3}\b"))
                {
                    string ip;
                    if (TryNormalizeStrictIPv4(m.Value, out ip))
                        set.Add(ip);
                }
            }
        }
        return set.ToList();
    }

    static bool RunDnsCmd(Options o, string args, string success)
    {
        var result = RunProcess("dnscmd.exe", ". " + args, 60000);
        if (result.ExitCode == 0)
            return Log(o, success);
        else
        {
            Log(o, "   ERROR: " + (result.Output + " " + result.Error).Trim());
            return false;
        }
    }

    static List<string> GetEligibleServerIPv4(Options o)
    {
        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        if (o.IncludeIPAddress.Count > 0)
        {
            foreach (string raw in o.IncludeIPAddress)
            {
                string ip;
                if (!TryNormalizeUsableIPv4(raw, out ip))
                    throw new ArgumentException("Invalid or unusable IPv4 address for -IncludeIPAddress: " + raw);
                set.Add(ip);
            }
            return set.ToList();
        }

        foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (ni.OperationalStatus != OperationalStatus.Up)
                continue;

            string alias = ni.Name ?? "";
            if (o.IncludeInterfaceAlias.Count > 0 && !WildcardAny(alias, o.IncludeInterfaceAlias))
                continue;
            if (o.ExcludeInterfaceAlias.Count > 0 && WildcardAny(alias, o.ExcludeInterfaceAlias))
                continue;

            foreach (UnicastIPAddressInformation addr in ni.GetIPProperties().UnicastAddresses)
            {
                if (addr.Address.AddressFamily != AddressFamily.InterNetwork)
                    continue;
                if (!o.IncludeUnpreferred && addr.DuplicateAddressDetectionState != DuplicateAddressDetectionState.Preferred)
                    continue;
                string ip;
                if (TryNormalizeUsableIPv4(addr.Address.ToString(), out ip))
                    set.Add(ip);
            }
        }
        return set.ToList();
    }

    static bool ShouldApply(Options o, string target, string action)
    {
        if (o.WhatIf)
        {
            Log(o, "WHATIF: " + action + " -> " + target);
            return false;
        }
        if (!o.Confirm)
            return Log(o, "AUDIT: approved " + action + " -> " + target);
        if (o.ResidentMode)
        {
            return MessageBox.Show(
                action + Environment.NewLine + target + Environment.NewLine +
                    Environment.NewLine + "Apply this DNS change?",
                executableBaseName,
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning,
                MessageBoxDefaultButton.Button2) == DialogResult.Yes &&
                Log(o, "AUDIT: user approved " + action + " -> " + target);
        }
        Console.Write(action + " " + target + "? [y/N] ");
        string answer = Console.ReadLine() ?? "";
        bool approved = answer.Equals("y", StringComparison.OrdinalIgnoreCase) ||
            answer.Equals("yes", StringComparison.OrdinalIgnoreCase);
        if (approved)
            return Log(o, "AUDIT: user approved " + action + " -> " + target);
        Log(o, "AUDIT: user declined " + action + " -> " + target);
        return false;
    }

    static bool Log(Options o, string msg)
    {
        string line = DateTime.Now.ToString(
            "yyyy-MM-dd HH:mm:ss",
            CultureInfo.InvariantCulture) + "  " + msg;
        bool written = false;
        string logError;
        if (ManagedLogFile.AppendLine(
            new ManagedLogFileSpec
            {
                FilePath = o.LogFile,
                LockWaitMilliseconds = 10000,
                MaximumBytes = (long)o.MaxLogMegabytes * 1024L * 1024L,
                RetentionCount = o.LogRetentionCount,
                WriteUtf8Bom = true
            },
            line,
            out logError))
        {
            written = true;
        }
        else
        {
            Console.Error.WriteLine(
                "WARNING: Could not write DNSAutoUpdate log '" + o.LogFile +
                "': " + logError);
        }
        try { Console.WriteLine(line); }
        catch { }
        return written;
    }

    static ProcessResult RunProcess(string file, string args, int timeoutMs)
    {
        var psi = new ProcessStartInfo(file, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        using (var p = new Process())
        using (var outputComplete = new ManualResetEvent(false))
        using (var errorComplete = new ManualResetEvent(false))
        {
            var output = new StringBuilder();
            var error = new StringBuilder();
            p.StartInfo = psi;
            p.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data == null)
                    outputComplete.Set();
                else
                    output.AppendLine(e.Data);
            };
            p.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data == null)
                    errorComplete.Set();
                else
                    error.AppendLine(e.Data);
            };

            if (!p.Start())
                throw new InvalidOperationException("Could not start " + file + ".");
            p.BeginOutputReadLine();
            p.BeginErrorReadLine();

            Stopwatch elapsed = Stopwatch.StartNew();
            while (!p.WaitForExit(250))
            {
                if (ExitRequested())
                {
                    try { p.Kill(); } catch { }
                    try { p.WaitForExit(5000); } catch { }
                    throw new OperationCanceledException(file + " was cancelled during application exit.");
                }
                if (elapsed.ElapsedMilliseconds >= timeoutMs)
                {
                    try { p.Kill(); } catch { }
                    try { p.WaitForExit(5000); } catch { }
                    throw new TimeoutException(file + " timed out; the DNS mutation result is uncertain.");
                }
            }
            if (!outputComplete.WaitOne(5000) || !errorComplete.WaitOne(5000))
                throw new TimeoutException(file + " output did not finish draining.");
            return new ProcessResult { ExitCode = p.ExitCode, Output = output.ToString(), Error = error.ToString() };
        }
    }

    sealed class ProcessResult
    {
        public int ExitCode;
        public string Output;
        public string Error;
    }

    static string NormalizeIP(string ip) { return (ip ?? "").Trim(); }
    static string NormalizeRecordName(string name) { string n = (name ?? "").Trim(); return n.Length == 0 ? "@" : n; }
    static bool IsUsableIPv4(string ip) { string normalized; return TryNormalizeUsableIPv4(ip, out normalized); }
    static bool TryNormalizeUsableIPv4(string raw, out string ip)
    {
        if (!TryNormalizeStrictIPv4(raw, out ip))
            return false;
        byte[] bytes = ip.Split('.').Select(part => Byte.Parse(
            part,
            NumberStyles.None,
            CultureInfo.InvariantCulture)).ToArray();
        if (bytes[0] == 0 || bytes[0] == 127 || bytes[0] >= 224 ||
            (bytes[0] == 169 && bytes[1] == 254) ||
            (bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 255 && bytes[3] == 255))
        {
            ip = "";
            return false;
        }
        return true;
    }

    static bool TryNormalizeStrictIPv4(string raw, out string ip)
    {
        ip = "";
        string[] parts = NormalizeIP(raw).Split('.');
        if (parts.Length != 4)
            return false;
        byte[] bytes = new byte[4];
        for (int index = 0; index < parts.Length; ++index)
        {
            string part = parts[index];
            if (part.Length == 0 || part.Length > 3 ||
                part.Any(character => character < '0' || character > '9'))
                return false;
            int octet;
            if (!Int32.TryParse(part, NumberStyles.None, CultureInfo.InvariantCulture, out octet) ||
                octet < 0 || octet > 255)
                return false;
            bytes[index] = (byte)octet;
        }
        ip = String.Join(".", bytes.Select(value =>
            value.ToString(CultureInfo.InvariantCulture)).ToArray());
        return true;
    }
    static bool WildcardAny(string value, List<string> patterns) { return patterns.Any(p => Wildcard(value, p)); }
    static bool Wildcard(string value, string pattern) { return Regex.IsMatch(value ?? "", "^" + Regex.Escape(pattern ?? "").Replace("\\*", ".*").Replace("\\?", ".") + "$", RegexOptions.IgnoreCase); }
    static string Quote(string value)
    {
        if (value == null || value.IndexOf('"') >= 0 || value.IndexOf('\0') >= 0 ||
            value.IndexOf('\r') >= 0 || value.IndexOf('\n') >= 0)
        {
            throw new ArgumentException("DNS command argument contains unsupported characters.");
        }
        return "\"" + value + "\"";
    }
    static void ValidateDnsToken(string value, string name, bool allowRoot)
    {
        string token = (value ?? "").Trim();
        if (token.Length == 0)
            throw new ArgumentException(name + " cannot be empty.");
        if (allowRoot && token == "@")
            return;
        if (token.Any(ch => Char.IsWhiteSpace(ch) || Char.IsControl(ch) || ch == '"' || ch == '/' || ch == '\\'))
            throw new ArgumentException(name + " contains unsupported characters: " + value);
    }
}

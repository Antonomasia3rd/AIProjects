using AIProjects.Dependencies;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

class CapsLockLight
{
    const string DefaultKeyboardTargetPath = "\\Device\\KeyboardClass0";
    const int DefaultBlinkIntervalMs = 500;
    const string SettingsSection = "Settings";
    const string KeyboardTargetKey = "KeyboardTargetPath";
    const string BlinkIntervalKey = "BlinkIntervalMs";
    const string StartupKey = "RunAtStartup";
    const string ShowTrayKey = "ShowTrayIcon";
    const string DropdownKey = "ShowMenuAsDropdown";

    static readonly string executablePath = GetExecutablePath();
    static readonly string executableDirectory = GetExecutableDirectory();
    static readonly string executableBaseName = GetExecutableBaseName();
    static readonly string iniPath = Path.Combine(executableDirectory, executableBaseName + ".ini");
    static readonly string logPath = Path.Combine(executableDirectory, executableBaseName + ".log");
    static readonly string profileIdentity = ManagedStartupShortcut.StableIdentityHash(iniPath);
    static readonly ManagedStartupShortcutSpec startupShortcut = BuildStartupShortcutSpec();
    static readonly ManagedIniFileSpec iniFile = BuildIniFileSpec();
    static readonly object logLock = new object();
    static readonly object settingsLock = new object();
    static readonly ManualResetEvent exitEvent = new ManualResetEvent(false);
    static readonly AutoResetEvent localReloadEvent = new AutoResetEvent(false);

    static EventWaitHandle externalReloadEvent;
    static EventWaitHandle externalExitEvent;
    static Mutex profileMutex;
    static bool ownsProfileMutex;
    static Thread hardwareThread;
    static Exception hardwareFailure;
    static RuntimeSettings activeSettings;
    static int activeSettingsVersion;
    static int appliedSettingsVersion = -1;
    static NotifyIcon tray;
    static System.Windows.Forms.Timer uiTimer;
    static MenuItem miTarget;
    static MenuItem miInterval;
    static MenuItem miStartup;
    static MenuItem miShowTray;
    static MenuItem miDropdown;
    static bool appliedDropdown;

    [DllImport(
        "kernel32.dll",
        EntryPoint = "DefineDosDeviceW",
        CharSet = CharSet.Unicode,
        ExactSpelling = true,
        SetLastError = true)]
    static extern bool DefineDosDevice(UInt32 flags, string deviceName, string targetPath);

    [DllImport(
        "kernel32.dll",
        EntryPoint = "CreateFileW",
        CharSet = CharSet.Unicode,
        ExactSpelling = true,
        SetLastError = true)]
    static extern IntPtr CreateFile(
        string fileName,
        UInt32 desiredAccess,
        UInt32 shareMode,
        IntPtr securityAttributes,
        UInt32 creationDisposition,
        UInt32 flagsAndAttributes,
        IntPtr templateFile);

    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBOARD_INDICATOR_PARAMETERS
    {
        public UInt16 unitID;
        public UInt16 LEDflags;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(
        IntPtr device,
        UInt32 ioControlCode,
        ref KEYBOARD_INDICATOR_PARAMETERS input,
        UInt32 inputSize,
        ref KEYBOARD_INDICATOR_PARAMETERS output,
        UInt32 outputSize,
        ref UInt32 bytesReturned,
        IntPtr overlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(
        IntPtr device,
        UInt32 ioControlCode,
        IntPtr input,
        UInt32 inputSize,
        ref KEYBOARD_INDICATOR_PARAMETERS output,
        UInt32 outputSize,
        ref UInt32 bytesReturned,
        IntPtr overlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(
        IntPtr device,
        UInt32 ioControlCode,
        ref KEYBOARD_INDICATOR_PARAMETERS input,
        UInt32 inputSize,
        IntPtr output,
        UInt32 outputSize,
        ref UInt32 bytesReturned,
        IntPtr overlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr handle);

    [DllImport("user32.dll", CharSet = CharSet.Auto, ExactSpelling = true)]
    static extern short GetKeyState(int keyCode);

#if !LEGACY_UTILITY_TESTS
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AttachConsole(UInt32 processId);

    [DllImport("kernel32.dll")]
    static extern IntPtr GetStdHandle(Int32 standardHandle);
#endif

    const int VK_CAPITAL = 0x14;

    sealed class RuntimeSettings
    {
        public string KeyboardTargetPath;
        public int BlinkIntervalMs;
        public bool RunAtStartup;
        public bool ShowTrayIcon;
        public bool ShowMenuAsDropdown;
    }

    sealed class ParsedCommandLine
    {
        public readonly Dictionary<string, string> Settings =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public bool ConfigureOnly;
        public bool ReloadRequested;
        public bool ExitRequested;
    }

    [STAThread]
    static int Main(string[] args)
    {
        PrepareCommandLineConsole(args);
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

        try
        {
            return RunApplication(commandLine);
        }
        catch (Exception ex)
        {
            Log("Fatal error: " + ex);
            Console.Error.WriteLine("ERROR: " + ex.Message);
            return 1;
        }
        finally
        {
            RequestExit();
            DisposeUi();
            DrainHardwareThread();
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

        RuntimeSettings initialSettings = LoadRuntimeSettings();
        ReconcileStartup(initialSettings.RunAtStartup);
        if (commandLine.ConfigureOnly)
        {
            Console.WriteLine("Settings saved to " + iniPath + ".");
            return 0;
        }

        externalReloadEvent = new EventWaitHandle(
            false,
            EventResetMode.AutoReset,
            ReloadEventName());
        externalExitEvent = new EventWaitHandle(
            false,
            EventResetMode.AutoReset,
            ExitEventName());
        profileMutex = new Mutex(false, ProfileMutexName());
        try
        {
            ownsProfileMutex = profileMutex.WaitOne(0);
        }
        catch (AbandonedMutexException)
        {
            ownsProfileMutex = true;
        }

        if (!ownsProfileMutex)
        {
            if (commandLine.Settings.Count != 0)
            {
                externalReloadEvent.Set();
                Console.WriteLine("Settings saved; the resident instance was asked to reload.");
                return 0;
            }
            Console.Error.WriteLine("ERROR: Another " + executableBaseName + " instance is already using " + iniPath + ".");
            return 1;
        }

        Console.CancelKeyPress += delegate(object sender, ConsoleCancelEventArgs e)
        {
            e.Cancel = true;
            RequestExit();
        };
        AppDomain.CurrentDomain.ProcessExit += delegate { RequestExit(); };

        PublishSettings(initialSettings);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        ApplyUiSettings(initialSettings);

        hardwareThread = new Thread(new ThreadStart(delegate { HardwareWorker(initialSettings); }));
        hardwareThread.Name = "capsblink hardware";
        hardwareThread.IsBackground = true;
        hardwareThread.Start();

        uiTimer = new System.Windows.Forms.Timer();
        uiTimer.Interval = 250;
        uiTimer.Tick += delegate { OnUiTimer(); };
        uiTimer.Start();
        Application.Run();

        RequestExit();
        DrainHardwareThread();
        if (hardwareFailure != null)
            throw new InvalidOperationException("The keyboard indicator worker failed.", hardwareFailure);
        return 0;
    }

    static ParsedCommandLine ParseCommandLine(string[] args)
    {
        var parsed = new ParsedCommandLine();
        for (int i = 0; i < args.Length; ++i)
        {
            string argument = args[i];
            if (!argument.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException("Unknown argument: " + argument);

            string token = argument.Substring(2);
            int equals = token.IndexOf('=');
            string name = equals >= 0 ? token.Substring(0, equals) : token;
            string inlineValue = equals >= 0 ? token.Substring(equals + 1) : null;
            string key;
            string flagValue;
            if (TryMapFlag(name, out key, out flagValue))
            {
                if (inlineValue != null)
                    throw new ArgumentException("--" + name + " does not accept a value.");
                parsed.Settings[key] = flagValue;
                continue;
            }

            if (name.Equals("configure-only", StringComparison.OrdinalIgnoreCase) ||
                name.Equals("reload", StringComparison.OrdinalIgnoreCase) ||
                name.Equals("exit", StringComparison.OrdinalIgnoreCase))
            {
                if (inlineValue != null)
                    throw new ArgumentException("--" + name + " does not accept a value.");
                if (name.Equals("configure-only", StringComparison.OrdinalIgnoreCase))
                    parsed.ConfigureOnly = true;
                else if (name.Equals("reload", StringComparison.OrdinalIgnoreCase))
                    parsed.ReloadRequested = true;
                else
                    parsed.ExitRequested = true;
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
                throw new ArgumentException("Unknown option: --" + name);
            value = inlineValue ?? RequireNextValue(args, ref i, "--" + name);
            parsed.Settings[key] = NormalizeSettingValue(key, value);
        }

        if (parsed.ReloadRequested && parsed.ExitRequested)
            throw new ArgumentException("--reload and --exit cannot be combined.");
        if (parsed.ConfigureOnly && (parsed.ReloadRequested || parsed.ExitRequested))
            throw new ArgumentException("--configure-only cannot be combined with resident-control actions.");
        if ((parsed.ReloadRequested || parsed.ExitRequested) && parsed.Settings.Count != 0)
            throw new ArgumentException("Resident-control actions cannot be combined with setting changes.");
        return parsed;
    }

    static bool TryMapFlag(string name, out string key, out string value)
    {
        key = null;
        value = null;
        if (name.Equals("startup", StringComparison.OrdinalIgnoreCase))
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
        if (name.Equals("keyboard-target-path", StringComparison.OrdinalIgnoreCase))
            return KeyboardTargetKey;
        if (name.Equals("blink-interval-ms", StringComparison.OrdinalIgnoreCase))
            return BlinkIntervalKey;
        if (name.Equals("run-at-startup", StringComparison.OrdinalIgnoreCase))
            return StartupKey;
        if (name.Equals("show-tray-icon", StringComparison.OrdinalIgnoreCase))
            return ShowTrayKey;
        if (name.Equals("show-menu-as-dropdown", StringComparison.OrdinalIgnoreCase))
            return DropdownKey;
        return null;
    }

    static string RequireNextValue(string[] args, ref int index, string option)
    {
        if (index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
            throw new ArgumentException("Missing value for " + option + ".");
        return args[++index];
    }

    static string CanonicalSettingName(string value)
    {
        if (String.Equals(value, KeyboardTargetKey, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "keyboard-target-path", StringComparison.OrdinalIgnoreCase))
            return KeyboardTargetKey;
        if (String.Equals(value, BlinkIntervalKey, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "blink-interval-ms", StringComparison.OrdinalIgnoreCase))
            return BlinkIntervalKey;
        if (String.Equals(value, StartupKey, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "run-at-startup", StringComparison.OrdinalIgnoreCase))
            return StartupKey;
        if (String.Equals(value, ShowTrayKey, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "show-tray", StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "show-tray-icon", StringComparison.OrdinalIgnoreCase))
            return ShowTrayKey;
        if (String.Equals(value, DropdownKey, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "show-menu-as-dropdown", StringComparison.OrdinalIgnoreCase))
            return DropdownKey;
        return null;
    }

    static string NormalizeSettingValue(string key, string value)
    {
        if (value == null || value.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0)
            throw new ArgumentException(key + " contains an invalid value.");
        string trimmed = value.Trim();
        if (key.Equals(KeyboardTargetKey, StringComparison.OrdinalIgnoreCase))
        {
            const string prefix = "\\Device\\KeyboardClass";
            if (!trimmed.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ||
                trimmed.Length <= prefix.Length ||
                trimmed.Length > 1024)
                throw new ArgumentException(KeyboardTargetKey + " must name a keyboard class device such as " + DefaultKeyboardTargetPath + ".");
            for (int i = prefix.Length; i < trimmed.Length; ++i)
            {
                if (trimmed[i] < '0' || trimmed[i] > '9')
                    throw new ArgumentException(KeyboardTargetKey + " must end in a decimal keyboard class number.");
            }
            return trimmed;
        }
        if (key.Equals(BlinkIntervalKey, StringComparison.OrdinalIgnoreCase))
        {
            int interval;
            if (!Int32.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out interval) ||
                interval < 50 || interval > 86400000)
                throw new ArgumentException(BlinkIntervalKey + " must be an integer from 50 through 86400000.");
            return interval.ToString(CultureInfo.InvariantCulture);
        }
        if (key.Equals(StartupKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(ShowTrayKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(DropdownKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeBoolean(trimmed, key);
        throw new ArgumentException("Unknown setting: " + key);
    }

    static string NormalizeBoolean(string value, string key)
    {
        if (value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("on", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("1", StringComparison.OrdinalIgnoreCase))
            return "true";
        if (value.Equals("false", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("no", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("off", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("0", StringComparison.OrdinalIgnoreCase))
            return "false";
        throw new ArgumentException(key + " must be true or false.");
    }

    static RuntimeSettings LoadRuntimeSettings()
    {
        Dictionary<string, string> values = LoadEffectiveSettingValues();
        return new RuntimeSettings
        {
            KeyboardTargetPath = values[KeyboardTargetKey],
            BlinkIntervalMs = Int32.Parse(values[BlinkIntervalKey], CultureInfo.InvariantCulture),
            RunAtStartup = values[StartupKey].Equals("true", StringComparison.Ordinal),
            ShowTrayIcon = values[ShowTrayKey].Equals("true", StringComparison.Ordinal),
            ShowMenuAsDropdown = values[DropdownKey].Equals("true", StringComparison.Ordinal)
        };
    }

    static Dictionary<string, string> LoadEffectiveSettingValues()
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { KeyboardTargetKey, DefaultKeyboardTargetPath },
            { BlinkIntervalKey, DefaultBlinkIntervalMs.ToString(CultureInfo.InvariantCulture) },
            { StartupKey, "false" },
            { ShowTrayKey, "true" },
            { DropdownKey, "true" }
        };

        foreach (KeyValuePair<string, string> rawSetting in ManagedIniFile.LoadSection(iniFile))
        {
            string rawKey = rawSetting.Key;
            string key = CanonicalSettingName(rawKey);
            if (key == null)
                throw new InvalidDataException("Unknown [Settings] key: " + rawKey);
            try
            {
                values[key] = NormalizeSettingValue(key, rawSetting.Value);
            }
            catch (ArgumentException ex)
            {
                throw new InvalidDataException("Invalid [Settings] " + rawKey + ": " + ex.Message, ex);
            }
        }
        return values;
    }

    static void EnsureIniFile()
    {
        ManagedIniFile.EnsureExists(iniFile);
    }

    static bool PersistSettingsAtomically(IDictionary<string, string> settings)
    {
        if (settings == null || settings.Count == 0)
        {
            EnsureIniFile();
            return true;
        }

        Dictionary<string, string> normalized;
        try
        {
            normalized = NormalizeSettingBatch(settings);
        }
        catch (Exception ex)
        {
            Log("Could not validate setting batch: " + ex.Message);
            return false;
        }

        string startupValue;
        if (!normalized.TryGetValue(StartupKey, out startupValue))
            return SaveIniOptions(normalized);

        bool startupDesired = startupValue.Equals("true", StringComparison.Ordinal);
        Dictionary<string, string> previousSettings = null;
        string transactionError;
        bool committed = ManagedStartupShortcut.CommitIniCoupledState(
            startupShortcut,
            startupDesired,
            delegate(out bool previousConfigured, out string snapshotError)
            {
                previousConfigured = false;
                snapshotError = null;
                try
                {
                    Dictionary<string, string> effective = LoadEffectiveSettingValues();
                    previousConfigured = effective[StartupKey].Equals(
                        "true",
                        StringComparison.Ordinal);
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
            Log("Could not commit Startup/INI transaction: " +
                (transactionError ?? "unknown error"));
        return committed;
    }

    static Dictionary<string, string> NormalizeSettingBatch(IDictionary<string, string> settings)
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
            Log(error);
        return saved;
    }

    static void ReconcileStartup(bool desired)
    {
        bool previous;
        string error;
        if (!ManagedStartupShortcut.SetDesiredState(
            startupShortcut,
            desired,
            out previous,
            out error))
            throw new IOException("Could not reconcile Startup: " + (error ?? "unknown error"));
    }

    static void HardwareWorker(RuntimeSettings initialSettings)
    {
        RuntimeSettings settings = initialSettings;
        try
        {
            while (!exitEvent.WaitOne(0))
            {
                bool reload = RunHardwareSession(ref settings);
                if (!reload || exitEvent.WaitOne(0))
                    break;
            }
        }
        catch (Exception ex)
        {
            hardwareFailure = ex;
            Log("Hardware worker error: " + ex);
            RequestExit();
        }
    }

    static bool RunHardwareSession(ref RuntimeSettings settings)
    {
        UInt32 bytesReturned = 0;
        IntPtr device = Flags.INVALID_HANDLE_VALUE;
        bool mappingDefined = false;
        Mutex deviceMutex = null;
        bool ownsDeviceMutex = false;
        string sessionTargetPath = settings.KeyboardTargetPath;
        string deviceName = "capsblinkKBD_" + Process.GetCurrentProcess().Id.ToString(CultureInfo.InvariantCulture);
        var indicators = new KEYBOARD_INDICATOR_PARAMETERS { unitID = 0, LEDflags = 0 };

        try
        {
            deviceMutex = new Mutex(
                false,
                ManagedNamedObjects.MachineScopedName(
                    "capsblink.Device",
                    sessionTargetPath.ToUpperInvariant()));
            try
            {
                ownsDeviceMutex = deviceMutex.WaitOne(0);
            }
            catch (AbandonedMutexException)
            {
                ownsDeviceMutex = true;
            }
            if (!ownsDeviceMutex)
                throw new InvalidOperationException("Another capsblink profile is already controlling " + sessionTargetPath + ".");

            Log("Starting hardware session with " + sessionTargetPath +
                " at " + settings.BlinkIntervalMs.ToString(CultureInfo.InvariantCulture) + " ms");
            if (!DefineDosDevice(
                Flags.DDD_RAW_TARGET_PATH | Flags.DDD_NO_BROADCAST_SYSTEM,
                deviceName,
                sessionTargetPath))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            mappingDefined = true;

            device = CreateFile(
                "\\\\.\\" + deviceName,
                Flags.GENERIC_WRITE,
                0,
                IntPtr.Zero,
                Flags.OPEN_EXISTING,
                0,
                IntPtr.Zero);
            if (device == Flags.INVALID_HANDLE_VALUE)
                throw new Win32Exception(Marshal.GetLastWin32Error());

            WaitHandle[] controls =
            {
                exitEvent,
                localReloadEvent,
                externalReloadEvent,
                externalExitEvent
            };
            while (!exitEvent.WaitOne(0))
            {
                QueryIndicators(device, ref indicators, ref bytesReturned);
                bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                UInt16 desiredFlags = capsLockOn
                    ? (UInt16)(indicators.LEDflags | Flags.KEYBOARD_CAPS_LOCK_ON)
                    : (UInt16)(indicators.LEDflags ^ Flags.KEYBOARD_CAPS_LOCK_ON);
                if (desiredFlags != indicators.LEDflags)
                {
                    indicators.LEDflags = desiredFlags;
                    SetIndicators(device, ref indicators, ref bytesReturned);
                }

                int signal = WaitHandle.WaitAny(controls, settings.BlinkIntervalMs);
                if (signal == WaitHandle.WaitTimeout)
                    continue;
                if (signal == 3)
                    RequestExit();
                if (signal == 0 || signal == 3)
                    return false;
                RuntimeSettings reloaded;
                try
                {
                    reloaded = LoadRuntimeSettings();
                }
                catch (Exception ex)
                {
                    Log("Could not reload settings; keeping the last valid settings: " + ex.Message);
                    continue;
                }
                try
                {
                    ReconcileStartup(reloaded.RunAtStartup);
                }
                catch (Exception ex)
                {
                    Log("Could not reconcile Startup during reload: " + ex.Message);
                }

                bool targetChanged = !String.Equals(
                    sessionTargetPath,
                    reloaded.KeyboardTargetPath,
                    StringComparison.OrdinalIgnoreCase);
                settings = reloaded;
                PublishSettings(reloaded);
                if (targetChanged)
                    return true;
                Log("Applied reloaded settings without reopening the keyboard device");
            }
            return false;
        }
        finally
        {
            if (device != Flags.INVALID_HANDLE_VALUE)
            {
                try
                {
                    SynchronizeCapsIndicator(device, ref indicators, ref bytesReturned);
                }
                catch (Exception ex)
                {
                    Log("Could not restore the Caps Lock indicator: " + ex.Message);
                }
                if (!CloseHandle(device))
                    Log("CloseHandle failed: " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
            }
            if (mappingDefined && !DefineDosDevice(
                Flags.DDD_REMOVE_DEFINITION |
                Flags.DDD_EXACT_MATCH_ON_REMOVE |
                Flags.DDD_RAW_TARGET_PATH |
                Flags.DDD_NO_BROADCAST_SYSTEM,
                deviceName,
                sessionTargetPath))
                Log("DefineDosDevice cleanup failed: " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
            if (ownsDeviceMutex)
            {
                try { deviceMutex.ReleaseMutex(); }
                catch (ApplicationException) { }
            }
            if (deviceMutex != null)
                deviceMutex.Dispose();
            Log("Hardware session stopped");
        }
    }

    static void QueryIndicators(
        IntPtr device,
        ref KEYBOARD_INDICATOR_PARAMETERS indicators,
        ref UInt32 bytesReturned)
    {
        if (!DeviceIoControl(
            device,
            Flags.IOCTL_KEYBOARD_QUERY_INDICATORS,
            IntPtr.Zero,
            0,
            ref indicators,
            (UInt32)Marshal.SizeOf(indicators),
            ref bytesReturned,
            IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }

    static void SetIndicators(
        IntPtr device,
        ref KEYBOARD_INDICATOR_PARAMETERS indicators,
        ref UInt32 bytesReturned)
    {
        if (!DeviceIoControl(
            device,
            Flags.IOCTL_KEYBOARD_SET_INDICATORS,
            ref indicators,
            (UInt32)Marshal.SizeOf(indicators),
            IntPtr.Zero,
            0,
            ref bytesReturned,
            IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }

    static void SynchronizeCapsIndicator(
        IntPtr device,
        ref KEYBOARD_INDICATOR_PARAMETERS indicators,
        ref UInt32 bytesReturned)
    {
        QueryIndicators(device, ref indicators, ref bytesReturned);
        bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        UInt16 desiredFlags = capsLockOn
            ? (UInt16)(indicators.LEDflags | Flags.KEYBOARD_CAPS_LOCK_ON)
            : (UInt16)(indicators.LEDflags & ~Flags.KEYBOARD_CAPS_LOCK_ON);
        if (desiredFlags != indicators.LEDflags)
        {
            indicators.LEDflags = desiredFlags;
            SetIndicators(device, ref indicators, ref bytesReturned);
        }
    }

    static void PublishSettings(RuntimeSettings settings)
    {
        lock (settingsLock)
        {
            activeSettings = settings;
            ++activeSettingsVersion;
        }
    }

    static RuntimeSettings GetPublishedSettings(out int version)
    {
        lock (settingsLock)
        {
            version = activeSettingsVersion;
            return activeSettings;
        }
    }

    static void OnUiTimer()
    {
        if (exitEvent.WaitOne(0))
        {
            Application.ExitThread();
            return;
        }
        int version;
        RuntimeSettings settings = GetPublishedSettings(out version);
        if (settings != null && version != appliedSettingsVersion)
        {
            ApplyUiSettings(settings);
            appliedSettingsVersion = version;
        }
    }

    static void ApplyUiSettings(RuntimeSettings settings)
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

    static void CreateTray(RuntimeSettings settings)
    {
        DisposeTray();
        ContextMenu menu = new ContextMenu();
        try
        {
            ManagedTrayBaseline.AppendHeader(menu, executableBaseName, "Version " + ProductVersion());
            miTarget = new MenuItem();
            miTarget.Click += delegate { PromptForPublishedSetting(KeyboardTargetKey); };
            miInterval = new MenuItem();
            miInterval.Click += delegate { PromptForPublishedSetting(BlinkIntervalKey); };
            miStartup = new MenuItem("Run at startup");
            miStartup.Click += delegate { PersistTraySetting(StartupKey, (!miStartup.Checked).ToString()); };
            miShowTray = new MenuItem("Show tray icon");
            miShowTray.Click += delegate
            {
                if (PersistTraySetting(ShowTrayKey, (!miShowTray.Checked).ToString()) && tray != null)
                    tray.Visible = false;
            };
            miDropdown = new MenuItem("Show menu as dropdown");
            miDropdown.Click += delegate { PersistTraySetting(DropdownKey, (!miDropdown.Checked).ToString()); };

            MenuItem[] settingsItems =
            {
                miTarget,
                miInterval,
                new MenuItem("-"),
                miStartup,
                miShowTray,
                miDropdown
            };
            if (settings.ShowMenuAsDropdown)
                menu.MenuItems.Add(new MenuItem("Settings", settingsItems));
            else
            {
                foreach (MenuItem item in settingsItems)
                    menu.MenuItems.Add(item);
            }

            menu.MenuItems.Add("-");
            menu.MenuItems.Add(new MenuItem("Reload settings", delegate { RequestReload(); }));
            menu.MenuItems.Add(new MenuItem("Open configuration", delegate { OpenPath(iniPath); }));
            menu.MenuItems.Add(new MenuItem("Open log", delegate { OpenPath(logPath); }));
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

    static void RefreshTray(RuntimeSettings settings)
    {
        if (tray == null)
            return;
        ManagedTrayBaseline.UpdateTooltip(tray, BuildTooltip(settings), executableBaseName);
        miTarget.Text = "Keyboard target: " + settings.KeyboardTargetPath;
        miInterval.Text = "Blink interval: " +
            settings.BlinkIntervalMs.ToString(CultureInfo.InvariantCulture) + " ms";
        string queryError;
        miStartup.Checked = ManagedStartupShortcut.IsInstalled(startupShortcut, out queryError);
        if (!String.IsNullOrEmpty(queryError))
            Log("Could not refresh Startup menu state: " + queryError);
        miShowTray.Checked = settings.ShowTrayIcon;
        miDropdown.Checked = settings.ShowMenuAsDropdown;
    }

    static string BuildTooltip(RuntimeSettings settings)
    {
        string target = Path.GetFileName(settings.KeyboardTargetPath.TrimEnd('\\'));
        if (String.IsNullOrEmpty(target))
            target = settings.KeyboardTargetPath;
        return executableBaseName + " - " + target + " - " +
            settings.BlinkIntervalMs.ToString(CultureInfo.InvariantCulture) + " ms";
    }

    static void PromptForSetting(string key, string currentValue)
    {
        string prompt = key == KeyboardTargetKey
            ? "Keyboard class device (for example, " + DefaultKeyboardTargetPath + "):"
            : "Blink interval in milliseconds (50 through 86400000):";
        string value;
        if (!ManagedTrayBaseline.TryPromptText(executableBaseName, prompt, currentValue, out value))
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

    static void PromptForPublishedSetting(string key)
    {
        int ignoredVersion;
        RuntimeSettings settings = GetPublishedSettings(out ignoredVersion);
        if (settings == null)
            return;
        string currentValue = key == KeyboardTargetKey
            ? settings.KeyboardTargetPath
            : settings.BlinkIntervalMs.ToString(CultureInfo.InvariantCulture);
        PromptForSetting(key, currentValue);
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
        RequestReload();
        return true;
    }

    static void ShowTrayError(string message)
    {
        MessageBox.Show(
            message,
            executableBaseName,
            MessageBoxButtons.OK,
            MessageBoxIcon.Error);
    }

    static void OpenPath(string path)
    {
        try
        {
            if (path.Equals(logPath, StringComparison.OrdinalIgnoreCase) && !File.Exists(path))
                Log("Log opened from tray");
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            ShowTrayError("Could not open " + path + ": " + ex.Message);
        }
    }

    static void RequestReload()
    {
        localReloadEvent.Set();
    }

    static void RequestExit()
    {
        exitEvent.Set();
    }

    static void DisposeTray()
    {
        if (tray != null && tray.ContextMenu != null)
            tray.ContextMenu.Dispose();
        ManagedTrayBaseline.DisposeNotifyIcon(ref tray);
        miTarget = null;
        miInterval = null;
        miStartup = null;
        miShowTray = null;
        miDropdown = null;
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

    static void DrainHardwareThread()
    {
        Thread draining = hardwareThread;
        if (draining == null || draining == Thread.CurrentThread)
            return;
        if (!draining.Join(15000))
            Log("Timed out waiting for the hardware worker to stop.");
        hardwareThread = null;
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
            Console.Error.WriteLine("ERROR: No resident " + executableBaseName + " instance is running for " + iniPath + ".");
            return false;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR: Could not signal the resident " + executableBaseName +
                " instance: " + ex.Message);
            return false;
        }
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
        if (externalReloadEvent != null)
        {
            externalReloadEvent.Dispose();
            externalReloadEvent = null;
        }
        if (externalExitEvent != null)
        {
            externalExitEvent.Dispose();
            externalExitEvent = null;
        }
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
            Log = Log
        };
    }

    static ManagedIniFileSpec BuildIniFileSpec()
    {
        return new ManagedIniFileSpec
        {
            FilePath = iniPath,
            SectionName = SettingsSection,
            DefaultContents =
                "[Settings]" + Environment.NewLine +
                "; All values can also be changed through command line or tray." + Environment.NewLine +
                KeyboardTargetKey + "=" + DefaultKeyboardTargetPath + Environment.NewLine +
                BlinkIntervalKey + "=" + DefaultBlinkIntervalMs.ToString(CultureInfo.InvariantCulture) + Environment.NewLine +
                StartupKey + "=false" + Environment.NewLine +
                ShowTrayKey + "=true" + Environment.NewLine +
                DropdownKey + "=true" + Environment.NewLine,
            Log = Log
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
        return String.IsNullOrEmpty(name) ? "capsblink" : name;
    }

    static string ProductVersion()
    {
        Version version = Assembly.GetExecutingAssembly().GetName().Version;
        return version == null ? "1.0.0.0" : version.ToString();
    }

    static string ProfileMutexName()
    {
        return ManagedNamedObjects.CurrentUserScopedName("capsblink.Profile", profileIdentity);
    }

    static string ReloadEventName()
    {
        return ManagedNamedObjects.CurrentUserScopedName("capsblink.Reload", profileIdentity);
    }

    static string ExitEventName()
    {
        return ManagedNamedObjects.CurrentUserScopedName("capsblink.Exit", profileIdentity);
    }

    static void PrepareCommandLineConsole(string[] args)
    {
#if !LEGACY_UTILITY_TESTS
        if (args == null || args.Length == 0)
            return;
        const Int32 StdOutputHandle = -11;
        const UInt32 AttachParentProcess = 0xFFFFFFFF;
        IntPtr output = GetStdHandle(StdOutputHandle);
        if (output != IntPtr.Zero && output != new IntPtr(-1))
            return;
        if (!AttachConsole(AttachParentProcess))
            return;
        var stdout = new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true };
        var stderr = new StreamWriter(Console.OpenStandardError()) { AutoFlush = true };
        Console.SetOut(stdout);
        Console.SetError(stderr);
#endif
    }

    static string StableHash(string value)
    {
        UInt32 hash = 2166136261;
        foreach (char character in (value ?? "").ToUpperInvariant())
        {
            hash ^= character;
            hash *= 16777619;
        }
        return hash.ToString("X8", CultureInfo.InvariantCulture);
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
        foreach (string argument in args)
        {
            if (predicate(argument))
                return true;
        }
        return false;
    }

    static void Usage()
    {
        Console.WriteLine("Usage: " + executableBaseName + ".exe [settings | action]");
        Console.WriteLine();
        Console.WriteLine("Persistent settings (INI and resident instance are updated together):");
        Console.WriteLine("  --keyboard-target-path PATH");
        Console.WriteLine("  --blink-interval-ms N");
        Console.WriteLine("  --startup | --no-startup");
        Console.WriteLine("  --tray | --no-tray");
        Console.WriteLine("  --dropdown | --flat-menu");
        Console.WriteLine("  --set Settings.Key=Value");
        Console.WriteLine("  --configure-only                 Save without starting hardware");
        Console.WriteLine();
        Console.WriteLine("Resident control:");
        Console.WriteLine("  --reload | --exit");
        Console.WriteLine();
        Console.WriteLine("Information: --help | --version");
        Console.WriteLine("Configuration: " + iniPath);
        Console.WriteLine("Startup: per-user shell:startup shortcut only");
    }

    static void Log(string message)
    {
        lock (logLock)
        {
            string line = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture) + "  " + message;
            string logError;
            if (!ManagedLogFile.AppendLine(
                new ManagedLogFileSpec
                {
                    FilePath = logPath,
                    LockWaitMilliseconds = 5000,
                    WriteUtf8Bom = true
                },
                line,
                out logError))
            {
                Console.Error.WriteLine(
                    "WARNING: Could not write capsblink log '" + logPath +
                    "': " + logError);
            }
            Console.WriteLine(line);
        }
    }
}

class Flags
{
    public static readonly IntPtr INVALID_HANDLE_VALUE = (IntPtr)(-1);
    public const UInt32 IOCTL_KEYBOARD_SET_INDICATORS =
        (0x0000000b << 16) | (0 << 14) | (0x0002 << 2) | 0;
    public const UInt32 IOCTL_KEYBOARD_QUERY_INDICATORS =
        (0x0000000b << 16) | (0 << 14) | (0x0010 << 2) | 0;

    public const UInt32 DDD_RAW_TARGET_PATH = 0x00000001;
    public const UInt32 DDD_REMOVE_DEFINITION = 0x00000002;
    public const UInt32 DDD_EXACT_MATCH_ON_REMOVE = 0x00000004;
    public const UInt32 DDD_NO_BROADCAST_SYSTEM = 0x00000008;

    public const UInt32 GENERIC_WRITE = 0x40000000;
    public const UInt32 OPEN_EXISTING = 3;
    public const UInt16 KEYBOARD_CAPS_LOCK_ON = 4;
}

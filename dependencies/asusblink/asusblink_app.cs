// asusblink product-owned implementation. Build it with the project metadata
// overlay and the root-level managed dependencies through BuildAsusBlink.cmd.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Windows.Forms;
using System.ComponentModel;
using System.Reflection;
using System.Globalization;
using System.Text.RegularExpressions;
using AIProjects.Dependencies;

public enum AsusFan { CPU = 0, GPU = 1, Mid = 2, XGM = 3 }
public enum AsusMode { Balanced = 0, Turbo = 1, Silent = 2 }
public enum AsusGPU { Eco = 0, Standard = 1, Ultimate = 2 }

public class AsusACPI
{
    const string FILE_NAME = @"\\.\\ATKACPI";
    const uint CONTROL_CODE = 0x0022240C;

    const uint DSTS = 0x53545344;
    const uint DEVS = 0x53564544;
    const uint INIT = 0x54494E49;

    public const uint UniversalControl = 0x00100021;

    public const int KB_Light_Up = 0xc4;
    public const int KB_Light_Down = 0xc5;
    public const int Brightness_Down = 0x10;
    public const int Brightness_Up = 0x20;
    public const int KB_Sleep = 0x6c;
    public const int KB_DUO_PgUpDn = 0x4B;
    public const int KB_DUO_SecondDisplay = 0x6A;

    public const int Touchpad_Toggle = 0x6B;

    public const int ChargerMode = 0x0012006C;

    public const int ChargerUSB = 2;
    public const int ChargerBarrel = 1;

    public const uint CPU_Fan = 0x00110013;
    public const uint GPU_Fan = 0x00110014;
    public const uint Mid_Fan = 0x00110031;

    public const uint PerformanceMode = 0x00120075;
    public const uint VivoBookMode = 0x00110019;

    public const uint GPUEco = 0x00090020;
    public const uint GPUXGConnected = 0x00090018;
    public const uint GPUXG = 0x00090019;
    public const uint GPUMux = 0x00090016;

    public const uint BatteryLimit = 0x00120057;
    public const uint ScreenOverdrive = 0x00050019;
    public const uint ScreenMiniled = 0x0005001E;

    public const uint DevsCPUFan = 0x00110022;
    public const uint DevsGPUFan = 0x00110023;

    public const uint DevsCPUFanCurve = 0x00110024;
    public const uint DevsGPUFanCurve = 0x00110025;
    public const uint DevsMidFanCurve = 0x00110032;

    public const int Temp_CPU = 0x00120094;
    public const int Temp_GPU = 0x00120097;

    public const int PPT_TotalA0 = 0x001200A0;
    public const int PPT_EDCA1 = 0x001200A1;
    public const int PPT_TDCA2 = 0x001200A2;
    public const int PPT_APUA3 = 0x001200A3;

    public const int PPT_CPUB0 = 0x001200B0;
    public const int PPT_CPUB1 = 0x001200B1;

    public const int PPT_GPUC0 = 0x001200C0;
    public const int PPT_APUC1 = 0x001200C1;
    public const int PPT_GPUC2 = 0x001200C2;

    public const int TUF_KB_BRIGHTNESS = 0x00050021;
    public const int TUF_KB = 0x00100056;
    public const int TUF_KB_STATE = 0x00100057;

    public const int MICMUTE_LED = 0x00040017;

    public const int TabletState = 0x00060077;
    public const int FnLock = 0x00100023;

    public const int ScreenPadToggle = 0x00050031;
    public const int ScreenPadBrightness = 0x00050032;

    public const int Tablet_Notebook = 0;
    public const int Tablet_Tablet = 1;
    public const int Tablet_Tent = 2;
    public const int Tablet_Rotated = 3;

    public const int PerformanceBalanced = 0;
    public const int PerformanceTurbo = 1;
    public const int PerformanceSilent = 2;
    public const int PerformanceManual = 4;

    public const int GPUModeEco = 0;
    public const int GPUModeStandard = 1;
    public const int GPUModeUltimate = 2;

    public const int MinTotal = 5;

    public static int MaxTotal = 150;
    public static int DefaultTotal = 125;

    public const int MinCPU = 5;
    public const int MaxCPU = 100;
    public const int DefaultCPU = 80;

    public const int MinGPUBoost = 5;
    public const int MaxGPUBoost = 25;

    public const int MinGPUTemp = 75;
    public const int MaxGPUTemp = 87;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile
    );

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(
        IntPtr hDevice,
        uint dwIoControlCode,
        byte[] lpInBuffer,
        uint nInBufferSize,
        byte[] lpOutBuffer,
        uint nOutBufferSize,
        ref uint lpBytesReturned,
        IntPtr lpOverlapped
    );

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const uint GENERIC_READ = 0x80000000;
    private const uint GENERIC_WRITE = 0x40000000;
    private const uint OPEN_EXISTING = 3;
    private const uint FILE_ATTRIBUTE_NORMAL = 0x80;
    private const uint FILE_SHARE_READ = 1;
    private const uint FILE_SHARE_WRITE = 2;

    private IntPtr handle;

    public AsusACPI()
    {
        handle = CreateFile(
            FILE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            IntPtr.Zero,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            IntPtr.Zero
        );

        if (handle == new IntPtr(-1))
        {
            throw new Win32Exception(
                Marshal.GetLastWin32Error(),
                "Can't connect to the ASUS ATKACPI device");
        }
    }

    public uint Control(uint dwIoControlCode, byte[] lpInBuffer, byte[] lpOutBuffer)
    {
        uint lpBytesReturned = 0;
        bool ok = DeviceIoControl(
            handle,
            dwIoControlCode,
            lpInBuffer,
            (uint)lpInBuffer.Length,
            lpOutBuffer,
            (uint)lpOutBuffer.Length,
            ref lpBytesReturned,
            IntPtr.Zero
        );
        if (!ok)
        {
            int error = Marshal.GetLastWin32Error();
            throw new Win32Exception(error, "ACPI DeviceIoControl failed");
        }
        return lpBytesReturned;
    }

    protected byte[] CallMethod(uint MethodID, byte[] args)
    {
        byte[] acpiBuf = new byte[8 + args.Length];
        byte[] outBuffer = new byte[16];

        BitConverter.GetBytes((uint)MethodID).CopyTo(acpiBuf, 0);
        BitConverter.GetBytes((uint)args.Length).CopyTo(acpiBuf, 4);
        Array.Copy(args, 0, acpiBuf, 8, args.Length);

        uint bytesReturned = Control(CONTROL_CODE, acpiBuf, outBuffer);
        if (bytesReturned < sizeof(int))
            throw new InvalidDataException(
                "ACPI returned a truncated response (" + bytesReturned + " bytes).");

        return outBuffer;
    }

    public int DeviceSet(uint DeviceID, int Status, string logName)
    {
        byte[] args = new byte[8];
        BitConverter.GetBytes((uint)DeviceID).CopyTo(args, 0);
        BitConverter.GetBytes((uint)Status).CopyTo(args, 4);

        byte[] status = CallMethod(DEVS, args);
        int result = BitConverter.ToInt32(status, 0);

        Console.WriteLine(string.Format("{0} = {1} : {2}", logName, Status, (result == 1 ? "OK" : result.ToString())));
        return result;
    }

    public int DeviceSet(uint DeviceID, byte[] Params, string logName)
    {
        byte[] args = new byte[4 + Params.Length];
        BitConverter.GetBytes((uint)DeviceID).CopyTo(args, 0);
        Params.CopyTo(args, 4);

        byte[] status = CallMethod(DEVS, args);
        int result = BitConverter.ToInt32(status, 0);

        Console.WriteLine(string.Format("{0} = {1} : {2}", logName, BitConverter.ToString(Params), (result == 1 ? "OK" : result.ToString())));
        return BitConverter.ToInt32(status, 0);
    }

    public int DeviceGet(uint DeviceID)
    {
        byte[] args = new byte[8];
        BitConverter.GetBytes((uint)DeviceID).CopyTo(args, 0);
        byte[] status = CallMethod(DSTS, args);

        return BitConverter.ToInt32(status, 0) - 65536;
    }

    public byte[] DeviceGetBuffer(uint DeviceID, uint Status = 0)
    {
        byte[] args = new byte[8];
        BitConverter.GetBytes((uint)DeviceID).CopyTo(args, 0);
        BitConverter.GetBytes((uint)Status).CopyTo(args, 4);

        return CallMethod(DSTS, args);
    }

    public void Close()
    {
        if (handle != IntPtr.Zero && handle != new IntPtr(-1))
        {
            IntPtr closingHandle = handle;
            handle = IntPtr.Zero;
            if (!CloseHandle(closingHandle))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CloseHandle(ATKACPI) failed");
        }
    }

    public int SetMicLed(int state)
    {
        return DeviceSet(MICMUTE_LED, state, "MicLED");
    }

    public int SetKeyboardState(int kbParam)
    {
        return DeviceSet((uint)TUF_KB_BRIGHTNESS, kbParam, "KB_Brightness");
    }
}

// ===== Program class (replace existing) =====
class Program
{
    const string applicationVersion = "1.0.0.0";
    const string SettingsSection = "Settings";
    const string ErrorLogKey = "ErrorLog";
    const string ErrorRetryKey = "ErrorRetry";
    const string ErrorActionKey = "ErrorAction";
    const string StartupKey = "RunAtStartup";
    const string ShowTrayKey = "ShowTrayIcon";
    const string DropdownKey = "ShowMenuAsDropdown";
    const string MicStateKey = "MicState";
    const string MicIntervalKey = "MicInterval";
    const string MicDurationKey = "MicDuration";
    const string KeyboardStateKey = "KeyboardState";
    const string KeyboardIntervalKey = "KeyboardInterval";
    const string KeyboardDurationKey = "KeyboardDuration";
    static readonly long HDD_LOW = 100 * 1024;
    static readonly long HDD_MID = 1 * 1024 * 1024;
    static readonly long HDD_HIGH = 10 * 1024 * 1024;

    static AsusACPI acpi;
    static readonly object acpiLock = new object();
    static readonly object logLock = new object();

    static volatile bool paused = false;
    static volatile bool exiting = false;
    static int processExitCode = 0;
    static Mutex profileMutex = null;
    static bool ownsProfileMutex = false;
    static EventWaitHandle externalReloadEvent = null;
    static EventWaitHandle externalExitEvent = null;

    static int errorRetryTimes = 3;
    static string errorLogPath = null;
    static List<string> errorActions = new List<string>();
    // Startup is intentionally driven by the INI, not a stale snapshot of the
    // command line that happened to install the shortcut.
    static readonly string startupArguments = "";
    static readonly string executablePath = Assembly.GetEntryAssembly().Location;
    static readonly string executableDirectory = GetExecutableDirectory();
    static readonly string executableBaseName = GetExecutableBaseName();
    static readonly string iniPath = Path.Combine(executableDirectory, executableBaseName + ".ini");
    static readonly string defaultLogPath = Path.Combine(executableDirectory, executableBaseName + ".log");
    static readonly string profileIdentity = ManagedStartupShortcut.StableIdentityHash(iniPath);
    static readonly ManagedStartupShortcutSpec startupShortcut = BuildStartupShortcutSpec();
    static readonly ManagedIniFileSpec iniFile = BuildIniFileSpec();
    static readonly ManagedIniFileSpec legacyIniFile = BuildLegacyIniFileSpec();
    static string lastStartupQueryError = null;
    static RuntimeProfile activeProfile = null;

    sealed class RuntimeProfile
    {
        public Dictionary<string, string> Settings;
        public List<KeyValuePair<string, DeviceEvent>> Events;
        public bool RunAtStartup;
        public bool ShowTrayIcon;
        public bool ShowMenuAsDropdown;
        public int ErrorRetry;
        public string ErrorLog;
        public List<string> ErrorActions;
    }

    class DeviceEvent
    {
        public string Name;
        public List<int> States = new List<int>();
        public List<long> IntervalsMs = new List<long>();
        public long DurationMs = -1;
        public int Priority = 0;
        public string Condition = null;
        public Dictionary<string, string> Extras = new Dictionary<string, string>();
        public bool IsActive = false;
    }

    // Keep global view of what we launched so the tray can read details
    static List<KeyValuePair<string, DeviceEvent>> configuredEvents = new List<KeyValuePair<string, DeviceEvent>>();
    static List<Task> runningTasks = new List<Task>();
    static List<CancellationTokenSource> runningCts = new List<CancellationTokenSource>();
    static object runningLock = new object();

    // Tray UI references so we can update labels/checked state
    static NotifyIcon tray = null;
    static MenuItem miPause = null;
    static MenuItem miStartup = null;
    static MenuItem miDropdown = null;
    static MenuItem miShowTray = null;
    static MenuItem miChangeLog = null;
    static MenuItem miShowLog = null;
    static MenuItem miOperationHeader = null;
    static MenuItem miRunningHeader = null;
    static System.Windows.Forms.Timer trayRefreshTimer = null;
    static bool showMenuAsDropdown = true;
    static bool trayRebuildRequested = false;
    static bool residentControlTickActive = false;

    static object trayLock = new object();

    static void Log(string fmt, params object[] args)
    {
        lock (logLock)
        {
            string line = "[" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + "] " + string.Format(fmt, args);
            if (errorLogPath != null && errorLogPath != "off")
            {
                string logError;
                if (!ManagedLogFile.AppendLine(
                    new ManagedLogFileSpec
                    {
                        FilePath = errorLogPath,
                        LockWaitMilliseconds = 5000,
                        WriteUtf8Bom = true
                    },
                    line,
                    out logError))
                {
                    Console.Error.WriteLine(
                        "WARNING: Could not write asusblink log: " + logError);
                }
            }
            Console.WriteLine(line);
        }
    }

    static long ParseTimeMs(string s, string optionName)
    {
        if (string.IsNullOrWhiteSpace(s))
            throw new ArgumentException("Missing time value for " + optionName + ".");
        s = s.Trim().ToLowerInvariant();
        double multiplier = 1;
        string number = s;
        if (s.EndsWith("ms"))
        {
            number = s.Substring(0, s.Length - 2);
        }
        else if (s.EndsWith("s"))
        {
            number = s.Substring(0, s.Length - 1);
            multiplier = 1000;
        }
        else if (s.EndsWith("m"))
        {
            number = s.Substring(0, s.Length - 1);
            multiplier = 60 * 1000;
        }
        else if (s.EndsWith("h"))
        {
            number = s.Substring(0, s.Length - 1);
            multiplier = 3600 * 1000;
        }
        else if (s.EndsWith("d"))
        {
            number = s.Substring(0, s.Length - 1);
            multiplier = 24 * 3600 * 1000;
        }

        double parsed;
        if (!double.TryParse(number, NumberStyles.Float, CultureInfo.InvariantCulture, out parsed) ||
            double.IsNaN(parsed) || double.IsInfinity(parsed) || parsed < 0)
        {
            throw new ArgumentException("Invalid time for " + optionName + ": " + s);
        }

        double milliseconds = parsed * multiplier;
        if (milliseconds > Int64.MaxValue)
            throw new ArgumentOutOfRangeException(optionName, s, "Time value is too large.");
        if (milliseconds != Math.Truncate(milliseconds))
            throw new ArgumentException(
                "Time for " + optionName +
                " must resolve to a whole number of milliseconds: " + s);
        return checked((long)milliseconds);
    }

    static string[] SplitComma(string s)
    {
        if (s == null) return new string[0];
        return s.Split(new char[] { ',' }, StringSplitOptions.RemoveEmptyEntries).Select(x => x.Trim()).ToArray();
    }

    static bool ApplyDeviceState(string device, int state)
    {
        int tries = 0;
        while (true)
        {
            try
            {
                if (device == "mic")
                {
                    lock (acpiLock)
                    {
                        if (acpi == null)
                            throw new InvalidOperationException("ACPI interface is closed.");
                        if (acpi.SetMicLed(state) != 1)
                            throw new InvalidOperationException("ASUS firmware rejected the mic LED state.");
                    }
                }
                else if (device == "keyboard")
                {
                    lock (acpiLock)
                    {
                        if (acpi == null)
                            throw new InvalidOperationException("ACPI interface is closed.");
                        if (acpi.SetKeyboardState(state) != 1)
                            throw new InvalidOperationException("ASUS firmware rejected the keyboard state.");
                    }
                }
                else
                {
                    Log("Unknown device {0}", device);
                    return false;
                }
                return true;
            }
            catch (Exception ex)
            {
                tries++;
                Log("Error applying {0}={1}: {2}", device, state, ex.Message);
                if (tries > errorRetryTimes)
                {
                    processExitCode = 1;
                    if (errorActions.Contains("pause"))
                    {
                        paused = true;
                        Log("Pausing execution due to error-action pause. Use tray to resume.");
                    }
                    if (errorActions.Contains("exit"))
                    {
                        Log("Exiting due to error-action exit.");
                        processExitCode = 1;
                        RequestExit();
                        return false;
                    }
                    if (errorActions.Contains("crash"))
                    {
                        throw;
                    }
                    return false;
                }
                for (int i = 0; i < 5 && !exiting; ++i)
                    Thread.Sleep(100);
                if (exiting)
                    return false;
            }
        }
    }

    static bool WaitWhilePaused(
        CancellationToken token,
        Stopwatch activeTimer)
    {
        if (!paused)
            return !token.IsCancellationRequested;

        if (activeTimer != null)
            activeTimer.Stop();
        while (paused && !token.IsCancellationRequested)
            token.WaitHandle.WaitOne(200);
        if (activeTimer != null && !token.IsCancellationRequested)
            activeTimer.Start();
        return !token.IsCancellationRequested;
    }

    static void SleepWithCancel(
        long ms,
        CancellationToken token,
        Stopwatch activeTimer = null)
    {
        if (ms <= 0) return;
        int step = 100;
        long waited = 0;
        while (waited < ms && !token.IsCancellationRequested)
        {
            if (!WaitWhilePaused(token, activeTimer))
                break;
            int toSleep = (int)Math.Min(step, ms - waited);
            if (token.WaitHandle.WaitOne(toSleep))
                break;
            waited += toSleep;
        }
    }

    static string GetExecutableDirectory()
    {
        string dir = Path.GetDirectoryName(executablePath);
        return string.IsNullOrEmpty(dir) ? AppDomain.CurrentDomain.BaseDirectory : dir;
    }

    static string GetExecutableBaseName()
    {
        string name = Path.GetFileNameWithoutExtension(executablePath);
        return string.IsNullOrEmpty(name) ? "asusblink" : name;
    }

    static ManagedStartupShortcutSpec BuildStartupShortcutSpec()
    {
        return new ManagedStartupShortcutSpec
        {
            ProductName = executableBaseName,
            ExecutablePath = executablePath,
            WorkingDirectory = executableDirectory,
            Arguments = startupArguments,
            IdentityPath = iniPath,
            LegacyFileName = executableBaseName + ".lnk",
            LegacyFileNames = new[]
            {
                executableBaseName + "-" + StableHash(executablePath) + ".lnk"
            },
            Log = message => Log("{0}", message)
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
                "; Every value is available through the command line and tray." + Environment.NewLine +
                ErrorLogKey + "=" + Path.GetFileName(defaultLogPath) + Environment.NewLine +
                ErrorRetryKey + "=3" + Environment.NewLine +
                ErrorActionKey + "=continue" + Environment.NewLine +
                MicStateKey + "=off" + Environment.NewLine +
                MicIntervalKey + "=0" + Environment.NewLine +
                MicDurationKey + "=once" + Environment.NewLine +
                KeyboardStateKey + "=off" + Environment.NewLine +
                KeyboardIntervalKey + "=0" + Environment.NewLine +
                KeyboardDurationKey + "=once" + Environment.NewLine +
                StartupKey + "=false" + Environment.NewLine +
                ShowTrayKey + "=true" + Environment.NewLine +
                DropdownKey + "=true" + Environment.NewLine,
            Log = message => Log("{0}", message)
        };
    }

    static ManagedIniFileSpec BuildLegacyIniFileSpec()
    {
        return new ManagedIniFileSpec
        {
            FilePath = iniPath,
            SectionName = "Options",
            DefaultContents = "[Options]" + Environment.NewLine,
            Log = message => Log("{0}", message)
        };
    }

    static void EnsureIniFile()
    {
        ManagedIniFile.EnsureExists(iniFile);
    }

    static Dictionary<string, string> LoadIniOptions()
    {
        Dictionary<string, string> missingLegacy;
        Dictionary<string, string> values = ReadEffectiveIniOptions(out missingLegacy);
        if (missingLegacy.Count != 0)
        {
            if (!PersistSettingsAtomically(missingLegacy))
                throw new IOException("Could not migrate the legacy [Options] settings.");
            Log("Migrated {0} missing legacy [Options] setting(s) to [Settings].",
                missingLegacy.Count);
            values = ReadEffectiveIniOptions(out missingLegacy);
            if (missingLegacy.Count != 0)
                throw new IOException("Legacy [Options] migration could not be verified.");
        }
        return values;
    }

    static Dictionary<string, string> ReadEffectiveIniOptions(
        out Dictionary<string, string> missingLegacy)
    {
        missingLegacy = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (!File.Exists(iniPath))
            return DefaultSettings();

        Dictionary<string, string> raw = ManagedIniFile.LoadSection(iniFile);
        Dictionary<string, string> legacy = ManagedIniFile.LoadSection(legacyIniFile);
        var canonicalRaw = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (KeyValuePair<string, string> setting in raw)
        {
            string key = CanonicalSettingName(setting.Key);
            if (key == null)
                throw new InvalidDataException("Unknown [Settings] key: " + setting.Key);
            if (canonicalRaw.ContainsKey(key))
            {
                throw new InvalidDataException(
                    "Duplicate semantic [Settings] key: " + setting.Key +
                    " (canonical name " + key + ").");
            }
            try
            {
                canonicalRaw[key] = NormalizeSettingValue(key, setting.Value);
            }
            catch (ArgumentException ex)
            {
                throw new InvalidDataException(
                    "Invalid [Settings] " + setting.Key + ": " + ex.Message,
                    ex);
            }
        }

        Dictionary<string, string> canonicalLegacy =
            NormalizeLegacySettingBatch(legacy);
        foreach (KeyValuePair<string, string> setting in canonicalLegacy)
        {
            if (!canonicalRaw.ContainsKey(setting.Key))
                missingLegacy[setting.Key] = setting.Value;
        }

        var values = DefaultSettings();
        foreach (KeyValuePair<string, string> setting in canonicalLegacy)
            values[setting.Key] = setting.Value;
        foreach (KeyValuePair<string, string> setting in canonicalRaw)
            values[setting.Key] = setting.Value;
        return values;
    }

    static bool SaveIniOption(string key, string value)
    {
        var settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        string canonicalKey = CanonicalSettingName(key);
        if (canonicalKey == null)
            throw new ArgumentException("Unknown setting: " + key);
        settings[canonicalKey] = NormalizeSettingValue(canonicalKey, value);
        return SaveIniOptions(settings);
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
        if (!saved && !string.IsNullOrEmpty(error))
            Log("{0}", error);
        return saved;
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

            Dictionary<string, string> ignoredLegacy;
            Dictionary<string, string> prospective =
                ReadEffectiveIniOptions(out ignoredLegacy);
            foreach (KeyValuePair<string, string> setting in normalized)
                prospective[setting.Key] = setting.Value;
            // Validate relationships between settings before either the INI or
            // Startup shortcut is mutated.  Individual typed-value validation
            // alone cannot catch combinations such as pause-on-error with the
            // only resident control surface disabled.
            BuildRuntimeProfile(prospective);
        }
        catch (Exception ex)
        {
            Log("Could not validate setting batch: {0}", ex.Message);
            return false;
        }

        string startupValue;
        if (!normalized.TryGetValue(StartupKey, out startupValue))
            return SaveIniOptions(normalized);

        bool startupDesired = ParseBooleanValue(startupValue, StartupKey);
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
                    Dictionary<string, string> ignoredLegacy;
                    Dictionary<string, string> effective =
                        ReadEffectiveIniOptions(out ignoredLegacy);
                    previousConfigured = ParseBooleanValue(
                        effective[StartupKey],
                        StartupKey);
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
            Log("Could not commit Startup/INI transaction: {0}",
                transactionError ?? "unknown error");
        return committed;
    }

    static Dictionary<string, string> DefaultSettings()
    {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { ErrorLogKey, Path.GetFileName(defaultLogPath) },
            { ErrorRetryKey, "3" },
            { ErrorActionKey, "continue" },
            { MicStateKey, "off" },
            { MicIntervalKey, "0" },
            { MicDurationKey, "once" },
            { KeyboardStateKey, "off" },
            { KeyboardIntervalKey, "0" },
            { KeyboardDurationKey, "once" },
            { StartupKey, "false" },
            { ShowTrayKey, "true" },
            { DropdownKey, "true" }
        };
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

    static Dictionary<string, string> NormalizeLegacySettingBatch(
        IDictionary<string, string> settings)
    {
        var normalized = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        bool legacyNoTraySeen = false;
        bool legacyNoTray = false;
        foreach (KeyValuePair<string, string> setting in settings)
        {
            if (setting.Key.Equals("no-tray", StringComparison.OrdinalIgnoreCase))
            {
                legacyNoTraySeen = true;
                // The original [Options] parser treated a present but empty
                // no-tray flag as enabled. Preserve that one migration case;
                // canonical typed booleans remain strict like the other
                // managed resident applications.
                legacyNoTray = String.IsNullOrWhiteSpace(setting.Value)
                    ? true
                    : ParseBooleanValue(setting.Value, setting.Key);
                continue;
            }
            string key = CanonicalSettingName(setting.Key);
            if (key == null)
                throw new InvalidDataException("Unknown legacy [Options] key: " + setting.Key);
            normalized[key] = NormalizeSettingValue(key, setting.Value);
        }
        if (legacyNoTraySeen && !normalized.ContainsKey(ShowTrayKey))
            normalized[ShowTrayKey] = legacyNoTray ? "false" : "true";
        return normalized;
    }

    static string PortableIniPathValue(string path)
    {
        string fullPath = Path.GetFullPath(path);
        string directory = Path.GetDirectoryName(fullPath);
        if (string.Equals(directory, executableDirectory, StringComparison.OrdinalIgnoreCase))
            return Path.GetFileName(fullPath);
        return fullPath;
    }

    static string ResolveModuleLocalPath(string path)
    {
        if (string.IsNullOrEmpty(path) || path == "off")
        {
            return path;
        }
        return Path.IsPathRooted(path) ? path : Path.Combine(executableDirectory, path);
    }

    static bool ParseBooleanValue(string value, string key)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new ArgumentException("Missing boolean for " + key + ".");
        if (value == "1" ||
            value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("on", StringComparison.OrdinalIgnoreCase))
            return true;
        if (value == "0" ||
            value.Equals("false", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("no", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("off", StringComparison.OrdinalIgnoreCase))
            return false;
        throw new ArgumentException("Invalid boolean for " + key + ": " + value);
    }

    static bool OptionEnabled(
        Dictionary<string, string> opts,
        string key,
        bool fallback)
    {
        string value;
        return opts.TryGetValue(key, out value)
            ? ParseBooleanValue(value, key)
            : fallback;
    }

    static bool OptionEnabled(Dictionary<string, string> opts, string key)
    {
        return OptionEnabled(opts, key, false);
    }

    sealed class ParsedCommandLine
    {
        public readonly Dictionary<string, string> Settings =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        // Kept as field aliases for compatibility with the existing reflection
        // smoke tests. All direct options are now persistent by design.
        public readonly Dictionary<string, string> RuntimeOptions;
        public readonly Dictionary<string, string> PersistentOptions;
        public bool ConfigureOnly;
        public bool ReloadRequested;
        public bool ExitRequested;

        public ParsedCommandLine()
        {
            RuntimeOptions = Settings;
            PersistentOptions = Settings;
        }
    }

    static ParsedCommandLine ParseArgs(string[] args)
    {
        var parsed = new ParsedCommandLine();
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            if (!a.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException("Unknown argument: " + a);

            string token = a.Substring(2);
            int equals = token.IndexOf('=');
            string name = equals >= 0 ? token.Substring(0, equals) : token;
            string inlineValue = equals >= 0 ? token.Substring(equals + 1) : null;

            string aliasSetting;
            string aliasValue;
            if (TryMapPersistentAlias(name, out aliasSetting, out aliasValue))
            {
                if (equals >= 0)
                    throw new ArgumentException("--" + name + " does not accept a value.");
                parsed.Settings[aliasSetting] = aliasValue;
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
                AddPersistentSet(parsed, value);
                continue;
            }

            string key = DirectOptionSetting(name);
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

    static string RequireNextValue(string[] args, ref int index, string option)
    {
        if (index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
            throw new ArgumentException("Missing value for " + option + ".");
        return args[++index];
    }

    static bool IsKnownOptionName(string key)
    {
        return CanonicalSettingName(key) != null ||
            (key ?? "").Equals("no-tray", StringComparison.OrdinalIgnoreCase);
    }

    static string DirectOptionSetting(string name)
    {
        return CanonicalSettingName(name);
    }

    static bool TryMapPersistentAlias(
        string key,
        out string setting,
        out string value)
    {
        setting = null;
        value = null;
        if (key.Equals("startup", StringComparison.OrdinalIgnoreCase))
        {
            setting = StartupKey;
            value = "true";
        }
        else if (key.Equals("no-startup", StringComparison.OrdinalIgnoreCase))
        {
            setting = StartupKey;
            value = "false";
        }
        else if (key.Equals("tray", StringComparison.OrdinalIgnoreCase))
        {
            setting = ShowTrayKey;
            value = "true";
        }
        else if (key.Equals("no-tray", StringComparison.OrdinalIgnoreCase))
        {
            setting = ShowTrayKey;
            value = "false";
        }
        else if (key.Equals("dropdown", StringComparison.OrdinalIgnoreCase))
        {
            setting = DropdownKey;
            value = "true";
        }
        else if (key.Equals("flat-menu", StringComparison.OrdinalIgnoreCase))
        {
            setting = DropdownKey;
            value = "false";
        }
        return setting != null;
    }

    static void AddPersistentSet(ParsedCommandLine parsed, string specification)
    {
        if (string.IsNullOrWhiteSpace(specification))
            throw new ArgumentException("--set requires Settings.Key=Value.");
        int equals = specification.IndexOf('=');
        if (equals <= 0)
            throw new ArgumentException("--set requires Settings.Key=Value.");

        string key = specification.Substring(0, equals).Trim();
        string value = specification.Substring(equals + 1).Trim();
        if (key.StartsWith(SettingsSection + ".", StringComparison.OrdinalIgnoreCase))
            key = key.Substring(SettingsSection.Length + 1);
        else if (key.StartsWith("Options.", StringComparison.OrdinalIgnoreCase))
            key = key.Substring("Options.".Length);
        if (key.Equals("no-tray", StringComparison.OrdinalIgnoreCase))
        {
            parsed.Settings[ShowTrayKey] = ParseBooleanValue(value, "no-tray")
                ? "false"
                : "true";
            return;
        }
        string canonicalKey = CanonicalSettingName(key);
        if (canonicalKey == null)
            throw new ArgumentException("Unknown --set setting: " + key);
        parsed.Settings[canonicalKey] = NormalizeSettingValue(canonicalKey, value);
    }

    static string CanonicalSettingName(string key)
    {
        if (String.IsNullOrWhiteSpace(key))
            return null;
        string compact = key.Trim().Replace("-", "").Replace("_", "");
        string[] fixedKeys =
        {
            ErrorLogKey, ErrorRetryKey, ErrorActionKey, StartupKey,
            ShowTrayKey, DropdownKey, MicStateKey, MicIntervalKey,
            MicDurationKey, KeyboardStateKey, KeyboardIntervalKey,
            KeyboardDurationKey
        };
        foreach (string fixedKey in fixedKeys)
        {
            if (compact.Equals(fixedKey, StringComparison.OrdinalIgnoreCase))
                return fixedKey;
        }

        if (compact.Equals("ShowTray", StringComparison.OrdinalIgnoreCase))
            return ShowTrayKey;
        if (compact.Equals("RunStartup", StringComparison.OrdinalIgnoreCase))
            return StartupKey;

        Match dynamic = Regex.Match(
            compact,
            @"^Event([0-9]{1,3})(Mic|Keyboard|Hdd)(State|Interval|Duration)$",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        if (!dynamic.Success)
            return null;
        return "Event" + dynamic.Groups[1].Value +
            Capitalize(dynamic.Groups[2].Value) + Capitalize(dynamic.Groups[3].Value);
    }

    static string Capitalize(string value)
    {
        if (String.IsNullOrEmpty(value))
            return value;
        return Char.ToUpperInvariant(value[0]) + value.Substring(1).ToLowerInvariant();
    }

    static string CanonicalizeOptionValue(string key, string value)
    {
        string canonicalKey = CanonicalSettingName(key);
        if (canonicalKey == null)
            throw new ArgumentException("Unknown setting: " + key);
        return NormalizeSettingValue(canonicalKey, value);
    }

    static string NormalizeSettingValue(string key, string value)
    {
        if (key.Equals(StartupKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(ShowTrayKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(DropdownKey, StringComparison.OrdinalIgnoreCase))
            return ParseBooleanValue(value, key) ? "true" : "false";

        if (string.IsNullOrWhiteSpace(value))
            throw new ArgumentException(key + " requires a value.");
        string trimmed = value.Trim();

        if (key.Equals(ErrorRetryKey, StringComparison.OrdinalIgnoreCase))
        {
            int retry;
            if (!int.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out retry) ||
                retry < 0 || retry > 100)
                throw new ArgumentException(ErrorRetryKey + " must be an integer from 0 to 100.");
            return retry.ToString(CultureInfo.InvariantCulture);
        }

        if (key.Equals(ErrorActionKey, StringComparison.OrdinalIgnoreCase))
        {
            var allowedActions = new HashSet<string>(
                new[] { "exit", "continue", "pause", "crash", "log" },
                StringComparer.OrdinalIgnoreCase);
            string[] actions = SplitComma(trimmed)
                .Select(action => action.ToLowerInvariant())
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToArray();
            if (actions.Length == 0 || actions.Any(action => !allowedActions.Contains(action)))
                throw new ArgumentException(
                    ErrorActionKey + " must contain only exit, continue, pause, crash, or log.");
            return string.Join(",", actions);
        }

        if (key.EndsWith("State", StringComparison.OrdinalIgnoreCase))
        {
            if (trimmed.Equals("off", StringComparison.OrdinalIgnoreCase))
                return "off";
            int maximum = key.IndexOf("mic", StringComparison.OrdinalIgnoreCase) >= 0 ? 1 : 255;
            string[] states = SplitComma(trimmed);
            if (states.Length == 0)
                throw new ArgumentException(key + " must contain at least one state.");
            var canonicalStates = new List<string>();
            foreach (string stateText in states)
            {
                int state;
                if (!int.TryParse(stateText, NumberStyles.Integer, CultureInfo.InvariantCulture, out state) ||
                    state < 0 || state > maximum)
                    throw new ArgumentException(
                        "Invalid state '" + stateText + "' for " + key +
                        ". Expected 0.." + maximum + ".");
                canonicalStates.Add(state.ToString(CultureInfo.InvariantCulture));
            }
            return String.Join(",", canonicalStates);
        }
        if (key.EndsWith("Interval", StringComparison.OrdinalIgnoreCase))
        {
            string[] intervals = SplitComma(trimmed);
            if (intervals.Length == 0)
                throw new ArgumentException(key + " must contain at least one time.");
            foreach (string interval in intervals)
                ParseTimeMs(interval, key);
            return String.Join(",", intervals.Select(item => item.ToLowerInvariant()));
        }
        if (key.EndsWith("Duration", StringComparison.OrdinalIgnoreCase))
        {
            if (trimmed.Equals("once", StringComparison.OrdinalIgnoreCase))
                return "once";
            ParseTimeMs(trimmed, key);
            return trimmed.ToLowerInvariant();
        }
        if (key.Equals(ErrorLogKey, StringComparison.OrdinalIgnoreCase) &&
            trimmed.IndexOfAny(new char[] { '\r', '\n', '\0' }) >= 0)
        {
            throw new ArgumentException(ErrorLogKey + " cannot contain line breaks or NUL characters.");
        }
        return trimmed;
    }

    static void ValidateOptions(
        Dictionary<string, string> options,
        string sourceName)
    {
        foreach (var option in options)
        {
            string key = CanonicalSettingName(option.Key);
            if (key == null)
                throw new ArgumentException("Unknown " + sourceName + " option: " + option.Key);
            NormalizeSettingValue(key, option.Value);
        }
    }

    static DeviceEvent BuildEventFromOptions(string name, Dictionary<string, string> options)
    {
        DeviceEvent ev = new DeviceEvent();
        ev.Name = name;

        string s = null;
        if (options.TryGetValue(name + "-state", out s) || options.TryGetValue(name + "_state", out s))
        {
            if (s.Equals("off", StringComparison.OrdinalIgnoreCase))
                return ev;
            foreach (var part in SplitComma(s))
            {
                int val;
                int max = name == "mic" ? 1 : 255;
                if (!int.TryParse(part, NumberStyles.Integer, CultureInfo.InvariantCulture, out val) ||
                    val < 0 || val > max)
                {
                    throw new ArgumentException(
                        "Invalid " + name + " state '" + part +
                        "'. Expected 0.." + max + ".");
                }
                ev.States.Add(val);
            }
            if (ev.States.Count == 0)
                throw new ArgumentException(name + "-state must contain at least one state.");
        }

        string iv = null;
        if (options.TryGetValue(name + "-interval", out iv) || options.TryGetValue(name + "_interval", out iv))
        {
            if (ev.States.Count == 0)
                throw new ArgumentException(name + "-interval requires " + name + "-state.");
            foreach (var part in SplitComma(iv))
                ev.IntervalsMs.Add(ParseTimeMs(part, name + "-interval"));
            if (ev.IntervalsMs.Count == 0)
                throw new ArgumentException(name + "-interval must contain at least one time.");
        }

        string du = null;
        if (options.TryGetValue(name + "-duration", out du) || options.TryGetValue(name + "_duration", out du))
        {
            if (ev.States.Count == 0)
                throw new ArgumentException(name + "-duration requires " + name + "-state.");
            ev.DurationMs = du.Equals("once", StringComparison.OrdinalIgnoreCase)
                ? -1
                : ParseTimeMs(du, name + "-duration");
        }

        if (ev.States.Count > 0 && ev.IntervalsMs.Count == 0)
        {
            ev.IntervalsMs.Add(0L);
        }

        return ev;
    }

    static RuntimeProfile LoadRuntimeProfile()
    {
        return BuildRuntimeProfile(LoadIniOptions());
    }

    static RuntimeProfile BuildRuntimeProfile(Dictionary<string, string> settings)
    {
        if (settings == null)
            throw new ArgumentNullException("settings");
        var legacyOptions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (KeyValuePair<string, string> setting in settings)
        {
            string legacyName = LegacyEventOptionName(setting.Key);
            if (legacyName != null)
                legacyOptions[legacyName] = setting.Value;
        }

        var toRun = new List<KeyValuePair<string, DeviceEvent>>();
        DeviceEvent micEvent = BuildEventFromOptions("mic", legacyOptions);
        DeviceEvent keyboardEvent = BuildEventFromOptions("keyboard", legacyOptions);
        if (micEvent.States.Count > 0)
            toRun.Add(new KeyValuePair<string, DeviceEvent>("mic", micEvent));
        if (keyboardEvent.States.Count > 0)
            toRun.Add(new KeyValuePair<string, DeviceEvent>("keyboard", keyboardEvent));

        var eventNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string key in legacyOptions.Keys)
        {
            Match match = Regex.Match(
                key,
                @"^(event[0-9]+)-",
                RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
            if (match.Success)
                eventNames.Add(match.Groups[1].Value.ToLowerInvariant());
        }
        foreach (string eventName in eventNames.OrderBy(value => value))
        {
            var subOptions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (KeyValuePair<string, string> setting in legacyOptions)
            {
                if (setting.Key.StartsWith(eventName + "-", StringComparison.OrdinalIgnoreCase))
                    subOptions[setting.Key.Substring(eventName.Length + 1)] = setting.Value;
            }

            AddConfiguredEvent(toRun, eventName, "mic", subOptions);
            AddConfiguredEvent(toRun, eventName, "keyboard", subOptions);
            AddConfiguredEvent(toRun, eventName, "hdd", subOptions);
        }

        string[] actions = SplitComma(settings[ErrorActionKey])
            .Select(action => action.ToLowerInvariant())
            .ToArray();
        bool showTray = OptionEnabled(settings, ShowTrayKey, true);
        if (!showTray && actions.Contains("pause"))
            throw new InvalidDataException(
                ErrorActionKey + "=pause requires " + ShowTrayKey + "=true.");

        return new RuntimeProfile
        {
            Settings = settings,
            Events = toRun,
            RunAtStartup = OptionEnabled(settings, StartupKey, false),
            ShowTrayIcon = showTray,
            ShowMenuAsDropdown = OptionEnabled(settings, DropdownKey, true),
            ErrorRetry = Int32.Parse(settings[ErrorRetryKey], CultureInfo.InvariantCulture),
            ErrorLog = settings[ErrorLogKey].Equals("off", StringComparison.OrdinalIgnoreCase)
                ? "off"
                : ResolveModuleLocalPath(settings[ErrorLogKey]),
            ErrorActions = actions.ToList()
        };
    }

    static void AddConfiguredEvent(
        List<KeyValuePair<string, DeviceEvent>> toRun,
        string eventName,
        string device,
        Dictionary<string, string> options)
    {
        string stateKey = device + "-state";
        if (!options.ContainsKey(stateKey) ||
            options[stateKey].Equals("off", StringComparison.OrdinalIgnoreCase))
            return;
        DeviceEvent built = BuildEventFromOptions(device, options);
        built.Name = eventName;
        int priority;
        if (Int32.TryParse(eventName.Substring(5), NumberStyles.None,
            CultureInfo.InvariantCulture, out priority))
            built.Priority = priority;
        if (device.Equals("hdd", StringComparison.OrdinalIgnoreCase))
        {
            built.Condition = "HDD activity";
            built.Extras["handler"] = "hdd";
            device = "keyboard";
        }
        toRun.Add(new KeyValuePair<string, DeviceEvent>(device, built));
    }

    static string LegacyEventOptionName(string canonicalKey)
    {
        if (canonicalKey.Equals(MicStateKey, StringComparison.OrdinalIgnoreCase)) return "mic-state";
        if (canonicalKey.Equals(MicIntervalKey, StringComparison.OrdinalIgnoreCase)) return "mic-interval";
        if (canonicalKey.Equals(MicDurationKey, StringComparison.OrdinalIgnoreCase)) return "mic-duration";
        if (canonicalKey.Equals(KeyboardStateKey, StringComparison.OrdinalIgnoreCase)) return "keyboard-state";
        if (canonicalKey.Equals(KeyboardIntervalKey, StringComparison.OrdinalIgnoreCase)) return "keyboard-interval";
        if (canonicalKey.Equals(KeyboardDurationKey, StringComparison.OrdinalIgnoreCase)) return "keyboard-duration";

        Match dynamic = Regex.Match(
            canonicalKey ?? "",
            @"^Event([0-9]{1,3})(Mic|Keyboard|Hdd)(State|Interval|Duration)$",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        if (!dynamic.Success)
            return null;
        return "event" + dynamic.Groups[1].Value + "-" +
            dynamic.Groups[2].Value.ToLowerInvariant() + "-" +
            dynamic.Groups[3].Value.ToLowerInvariant();
    }

    static int GetHddActivityLevel()
    {
        try
        {
            using (PerformanceCounter pc = new PerformanceCounter("PhysicalDisk", "Disk Bytes/sec", "_Total"))
            {
                float v1 = pc.NextValue();
                Thread.Sleep(200);
                float v2 = pc.NextValue();
                long bytes = (long)v2;
                if (bytes == 0) return 0;
                if (bytes <= HDD_LOW) return 1;
                if (bytes <= HDD_MID) return 2;
                if (bytes <= HDD_HIGH) return 3;
                return 4;
            }
        }
        catch (Exception ex)
        {
            Log("HDD monitor error: " + ex.Message);
            return 0;
        }
    }

    static void GetBatteryStatus(out bool plugged, out int percent)
    {
        try
        {
            PowerStatus ps = SystemInformation.PowerStatus;
            plugged = (ps.PowerLineStatus == PowerLineStatus.Online);
            percent = (int)(ps.BatteryLifePercent * 100);
        }
        catch
        {
            plugged = true;
            percent = 100;
        }
    }

    static bool IsStartupInstalled()
    {
        string error;
        bool installed = ManagedStartupShortcut.IsInstalled(startupShortcut, out error);
        if (!string.IsNullOrEmpty(error) &&
            !string.Equals(error, lastStartupQueryError, StringComparison.Ordinal))
        {
            lastStartupQueryError = error;
            Log("Could not query Startup shortcut: {0}", error);
        }
        else if (string.IsNullOrEmpty(error))
        {
            lastStartupQueryError = null;
        }
        return installed;
    }

    static bool InstallStartupShortcut(bool install)
    {
        bool previousInstalled;
        string error;
        if (!ManagedStartupShortcut.SetDesiredState(
            startupShortcut,
            install,
            out previousInstalled,
            out error))
        {
            Log("Startup shortcut change failed: {0}", error ?? "unknown error");
            return false;
        }
        return true;
    }

    // Build and show the tray (minimal creation, detailed items are filled/updated by RefreshTray)
    static bool CreateTray()
    {
        ContextMenu creatingMenu = null;
        try
        {
            lock (trayLock)
            {
                if (tray != null) return true;

                // top-level context menu root
                var cm = new ContextMenu();
                creatingMenu = cm;
                ManagedTrayBaseline.AppendHeader(
                    cm,
                    executableBaseName,
                    "Version " + applicationVersion);

                Menu.MenuItemCollection operationItems = cm.MenuItems;
                if (showMenuAsDropdown)
                {
                    var generalMenu = new MenuItem("General");
                    cm.MenuItems.Add(generalMenu);
                    operationItems = generalMenu.MenuItems;
                }

                // Operation header (unclickable)
                miOperationHeader = new MenuItem("Operation:");
                miOperationHeader.Enabled = false;
                operationItems.Add(miOperationHeader);

                // Pause checkbox item (dynamic text, checked when paused)
                miPause = new MenuItem("Pause");
                miPause.Checked = paused;
                miPause.Click += (s, e) =>
                {
                    paused = !paused;
                    UpdatePauseMenu();
                    Log("Paused = {0}", paused);
                };
                operationItems.Add(miPause);

                // Run as startup checkbox
                miStartup = new MenuItem("Run as startup");
                miStartup.Checked = IsStartupInstalled();
                miStartup.Click += (s, e) =>
                {
                    bool want = !miStartup.Checked;
                    var settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    settings[StartupKey] = want ? "true" : "false";
                    miStartup.Checked = PersistSettingsAtomically(settings)
                        ? want
                        : IsStartupInstalled();
                    if (miStartup.Checked == want)
                        RequestResidentReload();
                };
                operationItems.Add(miStartup);

                miShowTray = new MenuItem("Show tray icon");
                miShowTray.Checked = true;
                miShowTray.Click += (s, e) =>
                {
                    var settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    settings[ShowTrayKey] = "false";
                    if (PersistSettingsAtomically(settings))
                    {
                        miShowTray.Checked = false;
                        RequestResidentReload();
                    }
                };
                operationItems.Add(miShowTray);

                miDropdown = new MenuItem("Show menu as dropdown");
                miDropdown.Checked = showMenuAsDropdown;
                miDropdown.Click += (s, e) =>
                {
                    bool want = !miDropdown.Checked;
                    var settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    settings[DropdownKey] = want ? "true" : "false";
                    if (PersistSettingsAtomically(settings))
                    {
                        showMenuAsDropdown = want;
                        miDropdown.Checked = want;
                        trayRebuildRequested = true;
                        RequestResidentReload();
                    }
                };
                operationItems.Add(miDropdown);

                operationItems.Add("-");
                operationItems.Add(new MenuItem("Reload settings", (s, e) =>
                {
                    RequestResidentReload();
                }));
                operationItems.Add(new MenuItem("Open configuration", (s, e) =>
                {
                    OpenPathFromTray(iniPath, "configuration");
                }));

                Menu.MenuItemCollection settingItems = operationItems;
                if (showMenuAsDropdown)
                {
                    var settingsMenu = new MenuItem("Settings");
                    cm.MenuItems.Add(settingsMenu);
                    settingItems = settingsMenu.MenuItems;
                }
                AddTextSettingItem(settingItems, ErrorLogKey, "Error log");
                AddTextSettingItem(settingItems, ErrorRetryKey, "Error retries");
                AddTextSettingItem(settingItems, ErrorActionKey, "Error actions");
                settingItems.Add("-");
                AddTextSettingItem(settingItems, MicStateKey, "Mic states (or off)");
                AddTextSettingItem(settingItems, MicIntervalKey, "Mic intervals");
                AddTextSettingItem(settingItems, MicDurationKey, "Mic duration");
                AddTextSettingItem(settingItems, KeyboardStateKey, "Keyboard states (or off)");
                AddTextSettingItem(settingItems, KeyboardIntervalKey, "Keyboard intervals");
                AddTextSettingItem(settingItems, KeyboardDurationKey, "Keyboard duration");
                if (activeProfile != null)
                {
                    string[] dynamicKeys = activeProfile.Settings.Keys
                        .Where(key => key.StartsWith("Event", StringComparison.OrdinalIgnoreCase))
                        .OrderBy(key => key)
                        .ToArray();
                    if (dynamicKeys.Length != 0)
                        settingItems.Add("-");
                    foreach (string dynamicKey in dynamicKeys)
                        AddTextSettingItem(settingItems, dynamicKey, dynamicKey);
                }
                settingItems.Add("-");
                var addEventSetting = new MenuItem("Add/edit event setting...");
                addEventSetting.Click += (s, e) => PromptEventSetting();
                settingItems.Add(addEventSetting);

                // Change log path
                miChangeLog = new MenuItem("Change log path");
                miChangeLog.Click += (s, e) =>
                {
                    using (SaveFileDialog sd = new SaveFileDialog())
                    {
                        sd.Title = "Select log file path";
                        sd.Filter = "Log files (*.log)|*.log|All files (*.*)|*.*";
                        sd.InitialDirectory = executableDirectory;
                        sd.FileName = Path.GetFileName(defaultLogPath);
                        DialogResult dr = sd.ShowDialog();
                        if (dr == DialogResult.OK)
                        {
                            string iniValue = PortableIniPathValue(sd.FileName);
                            if (SaveIniOption(ErrorLogKey, iniValue))
                            {
                                errorLogPath = ResolveModuleLocalPath(iniValue);
                                Log("Log path changed and saved to: {0}", errorLogPath);
                                RequestResidentReload();
                            }
                            else
                            {
                                MessageBox.Show(
                                    "Could not save the selected log path to " + iniPath + ".",
                                    "asusblink",
                                    MessageBoxButtons.OK,
                                    MessageBoxIcon.Error);
                            }
                        }
                    }
                };
                operationItems.Add(miChangeLog);

                // Show log file (opens Notepad or shows info)
                miShowLog = new MenuItem("Show log file");
                miShowLog.Click += (s, e) =>
                {
                    if (!string.IsNullOrEmpty(errorLogPath) && errorLogPath != "off" && File.Exists(errorLogPath))
                    {
                        OpenPathFromTray(errorLogPath, "log file");
                    }
                    else
                    {
                        string msg = "No log file configured.";
                        if (errorLogPath == "off") msg = "Error logging is disabled (error-log set to 'off').";
                        else if (!string.IsNullOrEmpty(errorLogPath)) msg = "Configured log path: " + errorLogPath + Environment.NewLine + "(file does not exist yet)";
                        MessageBox.Show(msg, "Log file");
                    }
                };
                operationItems.Add(miShowLog);

                // Exit
                var miExit = new MenuItem("Exit");
                miExit.Click += (s, e) =>
                {
                    RequestExit();
                    try { tray.Visible = false; } catch { }
                    Application.ExitThread();
                };
                operationItems.Add(miExit);

                cm.MenuItems.Add("-"); // separator

                // Running tasks header
                miRunningHeader = new MenuItem("Running Tasks:");
                miRunningHeader.Enabled = false;
                cm.MenuItems.Add(miRunningHeader);

                // A placeholder; real entries will be inserted/updated by RefreshTray
                cm.MenuItems.Add(new MenuItem("Loading...") { Enabled = false });

                tray = ManagedTrayBaseline.CreateNotifyIcon(
                    SystemIcons.Application,
                    BuildTrayTooltip(),
                    executableBaseName,
                    cm);
                creatingMenu = null;

                // Ensure pause menu reflects initial state
                UpdatePauseMenu();

                // Start a timer to refresh dynamic parts of the menu every second
                trayRefreshTimer = new System.Windows.Forms.Timer();
                trayRefreshTimer.Interval = 1000;
                trayRefreshTimer.Tick += (s, e) => { RefreshTrayItems(); };
                trayRefreshTimer.Start();
            }
            return true;
        }
        catch (Exception ex)
        {
            if (creatingMenu != null)
                creatingMenu.Dispose();
            Log("Tray create error: " + ex.Message);
            DisposeTray();
            return false;
        }
    }

    static void OpenPathFromTray(string path, string description)
    {
        try
        {
            string fullPath = Path.GetFullPath(path);
            Process.Start(new ProcessStartInfo(fullPath) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                "Could not open " + description + ": " + ex.Message,
                executableBaseName,
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    static void AddTextSettingItem(
        Menu.MenuItemCollection items,
        string key,
        string label)
    {
        string current = "";
        if (activeProfile != null)
            activeProfile.Settings.TryGetValue(key, out current);
        MenuItem item = new MenuItem(label + ": " + current);
        item.Click += delegate
        {
            string value;
            if (!ManagedTrayBaseline.TryPromptText(
                executableBaseName + " — " + key,
                "Enter " + key + ":",
                current,
                out value))
                return;
            SaveSettingFromTray(key, value);
        };
        items.Add(item);
    }

    static void PromptEventSetting()
    {
        string rawKey;
        if (!ManagedTrayBaseline.TryPromptText(
            executableBaseName + " — event setting",
            "Event setting name (for example Event1HddState):",
            "Event1HddState",
            out rawKey))
            return;
        string key = CanonicalSettingName(rawKey);
        if (key == null || !key.StartsWith("Event", StringComparison.OrdinalIgnoreCase))
        {
            MessageBox.Show(
                "The setting must be Event<number><Mic|Keyboard|Hdd><State|Interval|Duration>.",
                executableBaseName,
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return;
        }
        string current = "";
        if (activeProfile != null)
            activeProfile.Settings.TryGetValue(key, out current);
        string value;
        if (ManagedTrayBaseline.TryPromptText(
            executableBaseName + " — " + key,
            "Enter " + key + " (use off to disable a State setting):",
            current,
            out value))
            SaveSettingFromTray(key, value);
    }

    static void SaveSettingFromTray(string key, string value)
    {
        try
        {
            string canonicalKey = CanonicalSettingName(key);
            if (canonicalKey == null)
                throw new ArgumentException("Unknown setting: " + key);
            string canonicalValue = NormalizeSettingValue(canonicalKey, value);
            var settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            settings[canonicalKey] = canonicalValue;
            if (!PersistSettingsAtomically(settings))
                throw new IOException("The setting transaction failed.");
            RequestResidentReload();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                ex.Message,
                executableBaseName,
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    static void RequestResidentReload()
    {
        if (externalReloadEvent != null)
            externalReloadEvent.Set();
    }

    static void UpdatePauseMenu()
    {
        if (miPause == null) return;
        miPause.Checked = paused;
        if (paused) miPause.Text = "Resume";
        else miPause.Text = "Pause";
        if (miOperationHeader != null)
            miOperationHeader.Text = paused ? "Operation: Paused" : "Operation: Running";
    }

    static void DisposeTray()
    {
        lock (trayLock)
        {
            if (trayRefreshTimer != null)
            {
                trayRefreshTimer.Stop();
                trayRefreshTimer.Dispose();
                trayRefreshTimer = null;
            }
            if (tray != null)
            {
                if (tray.ContextMenu != null)
                    tray.ContextMenu.Dispose();
                ManagedTrayBaseline.DisposeNotifyIcon(ref tray);
            }
            miPause = null;
            miStartup = null;
            miDropdown = null;
            miShowTray = null;
            miChangeLog = null;
            miShowLog = null;
            miOperationHeader = null;
            miRunningHeader = null;
        }
    }

    // Build human-friendly description of a DeviceEvent
    static string DescribeEvent(DeviceEvent ev)
    {
        if (ev == null) return "";
        string states = ev.States != null ? string.Join(", ", ev.States) : "";
        string intervals = ev.IntervalsMs != null ? string.Join(", ", ev.IntervalsMs.Select(i => i.ToString() + "ms")) : "";
        string duration = ev.DurationMs == -1 ? "one-cycle" : (ev.DurationMs == 0 ? "infinite" : (ev.DurationMs.ToString() + "ms"));
        return string.Format("{0}: State(s): {1} | Interval(s): {2} | Duration: {3} | Priority: {4}", ev.Name, states, intervals, duration, ev.Priority);
    }

    // Refresh the dynamic menu entries under "Running Tasks" to reflect configuredEvents & runningTasks
    static void RefreshTrayItems()
    {
        lock (trayLock)
        {
            if (tray == null || tray.ContextMenu == null) return;
            ManagedTrayBaseline.UpdateTooltip(
                tray,
                BuildTrayTooltip(),
                executableBaseName);
            if (trayRebuildRequested)
            {
                trayRebuildRequested = false;
                DisposeTray();
                if (!CreateTray())
                {
                    processExitCode = 1;
                    RequestExit();
                    Application.ExitThread();
                }
                return;
            }

            // find index of Running Tasks header and remove the placeholder entries below it
            int idx = -1;
            for (int i = 0; i < tray.ContextMenu.MenuItems.Count; i++)
            {
                if (tray.ContextMenu.MenuItems[i] == miRunningHeader)
                {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) return;

            // remove all items after the RunningTasks header (until end), then re-add fresh ones
            // keep the header itself, so remove at idx+1 onwards
            while (tray.ContextMenu.MenuItems.Count > idx + 1)
            {
                MenuItem removed = tray.ContextMenu.MenuItems[idx + 1];
                tray.ContextMenu.MenuItems.RemoveAt(idx + 1);
                removed.Dispose();
            }

            UpdatePauseMenu();
            if (miStartup != null)
                miStartup.Checked = IsStartupInstalled();

            // Populate configuredEvents (the ones we started)
            lock (runningLock)
            {
                if (configuredEvents.Count == 0)
                {
                    tray.ContextMenu.MenuItems.Add(new MenuItem("No configured events") { Enabled = false });
                }
                else
                {
                    foreach (var kv in configuredEvents)
                    {
                        string device = kv.Key;
                        DeviceEvent ev = kv.Value;

                        // Unclickable item showing device name
                        tray.ContextMenu.MenuItems.Add(new MenuItem(string.Format("{0}", device)) { Enabled = false });

                        // Detail lines: state, interval, duration
                        tray.ContextMenu.MenuItems.Add(new MenuItem("  State: " + (ev.States != null && ev.States.Count > 0 ? string.Join(", ", ev.States) : "(none)")) { Enabled = false });
                        tray.ContextMenu.MenuItems.Add(new MenuItem("  Interval: " + (ev.IntervalsMs != null && ev.IntervalsMs.Count > 0 ? string.Join(", ", ev.IntervalsMs.Select(i => i.ToString() + "ms")) : "(none)")) { Enabled = false });
                        string durationText = ev.DurationMs == -1 ? "one-cycle" : (ev.DurationMs == 0 ? "infinite" : (ev.DurationMs.ToString() + "ms"));
                        tray.ContextMenu.MenuItems.Add(new MenuItem("  Duration: " + durationText) { Enabled = false });
                        if (!string.IsNullOrEmpty(ev.Condition)) tray.ContextMenu.MenuItems.Add(new MenuItem("  Condition: " + ev.Condition) { Enabled = false });

                        tray.ContextMenu.MenuItems.Add(new MenuItem("-") { Enabled = false });
                    }
                }
            }

            // show active task count
            int activeCount = 0;
            lock (runningLock)
            {
                activeCount = runningTasks.Count(t => !t.IsCompleted && !t.IsCanceled);
                if (runningTasks.Any(task => task.IsFaulted))
                {
                    processExitCode = 1;
                    RequestExit();
                    Application.ExitThread();
                }
            }
            tray.ContextMenu.MenuItems.Add(new MenuItem("Active tasks: " + activeCount) { Enabled = false });
        }
    }

    static string BuildTrayTooltip()
    {
        int configuredCount;
        int activeCount;
        lock (runningLock)
        {
            configuredCount = configuredEvents.Count;
            activeCount = runningTasks.Count(task => !task.IsCompleted);
        }
        string state = paused ? "paused" : (activeCount == 0 ? "idle" : "running");
        return executableBaseName + " - " + state + " - " +
            configuredCount.ToString(CultureInfo.InvariantCulture) +
            (configuredCount == 1 ? " event" : " events");
    }

    static bool IsHelpRequest(string[] args)
    {
        return args.Any(arg =>
            string.Equals(arg, "--help", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(arg, "-h", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(arg, "/?", StringComparison.OrdinalIgnoreCase));
    }

    static bool IsVersionRequest(string[] args)
    {
        return args.Any(arg =>
            string.Equals(arg, "--version", StringComparison.OrdinalIgnoreCase));
    }

    static string StableHash(string value)
    {
        UInt32 hash = 2166136261;
        foreach (char ch in (value ?? "").ToUpperInvariant())
        {
            hash ^= ch;
            hash *= 16777619;
        }
        return hash.ToString("X8", CultureInfo.InvariantCulture);
    }

    static string ProfileMutexName()
    {
        return ManagedNamedObjects.CurrentUserScopedName(
            "asusblink.Profile",
            profileIdentity);
    }

    static string ReloadEventName()
    {
        return ManagedNamedObjects.CurrentUserScopedName(
            "asusblink.Reload",
            profileIdentity);
    }

    static string ExitEventName()
    {
        return ManagedNamedObjects.CurrentUserScopedName(
            "asusblink.Exit",
            profileIdentity);
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
            Console.Error.WriteLine(
                "ERROR: No resident " + executableBaseName +
                " instance is running for " + iniPath + ".");
            return false;
        }
        catch (UnauthorizedAccessException ex)
        {
            Console.Error.WriteLine(
                "ERROR: Could not signal the resident " + executableBaseName +
                " instance: " + ex.Message);
            return false;
        }
    }

    static void InitializeControlObjects()
    {
        externalReloadEvent = new EventWaitHandle(
            false,
            EventResetMode.AutoReset,
            ReloadEventName());
        externalExitEvent = new EventWaitHandle(
            false,
            EventResetMode.AutoReset,
            ExitEventName());
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

    [STAThread]
    static int Main(string[] args)
    {
        if (IsHelpRequest(args))
        {
            PrintHelp();
            return 0;
        }
        if (IsVersionRequest(args))
        {
            Console.WriteLine(executableBaseName + " " + applicationVersion);
            return 0;
        }

        ParsedCommandLine commandLine;
        try
        {
            commandLine = ParseArgs(args);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR: " + ex.Message);
            PrintHelp();
            return 2;
        }

        if (commandLine.ExitRequested)
            return SignalExistingControl(ExitEventName(), "exit") ? 0 : 1;
        if (commandLine.ReloadRequested)
            return SignalExistingControl(ReloadEventName(), "reload") ? 0 : 1;

        int result;
        try
        {
            result = RunApplication(commandLine);
        }
        catch (Exception ex)
        {
            processExitCode = 1;
            Log("Fatal error: {0}", ex);
            Console.Error.WriteLine("ERROR: " + ex.Message);
            result = 1;
        }
        finally
        {
            RequestExit();
            if (!WaitForRunningTasks(Timeout.Infinite))
                processExitCode = 1;
            DisposeTray();
            CloseAcpi();
            DisposeControlObjects();
        }
        return Math.Max(result, processExitCode);
    }

    static int RunApplication(ParsedCommandLine commandLine)
    {
        InitializeControlObjects();
        bool ownsProfile = AcquireProfileMutex();

        Console.CancelKeyPress += (sender, e) =>
        {
            e.Cancel = true;
            RequestExit();
        };
        AppDomain.CurrentDomain.ProcessExit += (sender, e) =>
        {
            RequestExit();
        };

        bool iniExisted = File.Exists(iniPath);
        if (!iniExisted && !commandLine.Settings.ContainsKey(StartupKey))
        {
            string migrationError;
            bool hadOwnedStartup = ManagedStartupShortcut.HasOwnedTarget(
                startupShortcut,
                out migrationError);
            if (!String.IsNullOrEmpty(migrationError))
                throw new IOException(
                    "Could not inspect the legacy Startup shortcut: " + migrationError);
            if (hadOwnedStartup)
                commandLine.Settings[StartupKey] = "true";
        }
        if (!PersistSettingsAtomically(commandLine.Settings))
            throw new IOException("Could not persist the command-line setting batch.");

        RuntimeProfile initialProfile = LoadRuntimeProfile();
        if (!ownsProfile)
        {
            if (commandLine.Settings.Count != 0)
            {
                externalReloadEvent.Set();
                Console.WriteLine("Settings saved; the resident instance was asked to reload.");
                return 0;
            }
            if (commandLine.ConfigureOnly)
            {
                Console.WriteLine("Settings validated in " + iniPath + ".");
                return 0;
            }
            Console.Error.WriteLine(
                "ERROR: Another " + executableBaseName +
                " instance already owns profile " + iniPath + ".");
            return 1;
        }

        if (!InstallStartupShortcut(initialProfile.RunAtStartup))
            throw new IOException("Could not reconcile the configured Startup shortcut state.");
        if (commandLine.ConfigureOnly)
        {
            Console.WriteLine("Settings saved to " + iniPath + ".");
            return 0;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        StartRuntimeProfile(initialProfile);
        RunResidentLoop();
        return processExitCode;
    }

    static void StartRuntimeProfile(RuntimeProfile profile)
    {
        if (profile == null)
            throw new ArgumentNullException("profile");
        if (exiting)
            throw new InvalidOperationException("Cannot start a profile while the app is exiting.");

        errorRetryTimes = profile.ErrorRetry;
        errorLogPath = profile.ErrorLog;
        errorActions = new List<string>(profile.ErrorActions);
        activeProfile = profile;
        showMenuAsDropdown = profile.ShowMenuAsDropdown;
        trayRebuildRequested = false;

        if (profile.Events.Count != 0)
        {
            lock (acpiLock)
            {
                if (acpi != null)
                    throw new InvalidOperationException("The previous ACPI session is still open.");
                acpi = new AsusACPI();
            }
        }

        lock (runningLock)
        {
            configuredEvents.Clear();
            runningTasks.Clear();
            runningCts.Clear();
            configuredEvents.AddRange(profile.Events);

            foreach (IGrouping<string, KeyValuePair<string, DeviceEvent>> group in
                profile.Events.GroupBy(item => item.Key))
            {
                string device = group.Key;
                List<DeviceEvent> events = group
                    .Select(item => item.Value)
                    .OrderBy(item => item.Priority)
                    .ToList();
                var cancellation = new CancellationTokenSource();
                runningCts.Add(cancellation);
                runningTasks.Add(Task.Factory.StartNew(
                    () => RunDeviceEventGroup(device, events, cancellation.Token),
                    cancellation.Token,
                    TaskCreationOptions.LongRunning,
                    TaskScheduler.Default));
            }
        }

        DisposeTray();
        if (profile.ShowTrayIcon)
        {
            if (!CreateTray())
                throw new InvalidOperationException("Could not create the tray UI.");
            RefreshTrayItems();
        }

        Log("Loaded {0} configured event(s); tray is {1}.",
            profile.Events.Count,
            profile.ShowTrayIcon ? "enabled" : "disabled");
    }

    static bool StopRuntimeProfile()
    {
        CancellationTokenSource[] cancellationSources;
        lock (runningLock)
            cancellationSources = runningCts.ToArray();
        foreach (CancellationTokenSource cancellation in cancellationSources)
        {
            try { cancellation.Cancel(); }
            catch (ObjectDisposedException) { }
        }

        bool stopped = WaitForRunningTasks(Timeout.Infinite);
        lock (runningLock)
            configuredEvents.Clear();
        DisposeTray();
        CloseAcpi();
        return stopped;
    }

    static void ReloadRuntimeProfile()
    {
        RuntimeProfile nextProfile;
        try
        {
            nextProfile = LoadRuntimeProfile();
            if (!InstallStartupShortcut(nextProfile.RunAtStartup))
                throw new IOException("Could not reconcile the configured Startup shortcut state.");
        }
        catch (Exception ex)
        {
            Log("Settings reload rejected; the active profile was kept: {0}", ex.Message);
            return;
        }

        if (!StopRuntimeProfile())
            throw new InvalidOperationException("A worker failed while the previous profile was stopping.");
        StartRuntimeProfile(nextProfile);
        Log("Settings reloaded from {0}.", iniPath);
    }

    static bool RunningTasksFaulted()
    {
        lock (runningLock)
            return runningTasks.Any(task => task.IsFaulted);
    }

    static bool RunningTasksCompleted()
    {
        lock (runningLock)
            return runningTasks.All(task => task.IsCompleted);
    }

    static void RunResidentLoop()
    {
        using (var residentTimer = new System.Windows.Forms.Timer())
        {
            residentTimer.Interval = 200;
            residentTimer.Tick += delegate
            {
                if (residentControlTickActive)
                    return;
                residentControlTickActive = true;
                try
                {
                    if (exiting ||
                        (externalExitEvent != null && externalExitEvent.WaitOne(0)))
                    {
                        RequestExit();
                        Application.ExitThread();
                        return;
                    }
                    if (RunningTasksFaulted())
                    {
                        processExitCode = 1;
                        RequestExit();
                        Application.ExitThread();
                        return;
                    }
                    if (externalReloadEvent != null && externalReloadEvent.WaitOne(0))
                    {
                        ReloadRuntimeProfile();
                        if (exiting)
                        {
                            Application.ExitThread();
                            return;
                        }
                    }
                    if (activeProfile != null &&
                        !activeProfile.ShowTrayIcon &&
                        RunningTasksCompleted())
                    {
                        Application.ExitThread();
                    }
                }
                catch (Exception ex)
                {
                    processExitCode = 1;
                    Log("Resident control error: {0}", ex);
                    RequestExit();
                    Application.ExitThread();
                }
                finally
                {
                    residentControlTickActive = false;
                }
            };
            residentTimer.Start();
            Application.Run();
            residentTimer.Stop();
        }
    }

    static void RequestExit()
    {
        exiting = true;
        lock (runningLock)
        {
            foreach (var cts in runningCts)
            {
                try { cts.Cancel(); }
                catch { }
            }
        }
    }

    static bool WaitForRunningTasks(int timeoutMs)
    {
        Task[] tasks;
        CancellationTokenSource[] cancellationSources;
        lock (runningLock)
        {
            tasks = runningTasks.ToArray();
            cancellationSources = runningCts.ToArray();
        }

        bool cancellationRequested = cancellationSources.Any(
            source => source.IsCancellationRequested);
        bool succeeded = true;
        if (tasks.Length > 0)
        {
            try
            {
                if (!Task.WaitAll(tasks, timeoutMs))
                {
                    Log("Timed out waiting for {0} worker task(s) to stop.", tasks.Length);
                    succeeded = false;
                }
            }
            catch (AggregateException ex)
            {
                foreach (Exception inner in ex.Flatten().InnerExceptions)
                {
                    if (cancellationRequested && inner is OperationCanceledException)
                        continue;
                    succeeded = false;
                    Log("Task exception: {0}", inner);
                }
            }

            foreach (Task task in tasks)
            {
                if (task.IsFaulted || (task.IsCanceled && !cancellationRequested))
                    succeeded = false;
            }
        }

        foreach (CancellationTokenSource source in cancellationSources)
            source.Dispose();
        lock (runningLock)
        {
            runningCts.Clear();
            runningTasks.Clear();
        }
        return succeeded;
    }

    static void CloseAcpi()
    {
        lock (acpiLock)
        {
            if (acpi == null)
            {
                return;
            }

            try
            {
                acpi.Close();
            }
            catch (Exception ex)
            {
                processExitCode = 1;
                Log("ACPI cleanup error: {0}", ex.Message);
            }
            acpi = null;
        }
    }

    static void RunDeviceEventGroup(string device, List<DeviceEvent> events, CancellationToken token)
    {
        if (events == null || events.Count == 0) return;
        if (events.Count > 1)
        {
            Log("Serializing {0} events for device {1} by priority.", events.Count, device);
        }

        foreach (var ev in events.OrderBy(e => e.Priority))
        {
            if (token.IsCancellationRequested) break;
            RunEventLoop(device, ev, token);
        }
    }

    static bool IsHddActivityEvent(DeviceEvent ev)
    {
        string handler;
        return ev != null && ev.Extras.TryGetValue("handler", out handler) && string.Equals(handler, "hdd", StringComparison.OrdinalIgnoreCase);
    }

    static void RunEventLoop(string device, DeviceEvent ev, CancellationToken token)
    {
        Log("Starting event {0} for device {1} (priority {2})", ev.Name, device, ev.Priority);

        int sCount = ev.States.Count;
        if (sCount == 0) { Log("Event {0} has no states, exiting", ev.Name); return; }

        if (IsHddActivityEvent(ev))
        {
            RunHddActivityLoop(device, ev, token);
            return;
        }

        bool infinite = ev.DurationMs == 0;
        long durationMs = ev.DurationMs;

        if (durationMs == -1 && !infinite)
        {
            for (int idx = 0; idx < sCount && !token.IsCancellationRequested; idx++)
            {
                if (!WaitWhilePaused(token, null))
                    break;
                int state = ev.States[idx];
                long interval = ev.IntervalsMs.Count > 0 ? ev.IntervalsMs[idx % ev.IntervalsMs.Count] : 0;
                ApplyDeviceState(device, state);
                if (interval > 0)
                {
                    SleepWithCancel(interval, token);
                }
            }
            Log("Event {0} finished one cycle", ev.Name);
            return;
        }

        int pos = 0;
        Stopwatch elapsedTimer = Stopwatch.StartNew();
        while (!token.IsCancellationRequested)
        {
            if (!WaitWhilePaused(token, elapsedTimer))
                break;
            int state = ev.States[pos % sCount];
            long interval = ev.IntervalsMs.Count > 0 ? ev.IntervalsMs[pos % ev.IntervalsMs.Count] : 0;

            ApplyDeviceState(device, state);

            if (interval == 0 && ev.DurationMs == 0)
            {
                while (!token.IsCancellationRequested)
                {
                    if (!WaitWhilePaused(token, null))
                        break;
                    ApplyDeviceState(device, state);
                    if (token.WaitHandle.WaitOne(200))
                        break;
                }
            }

            if (interval > 0)
            {
                SleepWithCancel(interval, token, elapsedTimer);
            }
            else if (!infinite)
            {
                SleepWithCancel(200, token, elapsedTimer);
            }

            if (!infinite && durationMs > 0)
            {
                if (elapsedTimer.ElapsedMilliseconds >= durationMs) break;
            }

            pos++;
        }

        Log("Event {0} ended", ev.Name);
    }

    static int StateForHddActivity(DeviceEvent ev)
    {
        int activityLevel = GetHddActivityLevel();
        int stateIndex = Math.Min(activityLevel, ev.States.Count - 1);
        return ev.States[stateIndex];
    }

    static void RunHddActivityLoop(string device, DeviceEvent ev, CancellationToken token)
    {
        long pollInterval = ev.IntervalsMs.Count > 0 ? ev.IntervalsMs[0] : 500;
        if (pollInterval <= 0) pollInterval = 500;

        if (ev.DurationMs == -1)
        {
            ApplyDeviceState(device, StateForHddActivity(ev));
            Log("Event {0} finished one HDD activity sample", ev.Name);
            return;
        }

        bool infinite = ev.DurationMs == 0;
        Stopwatch elapsedTimer = Stopwatch.StartNew();
        while (!token.IsCancellationRequested)
        {
            if (!WaitWhilePaused(token, elapsedTimer))
                break;
            ApplyDeviceState(device, StateForHddActivity(ev));

            if (!infinite && ev.DurationMs > 0)
            {
                if (elapsedTimer.ElapsedMilliseconds >= ev.DurationMs) break;
            }

            SleepWithCancel(pollInterval, token, elapsedTimer);
        }

        Log("Event {0} ended", ev.Name);
    }

    static void PrintHelp()
    {
        Console.WriteLine("Asus LED Controller - help");
        Console.WriteLine("Examples:");
        Console.WriteLine("  asusblink.exe --mic-state 0,1 --mic-interval 200,5000 --mic-duration 60s");
        Console.WriteLine("  asusblink.exe --keyboard-state 128,129,130,131 --keyboard-interval 200,100,50,2000 --keyboard-duration 5s");
        Console.WriteLine("  asusblink.exe --mic-state 1 --keyboard-state 130");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  --mic-state <csv of ints>         : mic states (0/1)");
        Console.WriteLine("  --mic-interval <csv of times>     : intervals in ms/s/m/h/d (wraps)");
        Console.WriteLine("  --mic-duration <time|once>        : total duration (0 = infinite)");
        Console.WriteLine("  --keyboard-state <csv of ints>    : keyboard levels (128..131)");
        Console.WriteLine("  --keyboard-interval <csv>         : intervals (wraps)");
        Console.WriteLine("  --keyboard-duration <time|once>   : duration");
        Console.WriteLine("  --event1-hdd-state <csv>          : map HDD activity levels 0..4 to keyboard states");
        Console.WriteLine("  --event1-mic-state ...            : custom events (event1,event2...)");
        Console.WriteLine("                                      same-device events run serially by priority");
        Console.WriteLine("  --error-log <path|off>            : error log");
        Console.WriteLine("  --error-retry <times>             : how many times to retry operations");
        Console.WriteLine("  --error-action <commalist>        : actions on error: exit,continue,pause,crash,log");
        Console.WriteLine("  --startup | --no-startup         : persist the Startup-folder preference");
        Console.WriteLine("  --tray | --no-tray               : persist tray visibility");
        Console.WriteLine("  --dropdown | --flat-menu         : persist tray menu layout");
        Console.WriteLine("  --set Settings.Key=Value         : persist any known typed option");
        Console.WriteLine("  --configure-only                 : save/validate without opening hardware");
        Console.WriteLine("  --reload | --exit                : control the resident profile instance");
        Console.WriteLine("  --version                        : print the version without side effects");
        Console.WriteLine();
    }
}
// ===== end Program class =====

// testing.cs
// Compile (tray): C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc /out:testing.exe /target:winexe /r:System.Windows.Forms.dll /r:System.Drawing.dll testing.cs
// Compile (console): C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc /out:testing.exe /target:exe testing.cs

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Management;
using System.Drawing;
using System.Windows.Forms;
using System.Timers;
using System.ComponentModel;
using System.Reflection;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Collections.Concurrent;
using System.Globalization;
using System.Diagnostics.PerformanceData;

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

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WaitForSingleObject(IntPtr hHandle, int dwMilliseconds);

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
            throw new Exception("Can't connect to ACPI");
        }
    }

    public void Control(uint dwIoControlCode, byte[] lpInBuffer, byte[] lpOutBuffer)
    {
        uint lpBytesReturned = 0;
        DeviceIoControl(
            handle,
            dwIoControlCode,
            lpInBuffer,
            (uint)lpInBuffer.Length,
            lpOutBuffer,
            (uint)lpOutBuffer.Length,
            ref lpBytesReturned,
            IntPtr.Zero
        );
    }

    protected byte[] CallMethod(uint MethodID, byte[] args)
    {
        byte[] acpiBuf = new byte[8 + args.Length];
        byte[] outBuffer = new byte[16];

        BitConverter.GetBytes((uint)MethodID).CopyTo(acpiBuf, 0);
        BitConverter.GetBytes((uint)args.Length).CopyTo(acpiBuf, 4);
        Array.Copy(args, 0, acpiBuf, 8, args.Length);

        Control(CONTROL_CODE, acpiBuf, outBuffer);

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
        CloseHandle(handle);
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
    static readonly long HDD_LOW = 100 * 1024;
    static readonly long HDD_MID = 1 * 1024 * 1024;
    static readonly long HDD_HIGH = 10 * 1024 * 1024;

    static AsusACPI acpi;
    static TextWriter logWriter = Console.Out;
    static object logLock = new object();

    static volatile bool paused = false;
    static volatile bool exiting = false;

    static int errorRetryTimes = 3;
    static string errorLogPath = null;
    static List<string> errorActions = new List<string>();

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
    static MenuItem miChangeLog = null;
    static MenuItem miShowLog = null;
    static MenuItem miOperationHeader = null;
    static MenuItem miRunningHeader = null;
    static System.Windows.Forms.Timer trayRefreshTimer = null;

    static object trayLock = new object();

    static void Log(string fmt, params object[] args)
    {
        lock (logLock)
        {
            string line = "[" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + "] " + string.Format(fmt, args);
            if (errorLogPath != null && errorLogPath != "off")
            {
                try
                {
                    File.AppendAllText(errorLogPath, line + Environment.NewLine);
                }
                catch { /* ignore logging error */ }
            }
            Console.WriteLine(line);
        }
    }

    static long ParseTimeMs(string s)
    {
        if (string.IsNullOrWhiteSpace(s)) return -1;
        s = s.Trim().ToLowerInvariant();
        try
        {
            if (s.EndsWith("ms")) return (long)double.Parse(s.Substring(0, s.Length - 2), CultureInfo.InvariantCulture);
            if (s.EndsWith("s")) return (long)(double.Parse(s.Substring(0, s.Length - 1), CultureInfo.InvariantCulture) * 1000);
            if (s.EndsWith("m")) return (long)(double.Parse(s.Substring(0, s.Length - 1), CultureInfo.InvariantCulture) * 60 * 1000);
            if (s.EndsWith("h")) return (long)(double.Parse(s.Substring(0, s.Length - 1), CultureInfo.InvariantCulture) * 3600 * 1000);
            if (s.EndsWith("d")) return (long)(double.Parse(s.Substring(0, s.Length - 1), CultureInfo.InvariantCulture) * 24 * 3600 * 1000);
            return (long)double.Parse(s, CultureInfo.InvariantCulture);
        }
        catch
        {
            return -1;
        }
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
                    acpi.SetMicLed(state);
                }
                else if (device == "keyboard")
                {
                    acpi.SetKeyboardState(state);
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
                if (errorActions.Contains("log"))
                {
                    try
                    {
                        if (!string.IsNullOrEmpty(errorLogPath) && errorLogPath != "off")
                        {
                            File.AppendAllText(errorLogPath, string.Format("[{0}] Error applying {1}={2}: {3}\n", DateTime.Now.ToString("o"), device, state, ex.ToString()));
                        }
                    }
                    catch { }
                }
                if (tries > errorRetryTimes)
                {
                    if (errorActions.Contains("pause"))
                    {
                        paused = true;
                        Log("Pausing execution due to error-action pause. Use tray to resume.");
                    }
                    if (errorActions.Contains("exit"))
                    {
                        Log("Exiting due to error-action exit.");
                        Environment.Exit(1);
                    }
                    if (errorActions.Contains("crash"))
                    {
                        throw;
                    }
                    return false;
                }
                Thread.Sleep(500);
            }
        }
    }

    static void SleepWithCancel(long ms, CancellationToken token)
    {
        if (ms <= 0) return;
        int step = 100;
        long waited = 0;
        while (waited < ms && !token.IsCancellationRequested)
        {
            while (paused && !token.IsCancellationRequested) Thread.Sleep(200);
            int toSleep = (int)Math.Min(step, ms - waited);
            Thread.Sleep(toSleep);
            waited += toSleep;
        }
    }

    static Dictionary<string, string> ParseArgs(string[] args)
    {
        var dict = new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase);
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            if (!a.StartsWith("--")) continue;
            string key = a.Substring(2);
            string val = null;
            if (i + 1 < args.Length && !args[i + 1].StartsWith("--"))
            {
                val = args[i + 1];
                i++;
            }
            dict[key] = val;
        }
        return dict;
    }

    static DeviceEvent BuildEventFromOptions(string name, Dictionary<string, string> options)
    {
        DeviceEvent ev = new DeviceEvent();
        ev.Name = name;

        string s = null;
        if (options.TryGetValue(name + "-state", out s) || options.TryGetValue(name + "_state", out s))
        {
            foreach (var part in SplitComma(s))
            {
                int val;
                if (int.TryParse(part, out val)) ev.States.Add(val);
            }
        }

        string iv = null;
        if (options.TryGetValue(name + "-interval", out iv) || options.TryGetValue(name + "_interval", out iv))
        {
            foreach (var part in SplitComma(iv))
            {
                long ms = ParseTimeMs(part);
                if (ms < 0) ms = 0;
                ev.IntervalsMs.Add(ms);
            }
        }

        string du = null;
        if (options.TryGetValue(name + "-duration", out du) || options.TryGetValue(name + "_duration", out du))
        {
            long ms = ParseTimeMs(du);
            if (ms < 0) ev.DurationMs = -1; else ev.DurationMs = ms;
        }

        if (ev.States.Count > 0 && ev.IntervalsMs.Count == 0)
        {
            ev.IntervalsMs.Add(0L);
        }

        return ev;
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
        string startup = Environment.GetFolderPath(Environment.SpecialFolder.Startup);
        string exe = Assembly.GetEntryAssembly().Location;
        string linkPath = Path.Combine(startup, Path.GetFileNameWithoutExtension(exe) + ".lnk");
        return File.Exists(linkPath);
    }

    static void InstallStartupShortcut(bool install)
    {
        try
        {
            string startup = Environment.GetFolderPath(Environment.SpecialFolder.Startup);
            string exe = Assembly.GetEntryAssembly().Location;
            string linkPath = Path.Combine(startup, Path.GetFileNameWithoutExtension(exe) + ".lnk");

            Type t = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(t);
            object shortcut = null;

            if (install)
            {
                shortcut = t.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { linkPath });
                Type scType = shortcut.GetType();
                scType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, new object[] { exe });
                scType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut, new object[] { Path.GetDirectoryName(exe) });
                scType.InvokeMember("Arguments", BindingFlags.SetProperty, null, shortcut, new object[] { "" });
                scType.InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, new object[] { });
                Log("Installed startup shortcut: " + linkPath);
            }
            else
            {
                if (File.Exists(linkPath)) File.Delete(linkPath);
                Log("Removed startup shortcut: " + linkPath);
            }
        }
        catch (Exception ex)
        {
            Log("InstallStartupShortcut error: " + ex.Message);
        }
    }

    // Build and show the tray (minimal creation, detailed items are filled/updated by RefreshTray)
    static void CreateTray()
    {
        try
        {
            lock (trayLock)
            {
                if (tray != null) return;

                tray = new NotifyIcon();
                tray.Icon = SystemIcons.Application;
                tray.Visible = true;

                // top-level context menu root
                var cm = new ContextMenu();

                // Operation header (unclickable)
                miOperationHeader = new MenuItem("Operation:");
                miOperationHeader.Enabled = false;
                cm.MenuItems.Add(miOperationHeader);

                // Pause checkbox item (dynamic text, checked when paused)
                miPause = new MenuItem("Pause");
                miPause.Checked = paused;
                miPause.Click += (s, e) =>
                {
                    paused = !paused;
                    UpdatePauseMenu();
                    Log("Paused = {0}", paused);
                };
                cm.MenuItems.Add(miPause);

                // Run as startup checkbox
                miStartup = new MenuItem("Run as startup");
                miStartup.Checked = IsStartupInstalled();
                miStartup.Click += (s, e) =>
                {
                    bool want = !miStartup.Checked;
                    InstallStartupShortcut(want);
                    miStartup.Checked = want;
                };
                cm.MenuItems.Add(miStartup);

                // Change log path
                miChangeLog = new MenuItem("Change log path");
                miChangeLog.Click += (s, e) =>
                {
                    SaveFileDialog sd = new SaveFileDialog();
                    sd.Title = "Select log file path (or Cancel to disable logging)";
                    sd.Filter = "Log files (*.log)|*.log|All files (*.*)|*.*";
                    sd.FileName = "asus_led_controller.log";
                    DialogResult dr = sd.ShowDialog();
                    if (dr == DialogResult.OK)
                    {
                        errorLogPath = sd.FileName;
                        Log("Log path changed to: {0}", errorLogPath);
                        RefreshTrayItems(); // reflect path in show dialog
                    }
                };
                cm.MenuItems.Add(miChangeLog);

                // Show log file (opens Notepad or shows info)
                miShowLog = new MenuItem("Show log file");
                miShowLog.Click += (s, e) =>
                {
                    if (!string.IsNullOrEmpty(errorLogPath) && errorLogPath != "off" && File.Exists(errorLogPath))
                    {
                        try { Process.Start("notepad.exe", errorLogPath); }
                        catch { MessageBox.Show("Failed to open log file: " + errorLogPath); }
                    }
                    else
                    {
                        string msg = "No log file configured.";
                        if (errorLogPath == "off") msg = "Error logging is disabled (error-log set to 'off').";
                        else if (!string.IsNullOrEmpty(errorLogPath)) msg = "Configured log path: " + errorLogPath + Environment.NewLine + "(file does not exist yet)";
                        MessageBox.Show(msg, "Log file");
                    }
                };
                cm.MenuItems.Add(miShowLog);

                // Exit
                var miExit = new MenuItem("Exit");
                miExit.Click += (s, e) =>
                {
                    exiting = true;
                    try { tray.Visible = false; } catch { }
                    Application.Exit();
                    Environment.Exit(0);
                };
                cm.MenuItems.Add(miExit);

                cm.MenuItems.Add("-"); // separator

                // Running tasks header
                miRunningHeader = new MenuItem("Running Tasks:");
                miRunningHeader.Enabled = false;
                cm.MenuItems.Add(miRunningHeader);

                // A placeholder; real entries will be inserted/updated by RefreshTray
                cm.MenuItems.Add(new MenuItem("Loading...") { Enabled = false });

                tray.ContextMenu = cm;

                // Ensure pause menu reflects initial state
                UpdatePauseMenu();

                // Start a timer to refresh dynamic parts of the menu every second
                trayRefreshTimer = new System.Windows.Forms.Timer();
                trayRefreshTimer.Interval = 1000;
                trayRefreshTimer.Tick += (s, e) => { RefreshTrayItems(); };
                trayRefreshTimer.Start();
            }
        }
        catch (Exception ex)
        {
            Log("Tray create error: " + ex.Message);
        }
    }

    static void UpdatePauseMenu()
    {
        if (miPause == null) return;
        miPause.Checked = paused;
        if (paused) miPause.Text = "Resume";
        else miPause.Text = "Pause";
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
                tray.ContextMenu.MenuItems.RemoveAt(idx + 1);
            }

            // Add a summary operation info (unclickable)
            var opSummary = new MenuItem("Operation:");
            opSummary.Enabled = false;
            tray.ContextMenu.MenuItems.Add(opSummary);

            var miPauseState = new MenuItem(string.Format("Paused: {0}", paused ? "Yes" : "No")) { Enabled = false };
            tray.ContextMenu.MenuItems.Add(miPauseState);

            var miStartupState = new MenuItem(string.Format("Run at startup: {0}", IsStartupInstalled() ? "Yes" : "No")) { Enabled = false };
            tray.ContextMenu.MenuItems.Add(miStartupState);

            var miLogState = new MenuItem("Log path: " + (string.IsNullOrEmpty(errorLogPath) ? "(none)" : errorLogPath)) { Enabled = false };
            tray.ContextMenu.MenuItems.Add(miLogState);

            tray.ContextMenu.MenuItems.Add("-");

            // Add Running Tasks header (again)
            var runningHdr = new MenuItem("Running Tasks:");
            runningHdr.Enabled = false;
            tray.ContextMenu.MenuItems.Add(runningHdr);

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
            }
            tray.ContextMenu.MenuItems.Add(new MenuItem("Active tasks: " + activeCount) { Enabled = false });
        }
    }

    [STAThread]
    static void Main(string[] args)
    {
        if (args.Length == 0)
        {
            PrintHelp();
            return;
        }

        var opts = ParseArgs(args);

        string er;
        if (opts.TryGetValue("error-retry", out er))
        {
            int.TryParse(er, out errorRetryTimes);
            if (errorRetryTimes < 0) errorRetryTimes = 0;
        }
        string el;
        if (opts.TryGetValue("error-log", out el))
        {
            if (!string.IsNullOrEmpty(el))
            {
                errorLogPath = el == "off" ? "off" : el;
            }
        }
        string ea;
        if (opts.TryGetValue("error-action", out ea))
        {
            errorActions = SplitComma(ea).ToList();
        }

        try
        {
            acpi = new AsusACPI();
        }
        catch (Exception ex)
        {
            Log("Failed to open ACPI interface: " + ex.Message);
            return;
        }

        bool noTray = opts.ContainsKey("no-tray");

        // Build events (same as before)
        var micEvent = BuildEventFromOptions("mic", opts);
        var keyboardEvent = BuildEventFromOptions("keyboard", opts);

        List<KeyValuePair<string, DeviceEvent>> toRun = new List<KeyValuePair<string, DeviceEvent>>();
        if (micEvent.States.Count > 0) toRun.Add(new KeyValuePair<string, DeviceEvent>("mic", micEvent));
        if (keyboardEvent.States.Count > 0) toRun.Add(new KeyValuePair<string, DeviceEvent>("keyboard", keyboardEvent));

        var eventNames = new HashSet<string>();
        foreach (var k in opts.Keys)
        {
            if (k.StartsWith("event"))
            {
                var dash = k.IndexOf('-', 5);
                string evname;
                if (dash > 0) evname = k.Substring(0, dash);
                else evname = k;
                eventNames.Add(evname);
            }
        }
        foreach (var evn in eventNames.OrderBy(x => x))
        {
            var subOpts = new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase);
            foreach (var kv in opts)
            {
                if (kv.Key.StartsWith(evn + "-"))
                {
                    string subkey = kv.Key.Substring(evn.Length + 1);
                    subOpts[subkey] = kv.Value;
                }
            }
            if (subOpts.ContainsKey("mic-state"))
            {
                var tdict = new Dictionary<string, string>();
                foreach (var sk in subOpts.Keys) tdict[sk] = subOpts[sk];
                var miccfg = new Dictionary<string, string>();
                foreach (var kk in tdict.Keys)
                    miccfg["mic-" + kk] = tdict[kk];
                var built = BuildEventFromOptions("mic", miccfg);
                built.Name = evn;
                int eidx;
                if (int.TryParse(evn.Substring(5), out eidx)) built.Priority = eidx;
                toRun.Add(new KeyValuePair<string, DeviceEvent>("mic", built));
            }
            if (subOpts.ContainsKey("keyboard-state"))
            {
                var built = BuildEventFromOptions("keyboard", subOpts);
                built.Name = evn;
                int eidx;
                if (int.TryParse(evn.Substring(5), out eidx)) built.Priority = eidx;
                toRun.Add(new KeyValuePair<string, DeviceEvent>("keyboard", built));
            }
            if (subOpts.ContainsKey("hdd-state"))
            {
                var built = BuildEventFromOptions("hdd", subOpts);
                built.Name = evn;
                int eidx;
                if (int.TryParse(evn.Substring(5), out eidx)) built.Priority = eidx;
                toRun.Add(new KeyValuePair<string, DeviceEvent>("hdd", built));
            }
        }

        if (!toRun.Any())
        {
            PrintHelp();
            return;
        }

        // Create tray early so user sees it — the dynamic contents will populate shortly
        if (!noTray)
        {
            Application.EnableVisualStyles();
            CreateTray();
            RefreshTrayItems();
        }

        // Start events and gather tasks so we can wait for one-shot events
        lock (runningLock)
        {
            configuredEvents.Clear();
            runningTasks.Clear();
            runningCts.Clear();
            foreach (var kv in toRun)
            {
                configuredEvents.Add(kv);
            }

            var grouped = toRun.GroupBy(k => k.Key);
            foreach (var g in grouped)
            {
                string device = g.Key;
                var chosenEvents = g.Select(x => x.Value).OrderBy(e => e.Priority).ToList();

                foreach (var ev in chosenEvents)
                {
                    var cts = new CancellationTokenSource();
                    runningCts.Add(cts);
                    Task t = Task.Factory.StartNew(
                        (obj) => RunEventLoop(device, (DeviceEvent)obj, cts.Token),
                        ev,
                        cts.Token,
                        TaskCreationOptions.LongRunning,
                        TaskScheduler.Default
                    );
                    runningTasks.Add(t);
                }
            }
        }

        RefreshTrayItems(); // immediately populate details

        if (!noTray)
        {
            Log("Tray mode active — starting message loop.");
            Application.Run(); // keeps tray responsive; Exit via tray menu
        }
        else
        {
            bool allOneShot = toRun.All(kv => kv.Value.DurationMs == -1);
            if (allOneShot)
            {
                Log("Running in console mode and all events are one-shot: waiting for tasks to complete...");
                try
                {
                    Task.WaitAll(runningTasks.ToArray());
                }
                catch (AggregateException ae)
                {
                    foreach (var ex in ae.InnerExceptions) Log("Task exception: {0}", ex.Message);
                }
                Log("All one-shot events finished — exiting.");
            }
            else
            {
                Log("Console mode, long-running events present. Press Ctrl-C to exit.");
                while (!exiting)
                {
                    Thread.Sleep(500);
                }
            }
        }
    }

    static void RunEventLoop(string device, DeviceEvent ev, CancellationToken token)
    {
        Log("Starting event {0} for device {1} (priority {2})", ev.Name, device, ev.Priority);

        int sCount = ev.States.Count;
        if (sCount == 0) { Log("Event {0} has no states, exiting", ev.Name); return; }

        bool infinite = ev.DurationMs == 0;
        long durationMs = ev.DurationMs;

        if (durationMs == -1 && !infinite)
        {
            for (int idx = 0; idx < sCount && !token.IsCancellationRequested; idx++)
            {
                while (paused && !token.IsCancellationRequested) Thread.Sleep(200);
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
        long loopStart = Environment.TickCount;
        while (!token.IsCancellationRequested)
        {
            while (paused && !token.IsCancellationRequested) Thread.Sleep(200);
            int state = ev.States[pos % sCount];
            long interval = ev.IntervalsMs.Count > 0 ? ev.IntervalsMs[pos % ev.IntervalsMs.Count] : 0;

            ApplyDeviceState(device, state);

            if (interval == 0 && ev.DurationMs == 0)
            {
                while (!token.IsCancellationRequested)
                {
                    while (paused && !token.IsCancellationRequested) Thread.Sleep(200);
                    ApplyDeviceState(device, state);
                    Thread.Sleep(200);
                }
            }

            if (interval > 0)
            {
                SleepWithCancel(interval, token);
            }

            if (!infinite && durationMs > 0)
            {
                long elapsed = Environment.TickCount - loopStart;
                if (elapsed >= durationMs) break;
            }

            pos++;
        }

        Log("Event {0} ended", ev.Name);
    }

    static void PrintHelp()
    {
        Console.WriteLine("Asus LED Controller - help");
        Console.WriteLine("Examples:");
        Console.WriteLine("  testing.exe --mic-state 0,1 --mic-interval 200,5000 --mic-duration 60s");
        Console.WriteLine("  testing.exe --keyboard-state 128,129,130,131 --keyboard-interval 200,100,50,2000 --keyboard-duration 5s");
        Console.WriteLine("  testing.exe --mic-state 1 --keyboard-state 130");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  --mic-state <csv of ints>         : mic states (0/1)");
        Console.WriteLine("  --mic-interval <csv of times>     : intervals in ms/s/m/h/d (wraps)");
        Console.WriteLine("  --mic-duration <time>             : total duration (0 = infinite)");
        Console.WriteLine("  --mic-check                       : check current state and apply if different");
        Console.WriteLine("  --keyboard-state <csv of ints>    : keyboard levels (128..131)");
        Console.WriteLine("  --keyboard-interval <csv>         : intervals (wraps)");
        Console.WriteLine("  --keyboard-duration <time>        : duration");
        Console.WriteLine("  --event1-mic-state ...            : custom events (event1,event2...)");
        Console.WriteLine("  --error-log <path|off>            : error log");
        Console.WriteLine("  --error-retry <times>             : how many times to retry operations");
        Console.WriteLine("  --error-action <commalist>        : actions on error: exit,continue,pause,crash,log");
        Console.WriteLine("  --no-tray                         : don't create tray icon");
        Console.WriteLine();
    }
}
// ===== end Program class =====

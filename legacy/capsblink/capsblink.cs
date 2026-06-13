using System.Runtime.InteropServices;
using System.ComponentModel;
using System;
using System.IO;
using System.Reflection;
using System.Threading;

class CapsLockLight
{
    const string DefaultKeyboardTargetPath = "\\Device\\KeyboardClass0";
    static volatile bool exiting = false;
    static readonly ManualResetEvent exitEvent = new ManualResetEvent(false);
    static readonly string executablePath = Assembly.GetEntryAssembly().Location;
    static readonly string executableDirectory = GetExecutableDirectory();
    static readonly string executableBaseName = GetExecutableBaseName();
    static readonly string iniPath = Path.Combine(executableDirectory, executableBaseName + ".ini");
    static readonly string logPath = Path.Combine(executableDirectory, executableBaseName + ".log");

    [DllImport(
        "kernel32.dll",
        EntryPoint = "DefineDosDeviceW",
        CharSet = CharSet.Unicode,
        ExactSpelling = true,
        SetLastError = true)]
    public static extern Boolean DefineDosDevice(UInt32 flags, String deviceName, String targetPath);

    [DllImport(
        "kernel32.dll",
        EntryPoint = "CreateFileW",
        CharSet = CharSet.Unicode,
        ExactSpelling = true,
        SetLastError = true)]
    public static extern IntPtr CreateFile(String fileName,
                       UInt32 desiredAccess, UInt32 shareMode, IntPtr securityAttributes,
                       UInt32 creationDisposition, UInt32 flagsAndAttributes, IntPtr templateFile
                      );

    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBOARD_INDICATOR_PARAMETERS
    {
        public UInt16 unitID;
        public UInt16 LEDflags;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern Boolean DeviceIoControl(IntPtr device, UInt32 ioControlCode,
                          ref KEYBOARD_INDICATOR_PARAMETERS KIPin, UInt32 inBufferSize,
                          ref KEYBOARD_INDICATOR_PARAMETERS KIPout, UInt32 outBufferSize,
                          ref UInt32 bytesReturned, IntPtr overlapped
                         );
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern Boolean DeviceIoControl(IntPtr device, UInt32 ioControlCode,
                          IntPtr KIPin, UInt32 inBufferSize,
                          ref KEYBOARD_INDICATOR_PARAMETERS KIPout, UInt32 outBufferSize,
                          ref UInt32 bytesReturned, IntPtr overlapped
                         );
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern Boolean DeviceIoControl(IntPtr device, UInt32 ioControlCode,
                          ref KEYBOARD_INDICATOR_PARAMETERS KIPin, UInt32 inBufferSize,
                          IntPtr KIPout, UInt32 outBufferSize,
                          ref UInt32 bytesReturned, IntPtr overlapped
                         );

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern Boolean CloseHandle(IntPtr handle);

    [DllImport("user32.dll", CharSet = CharSet.Auto, ExactSpelling = true)]
    public static extern short GetKeyState(int keyCode);

    const int VK_CAPITAL = 0x14;

    static int Main(string[] args)
    {
        if (args.Length != 0)
        {
            if (args.Length == 1 && IsHelpOption(args[0]))
            {
                Usage();
                return 0;
            }
            Console.Error.WriteLine("ERROR: Unknown argument: " + args[0]);
            Usage();
            return 2;
        }

        UInt32 bytesReturned = 0;
        IntPtr device = Flags.INVALID_HANDLE_VALUE;
        bool mappingDefined = false;
        Mutex instanceMutex = null;
        bool ownsInstanceMutex = false;
        string deviceName = "capsblinkKBD_" + System.Diagnostics.Process.GetCurrentProcess().Id.ToString();
        string keyboardTargetPath = DefaultKeyboardTargetPath;
        int blinkIntervalMs = 500;
        KEYBOARD_INDICATOR_PARAMETERS KIPbuf = new KEYBOARD_INDICATOR_PARAMETERS { unitID = 0, LEDflags = 0 };
        int exitCode = 0;

        try
        {
            EnsureIniFile();
            keyboardTargetPath = ReadIniString("Settings", "KeyboardTargetPath", DefaultKeyboardTargetPath);
            blinkIntervalMs = ReadIniInt("Settings", "BlinkIntervalMs", 500);
            if (String.IsNullOrWhiteSpace(keyboardTargetPath) ||
                keyboardTargetPath.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0 ||
                !keyboardTargetPath.StartsWith("\\Device\\KeyboardClass", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException(
                    "KeyboardTargetPath must name a keyboard class device such as \\Device\\KeyboardClass0.");
            if (blinkIntervalMs < 50 || blinkIntervalMs > 86400000)
                throw new InvalidDataException("BlinkIntervalMs must be from 50 through 86400000.");

            instanceMutex = new Mutex(false, @"Local\capsblink-" + StableHash(keyboardTargetPath));
            try
            {
                ownsInstanceMutex = instanceMutex.WaitOne(0);
            }
            catch (AbandonedMutexException)
            {
                ownsInstanceMutex = true;
            }
            if (!ownsInstanceMutex)
                throw new InvalidOperationException("Another capsblink instance is already controlling " + keyboardTargetPath + ".");

            Log("Starting with keyboard target " + keyboardTargetPath);
            if (!DefineDosDevice(Flags.DDD_RAW_TARGET_PATH, deviceName, keyboardTargetPath))
            {
                Int32 err = Marshal.GetLastWin32Error();
                throw new Win32Exception(err);
            }
            mappingDefined = true;

            device = CreateFile("\\\\.\\" + deviceName, Flags.GENERIC_WRITE, 0, IntPtr.Zero, Flags.OPEN_EXISTING, 0, IntPtr.Zero);
            if (device == Flags.INVALID_HANDLE_VALUE)
            {
                Int32 err = Marshal.GetLastWin32Error();
                throw new Win32Exception(err);
            }

            Console.CancelKeyPress += (sender, e) =>
            {
                e.Cancel = true;
                exiting = true;
                exitEvent.Set();
            };

            while (!exiting)
            {
                bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;

                if (!capsLockOn)
                {
                    if (!DeviceIoControl(device, Flags.IOCTL_KEYBOARD_QUERY_INDICATORS, IntPtr.Zero, 0, ref KIPbuf, (UInt32)Marshal.SizeOf(KIPbuf), ref bytesReturned, IntPtr.Zero))
                    {
                        Int32 err = Marshal.GetLastWin32Error();
                        throw new Win32Exception(err);
                    }

                    KIPbuf.LEDflags = (UInt16)(KIPbuf.LEDflags ^ Flags.KEYBOARD_CAPS_LOCK_ON);

                    if (!DeviceIoControl(device, Flags.IOCTL_KEYBOARD_SET_INDICATORS, ref KIPbuf, (UInt32)Marshal.SizeOf(KIPbuf), IntPtr.Zero, 0, ref bytesReturned, IntPtr.Zero))
                    {
                        Int32 err = Marshal.GetLastWin32Error();
                        throw new Win32Exception(err);
                    }
                }

                if (exitEvent.WaitOne(blinkIntervalMs))
                    break;
            }
        }
        catch (Exception ex)
        {
            Log("Error: " + ex);
            Console.Error.WriteLine("ERROR: " + ex.Message);
            exitCode = 1;
        }
        finally
        {
            if (device != Flags.INVALID_HANDLE_VALUE)
            {
                try
                {
                    SynchronizeCapsIndicator(device, ref KIPbuf, ref bytesReturned);
                }
                catch (Exception ex)
                {
                    Log("Could not restore Caps Lock indicator state: " + ex.Message);
                    exitCode = 1;
                }
                if (!CloseHandle(device))
                {
                    Log("CloseHandle failed: " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
                    exitCode = 1;
                }
            }
            if (mappingDefined)
            {
                if (!DefineDosDevice(
                    Flags.DDD_REMOVE_DEFINITION |
                    Flags.DDD_EXACT_MATCH_ON_REMOVE |
                    Flags.DDD_RAW_TARGET_PATH,
                    deviceName,
                    keyboardTargetPath))
                {
                    Log("DefineDosDevice cleanup failed: " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
                    exitCode = 1;
                }
            }
            if (ownsInstanceMutex)
            {
                try
                {
                    instanceMutex.ReleaseMutex();
                }
                catch (ApplicationException)
                {
                }
            }
            if (instanceMutex != null)
            {
                instanceMutex.Dispose();
            }
            Log("Stopped");
        }
        return exitCode;
    }

    static void SynchronizeCapsIndicator(
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
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        if (capsLockOn)
            indicators.LEDflags = (UInt16)(indicators.LEDflags | Flags.KEYBOARD_CAPS_LOCK_ON);
        else
            indicators.LEDflags = (UInt16)(indicators.LEDflags & ~Flags.KEYBOARD_CAPS_LOCK_ON);

        if (!DeviceIoControl(
            device,
            Flags.IOCTL_KEYBOARD_SET_INDICATORS,
            ref indicators,
            (UInt32)Marshal.SizeOf(indicators),
            IntPtr.Zero,
            0,
            ref bytesReturned,
            IntPtr.Zero))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
    }

    static string StableHash(string value)
    {
        UInt32 hash = 2166136261;
        foreach (char ch in (value ?? "").ToUpperInvariant())
        {
            hash ^= ch;
            hash *= 16777619;
        }
        return hash.ToString("X8");
    }

    static string GetExecutableDirectory()
    {
        string dir = Path.GetDirectoryName(executablePath);
        return string.IsNullOrEmpty(dir) ? AppDomain.CurrentDomain.BaseDirectory : dir;
    }

    static string GetExecutableBaseName()
    {
        string name = Path.GetFileNameWithoutExtension(executablePath);
        return string.IsNullOrEmpty(name) ? "capsblink" : name;
    }

    static void EnsureIniFile()
    {
        if (File.Exists(iniPath))
        {
            return;
        }

        Directory.CreateDirectory(executableDirectory);
        string temporaryPath = iniPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporaryPath,
                "[Settings]" + Environment.NewLine +
                "KeyboardTargetPath=" + DefaultKeyboardTargetPath + Environment.NewLine +
                "BlinkIntervalMs=500" + Environment.NewLine);
            try
            {
                File.Move(temporaryPath, iniPath);
            }
            catch (IOException)
            {
                if (!File.Exists(iniPath))
                    throw;
            }
        }
        finally
        {
            if (File.Exists(temporaryPath))
                File.Delete(temporaryPath);
        }
    }

    static string ReadIniString(string section, string key, string fallback)
    {
        try
        {
            string currentSection = "";
            foreach (string rawLine in File.ReadAllLines(iniPath))
            {
                string line = rawLine.Trim();
                if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#"))
                {
                    continue;
                }
                if (line.StartsWith("[") && line.EndsWith("]"))
                {
                    currentSection = line.Substring(1, line.Length - 2).Trim();
                    continue;
                }
                if (!currentSection.Equals(section, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                int equals = line.IndexOf('=');
                if (equals <= 0)
                {
                    continue;
                }
                if (line.Substring(0, equals).Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
                {
                    string value = line.Substring(equals + 1).Trim();
                    return value.Length == 0 ? fallback : value;
                }
            }
        }
        catch (IOException)
        {
            throw;
        }
        catch (UnauthorizedAccessException)
        {
            throw;
        }
        return fallback;
    }

    static int ReadIniInt(string section, string key, int fallback)
    {
        string raw = ReadIniString(section, key, "");
        if (raw.Length == 0)
            return fallback;
        int value;
        if (!int.TryParse(raw, out value))
            throw new InvalidDataException(key + " must be an integer.");
        return value;
    }

    static bool IsHelpOption(string value)
    {
        return String.Equals(value, "--help", StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "-h", StringComparison.OrdinalIgnoreCase) ||
            String.Equals(value, "/?", StringComparison.OrdinalIgnoreCase);
    }

    static void Usage()
    {
        Console.WriteLine("Usage: capsblink.exe");
        Console.WriteLine("Settings are read from " + iniPath);
    }

    static void Log(string message)
    {
        try
        {
            Directory.CreateDirectory(executableDirectory);
            File.AppendAllText(logPath, DateTime.UtcNow.ToString("o") + "  " + message + Environment.NewLine);
        }
        catch
        {
        }
    }
}

class Flags
{
    public static IntPtr INVALID_HANDLE_VALUE = (IntPtr)(-1);
    public const UInt32 IOCTL_KEYBOARD_SET_INDICATORS = (0x0000000b << 16) | (0 << 14) | (0x0002 << 2) | 0; // from ntddkbd.h, ntddk.h
    public const UInt32 IOCTL_KEYBOARD_QUERY_INDICATORS = (0x0000000b << 16) | (0 << 14) | (0x0010 << 2) | 0; // from ntddkbd.h, ntddk.h

    public const UInt32 DDD_RAW_TARGET_PATH = 0x00000001;
    public const UInt32 DDD_REMOVE_DEFINITION = 0x00000002;
    public const UInt32 DDD_EXACT_MATCH_ON_REMOVE = 0x00000004;
    public const UInt32 DDD_NO_BROADCAST_SYSTEM = 0x00000008;

    public const UInt32 GENERIC_ALL = 0x10000000;
    public const UInt32 GENERIC_EXECUTE = 0x20000000;
    public const UInt32 GENERIC_WRITE = 0x40000000;
    public const UInt32 GENERIC_READ = 0x80000000;

    public const UInt32 CREATE_NEW = 1;
    public const UInt32 CREATE_ALWAYS = 2;
    public const UInt32 OPEN_EXISTING = 3;
    public const UInt32 OPEN_ALWAYS = 4;
    public const UInt32 TRUNCATE_EXISTING = 5;

    public const UInt16 KEYBOARD_SCROLL_LOCK_ON = 1;
    public const UInt16 KEYBOARD_NUM_LOCK_ON = 2;
    public const UInt16 KEYBOARD_CAPS_LOCK_ON = 4;
    public const UInt16 KEYBOARD_SHADOW = 0x4000;
    public const UInt16 KEYBOARD_LED_INJECTED = 0x8000;
}

using System.Runtime.InteropServices;
using System.ComponentModel;
using System;
using System.Threading;

class CapsLockLight
{
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern Boolean DefineDosDevice(UInt32 flags, String deviceName, String targetPath);

    [DllImport("kernel32.dll", SetLastError = true)]
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

    static void Main(string[] args)
    {
        UInt32 bytesReturned = 0;
        IntPtr device;
        KEYBOARD_INDICATOR_PARAMETERS KIPbuf = new KEYBOARD_INDICATOR_PARAMETERS { unitID = 0, LEDflags = 0 };

        if (!DefineDosDevice(Flags.DDD_RAW_TARGET_PATH, "myKBD", "\\Device\\KeyboardClass0"))
        {
            Int32 err = Marshal.GetLastWin32Error();
            throw new Win32Exception(err);
        }

        device = CreateFile("\\\\.\\myKBD", Flags.GENERIC_WRITE, 0, IntPtr.Zero, Flags.OPEN_EXISTING, 0, IntPtr.Zero);
        if (device == Flags.INVALID_HANDLE_VALUE)
        {
            Int32 err = Marshal.GetLastWin32Error();
            throw new Win32Exception(err);
        }

        Console.CancelKeyPress += (sender, e) =>
        {
            e.Cancel = true; // Cancel the termination to perform cleanup

            if (!CloseHandle(device))
            {
                Int32 err = Marshal.GetLastWin32Error();
                throw new Win32Exception(err);
            }

            if (!DefineDosDevice(Flags.DDD_REMOVE_DEFINITION, "myKBD", null))
            {
                Int32 err = Marshal.GetLastWin32Error();
                throw new Win32Exception(err);
            }

            Environment.Exit(0); // Exit the program
        };

        while (true)
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

            Thread.Sleep(500); // Adjust the blink interval as needed
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

using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.ServiceProcess;
using System.Threading;

public class AllowContentService : ServiceBase
{
    private const string BasePath = @"Software\Microsoft\Windows\CurrentVersion\Notifications\Settings";
    private List<Thread> watcherThreads = new List<Thread>();
    private volatile bool running = true;

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern int RegNotifyChangeKeyValue(
        IntPtr hKey,
        bool bWatchSubtree,
        RegChangeNotifyFilter dwNotifyFilter,
        IntPtr hEvent,
        bool fAsynchronous
    );

    [Flags]
    enum RegChangeNotifyFilter
    {
        Name = 1,
        Attributes = 2,
        LastSet = 4,
        Security = 8,
    }

    public AllowContentService()
    {
        ServiceName = "AllowContentAboveLockService";
    }

    protected override void OnStart(string[] args)
    {
        Log("Service started");

        // Watch HKU for new users
        Thread hkuWatcher = new Thread(WatchHKU);
        hkuWatcher.Start();
        watcherThreads.Add(hkuWatcher);

        // Attach to already loaded users
        AttachToExistingUsers();
    }

    protected override void OnStop()
    {
        running = false;
        Log("Service stopping...");
        // Trigger exit by forcing threads to continue
        Environment.Exit(0);
    }

    private void WatchHKU()
    {
        using (RegistryKey hku = Registry.Users)
        {
            while (running)
            {
                IntPtr handle = hku.Handle.DangerousGetHandle();

                RegNotifyChangeKeyValue(
                    handle,
                    false,
                    RegChangeNotifyFilter.Name,
                    IntPtr.Zero,
                    false
                );

                Log("HKU changed (user login/logout?)");

                AttachToExistingUsers();
            }
        }
    }

    private void AttachToExistingUsers()
    {
        foreach (string sid in Registry.Users.GetSubKeyNames())
        {
            if (!sid.StartsWith("S-1-5-21-")) continue;

            string fullPath = sid + "\\" + BasePath;

            try
            {
                RegistryKey key = Registry.Users.OpenSubKey(fullPath, true);
                if (key == null) continue;

                Thread t = new Thread(() => WatchUserKey(key, sid));
                t.Start();
                watcherThreads.Add(t);

                ProcessAllKeys(key, sid);

                Log("Attached to " + sid);
            }
            catch { }
        }
    }

    private void WatchUserKey(RegistryKey baseKey, string sid)
    {
        while (running)
        {
            IntPtr handle = baseKey.Handle.DangerousGetHandle();

            RegNotifyChangeKeyValue(
                handle,
                false,
                RegChangeNotifyFilter.Name,
                IntPtr.Zero,
                false
            );

            Log("Change detected for " + sid);

            ProcessAllKeys(baseKey, sid);
        }
    }

    private void ProcessAllKeys(RegistryKey baseKey, string sid)
    {
        foreach (string name in baseKey.GetSubKeyNames())
        {
            try
            {
                using (RegistryKey subKey = baseKey.OpenSubKey(name, true))
                {
                    if (subKey == null) continue;

                    object val = subKey.GetValue("AllowContentAboveLock");

                    if (val == null || (int)val != 1)
                    {
                        subKey.SetValue("AllowContentAboveLock", 1, RegistryValueKind.DWord);
                        Log("Updated: " + sid + "\\" + name);
                    }
                }
            }
            catch (Exception ex)
            {
                Log("Error: " + ex.Message);
            }
        }
    }

    private void Log(string message)
    {
        string source = "AllowContentAboveLockService";

        if (!EventLog.SourceExists(source))
        {
            EventLog.CreateEventSource(source, "Application");
        }

        EventLog.WriteEntry(source, message);
    }

    public static void Main()
    {
        ServiceBase.Run(new AllowContentService());
    }
}

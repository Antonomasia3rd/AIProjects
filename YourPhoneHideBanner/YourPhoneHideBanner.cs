using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using Microsoft.Win32.SafeHandles;
using System.Runtime.InteropServices;
using System.ServiceProcess;
using System.Threading;

public class AllowContentService : ServiceBase
{
    private const string BasePath = @"Software\Microsoft\Windows\CurrentVersion\Notifications\Settings";
    private readonly object watcherLock = new object();
    private readonly List<Thread> watcherThreads = new List<Thread>();
    private readonly HashSet<string> watchedSids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    private readonly ManualResetEvent stopEvent = new ManualResetEvent(false);
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
        ServiceName = "YourPhoneHideBannerService";
    }

    protected override void OnStart(string[] args)
    {
        running = true;
        stopEvent.Reset();
        Log("Service started");

        Thread hkuWatcher = new Thread(WatchHKU);
        hkuWatcher.IsBackground = true;
        hkuWatcher.Start();
        lock (watcherLock)
        {
            watcherThreads.Add(hkuWatcher);
        }

        AttachToExistingUsers();
    }

    protected override void OnStop()
    {
        running = false;
        stopEvent.Set();
        Log("Service stopping...");

        Thread[] threads;
        lock (watcherLock)
        {
            threads = watcherThreads.ToArray();
        }

        foreach (Thread thread in threads)
        {
            if (thread != Thread.CurrentThread && thread.IsAlive)
            {
                thread.Join(2000);
            }
        }
    }

    private void WatchHKU()
    {
        using (RegistryKey hku = Registry.Users)
        {
            while (running)
            {
                using (ManualResetEvent changed = new ManualResetEvent(false))
                {
                    int status = RegNotifyChangeKeyValue(
                        hku.Handle.DangerousGetHandle(),
                        false,
                        RegChangeNotifyFilter.Name,
                        changed.SafeWaitHandle.DangerousGetHandle(),
                        true
                    );

                    if (status != 0)
                    {
                        Log("RegNotifyChangeKeyValue(HKU) failed: " + status);
                        stopEvent.WaitOne(5000);
                        continue;
                    }

                    int signaled = WaitHandle.WaitAny(new WaitHandle[] { stopEvent, changed });
                    if (signaled == 0 || !running)
                    {
                        break;
                    }
                }

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

            RegistryKey key = null;
            bool registered = false;
            try
            {
                key = Registry.Users.OpenSubKey(fullPath, true);
                if (key == null) continue;

                lock (watcherLock)
                {
                    if (watchedSids.Contains(sid))
                    {
                        key.Dispose();
                        continue;
                    }

                    watchedSids.Add(sid);
                    registered = true;
                }

                ProcessAllKeys(key, sid);

                Thread t = new Thread(() => WatchUserKey(key, sid));
                t.IsBackground = true;
                t.Start();
                lock (watcherLock)
                {
                    watcherThreads.Add(t);
                }

                Log("Attached to " + sid);
            }
            catch (Exception ex)
            {
                if (key != null)
                {
                    key.Dispose();
                }
                if (registered)
                {
                    lock (watcherLock)
                    {
                        watchedSids.Remove(sid);
                    }
                }
                Log("Attach error for " + sid + ": " + ex.Message);
            }
        }
    }

    private void WatchUserKey(RegistryKey baseKey, string sid)
    {
        try
        {
            while (running)
            {
                using (ManualResetEvent changed = new ManualResetEvent(false))
                {
                    int status = RegNotifyChangeKeyValue(
                        baseKey.Handle.DangerousGetHandle(),
                        true,
                        RegChangeNotifyFilter.Name | RegChangeNotifyFilter.LastSet,
                        changed.SafeWaitHandle.DangerousGetHandle(),
                        true
                    );

                    if (status != 0)
                    {
                        Log("RegNotifyChangeKeyValue failed for " + sid + ": " + status);
                        break;
                    }

                    int signaled = WaitHandle.WaitAny(new WaitHandle[] { stopEvent, changed });
                    if (signaled == 0 || !running)
                    {
                        break;
                    }
                }

                Log("Change detected for " + sid);
                ProcessAllKeys(baseKey, sid);
            }
        }
        finally
        {
            baseKey.Dispose();
            lock (watcherLock)
            {
                watchedSids.Remove(sid);
            }
        }
    }

    private void ProcessAllKeys(RegistryKey baseKey, string sid)
    {
        foreach (string name in baseKey.GetSubKeyNames())
        {
            if (!name.StartsWith("Microsoft.YourPhone_8wekyb3d8bbwe!YourPhoneNotifications_"))
            {
                continue;
            }

            ProcessOneKey(baseKey, sid, name);
        }
    }

    private void ProcessOneKey(RegistryKey baseKey, string sid, string name)
    {
        try
        {
            using (RegistryKey subKey = baseKey.OpenSubKey(name, true))
            {
                if (subKey == null) return;

                object bannerObj = subKey.GetValue("ShowBanner");
                int bannerVal = bannerObj != null ? Convert.ToInt32(bannerObj) : -1;

                if (bannerVal != 0)
                {
                    subKey.SetValue("ShowBanner", 0, RegistryValueKind.DWord);
                    Log("Updated ShowBanner: " + sid + "\\" + name);
                }

                object soundObj = subKey.GetValue("SoundFile");
                string soundVal = soundObj as string;

                if (soundVal == null || soundVal.Length != 0)
                {
                    subKey.SetValue("SoundFile", "", RegistryValueKind.String);
                    Log("Updated SoundFile: " + sid + "\\" + name);
                }
            }
        }
        catch (Exception ex)
        {
            Log("Error: " + ex.Message);
        }
    }

    private void Log(string message)
    {
        string source = "YourPhoneHideBannerService";

        try
        {
            if (!EventLog.SourceExists(source))
            {
                EventLog.CreateEventSource(source, "Application");
            }

            EventLog.WriteEntry(source, message);
        }
        catch
        {
        }
    }

    public static void Main()
    {
        ServiceBase.Run(new AllowContentService());
    }
}

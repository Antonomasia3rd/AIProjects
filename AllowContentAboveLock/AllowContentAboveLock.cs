using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
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
    private readonly HashSet<string> watchedRootSids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
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
        ServiceName = "AllowContentAboveLockService";
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

            TryAttachUser(sid);
        }
    }

    private bool TryAttachUser(string sid)
    {
        string fullPath = sid + "\\" + BasePath;

        RegistryKey key = null;
        bool registered = false;
        try
        {
            key = Registry.Users.OpenSubKey(fullPath, true);
            if (key == null)
            {
                EnsureRootWatcher(sid);
                return false;
            }

            lock (watcherLock)
            {
                if (watchedSids.Contains(sid))
                {
                    key.Dispose();
                    return true;
                }

                watchedSids.Add(sid);
                watchedRootSids.Remove(sid);
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
            return true;
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
            return false;
        }
    }

    private void EnsureRootWatcher(string sid)
    {
        lock (watcherLock)
        {
            if (watchedSids.Contains(sid) || watchedRootSids.Contains(sid))
            {
                return;
            }

            watchedRootSids.Add(sid);
        }

        RegistryKey rootKey = null;
        try
        {
            rootKey = Registry.Users.OpenSubKey(sid, false);
            if (rootKey == null)
            {
                lock (watcherLock)
                {
                    watchedRootSids.Remove(sid);
                }
                return;
            }

            Thread t = new Thread(() => WatchUserRoot(rootKey, sid));
            t.IsBackground = true;
            t.Start();
            lock (watcherLock)
            {
                watcherThreads.Add(t);
            }

            Log("Watching user root for notification settings: " + sid);
        }
        catch (Exception ex)
        {
            if (rootKey != null)
            {
                rootKey.Dispose();
            }
            lock (watcherLock)
            {
                watchedRootSids.Remove(sid);
            }
            Log("Root watch error for " + sid + ": " + ex.Message);
        }
    }

    private void WatchUserRoot(RegistryKey rootKey, string sid)
    {
        try
        {
            while (running)
            {
                using (RegistryKey existing = Registry.Users.OpenSubKey(sid + "\\" + BasePath, true))
                {
                    if (existing != null)
                    {
                        if (TryAttachUser(sid))
                        {
                            break;
                        }
                    }
                }

                using (ManualResetEvent changed = new ManualResetEvent(false))
                {
                    int status = RegNotifyChangeKeyValue(
                        rootKey.Handle.DangerousGetHandle(),
                        true,
                        RegChangeNotifyFilter.Name,
                        changed.SafeWaitHandle.DangerousGetHandle(),
                        true
                    );

                    if (status != 0)
                    {
                        Log("RegNotifyChangeKeyValue(root) failed for " + sid + ": " + status);
                        stopEvent.WaitOne(5000);
                        continue;
                    }

                    int signaled = WaitHandle.WaitAny(new WaitHandle[] { stopEvent, changed });
                    if (signaled == 0 || !running)
                    {
                        break;
                    }
                }
            }
        }
        finally
        {
            rootKey.Dispose();
            lock (watcherLock)
            {
                watchedRootSids.Remove(sid);
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

                object val = subKey.GetValue("AllowContentAboveLock");
                int current = val == null ? 0 : Convert.ToInt32(val);

                if (current != 1)
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

    private void Log(string message)
    {
        string source = "AllowContentAboveLockService";

        try
        {
            if (!EventLog.SourceExists(source))
            {
                EventLog.CreateEventSource(source, "Application");
            }

            EventLog.WriteEntry(source, message);
        }
        catch (Exception ex)
        {
            FallbackLog(source, message + " (EventLog write failed: " + ex.Message + ")");
        }
    }

    private void FallbackLog(string source, string message)
    {
        try
        {
            string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "AIProjects");
            Directory.CreateDirectory(dir);
            string path = Path.Combine(dir, source + ".log");
            File.AppendAllText(path, DateTime.UtcNow.ToString("o") + "  " + message + Environment.NewLine);
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

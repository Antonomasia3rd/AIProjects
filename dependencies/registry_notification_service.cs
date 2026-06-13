using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.ServiceProcess;
using System.Threading;

public abstract class RegistryNotificationServiceBase : ServiceBase
{
    private const string NotificationSettingsPath =
        @"Software\Microsoft\Windows\CurrentVersion\Notifications\Settings";

    private readonly object watcherLock = new object();
    private readonly object logLock = new object();
    private readonly List<Thread> watcherThreads = new List<Thread>();
    private readonly HashSet<string> watchedSids =
        new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> watchedRootSids =
        new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    private readonly ManualResetEvent stopEvent = new ManualResetEvent(false);
    private readonly string settingsBaseName;
    private volatile bool running;

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern int RegNotifyChangeKeyValue(
        IntPtr hKey,
        bool watchSubtree,
        RegChangeNotifyFilter notifyFilter,
        IntPtr eventHandle,
        bool asynchronous);

    [Flags]
    private enum RegChangeNotifyFilter
    {
        Name = 1,
        Attributes = 2,
        LastSet = 4,
        Security = 8,
    }

    protected RegistryNotificationServiceBase(
        string serviceName,
        string settingsBaseName)
    {
        if (String.IsNullOrWhiteSpace(serviceName))
            throw new ArgumentException("A service name is required.", "serviceName");

        ServiceName = serviceName;
        this.settingsBaseName = String.IsNullOrWhiteSpace(settingsBaseName)
            ? serviceName
            : settingsBaseName;
    }

    protected abstract void ProcessAllKeys(RegistryKey baseKey, string sid);

    protected override void OnStart(string[] args)
    {
        lock (watcherLock)
        {
            if (watcherThreads.Exists(thread => thread.IsAlive))
                throw new InvalidOperationException(
                    "Previous registry watcher threads are still stopping.");

            watcherThreads.Clear();
            watchedSids.Clear();
            watchedRootSids.Clear();
        }

        stopEvent.Reset();
        running = true;
        EnsureIniFile();
        Log("Service started");

        try
        {
            if (!StartWatcher(WatchUsersRoot))
                throw new InvalidOperationException(
                    "The HKEY_USERS watcher could not be started.");

            AttachToExistingUsers();
        }
        catch
        {
            StopWatchers(10000);
            throw;
        }
    }

    protected override void OnStop()
    {
        Log("Service stopping...");
        StopWatchers(10000);
    }

    private void StopWatchers(int timeoutMilliseconds)
    {
        running = false;
        stopEvent.Set();

        Thread[] threads;
        lock (watcherLock)
        {
            threads = watcherThreads.ToArray();
        }

        Stopwatch stopwatch = Stopwatch.StartNew();
        foreach (Thread thread in threads)
        {
            if (thread == Thread.CurrentThread || !thread.IsAlive)
                continue;

            int remaining = Math.Max(
                0,
                timeoutMilliseconds - (int)Math.Min(
                    Int32.MaxValue,
                    stopwatch.ElapsedMilliseconds));
            if (remaining == 0 || !thread.Join(remaining))
                break;
        }

        int remainingThreads;
        lock (watcherLock)
        {
            watcherThreads.RemoveAll(thread => !thread.IsAlive);
            remainingThreads = watcherThreads.Count;
        }
        if (remainingThreads > 0)
        {
            Log(
                "WARNING: " + remainingThreads +
                " registry watcher thread(s) did not stop within " +
                timeoutMilliseconds + " ms total.");
        }
    }

    private bool StartWatcher(ThreadStart action)
    {
        Thread thread = null;
        thread = new Thread(delegate()
        {
            try
            {
                action();
            }
            catch (Exception ex)
            {
                Log("Registry watcher terminated unexpectedly: " + ex);
            }
            finally
            {
                lock (watcherLock)
                {
                    watcherThreads.Remove(thread);
                }
            }
        });
        thread.IsBackground = true;

        lock (watcherLock)
        {
            if (!running)
                return false;

            watcherThreads.Add(thread);
            try
            {
                thread.Start();
            }
            catch
            {
                watcherThreads.Remove(thread);
                throw;
            }
        }
        return true;
    }

    private void WatchUsersRoot()
    {
        while (running)
        {
            try
            {
                using (RegistryKey users = Registry.Users)
                {
                    while (running)
                    {
                        using (ManualResetEvent changed = new ManualResetEvent(false))
                        {
                            int status = RegNotifyChangeKeyValue(
                                users.Handle.DangerousGetHandle(),
                                false,
                                RegChangeNotifyFilter.Name,
                                changed.SafeWaitHandle.DangerousGetHandle(),
                                true);

                            if (status != 0)
                            {
                                Log("RegNotifyChangeKeyValue(HKU) failed: " + status);
                                if (stopEvent.WaitOne(5000))
                                    return;
                                continue;
                            }

                            int signaled = WaitHandle.WaitAny(
                                new WaitHandle[] { stopEvent, changed });
                            if (signaled == 0 || !running)
                                return;
                        }

                        Log("HKEY_USERS changed; refreshing loaded-user watchers.");
                        AttachToExistingUsers();
                    }
                }
            }
            catch (Exception ex)
            {
                Log("HKEY_USERS watcher error: " + ex.Message);
                if (stopEvent.WaitOne(5000))
                    return;
            }
        }
    }

    private void AttachToExistingUsers()
    {
        string[] subKeys;
        try
        {
            subKeys = Registry.Users.GetSubKeyNames();
        }
        catch (Exception ex)
        {
            Log("Could not enumerate HKEY_USERS: " + ex.Message);
            return;
        }

        foreach (string sid in subKeys)
        {
            if (!running)
                return;
            if (IsLoadedUserSid(sid))
                TryAttachUser(sid);
        }
    }

    private static bool IsLoadedUserSid(string sid)
    {
        if (String.IsNullOrEmpty(sid))
            return false;
        if (sid.EndsWith("_Classes", StringComparison.OrdinalIgnoreCase))
            return false;
        if (sid.Equals(".DEFAULT", StringComparison.OrdinalIgnoreCase))
            return false;
        if (sid.Equals("S-1-5-18", StringComparison.OrdinalIgnoreCase) ||
            sid.Equals("S-1-5-19", StringComparison.OrdinalIgnoreCase) ||
            sid.Equals("S-1-5-20", StringComparison.OrdinalIgnoreCase))
            return false;

        return sid.StartsWith("S-1-5-21-", StringComparison.OrdinalIgnoreCase) ||
            sid.StartsWith("S-1-12-1-", StringComparison.OrdinalIgnoreCase);
    }

    private bool TryAttachUser(string sid)
    {
        RegistryKey key = null;
        bool registered = false;
        try
        {
            key = Registry.Users.OpenSubKey(
                sid + "\\" + NotificationSettingsPath,
                true);
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
            if (!StartWatcher(delegate() { WatchUserKey(key, sid); }))
            {
                key.Dispose();
                lock (watcherLock)
                {
                    watchedSids.Remove(sid);
                }
                return false;
            }

            Log("Attached to " + sid);
            return true;
        }
        catch (Exception ex)
        {
            if (key != null)
                key.Dispose();
            if (registered)
            {
                lock (watcherLock)
                {
                    watchedSids.Remove(sid);
                }
            }

            Log("Attach error for " + sid + ": " + ex.Message);
            if (running)
                EnsureRootWatcher(sid);
            return false;
        }
    }

    private void EnsureRootWatcher(string sid)
    {
        lock (watcherLock)
        {
            if (!running ||
                watchedSids.Contains(sid) ||
                watchedRootSids.Contains(sid))
                return;

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

            if (!StartWatcher(delegate() { WatchUserRoot(rootKey, sid); }))
            {
                rootKey.Dispose();
                lock (watcherLock)
                {
                    watchedRootSids.Remove(sid);
                }
                return;
            }

            Log("Watching user root for notification settings: " + sid);
        }
        catch (Exception ex)
        {
            if (rootKey != null)
                rootKey.Dispose();
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
                bool settingsExist;
                using (RegistryKey existing = Registry.Users.OpenSubKey(
                    sid + "\\" + NotificationSettingsPath,
                    true))
                {
                    settingsExist = existing != null;
                }
                if (settingsExist && TryAttachUser(sid))
                    return;

                using (ManualResetEvent changed = new ManualResetEvent(false))
                {
                    int status = RegNotifyChangeKeyValue(
                        rootKey.Handle.DangerousGetHandle(),
                        true,
                        RegChangeNotifyFilter.Name,
                        changed.SafeWaitHandle.DangerousGetHandle(),
                        true);

                    if (status != 0)
                    {
                        Log(
                            "RegNotifyChangeKeyValue(root) failed for " +
                            sid + ": " + status);
                        if (stopEvent.WaitOne(5000))
                            return;
                        continue;
                    }

                    int signaled = WaitHandle.WaitAny(
                        new WaitHandle[] { stopEvent, changed });
                    if (signaled == 0 || !running)
                        return;
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
                        RegChangeNotifyFilter.Name |
                            RegChangeNotifyFilter.LastSet,
                        changed.SafeWaitHandle.DangerousGetHandle(),
                        true);

                    if (status != 0)
                    {
                        Log(
                            "RegNotifyChangeKeyValue failed for " +
                            sid + ": " + status);
                        break;
                    }

                    int signaled = WaitHandle.WaitAny(
                        new WaitHandle[] { stopEvent, changed });
                    if (signaled == 0 || !running)
                        return;
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

            if (running)
                EnsureRootWatcher(sid);
        }
    }

    protected void Log(string message)
    {
        lock (logLock)
        {
            try
            {
                EnsureIniFile();
                if (!ReadIniBool("Settings", "LoggingEnabled", true))
                    return;

                string path = ModuleLocalPath(".log");
                string directory = Path.GetDirectoryName(path);
                if (!String.IsNullOrEmpty(directory))
                    Directory.CreateDirectory(directory);

                File.AppendAllText(
                    path,
                    DateTime.UtcNow.ToString("o") + "  " +
                        message + Environment.NewLine);
            }
            catch
            {
            }
        }
    }

    private void EnsureIniFile()
    {
        try
        {
            string path = ModuleLocalPath(".ini");
            if (File.Exists(path))
                return;

            string directory = Path.GetDirectoryName(path);
            if (!String.IsNullOrEmpty(directory))
                Directory.CreateDirectory(directory);

            File.WriteAllText(
                path,
                "[Settings]" + Environment.NewLine +
                    "LoggingEnabled=1" + Environment.NewLine);
        }
        catch
        {
        }
    }

    private bool ReadIniBool(string section, string key, bool fallback)
    {
        try
        {
            string path = ModuleLocalPath(".ini");
            if (!File.Exists(path))
                return fallback;

            string currentSection = "";
            foreach (string rawLine in File.ReadAllLines(path))
            {
                string line = rawLine.Trim();
                if (line.Length == 0 ||
                    line.StartsWith(";") ||
                    line.StartsWith("#"))
                    continue;

                if (line.StartsWith("[") && line.EndsWith("]"))
                {
                    currentSection =
                        line.Substring(1, line.Length - 2).Trim();
                    continue;
                }
                if (!currentSection.Equals(
                    section,
                    StringComparison.OrdinalIgnoreCase))
                    continue;

                int equals = line.IndexOf('=');
                if (equals <= 0)
                    continue;

                string name = line.Substring(0, equals).Trim();
                if (!name.Equals(key, StringComparison.OrdinalIgnoreCase))
                    continue;

                bool parsed;
                return TryParseBool(
                    line.Substring(equals + 1).Trim(),
                    out parsed)
                    ? parsed
                    : fallback;
            }
        }
        catch
        {
        }

        return fallback;
    }

    private static bool TryParseBool(string value, out bool parsed)
    {
        if (value == "1" ||
            value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("on", StringComparison.OrdinalIgnoreCase))
        {
            parsed = true;
            return true;
        }
        if (value == "0" ||
            value.Equals("false", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("no", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("off", StringComparison.OrdinalIgnoreCase))
        {
            parsed = false;
            return true;
        }

        parsed = false;
        return false;
    }

    private string ModuleLocalPath(string extension)
    {
        string module =
            System.Reflection.Assembly.GetExecutingAssembly().Location;
        string directory = Path.GetDirectoryName(module);
        if (String.IsNullOrEmpty(directory))
            directory = AppDomain.CurrentDomain.BaseDirectory;

        string name = Path.GetFileNameWithoutExtension(module);
        if (String.IsNullOrEmpty(name))
            name = settingsBaseName;

        return Path.Combine(directory, name + extension);
    }

}

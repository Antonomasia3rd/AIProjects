using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO.Pipes;
using System.Threading.Tasks;
using AIProjects.Dependencies;

static class RegistryNotificationAppTests
{
    static int checks;
    static void Check(bool ok, string message)
    { ++checks; if (!ok) throw new Exception(message); }
    sealed class Runtime : IRegistryNotificationUserRuntime
    {
        public int Starts, Stops, Disposals;
        public bool Logging, RejectStop;
        public Action<string> Report;
        public void Start(bool logging, Action<string> report) { ++Starts; Logging = logging; Report = report; }
        public void Stop() { ++Stops; if (RejectStop) throw new IOException("fixture stop pending"); }
        public void Dispose() { ++Disposals; Stop(); }
    }
    sealed class Startup : IManagedConfigurationStartupPlatform
    {
        public bool Installed;
        public ManagedStartupShortcutSpec Last;
        public bool IsInstalled(ManagedStartupShortcutSpec spec, out string error) { Last = spec; error = null; return Installed; }
        public bool Commit(ManagedStartupShortcutSpec spec, bool desired, Func<IDisposable> scope,
            ManagedStartupPersistenceSnapshot capture, ManagedStartupPersistenceMutation persist,
            ManagedStartupPersistenceMutation restore, out string error)
        {
            Last = spec;
            using (scope())
            {
                bool previous;
                if (!capture(out previous, out error)) return false;
                return ManagedStartupShortcut.CommitObservedCoupledState(desired, previous, Installed,
                    delegate(bool state, out string failure) { Installed = state; failure = null; return true; },
                    persist, restore, out error);
            }
        }
    }

    static Func<string[], int> Runner(string product)
    {
        return args => ManagedRegistryNotificationApp.Run(args, product + "Service", product, "fixture",
            delegate { throw new Exception("Real service factory was reached."); });
    }

    static void TestSecondInvocation(string product, string root)
    {
        string ini = Path.Combine(root, product + "-resident.ini");
        File.WriteAllText(ini, "[Settings]\nEnabled=1\nLoggingEnabled=1\nRunAtStartup=0\n");
        var runtime = new Runtime();
        var startup = new Startup();
        int factories = 0, reloads = 0;
        ManagedRegistryNotificationApp.RuntimeFactory = factory => { ++factories; return runtime; };
        ManagedConfigurationStartup.PlatformFactory = () => startup;
        ManagedRegistryNotificationApp.TrayRunner = ManagedConfigurationTray.Run;
        var owner = ManagedRegistryNotificationApp.BuildSpec(product, ini,
            delegate { throw new Exception("Real service factory was reached."); });
        var run = Runner(product);
        bool created;
        using (var ownership = new Mutex(true, ManagedConfigurationTray.InstanceMutexName(owner), out created))
        {
            Check(created, "The resident fixture must have a private profile identity.");
            try
            {
                using (owner.OpenResidentSession())
                using (var endpoint = new ManagedConfigurationReload(ManagedConfigurationTray.InstanceIdentity(owner),
                    action => action(), delegate { ++reloads; owner.ReloadResidentSession(); }))
                {
                    Check(runtime.Starts == 1, "The owner has exactly one injected watcher.");
                    Check(run(new[] { "--ini", ini, "--disable" }) == 0 && owner.ResidentStatus() == "Disabled" &&
                        reloads == 1 && factories == 1, "Second-invocation --disable is acknowledged only after the existing fake watcher stops.");
                    Check(run(new[] { "--ini", ini, "--enable", "--set", "LoggingEnabled=0" }) == 0 &&
                        owner.ResidentStatus() == "Watching current user" && !runtime.Logging && runtime.Starts == 2 &&
                        reloads == 2 && factories == 1, "Second-invocation enabled/logging batch reaches the existing watcher without a replacement tray.");
                    int stops = runtime.Stops;
                    Check(run(new[] { "--ini", ini, "--tray" }) == 0 && reloads == 2 && runtime.Stops == stops,
                        "An ordinary duplicate --tray invocation does not reload resident settings.");
                    Check(run(new[] { "--ini", ini, "--configure-only", "--disable" }) == 0 &&
                        owner.ResidentStatus() == "Watching current user" && reloads == 2 && runtime.Stops == stops,
                        "Offline configuration does not signal or mutate the existing fake watcher.");
                    Check(run(new[] { "--ini", ini, "--show-config" }) == 0 && reloads == 2,
                        "Configuration inspection remains passive while a resident exists.");
                    runtime.RejectStop = true;
                    Check(run(new[] { "--ini", ini, "--disable" }) != 0 &&
                        ManagedIniFile.LoadSection(ManagedRegistryNotificationApp.IniSpec(ini), false)["Enabled"] == "0" &&
                        runtime.Starts == 2 && reloads == 3,
                        "Failed resident application returns nonzero while preserving the already-saved preference.");
                    runtime.RejectStop = false;
                    Check(run(new[] { "--ini", ini, "--disable" }) == 0 && owner.ResidentStatus() == "Disabled" && reloads == 4,
                        "A later invocation retries a rejected live reload successfully.");
                    var other = ManagedRegistryNotificationApp.BuildSpec(product, Path.Combine(root, "other-" + product + ".ini"),
                        delegate { throw new Exception("Real service factory was reached."); });
                    string error;
                    Check(!ManagedConfigurationReload.Request(ManagedConfigurationTray.InstanceIdentity(other), 80, out error) &&
                        reloads == 4, "A different INI identity cannot signal this resident.");
                }
                Check(runtime.Disposals == 1, "The injected owner session is disposed after the receiver stops.");
                string missingError;
                Check(!ManagedConfigurationReload.Request(ManagedConfigurationTray.InstanceIdentity(owner), 80, out missingError),
                    "A disposed private receiver no longer acknowledges requests.");
                Check(ManagedConfigurationTray.ReloadExisting(owner, 80) != 0,
                    "An existing owner without a receiver cannot report a live settings mutation as successful.");
            }
            finally { ownership.ReleaseMutex(); }
        }
    }

    static void TestReloadDeadlinesAndDisposal()
    {
        string identity = "notification-reload-fixture-" + Guid.NewGuid().ToString("N");
        var queue = new ConcurrentQueue<Action>();
        int calls = 0, queueMode = 1;
        using (var endpoint = new ManagedConfigurationReload(identity,
            action => { if (Interlocked.CompareExchange(ref queueMode, 0, 0) == 1) queue.Enqueue(action); else action(); },
            delegate { Interlocked.Increment(ref calls); }))
        {
            string error;
            var clock = Stopwatch.StartNew();
            Check(!ManagedConfigurationReload.Request(identity, 750, out error) && clock.ElapsedMilliseconds < 2000,
                "An unresponsive UI dispatcher has a bounded acknowledgment deadline.");
            Action expired;
            Check(queue.TryDequeue(out expired), "The deadline fixture queued one synthetic UI callback.");
            expired();
            Check(calls == 0, "Expired queued callbacks do not apply settings after the caller timed out.");
            Interlocked.Exchange(ref queueMode, 0);
            Check(ManagedConfigurationReload.Request(identity, 1000, out error) && calls == 1,
                "A timed-out connection cannot supply a stale acknowledgment to a later request.");
        }

        identity = "notification-reload-dispose-" + Guid.NewGuid().ToString("N");
        var queued = new ConcurrentQueue<Action>();
        var receiver = new ManagedConfigurationReload(identity, action => queued.Enqueue(action), delegate { ++calls; });
        using (var client = new NamedPipeClientStream(".", ManagedConfigurationReload.PipeName(identity), PipeDirection.InOut, PipeOptions.Asynchronous))
        {
            client.Connect(500);
            // A peer that connects but sends nothing must not trap disposal.
            var clock = Stopwatch.StartNew();
            receiver.Dispose();
            Check(clock.ElapsedMilliseconds < 2000, "Receiver disposal cancels a connected peer's incomplete request without an unbounded join.");
        }
        receiver.Dispose();
        string missing;
        Check(!ManagedConfigurationReload.Request(identity, 80, out missing),
            "Disposal leaves no private fixture listener accepting new work.");

        identity = "notification-reload-queued-dispose-" + Guid.NewGuid().ToString("N");
        queued = new ConcurrentQueue<Action>();
        receiver = new ManagedConfigurationReload(identity, action => queued.Enqueue(action), delegate { ++calls; });
        try
        {
            string queuedIdentity = identity;
            Task<bool> request = Task<bool>.Factory.StartNew(delegate
            {
                string failure;
                return ManagedConfigurationReload.Request(queuedIdentity, 1500, out failure);
            });
            var queueDeadline = Stopwatch.StartNew();
            while (queued.IsEmpty && !request.IsCompleted && queueDeadline.ElapsedMilliseconds < 1000) Thread.Sleep(1);
            Action canceled;
            Check(queued.TryDequeue(out canceled), "Disposal fixture queued a private UI request.");
            int before = calls;
            receiver.Dispose();
            canceled();
            Check(calls == before && request.Wait(2000) && !request.Result,
                "Receiver disposal suppresses queued UI work and never acknowledges an unapplied change.");
        }
        finally { receiver.Dispose(); }
    }

    static void TestUtilityTrayCompatibility(string root)
    {
        var spec = new ManagedConfigurationTraySpec {
            ProductName = "Private utility fixture", IniPath = Path.Combine(root, "utility.ini"),
            ReadSettings = () => new Dictionary<string, string>(),
            SaveSettings = values => { throw new Exception("Utility persistence was unexpectedly invoked."); },
            ReloadExistingInstance = true // No resident callback: no IPC receiver/client.
        };
        bool created;
        using (var mutex = new Mutex(true, ManagedConfigurationTray.InstanceMutexName(spec), out created))
        {
            Check(created, "The utility fixture identity is private.");
            try
            {
                Check(ManagedConfigurationTray.Run(spec) == 0 && !File.Exists(spec.IniPath),
                    "Duplicate utility configuration trays keep their behavior and open no resident reload channel.");
            }
            finally { mutex.ReleaseMutex(); }
        }
    }
    [STAThread]
    static int Main()
    {
        using (var deadline = new Timer(delegate { Environment.FailFast("Notification app tests exceeded 45 seconds."); }, null, 45000, Timeout.Infinite))
        {
            string parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar);
            string root = Path.GetFullPath(Path.Combine(parent, "AIProjects-NotificationApp-" + Guid.NewGuid().ToString("N")));
            if (!String.Equals(Path.GetDirectoryName(root), parent, StringComparison.OrdinalIgnoreCase)) return 2;
            Directory.CreateDirectory(root);
            var oldRuntime = ManagedRegistryNotificationApp.RuntimeFactory;
            var oldTray = ManagedRegistryNotificationApp.TrayRunner;
            var oldStartup = ManagedConfigurationStartup.PlatformFactory;
            try
            {
                foreach (string product in new[] { "AllowContentAboveLock", "YourPhoneHideBanner" })
                {
                    string ini = Path.Combine(root, product + ".ini");
                    int factories = 0, trayCalls = 0;
                    var runtime = new Runtime();
                    var startup = new Startup();
                    ManagedRegistryNotificationApp.RuntimeFactory = factory => { ++factories; return runtime; };
                    ManagedConfigurationStartup.PlatformFactory = () => startup;
                    ManagedRegistryNotificationApp.TrayRunner = spec => { ++trayCalls; return 0; };
                    Func<string[], int> run = args => ManagedRegistryNotificationApp.Run(args,
                        product + "Service", product, "fixture", delegate { throw new Exception("Real service factory was reached."); });
                    foreach (string option in new[] { "--help", "--version", "--show-config" })
                        Check(run(new[] { "--ini", ini, option }) == 0 && !File.Exists(ini), product + " passive mode must not create a profile");
                    Check(factories == 0 && trayCalls == 0, "Passive modes must not create registry sessions or UI.");
                    Check(run(new[] { "--ini", ini, "--configure-only", "--set", "Enabled=off" }) == 0 && factories == 0,
                        "Offline configure-only must not apply the policy.");
                    var file = ManagedRegistryNotificationApp.IniSpec(ini);
                    Check(ManagedIniFile.LoadSection(file, false)["Enabled"] == "0", "Saved CLI boolean must be canonical.");
                    byte[] original = File.ReadAllBytes(ini);
                    foreach (var args in new[] {
                        new[] { "--configure-only", "--startup" },
                        new[] { "--configure-only", "--set", "RunAtStartup=1" },
                        new[] { "--show-config", "--disable" },
                        new[] { "--tray", "--show-config" },
                        new[] { "--set", "Enabled=invalid" }, new[] { "--unknown" }
                    })
                        Check(run(new[] { "--ini", ini }.Concat(args).ToArray()) != 0 && original.SequenceEqual(File.ReadAllBytes(ini)),
                            "Invalid/offline-integration command must leave the profile unchanged.");
                    Check(run(new[] { "--ini", ini, "--startup" }) == 0 && startup.Installed && factories == 0,
                        "Startup management must not run the policy.");
                    Check(startup.Last.Arguments == "--tray --ini " + ManagedConfigurationStartup.QuoteArgument(ini),
                        "Per-user Startup must target this profile's tray.");
                    Check(run(new[] { "--ini", ini, "--no-startup" }) == 0 && !startup.Installed,
                        "Startup can be disabled through the same adapter.");

                    ManagedRegistryNotificationApp.TrayRunner = spec =>
                    {
                        Check(factories == 0, "Constructing a tray specification must remain inert.");
                        using (spec.OpenResidentSession())
                        {
                            Check(runtime.Starts == 0 && spec.ResidentStatus() == "Disabled", "Saved disabled state must not start a watcher.");
                            spec.SaveSettings(new Dictionary<string, string> { { "Enabled", "1" }, { "LoggingEnabled", "0" } });
                            Check(runtime.Starts == 1 && !runtime.Logging, "Tray edits apply a current-user session after persistence.");
                            spec.ReloadResidentSession();
                            Check(runtime.Starts == 1, "Unchanged reload must not create duplicate watchers.");
                            runtime.Report("Attach error: fixture unavailable");
                            Check(spec.ResidentStatus().Contains("fixture unavailable"), "Watcher errors must reach the tray status.");
                            runtime.RejectStop = true;
                            bool failed = false;
                            try { spec.SaveSettings(new Dictionary<string, string> { { "Enabled", "0" } }); }
                            catch (IOException ex) { failed = ex.Message.Contains("Settings saved"); }
                            Check(failed && ManagedIniFile.LoadSection(file, false)["Enabled"] == "0" && runtime.Starts == 1,
                                "Failed runtime reload must report a successful save separately and never start another watcher.");
                            runtime.RejectStop = false;
                            spec.ReloadResidentSession();
                            Check(spec.ResidentStatus() == "Disabled", "Reload retries a pending stop using the saved preference.");
                            File.WriteAllText(ini, "[Settings]\nEnabled=broken\nLoggingEnabled=bad\n");
                            spec.SaveSettings(new Dictionary<string, string> { { "Enabled", "1" }, { "LoggingEnabled", "1" } });
                            Check(runtime.Starts == 2 && runtime.Logging, "A complete tray batch repairs invalid saved values.");
                            runtime.Report("Attach error: fixture retry");
                            spec.ReloadResidentSession();
                            Check(runtime.Starts == 3 && spec.ResidentStatus() == "Watching current user",
                                "Explicit reload retries a faulted watcher even when configuration is unchanged.");
                        }
                        Check(runtime.Disposals == 1, "Closing a resident tray must dispose its session.");
                        return 0;
                    };
                    Check(run(new[] { "--ini", ini, "--tray" }) == 0 && factories == 1, "Explicit tray runs exactly one injected session.");
                    TestSecondInvocation(product, root);
                }
                TestReloadDeadlinesAndDisposal();
                TestUtilityTrayCompatibility(root);
                Check(!RegistryNotificationServiceBase.ParseLoggingSetting("[Settings]\n\"LoggingEnabled\" = \"off\" ; tray\n"),
                    "Manual service mode accepts the same quoted INI dialect as the tray.");
                Console.WriteLine("Notification app checks passed: " + checks + ". No registry, service or real Startup operations ran.");
                return 0;
            }
            catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
            finally
            {
                ManagedRegistryNotificationApp.RuntimeFactory = oldRuntime;
                ManagedRegistryNotificationApp.TrayRunner = oldTray;
                ManagedConfigurationStartup.PlatformFactory = oldStartup;
                if (!String.Equals(Path.GetDirectoryName(Path.GetFullPath(root)), parent, StringComparison.OrdinalIgnoreCase))
                    throw new IOException("Unexpected fixture cleanup path.");
                Directory.Delete(root, true);
            }
        }
    }
}

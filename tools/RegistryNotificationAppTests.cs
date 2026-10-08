using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
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
                }
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

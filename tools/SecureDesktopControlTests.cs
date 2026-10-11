using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;
using AIProjects.Dependencies;

static class SecureDesktopControlTests
{
    static int checks;
    static void Check(bool ok, string message) { ++checks; if (!ok) throw new Exception(message); }
    static void Throws<T>(Action action, string message) where T : Exception
    { ++checks; try { action(); } catch (T) { return; } throw new Exception(message); }
    static Dictionary<string, string> Batch(params string[] pairs)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int index = 0; index < pairs.Length; index += 2) result[pairs[index]] = pairs[index + 1];
        return result;
    }
    sealed class Startup : IManagedConfigurationStartupPlatform
    {
        public bool Installed;
        public int FailRemovals, Calls;
        public ManagedStartupShortcutSpec Last;
        public bool IsInstalled(ManagedStartupShortcutSpec spec, out string error) { Last = spec; error = null; return Installed; }
        public bool Commit(ManagedStartupShortcutSpec spec, bool desired, Func<IDisposable> scope,
            ManagedStartupPersistenceSnapshot capture, ManagedStartupPersistenceMutation persist,
            ManagedStartupPersistenceMutation restore, out string error)
        {
            ++Calls; Last = spec;
            using (scope())
            {
                bool previous;
                if (!capture(out previous, out error)) return false;
                return ManagedStartupShortcut.CommitObservedCoupledState(desired, previous, Installed,
                    delegate(bool state, out string failure)
                    {
                        Installed = state;
                        if (!state && FailRemovals-- > 0) { failure = "fixture removal failed"; return false; }
                        failure = null; return true;
                    }, persist, restore, out error);
            }
        }
    }
    [STAThread]
    static int Main()
    {
        using (var timeout = new System.Threading.Timer(delegate { Environment.FailFast("Secure controller fixture exceeded 45 seconds."); }, null, 45000, Timeout.Infinite))
        {
            string parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar);
            string root = Path.GetFullPath(Path.Combine(parent, "AIProjects-SecureControl-" + Guid.NewGuid().ToString("N")));
            if (!String.Equals(Path.GetDirectoryName(root), parent, StringComparison.OrdinalIgnoreCase)) return 2;
            Directory.CreateDirectory(root);
            var oldTray = SecureDesktopLauncherControl.TrayRunner;
            var oldEditor = SecureDesktopLauncherControl.EditorRunner;
            var oldNative = SecureDesktopLauncherControl.NativeCommand;
            var oldImage = SecureDesktopLauncherControl.InstalledImagePath;
            var oldElevated = SecureDesktopLauncherControl.ElevatedCommand;
            var oldStartup = ManagedConfigurationStartup.PlatformFactory;
            string previousDirectory = Directory.GetCurrentDirectory();
            try
            {
                string ini = Path.Combine(root, "SecureDesktopLauncher.ini");
                string passwordIni = Path.Combine(root, "SecureDesktopPasswordLauncher.ini");
                var startup = new Startup();
                ManagedConfigurationStartup.PlatformFactory = () => startup;
                int nativeCalls = 0, trayCalls = 0, editorCalls = 0;
                string lastAction = null, lastIni = null;
                SecureDesktopLauncherControl.NativeCommand = delegate(string action, string nativeExe, string selected)
                { ++nativeCalls; lastAction = action; lastIni = selected; return 12; };
                SecureDesktopLauncherControl.TrayRunner = spec => { ++trayCalls; Check(spec.OpenResidentSession == null, "Configuration tray must not start a resident launcher."); return 0; };
                SecureDesktopLauncherControl.EditorRunner = spec => { ++editorCalls; };
                Func<string[], int> run = args => SecureDesktopLauncherControl.Run(new[] { "--ini", ini }.Concat(args).ToArray());
                foreach (string option in new[] { "--help", "--version", "--show-config" })
                    Check(run(new[] { option }) == 0 && !File.Exists(ini), "Passive mode must not create the native profile.");
                Check(nativeCalls == 0 && trayCalls == 0 && editorCalls == 0, "Passive modes must not launch helpers, editors or tray.");
                Check(run(new string[0]) == 0 && trayCalls == 1 && nativeCalls == 0, "Default entry opens only configuration.");
                Check(run(new[] { "--tray" }) == 0 && nativeCalls == 0, "Explicit tray entry must not launch native helpers.");
                Check(run(new[] { "--configure-only", "--set", "General.LaunchSpacingMs=50", "--set", "Control.PasswordIniPath=" + passwordIni }) == 0,
                    "Offline multi-section edit must save coherently.");
                var profile = new SecureDesktopControlProfile(ini, false);
                Check(profile.Read()["General.LaunchSpacingMs"] == "50" && nativeCalls == 0, "Offline save must not invoke a service.");
                Check(run(new[] { "--add-program", "Example.With.Dots", "--configure-only" }) == 0,
                    "Program names containing dots must remain unambiguous.");
                Check(profile.Read()["Program:Example.With.Dots.Enabled"] == "0", "New entries default disabled.");
                byte[] before = File.ReadAllBytes(ini);
                Check(run(new[] { "--add-program", "Example.With.Dots", "--configure-only" }) != 0 && before.SequenceEqual(File.ReadAllBytes(ini)),
                    "Duplicate program creation must never disable/merge an existing entry.");
                Check(run(new[] { "--configure-only", "--set", "Program:Example.With.Dots.Path=" + Path.Combine(root, "example.exe"),
                    "--set", "Program:Example.With.Dots.Enabled=on", "--set", "Program:Example.With.Dots.Arguments=--user \"{Account}\" --session {SessionId}" }) == 0,
                    "A whole batch can enable a configured program and preserve literal native placeholders.");
                Check(profile.Read()["Program:Example.With.Dots.WorkingDirectory"] == root &&
                    profile.Read()["Program:Example.With.Dots.LaunchSpacingMs"] == "50", "Missing program fields must inherit native defaults.");
                Check(run(new[] { "--configure-only", "--set", "General.LaunchSpacingMs=70" }) == 0 &&
                    profile.Read()["Program:Example.With.Dots.LaunchSpacingMs"] == "70", "General edits must change inherited effective defaults.");
                var raw = ManagedIniFile.LoadDocument(profile.Document);
                Check(!raw.GetSection("Program:Example.With.Dots").ContainsKey("LaunchSpacingMs"), "Inherited values must not be materialized by unrelated edits.");
                int added = 0, duplicates = 0;
                Exception addError = null;
                var contenders = Enumerable.Range(0, 4).Select(index => new Thread(delegate()
                {
                    try { profile.AddProgram("Concurrent.With.Dots", Batch()); Interlocked.Increment(ref added); }
                    catch (ArgumentException) { Interlocked.Increment(ref duplicates); }
                    catch (Exception ex) { addError = ex; }
                }) { IsBackground = true }).ToArray();
                foreach (var thread in contenders) thread.Start();
                Check(contenders.All(thread => thread.Join(4000)) && addError == null && added == 1 && duplicates == 3,
                    "Concurrent program creation must accept one writer and reject every duplicate without disabling an existing program.");
                Check(profile.Read()["Program:Concurrent.With.Dots.Enabled"] == "0", "Concurrent creation must retain the intended initial disabled state.");

                before = File.ReadAllBytes(ini);
                foreach (var args in new[] {
                    new[] { "--configure-only", "--startup" }, new[] { "--configure-only", "--set", "Control.RunAtStartup=1" },
                    new[] { "--configure-only", "--set", "General.LaunchSpacingMs=invalid" }, new[] { "--configure-only", "--set", "Unknown.Key=1" },
                    new[] { "--start-service", "--show-config" }, new[] { "--start-service", "--password-profile" },
                    new[] { "--configure-only", "--set", "Program:Example.With.Dots.Path=", "--set", "Program:Example.With.Dots.CommandLine=anything.exe" }
                }) Check(run(args) != 0 && before.SequenceEqual(File.ReadAllBytes(ini)), "Rejected mixed/invalid commands must leave every section unchanged.");
                Check(nativeCalls == 0, "Rejected commands must not perform native actions.");
                using (var held = new FileStream(ini, FileMode.Open, FileAccess.Read, FileShare.Read))
                    Check(run(new[] { "--configure-only", "--set", "General.LaunchSpacingMs=80" }) != 0 && before.SequenceEqual(File.ReadAllBytes(ini)),
                        "A pinned/locked native file must fail without partial changes.");
                Check(run(new[] { "--configure-only", "--set", "General.LaunchSpacingMs=80" }) == 0, "An unlocked configuration edit must be retryable.");

                File.WriteAllText(ini, "; preserve every section\r\n[Control]\r\nRunAtStartup=1\r\n[General]\r\nLaunchSpacingMs=90\r\n[Other]\r\nKeep=this\r\n", System.Text.Encoding.Unicode);
                before = File.ReadAllBytes(ini); startup.Installed = true; startup.FailRemovals = 1;
                Check(run(new[] { "--no-startup", "--set", "General.LaunchSpacingMs=100" }) != 0 && before.SequenceEqual(File.ReadAllBytes(ini)) && startup.Installed,
                    "Startup removal failure must restore exact bytes of the whole multi-section profile.");
                Check(run(new[] { "--no-startup", "--set", "General.LaunchSpacingMs=100" }) == 0 && !startup.Installed,
                    "Startup and native field changes must commit together on retry.");
                Check(run(new[] { "--startup" }) == 0 && startup.Installed && nativeCalls == 0, "Startup management must start only the future tray.");
                Check(startup.Last.Arguments == "--tray --ini " + ManagedConfigurationStartup.QuoteArgument(ini), "Startup must select this exact profile.");
                Check(ManagedIniFile.LoadDocument(profile.Document).GetSection("Other")["Keep"] == "this", "Unknown sections must survive edits.");
                File.WriteAllText(ini, "[Control]\nRunAtStartup=broken\n[General]\nLaunchSpacingMs=invalid\n");
                startup.Installed = false;
                Check(run(new[] { "--startup", "--set", "General.LaunchSpacingMs=75" }) == 0 && startup.Installed &&
                    profile.Read()["General.LaunchSpacingMs"] == "75", "One batch must repair invalid Startup and native settings before validation/publication.");

                Check(run(new[] { "--password-ini", passwordIni, "--configure-only", "--set", "UI.TopMost=true", "--set", "Security.MaxAttempts=5" }) == 0,
                    "The separate password profile must share the editor/writer dialect.");
                var password = new SecureDesktopControlProfile(passwordIni, true);
                before = File.ReadAllBytes(passwordIni);
                foreach (string key in new[] { "Kdf", "Iterations", "SaltHex", "PasswordHashHex", "PasswordPbkdf2HashHex", "PasswordAttemptCount", "LockoutUntilUtcFileTime" })
                    Check(run(new[] { "--password-ini", passwordIni, "--configure-only", "--set", "Security." + key + "=changed" }) != 0 && before.SequenceEqual(File.ReadAllBytes(passwordIni)),
                        "Password-derived fields must stay under the native password lifecycle.");
                var passwordSpec = SecureDesktopLauncherControl.BuildSpec(password);
                using (var form = ManagedConfigurationTray.CreateEditor(passwordSpec, delegate { throw new Exception("Unexpected offscreen editor error."); }))
                {
                    var grid = (DataGridView)form.Controls.Find("ConfigurationSettingsGrid", true).Single();
                    foreach (string key in new[] { "Kdf", "Iterations", "SaltHex", "PasswordHashHex", "PasswordPbkdf2HashHex", "PasswordAttemptCount", "LockoutUntilUtcFileTime" })
                        Check(grid.Rows.Cast<DataGridViewRow>().Single(row => (string)row.Cells[0].Value == "Security." + key).Cells[1].ReadOnly,
                            "Actual editor must keep Security." + key + " read-only.");
                    Check(!grid.Rows.Cast<DataGridViewRow>().Single(row => (string)row.Cells[0].Value == "Security.MaxAttempts").Cells[1].ReadOnly,
                        "Ordinary password policy settings must remain editable.");
                }
                var editor = new ManagedConfigurationEditor(passwordSpec);
                var altered = editor.Values; altered["Security.Iterations"] = "1";
                Throws<ArgumentException>(() => editor.Save(altered), "Programmatic editor saves must reject readonly changes too.");
                Check(before.SequenceEqual(File.ReadAllBytes(passwordIni)), "Readonly rejection must not publish data.");

                Check(run(new[] { "--start-service" }) == 12 && lastAction == "start-service" && lastIni == ini, "Explicit action must propagate the injected native result.");
                string draft = Path.Combine(root, "draft.ini");
                Check(SecureDesktopLauncherControl.Run(new[] { "--ini", draft, "--install-service" }) != 0, "Native actions must reject alternate draft routing.");
                Directory.SetCurrentDirectory(root);
                Check(run(new[] { "--password-ini", "SecureDesktopPasswordLauncher.ini", "--set-password" }) == 12 && lastIni == passwordIni,
                    "Relative password selection must use the same base in editor and native action routing.");
                int elevatedCalls = 0;
                string exe = Path.ChangeExtension(ini, ".exe"); File.WriteAllText(exe, "inert fixture - never execute");
                SecureDesktopLauncherControl.ElevatedCommand = delegate(string tool, string arguments, string mode) { ++elevatedCalls; return 0; };
                SecureDesktopLauncherControl.InstalledImagePath = () => "\"C:\\another\\SecureDesktopLauncher.exe\"";
                foreach (string action in new[] { "start-service", "stop-service", "uninstall-service" })
                    Throws<IOException>(() => SecureDesktopLauncherControl.ExecuteNative(action, exe, ini), "Every installed-service action must verify routing ownership.");
                Check(elevatedCalls == 0, "Foreign service routing must never request elevation.");
                Check(!SecureDesktopLauncherControl.OwnsInstalledImage(ManagedConfigurationStartup.QuoteArgument(exe) + " --other", exe),
                    "Installed command arguments must not be mistaken for the selected sibling helper.");
                Check(!SecureDesktopLauncherControl.OwnsInstalledImage(@"C:\Program Files\Tools\service.exe", @"C:\Program Files\Tools\service.exe"),
                    "Ambiguous unquoted paths containing spaces must require explicit installer correction.");
                SecureDesktopLauncherControl.InstalledImagePath = () => null;
                Check(SecureDesktopLauncherControl.ExecuteNative("uninstall-service", exe, ini) == 0 && elevatedCalls == 0,
                    "Uninstalling an absent service must be an inert success.");
                SecureDesktopLauncherControl.InstalledImagePath = () => ManagedConfigurationStartup.QuoteArgument(exe);
                Check(SecureDesktopLauncherControl.ExecuteNative("stop-service", exe, ini) == 0 && elevatedCalls == 1, "Owned routing can dispatch an explicit stop through the injected platform.");
                Throws<ArgumentException>(() => SecureDesktopLauncherControl.ExecuteNative("unknown", exe, ini), "Unknown native actions must never fall through to password setup.");
                Console.WriteLine("SecureDesktop control checks passed: " + checks + ". No service, elevation, Startup or native launcher ran.");
                return 0;
            }
            catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
            finally
            {
                Directory.SetCurrentDirectory(previousDirectory);
                SecureDesktopLauncherControl.TrayRunner = oldTray; SecureDesktopLauncherControl.EditorRunner = oldEditor;
                SecureDesktopLauncherControl.NativeCommand = oldNative; SecureDesktopLauncherControl.InstalledImagePath = oldImage;
                SecureDesktopLauncherControl.ElevatedCommand = oldElevated; ManagedConfigurationStartup.PlatformFactory = oldStartup;
                if (!String.Equals(Path.GetDirectoryName(Path.GetFullPath(root)), parent, StringComparison.OrdinalIgnoreCase)) throw new IOException("Unexpected fixture cleanup target.");
                Directory.Delete(root, true);
            }
        }
    }
}

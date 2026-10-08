using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using AIProjects.Dependencies;

static class ChromeProfileCounterAppTests
{
    const string Original = "{\"profile\":{\"profiles_created\":4,\"info_cache\":{\"Profile 3\":{}}},\"other\":\"preserve\"}";
    static int checks;
    static int engines;
    static int observations;
    static int trays;
    static bool running;
    static string output;
    static string errors;
    static string root;
    static string profile;
    static string data;
    static string state;
    static readonly Queue<string> answers = new Queue<string>();
    static readonly FixtureStartup startup = new FixtureStartup();
    static ChromeProfileCounterFileSystem files;
    static ManagedConfigurationTraySpec shownTray;
    static ChromeProfileCounter.Options lastOptions;

    static void Check(bool condition, string message)
    { ++checks; if (!condition) throw new Exception(message); }
    static int Run(params string[] args)
    {
        TextWriter previousOut = Console.Out, previousError = Console.Error;
        using (var stdout = new StringWriter())
        using (var stderr = new StringWriter())
        {
            Console.SetOut(stdout); Console.SetError(stderr);
            try { return ChromeProfileCounter.Run(args); }
            finally { output = stdout.ToString(); errors = stderr.ToString(); Console.SetOut(previousOut); Console.SetError(previousError); }
        }
    }
    static int ProfileRun(params string[] args)
    { return Run(new[] { "--ini", profile }.Concat(args).ToArray()); }
    static void ResetState()
    { File.WriteAllText(state, Original, new UTF8Encoding(false)); running = false; answers.Clear(); files = null; }

    sealed class FixtureStartup : IManagedConfigurationStartupPlatform
    {
        public bool Installed;
        public bool FailRemoval;
        public int Commits;
        public ManagedStartupShortcutSpec Shortcut;
        public bool IsInstalled(ManagedStartupShortcutSpec spec, out string error)
        { error = null; return Installed; }
        public bool Commit(ManagedStartupShortcutSpec spec, bool desired, Func<IDisposable> scope,
            ManagedStartupPersistenceSnapshot capture, ManagedStartupPersistenceMutation persist,
            ManagedStartupPersistenceMutation restore, out string error)
        {
            ++Commits; Shortcut = spec;
            try
            {
                using (scope())
                {
                    bool previous;
                    if (!capture(out previous, out error)) return false;
                    return ManagedStartupShortcut.CommitObservedCoupledState(desired, previous, Installed,
                        delegate(bool enabled, out string mutationError)
                        {
                            mutationError = null;
                            if (!enabled && FailRemoval) { FailRemoval = false; mutationError = "Injected removal failure."; return false; }
                            Installed = enabled; return true;
                        }, persist, restore, out error);
                }
            }
            catch (Exception ex) { error = ex.Message; return false; }
        }
    }

    sealed class VerifyFailureFiles : ChromeProfileCounterFileSystem
    {
        public override void Replace(string temporary, string destination, string backup)
        {
            base.Replace(temporary, destination, backup);
            File.WriteAllText(destination, ChromeProfileCounterEngine.SetCounterInText(Original, 17));
        }
    }

    static void ReadOnlyAndConfiguration()
    {
        string missing = Path.Combine(root, "not-created", "profile.ini");
        Check(Run("--help", "--unknown", "--ini", "") == 0 && output.Contains("--tray"), "Help must be independent of invalid operational options.");
        Check(Run("--version", "--unknown") == 0 && output.Contains("ChromeProfileCounter"), "Version must be independent of invalid operational options.");
        Check(Run("--ini", missing, "--show-config") == 0 && !Directory.Exists(Path.GetDirectoryName(missing)), "Read-only configuration inspection created its parent/profile.");
        Check(engines == 0 && observations == 0 && startup.Commits == 0 && trays == 0, "Information/configuration inspection reached an operational dependency.");
        Check(ProfileRun("--configure-only", "--set", "Settings.UserDataDirectory=" + data,
            "--set", "Settings.BackupDirectory=Backups") == 0 && File.Exists(profile), "Configuration batch was not persisted.");
        Check(!File.Exists(state), "Saving configuration created Chrome Local State.");
        Check(ProfileRun("--show-config") == 0 && output.Contains(Path.Combine(data, "Backups")), "Relative backup path did not use UserDataDirectory.");
        Check(engines == 0 && observations == 0 && startup.Commits == 0, "Configuration writes reached operational dependencies.");
        byte[] initial = File.ReadAllBytes(profile);
        string[][] invalid = {
            new[] { "--unknown" }, new[] { "--ini", "" }, new[] { "--set-counter" },
            new[] { "--set-counter", "0" }, new[] { "--set-counter", "2147483648" },
            new[] { "--set-counter", "1", "--auto-fix" }, new[] { "--status", "--show-config" },
            new[] { "--tray", "--auto-fix" }, new[] { "--startup", "--set-counter", "1" },
            new[] { "--no-startup", "--auto-fix", "--yes" }, new[] { "--configure-only", "--auto-fix" },
            new[] { "--yes" }, new[] { "--status", "--yes" }, new[] { "--tray", "--yes" },
            new[] { "--startup", "--no-startup" }, new[] { "--startup", "--show-config" },
            new[] { "--tray", "--set", "UserDataDirectory=" + data },
            new[] { "--configure-only", "--user-data", data },
            new[] { "--set", "BackupDirectory=Other" },
            new[] { "--configure-only", "--set", "RunAtStartup=1" },
            new[] { "--configure-only", "--set", "Other.BackupDirectory=Other" },
            new[] { "--configure-only", "--set", "Unknown=1" },
            new[] { "--configure-only", "--set", "BackupDirectory=" },
            new[] { "--configure-only", "--set", "BackupDirectory=bad\npath" },
            new[] { "--configure-only", "--set", "BackupDirectory=" + state }
        };
        foreach (string[] args in invalid)
        {
            Check(ProfileRun(args) != 0 && errors.Contains("ERROR:"), "Invalid arguments must return a clear error: " + String.Join(" ", args));
            Check(initial.SequenceEqual(File.ReadAllBytes(profile)), "Rejected arguments changed the profile.");
        }
        Check(engines == 0 && observations == 0 && startup.Commits == 0, "Rejected passive/action combinations reached Chrome or Startup.");
        string collided = Path.Combine(root, "collision", "Local State");
        Check(Run("--ini", collided, "--configure-only", "--set", "UserDataDirectory=" + Path.GetDirectoryName(collided)) != 0 && !File.Exists(collided), "Configuration may not create an INI over Chrome Local State.");

        File.WriteAllText(profile, "; retain comment\n[Settings]\nUserDataDirectory=\nBackupDirectory=Backups\nRunAtStartup=0\n");
        var editor = new ManagedConfigurationEditor(ChromeProfileCounter.BuildTraySpec(profile));
        var edited = editor.Values; edited["UserDataDirectory"] = data; editor.Save(edited);
        Check(File.ReadAllText(profile).Contains("; retain comment") && ChromeProfileCounter.ReadOptions(profile, new Dictionary<string, string>()).UserDataDirectory == data, "Shared tray writer could not repair malformed stored values while preserving comments.");
        Check(ProfileRun("--configure-only", "--set", "BackupDirectory=Backups") == 0, "CLI and tray profile writers diverged.");
        Check(ProfileRun("--show-config", "--user-data", "relative-data", "--backup-directory", "relative-backups") == 0 &&
            output.Contains(Path.Combine(Directory.GetCurrentDirectory(), "relative-data")) && output.Contains(Path.Combine(Directory.GetCurrentDirectory(), "relative-backups")),
            "One-run relative path overrides must use the working directory.");
    }

    static void StartupAndTray()
    {
        int oldEngines = engines, oldObservations = observations;
        Check(ProfileRun("--startup") == 0 && startup.Installed, "Startup enable did not use the injected transaction platform.");
        Check(startup.Shortcut.Arguments == "--tray --ini " + ManagedConfigurationStartup.QuoteArgument(profile), "Sign-in arguments must open configuration only and retain the selected profile.");
        Check(!startup.Shortcut.Arguments.Contains("--auto-fix") && !startup.Shortcut.Arguments.Contains("--yes"), "Sign-in arguments contain a repair action.");
        Check(ProfileRun("--tray") == 0 && trays == 1 && shownTray != null, "CLI tray did not dispatch to the shared host.");
        Check(shownTray.ProductName == "ChromeProfileCounter" && shownTray.IniPath == profile, "Tray uses a different profile or product identity.");
        byte[] original = File.ReadAllBytes(profile);
        startup.FailRemoval = true;
        Check(ProfileRun("--no-startup") != 0 && errors.Contains("Injected removal failure") && startup.Installed && original.SequenceEqual(File.ReadAllBytes(profile)), "Startup failure did not preserve exact INI bytes and launch state.");
        Check(ProfileRun("--no-startup", "--tray") == 0 && !startup.Installed && trays == 2, "Disable-plus-tray did not finish through the same shared surfaces.");
        var editor = new ManagedConfigurationEditor(shownTray);
        var values = editor.Values; values["RunAtStartup"] = "yes"; editor.Save(values);
        Check(startup.Installed && ManagedIniFile.LoadSection(ChromeProfileCounter.IniSpec(profile), false)["RunAtStartup"] == "1", "Tray boolean edit diverged from Startup CLI normalization.");
        File.WriteAllText(profile, "[Settings]\nUserDataDirectory=" + data + "\nBackupDirectory=Backups\nRunAtStartup=invalid\n");
        Check(ProfileRun("--no-startup") == 0 && !startup.Installed, "A malformed saved Startup value must remain repairable.");
        Check(engines == oldEngines && observations == oldObservations && !File.Exists(state), "Startup/tray interaction opened Chrome state or discovered browser processes.");
    }

    static void Operations()
    {
        Directory.CreateDirectory(data); Directory.CreateDirectory(Path.Combine(data, "Profile 1")); ResetState();
        byte[] original = File.ReadAllBytes(state);
        Check(ProfileRun("--status") == 0 && output.Contains("Current Chrome counter: 4") && output.Contains("Profile 1") && output.Contains("Profile 3"), "Status lost disk or registered profile information.");
        Check(ProfileRun("--first-free") == 0 && output.Trim() == "2", "First-free action does not combine disk and registration sets.");
        Check(original.SequenceEqual(File.ReadAllBytes(state)) && !Directory.Exists(Path.Combine(data, "Backups")), "Read-only counter inspection changed data or created backups.");
        Check(observations == 0, "Status/first-free performed process discovery.");
        Check(lastOptions.UserDataDirectory == data && lastOptions.BackupDirectory == Path.Combine(data, "Backups"), "Operational engine received paths inconsistent with the profile.");
        answers.Enqueue("NO");
        Check(ProfileRun("--set-counter", "8") == 0 && output.Contains("Cancelled") && original.SequenceEqual(File.ReadAllBytes(state)), "Repair cancellation did not preserve Local State.");
        answers.Enqueue("yes");
        Check(ProfileRun("--auto-fix") == 0 && output.Contains("Cancelled") && original.SequenceEqual(File.ReadAllBytes(state)), "Confirmation must match exact typed YES.");
        answers.Enqueue("YES");
        Check(ProfileRun("--set-counter", "8") == 0 && output.Contains("SUCCESS") && ChromeProfileCounterEngine.GetCounter(File.ReadAllText(state)) == 8, "Confirmed manual repair failed.");
        Check(Directory.GetFiles(Path.Combine(data, "Backups")).Any(path => File.ReadAllBytes(path).SequenceEqual(original)), "Repair did not preserve the exact original backup bytes.");
        ResetState();
        Check(ProfileRun("--auto-fix", "--yes") == 0 && ChromeProfileCounterEngine.GetCounter(File.ReadAllText(state)) == 2, "Explicit command-line confirmation did not repair to the first free number.");
        ResetState();
        Check(ProfileRun("--set-counter", "3", "--yes") != 0 && errors.Contains("already exists") && original.SequenceEqual(File.ReadAllBytes(state)), "Registered occupancy was not rejected before repair.");
        running = true;
        Check(ProfileRun("--set-counter", "8", "--yes") != 0 && errors.Contains("Chrome is currently running") && original.SequenceEqual(File.ReadAllBytes(state)), "A running-browser observation was ignored.");
        ResetState(); files = new VerifyFailureFiles();
        Check(ProfileRun("--set-counter", "8", "--yes") != 0 && errors.Contains("replacement completed, but verification failed") && errors.Contains("Backup:") && !output.Contains("SUCCESS"), "Post-publication verification failure must clearly retain the backup path and never report success.");
        ResetState();
        string opened = null; ChromeProfileCounter.OpenFolder = value => opened = value;
        foreach (string answer in new[] { "1", "4", "3", "8", "YES", "2", "YES", "Q" }) answers.Enqueue(answer);
        Check(ProfileRun() == 0 && opened == data && output.Contains("SUCCESS") && ChromeProfileCounterEngine.GetCounter(File.ReadAllText(state)) == 2, "Compatibility menu lost status/folder/manual/automatic operations.");
        Check(answers.Count == 0, "Compatibility menu did not consume the expected confirmation flow.");
        Check(Directory.GetFiles(data, ".Local State.*.tmp").Length == 0, "Operational commands leaked temporary files.");
    }

    [STAThread]
    static int Main()
    {
        var oldFactory = ChromeProfileCounter.EngineFactory;
        var oldObservation = ChromeProfileCounter.ObserveBrowser;
        var oldTray = ChromeProfileCounter.TrayRunner;
        var oldAnswers = ChromeProfileCounter.ReadAnswer;
        var oldFolder = ChromeProfileCounter.OpenFolder;
        var oldStartup = ManagedConfigurationStartup.PlatformFactory;
        string parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar);
        root = Path.Combine(parent, "AIProjects-ChromeApp-" + Guid.NewGuid().ToString("N"));
        profile = Path.Combine(root, "selected.ini"); data = Path.Combine(root, "User Data"); state = Path.Combine(data, "Local State");
        try
        {
            Directory.CreateDirectory(root);
            ChromeProfileCounter.ObserveBrowser = delegate { ++observations; return running; };
            ChromeProfileCounter.EngineFactory = delegate(ChromeProfileCounter.Options options)
            {
                ++engines; lastOptions = options;
                if (options.UserDataDirectory != data) throw new Exception("A test attempted access outside the fixture User Data directory.");
                return new ChromeProfileCounterEngine(options.UserDataDirectory, options.BackupDirectory, ChromeProfileCounter.ObserveBrowser, files);
            };
            ChromeProfileCounter.TrayRunner = delegate(ManagedConfigurationTraySpec spec) { ++trays; shownTray = spec; return 0; };
            ChromeProfileCounter.ReadAnswer = delegate { if (answers.Count == 0) throw new Exception("Unexpected interactive read."); return answers.Dequeue(); };
            ChromeProfileCounter.OpenFolder = delegate { throw new Exception("Unexpected folder launch."); };
            ManagedConfigurationStartup.PlatformFactory = () => startup;
            ReadOnlyAndConfiguration(); StartupAndTray(); Operations();
            Console.WriteLine("Passed " + checks + " ChromeProfileCounter app checks with fixture files and injected browser, Startup, tray, and folder operations.");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
        finally
        {
            ChromeProfileCounter.EngineFactory = oldFactory; ChromeProfileCounter.ObserveBrowser = oldObservation;
            ChromeProfileCounter.TrayRunner = oldTray; ChromeProfileCounter.ReadAnswer = oldAnswers;
            ChromeProfileCounter.OpenFolder = oldFolder; ManagedConfigurationStartup.PlatformFactory = oldStartup;
            if (Path.GetDirectoryName(Path.GetFullPath(root)) != parent || !Path.GetFileName(root).StartsWith("AIProjects-ChromeApp-", StringComparison.Ordinal))
                throw new IOException("Refusing cleanup outside the generated app fixture directory.");
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }
}

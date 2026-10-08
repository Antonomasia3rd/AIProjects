using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using AIProjects.Dependencies;

static class ChromeProfileCounter
{
    const string UserDataKey = "UserDataDirectory";
    const string BackupKey = "BackupDirectory";
    const string Section = "Settings";
    internal static Func<bool> ObserveBrowser = DiscoverBrowser;
    internal static Func<Options, ChromeProfileCounterEngine> EngineFactory = options =>
        new ChromeProfileCounterEngine(options.UserDataDirectory, options.BackupDirectory, ObserveBrowser);
    internal static Func<ManagedConfigurationTraySpec, int> TrayRunner = ManagedConfigurationTray.Run;
    internal static Func<string> ReadAnswer = Console.ReadLine;
    internal static Action<string> OpenFolder = path => Process.Start(new ProcessStartInfo("explorer.exe", ManagedConfigurationStartup.QuoteArgument(path)) { UseShellExecute = true });

    internal sealed class Options
    {
        public string IniPath;
        public string UserDataDirectory;
        public string BackupDirectory;
        public bool RunAtStartup;
    }

    sealed class Command
    {
        public string IniPath;
        public bool ConfigureOnly;
        public bool ShowConfig;
        public bool Tray;
        public bool Startup;
        public bool Yes;
        public string Action;
        public int Counter;
        public readonly Dictionary<string, string> Persistent = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public readonly Dictionary<string, string> Direct = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    }

    [STAThread]
    static int Main(string[] args)
    {
        // .NET Framework otherwise uses the console's legacy code page even
        // for redirected output, losing non-ASCII profile paths in pipelines.
        if (Console.IsOutputRedirected) Console.OutputEncoding = new UTF8Encoding(false);
        return Run(args);
    }

    internal static int Run(string[] args)
    {
        args = args ?? new string[0];
        if (args.Any(a => Is(a, "--help") || Is(a, "-h") || Is(a, "/?"))) { Usage(); return 0; }
        if (args.Any(a => Is(a, "--version") || Is(a, "-version"))) { Console.WriteLine("ChromeProfileCounter " + Version()); return 0; }
        try
        {
            Command command = Parse(args);
            string ini = ManagedProfile.ResolveIniPath(command.IniPath, AppDomain.CurrentDomain.BaseDirectory, "ChromeProfileCounter.ini");
            if (command.Startup)
            {
                var tray = BuildTraySpec(ini);
                tray.SaveSettings(command.Persistent);
                if (command.Tray) return TrayRunner(tray);
                Console.WriteLine("Saved Startup configuration: " + ini);
                return 0;
            }
            if (command.Tray) return TrayRunner(BuildTraySpec(ini));
            var changes = new Dictionary<string, string>(command.Persistent, StringComparer.OrdinalIgnoreCase);
            foreach (var entry in command.Direct)
                changes[entry.Key] = ManagedProfile.ResolvePath(Directory.GetCurrentDirectory(), Environment.ExpandEnvironmentVariables(entry.Value));
            Options options = ReadOptions(ini, changes);
            if (command.ConfigureOnly)
            {
                SaveProfile(ini, command.Persistent);
                options = ReadOptions(ini, new Dictionary<string, string>());
                if (command.ShowConfig) PrintOptions(options);
                Console.WriteLine("Saved configuration: " + ini);
                return 0;
            }
            if (command.ShowConfig) { PrintOptions(options); return 0; }
            // All information/configuration/Startup/tray paths returned above.
            // Creating a job engine is exclusive to explicit operational use.
            var engine = EngineFactory(options);
            switch (command.Action ?? "menu")
            {
                case "status": ShowStatus(engine); return 0;
                case "first-free": Console.WriteLine(engine.GetFirstSafeNumber(engine.ReadStateText()).ToString(CultureInfo.InvariantCulture)); return 0;
                case "set-counter": Repair(engine, command.Counter, false, command.Yes); return 0;
                case "auto-fix": Repair(engine, 0, true, command.Yes); return 0;
                default: return Menu(engine, options);
            }
        }
        catch (Exception ex) { Console.Error.WriteLine("ERROR: " + ex.Message); return 1; }
    }

    static bool DiscoverBrowser()
    {
        Process[] processes = Process.GetProcessesByName("chrome");
        try { return processes.Length != 0; }
        finally { foreach (Process process in processes) process.Dispose(); }
    }

    static bool Is(string value, string option) { return String.Equals(value, option, StringComparison.OrdinalIgnoreCase); }
    static string Value(string[] args, ref int index, string option)
    { if (++index == args.Length) throw new ArgumentException(option + " requires a value."); return args[index]; }
    static int PositiveCounter(string text)
    {
        int value;
        if (!Int32.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value) || value < 1)
            throw new ArgumentException("The counter must be an integer between 1 and " + Int32.MaxValue.ToString(CultureInfo.InvariantCulture) + ".");
        return value;
    }
    static void SelectAction(Command command, string action)
    { if (command.Action != null) throw new ArgumentException("Choose exactly one counter action."); command.Action = action; }

    static Command Parse(string[] args)
    {
        var command = new Command();
        for (int i = 0; i < args.Length; ++i)
        {
            string arg = args[i];
            if (Is(arg, "--ini") || Is(arg, "-IniFile")) command.IniPath = Value(args, ref i, arg);
            else if (Is(arg, "--set")) ManagedProfile.SetAssignment(command.Persistent, Value(args, ref i, arg), Section, CanonicalKey, Normalize);
            else if (Is(arg, "--user-data")) command.Direct[UserDataKey] = Normalize(UserDataKey, Value(args, ref i, arg));
            else if (Is(arg, "--backup-directory")) command.Direct[BackupKey] = Normalize(BackupKey, Value(args, ref i, arg));
            else if (Is(arg, "--configure-only")) command.ConfigureOnly = true;
            else if (Is(arg, "--show-config") || Is(arg, "--print-config")) command.ShowConfig = true;
            else if (Is(arg, "--tray")) command.Tray = true;
            else if (Is(arg, "--startup") || Is(arg, "--no-startup"))
            {
                if (command.Startup) throw new ArgumentException("Choose either --startup or --no-startup once.");
                command.Startup = true;
                command.Persistent[ManagedConfigurationStartup.Key] = Is(arg, "--startup") ? "1" : "0";
            }
            else if (Is(arg, "--yes")) command.Yes = true;
            else if (Is(arg, "--status")) SelectAction(command, "status");
            else if (Is(arg, "--first-free")) SelectAction(command, "first-free");
            else if (Is(arg, "--auto-fix")) SelectAction(command, "auto-fix");
            else if (Is(arg, "--set-counter")) { SelectAction(command, "set-counter"); command.Counter = PositiveCounter(Value(args, ref i, arg)); }
            else if (Is(arg, "--menu") || Is(arg, "--interactive")) SelectAction(command, "menu");
            else throw new ArgumentException("Unknown argument: " + arg);
        }
        bool passive = command.ConfigureOnly || command.ShowConfig || command.Tray || command.Startup;
        if (command.Action != null && passive) throw new ArgumentException("Counter actions cannot be combined with configuration, tray, or Startup commands.");
        if (command.Yes && (passive || (command.Action != "set-counter" && command.Action != "auto-fix")))
            throw new ArgumentException("--yes confirms only an explicit --set-counter or --auto-fix action.");
        if (command.Tray && (command.ConfigureOnly || command.ShowConfig || command.Direct.Count != 0 ||
            (command.Persistent.Count != 0 && !command.Startup)))
            throw new ArgumentException("--tray accepts --ini and optional Startup management, without job or offline configuration options.");
        if (command.Startup && (command.ConfigureOnly || command.ShowConfig || command.Direct.Count != 0))
            throw new ArgumentException("Startup management cannot be combined with offline inspection/configuration or one-run path overrides.");
        if (command.Persistent.ContainsKey(ManagedConfigurationStartup.Key) && !command.Startup)
            throw new ArgumentException("Use --startup or --no-startup to change Startup; --configure-only never changes Startup integration.");
        if (command.ConfigureOnly && command.Direct.Count != 0)
            throw new ArgumentException("Use persistent --set values with --configure-only, not one-run path overrides.");
        if (command.Persistent.Count != 0 && !command.ConfigureOnly && !command.Startup)
            throw new ArgumentException("--set requires --configure-only or explicit Startup management.");
        return command;
    }

    static string CanonicalKey(string name)
    {
        foreach (string key in new[] { UserDataKey, BackupKey, ManagedConfigurationStartup.Key })
            if (Is((name ?? "").Trim(), key)) return key;
        return null;
    }
    static string Normalize(string key, string value)
    {
        if (Is(key, ManagedConfigurationStartup.Key)) return ManagedConfigurationStartup.NormalizeValue(value);
        if (String.IsNullOrWhiteSpace(value) || value.IndexOfAny(new[] { '\0', '\r', '\n' }) >= 0)
            throw new ArgumentException(key + " must be a nonempty single-line path.");
        ManagedProfile.ValidatePath(Environment.ExpandEnvironmentVariables(value), key);
        return value;
    }
    static Dictionary<string, string> Defaults()
    {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            { UserDataKey, "%LOCALAPPDATA%\\Google\\Chrome\\User Data" },
            { BackupKey, "Profile Counter Backups" }, { ManagedConfigurationStartup.Key, "0" }
        };
    }
    internal static ManagedIniFileSpec IniSpec(string path)
    {
        return new ManagedIniFileSpec { FilePath = path, SectionName = Section,
            DefaultContents = "[Settings]\r\nUserDataDirectory=%LOCALAPPDATA%\\Google\\Chrome\\User Data\r\nBackupDirectory=Profile Counter Backups\r\nRunAtStartup=0\r\n" };
    }
    static Options Resolve(string ini, IDictionary<string, string> stored, IDictionary<string, string> changes)
    {
        var values = ManagedProfile.ResolveSettings(Defaults(), stored, changes, CanonicalKey, Normalize);
        string userData = ManagedProfile.ResolvePath(Path.GetDirectoryName(ini), Environment.ExpandEnvironmentVariables(values[UserDataKey]));
        var options = new Options { IniPath = ini, UserDataDirectory = userData,
            BackupDirectory = ManagedProfile.ResolvePath(userData, Environment.ExpandEnvironmentVariables(values[BackupKey])),
            RunAtStartup = values[ManagedConfigurationStartup.Key] == "1" };
        string state = Path.Combine(userData, "Local State");
        if (Is(ini, state) || Is(options.BackupDirectory, state) || Is(ini, options.BackupDirectory))
            throw new ArgumentException("The INI, Chrome Local State file, and backup directory must use different paths.");
        return options;
    }
    internal static Options ReadOptions(string ini, IDictionary<string, string> changes)
    { return Resolve(ini, ManagedIniFile.LoadSection(IniSpec(ini), false), changes); }
    static void SaveProfile(string ini, IDictionary<string, string> changes)
    {
        string error;
        if (!ManagedIniFile.SaveSectionBatch(IniSpec(ini), changes,
            prospective => Resolve(ini, prospective, new Dictionary<string, string>()), out error))
            throw new IOException("Could not save ChromeProfileCounter configuration: " + error);
    }
    internal static ManagedConfigurationTraySpec BuildTraySpec(string ini)
    {
        var spec = new ManagedConfigurationTraySpec { ProductName = "ChromeProfileCounter", Version = Version(), IniPath = ini,
            ReadSettings = () => ManagedConfigurationTray.ReadForEditing(Defaults(), IniSpec(ini)) };
        spec.Startup = new ManagedConfigurationStartup(IniSpec(ini),
            ManagedConfigurationStartup.BuildShortcut(spec.ProductName, Assembly.GetExecutingAssembly().Location, ini),
            spec.ReadSettings, changes => SaveProfile(ini, changes), changes => ReadOptions(ini, changes));
        spec.SaveSettings = spec.Startup.Save;
        return spec;
    }
    static void PrintOptions(Options options)
    {
        Console.WriteLine("Configuration: " + options.IniPath);
        Console.WriteLine(UserDataKey + " = " + options.UserDataDirectory);
        Console.WriteLine(BackupKey + " = " + options.BackupDirectory);
        Console.WriteLine(ManagedConfigurationStartup.Key + " = " + (options.RunAtStartup ? "1" : "0"));
    }

    static void ShowStatus(ChromeProfileCounterEngine engine)
    {
        string text = engine.ReadStateText();
        Console.WriteLine("User Data: " + engine.UserDataDirectory);
        Console.WriteLine("Current Chrome counter: " + ChromeProfileCounterEngine.GetCounter(text).ToString(CultureInfo.InvariantCulture));
        Console.WriteLine("First safe free number: " + engine.GetFirstSafeNumber(text).ToString(CultureInfo.InvariantCulture));
        var disk = engine.GetDiskProfiles();
        var registered = ChromeProfileCounterEngine.GetRegisteredProfiles(text);
        Console.WriteLine("PROFILE      ON DISK   REGISTERED");
        foreach (int number in disk.Union(registered).OrderBy(number => number))
            Console.WriteLine("{0,-12} {1,-9} {2}", "Profile " + number.ToString(CultureInfo.InvariantCulture), disk.Contains(number) ? "Yes" : "-", registered.Contains(number) ? "Yes" : "-");
    }
    static void Repair(ChromeProfileCounterEngine engine, int number, bool automatic, bool confirmed)
    {
        if (ObserveBrowser()) throw new IOException("Chrome is currently running. Close all Chrome windows before editing the counter.");
        string reviewed = engine.ReadStateText();
        int previous = ChromeProfileCounterEngine.GetCounter(reviewed);
        if (automatic) number = engine.GetFirstSafeNumber(reviewed);
        if (engine.GetDiskProfiles().Contains(number) || ChromeProfileCounterEngine.GetRegisteredProfiles(reviewed).Contains(number))
            throw new IOException("Profile " + number.ToString(CultureInfo.InvariantCulture) + " already exists on disk or is registered in Chrome.");
        Console.WriteLine("User Data: " + engine.UserDataDirectory);
        Console.WriteLine("Current counter: " + previous.ToString(CultureInfo.InvariantCulture));
        Console.WriteLine("New counter: " + number.ToString(CultureInfo.InvariantCulture));
        Console.WriteLine("Next profile: Profile " + number.ToString(CultureInfo.InvariantCulture));
        if (!confirmed)
        {
            Console.Write("Type YES to apply: ");
            if (!String.Equals(ReadAnswer(), "YES", StringComparison.Ordinal)) { Console.WriteLine("Cancelled; Local State was left unchanged."); return; }
        }
        string backup = engine.ChangeCounter(reviewed, number);
        Console.WriteLine("Backup: " + backup);
        try
        {
            int actual = ChromeProfileCounterEngine.GetCounter(engine.ReadStateText());
            if (actual != number) throw new IOException("Expected " + number.ToString(CultureInfo.InvariantCulture) + ", found " + actual.ToString(CultureInfo.InvariantCulture) + ".");
        }
        catch (Exception ex) { throw new IOException("The replacement completed, but verification failed. Backup: " + backup + ". " + ex.Message, ex); }
        Console.WriteLine("SUCCESS. profiles_created = " + number.ToString(CultureInfo.InvariantCulture));
    }
    static int Menu(ChromeProfileCounterEngine engine, Options options)
    {
        while (true)
        {
            Console.WriteLine("\nCHROME PROFILE COUNTER TOOL\n[1] Show profile status\n[2] Auto-fix counter to first free number\n[3] Set counter manually\n[4] Open Chrome User Data folder\n[Q] Quit");
            Console.Write("Choose: ");
            string choice = ReadAnswer();
            if (choice == null || Is(choice, "Q")) return 0;
            try
            {
                switch (choice)
                {
                    case "1": ShowStatus(engine); break;
                    case "2": Repair(engine, 0, true, false); break;
                    case "3": Console.Write("Enter desired next Profile number: "); Repair(engine, PositiveCounter(ReadAnswer()), false, false); break;
                    case "4": OpenFolder(options.UserDataDirectory); break;
                    default: Console.WriteLine("Unknown option."); break;
                }
            }
            catch (Exception ex) { Console.Error.WriteLine("ERROR: " + ex.Message); }
        }
    }
    static string Version() { return Assembly.GetExecutingAssembly().GetName().Version.ToString(); }
    static void Usage()
    {
        Console.WriteLine("ChromeProfileCounter " + Version());
        Console.WriteLine("No action opens the interactive menu. Counter changes require typed YES, or explicit --yes.");
        Console.WriteLine("  --ini <file>                         select a profile (relative to the working directory)");
        Console.WriteLine("  --show-config | --print-config        inspect effective settings without creating files");
        Console.WriteLine("  --configure-only --set Settings.Key=Value  save settings without reading Chrome data");
        Console.WriteLine("  --tray                               open the shared configuration tray; no repair runs");
        Console.WriteLine("  --startup | --no-startup              per-user Startup folder; sign-in opens --tray --ini");
        Console.WriteLine("  --status | --first-free               inspect Chrome state without modifying it");
        Console.WriteLine("  --set-counter <integer> | --auto-fix   review and repair; --yes explicitly confirms this action");
        Console.WriteLine("  --user-data <directory> --backup-directory <directory>  one-run paths, relative to the working directory");
        Console.WriteLine("  --menu | --interactive                status/auto-fix/manual/folder/quit menu");
        Console.WriteLine("Settings: UserDataDirectory, BackupDirectory, RunAtStartup. Relative UserDataDirectory uses the INI folder; relative BackupDirectory uses UserDataDirectory. Environment variables are expanded in paths.");
        Console.WriteLine("Keep Chrome closed throughout a repair. Backups contain the exact replaced Local State file; output is UTF-8 without BOM. Chrome does not acquire this tool's mutex, so concurrent external edits remain possible.");
    }
}

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;
using AIProjects.Dependencies;

static class YouTubeMusicMigrate
{
    internal sealed class WorkerRequest
    {
        public int Version = 1;
        public string IniPath;
        public string DataRoot;
        public string ProfileRoot;
        public string HostPath;
        public string Intent;
        public Dictionary<string, object> Values;
        public string[] Explicit;
        public string[] Switches;
        public Dictionary<string, string> IdentityBaseline;
    }
    internal static Func<string> DefaultDataRoot = FindDataRoot;
    internal static Func<WorkerRequest, int> WorkerRunner = RunPowerShellWorker;
    internal static Func<ManagedConfigurationTraySpec, int> TrayRunner = ManagedConfigurationTray.Run;
    static JavaScriptSerializer Json() { return new JavaScriptSerializer { MaxJsonLength = 1024 * 1024 }; }
    static Dictionary<string, string> Empty() { return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase); }
    static bool Is(string a, string b) { return String.Equals(a, b, StringComparison.OrdinalIgnoreCase); }
    static string FindDataRoot()
    {
        string directory = AppDomain.CurrentDomain.BaseDirectory;
        string parent = Path.GetDirectoryName(directory.TrimEnd(Path.DirectorySeparatorChar));
        return parent != null && File.Exists(Path.Combine(parent, "youtube_music_tidy.ps1")) ? parent : directory;
    }
    static string IniPath(string supplied, string root)
    { return ManagedProfile.ResolveIniPath(supplied, root, "YouTubeMusicMigrate.ini"); }
    static string Take(string[] args, ref int index)
    { if (++index == args.Length) throw new ArgumentException("Missing option value."); return args[index]; }
    [STAThread]
    static int Main(string[] args)
    {
        if (Console.IsOutputRedirected) Console.OutputEncoding = new UTF8Encoding(false);
        return Run(args);
    }
    internal static int Run(string[] args)
    {
        args = args ?? new string[0];
        if (args.Any(a => Is(a, "--help") || Is(a, "-h"))) { Help(); return 0; }
        if (args.Any(a => Is(a, "--version"))) { Console.WriteLine("YouTubeMusicMigrate 1.0"); return 0; }
        try
        {
            if (args.Length == 2 && Is(args[0], "--legacy-request")) return RunLegacy(args[1]);
            if (args.Length == 2 && Is(args[0], "--persist-worker-preferences")) { PersistIdentityRequest(args[1]); return 0; }
            string suppliedIni = null, intent = null;
            bool configure = false, show = false, tray = false, startup = false;
            var changes = Empty(); var overrides = Empty();
            for (int i = 0; i < args.Length; ++i)
            {
                string arg = args[i];
                if (Is(arg, "--ini")) suppliedIni = Take(args, ref i);
                else if (Is(arg, "--set")) ManagedProfile.SetAssignment(changes, Take(args, ref i), "Settings", YouTubeMusicProfile.Canonical, YouTubeMusicProfile.Normalize);
                else if (Is(arg, "--arg"))
                {
                    ManagedProfile.SetAssignment(overrides, Take(args, ref i), "Settings", name => {
                        string key = YouTubeMusicProfile.Canonical(name);
                        return YouTubeMusicProfile.Parameters.Any(p => p.Name == key) ? key : null;
                    }, YouTubeMusicProfile.Normalize);
                }
                else if (Is(arg, "--configure-only")) configure = true;
                else if (Is(arg, "--show-config") || Is(arg, "--print-config")) show = true;
                else if (Is(arg, "--tray")) tray = true;
                else if (Is(arg, "--startup") || Is(arg, "--no-startup"))
                {
                    if (startup) throw new ArgumentException("Choose one Startup command.");
                    startup = true; changes["RunAtStartup"] = Is(arg, "--startup") ? "1" : "0";
                }
                else if (Is(arg, "--run") || Is(arg, "--menu"))
                {
                    if (intent != null) throw new ArgumentException("Choose one worker launch mode.");
                    intent = Is(arg, "--menu") ? "menu" : "run";
                }
                else throw new ArgumentException("Unknown argument: " + arg);
            }
            if (intent != null && (configure || show || tray || startup || changes.Count != 0))
                throw new ArgumentException("Worker execution cannot be combined with configuration, tray or Startup changes.");
            if (overrides.Count != 0 && intent == null) throw new ArgumentException("--arg requires explicit --run or --menu.");
            if (tray && (configure || show || (changes.Count != 0 && !startup))) throw new ArgumentException("Tray mode accepts --ini and optional Startup management.");
            if (startup && (configure || show)) throw new ArgumentException("Startup management is separate from offline configuration/inspection.");
            if (changes.ContainsKey("RunAtStartup") && !startup) throw new ArgumentException("Use --startup or --no-startup; offline configuration never changes Startup integration.");
            if (changes.Count != 0 && !configure && !startup) throw new ArgumentException("--set requires --configure-only or explicit Startup management.");
            string root = DefaultDataRoot(); string ini = IniPath(suppliedIni, root);
            if (startup)
            {
                var spec = TraySpec(ini, root); spec.SaveSettings(changes);
                if (tray) return TrayRunner(spec);
                Console.WriteLine("Saved Startup configuration: " + ini); return 0;
            }
            if (configure) { YouTubeMusicProfile.Save(ini, root, changes); Console.WriteLine("Saved preferences: " + ini); }
            if (show)
            {
                Console.WriteLine("Configuration: " + ini);
                foreach (var entry in YouTubeMusicProfile.Read(ini, root, Empty()).OrderBy(p => p.Key)) Console.WriteLine(entry.Key + " = " + entry.Value);
                return 0;
            }
            if (configure) return 0;
            if (intent == null) return TrayRunner(TraySpec(ini, root));
            return Launch(ini, root, overrides, overrides.Keys.ToArray(), intent);
        }
        catch (Exception ex) { Console.Error.WriteLine("ERROR: " + ex.Message); return 1; }
    }
    internal static ManagedConfigurationTraySpec TraySpec(string ini, string root)
    {
        var spec = new ManagedConfigurationTraySpec { ProductName = "YouTubeMusicMigrate", Version = "1.0", IniPath = ini,
            ReadSettings = () => ManagedConfigurationTray.ReadForEditing(YouTubeMusicProfile.Defaults(root), YouTubeMusicProfile.Spec(ini)) };
        spec.Startup = new ManagedConfigurationStartup(YouTubeMusicProfile.Spec(ini),
            ManagedConfigurationStartup.BuildShortcut(spec.ProductName, Assembly.GetExecutingAssembly().Location, ini),
            spec.ReadSettings, changes => YouTubeMusicProfile.Save(ini, root, changes), changes => YouTubeMusicProfile.Read(ini, root, changes));
        spec.SaveSettings = spec.Startup.Save; return spec;
    }
    static Dictionary<string, object> ReadRequest(string path)
    {
        var file = new FileInfo(path);
        if (file.Length > 1024 * 1024) throw new InvalidDataException("The worker request is too large.");
        var result = Json().DeserializeObject(File.ReadAllText(path, Encoding.UTF8)) as Dictionary<string, object>;
        if (result == null) throw new InvalidDataException("Expected a worker request object.");
        return result;
    }
    static string Required(Dictionary<string, object> request, string key)
    {
        object value;
        if (!request.TryGetValue(key, out value) || !(value is string)) throw new InvalidDataException("Missing request string: " + key);
        return (string)value;
    }
    static Dictionary<string, object> Object(Dictionary<string, object> request, string key)
    {
        object value;
        if (!request.TryGetValue(key, out value) || !(value is Dictionary<string, object>)) throw new InvalidDataException("Missing request object: " + key);
        return (Dictionary<string, object>)value;
    }
    static int RunLegacy(string path)
    {
        var request = ReadRequest(path);
        string root = Path.GetFullPath(Required(request, "DataRoot"));
        string ini = IniPath(Required(request, "IniPath"), root);
        var overrides = Empty();
        foreach (var entry in Object(request, "Arguments"))
        {
            string key = YouTubeMusicProfile.Canonical(entry.Key);
            var parameter = YouTubeMusicProfile.Parameters.FirstOrDefault(p => p.Name == key);
            if (parameter == null) throw new ArgumentException("Unknown legacy parameter: " + entry.Key);
            string value = parameter.Type == YouTubeMusicProfile.Kind.List ? Json().Serialize(entry.Value) : Convert.ToString(entry.Value, CultureInfo.InvariantCulture);
            value = YouTubeMusicProfile.Normalize(key, value);
            if (parameter.Type == YouTubeMusicProfile.Kind.Path) value = Path.GetFullPath(value);
            overrides[key] = value;
        }
        return Launch(ini, root, overrides, overrides.Keys.ToArray(), "legacy");
    }
    internal static int Launch(string ini, string root, Dictionary<string, string> overrides, string[] explicitKeys, string intent)
    {
        var preliminary = YouTubeMusicProfile.Read(ini, root, overrides);
        if (intent == "menu" && preliminary["NoInteractive"] == "1")
            throw new ArgumentException("--menu conflicts with NoInteractive=1. Override NoInteractive=0 explicitly to open the menu.");
        // Import only as part of an explicit worker launch. Passive commands
        // never open account-state JSON or browser authentication files.
        ImportLegacyIdentity(ini, root, overrides);
        var saved = YouTubeMusicProfile.Read(ini, root, Empty());
        var values = YouTubeMusicProfile.Read(ini, root, overrides);
        var request = new WorkerRequest { IniPath = ini, ProfileRoot = root,
            DataRoot = ManagedProfile.ResolvePath(Path.GetDirectoryName(ini), Environment.ExpandEnvironmentVariables(values["DataRoot"])),
            HostPath = Assembly.GetExecutingAssembly().Location, Intent = intent,
            Values = YouTubeMusicProfile.Typed(ini, values), Explicit = explicitKeys,
            Switches = YouTubeMusicProfile.Parameters.Where(p => p.Type == YouTubeMusicProfile.Kind.Switch).Select(p => p.Name).ToArray(),
            IdentityBaseline = new[] { "ExpectedAccountName", "ExpectedChannelHandle", "DataRoot", "ConfigPath" }.ToDictionary(key => key, key => saved[key]) };
        return WorkerRunner(request);
    }
    static void ImportLegacyIdentity(string ini, string root, Dictionary<string, string> overrides)
    {
        using (ManagedIniFile.BeginTransaction(YouTubeMusicProfile.Spec(ini)))
        {
            var saved = YouTubeMusicProfile.Read(ini, root, Empty());
            if (saved["ImportLegacyIdentity"] != "1") return;
            string path = (string)YouTubeMusicProfile.Typed(ini, YouTubeMusicProfile.Read(ini, root, overrides))["ConfigPath"];
            var changes = Empty(); changes["ImportLegacyIdentity"] = "0";
            if (File.Exists(path))
            {
                var state = ReadRequest(path);
                foreach (string key in YouTubeMusicProfile.IdentityKeys)
                {
                    object value;
                    if (saved[key].Length == 0 && state.TryGetValue(key, out value) && value is string)
                        changes[key] = YouTubeMusicProfile.Normalize(key, (string)value);
                }
            }
            YouTubeMusicProfile.Save(ini, root, changes);
        }
    }
    internal static void PersistIdentityRequest(string path)
    {
        var request = ReadRequest(path);
        string ini = Path.GetFullPath(Required(request, "IniPath"));
        string root = Path.GetFullPath(Required(request, "DataRoot"));
        var baseline = Object(request, "Baseline"); var desired = Object(request, "Values");
        if (baseline.Count != 4 || desired.Count != 2) throw new ArgumentException("The identity baseline must include its original data/config paths; only the two identity preferences may be changed.");
        using (ManagedIniFile.BeginTransaction(YouTubeMusicProfile.Spec(ini)))
        {
            var current = YouTubeMusicProfile.Read(ini, root, Empty()); var changes = Empty();
            foreach (string key in new[] { "ExpectedAccountName", "ExpectedChannelHandle", "DataRoot", "ConfigPath" })
            {
                if (!String.Equals(current[key], Required(baseline, key), StringComparison.Ordinal))
                    throw new IOException("Account preferences changed while the worker was running. Reload configuration and retry setup; concurrent edits were preserved.");
                if (YouTubeMusicProfile.IdentityKeys.Contains(key)) changes[key] = YouTubeMusicProfile.Normalize(key, Required(desired, key));
            }
            changes["ImportLegacyIdentity"] = "0";
            YouTubeMusicProfile.Save(ini, root, changes);
        }
    }
    static int RunPowerShellWorker(WorkerRequest request)
    {
        string bridge = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "youtube_music_worker_bridge.ps1");
        if (!File.Exists(bridge)) throw new FileNotFoundException("The packaged PowerShell worker bridge is missing.", bridge);
        string path = Path.Combine(Path.GetTempPath(), "AIProjects-YtmRequest-" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            File.WriteAllText(path, Json().Serialize(request), new UTF8Encoding(false));
            var info = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell\\v1.0\\powershell.exe"),
                "-NoProfile -File " + ManagedConfigurationStartup.QuoteArgument(bridge) + " -RequestPath " + ManagedConfigurationStartup.QuoteArgument(path));
            info.UseShellExecute = false; info.WorkingDirectory = Directory.GetCurrentDirectory();
            using (var process = Process.Start(info))
            { if (process == null) throw new IOException("Could not start the requested worker."); process.WaitForExit(); return process.ExitCode; }
        }
        finally { if (File.Exists(path)) File.Delete(path); }
    }
    static void Help()
    {
        Console.WriteLine("YouTubeMusicMigrate 1.0 - no action opens configuration only");
        Console.WriteLine("--ini <path> --show-config | --configure-only --set Settings.Key=Value | --tray");
        Console.WriteLine("--startup | --no-startup: current-user Startup folder, opening --tray --ini only");
        Console.WriteLine("--run | --menu [--arg Parameter=Value]: explicitly launch the existing PowerShell worker");
        Console.WriteLine("Every legacy parameter is editable in Settings. Arrays use JSON such as [\"id1\",\"id2\"]. Paths use DataRoot; relative DataRoot uses the INI folder. --arg overrides one run.");
        Console.WriteLine("Account-changing worker actions retain their YES prompts. No job runs from Startup or configuration. Authentication headers remain plaintext; keep the data folder private.");
    }
}

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;
using AIProjects.Dependencies;

static class YouTubeMusicFrontendTests
{
    static int checks, jobs, trays;
    static string root, ini;
    static YouTubeMusicMigrate.WorkerRequest observed;
    static void Check(bool ok, string message) { ++checks; if (!ok) throw new Exception(message); }
    static int Run(params string[] args) { return YouTubeMusicMigrate.Run(new[] { "--ini", ini }.Concat(args).ToArray()); }
    static string Request(string name, object value)
    {
        string path = Path.Combine(root, name + ".json");
        File.WriteAllText(path, new JavaScriptSerializer().Serialize(value), new UTF8Encoding(false)); return path;
    }
    sealed class Startup : IManagedConfigurationStartupPlatform
    {
        public bool Installed;
        public ManagedStartupShortcutSpec Spec;
        public bool IsInstalled(ManagedStartupShortcutSpec spec, out string error) { error = null; return Installed; }
        public bool Commit(ManagedStartupShortcutSpec spec, bool desired, Func<IDisposable> scope,
            ManagedStartupPersistenceSnapshot capture, ManagedStartupPersistenceMutation persist, ManagedStartupPersistenceMutation restore, out string error)
        {
            Spec = spec;
            using (scope())
            {
                bool previous;
                if (!capture(out previous, out error) || !persist(out error)) return false;
                Installed = desired; return true;
            }
        }
    }
    [STAThread]
    static int Main()
    {
        string parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar);
        root = Path.Combine(parent, "AIProjects-YtmFrontend-" + Guid.NewGuid().ToString("N"));
        ini = Path.Combine(root, "profile.ini");
        var oldRoot = YouTubeMusicMigrate.DefaultDataRoot; var oldRunner = YouTubeMusicMigrate.WorkerRunner;
        var oldTray = YouTubeMusicMigrate.TrayRunner; var oldStartup = ManagedConfigurationStartup.PlatformFactory;
        try
        {
            Directory.CreateDirectory(root);
            YouTubeMusicMigrate.DefaultDataRoot = () => root;
            YouTubeMusicMigrate.WorkerRunner = request => { ++jobs; observed = request; return 17; };
            YouTubeMusicMigrate.TrayRunner = spec => { ++trays; Check(spec.ReadSettings().Count == 50, "Tray must expose all 47 existing parameters and three application settings."); return 0; };
            var startup = new Startup(); ManagedConfigurationStartup.PlatformFactory = () => startup;
            Check(YouTubeMusicProfile.Parameters.Length == 47, "Legacy parameter catalog changed size.");
            Check(YouTubeMusicMigrate.Run(new[] { "--help", "--unknown" }) == 0 && jobs == 0, "Help reached worker code.");
            Check(Run("--show-config") == 0 && !File.Exists(ini) && jobs == 0, "Inspection created an INI or launched a worker.");
            Check(Run() == 0 && trays == 1 && jobs == 0, "Default launch must open configuration only.");
            Check(Run("--configure-only", "--set", "BatchSize=3", "--set", "BatchDelaySeconds=0.25", "--set", "AdditionalNonMusicPlaylistIds=[\"a\",\"b\"]") == 0 && jobs == 0, "Preferences could not be saved without a worker.");
            byte[] previous = File.ReadAllBytes(ini);
            foreach (string[] bad in new[] {
                new[] { "--run", "--tray" }, new[] { "--menu", "--startup" }, new[] { "--run", "--show-config" },
                new[] { "--menu", "--arg", "NoInteractive=1" },
                new[] { "--arg", "FullScan=1" }, new[] { "--configure-only", "--set", "RunAtStartup=1" },
                new[] { "--configure-only", "--set", "BatchSize=51" }, new[] { "--configure-only", "--set", "BatchDelaySeconds=NaN" },
                new[] { "--configure-only", "--set", "ReportPath=raw_headers.txt" },
                new[] { "--configure-only", "--set", "ConfigPath=raw_headers.txt" },
                new[] { "--configure-only", "--set", "ReportPath=reports/../youtube_music_tidy_cache.json" },
                new[] { "--configure-only", "--set", "AdditionalNonMusicPlaylistIds=[1]" } })
                Check(Run(bad) != 0 && previous.SequenceEqual(File.ReadAllBytes(ini)) && jobs == 0, "Rejected command changed preferences or ran a worker.");
            Check(Run("--startup") == 0 && startup.Installed && startup.Spec.Arguments == "--tray --ini " + ManagedConfigurationStartup.QuoteArgument(ini) && jobs == 0, "Startup did not target configuration-only mode.");
            Check(Run("--no-startup") == 0 && !startup.Installed && jobs == 0, "Startup disable launched a worker.");
            File.WriteAllText(Path.Combine(root, "youtube_music_tidy_config.json"), "{\"ExpectedAccountName\":\"legacy fixture\",\"ExpectedChannelHandle\":\"@fixture\",\"SetupCompletedAt\":\"fixture-time\"}");
            Check(Run("--run") == 17 && jobs == 1 && observed.Intent == "run" && observed.Explicit.Length == 0, "Defaults became explicit or worker exit code was lost.");
            Check(observed.Values.Count == 47 && observed.Values["ExpectedAccountName"].Equals("legacy fixture"), "Legacy saved identity was not imported on explicit launch.");
            var saved = ManagedIniFile.LoadSection(YouTubeMusicProfile.Spec(ini), false);
            Check(saved["ExpectedAccountName"] == "legacy fixture" && saved["ImportLegacyIdentity"] == "0", "Imported identity is not owned by INI.");
            foreach (var parameter in YouTubeMusicProfile.Parameters)
            {
                object value = observed.Values[parameter.Name];
                Type expected = parameter.Type == YouTubeMusicProfile.Kind.Integer ? typeof(int) :
                    parameter.Type == YouTubeMusicProfile.Kind.Number ? typeof(double) :
                    parameter.Type == YouTubeMusicProfile.Kind.Switch ? typeof(bool) :
                    parameter.Type == YouTubeMusicProfile.Kind.List ? typeof(object[]) : typeof(string);
                Check(value.GetType() == expected, "Typed worker parameter changed: " + parameter.Name);
                if (parameter.Type == YouTubeMusicProfile.Kind.Path) Check(((string)value).StartsWith(root, StringComparison.Ordinal), "Default data path escaped its supplied root.");
            }
            Check(Run("--menu", "--arg", "ReportOnly=false", "--arg", "ExpectedChannelHandle=") == 17 &&
                observed.Intent == "menu" && observed.Explicit.OrderBy(x => x).SequenceEqual(new[] { "ExpectedChannelHandle", "ReportOnly" }) &&
                observed.Values["ReportOnly"].Equals(false) && observed.Values["ExpectedChannelHandle"].Equals(""), "Explicit false/empty settings or menu intent was lost.");
            string legacy = Request("legacy", new { DataRoot = root, IniPath = ini, Arguments = new Dictionary<string, object> {
                { "HeadCheckCount", 0 }, { "ReportOnly", false }, { "BatchDelaySeconds", 0.5 },
                { "AdditionalNonMusicPlaylistIds", new[] { "x", "y" } }, { "ExpectedAccountName", "" } } });
            Check(YouTubeMusicMigrate.Run(new[] { "--legacy-request", legacy }) == 17 && observed.Intent == "legacy" && observed.Explicit.Length == 5 &&
                observed.Values["HeadCheckCount"].Equals(0) && observed.Values["ExpectedAccountName"].Equals(""), "Legacy typed request/explicitness was changed.");
            int before = jobs;
            var identity = new Dictionary<string, string> { { "ExpectedAccountName", "new fixture" }, { "ExpectedChannelHandle", "@new" } };
            string update = Request("identity", new { IniPath = ini, DataRoot = root, Baseline = observed.IdentityBaseline, Values = identity });
            Check(YouTubeMusicMigrate.Run(new[] { "--persist-worker-preferences", update }) == 0 && jobs == before &&
                ManagedIniFile.LoadSection(YouTubeMusicProfile.Spec(ini), false)["ExpectedAccountName"] == "new fixture", "Worker setup did not use the shared INI writer.");
            byte[] fresh = File.ReadAllBytes(ini);
            Check(YouTubeMusicMigrate.Run(new[] { "--persist-worker-preferences", update }) != 0 && fresh.SequenceEqual(File.ReadAllBytes(ini)), "Stale worker identity overwrote a newer configuration.");
            var editor = new ManagedConfigurationEditor(YouTubeMusicMigrate.TraySpec(ini, root));
            var edited = editor.Values; edited["BatchSize"] = "7"; editor.Save(edited);
            Check(ManagedIniFile.LoadSection(YouTubeMusicProfile.Spec(ini), false)["BatchSize"] == "7" && jobs == before, "Tray configuration diverged from shared CLI validation or ran a job.");
            Console.WriteLine("Passed " + checks + " YouTube frontend checks; worker, Startup and tray operations were injected fixtures."); return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
        finally
        {
            YouTubeMusicMigrate.DefaultDataRoot = oldRoot; YouTubeMusicMigrate.WorkerRunner = oldRunner;
            YouTubeMusicMigrate.TrayRunner = oldTray; ManagedConfigurationStartup.PlatformFactory = oldStartup;
            if (Path.GetDirectoryName(Path.GetFullPath(root)) != parent || !Path.GetFileName(root).StartsWith("AIProjects-YtmFrontend-", StringComparison.Ordinal)) throw new IOException("Unsafe fixture cleanup path.");
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }
}

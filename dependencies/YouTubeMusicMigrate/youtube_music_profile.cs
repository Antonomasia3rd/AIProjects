using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Web.Script.Serialization;
using AIProjects.Dependencies;

static class YouTubeMusicProfile
{
    internal enum Kind { Path, Text, Switch, Integer, Number, List }
    internal sealed class Parameter
    {
        public string Name;
        public Kind Type;
        public string Default;
        public Parameter(string name, Kind type, string value) { Name = name; Type = type; Default = value; }
    }
    internal static readonly Parameter[] Parameters = {
        new Parameter("MigrationCsvPath", Kind.Path, "youtube_music_migration.csv"),
        new Parameter("AutoAssignmentsPath", Kind.Path, "youtube_music_auto_assignments.csv"),
        new Parameter("StatePath", Kind.Path, "youtube_music_migration_state.json"),
        new Parameter("HeadersPath", Kind.Path, "raw_headers.txt"),
        new Parameter("CachePath", Kind.Path, "youtube_music_tidy_cache.json"),
        new Parameter("ReportPath", Kind.Path, "reports/youtube_music_tidy_report.csv"),
        new Parameter("ActionsPath", Kind.Path, "reports/youtube_music_tidy_actions.csv"),
        new Parameter("ExpectedAccountName", Kind.Text, ""),
        new Parameter("ExpectedChannelHandle", Kind.Text, ""),
        new Parameter("ConfigPath", Kind.Path, "youtube_music_tidy_config.json"),
        new Parameter("Setup", Kind.Switch, "0"), new Parameter("NoInteractive", Kind.Switch, "0"),
        new Parameter("HeadCheckCount", Kind.Integer, "5"), new Parameter("BatchSize", Kind.Integer, "20"),
        new Parameter("BatchDelaySeconds", Kind.Number, "2.0"), new Parameter("MaxRetries", Kind.Integer, "4"),
        new Parameter("RetryBaseSeconds", Kind.Integer, "2"), new Parameter("FullScan", Kind.Switch, "0"),
        new Parameter("ReportOnly", Kind.Switch, "0"), new Parameter("ManagedPlaylistsOnly", Kind.Switch, "0"),
        new Parameter("NormalizeManagedPlaylistSettings", Kind.Switch, "0"),
        new Parameter("PlaylistSettingsReportPath", Kind.Path, "reports/youtube_music_tidy_playlist_settings.csv"),
        new Parameter("SuggestTasteLanes", Kind.Switch, "0"), new Parameter("TasteLaneCount", Kind.Integer, "8"),
        new Parameter("TasteLaneSuggestionsPath", Kind.Path, "reports/youtube_music_tidy_taste_lane_suggestions.csv"),
        new Parameter("ReusePlaylistCache", Kind.Switch, "0"), new Parameter("IncludeLikeActions", Kind.Switch, "0"),
        new Parameter("TraceRequests", Kind.Switch, "1"), new Parameter("QuietRequests", Kind.Switch, "0"),
        new Parameter("IncludeDynamicPlaylists", Kind.Switch, "0"),
        new Parameter("AdditionalNonMusicPlaylistIds", Kind.List, "[]"),
        new Parameter("NonMusicPlaylistConfigPath", Kind.Path, "youtube_music_tidy_nonmusic_playlists.txt"),
        new Parameter("ResolveUnknownLibrary", Kind.Switch, "0"), new Parameter("ResolveLibraryLimit", Kind.Integer, "250"),
        new Parameter("LibraryResolutionCachePath", Kind.Path, "youtube_music_tidy_library_resolution.json"),
        new Parameter("EnrichLibraryTokens", Kind.Switch, "0"), new Parameter("EnrichLibraryTokenLimit", Kind.Integer, "100"),
        new Parameter("RetryUnavailableLibraryTokens", Kind.Switch, "0"),
        new Parameter("LibraryTokenCachePath", Kind.Path, "youtube_music_tidy_library_tokens.json"),
        new Parameter("AddLibraryWriteLimit", Kind.Integer, "25"), new Parameter("AddLibraryFeedbackBatchSize", Kind.Integer, "20"),
        new Parameter("AddLibraryMutationStatePath", Kind.Path, "youtube_music_tidy_add_library_state.json"),
        new Parameter("AddLibraryVerificationDelaySeconds", Kind.Integer, "8"), new Parameter("ReuseRecentWritePreflight", Kind.Switch, "0"),
        new Parameter("RecentWritePreflightMaxAgeMinutes", Kind.Integer, "30"),
        new Parameter("NetworkOutageRetryMinutes", Kind.Integer, "30"), new Parameter("CheckpointEveryPlaylists", Kind.Integer, "1")
    };
    internal static readonly string[] IdentityKeys = { "ExpectedAccountName", "ExpectedChannelHandle" };
    internal static string Canonical(string name)
    {
        name = (name ?? "").Trim();
        foreach (string key in new[] { "DataRoot", "RunAtStartup", "ImportLegacyIdentity" })
            if (String.Equals(name, key, StringComparison.OrdinalIgnoreCase)) return key;
        Parameter parameter = Parameters.FirstOrDefault(p => String.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase));
        return parameter == null ? null : parameter.Name;
    }
    internal static Dictionary<string, string> Defaults(string dataRoot)
    {
        var values = Parameters.ToDictionary(p => p.Name, p => p.Default, StringComparer.OrdinalIgnoreCase);
        values["DataRoot"] = dataRoot; values["RunAtStartup"] = "0"; values["ImportLegacyIdentity"] = "1";
        return values;
    }
    internal static string Normalize(string key, string value)
    {
        if (value == null || value.IndexOfAny(new[] { '\0', '\r', '\n' }) >= 0) throw new ArgumentException(key + " must be a single-line value.");
        if (key == "RunAtStartup" || key == "ImportLegacyIdentity") return ManagedConfigurationStartup.NormalizeValue(value);
        Kind type = key == "DataRoot" ? Kind.Path : Parameters.Single(p => p.Name == key).Type;
        if (type == Kind.Switch) return ManagedConfigurationStartup.NormalizeValue(value);
        if (type == Kind.Path)
        {
            if (String.IsNullOrWhiteSpace(value)) throw new ArgumentException(key + " requires a path.");
            ManagedProfile.ValidatePath(Environment.ExpandEnvironmentVariables(value), key);
        }
        if (type == Kind.Integer)
        {
            int number;
            if (!Int32.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out number)) throw new ArgumentException(key + " requires an integer.");
            if ((key == "BatchSize" && (number < 1 || number > 50)) ||
                (key == "AddLibraryFeedbackBatchSize" && number < 1) ||
                ((key == "AddLibraryWriteLimit" || key == "AddLibraryVerificationDelaySeconds" || key == "RecentWritePreflightMaxAgeMinutes") && number < 0))
                throw new ArgumentOutOfRangeException(key, "The value is outside the existing worker's supported range.");
            return number.ToString(CultureInfo.InvariantCulture);
        }
        if (type == Kind.Number)
        {
            double number;
            if (!Double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out number) || Double.IsNaN(number) || Double.IsInfinity(number))
                throw new ArgumentException(key + " requires a finite number using a decimal point.");
            return number.ToString("R", CultureInfo.InvariantCulture);
        }
        if (type == Kind.List)
        {
            var list = new JavaScriptSerializer().DeserializeObject(value) as object[];
            if (list == null || list.Any(item => !(item is string))) throw new ArgumentException(key + " requires a JSON array of strings.");
            return new JavaScriptSerializer().Serialize(list);
        }
        return value;
    }
    internal static Dictionary<string, string> Read(string ini, string dataRoot, IDictionary<string, string> overrides)
    {
        var values = ManagedProfile.ResolveSettings(Defaults(dataRoot), ManagedIniFile.LoadSection(Spec(ini), false), overrides, Canonical, Normalize);
        ValidatePaths(ini, values);
        return values;
    }
    internal static ManagedIniFileSpec Spec(string ini)
    { return new ManagedIniFileSpec { FilePath = ini, SectionName = "Settings", DefaultContents = "[Settings]\r\n" }; }
    internal static void ValidatePaths(string ini, Dictionary<string, string> values)
    {
        string root = ManagedProfile.ResolvePath(Path.GetDirectoryName(ini), Environment.ExpandEnvironmentVariables(values["DataRoot"]));
        var destinations = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ini, root };
        foreach (Parameter parameter in Parameters.Where(p => p.Type == Kind.Path))
        {
            string path = ManagedProfile.ResolvePath(root, Environment.ExpandEnvironmentVariables(values[parameter.Name]));
            if (!destinations.Add(path)) throw new ArgumentException(parameter.Name + " collides with another data file, the INI, or DataRoot. Every input/output file requires its own path.");
        }
    }
    internal static void Save(string ini, string dataRoot, IDictionary<string, string> changes)
    {
        string error;
        if (!ManagedIniFile.SaveSectionBatch(Spec(ini), changes, prospective => {
            var values = ManagedProfile.ResolveSettings(Defaults(dataRoot), prospective, new Dictionary<string, string>(), Canonical, Normalize);
            ValidatePaths(ini, values);
        }, out error)) throw new IOException("Could not save YouTube Music preferences: " + error);
    }
    internal static Dictionary<string, object> Typed(string ini, Dictionary<string, string> values)
    {
        string root = ManagedProfile.ResolvePath(Path.GetDirectoryName(ini), Environment.ExpandEnvironmentVariables(values["DataRoot"]));
        var result = new Dictionary<string, object>();
        foreach (Parameter parameter in Parameters)
        {
            string value = values[parameter.Name];
            object typed = value;
            if (parameter.Type == Kind.Switch) typed = value == "1";
            else if (parameter.Type == Kind.Integer) typed = Int32.Parse(value, CultureInfo.InvariantCulture);
            else if (parameter.Type == Kind.Number) typed = Double.Parse(value, CultureInfo.InvariantCulture);
            else if (parameter.Type == Kind.List) typed = new JavaScriptSerializer().DeserializeObject(value);
            else if (parameter.Type == Kind.Path) typed = ManagedProfile.ResolvePath(root, Environment.ExpandEnvironmentVariables(value));
            result.Add(parameter.Name, typed);
        }
        return result;
    }
}

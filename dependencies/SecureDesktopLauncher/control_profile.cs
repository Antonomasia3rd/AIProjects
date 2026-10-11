using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using AIProjects.Dependencies;

internal sealed class SecureDesktopControlProfile
{
    internal readonly string IniPath;
    internal readonly bool Password;
    internal SecureDesktopControlProfile(string path, bool password)
    { IniPath = Path.GetFullPath(path); Password = password; }
    internal ManagedIniDocumentSpec Document
    { get { return new ManagedIniDocumentSpec { FilePath = IniPath, DefaultContents = "" }; } }
    static Dictionary<string, string> Map(params string[] pairs)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int index = 0; index < pairs.Length; index += 2) result.Add(pairs[index], pairs[index + 1]);
        return result;
    }
    static readonly Dictionary<string, string> General = Map(
        "Desktop", @"WinSta0\Winlogon", "LaunchSpacingMs", "3000", "MaxProgramsPerSession", "0",
        "StopOnServiceStop", "1", "StartOnServiceStart", "1", "StartOnConsoleConnect", "1",
        "StartOnRemoteConnect", "1", "StartOnLogon", "1", "StartOnLock", "1", "StartOnUnlock", "1",
        "LaunchDisconnectedSessions", "1", "IncludeUsers", "", "ExcludeUsers", "");
    static readonly Dictionary<string, string> Launch = Map("Path", Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "cmd.exe"), "Arguments", "",
        "WorkingDirectory", Environment.GetFolderPath(Environment.SpecialFolder.System), "Desktop", @"WinSta0\Winlogon", "ShowWindow", "1");
    static readonly Dictionary<string, string> Ui = Map("StartMinimized", "1", "TopMost", "0", "AutoLockMinutes", "0");
    static readonly Dictionary<string, string> Security = Map("Kdf", "", "Iterations", "210000", "SaltHex", "",
        "PasswordHashHex", "", "PasswordPbkdf2HashHex", "", "KeepLegacySha256Hash", "0", "MaxAttempts", "3",
        "LockoutSeconds", "30", "PasswordAttemptCount", "0", "LockoutUntilUtcFileTime", "0");
    static readonly HashSet<string> Booleans = new HashSet<string>(new[] { "Enabled", "StopOnServiceStop",
        "StartOnServiceStart", "StartOnConsoleConnect", "StartOnRemoteConnect", "StartOnLogon", "StartOnLock",
        "StartOnUnlock", "LaunchDisconnectedSessions", "PreventDuplicate", "StartMinimized", "TopMost", "KeepLegacySha256Hash" },
        StringComparer.OrdinalIgnoreCase);
    static readonly HashSet<string> Dwords = new HashSet<string>(new[] { "LaunchSpacingMs", "MaxProgramsPerSession",
        "AutoLockMinutes", "MaxAttempts", "LockoutSeconds" }, StringComparer.OrdinalIgnoreCase);

    static bool Is(string left, string right) { return String.Equals(left, right, StringComparison.OrdinalIgnoreCase); }
    static string ProgramName(string section)
    { return section.StartsWith("Program:", StringComparison.OrdinalIgnoreCase) ? section.Substring(8) : null; }
    internal static bool ReadOnly(string label)
    {
        int split = label.LastIndexOf('.');
        if (split < 0 || !Is(label.Substring(0, split), "Security")) return false;
        return new[] { "Kdf", "Iterations", "SaltHex", "PasswordHashHex", "PasswordPbkdf2HashHex",
            "PasswordAttemptCount", "LockoutUntilUtcFileTime" }.Any(key => Is(key, label.Substring(split + 1)));
    }
    internal Dictionary<string, string> EditableValues(ManagedIniDocumentSnapshot snapshot)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        Action<string, IDictionary<string, string>> section = delegate(string name, IDictionary<string, string> defaults)
        {
            var stored = snapshot.GetSection(name);
            foreach (var field in defaults)
                result[name + "." + field.Key] = stored.ContainsKey(field.Key) ? stored[field.Key] : field.Value;
        };
        if (Password)
        { section("Launch", Launch); section("UI", Ui); section("Security", Security); }
        else
        {
            section("General", General);
            var control = Map("ServiceExecutable", Path.Combine(Path.GetDirectoryName(IniPath), "SecureDesktopLauncher.exe"),
                "PasswordExecutable", Path.Combine(Path.GetDirectoryName(IniPath), "SecureDesktopPasswordLauncher.exe"),
                "PasswordIniPath", Path.Combine(Path.GetDirectoryName(IniPath), "SecureDesktopPasswordLauncher.ini"));
            section("Control", control);
            string startup;
            result[ManagedConfigurationStartup.Key] = snapshot.TryGetValue("Control", "RunAtStartup", out startup) ? startup : "0";
            foreach (string name in snapshot.Sections.Where(name => ProgramName(name) != null))
            {
                var stored = snapshot.GetSection(name);
                string path = stored.ContainsKey("Path") ? stored["Path"] : "";
                string directory;
                try { directory = Path.GetDirectoryName(path) ?? ""; } catch (ArgumentException) { directory = ""; }
                var program = Map("Enabled", "1", "Path", "", "Arguments", "", "CommandLine", "",
                    "WorkingDirectory", directory, "Desktop", result["General.Desktop"], "IncludeUsers", "", "ExcludeUsers", "",
                    "PreventDuplicate", "1", "StopOnServiceStop", result["General.StopOnServiceStop"],
                    "LaunchSpacingMs", result["General.LaunchSpacingMs"], "ShowWindow", "4");
                section(name, program);
            }
        }
        return result;
    }
    internal Dictionary<string, string> Read()
    { return EditableValues(ManagedIniFile.LoadDocument(Document)); }

    internal ManagedIniSetting Setting(string label, string value)
    {
        if (!Password && Is(label, "RunAtStartup")) return new ManagedIniSetting("Control", "RunAtStartup", ManagedConfigurationStartup.NormalizeValue(value));
        int split = (label ?? "").LastIndexOf('.');
        if (split <= 0 || split == label.Length - 1) throw new ArgumentException("Use Section.Key=Value.");
        string section = label.Substring(0, split), key = label.Substring(split + 1);
        IDictionary<string, string> fields = null;
        if (Password)
        {
            if (Is(section, "Launch")) fields = Launch;
            if (Is(section, "UI")) fields = Ui;
            if (Is(section, "Security")) fields = Security;
        }
        else
        {
            if (Is(section, "General")) fields = General;
            if (Is(section, "Control")) fields = Map("ServiceExecutable", "", "PasswordExecutable", "", "PasswordIniPath", "", "RunAtStartup", "0");
            if (ProgramName(section) != null)
            {
                ValidateProgramName(ProgramName(section));
                fields = Map("Enabled", "", "Path", "", "Arguments", "", "CommandLine", "", "WorkingDirectory", "", "Desktop", "",
                    "IncludeUsers", "", "ExcludeUsers", "", "PreventDuplicate", "", "StopOnServiceStop", "", "LaunchSpacingMs", "", "ShowWindow", "");
            }
        }
        if (fields == null || !fields.ContainsKey(key)) throw new ArgumentException("Unknown setting: " + label);
        if (ReadOnly(label)) throw new ArgumentException("Use the native set-password action to change password-derived values: " + label);
        string canonical = fields.Keys.First(field => Is(field, key));
        return new ManagedIniSetting(section, canonical, Normalize(section, canonical, value));
    }
    internal static void ValidateProgramName(string name)
    {
        if (String.IsNullOrWhiteSpace(name) || name != name.Trim() || name.IndexOfAny(new[] { '[', ']', '\r', '\n', '\0', '=' }) >= 0)
            throw new ArgumentException("A nonempty program name without INI delimiters is required.");
    }
    static string Normalize(string section, string key, string value)
    {
        if (value == null || value.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0) throw new ArgumentException(section + "." + key + " must be a single-line value.");
        if (Booleans.Contains(key) || Is(key, "RunAtStartup"))
        {
            try { return ManagedConfigurationStartup.NormalizeValue(value); }
            catch (ArgumentException ex) { throw new ArgumentException(section + "." + key + " must be true/false or 1/0.", ex); }
        }
        if (Dwords.Contains(key))
        {
            uint number;
            if (!UInt32.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out number))
                throw new ArgumentException(section + "." + key + " must be an unsigned 32-bit integer.");
            if (Is(key, "MaxAttempts") && (number < 1 || number > 100)) throw new ArgumentException("Security.MaxAttempts must be 1 through 100.");
            return number.ToString(CultureInfo.InvariantCulture);
        }
        if (Is(key, "ShowWindow"))
        {
            int number;
            if (!Int32.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out number)) throw new ArgumentException(section + ".ShowWindow must be an integer.");
            return number.ToString(CultureInfo.InvariantCulture);
        }
        if (Is(section, "Control"))
        {
            if (String.IsNullOrWhiteSpace(value)) throw new ArgumentException(key + " requires a path.");
            ManagedProfile.ValidatePath(Environment.ExpandEnvironmentVariables(value), key);
        }
        return value;
    }
    internal void Validate(ManagedIniDocumentSnapshot snapshot)
    {
        var values = EditableValues(snapshot);
        foreach (var field in values)
        {
            if (ReadOnly(field.Key)) continue;
            int split = field.Key.LastIndexOf('.');
            if (split >= 0 && ProgramName(field.Key.Substring(0, split)) != null &&
                !Is(field.Key.Substring(split + 1), "Enabled") &&
                ManagedConfigurationStartup.NormalizeValue(values[field.Key.Substring(0, split) + ".Enabled"]) == "0")
                continue;
            if (split < 0) ManagedConfigurationStartup.NormalizeValue(field.Value);
            else Normalize(field.Key.Substring(0, split), field.Key.Substring(split + 1), field.Value);
        }
        foreach (string section in snapshot.Sections.Where(name => !Password && ProgramName(name) != null))
        {
            string prefix = section + ".";
            if (ManagedConfigurationStartup.NormalizeValue(values[prefix + "Enabled"]) == "1" && String.IsNullOrWhiteSpace(values[prefix + "Path"]))
                throw new ArgumentException(section + " requires Path when enabled, including when CommandLine is supplied.");
        }
        if (Password && String.IsNullOrWhiteSpace(values["Launch.Path"])) throw new ArgumentException("Launch.Path is required.");
    }
    internal void ValidateChanges(IDictionary<string, string> changes)
    {
        var current = ManagedIniFile.LoadDocument(Document);
        Validate(current.WithChanges(changes.Select(change => Setting(change.Key, change.Value))));
    }
    internal void Save(IDictionary<string, string> changes)
    {
        string error;
        if (!ManagedIniFile.SaveDocumentBatch(Document, changes.Select(change => Setting(change.Key, change.Value)), Validate, out error))
            throw new IOException("Could not save the configuration: " + error);
    }
    internal void AddProgram(string name, IDictionary<string, string> extraChanges)
    {
        ValidateProgramName(name);
        using (ManagedIniFile.BeginTransaction(Document))
        {
            if (ManagedIniFile.LoadDocument(Document).ContainsSection("Program:" + name))
                throw new ArgumentException("That program already exists.");
            var changes = new Dictionary<string, string>(extraChanges, StringComparer.OrdinalIgnoreCase);
            if (!changes.ContainsKey("Program:" + name + ".Enabled")) changes["Program:" + name + ".Enabled"] = "0";
            Save(changes);
        }
    }
}

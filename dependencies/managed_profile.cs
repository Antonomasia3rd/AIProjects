using System;
using System.Collections.Generic;
using System.IO;

namespace AIProjects.Dependencies
{
    // Shared mechanics for managed product overlays. Products supply only their
    // key catalog, defaults, typed validator, and path-base policy.
    public static class ManagedProfile
    {
        public static void SetAssignment(IDictionary<string, string> changes,
            string assignment, string section, Func<string, string> canonicalKey,
            Func<string, string, string> normalize)
        {
            int equals = (assignment ?? "").IndexOf('=');
            if (equals <= 0) throw new ArgumentException("--set requires " + section + ".Key=Value.");
            string key = assignment.Substring(0, equals).Trim();
            int separator = key.IndexOf('.');
            if (separator >= 0)
            {
                if (!key.Substring(0, separator).Equals(section, StringComparison.OrdinalIgnoreCase))
                    throw new ArgumentException("--set supports only the [" + section + "] section.");
                key = key.Substring(separator + 1);
            }
            string canonical = canonicalKey(key);
            if (canonical == null) throw new ArgumentException("Unknown setting: " + key);
            changes[canonical] = normalize(canonical, assignment.Substring(equals + 1));
        }

        public static Dictionary<string, string> ResolveSettings(
            IDictionary<string, string> defaults, IDictionary<string, string> stored,
            IDictionary<string, string> overrides, Func<string, string> canonicalKey,
            Func<string, string, string> normalize)
        {
            var values = new Dictionary<string, string>(defaults, StringComparer.OrdinalIgnoreCase);
            foreach (var setting in stored)
            {
                string key = canonicalKey(setting.Key);
                if (key == null) throw new InvalidDataException("Unknown [Settings] key: " + setting.Key);
                values[key] = setting.Value;
            }
            // Apply a replacement before validating the effective value, so
            // --set can repair an invalid saved value instead of rejecting it.
            foreach (var setting in overrides) values[setting.Key] = setting.Value;
            foreach (string key in new List<string>(values.Keys))
                values[key] = normalize(key, values[key]);
            return values;
        }

        public static string ResolveIniPath(string supplied, string executableDirectory, string defaultName)
        {
            // null means no override; explicitly supplied whitespace is an error.
            string path = supplied == null
                ? ResolvePath(executableDirectory, defaultName)
                : ResolvePath(Directory.GetCurrentDirectory(), supplied.Trim());
            if (path == null || String.IsNullOrEmpty(Path.GetFileName(path)) ||
                (supplied != null && (supplied.EndsWith("\\") || supplied.EndsWith("/"))) ||
                Directory.Exists(path))
                throw new ArgumentException("--ini requires a file path.");
            return path;
        }

        public static string ResolvePath(string directory, string value)
        {
            if (String.IsNullOrWhiteSpace(value)) return null;
            return Path.GetFullPath(Path.IsPathRooted(value) ? value : Path.Combine(directory, value));
        }

        public static void ValidatePath(string value, string settingName)
        {
            try { Path.GetFullPath(value); }
            catch (ArgumentException ex) { throw new ArgumentException("Invalid " + settingName + " path: " + ex.Message, ex); }
            catch (NotSupportedException ex) { throw new ArgumentException("Invalid " + settingName + " path: " + ex.Message, ex); }
            catch (PathTooLongException ex) { throw new ArgumentException("Invalid " + settingName + " path: " + ex.Message, ex); }
        }
    }
}

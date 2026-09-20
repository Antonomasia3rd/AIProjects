using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;

namespace AIProjects.Dependencies
{
    public sealed class ManagedIniFileSpec
    {
        public string FilePath;
        public string SectionName;
        public string DefaultContents;
        public Action<string> Log;
        public int MutationWaitMilliseconds = 10000;
        public long MaximumBytes = 16L * 1024L * 1024L;
    }

    public static class ManagedIniFile
    {
        public static void EnsureExists(ManagedIniFileSpec spec)
        {
            string error;
            if (!ValidateSpec(spec, out error))
                throw new ArgumentException(error, "spec");
            if (File.Exists(spec.FilePath))
                return;

            using (Mutex mutationMutex = CreateMutationMutex(spec))
            {
                bool ownsMutex = WaitForMutex(mutationMutex, spec.MutationWaitMilliseconds);
                try
                {
                    EnsureExistsUnderLock(spec);
                }
                finally
                {
                    if (ownsMutex)
                        mutationMutex.ReleaseMutex();
                }
            }
        }

        public static Dictionary<string, string> LoadSection(ManagedIniFileSpec spec)
        {
            string error;
            if (!ValidateSpec(spec, out error))
                throw new ArgumentException(error, "spec");
            using (Mutex mutationMutex = CreateMutationMutex(spec))
            {
                bool ownsMutex = WaitForMutex(mutationMutex, spec.MutationWaitMilliseconds);
                try
                {
                    EnsureExistsUnderLock(spec);
                    List<string> lines = ReadLinesUnderLock(spec);
                    return ParseTargetSection(spec, lines);
                }
                finally
                {
                    if (ownsMutex)
                        mutationMutex.ReleaseMutex();
                }
            }
        }

        public static bool SaveSectionBatch(
            ManagedIniFileSpec spec,
            IDictionary<string, string> settings,
            out string error)
        {
            error = null;
            string validationError;
            if (!ValidateSpec(spec, out validationError))
            {
                error = validationError;
                return false;
            }
            if (settings == null || settings.Count == 0)
            {
                try
                {
                    EnsureExists(spec);
                    return true;
                }
                catch (Exception ex)
                {
                    error = "Could not create the INI: " + ex.Message;
                    SafeLog(spec, error);
                    return false;
                }
            }
            foreach (KeyValuePair<string, string> setting in settings)
            {
                if (!IsValidKey(setting.Key) || setting.Value == null ||
                    setting.Value.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0)
                {
                    error = "The INI setting batch contains an invalid key or value.";
                    return false;
                }
            }

            try
            {
                using (Mutex mutationMutex = CreateMutationMutex(spec))
                {
                    bool ownsMutex = WaitForMutex(mutationMutex, spec.MutationWaitMilliseconds);
                    try
                    {
                        EnsureExistsUnderLock(spec);
                        GuardFileSize(spec);
                        SaveSectionBatchUnderLock(spec, settings);
                        return true;
                    }
                    finally
                    {
                        if (ownsMutex)
                            mutationMutex.ReleaseMutex();
                    }
                }
            }
            catch (Exception ex)
            {
                error = "Could not save INI settings: " + ex.Message;
                SafeLog(spec, error);
                return false;
            }
        }

        static void EnsureExistsUnderLock(ManagedIniFileSpec spec)
        {
            if (File.Exists(spec.FilePath))
                return;
            // Refuse to create a default that this same helper cannot load.
            ParseTargetSection(spec, SplitLines(spec.DefaultContents ?? ""));
            string directory = Path.GetDirectoryName(spec.FilePath);
            Directory.CreateDirectory(directory);
            string temporaryPath = Path.Combine(
                directory,
                ".AIProjects-" + Guid.NewGuid().ToString("N") + ".tmp");
            try
            {
                File.WriteAllText(
                    temporaryPath,
                    spec.DefaultContents ?? "",
                    new UTF8Encoding(true, true));
                GuardFileSize(spec, temporaryPath);
                try
                {
                    File.Move(temporaryPath, spec.FilePath);
                }
                catch (IOException)
                {
                    if (!File.Exists(spec.FilePath))
                        throw;
                }
            }
            finally
            {
                try
                {
                    if (File.Exists(temporaryPath))
                        File.Delete(temporaryPath);
                }
                catch { }
            }
        }

        static void SaveSectionBatchUnderLock(
            ManagedIniFileSpec spec,
            IDictionary<string, string> settings)
        {
            var lines = ReadLinesUnderLock(spec);
            // A managed save never papers over a malformed target section. The
            // caller must repair invalid configuration before mutation.
            ParseTargetSection(spec, lines);
            foreach (KeyValuePair<string, string> setting in settings)
            {
                var matches = new List<int>();
                int firstSectionHeader = -1;
                int firstSectionEnd = lines.Count;
                bool inTargetSection = false;
                bool foundFirstTargetSection = false;
                for (int i = 0; i < lines.Count; ++i)
                {
                    string trimmed = lines[i].Trim();
                    string section;
                    if (TryParseSectionHeader(trimmed, out section))
                    {
                        if (inTargetSection && foundFirstTargetSection && firstSectionEnd == lines.Count)
                            firstSectionEnd = i;
                        inTargetSection = section.Equals(spec.SectionName, StringComparison.OrdinalIgnoreCase);
                        if (inTargetSection && !foundFirstTargetSection)
                        {
                            foundFirstTargetSection = true;
                            firstSectionHeader = i;
                        }
                        continue;
                    }
                    if (!inTargetSection || trimmed.Length == 0 ||
                        trimmed.StartsWith(";") || trimmed.StartsWith("#"))
                        continue;
                    int equals = trimmed.IndexOf('=');
                    if (equals > 0 && UnquoteIniString(trimmed.Substring(0, equals)).Equals(
                        setting.Key,
                        StringComparison.OrdinalIgnoreCase))
                        matches.Add(i);
                }

                string assignment = QuoteIniString(setting.Key) + " = " + QuoteIniString(setting.Value);
                if (matches.Count != 0)
                {
                    lines[matches[0]] = assignment;
                    for (int i = matches.Count - 1; i >= 1; --i)
                        lines.RemoveAt(matches[i]);
                }
                else if (firstSectionHeader >= 0)
                {
                    lines.Insert(firstSectionEnd, assignment);
                }
                else
                {
                    if (lines.Count != 0 && lines[lines.Count - 1].Length != 0)
                        lines.Add("");
                    lines.Add("[" + spec.SectionName + "]");
                    lines.Add(assignment);
                }
            }

            string directory = Path.GetDirectoryName(spec.FilePath);
            string temporaryPath = Path.Combine(
                directory,
                ".AIProjects-" + Guid.NewGuid().ToString("N") + ".tmp");
            try
            {
                File.WriteAllLines(temporaryPath, lines, new UTF8Encoding(true, true));
                GuardFileSize(spec, temporaryPath);
                if (File.Exists(spec.FilePath))
                    File.Replace(temporaryPath, spec.FilePath, null);
                else
                    File.Move(temporaryPath, spec.FilePath);
            }
            finally
            {
                try
                {
                    if (File.Exists(temporaryPath))
                        File.Delete(temporaryPath);
                }
                catch { }
            }
        }

        static Mutex CreateMutationMutex(ManagedIniFileSpec spec)
        {
            return new Mutex(
                false,
                ManagedNamedObjects.CurrentUserScopedName(
                    "ManagedIni",
                    NormalizePath(spec.FilePath)));
        }

        static bool WaitForMutex(Mutex mutex, int waitMilliseconds)
        {
            try
            {
                if (mutex.WaitOne(Math.Max(0, waitMilliseconds)))
                    return true;
            }
            catch (AbandonedMutexException)
            {
                return true;
            }
            throw new TimeoutException("Timed out waiting for the INI mutation lock.");
        }

        static List<string> ReadLinesUnderLock(ManagedIniFileSpec spec)
        {
            GuardFileSize(spec, spec.FilePath);
            using (var stream = new FileStream(
                spec.FilePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read | FileShare.Delete))
            {
                long limit = Math.Max(1024L, spec.MaximumBytes);
                if (stream.Length > limit)
                    throw OversizedFile(limit);
                using (var bytes = new MemoryStream())
                {
                    var buffer = new byte[8192];
                    long total = 0;
                    int read;
                    while ((read = stream.Read(buffer, 0, buffer.Length)) != 0)
                    {
                        if (read > limit - total)
                            throw OversizedFile(limit);
                        bytes.Write(buffer, 0, read);
                        total += read;
                    }
                    return SplitLines(DecodeIniBytes(bytes.ToArray()));
                }
            }
        }

        static string DecodeIniBytes(byte[] bytes)
        {
            if (bytes == null || bytes.Length == 0)
                return "";

            Encoding encoding;
            int offset;
            if (bytes.Length >= 4 && bytes[0] == 0xFF && bytes[1] == 0xFE &&
                bytes[2] == 0x00 && bytes[3] == 0x00)
            {
                encoding = new UTF32Encoding(false, true, true);
                offset = 4;
            }
            else if (bytes.Length >= 4 && bytes[0] == 0x00 && bytes[1] == 0x00 &&
                bytes[2] == 0xFE && bytes[3] == 0xFF)
            {
                encoding = new UTF32Encoding(true, true, true);
                offset = 4;
            }
            else if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
                bytes[2] == 0xBF)
            {
                encoding = new UTF8Encoding(false, true);
                offset = 3;
            }
            else if (bytes.Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
            {
                encoding = new UnicodeEncoding(false, true, true);
                offset = 2;
            }
            else if (bytes.Length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
            {
                encoding = new UnicodeEncoding(true, true, true);
                offset = 2;
            }
            else
            {
                encoding = new UTF8Encoding(false, true);
                offset = 0;
            }
            return encoding.GetString(bytes, offset, bytes.Length - offset);
        }

        static Dictionary<string, string> ParseTargetSection(
            ManagedIniFileSpec spec,
            IEnumerable<string> lines)
        {
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            string currentSection = "";
            foreach (string rawLine in lines)
            {
                string line = (rawLine ?? "").Trim();
                if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#"))
                    continue;
                string section;
                if (TryParseSectionHeader(line, out section))
                {
                    currentSection = section;
                    continue;
                }
                if (!currentSection.Equals(spec.SectionName, StringComparison.OrdinalIgnoreCase))
                    continue;
                int equals = line.IndexOf('=');
                if (equals <= 0)
                    throw new InvalidDataException(
                        "Malformed assignment in [" + spec.SectionName + "]: " + line);
                string key = UnquoteIniString(line.Substring(0, equals));
                if (!IsValidKey(key))
                    throw new InvalidDataException(
                        "Invalid key in [" + spec.SectionName + "]: " + key);
                values[key] = ParseIniValue(line.Substring(equals + 1));
            }
            return values;
        }

        // Keep this dialect aligned with config_ini.inc. Both readers accept
        // existing bare assignments and DesktopStub's quoted UTF-8 format.
        // Only quotes/backslashes are unescaped here; template \n/\r/\t
        // decoding remains an application decision.
        static bool IsBackslashEscaped(string text, int position)
        {
            int count = 0;
            for (int i = position; i > 0 && text[i - 1] == '\\'; --i)
                ++count;
            return count % 2 != 0;
        }

        static string UnquoteIniString(string text)
        {
            string value = (text ?? "").Trim();
            if (value.Length < 2 || (value[0] != '"' && value[0] != '\'') ||
                value[value.Length - 1] != value[0])
                return value;
            char quote = value[0];
            var result = new StringBuilder(value.Length - 2);
            for (int i = 1; i + 1 < value.Length; ++i)
            {
                if (value[i] == '\\' && i + 2 < value.Length && value[i + 1] == quote)
                    ++i;
                result.Append(value[i]);
            }
            return result.ToString();
        }

        static bool TryParseSectionHeader(string text, out string name)
        {
            name = null;
            string value = (text ?? "").TrimStart('\uFEFF').Trim();
            if (value.Length == 0 || value[0] != '[')
                return false;
            char quote = '\0';
            for (int i = 1; i < value.Length; ++i)
            {
                char character = value[i];
                if ((character == '"' || character == '\'') && !IsBackslashEscaped(value, i))
                {
                    if (quote == '\0') quote = character;
                    else if (quote == character) quote = '\0';
                    continue;
                }
                if (quote == '\0' && character == ']')
                {
                    string remainder = value.Substring(i + 1).Trim();
                    if (remainder.Length != 0 && remainder[0] != ';' && remainder[0] != '#')
                        return false;
                    name = UnquoteIniString(value.Substring(1, i - 1));
                    return true;
                }
            }
            return false;
        }

        static string StripInlineComment(string value, bool requireLeadingWhitespace)
        {
            char quote = '\0';
            for (int i = 0; i < value.Length; ++i)
            {
                char character = value[i];
                if ((character == '"' || character == '\'') && !IsBackslashEscaped(value, i))
                {
                    if (quote == '\0') quote = character;
                    else if (quote == character) quote = '\0';
                    continue;
                }
                if (quote == '\0' && (character == ';' || character == '#') &&
                    (!requireLeadingWhitespace || i == 0 || Char.IsWhiteSpace(value[i - 1])))
                    return value.Substring(0, i).TrimEnd();
            }
            return value.TrimEnd();
        }

        static bool HasLaterQuote(string value, int start, char quote)
        {
            for (int i = start; i < value.Length; ++i)
                if (value[i] == quote && !IsBackslashEscaped(value, i))
                    return true;
            return false;
        }

        static string ParseIniValue(string text)
        {
            string value = text.Trim();
            if (value.Length == 0) return value;
            char quote = value[0];
            if (quote != '"' && quote != '\'')
                return UnquoteIniString(StripInlineComment(text, false));
            var result = new StringBuilder(value.Length);
            for (int i = 1; i < value.Length; ++i)
            {
                char character = value[i];
                if (character == '\\' && i + 1 < value.Length &&
                    (value[i + 1] == quote || value[i + 1] == '\\'))
                {
                    // Native compatibility: a raw quoted C:\path\ remains
                    // usable when the last backslash precedes the end quote.
                    if (value[i + 1] == quote && !HasLaterQuote(value, i + 2, quote))
                    {
                        result.Append(character);
                        continue;
                    }
                    result.Append(value[++i]);
                    continue;
                }
                if (character == quote) return result.ToString();
                result.Append(character);
            }
            return UnquoteIniString(StripInlineComment(text, true));
        }

        static string QuoteIniString(string value)
        {
            return "\"" + value.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
        }

        static List<string> SplitLines(string text)
        {
            var lines = new List<string>();
            using (var reader = new StringReader(text ?? ""))
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                    lines.Add(line);
            }
            return lines;
        }

        static void GuardFileSize(ManagedIniFileSpec spec)
        {
            GuardFileSize(spec, spec.FilePath);
        }

        static void GuardFileSize(ManagedIniFileSpec spec, string path)
        {
            long limit = Math.Max(1024L, spec.MaximumBytes);
            var info = new FileInfo(path);
            if (info.Exists && info.Length > limit)
                throw OversizedFile(limit);
        }

        static InvalidDataException OversizedFile(long limit)
        {
            return new InvalidDataException(
                "INI file exceeds the " + limit.ToString(CultureInfo.InvariantCulture) +
                " byte safety limit.");
        }

        static bool ValidateSpec(ManagedIniFileSpec spec, out string error)
        {
            error = null;
            if (spec == null)
            {
                error = "The managed INI specification is missing.";
                return false;
            }
            if (String.IsNullOrWhiteSpace(spec.FilePath) ||
                String.IsNullOrWhiteSpace(spec.SectionName))
            {
                error = "The managed INI specification is incomplete.";
                return false;
            }
            if (!Path.IsPathRooted(spec.FilePath) ||
                String.IsNullOrWhiteSpace(Path.GetDirectoryName(spec.FilePath)))
            {
                error = "The managed INI path must be absolute and have a parent directory.";
                return false;
            }
            if (!spec.SectionName.Equals(spec.SectionName.Trim(), StringComparison.Ordinal) ||
                spec.SectionName.StartsWith(";") || spec.SectionName.StartsWith("#") ||
                spec.SectionName.IndexOfAny(new[] { '\r', '\n', '\0', '[', ']' }) >= 0)
            {
                error = "The managed INI section name is invalid.";
                return false;
            }
            if (spec.MutationWaitMilliseconds < 0 || spec.MaximumBytes < 1024)
            {
                error = "The managed INI wait or size limit is invalid.";
                return false;
            }
            return true;
        }

        static bool IsValidKey(string key)
        {
            return !String.IsNullOrWhiteSpace(key) &&
                key.Equals(key.Trim(), StringComparison.Ordinal) &&
                !key.StartsWith(";") && !key.StartsWith("#") &&
                key.IndexOfAny(new[] { '\r', '\n', '\0', '=', '[', ']' }) < 0;
        }

        static string NormalizePath(string path)
        {
            try
            {
                return Path.GetFullPath(path ?? "").TrimEnd(
                    Path.DirectorySeparatorChar,
                    Path.AltDirectorySeparatorChar).ToUpperInvariant();
            }
            catch
            {
                return (path ?? "").ToUpperInvariant();
            }
        }

        static void SafeLog(ManagedIniFileSpec spec, string message)
        {
            if (spec == null || spec.Log == null)
                return;
            try { spec.Log(message); }
            catch { }
        }
    }
}

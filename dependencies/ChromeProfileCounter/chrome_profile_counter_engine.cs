using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;

namespace AIProjects.Dependencies
{
    // This seam performs only operations on caller-supplied paths. Tests can
    // inject failures at the same boundaries used by the real replacement.
    public class ChromeProfileCounterFileSystem
    {
        public virtual string ReadText(string path)
        {
            byte[] bytes = File.ReadAllBytes(path);
            int skip = 0;
            Encoding encoding = new UTF8Encoding(false, true);
            if (bytes.Length >= 4 && bytes[0] == 0xFF && bytes[1] == 0xFE && bytes[2] == 0 && bytes[3] == 0)
            { encoding = new UTF32Encoding(false, true, true); skip = 4; }
            else if (bytes.Length >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xFE && bytes[3] == 0xFF)
            { encoding = new UTF32Encoding(true, true, true); skip = 4; }
            else if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
                skip = 3;
            else if (bytes.Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
            { encoding = new UnicodeEncoding(false, true, true); skip = 2; }
            else if (bytes.Length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
            { encoding = new UnicodeEncoding(true, true, true); skip = 2; }
            // Decoder replacement would alter unrelated data during a repair.
            // Detect BOMs explicitly because StreamReader's BOM-selected codecs
            // do not inherit the supplied UTF-8 codec's exception fallback.
            return encoding.GetString(bytes, skip, bytes.Length - skip);
        }
        public virtual IEnumerable<string> EnumerateDirectories(string path) { return Directory.EnumerateDirectories(path); }
        public virtual void CreateDirectory(string path) { Directory.CreateDirectory(path); }
        public virtual bool FileExists(string path) { return File.Exists(path); }
        public virtual void WriteTemporary(string path, string text)
        { File.WriteAllText(path, text, new UTF8Encoding(false, true)); }
        public virtual void Replace(string temporary, string destination, string backup)
        { File.Replace(temporary, destination, backup); }
        public virtual void DeleteTemporary(string path) { File.Delete(path); }
    }

    // Engine-only migration: this class is not wired into the legacy launcher.
    // No environment lookup, process discovery, console UI or job dispatch
    // happens here. The caller must explicitly supply paths and observation.
    public sealed class ChromeProfileCounterEngine
    {
        static readonly Regex CounterPattern = new Regex("\"profiles_created\"\\s*:\\s*(\\d+)(?=\\s*[,}])", RegexOptions.CultureInvariant);
        static readonly Regex DiskProfilePattern = new Regex(@"^Profile (\d+)$", RegexOptions.CultureInvariant);
        static readonly Regex RegisteredProfilePattern = new Regex(@"^Profile (\d+)$", RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);
        readonly ChromeProfileCounterFileSystem files;
        readonly Func<bool> isBrowserRunning;
        readonly int mutexTimeoutMilliseconds;

        public string UserDataDirectory { get; private set; }
        public string LocalStatePath { get; private set; }
        public string BackupDirectory { get; private set; }

        public ChromeProfileCounterEngine(string userDataDirectory, string backupDirectory,
            Func<bool> isBrowserRunning, ChromeProfileCounterFileSystem files = null,
            int mutexTimeoutMilliseconds = 5000)
        {
            if (String.IsNullOrWhiteSpace(userDataDirectory)) throw new ArgumentException("A user-data directory is required.", "userDataDirectory");
            if (String.IsNullOrWhiteSpace(backupDirectory)) throw new ArgumentException("A backup directory is required.", "backupDirectory");
            if (isBrowserRunning == null) throw new ArgumentNullException("isBrowserRunning");
            if (mutexTimeoutMilliseconds < 0) throw new ArgumentOutOfRangeException("mutexTimeoutMilliseconds");
            UserDataDirectory = Path.GetFullPath(userDataDirectory);
            LocalStatePath = Path.Combine(UserDataDirectory, "Local State");
            BackupDirectory = Path.GetFullPath(backupDirectory);
            this.isBrowserRunning = isBrowserRunning;
            this.files = files ?? new ChromeProfileCounterFileSystem();
            this.mutexTimeoutMilliseconds = mutexTimeoutMilliseconds;
        }

        public string ReadStateText() { return files.ReadText(LocalStatePath); }

        static Dictionary<string, object> ReadProfile(string text)
        {
            CounterSpan ignored;
            return ReadProfile(text, out ignored);
        }

        static Dictionary<string, object> ReadProfile(string text, out CounterSpan counterSpan)
        {
            // JavaScriptSerializer alone accepts JavaScript extensions such as
            // single-quoted strings. A Local State repair requires actual JSON.
            counterSpan = JsonSyntax.Validate(text);
            object parsed;
            try
            {
                parsed = new JavaScriptSerializer { MaxJsonLength = Int32.MaxValue, RecursionLimit = 256 }.DeserializeObject(text);
            }
            catch (ArgumentException ex) { throw new InvalidDataException("Local State is not readable JSON.", ex); }
            catch (InvalidOperationException ex) { throw new InvalidDataException("Local State is not readable JSON.", ex); }
            var root = parsed as Dictionary<string, object>;
            var profile = Member(root, "profile") as Dictionary<string, object>;
            if (profile == null) throw new InvalidDataException("Local State has no readable profile object.");
            return profile;
        }

        static object Member(Dictionary<string, object> value, string key)
        {
            object result = null;
            bool found = false;
            if (value == null) return null;
            foreach (var entry in value)
                if (String.Equals(entry.Key, key, StringComparison.OrdinalIgnoreCase))
                {
                    if (found) throw new InvalidDataException("Local State has ambiguous property casing: " + key);
                    found = true;
                    result = entry.Value;
                }
            return result;
        }

        static Group CounterDigits(string text)
        {
            CounterSpan span;
            var profile = ReadProfile(text, out span);
            var matches = CounterPattern.Matches(text);
            int counter;
            object value = Member(profile, "profiles_created");
            if (span == null || matches.Count != 1 ||
                matches[0].Groups[1].Index != span.Start || matches[0].Groups[1].Length != span.Length ||
                !Int32.TryParse(matches[0].Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out counter) ||
                !(value is int) || (int)value != counter)
                throw new InvalidDataException("Local State must contain one unambiguous integer profile.profiles_created value.");
            return matches[0].Groups[1];
        }

        public static int GetCounter(string text)
        { return Int32.Parse(CounterDigits(text).Value, CultureInfo.InvariantCulture); }

        public static string SetCounterInText(string text, int newValue)
        {
            if (newValue < 1) throw new ArgumentOutOfRangeException("newValue", "Counter must be 1 or greater.");
            Group digits = CounterDigits(text);
            return text.Substring(0, digits.Index) + newValue.ToString(CultureInfo.InvariantCulture) +
                text.Substring(digits.Index + digits.Length);
        }

        public HashSet<int> GetDiskProfiles()
        {
            var result = new HashSet<int>();
            foreach (string directory in files.EnumerateDirectories(UserDataDirectory))
            {
                Match match = DiskProfilePattern.Match(Path.GetFileName(directory));
                int number;
                if (match.Success && Int32.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out number))
                    result.Add(number);
            }
            return result;
        }

        public static HashSet<int> GetRegisteredProfiles(string text)
        {
            object value = Member(ReadProfile(text), "info_cache");
            var result = new HashSet<int>();
            if (value == null) return result;
            var cache = value as Dictionary<string, object>;
            // Do not mistake an unreadable cache for a list of free numbers.
            if (cache == null) throw new InvalidDataException("profile.info_cache must be a JSON object.");
            foreach (string name in cache.Keys)
            {
                Match match = RegisteredProfilePattern.Match(name);
                if (!match.Success) continue;
                int number;
                if (!Int32.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out number))
                    throw new InvalidDataException("A registered profile number exceeds the counter range.");
                result.Add(number);
            }
            return result;
        }

        public int GetFirstSafeNumber(string reviewedText)
        {
            var disk = GetDiskProfiles();
            var registered = GetRegisteredProfiles(reviewedText);
            int value = 1;
            while (disk.Contains(value) || registered.Contains(value))
            {
                if (value == Int32.MaxValue) throw new InvalidDataException("No available numbered profile fits the counter range.");
                ++value;
            }
            return value;
        }

        internal static string MutationMutexName(string localStatePath)
        {
            using (var hash = SHA256.Create())
                return "Global\\AIProjects.ChromeProfileCounter." + BitConverter.ToString(hash.ComputeHash(
                    Encoding.UTF8.GetBytes(Path.GetFullPath(localStatePath).ToUpperInvariant()))).Replace("-", "");
        }

        // The caller owns review/confirmation. Only a freshly generated digit
        // replacement can reach the writer, not an arbitrary replacement file.
        public string ChangeCounter(string reviewedText, int newValue)
        {
            string replacement = SetCounterInText(reviewedText, newValue);
            if (isBrowserRunning()) throw new IOException("Chrome started while the counter change was being prepared. Local State was left unchanged.");
            string temporary = Path.Combine(UserDataDirectory, ".Local State." + Guid.NewGuid().ToString("N") + ".tmp");
            using (var mutex = new Mutex(false, MutationMutexName(LocalStatePath)))
            {
                bool locked = false;
                try
                {
                    try { locked = mutex.WaitOne(mutexTimeoutMilliseconds); }
                    catch (AbandonedMutexException) { locked = true; }
                    if (!locked) throw new IOException("Another counter edit is still running. Try again later.");
                    files.WriteTemporary(temporary, replacement);
                    if (isBrowserRunning()) throw new IOException("Chrome started before Local State could be replaced. Local State was left unchanged.");
                    if (!String.Equals(ReadStateText(), reviewedText, StringComparison.Ordinal))
                        throw new IOException("Local State changed after review. Nothing was replaced; review the new state and retry.");
                    if (GetDiskProfiles().Contains(newValue) || GetRegisteredProfiles(reviewedText).Contains(newValue))
                        throw new IOException("The selected profile number is now in use. Review the current state and retry.");
                    files.CreateDirectory(BackupDirectory);
                    string backup = Path.Combine(BackupDirectory, "Local State." + DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss-fff", CultureInfo.InvariantCulture) + "." + Guid.NewGuid().ToString("N") + ".backup");
                    if (files.FileExists(backup)) throw new IOException("Backup filename already exists; retry the change.");
                    // Directory/backup preparation can take time. Recheck at
                    // the final publication boundary as well. Chrome does not
                    // use this mutex, so an external race remains possible.
                    if (isBrowserRunning()) throw new IOException("Chrome started before Local State could be replaced. Local State was left unchanged.");
                    if (!String.Equals(ReadStateText(), reviewedText, StringComparison.Ordinal))
                        throw new IOException("Local State changed after review. Nothing was replaced; review the new state and retry.");
                    // File.Replace captures the exact old bytes, including BOM
                    // and encoding, rather than a separately reserialized copy.
                    files.Replace(temporary, LocalStatePath, backup);
                    return backup;
                }
                finally
                {
                    try { files.DeleteTemporary(temporary); }
                    finally { if (locked) mutex.ReleaseMutex(); }
                }
            }
        }

        sealed class CounterSpan
        {
            public int Start;
            public int Length;
        }

        // Besides strict syntax, preserve the target's original token location
        // before the framework parser can collapse escaped/duplicate keys.
        sealed class JsonSyntax
        {
            readonly string text;
            int position;
            int profiles;
            int counters;
            int caches;
            CounterSpan counter;
            JsonSyntax(string text) { this.text = text; }
            public static CounterSpan Validate(string text)
            {
                if (text == null) throw new ArgumentNullException("text");
                var parser = new JsonSyntax(text);
                parser.Value(0, true, false);
                parser.Space();
                if (parser.position != text.Length) parser.Invalid();
                return parser.counter;
            }
            void Invalid() { throw new InvalidDataException("Local State contains invalid JSON near character " + position.ToString(CultureInfo.InvariantCulture) + "."); }
            void Space()
            { while (position < text.Length && (text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n')) ++position; }
            bool Take(char value)
            { if (position < text.Length && text[position] == value) { ++position; return true; } return false; }
            void Need(char value) { if (!Take(value)) Invalid(); }
            void Literal(string value)
            { foreach (char c in value) Need(c); }
            bool Digit() { return position < text.Length && text[position] >= '0' && text[position] <= '9'; }
            void Digits() { if (!Digit()) Invalid(); while (Digit()) ++position; }
            void Number()
            {
                Take('-');
                if (!Take('0')) Digits();
                if (Take('.')) Digits();
                if (Take('e') || Take('E')) { if (!Take('+')) Take('-'); Digits(); }
            }
            string ReadString()
            {
                Need('"');
                var value = new StringBuilder();
                while (position < text.Length)
                {
                    char c = text[position++];
                    if (c == '"') return value.ToString();
                    if (c < 0x20) Invalid();
                    if (c != '\\') { value.Append(c); continue; }
                    if (position == text.Length) Invalid();
                    char escape = text[position++];
                    if (escape == 'u')
                    {
                        int code = 0;
                        for (int n = 0; n < 4; ++n)
                        {
                            if (position == text.Length) Invalid();
                            char hex = text[position++];
                            if (!((hex >= '0' && hex <= '9') || (hex >= 'A' && hex <= 'F') || (hex >= 'a' && hex <= 'f'))) Invalid();
                            code = code * 16 + (hex <= '9' ? hex - '0' : (hex <= 'F' ? hex - 'A' : hex - 'a') + 10);
                        }
                        value.Append((char)code);
                    }
                    else
                    {
                        switch (escape)
                        {
                            case '"': case '\\': case '/': value.Append(escape); break;
                            case 'b': value.Append('\b'); break;
                            case 'f': value.Append('\f'); break;
                            case 'n': value.Append('\n'); break;
                            case 'r': value.Append('\r'); break;
                            case 't': value.Append('\t'); break;
                            default: Invalid(); break;
                        }
                    }
                }
                Invalid();
                return null;
            }
            void Value(int depth, bool isRoot, bool isProfile)
            {
                if (depth > 256) Invalid();
                Space();
                if (position == text.Length) Invalid();
                if (Take('{'))
                {
                    Space();
                    if (Take('}')) return;
                    do
                    {
                        Space(); string key = ReadString(); Space(); Need(':'); Space();
                        bool childProfile = isRoot && String.Equals(key, "profile", StringComparison.OrdinalIgnoreCase);
                        bool childCounter = isProfile && String.Equals(key, "profiles_created", StringComparison.OrdinalIgnoreCase);
                        if (childProfile && ++profiles != 1) Invalid();
                        if (childCounter && ++counters != 1) Invalid();
                        if (isProfile && String.Equals(key, "info_cache", StringComparison.OrdinalIgnoreCase) && ++caches != 1) Invalid();
                        int start = position;
                        Value(depth + 1, false, childProfile);
                        if (childCounter) counter = new CounterSpan { Start = start, Length = position - start };
                        Space();
                    } while (Take(','));
                    Need('}');
                }
                else if (Take('['))
                {
                    Space();
                    if (Take(']')) return;
                    do { Value(depth + 1, false, false); Space(); } while (Take(','));
                    Need(']');
                }
                else if (text[position] == '"') ReadString();
                else if (text[position] == 't') Literal("true");
                else if (text[position] == 'f') Literal("false");
                else if (text[position] == 'n') Literal("null");
                else Number();
            }
        }
    }
}

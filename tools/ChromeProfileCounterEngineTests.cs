using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using AIProjects.Dependencies;

static class ChromeProfileCounterEngineTests
{
    const string Original = "{\"profile\":{\"profiles_created\":4,\"info_cache\":{\"Profile 3\":{}}},\"other\":\"keep this\"}";
    static int checks;

    static void Require(bool condition, string description)
    {
        ++checks;
        if (!condition) throw new Exception(description);
    }

    static T Throws<T>(Action action, string description) where T : Exception
    {
        ++checks;
        try { action(); }
        catch (T exception) { return exception; }
        throw new Exception(description);
    }

    sealed class Fixture : IDisposable
    {
        public readonly string Root;
        public readonly string State;
        public readonly string Backups;
        readonly string parent;
        public Fixture()
        {
            parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            Root = Path.Combine(parent, "AIProjects-ChromeEngine-" + Guid.NewGuid().ToString("N"));
            State = Path.Combine(Root, "Local State");
            Backups = Path.Combine(Root, "Profile Counter Backups");
            Directory.CreateDirectory(Root);
        }
        public ChromeProfileCounterEngine Engine(Func<bool> observation = null,
            ChromeProfileCounterFileSystem files = null, int wait = 5000)
        { return new ChromeProfileCounterEngine(Root, Backups, observation ?? (() => false), files, wait); }
        public void Write(string text = Original) { File.WriteAllText(State, text, new UTF8Encoding(false)); }
        public void NoTemporaryFiles()
        { Require(Directory.GetFiles(Root, ".Local State.*.tmp").Length == 0, "Temporary Local State file leaked."); }
        public void NoBackups()
        { Require(!Directory.Exists(Backups) || Directory.GetFiles(Backups).Length == 0, "Rejected change created a backup."); }
        public void Unchanged(string expected = Original)
        { Require(File.ReadAllText(State) == expected, "Rejected change replaced Local State."); NoTemporaryFiles(); NoBackups(); }
        public void Dispose()
        {
            string resolved = Path.GetFullPath(Root);
            if (!resolved.StartsWith(parent, StringComparison.OrdinalIgnoreCase) ||
                Path.GetFileName(resolved).IndexOf("AIProjects-ChromeEngine-", StringComparison.Ordinal) != 0 ||
                Path.GetDirectoryName(resolved) + Path.DirectorySeparatorChar != parent)
                throw new IOException("Refusing cleanup outside the generated fixture directory.");
            if (Directory.Exists(resolved)) Directory.Delete(resolved, true);
        }
    }

    sealed class FaultFiles : ChromeProfileCounterFileSystem
    {
        public bool FailWrite;
        public bool FailReplace;
        public bool FailEnumeration;
        public bool SimulateBackupCollision;
        public Action AfterWrite;
        public Action AfterCreateDirectory;
        public int ReplaceCalls;
        public override void WriteTemporary(string path, string text)
        {
            base.WriteTemporary(path, text);
            if (AfterWrite != null) AfterWrite();
            if (FailWrite) throw new IOException("Injected partial temporary-write failure.");
        }
        public override IEnumerable<string> EnumerateDirectories(string path)
        {
            if (FailEnumeration) throw new UnauthorizedAccessException("Injected inaccessible profile directory.");
            return base.EnumerateDirectories(path);
        }
        public override bool FileExists(string path)
        { return SimulateBackupCollision || base.FileExists(path); }
        public override void CreateDirectory(string path)
        { base.CreateDirectory(path); if (AfterCreateDirectory != null) AfterCreateDirectory(); }
        public override void Replace(string temporary, string destination, string backup)
        {
            ++ReplaceCalls;
            if (FailReplace) throw new IOException("Injected replacement failure.");
            base.Replace(temporary, destination, backup);
        }
    }

    static void PureModel()
    {
        Require(ChromeProfileCounterEngine.GetCounter(Original) == 4, "The oracle fixture counter changed.");
        Require(ChromeProfileCounterEngine.SetCounterInText(Original, 8) == Original.Replace("\"profiles_created\":4", "\"profiles_created\":8"), "The oracle fixture must change only its digit span.");
        Require(ChromeProfileCounterEngine.GetRegisteredProfiles(Original).SetEquals(new[] { 3 }), "The oracle fixture registration was lost.");
        Require(ChromeProfileCounterEngine.GetRegisteredProfiles("{\"profile\":{}}").Count == 0, "Absent info_cache must mean no registered numbered profiles.");
        Require(ChromeProfileCounterEngine.GetRegisteredProfiles("{\"profile\":{\"info_cache\":null}}").Count == 0, "Null info_cache must preserve old behavior.");
        Require(ChromeProfileCounterEngine.GetRegisteredProfiles("{\"profile\":{\"info_cache\":{\"profile 12\":{},\"Default\":{},\"Profile 002\":{}}}}").SetEquals(new[] { 12, 2 }), "Registered names must retain PowerShell's case-insensitive numbered-name match.");
        Require(ChromeProfileCounterEngine.GetCounter("{\"PROFILE\":{\"profiles_created\":0}}") == 0, "Reading a zero counter and case-insensitive profile lookup must preserve the oracle behavior.");
        Require(ChromeProfileCounterEngine.GetCounter("{\"profile\":{\"profiles_created\":2147483647}}") == Int32.MaxValue, "Maximum counter cannot be rounded.");
        Require(ChromeProfileCounterEngine.SetCounterInText("{\"pro\\u0066ile\":{\"profiles_created\":4}}", 8) == "{\"pro\\u0066ile\":{\"profiles_created\":8}}", "Decoded profile-key matching must retain the original key bytes.");

        string unicode = " {\r\n\"other\": [true,false,null,-1.5e+2,\"日本語 😀 \\\" \\u0061 \\\\\\/\\b\\f\\n\\r\\t\"],\r\n\"profile\":{\"profiles_created\" : 4, \"info_cache\":{}} }\t";
        string prefix = unicode.Substring(0, unicode.IndexOf(" : 4", StringComparison.Ordinal) + 3);
        string suffix = unicode.Substring(prefix.Length + 1);
        foreach (int number in new[] { 1, 8, 10, 255, 9999, Int32.MaxValue })
            Require(ChromeProfileCounterEngine.SetCounterInText(unicode, number) == prefix + number + suffix, "Escapes, non-ASCII text, whitespace, and unrelated values must remain byte-for-byte equal as text.");

        string[] invalid = {
            "", "{invalid", "null", "[]", "{}", "{\"profile\":[]}",
            "{\"profile\":{\"profiles_created\":-1}}", "{\"profile\":{\"profiles_created\":4.0}}",
            "{\"profile\":{\"profiles_created\":4e0}}", "{\"profile\":{\"profiles_created\":\"4\"}}",
            "{\"profile\":{\"profiles_created\":true}}", "{\"profile\":{\"profiles_created\":2147483648}}",
            "{\"profile\":{\"profiles_created\":4},\"other\":{\"profiles_created\":9}}",
            "{\"profile\":{\"profiles_created\":4,\"profiles_created\":4}}",
            "{\"profile\":{\"profiles_created\":4,\"Profiles_Created\":4}}",
            "{\"profile\":{\"profiles_created\":4},\"PROFILE\":{}}",
            "{\"profile\":{\"profiles\\u005fcreated\":4},\"other\":{\"profiles_created\":4}}",
            "{\"profile\":{\"profiles_created\":4,\"profiles\\u005fcreated\":4}}",
            "{\"profile\":{\"profiles\\u005fcreated\":4,\"profiles_created\":4}}",
            "{\"profile\":{},\"pro\\u0066ile\":{\"profiles_created\":4}}",
            "{\"profile\":{\"profiles_created\":4},\"profile\":{\"profiles\\u005fcreated\":4}}",
            "{\"profile\":{\"profiles_created\":4.0},\"other\":{\"profiles_created\":4}}",
            "{\"profile\":{\"profiles_created\":4,\"info_cache\":{\"Profile 8\":{}},\"info\\u005fcache\":{}}}",
            "{\"profile\":{\"other\":{\"profiles_created\":4}}}",
            "{\"profile\":{\"profiles_created\":04}}", "{\"profile\":{\"profiles_created\":+4}}",
            "{'profile':{'profiles_created':4}}", "{profile:{\"profiles_created\":4}}",
            "{\"profile\":{\"profiles_created\":4,}}", "{\"profile\":{\"profiles_created\":4},}",
            "{\"profile\":{\"profiles_created\":4},\"a\":[1,]}",
            "{\"profile\":{/*comment*/\"profiles_created\":4}}",
            "{\"profile\":{\"profiles_created\":4},\"a\":NaN}",
            "{\"profile\":{\"profiles_created\":4},\"a\":1.}",
            "{\"profile\":{\"profiles_created\":4},\"a\":1e+}",
            "{\"profile\":{\"profiles_created\":4},\"a\":\"\\x41\"}",
            "{\"profile\":{\"profiles_created\":4},\"a\":\"\\u0x00\"}",
            "{\"profile\":{\"profiles_created\":4},\"a\":\"raw\nnewline\"}",
            "{\"profile\":{\"profiles_created\":4}} trailing"
        };
        foreach (string text in invalid)
            Throws<InvalidDataException>(() => ChromeProfileCounterEngine.GetCounter(text), "Invalid/ambiguous JSON was accepted: " + text);
        Throws<ArgumentNullException>(() => ChromeProfileCounterEngine.GetCounter(null), "Null input was accepted.");
        Throws<ArgumentOutOfRangeException>(() => ChromeProfileCounterEngine.SetCounterInText(Original, 0), "Zero replacement was accepted.");
        Throws<ArgumentOutOfRangeException>(() => ChromeProfileCounterEngine.SetCounterInText(Original, -1), "Negative replacement was accepted.");
        Throws<InvalidDataException>(() => ChromeProfileCounterEngine.GetRegisteredProfiles("{\"profile\":{\"info_cache\":[]}}"), "A malformed registration cache must not become an empty set.");
        Throws<InvalidDataException>(() => ChromeProfileCounterEngine.GetRegisteredProfiles("{\"profile\":{\"info_cache\":{\"Profile 2147483648\":{}}}}"), "An overflowing registered number was silently ignored.");
        Throws<InvalidDataException>(() => ChromeProfileCounterEngine.GetCounter("{\"profile\":{\"profiles_created\":4},\"deep\":" + new string('[', 260) + "0" + new string(']', 260) + "}"), "Unbounded JSON recursion was accepted.");
    }

    static void DiskModel()
    {
        using (var f = new Fixture())
        {
            foreach (string name in new[] { "Default", "Profile 1", "Profile 002", "Profile 7", "Profile 2147483648", "profile 5", "Profile 8 extra" })
                Directory.CreateDirectory(Path.Combine(f.Root, name));
            File.WriteAllText(Path.Combine(f.Root, "Profile 6"), "not a directory");
            var engine = f.Engine();
            Require(engine.GetDiskProfiles().SetEquals(new[] { 1, 2, 7 }), "Directory enumeration must preserve numbered-name filtering and ignore files/overflow.");
            Require(engine.GetFirstSafeNumber(Original) == 4, "First free number must combine disk and registration sets.");
            Throws<FileNotFoundException>(() => engine.ReadStateText(), "Missing Local State must fail without being synthesized.");
            Require(!File.Exists(f.State), "Read-only inspection created Local State.");
            Require(!Directory.Exists(f.Backups), "Read-only inspection created backups.");
            Throws<ArgumentNullException>(() => new ChromeProfileCounterEngine(f.Root, f.Backups, null), "Implicit process observation must not be allowed.");
            Throws<ArgumentOutOfRangeException>(() => new ChromeProfileCounterEngine(f.Root, f.Backups, () => false, null, -1), "An unbounded mutex wait was accepted.");
        }
    }

    static void EncodingAndBackup()
    {
        foreach (Encoding encoding in new Encoding[] { new UTF8Encoding(false), new UTF8Encoding(true), Encoding.Unicode, Encoding.BigEndianUnicode, new UTF32Encoding(false, true), new UTF32Encoding(true, true) })
        using (var f = new Fixture())
        {
            string text = Original.Replace("keep this", "日本語 😀 keep this");
            byte[] originalBytes = encoding.GetPreamble().Concat(encoding.GetBytes(text)).ToArray();
            File.WriteAllBytes(f.State, originalBytes);
            var engine = f.Engine();
            string reviewed = engine.ReadStateText();
            Require(reviewed == text, "BOM-aware decoding changed reviewed text.");
            string backup = engine.ChangeCounter(reviewed, 8);
            Require(Path.GetDirectoryName(backup) == f.Backups && Path.GetFileName(backup).StartsWith("Local State.", StringComparison.Ordinal), "Backup was not stored under the explicit backup directory.");
            Require(File.ReadAllBytes(backup).SequenceEqual(originalBytes), "Backup lost exact original encoding/BOM bytes.");
            byte[] expected = new UTF8Encoding(false, true).GetBytes(text.Replace("\"profiles_created\":4", "\"profiles_created\":8"));
            Require(File.ReadAllBytes(f.State).SequenceEqual(expected), "Published file must retain the oracle's UTF-8-without-BOM output and exact text replacement.");
            string secondBackup = engine.ChangeCounter(engine.ReadStateText(), 9);
            Require(secondBackup != backup, "Consecutive edits reused a backup filename.");
            Require(File.ReadAllBytes(secondBackup).SequenceEqual(expected), "Second backup does not contain the immediately replaced file.");
            Require(File.ReadAllBytes(backup).SequenceEqual(originalBytes), "Second edit overwrote the earlier backup.");
            f.NoTemporaryFiles();
        }
    }

    static void InvalidEncoding()
    {
        var encodings = new Encoding[] { new UTF8Encoding(false), new UTF8Encoding(true), Encoding.Unicode, Encoding.BigEndianUnicode, new UTF32Encoding(false, true), new UTF32Encoding(true, true) };
        byte[][] invalidUnits = { new byte[] { 0xFF }, new byte[] { 0xC3, 0x28 }, new byte[] { 0x00, 0xD8 }, new byte[] { 0xD8, 0x00 }, new byte[] { 0x00, 0x00, 0x11, 0x00 }, new byte[] { 0x00, 0x11, 0x00, 0x00 } };
        int offset = Original.IndexOf("keep this", StringComparison.Ordinal);
        for (int n = 0; n < encodings.Length; ++n)
        using (var f = new Fixture())
        {
            Encoding encoding = encodings[n];
            byte[] bytes = encoding.GetPreamble().Concat(encoding.GetBytes(Original.Substring(0, offset)))
                .Concat(invalidUnits[n]).Concat(encoding.GetBytes(Original.Substring(offset + "keep this".Length))).ToArray();
            File.WriteAllBytes(f.State, bytes);
            var engine = f.Engine();
            Throws<DecoderFallbackException>(() => engine.ReadStateText(), "Undecodable state was silently normalized while reading.");
            Throws<DecoderFallbackException>(() => engine.ChangeCounter(Original, 8), "Undecodable changed state was silently normalized while repairing.");
            Require(File.ReadAllBytes(f.State).SequenceEqual(bytes), "Encoding rejection altered original bytes.");
            f.NoTemporaryFiles(); f.NoBackups();
        }
    }

    static void RejectionsAndCleanup()
    {
        using (var f = new Fixture())
        {
            f.Write();
            var files = new FaultFiles();
            Throws<IOException>(() => f.Engine(() => true, files).ChangeCounter(Original, 8), "Already running browser was ignored.");
            Require(files.ReplaceCalls == 0, "Browser guard reached file replacement.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write(); int observations = 0;
            Throws<IOException>(() => f.Engine(() => ++observations > 1).ChangeCounter(Original, 8), "Browser restart while staging was ignored.");
            Require(observations == 2, "Browser state was not rechecked immediately after staging.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write(); bool running = false;
            var files = new FaultFiles { AfterCreateDirectory = () => running = true };
            Throws<IOException>(() => f.Engine(() => running, files).ChangeCounter(Original, 8), "A restart during backup preparation reached replacement.");
            Require(files.ReplaceCalls == 0, "The final browser-state check ran too early.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write(); string changed = Original.Replace("keep this", "late external edit");
            var files = new FaultFiles { AfterCreateDirectory = () => File.WriteAllText(f.State, changed) };
            Throws<IOException>(() => f.Engine(null, files).ChangeCounter(Original, 8), "A state change during backup preparation reached replacement.");
            Require(files.ReplaceCalls == 0, "The final reviewed-text check ran too early.");
            f.Unchanged(changed);
        }
        using (var f = new Fixture())
        {
            f.Write();
            Throws<InvalidOperationException>(() => f.Engine(() => { throw new InvalidOperationException("Unavailable observation"); }).ChangeCounter(Original, 8), "Observation errors must not imply a stopped browser.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            string changed = Original.Replace("keep this", "changed elsewhere"); f.Write(changed);
            Throws<IOException>(() => f.Engine().ChangeCounter(Original, 8), "Stale reviewed snapshot was accepted.");
            f.Unchanged(changed);
        }
        using (var f = new Fixture())
        {
            f.Write();
            var files = new FaultFiles { AfterWrite = () => File.WriteAllText(f.State, Original + " ") };
            Throws<IOException>(() => f.Engine(null, files).ChangeCounter(Original, 8), "A concurrent state change while staging was accepted.");
            f.Unchanged(Original + " ");
        }
        using (var f = new Fixture())
        {
            f.Write(); Directory.CreateDirectory(Path.Combine(f.Root, "Profile 8"));
            Throws<IOException>(() => f.Engine().ChangeCounter(Original, 8), "An occupied directory was accepted.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write();
            var files = new FaultFiles { AfterWrite = () => Directory.CreateDirectory(Path.Combine(f.Root, "Profile 8")) };
            Throws<IOException>(() => f.Engine(null, files).ChangeCounter(Original, 8), "A newly occupied directory was accepted.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write();
            Throws<IOException>(() => f.Engine().ChangeCounter(Original, 3), "An occupied registered profile was accepted.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write(); var files = new FaultFiles { FailWrite = true };
            Throws<IOException>(() => f.Engine(null, files).ChangeCounter(Original, 8), "Partial temporary-write failure was hidden.");
            f.Unchanged();
            files.FailWrite = false;
            Require(File.Exists(f.Engine(null, files).ChangeCounter(Original, 8)), "Failure leaked the writer mutex.");
            f.NoTemporaryFiles();
        }
        using (var f = new Fixture())
        {
            f.Write();
            Throws<UnauthorizedAccessException>(() => f.Engine(null, new FaultFiles { FailEnumeration = true }).ChangeCounter(Original, 8), "Unreadable disk occupancy became an empty set.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write(); File.WriteAllText(f.Backups, "blocks backup directory creation");
            Throws<IOException>(() => f.Engine().ChangeCounter(Original, 8), "Backup directory creation failure was ignored.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write();
            Throws<IOException>(() => f.Engine(null, new FaultFiles { SimulateBackupCollision = true }).ChangeCounter(Original, 8), "An existing backup path was overwritten.");
            f.Unchanged();
        }
        using (var f = new Fixture())
        {
            f.Write();
            Throws<IOException>(() => f.Engine(null, new FaultFiles { FailReplace = true }).ChangeCounter(Original, 8), "Replacement failure was hidden.");
            f.Unchanged();
        }
    }

    static void MutexContentionAndAbandonment()
    {
        using (var f = new Fixture())
        using (var ready = new ManualResetEvent(false))
        using (var release = new ManualResetEvent(false))
        {
            f.Write(); Exception workerError = null;
            var worker = new Thread(() =>
            {
                try
                {
                    using (var mutex = new Mutex(false, ChromeProfileCounterEngine.MutationMutexName(f.State)))
                    {
                        bool locked = mutex.WaitOne(1000);
                        if (!locked) throw new IOException("Fixture mutex was unexpectedly busy.");
                        try { ready.Set(); if (!release.WaitOne(5000)) throw new TimeoutException("Fixture release timed out."); }
                        finally { mutex.ReleaseMutex(); }
                    }
                }
                catch (Exception ex) { workerError = ex; ready.Set(); }
            });
            worker.IsBackground = true; worker.Start();
            try
            {
                Require(ready.WaitOne(3000), "Writer fixture did not acquire its mutex.");
                if (workerError != null) throw workerError;
                Throws<IOException>(() => f.Engine(null, null, 50).ChangeCounter(Original, 8), "A concurrent tool writer was not refused within the bound.");
                f.Unchanged();
            }
            finally { release.Set(); if (!worker.Join(3000)) throw new TimeoutException("Writer fixture failed to stop."); }
            if (workerError != null) throw workerError;
            Require(File.Exists(f.Engine().ChangeCounter(Original, 8)), "Released writer mutex remained blocked.");
        }
        using (var f = new Fixture())
        {
            f.Write(); Mutex abandoned = null; Exception workerError = null;
            var worker = new Thread(() =>
            {
                try
                {
                    abandoned = new Mutex(false, ChromeProfileCounterEngine.MutationMutexName(f.State));
                    if (!abandoned.WaitOne(1000)) throw new IOException("Abandonment fixture mutex was unexpectedly busy.");
                    // Deliberately terminate only this test thread while it owns
                    // the fixture's mutex; no child process is launched.
                }
                catch (Exception ex) { workerError = ex; }
            });
            worker.IsBackground = true; worker.Start();
            try
            {
                Require(worker.Join(3000), "Abandonment fixture did not finish.");
                if (workerError != null) throw workerError;
                Require(File.Exists(f.Engine().ChangeCounter(Original, 8)), "An abandoned tool mutex was not recovered.");
                f.NoTemporaryFiles();
            }
            finally { if (abandoned != null) abandoned.Dispose(); }
        }
    }

    static int Main()
    {
        try
        {
            PureModel(); DiskModel();
            if (Environment.OSVersion.Platform != PlatformID.Win32NT)
            {
                Console.WriteLine("Passed " + checks + " portable model checks; Windows replacement/mutex checks skipped.");
                return 0;
            }
            EncodingAndBackup(); InvalidEncoding(); RejectionsAndCleanup(); MutexContentionAndAbandonment();
            Console.WriteLine("Passed " + checks + " ChromeProfileCounter engine checks; only generated fixture paths and injected browser observations were used.");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}

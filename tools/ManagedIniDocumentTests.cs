using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using AIProjects.Dependencies;

// Every write is inside a new temporary directory. No application, Startup,
// registry, service, hardware, network or GUI operations are performed.
static class ManagedIniDocumentTests
{
    static int checks;

    static int Main(string[] args)
    {
        if (args.Length != 0) return 2;
        string temporary = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string root = Path.GetFullPath(Path.Combine(temporary, "AIProjects-IniDocument-" + Guid.NewGuid().ToString("N")));
        if (!String.Equals(Path.GetDirectoryName(root), temporary, StringComparison.OrdinalIgnoreCase)) return 2;
        using (var deadline = new Timer(delegate(object ignored)
        {
            Console.Error.WriteLine("INI document tests exceeded their 45-second deadline.");
            Environment.Exit(124);
        }, null, 45000, Timeout.Infinite))
        {
            Directory.CreateDirectory(root);
            try
            {
                TestSnapshots(root);
                TestPureSnapshotChanges(root);
                TestPreservationAndDialect(root);
                TestNameCompatibility(root);
                TestProspectiveValidation(root);
                TestFrozenInputs(root);
                TestEncodingAndRollback(root);
                TestBoundsAndWriteRetry(root);
                TestCooperatingWriters(root);
                Console.WriteLine("Managed INI document checks passed: " + checks);
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine(ex);
                return 1;
            }
            finally
            {
                if (!String.Equals(Path.GetDirectoryName(Path.GetFullPath(root)), temporary, StringComparison.OrdinalIgnoreCase))
                    throw new IOException("Refusing cleanup outside the fixture's temporary parent.");
                Directory.Delete(root, true);
            }
        }
    }

    static ManagedIniDocumentSpec Spec(string root, string name)
    {
        return new ManagedIniDocumentSpec
        {
            FilePath = Path.Combine(root, name), MutationWaitMilliseconds = 2500,
            DefaultContents = "; original\r\n[First]\r\nValue=old\r\n[Second]\r\nValue=old\r\n"
        };
    }

    static ManagedIniSetting Change(string section, string key, string value)
    {
        return new ManagedIniSetting(section, key, value);
    }

    static string Value(ManagedIniDocumentSnapshot snapshot, string section, string key)
    {
        string value;
        if (!snapshot.TryGetValue(section, key, out value)) throw new InvalidDataException("Missing " + section + "." + key);
        return value;
    }

    static void Save(ManagedIniDocumentSpec spec, params ManagedIniSetting[] changes)
    {
        string error;
        if (!ManagedIniFile.SaveDocumentBatch(spec, changes, out error)) throw new IOException(error);
    }

    static void TestSnapshots(string root)
    {
        var spec = Spec(root, "absent-parent/settings.ini");
        var parsed = ManagedIniFile.ParseDocumentText(spec.DefaultContents);
        var loaded = ManagedIniFile.LoadDocument(spec);
        Check(Value(parsed, "first", "VALUE") == "old" && Value(loaded, "second", "value") == "old",
            "pure parser and absent-file reader return effective defaults with case-insensitive names");
        Check(!Directory.Exists(Path.GetDirectoryName(spec.FilePath)), "reading an absent document creates no directory or INI");
        var section = loaded.GetSection("First");
        section["Value"] = "changed copy";
        Check(Value(loaded, "First", "Value") == "old" && loaded.GetSection("Missing").Count == 0,
            "snapshot sections are detached copies and missing sections are empty");
        var repeated = ManagedIniFile.ParseDocumentText("ignored preamble\n[Empty]\n[First]\nKey=one\n[first]\nKEY=two\n");
        Check(repeated.Sections.Count() == 2 && repeated.ContainsSection("empty") && Value(repeated, "First", "Key") == "two",
            "parser retains empty sections and merges repeated sections using the last assignment");
    }

    static void TestPureSnapshotChanges(string root)
    {
        var spec = Spec(root, "pure-transform-never-created/settings.ini");
        var original = ManagedIniFile.LoadDocument(spec);
        var changes = new List<ManagedIniSetting>
        {
            Change("FIRST", "VALUE", "intermediate"), Change("first", "Value", "together"),
            Change("Second", "Value", "together"), Change("Program:Example.With.Dots", "Path", "  quoted;#path\\  ")
        };
        var prospective = original.WithChanges(changes);
        Check(Value(prospective, "First", "Value") == "together" && Value(prospective, "Second", "Value") == "together" &&
            prospective.GetSection("First").Count == 1 && Value(prospective, "Program:Example.With.Dots", "Path") == "  quoted;#path\\  ",
            "pure transform merges all changes case-insensitively, takes the last duplicate and retains literal values");
        Check(Value(original, "First", "Value") == "old" && Value(original, "Second", "Value") == "old" &&
            !original.ContainsSection("Program:Example.With.Dots") && changes.Count == 4,
            "pure transform leaves the original snapshot and input change collection untouched");
        changes.Clear();
        prospective.GetSection("First")["Value"] = "modified detached copy";
        var further = prospective.WithChanges(new[] { Change("First", "Value", "later") });
        Check(Value(prospective, "First", "Value") == "together" && Value(further, "First", "Value") == "later" &&
            Value(original, "First", "Value") == "old", "pure transform freezes input edits and keeps successive snapshots independently immutable");
        var unchanged = prospective.WithChanges(null);
        Check(!Object.ReferenceEquals(unchanged, prospective) && Value(unchanged, "First", "Value") == "together" &&
            unchanged.Sections.Count() == prospective.Sections.Count(), "an empty pure transform returns a detached equivalent snapshot");
        Throws<ArgumentException>(delegate { original.WithChanges(new[] { Change(" Padded", "Value", "bad") }); },
            "pure transform uses the file batch's section-name validation");
        Throws<ArgumentException>(delegate { original.WithChanges(new[] { Change("First", "Bad=Key", "bad") }); },
            "pure transform uses the file batch's key validation");
        Throws<ArgumentException>(delegate { original.WithChanges(new[] { Change("First", "Value", "bad\nvalue") }); },
            "pure transform uses the file batch's single-line value validation");
        Throws<ArgumentException>(delegate { original.WithChanges(new[] { Change("First", "Value", null) }); },
            "pure transform rejects null values like the file batch");
        Throws<ArgumentException>(delegate { original.WithChanges(new ManagedIniSetting[] { null }); },
            "pure transform rejects null settings like the file batch");
        Check(!Directory.Exists(Path.GetDirectoryName(spec.FilePath)) && Value(original, "First", "Value") == "old",
            "successful and rejected pure transforms create no INI, temporary file or directory");
    }

    static void TestPreservationAndDialect(string root)
    {
        var spec = Spec(root, "preserve.ini");
        File.WriteAllText(spec.FilePath,
            "; leading comment\n[\"First\"] ; header comment\nValue=one ; assignment comment\nKeep=unmodified\n" +
            "[Unknown]\n; unknown comment\nVendor = vendor value\n[first] # repeated header\nVALUE=stale # duplicate comment\n" +
            "[Second]\nValue=old\n", new UTF8Encoding(false, true));
        Save(spec, Change("FIRST", "value", "#literal;equals=\"quoted\"\\tail"),
            Change("Second", "Value", "  亚洲 😀  "), Change("First", "Added", "C:\\logs\\"));
        var saved = ManagedIniFile.LoadDocument(spec);
        string text = File.ReadAllText(spec.FilePath);
        Check(Value(saved, "First", "Value") == "#literal;equals=\"quoted\"\\tail" &&
            Value(saved, "Second", "Value") == "  亚洲 😀  " && Value(saved, "First", "Added") == "C:\\logs\\",
            "one multi-section save round-trips punctuation, quotes, backslashes, significant spaces and Unicode");
        Check(text.Contains("; leading comment") && text.Contains("[\"First\"] ; header comment") &&
            text.Contains("# repeated header") && text.Contains("; assignment comment") && text.Contains("# duplicate comment") &&
            text.Contains("[Unknown]") && text.Contains("Vendor = vendor value") && text.Contains("Keep=unmodified"),
            "batch preserves standalone, header and assignment comments plus unknown sections and unchanged assignments");
        Check(saved.GetSection("First").Count == 3 && text.Split(new[] { "\"value\" =" }, StringSplitOptions.None).Length == 2 &&
            !text.Contains("VALUE=stale"), "edited keys are deduplicated across repeated section blocks");

        string dialect = "[\"Dialect\"] ; header\nBare=yes#comment\n'One'='it\\'s fine' # comment\n" +
            "Path=\"C:\\logs\\\" ; path comment\nTemplate=\"Line1\\nLine2\\r\\t\"\n" +
            "Literal=\"#topic;literal=equals\" ; real comment\nEscaped=\"say \\\"hello\\\"\"\n";
        var parsed = ManagedIniFile.ParseDocumentText(dialect);
        var sectionParsed = ManagedIniFile.ParseSectionText(dialect, "Dialect");
        Check(parsed.GetSection("Dialect").Count == sectionParsed.Count && sectionParsed.All(pair => Value(parsed, "Dialect", pair.Key) == pair.Value) &&
            Value(parsed, "Dialect", "One") == "it's fine" && Value(parsed, "Dialect", "Path") == "C:\\logs\\" &&
            Value(parsed, "Dialect", "Template") == "Line1\\nLine2\\r\\t", "document parser reuses the existing native-compatible quote, comment and template dialect");
        File.WriteAllText(spec.FilePath, dialect);
        Save(spec, Change("Dialect", "Path", "new path"), Change("Section.With.Dot", "Key.With.Dot", "value"),
            Change("Quoted'Name", "Back\\Slash", "named key"));
        saved = ManagedIniFile.LoadDocument(spec);
        Check(Value(saved, "Section.With.Dot", "Key.With.Dot") == "value" && Value(saved, "Quoted'Name", "Back\\Slash") == "named key" &&
            File.ReadAllText(spec.FilePath).Contains("; path comment"), "separate section and key names support dots, quoted sections and native backslash names");
    }

    static void TestNameCompatibility(string root)
    {
        var spec = Spec(root, "names.ini");
        var sectionSpec = new ManagedIniFileSpec { FilePath = spec.FilePath, SectionName = "Names" };
        string[] names = { "Key\\Path", "Trailing\\", "Quote\"Name", "Slash\\\"Name", "Unicode 亚洲", "normal.dot#;" };
        foreach (string name in names)
        {
            string error;
            Check(ManagedIniFile.SaveSectionBatch(sectionSpec,
                new Dictionary<string, string> { { name, "section value" } }, out error),
                "managed section writer supports native-compatible backslash, quote and punctuation names");
            var section = ManagedIniFile.ParseSectionText(File.ReadAllText(spec.FilePath), "Names");
            Check(section.ContainsKey(name) && section[name] == "section value" &&
                Value(ManagedIniFile.LoadDocument(spec), "Names", name) == "section value",
                "section-only and document readers agree on the exact serialized key name");
            Save(spec, Change("Names", name, "document value"));
            Check(ManagedIniFile.ParseSectionText(File.ReadAllText(spec.FilePath), "Names")[name] == "document value",
                "managed document writer updates the same section-written name without creating an escaped alias");
            Check(ManagedIniFile.SaveSectionBatch(sectionSpec,
                new Dictionary<string, string> { { name, "section retry" } }, out error) &&
                Value(ManagedIniFile.LoadDocument(spec), "Names", name) == "section retry",
                "managed section writer updates the same document-written name");
        }
        Check(ManagedIniFile.LoadDocument(spec).GetSection("Names").Count == names.Length,
            "mixed section/document name writes retain exactly one entry per key");
        const string oldName = "Old\\\\Name";
        File.AppendAllText(spec.FilePath, "\r\n[Names]\r\n\"Old\\\\Name\" = \"legacy\"\r\n");
        Save(spec, Change("Names", "Unrelated", "changed"));
        Check(Value(ManagedIniFile.LoadDocument(spec), "Names", oldName) == "legacy",
            "legacy name parsing keeps literal doubled backslashes after unrelated writes");
    }

    static void TestProspectiveValidation(string root)
    {
        var spec = Spec(root, "validate.ini");
        Save(spec, Change("First", "Value", "same"), Change("Second", "Value", "same"));
        int validations = 0;
        Action<ManagedIniDocumentSnapshot> validator = delegate(ManagedIniDocumentSnapshot prospective)
        {
            ++validations;
            if (Value(prospective, "First", "Value") != Value(prospective, "Second", "Value"))
                throw new InvalidDataException("Both values must agree.");
        };
        byte[] original = File.ReadAllBytes(spec.FilePath);
        DateTime originalWrite = File.GetLastWriteTimeUtc(spec.FilePath);
        string error;
        Check(!ManagedIniFile.SaveDocumentBatch(spec, new[] { Change("First", "Value", "next") }, validator, out error) &&
            error.Contains("Both values must agree") && validations == 1 && original.SequenceEqual(File.ReadAllBytes(spec.FilePath)) &&
            File.GetLastWriteTimeUtc(spec.FilePath) == originalWrite && Directory.GetFiles(root, ".AIProjects-*.tmp").Length == 0,
            "failed whole-document validation executes once and writes neither destination nor temporary file");
        Check(ManagedIniFile.SaveDocumentBatch(spec, new[] { Change("First", "Value", "next"), Change("Second", "Value", "next") },
            validator, out error) && validations == 2 && Value(ManagedIniFile.LoadDocument(spec), "Second", "Value") == "next",
            "interdependent edits validate together and commit as one document");

        var absent = Spec(root, "zero-write/absent.ini");
        bool sawNoWrites = false;
        Check(!ManagedIniFile.SaveDocumentBatch(absent, new[] { Change("First", "Value", "changed") }, delegate(ManagedIniDocumentSnapshot prospective)
        {
            sawNoWrites = !Directory.Exists(Path.GetDirectoryName(absent.FilePath)) && Value(prospective, "First", "Value") == "changed";
            throw new InvalidDataException("Deliberate rejection.");
        }, out error) && sawNoWrites && !Directory.Exists(Path.GetDirectoryName(absent.FilePath)),
            "rejected prospective creation never creates even its parent directory");
        byte[] beforeNoOp = File.ReadAllBytes(spec.FilePath);
        Check(ManagedIniFile.SaveDocumentBatch(spec, null, validator, out error) && validations == 3 &&
            beforeNoOp.SequenceEqual(File.ReadAllBytes(spec.FilePath)), "validation-only batch validates current contents without rewriting them");
    }

    static void TestFrozenInputs(string root)
    {
        var spec = Spec(root, "frozen.ini");
        string originalPath = spec.FilePath;
        string unrelated = Path.Combine(root, "unrelated.ini");
        File.WriteAllText(unrelated, "keep these bytes");
        var changes = new List<ManagedIniSetting> { Change("First", "Value", "first"), Change("Second", "Value", "second") };
        string error;
        Check(ManagedIniFile.SaveDocumentBatch(spec, changes, delegate(ManagedIniDocumentSnapshot prospective)
        {
            spec.FilePath = unrelated;
            spec.DefaultContents = "broken defaults";
            spec.MaximumBytes = 1;
            changes.Clear();
            changes.Add(Change("First", "Value", "redirected"));
            prospective.GetSection("First")["Value"] = "mutated snapshot copy";
            if (Value(prospective, "First", "Value") != "first") throw new InvalidDataException("Snapshot was mutable.");
        }, out error) && File.ReadAllText(unrelated) == "keep these bytes" &&
            Value(ManagedIniFile.ParseDocumentText(File.ReadAllText(originalPath)), "First", "Value") == "first",
            "caller mutations during validation cannot redirect the frozen path, limits or settings batch");
    }

    static void TestEncodingAndRollback(string root)
    {
        Encoding[] encodings = { Encoding.Unicode, Encoding.BigEndianUnicode, new UTF32Encoding(false, true, true), new UTF32Encoding(true, true, true) };
        for (int i = 0; i < encodings.Length; ++i)
        {
            var spec = Spec(root, "encoding-" + i + ".ini");
            File.WriteAllText(spec.FilePath, "; exact bytes\r\n[First]\r\nValue = 亚洲\r\n[Second]\r\nValue=old\r\n", encodings[i]);
            byte[] original = File.ReadAllBytes(spec.FilePath);
            string capturedPath = spec.FilePath;
            using (var transaction = ManagedIniFile.BeginTransaction(spec))
            {
                Check(Value(ManagedIniFile.LoadDocument(spec), "First", "Value") == "亚洲", "reader accepts BOM-marked encoding fixture " + i);
                Save(spec, Change("First", "Value", "changed"), Change("Second", "Value", "changed"));
                Check(File.ReadAllBytes(spec.FilePath).Take(3).SequenceEqual(new byte[] { 0xEF, 0xBB, 0xBF }), "multi-section edit canonicalizes encoding to UTF-8 BOM fixture " + i);
                spec.FilePath = Path.Combine(root, "not-the-captured-path.ini");
                string error;
                Check(transaction.TryRestoreOriginal(out error) && original.SequenceEqual(File.ReadAllBytes(capturedPath)),
                    "document transaction restores exact original encoding, comments and line endings despite changed spec " + i);
            }
        }
        var absent = Spec(root, "originally-absent.ini");
        using (var transaction = ManagedIniFile.BeginTransaction(absent))
        {
            Check(!File.Exists(absent.FilePath), "capturing an absent document creates no defaults");
            Save(absent, Change("First", "Value", "created"), Change("Second", "Value", "created"));
            string error;
            Check(transaction.TryRestoreOriginal(out error) && !File.Exists(absent.FilePath), "document transaction restores original absence after multi-section creation");
        }
        var invalid = Spec(root, "invalid-utf8.ini");
        File.WriteAllBytes(invalid.FilePath, new byte[] { 0xC3, 0x28 });
        Throws<DecoderFallbackException>(delegate { ManagedIniFile.LoadDocument(invalid); }, "document reader rejects malformed UTF-8 losslessly");
    }

    static void TestBoundsAndWriteRetry(string root)
    {
        var spec = Spec(root, "write-retry.ini");
        Save(spec, Change("First", "Value", "before"), Change("Second", "Value", "before"));
        byte[] before = File.ReadAllBytes(spec.FilePath);
        string error;
        using (var denyReplacement = new FileStream(spec.FilePath, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            Check(!ManagedIniFile.SaveDocumentBatch(spec, new[] { Change("First", "Value", "after"), Change("Second", "Value", "after") }, out error) &&
                !String.IsNullOrEmpty(error) && before.SequenceEqual(File.ReadAllBytes(spec.FilePath)) && Directory.GetFiles(root, ".AIProjects-*.tmp").Length == 0,
                "denied atomic replacement preserves original bytes and removes its temporary sibling");
        }
        Save(spec, Change("First", "Value", "after"), Change("Second", "Value", "after"));
        Check(Value(ManagedIniFile.LoadDocument(spec), "Second", "Value") == "after", "failed publication releases its lock and permits a successful retry");
        spec.MaximumBytes = 1024;
        byte[] bounded = File.ReadAllBytes(spec.FilePath);
        Check(!ManagedIniFile.SaveDocumentBatch(spec, new[] { Change("First", "Large", new string('x', 2048)) }, out error) &&
            bounded.SequenceEqual(File.ReadAllBytes(spec.FilePath)), "prospective encoded byte limit rejects oversized output without replacement");
        var tooLargeDefault = Spec(root, "no-encoded-write/absent.ini");
        tooLargeDefault.MaximumBytes = 1024;
        tooLargeDefault.DefaultContents = "[First]\nLarge=" + new string('x', 2048);
        Check(!ManagedIniFile.SaveDocumentBatch(tooLargeDefault, null, out error) && !Directory.Exists(Path.GetDirectoryName(tooLargeDefault.FilePath)),
            "oversized defaults are rejected in memory before directory or file creation");
        tooLargeDefault.DefaultContents = "[First]\nValue=\uD800";
        Check(!ManagedIniFile.SaveDocumentBatch(tooLargeDefault, null, out error) && !Directory.Exists(Path.GetDirectoryName(tooLargeDefault.FilePath)),
            "invalid Unicode defaults are rejected in memory before any filesystem write");
        Check(!ManagedIniFile.SaveDocumentBatch(spec, new[] { Change("First", "Bad=Key", "value") }, out error) &&
            bounded.SequenceEqual(File.ReadAllBytes(spec.FilePath)), "invalid keys are rejected before acquiring a mutation scope");
        File.WriteAllText(spec.FilePath, "[First]\nValue=before\n[Unknown]\nbroken assignment\n");
        byte[] malformed = File.ReadAllBytes(spec.FilePath);
        Check(!ManagedIniFile.SaveDocumentBatch(spec, new[] { Change("First", "Value", "after") }, out error) && malformed.SequenceEqual(File.ReadAllBytes(spec.FilePath)),
            "whole-document save reports malformed assignments in any named section instead of silently repairing them");
    }

    static void TestCooperatingWriters(string root)
    {
        var spec = Spec(root, "concurrent.ini");
        Save(spec, Change("First", "Value", "0"), Change("Second", "Value", "0"));
        var sectionSpec = new ManagedIniFileSpec { FilePath = spec.FilePath, SectionName = "Other", MutationWaitMilliseconds = 2500 };
        Exception batchFailure = null, sectionFailure = null, readerFailure = null;
        int observed = 0;
        using (var start = new ManualResetEvent(false))
        using (var held = new ManualResetEvent(false))
        using (var finished = new ManualResetEvent(false))
        {
            var batchWriter = new Thread(delegate()
            {
                try
                {
                    start.WaitOne();
                    for (int i = 1; i <= 40; ++i) Save(spec, Change("First", "Value", i.ToString()), Change("Second", "Value", i.ToString()));
                }
                catch (Exception ex) { batchFailure = ex; }
            });
            var sectionWriter = new Thread(delegate()
            {
                held.Set();
                try
                {
                    start.WaitOne();
                    for (int i = 1; i <= 40; ++i)
                    {
                        string error;
                        if (!ManagedIniFile.SaveSectionBatch(sectionSpec, new Dictionary<string, string> { { "Counter", i.ToString() } }, out error))
                            throw new IOException(error);
                    }
                }
                catch (Exception ex) { sectionFailure = ex; }
                finally { finished.Set(); }
            });
            var reader = new Thread(delegate()
            {
                try
                {
                    start.WaitOne();
                    for (int i = 0; i < 160; ++i)
                    {
                        // One handle pins either version while publication
                        // replaces the directory entry. This reader does not
                        // acquire the helper mutex and can catch split batches.
                        var snapshot = ReadDirectSnapshot(spec.FilePath);
                        if (Value(snapshot, "First", "Value") != Value(snapshot, "Second", "Value"))
                            throw new InvalidDataException("Observed a partial multi-section batch.");
                        ++observed;
                    }
                }
                catch (Exception ex) { readerFailure = ex; }
            });
            batchWriter.IsBackground = sectionWriter.IsBackground = reader.IsBackground = true;
            using (var transaction = ManagedIniFile.BeginTransaction(spec))
            {
                sectionWriter.Start();
                Check(held.WaitOne(1000), "section writer starts while a document transaction owns the path mutex");
                start.Set();
                Check(!finished.WaitOne(100), "existing section writer waits on the document transaction's same mutex");
            }
            batchWriter.Start();
            reader.Start();
            Check(batchWriter.Join(7000) && sectionWriter.Join(7000) && reader.Join(7000), "all cooperating writers and the direct reader finish within bounded waits");
        }
        if (batchFailure != null) throw new IOException("Batch writer failed.", batchFailure);
        if (sectionFailure != null) throw new IOException("Section writer failed.", sectionFailure);
        if (readerFailure != null) throw new IOException("Direct reader failed.", readerFailure);
        var saved = ManagedIniFile.LoadDocument(spec);
        Check(observed == 160 && Value(saved, "First", "Value") == "40" && Value(saved, "Second", "Value") == "40" &&
            Value(saved, "Other", "Counter") == "40", "cooperating section and document writers retain each other's edits, and direct readers observe only complete batches");
    }

    static ManagedIniDocumentSnapshot ReadDirectSnapshot(string path)
    {
        for (int attempt = 0; ; ++attempt)
        {
            try
            {
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete))
                using (var text = new StreamReader(stream, new UTF8Encoding(false, true), true))
                    return ManagedIniFile.ParseDocumentText(text.ReadToEnd());
            }
            catch (IOException ex)
            {
                // Windows replacement can briefly prevent an uncoordinated
                // pathname open. A bounded retry does not relax the assertion
                // that every successfully opened file is one whole version.
                int code = ex.HResult & 0xFFFF;
                if (attempt >= 49 || (code != 2 && code != 32 && code != 33)) throw;
                Thread.Sleep(1);
            }
        }
    }

    static void Throws<T>(Action action, string label) where T : Exception
    {
        try { action(); }
        catch (T) { Check(true, label); return; }
        throw new InvalidOperationException("FAIL: " + label);
    }

    static void Check(bool passed, string label)
    {
        if (!passed) throw new InvalidOperationException("FAIL: " + label);
        ++checks;
        Console.WriteLine("ok - " + label);
    }
}

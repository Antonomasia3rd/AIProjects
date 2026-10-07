using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using AIProjects.Dependencies;

// No Startup, COM, GUI or application operations. Every file belongs to one
// new temporary fixture directory; competing writers use the real INI helper.
static class ManagedIniTransactionTests
{
    static int checks;

    static int Main(string[] args)
    {
        if (args.Length != 0) return 2;
        string temporary = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string root = Path.GetFullPath(Path.Combine(temporary, "AIProjects-IniTransaction-" + Guid.NewGuid().ToString("N")));
        if (!String.Equals(Path.GetDirectoryName(root), temporary, StringComparison.OrdinalIgnoreCase)) return 2;
        using (var deadline = new Timer(delegate(object ignored)
        {
            Console.Error.WriteLine("INI transaction tests exceeded their 45-second deadline.");
            Environment.Exit(124);
        }, null, 45000, Timeout.Infinite))
        {
            Directory.CreateDirectory(root);
            try
            {
                TestExactRestoration(root);
                TestAbsentRestoration(root);
                TestCompetingWriter(root);
                TestCaptureFailures(root);
                TestRestoreFailureAndLifetime(root);
                Console.WriteLine("Managed INI transaction checks passed: " + checks);
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

    static ManagedIniFileSpec Spec(string root, string name)
    {
        return new ManagedIniFileSpec
        {
            FilePath = Path.Combine(root, name), SectionName = "Settings",
            DefaultContents = "[Settings]\r\nValue=default\r\n", MutationWaitMilliseconds = 1500
        };
    }

    static void Save(ManagedIniFileSpec spec, string value)
    {
        string error;
        if (!ManagedIniFile.SaveSectionBatch(spec, new Dictionary<string, string> { { "Value", value } }, out error))
            throw new IOException(error);
    }

    static void TestExactRestoration(string root)
    {
        var spec = Spec(root, "utf16.ini");
        File.WriteAllText(spec.FilePath, "; keep exact encoding and spacing\r\n[Settings]\r\nValue = original\r\n", Encoding.Unicode);
        byte[] original = File.ReadAllBytes(spec.FilePath);
        using (var transaction = ManagedIniFile.BeginTransaction(spec))
        {
            Save(spec, "modified");
            Check(ManagedIniFile.LoadSection(spec, false)["Value"] == "modified",
                "Save and Load reenter the transaction's existing mutex");
            Check(!original.SequenceEqual(File.ReadAllBytes(spec.FilePath)), "fixture write changes the original bytes");
            string error;
            Check(transaction.TryRestoreOriginal(out error) && original.SequenceEqual(File.ReadAllBytes(spec.FilePath)),
                "rollback restores the exact UTF-16 BOM, comments, spacing and line endings");
        }
        using (var transaction = ManagedIniFile.BeginTransaction(spec)) Save(spec, "committed");
        Check(ManagedIniFile.LoadSection(spec, false)["Value"] == "committed",
            "Dispose releases a successful transaction without an implicit rollback");

        string capturedPath = spec.FilePath;
        string unrelatedPath = Path.Combine(root, "unrelated.ini");
        File.WriteAllText(unrelatedPath, "unrelated bytes");
        byte[] capturedBytes = File.ReadAllBytes(capturedPath);
        using (var transaction = ManagedIniFile.BeginTransaction(spec))
        {
            Save(spec, "changed again");
            spec.FilePath = unrelatedPath;
            string error;
            Check(transaction.TryRestoreOriginal(out error) && capturedBytes.SequenceEqual(File.ReadAllBytes(capturedPath)) &&
                File.ReadAllText(unrelatedPath) == "unrelated bytes", "rollback freezes its captured path despite a mutated caller specification");
        }
    }

    static void TestAbsentRestoration(string root)
    {
        var spec = Spec(root, "absent.ini");
        string sibling = Path.Combine(root, "keep-sibling.txt");
        File.WriteAllText(sibling, "keep");
        using (var transaction = ManagedIniFile.BeginTransaction(spec))
        {
            Check(!File.Exists(spec.FilePath), "beginning a missing-file transaction does not create a default profile");
            Save(spec, "created");
            string error;
            Check(File.Exists(spec.FilePath) && transaction.TryRestoreOriginal(out error) && !File.Exists(spec.FilePath),
                "rollback restores original absence after a new profile was written");
            Check(File.ReadAllText(sibling) == "keep", "absent rollback removes only the configured file");
            Check(transaction.TryRestoreOriginal(out error), "restoring original absence is repeatable");
            Directory.CreateDirectory(spec.FilePath);
            Check(!transaction.TryRestoreOriginal(out error) && Directory.Exists(spec.FilePath),
                "absent rollback refuses to remove a directory that replaced the configured file");
            Directory.Delete(spec.FilePath);
        }
    }

    static void TestCompetingWriter(string root)
    {
        var spec = Spec(root, "concurrent.ini");
        File.WriteAllText(spec.FilePath, "; original\n[Settings]\nValue=before\n");
        byte[] original = File.ReadAllBytes(spec.FilePath);
        Exception workerError = null;
        using (var started = new ManualResetEvent(false))
        using (var finished = new ManualResetEvent(false))
        {
            var writer = new Thread(delegate()
            {
                started.Set();
                try { Save(spec, "later writer"); }
                catch (Exception ex) { workerError = ex; }
                finally { finished.Set(); }
            });
            writer.IsBackground = true;
            try
            {
                using (var transaction = ManagedIniFile.BeginTransaction(spec))
                {
                    Save(spec, "transaction write");
                    var impatientSpec = Spec(root, "concurrent.ini");
                    impatientSpec.MutationWaitMilliseconds = 50;
                    bool timedOut = false;
                    Exception impatientError = null;
                    var impatient = new Thread(delegate()
                    {
                        try { using (ManagedIniFile.BeginTransaction(impatientSpec)) { } }
                        catch (TimeoutException) { timedOut = true; }
                        catch (Exception ex) { impatientError = ex; }
                    });
                    impatient.IsBackground = true;
                    impatient.Start();
                    Check(impatient.Join(1500) && timedOut && impatientError == null,
                        "a competing transaction observes its configured wait limit without releasing the owner's lock");
                    writer.Start();
                    Check(started.WaitOne(1000), "competing writer starts within its deadline");
                    Check(!finished.WaitOne(100), "competing SaveSectionBatch waits while the transaction is held");
                    string error;
                    Check(transaction.TryRestoreOriginal(out error) && original.SequenceEqual(File.ReadAllBytes(spec.FilePath)),
                        "rollback completes before the waiting writer can replace the INI");
                    Check(!finished.WaitOne(0), "rollback keeps the transaction mutex until Dispose");
                }
                Check(finished.WaitOne(2000) && writer.Join(1000), "competing writer completes after Dispose");
                if (workerError != null) throw new IOException("Competing writer failed.", workerError);
                Check(ManagedIniFile.LoadSection(spec, false)["Value"] == "later writer",
                    "the later cooperating writer wins after rollback releases its scope");
            }
            finally
            {
                if (writer.IsAlive && !writer.Join(2500)) throw new TimeoutException("Competing fixture writer did not stop.");
            }
        }
    }

    static void TestCaptureFailures(string root)
    {
        var oversized = Spec(root, "oversized.ini");
        oversized.MaximumBytes = 1024;
        File.WriteAllText(oversized.FilePath, new string('x', 2048));
        Throws<InvalidDataException>(delegate { using (ManagedIniFile.BeginTransaction(oversized)) { } },
            "oversized snapshots fail before retaining a transaction");
        oversized.MaximumBytes = 4096;
        CheckCanAcquireFromAnotherThread(oversized, "oversized capture failure releases the mutex");

        var unreadable = Spec(root, "unreadable.ini");
        File.WriteAllText(unreadable.FilePath, "[Settings]\nValue=original\n");
        using (var exclusive = new FileStream(unreadable.FilePath, FileMode.Open, FileAccess.Read, FileShare.None))
            Throws<IOException>(delegate { using (ManagedIniFile.BeginTransaction(unreadable)) { } },
                "an unreadable existing file is an error, not a missing-file snapshot");
        CheckCanAcquireFromAnotherThread(unreadable, "unreadable capture failure releases the mutex");

        var directory = Spec(root, "directory.ini");
        Directory.CreateDirectory(directory.FilePath);
        Throws<IOException>(delegate { using (ManagedIniFile.BeginTransaction(directory)) { } },
            "a directory at the configured INI path is rejected");
        Directory.Delete(directory.FilePath);
        CheckCanAcquireFromAnotherThread(directory, "directory capture failure releases the mutex");
    }

    static void TestRestoreFailureAndLifetime(string root)
    {
        var spec = Spec(root, "restore-failure.ini");
        File.WriteAllText(spec.FilePath, "; snapshot\n[Settings]\nValue=before\n");
        byte[] original = File.ReadAllBytes(spec.FilePath);
        var transaction = ManagedIniFile.BeginTransaction(spec);
        try
        {
            Save(spec, "after");
            byte[] modified = File.ReadAllBytes(spec.FilePath);
            using (var denyReplacement = new FileStream(spec.FilePath, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                string error;
                Check(!transaction.TryRestoreOriginal(out error) && !String.IsNullOrEmpty(error) &&
                    modified.SequenceEqual(File.ReadAllBytes(spec.FilePath)),
                    "a failed atomic restore reports its error and preserves current file bytes");
            }
            string retryError;
            Check(transaction.TryRestoreOriginal(out retryError) && original.SequenceEqual(File.ReadAllBytes(spec.FilePath)),
                "failed rollback can be retried under the still-owned mutex");
            Check(Directory.GetFiles(root, ".AIProjects-*.tmp").Length == 0, "failed restore cleans up its temporary sibling");

            bool rejectedCrossThread = false;
            Exception workerError = null;
            var worker = new Thread(delegate()
            {
                try
                {
                    string error;
                    rejectedCrossThread = !transaction.TryRestoreOriginal(out error) && !String.IsNullOrEmpty(error);
                    try { transaction.Dispose(); rejectedCrossThread = false; }
                    catch (InvalidOperationException) { }
                }
                catch (Exception ex) { workerError = ex; }
            });
            worker.IsBackground = true;
            worker.Start();
            Check(worker.Join(2000) && workerError == null && rejectedCrossThread,
                "cross-thread use and Dispose are rejected without releasing the owner's mutex");
        }
        finally { transaction.Dispose(); }
        transaction.Dispose();
        string disposedError;
        Check(!transaction.TryRestoreOriginal(out disposedError) && !String.IsNullOrEmpty(disposedError),
            "a disposed transaction cannot restore its old snapshot");
        CheckCanAcquireFromAnotherThread(spec, "owner disposal after failures releases the mutex");
    }

    static void CheckCanAcquireFromAnotherThread(ManagedIniFileSpec spec, string label)
    {
        Exception error = null;
        var worker = new Thread(delegate()
        {
            try { using (ManagedIniFile.BeginTransaction(spec)) { } }
            catch (Exception ex) { error = ex; }
        });
        worker.IsBackground = true;
        worker.Start();
        Check(worker.Join(2500) && error == null, label);
    }

    static void Throws<T>(Action action, string label) where T : Exception
    {
        try { action(); }
        catch (T) { Check(true, label); return; }
        throw new InvalidOperationException("FAIL: " + label);
    }

    static void Check(bool passed, string message)
    {
        if (!passed) throw new InvalidOperationException("FAIL: " + message);
        ++checks;
        Console.WriteLine("ok - " + message);
    }
}

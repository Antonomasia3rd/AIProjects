using System;
using System.IO;
using System.Text;
using System.Threading;

namespace AIProjects.Dependencies
{
    public sealed class ManagedLogFileSpec
    {
        public string FilePath;
        public int LockWaitMilliseconds = 5000;
        public long MaximumBytes;
        public int RetentionCount;
        public bool WriteUtf8Bom = true;
    }

    // Shared process- and thread-safe sidecar logging for managed desktop apps.
    // Product code remains responsible for choosing which messages also reach
    // its console or UI; this class owns only the durable file lifecycle.
    public static class ManagedLogFile
    {
        static readonly UTF8Encoding Utf8WithoutBom = new UTF8Encoding(false, true);
        static readonly byte[] Utf8Preamble = new UTF8Encoding(true).GetPreamble();

        public static bool AppendLine(
            ManagedLogFileSpec spec,
            string line,
            out string error)
        {
            error = null;
            try
            {
                string path = ValidateAndCanonicalize(spec);
                string lockName = ManagedNamedObjects.MachineScopedName(
                    "ManagedLog",
                    path.ToUpperInvariant());
                using (var mutationLock = new Mutex(false, lockName))
                {
                    bool owns = false;
                    try
                    {
                        try
                        {
                            owns = mutationLock.WaitOne(spec.LockWaitMilliseconds);
                        }
                        catch (AbandonedMutexException)
                        {
                            owns = true;
                        }
                        if (!owns)
                            throw new TimeoutException(
                                "Timed out waiting for the managed log mutation lock.");

                        string directory = Path.GetDirectoryName(path);
                        if (!String.IsNullOrWhiteSpace(directory))
                            Directory.CreateDirectory(directory);

                        byte[] encodedLine = Utf8WithoutBom.GetBytes(
                            (line ?? "") + Environment.NewLine);
                        RotateIfNeeded(spec, path, encodedLine.Length);
                        AppendEncodedLine(spec, path, encodedLine);
                    }
                    finally
                    {
                        if (owns)
                        {
                            try { mutationLock.ReleaseMutex(); }
                            catch (ApplicationException) { }
                        }
                    }
                }
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        static string ValidateAndCanonicalize(ManagedLogFileSpec spec)
        {
            if (spec == null)
                throw new ArgumentNullException("spec");
            if (String.IsNullOrWhiteSpace(spec.FilePath))
                throw new ArgumentException("A managed log file path is required.", "spec");
            if (spec.LockWaitMilliseconds < 0 || spec.LockWaitMilliseconds > 60000)
                throw new ArgumentOutOfRangeException(
                    "spec",
                    "The managed log lock wait must be from 0 through 60000 milliseconds.");
            if (spec.MaximumBytes < 0)
                throw new ArgumentOutOfRangeException(
                    "spec",
                    "The managed log maximum size cannot be negative.");
            if (spec.RetentionCount < 0 || spec.RetentionCount > 100)
                throw new ArgumentOutOfRangeException(
                    "spec",
                    "The managed log retention count must be from 0 through 100.");
            return Path.GetFullPath(spec.FilePath);
        }

        static void RotateIfNeeded(
            ManagedLogFileSpec spec,
            string path,
            int pendingBytes)
        {
            if (spec.MaximumBytes == 0 || !File.Exists(path))
                return;

            long existingBytes = new FileInfo(path).Length;
            int preambleBytes = existingBytes == 0 && spec.WriteUtf8Bom
                ? Utf8Preamble.Length
                : 0;
            if (existingBytes == 0 ||
                existingBytes + preambleBytes + pendingBytes <= spec.MaximumBytes)
                return;

            if (spec.RetentionCount == 0)
            {
                File.Delete(path);
                return;
            }

            for (int index = spec.RetentionCount; index >= 1; --index)
            {
                string source = index == 1
                    ? path
                    : path + "." + (index - 1).ToString();
                string destination = path + "." + index.ToString();
                if (!File.Exists(source))
                    continue;
                if (File.Exists(destination))
                    File.Delete(destination);
                File.Move(source, destination);
            }
        }

        static void AppendEncodedLine(
            ManagedLogFileSpec spec,
            string path,
            byte[] encodedLine)
        {
            using (var stream = new FileStream(
                path,
                FileMode.OpenOrCreate,
                FileAccess.Write,
                FileShare.Read))
            {
                stream.Seek(0, SeekOrigin.End);
                if (stream.Position == 0 && spec.WriteUtf8Bom)
                {
                    stream.Write(Utf8Preamble, 0, Utf8Preamble.Length);
                }
                stream.Write(encodedLine, 0, encodedLine.Length);
                stream.Flush();
            }
        }
    }
}

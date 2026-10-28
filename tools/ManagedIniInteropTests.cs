using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Runtime.InteropServices;
using AIProjects.Dependencies;

// Files belong to one new temp directory. The native worker only hashes names,
// owns ephemeral mutexes or edits this fixture; no actual Startup is exercised.
static class ManagedIniInteropTests
{
    static string native;
    static int checks;
    static Process Start(string root, params string[] arguments)
    { return StartWithTemp(root,null,arguments); }
    static Process StartWithTemp(string root,string temporary,params string[] arguments)
    {
        var info = new ProcessStartInfo(native, String.Join(" ", arguments.Select(ManagedConfigurationStartup.QuoteArgument))) {
            WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true, RedirectStandardInput = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
        };
        if(temporary!=null){info.EnvironmentVariables["TMP"]=temporary;info.EnvironmentVariables["TEMP"]=temporary;}
        return Process.Start(info);
    }
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern uint GetShortPathName(string path,StringBuilder output,uint length);
    static void Aliases(string root)
    {
        string parent=Path.Combine(root,"Temporary directory with a long name");Directory.CreateDirectory(parent);
        string fixture=Path.Combine(parent,"AIProjects-IniInterop-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);
        string path=Path.Combine(fixture,"probe.ini");File.WriteAllText(path,"; unchanged alias fixture\r\n");
        var buffer=new StringBuilder(32768);uint length=GetShortPathName(parent,buffer,(uint)buffer.Capacity);
        string spelling=length>0&&length<buffer.Capacity?buffer.ToString():parent;
        using(var worker=StartWithTemp(root,spelling,"--probe",path,"0")){
            var output=worker.StandardOutput.ReadToEndAsync();var error=worker.StandardError.ReadToEndAsync();
            if(!worker.WaitForExit(5000)){worker.Kill();throw new TimeoutException("Temporary alias fixture exceeded five seconds.");}
            Check(worker.ExitCode==0,"native temp guard accepts the same physical directory through short/long spellings");
            if(worker.ExitCode!=0)throw new IOException(output.Result+error.Result);
        }
        foreach(string denied in new[]{path,Path.Combine(root,"nested","probe.ini"),Path.Combine(Path.GetDirectoryName(native),"forbidden.ini")})
            Check(Run(root,2,"--probe",denied,"0").Contains("Fixture path rejected"),"temp guard rejects nested and outside fixture targets without mutation");
        string invalidLeaf=Path.Combine(parent,"AIProjects-IniInterop-not-a-guid");Directory.CreateDirectory(invalidLeaf);
        using(var worker=StartWithTemp(root,spelling,"--probe",Path.Combine(invalidLeaf,"probe.ini"),"0")){
            var output=worker.StandardOutput.ReadToEndAsync();var error=worker.StandardError.ReadToEndAsync();
            if(!worker.WaitForExit(5000)){worker.Kill();throw new TimeoutException("Rejected alias fixture exceeded five seconds.");}
            Check(worker.ExitCode==2,"a matching prefix without an exact GUID leaf remains rejected");
            GC.KeepAlive(output.Result);GC.KeepAlive(error.Result);
        }
        Check(File.ReadAllText(path)=="; unchanged alias fixture\r\n"&&!File.Exists(Path.Combine(invalidLeaf,"probe.ini")),"alias probes and rejected paths leave fixture data unchanged");
    }
    static string Run(string root, int expected, params string[] arguments)
    {
        using (var process = Start(root, arguments))
        {
            var stdout = process.StandardOutput.ReadToEndAsync();
            var stderr = process.StandardError.ReadToEndAsync();
            if (!process.WaitForExit(5000)) { process.Kill(); throw new TimeoutException("Native fixture worker exceeded five seconds."); }
            string output = stdout.Result + stderr.Result;
            if (process.ExitCode != expected) throw new IOException("Unexpected native fixture exit for " + arguments[0] + ": " + process.ExitCode + " " + output);
            return output.Trim();
        }
    }
    static Dictionary<string, string> Fields(string output)
    {
        var fields = new Dictionary<string, string>();
        foreach (string line in output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
        {
            int equal = line.IndexOf('=');
            if (equal > 0) fields.Add(line.Substring(0, equal), line.Substring(equal + 1));
        }
        return fields;
    }
    static ManagedIniDocumentSpec Spec(string path)
    { return new ManagedIniDocumentSpec { FilePath = path, DefaultContents = "", MutationWaitMilliseconds = 1500 }; }
    static void Names(string root)
    {
        string actual = Path.Combine(root, "École-亚洲-😀.ini");
        string relative = Path.Combine(Path.GetFileName(root), "..", Path.GetFileName(root), "École-亚洲-😀.ini");
        // The relative alias is resolved from the same temporary parent in both languages.
        string parent = Path.GetDirectoryName(root);
        string previous = Environment.CurrentDirectory;
        try
        {
            Environment.CurrentDirectory = parent;
            string expectedIdentity = ManagedNamedObjects.NormalizeFileIdentity(actual);
            string expectedName = ManagedNamedObjects.MachineScopedName("ConfigIni", expectedIdentity);
            foreach (string alias in new[] { actual, actual.ToLowerInvariant(), actual.Replace('\\', '/'), relative,
                Path.Combine(root, ".", "École-亚洲-😀.ini"), Path.Combine(root, "sub", "..", "École-亚洲-😀.ini") })
            {
                var fields = Fields(Run(parent, 0, "--name", alias));
                Check(fields["IDENTITY"] == ManagedNamedObjects.NormalizeFileIdentity(alias) && fields["NAME"] == expectedName,
                    "native/managed file name matches across case, slashes, relative/dot aliases and Unicode");
            }
            Check(expectedName.StartsWith(@"Global\AIProjects.ConfigIni.") && expectedName.Split('.').Length == 3,
                "INI identity is Global and scoped only to its normalized file, with no account/session suffix");
            var different = Fields(Run(root, 0, "--name", Path.Combine(root, "different.ini")));
            Check(different["NAME"] != expectedName, "different files keep independent shared lock identities");
            foreach (string value in new[] { "", "ASCII identity", "亚洲 😀 é" })
                Check(Run(root, 0, "--hash", value) == ManagedNamedObjects.StableIdentityHash(value), "native UTF-8 SHA identity uses the same first eight bytes and uppercase hex as managed");
            var startup = Fields(Run(root, 0, "--startup-name", Path.Combine(root, "synthetic-shortcut.lnk")));
            Check(startup["NAME"] == ManagedNamedObjects.CurrentUserScopedName("StartupShortcut", startup["IDENTITY"]) &&
                startup["NAME"].StartsWith(@"Global\AIProjects.StartupShortcut."), "native Startup uses Global user scope and its existing shortcut-path identity without creating a shortcut");
        }
        finally { Environment.CurrentDirectory = previous; }
    }
    static void Locks(string root, string path)
    {
        File.WriteAllText(path, "; exact original\r\n[Managed]\r\nCount=0\r\n[Native]\r\nCount=0\r\n", Encoding.Unicode);
        var spec = Spec(path);
        using (var transaction = ManagedIniFile.BeginTransaction(spec))
        {
            Check(Run(root, 3, "--probe", path.Replace('\\', '/').ToUpperInvariant(), "50").StartsWith("LOCK_UNAVAILABLE="),
                "managed owner blocks native alias acquisition on the same Global file mutex");
            string error;
            Check(transaction.TryRestoreOriginal(out error), "managed exact-original rollback still works with the shared native identity");
        }
        Check(Run(root, 0, "--nested", path) == "", "native file mutex retains same-thread recursive ownership");
        using (var holder = Start(root, "--hold", path))
        {
            try
            {
                Check(holder.StandardOutput.ReadLine() == "READY", "native owner acquires its ephemeral fixture mutex");
                var impatient = Spec(path); impatient.MutationWaitMilliseconds = 50;
                bool timedOut = false;
                try { using (ManagedIniFile.BeginTransaction(impatient)) { } }
                catch (TimeoutException) { timedOut = true; }
                Check(timedOut, "native owner blocks managed acquisition using the configured bounded wait");
                holder.StandardInput.WriteLine("release"); holder.StandardInput.Flush();
                Check(holder.WaitForExit(3000) && holder.ExitCode == 0, "native owner releases without a file or Startup mutation");
            }
            finally { if (!holder.HasExited) holder.Kill(); }
        }
    }
    static void Writers(string root, string path)
    {
        const int count = 100;
        var spec = Spec(path);
        using (var worker = Start(root, "--write", path.Replace('\\', '/').ToLowerInvariant(), count.ToString(CultureInfo.InvariantCulture)))
        {
            var stdout = worker.StandardOutput.ReadToEndAsync();
            var stderr = worker.StandardError.ReadToEndAsync();
            try
            {
                for (int index = 0; index < count; ++index)
                {
                    using (var transaction = ManagedIniFile.BeginTransaction(spec))
                    {
                        var current = ManagedIniFile.LoadDocument(spec);
                        string raw;
                        int value = current.TryGetValue("Managed", "Count", out raw) ? Int32.Parse(raw, CultureInfo.InvariantCulture) : 0;
                        Thread.Sleep(1);
                        string error;
                        if (!ManagedIniFile.SaveDocumentBatch(spec, new[] { new ManagedIniSetting("Managed", "Count", (value + 1).ToString(CultureInfo.InvariantCulture)) }, out error))
                            throw new IOException(error);
                    }
                }
                Check(worker.WaitForExit(5000) && worker.ExitCode == 0, "bounded managed/native writers complete without lock or publication failures");
                if (worker.ExitCode != 0) throw new IOException(stdout.Result + stderr.Result);
            }
            finally { if (!worker.HasExited) worker.Kill(); }
        }
        var final = ManagedIniFile.LoadDocument(spec);
        Check(final.GetSection("Managed")["Count"] == count.ToString(CultureInfo.InvariantCulture) && final.GetSection("Native")["Count"] == count.ToString(CultureInfo.InvariantCulture),
            "100 read-modify-writes per language preserve both sections without losing unrelated concurrent edits");
        Check(File.ReadAllText(path).Contains("; exact original") && Directory.GetFiles(root, "*.tmp*").Length == 0,
            "mixed publication preserves comments and leaves no temporary siblings");
    }
    static void Check(bool passed, string label)
    {
        if (!passed) throw new InvalidOperationException("FAIL: " + label);
        ++checks; Console.WriteLine("ok - " + label);
    }
    static int Main(string[] args)
    {
        if (args.Length != 1 || !Path.IsPathRooted(args[0]) || !File.Exists(args[0])) return 2;
        native = Path.GetFullPath(args[0]);
        string parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string root = Path.GetFullPath(Path.Combine(parent, "AIProjects-IniInterop-" + Guid.NewGuid().ToString("N")));
        if (!String.Equals(Path.GetDirectoryName(root), parent, StringComparison.OrdinalIgnoreCase)) return 2;
        using (var deadline = new Timer(delegate(object ignored) { Console.Error.WriteLine("Managed INI interop fixture exceeded 45 seconds."); Environment.Exit(124); }, null, 45000, Timeout.Infinite))
        {
            Directory.CreateDirectory(root);
            try
            {
                Names(root);Aliases(root); string path = Path.Combine(root, "shared.ini"); Locks(root, path); Writers(root, path);
                Console.WriteLine("Managed/native INI interoperability checks passed: " + checks); return 0;
            }
            catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
            finally
            {
                if (!String.Equals(Path.GetDirectoryName(Path.GetFullPath(root)), parent, StringComparison.OrdinalIgnoreCase)) throw new IOException("Refusing cleanup outside fixture parent.");
                Directory.Delete(root, true);
            }
        }
    }
}

using System;
using System.Collections;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;

static class LegacyUtilitiesTests
{
    static int failures;

    static int Main(string[] args)
    {
        string repositoryRoot =
            args.Length > 0 ? Path.GetFullPath(args[0]) : Directory.GetCurrentDirectory();
        TestDnsRecordParsing();
        TestStrictArgumentParsing();
        TestPhotoCollageAtomicOutput();
        TestTaskXmlHardening(repositoryRoot);
        TestCapsBlinkIdentity(repositoryRoot);
        TestAsusBlinkGuardrails(repositoryRoot);
        TestAlwaysUiAccessSource(repositoryRoot);

        if (failures != 0)
        {
            Console.Error.WriteLine("Legacy utility tests failed: " + failures);
            return 1;
        }

        Console.WriteLine("Legacy utility tests passed.");
        return 0;
    }

    static void TestDnsRecordParsing()
    {
        MethodInfo parse = PrivateMethod(typeof(DNSAutoUpdate), "ParseExactARecords");
        string sample =
            "Server address 10.0.0.53\r\n" +
            "@ 3600 A 192.168.1.10\r\n" +
            "unrelated 172.16.0.1\r\n" +
            "app 300 A 192.168.1.11\r\n";
        var values = ((IEnumerable)parse.Invoke(null, new object[] { sample }))
            .Cast<object>()
            .Select(value => value.ToString())
            .ToArray();
        Check(
            values.SequenceEqual(new[] { "192.168.1.10", "192.168.1.11" }),
            "DNS parser accepts only IPv4 values from A-record lines");
    }

    static void TestStrictArgumentParsing()
    {
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(DNSAutoUpdate),
                    "ParseArgs",
                    new object[] { new[] { "--zone-name", "--once" } });
            },
            "DNS parser rejects a missing option value");

        CheckThrows<ArgumentOutOfRangeException>(
            delegate
            {
                InvokePrivate(
                    typeof(PhotoCollage),
                    "ParseArgs",
                    new object[] { new[] { "--cols", "0" } });
            },
            "PhotoCollage rejects out-of-range numeric options");

        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(TaskSchedulerMigration),
                    "ParseArgs",
                    new object[] { new[] { "--old-sid", "not-a-sid", "--new-user", "User" } });
            },
            "TaskSchedulerMigration validates the source SID");
    }

    static void TestPhotoCollageAtomicOutput()
    {
        string root = Path.Combine(
            Path.GetTempPath(),
            "AIProjects-LegacyUtilitiesTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            string first = Path.Combine(root, "first.png");
            string second = Path.Combine(root, "second.png");
            using (var bitmap = new Bitmap(8, 6))
            {
                bitmap.SetPixel(0, 0, Color.Red);
                bitmap.Save(first);
                bitmap.SetPixel(0, 0, Color.Blue);
                bitmap.Save(second);
            }

            Type optionsType = typeof(PhotoCollage).GetNestedType(
                "Options",
                BindingFlags.NonPublic);
            object options = Activator.CreateInstance(optionsType, true);
            SetField(options, "InputFolder", root);
            string output = Path.Combine(root, "collage.png");
            SetField(options, "OutputFile", output);
            SetField(options, "Cols", 2);
            SetField(options, "MaxImages", 2);
            SetField(options, "JpegQuality", 80L);
            SetField(options, "MaxCanvasMegapixels", 10L);
            SetField(options, "LogFile", Path.Combine(root, "test.log"));

            InvokePrivate(typeof(PhotoCollage), "Run", new[] { options });
            using (Image image = Image.FromFile(output))
                Check(image.Width == 16 && image.Height == 6, "PhotoCollage writes the expected canvas");

            File.WriteAllText(output, "old output");
            InvokePrivate(typeof(PhotoCollage), "Run", new[] { options });
            using (Image image = Image.FromFile(output))
                Check(image.Width == 16 && image.Height == 6, "PhotoCollage atomically replaces an existing output");

            bool temporaryFileLeftBehind = Directory
                .EnumerateFiles(root, ".*.tmp", SearchOption.TopDirectoryOnly)
                .Any();
            Check(!temporaryFileLeftBehind, "PhotoCollage removes temporary output files");
        }
        finally
        {
            try
            {
                Directory.Delete(root, true);
            }
            catch
            {
            }
        }
    }

    static void TestTaskXmlHardening(string repositoryRoot)
    {
        CheckThrows<System.Xml.XmlException>(
            delegate
            {
                InvokePrivate(
                    typeof(TaskSchedulerMigration),
                    "LoadTaskXml",
                    new object[] { "<!DOCTYPE Task [<!ENTITY x SYSTEM \"file:///missing\">]><Task>&x;</Task>" });
            },
            "TaskSchedulerMigration rejects task XML DTDs");

        string path = Path.Combine(
            repositoryRoot,
            "legacy",
            "TaskSchedulerMigration",
            "TaskSchedulerMigration.cs");
        string source = File.ReadAllText(path);
        Check(
            source.Contains("taskCollection.Item(i)") &&
            source.Contains("folderCollection.Item(i)") &&
            source.Contains("ReleaseComObject(taskCollection)") &&
            source.Contains("ReleaseComObject(folderCollection)"),
            "TaskSchedulerMigration releases COM collections and avoids COM foreach enumerators");
        Check(
            source.Contains("foreach (object folder in folders)") &&
            !source.Contains("ReleaseComObject(taskRef.Folder)"),
            "TaskSchedulerMigration releases each retained task folder once");
        Check(
            source.Contains("Task folders/items that could not be enumerated") &&
            source.Contains("enumerationFailures == 0"),
            "TaskSchedulerMigration reports partial enumeration as a nonzero result");
    }

    static void TestCapsBlinkIdentity(string repositoryRoot)
    {
        string first = (string)InvokePrivate(
            typeof(CapsLockLight),
            "StableHash",
            new object[] { "\\Device\\KeyboardClass0" });
        string second = (string)InvokePrivate(
            typeof(CapsLockLight),
            "StableHash",
            new object[] { "\\device\\keyboardclass0" });
        Check(first == second && first.Length == 8, "capsblink uses a stable case-insensitive device identity");

        string source = File.ReadAllText(Path.Combine(
            repositoryRoot,
            "legacy",
            "capsblink",
            "capsblink.cs"));
        Check(
            source.Contains("if (args.Length != 0)") &&
            source.Contains("ERROR: Unknown argument:"),
            "capsblink rejects unknown command-line arguments before touching hardware");
        Check(
            source.Contains("exitEvent.WaitOne(blinkIntervalMs)") &&
            source.Contains("exitEvent.Set();"),
            "capsblink interrupts long blink waits during Ctrl+C shutdown");
        Check(
            source.Contains("BlinkIntervalMs must be from 50 through 86400000") &&
            source.Contains("throw new InvalidDataException(key + \" must be an integer.\")"),
            "capsblink rejects malformed or out-of-range INI timing");
        Check(
            source.Contains("EntryPoint = \"DefineDosDeviceW\"") &&
            source.Contains("EntryPoint = \"CreateFileW\"") &&
            source.Contains("CharSet = CharSet.Unicode"),
            "capsblink binds string Win32 APIs to explicit Unicode entry points");
    }

    static void TestAsusBlinkGuardrails(string repositoryRoot)
    {
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseArgs",
                    new object[] { new[] { "--mic-state", "--no-tray" } });
            },
            "asusblink rejects a missing option value");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseArgs",
                    new object[] { new[] { "--unknown", "1" } });
            },
            "asusblink rejects unknown options");
        CheckThrows<ArgumentException>(
            delegate
            {
                InvokePrivate(
                    typeof(Program),
                    "ParseTimeMs",
                    new object[] { "not-a-time", "test-time" });
            },
            "asusblink rejects malformed time values");

        string path = Path.Combine(
            repositoryRoot,
            "legacy",
            "asusblink",
            "asusblink.cs");
        string source = File.ReadAllText(path);
        Check(
            source.IndexOf("var commandLineOptions = ParseArgs(args);", StringComparison.Ordinal) <
            source.IndexOf("var opts = LoadIniOptions();", StringComparison.Ordinal),
            "asusblink validates command-line syntax before creating/loading the INI");
        Check(
            source.Contains("Guid.NewGuid().ToString(\"N\") + \".tmp\"") &&
            source.Contains("File.Move(temporaryPath, iniPath)") &&
            source.Contains("if (!File.Exists(iniPath))"),
            "asusblink creates its default INI atomically under startup races");
        Check(
            !source.Contains("Environment.Exit(") &&
            !source.Contains("Environment.TickCount"),
            "asusblink uses cooperative shutdown and monotonic duration timing");
        Check(
            source.Contains("WaitForRunningTasks(Timeout.Infinite)") &&
            source.Contains("lock (acpiLock)"),
            "asusblink drains workers before synchronized ACPI cleanup");
        Check(
            source.Contains("static bool CreateTray()") &&
            source.Contains("Application.ExitThread()"),
            "asusblink reports tray creation failure and exits its message loop cooperatively");
        Check(
            source.Contains("GetScopedStartupShortcutPath()") &&
            source.Contains("ShortcutTargetsCurrentExecutable") &&
            source.Contains("StableHash(executablePath)"),
            "asusblink scopes startup shortcuts to the executable and validates their target");
        Check(
            source.Contains("if (bytesReturned < sizeof(int))") &&
            source.Contains("ACPI returned a truncated response"),
            "asusblink rejects truncated ACPI firmware responses");
    }

    static void TestAlwaysUiAccessSource(string repositoryRoot)
    {
        string path = Path.Combine(
            repositoryRoot,
            "legacy",
            "WindhawkMods",
            "local@always-uiaccess.wh.cpp");
        string source = File.ReadAllText(path);
        Check(
            source.Contains("bytesRead != sizeof(*request)") &&
            source.Contains("ValidateUIAccessRequest(request.get(), &response)") &&
            source.Contains("RequestEnvironmentIsValid(request)"),
            "Always UIAccess validates the complete pipe request");
        Check(
            source.Contains("Requested path does not match the target process image.") &&
            source.Contains("Target process is not a child of the requesting process."),
            "Always UIAccess binds token patches to the real child image");
        Check(
            source.Contains("windowsDirectoryBuffer.resize(windowsDirectoryLength)") &&
            !source.Contains("WCHAR windowsDirectory[MAX_PATH]"),
            "Always UIAccess native System32 validation supports long Windows paths");
        Check(
            source.Contains("CopyEnvironmentToRequest(") &&
            !source.Contains("CreateEnvironmentBlock"),
            "Always UIAccess forwards the caller environment instead of inheriting the service environment");
        Check(
            source.Contains("PipeReadWithStop(") &&
            source.Contains("PipeWriteWithStop(") &&
            source.Contains("CancelIoEx(pipe, overlapped)"),
            "Always UIAccess uses cancellable overlapped pipe I/O");
        Check(
            source.Contains("CloseDuplicatedHandleInClient(") &&
            source.Contains("response.processHandle"),
            "Always UIAccess cleans up duplicated client handles on failed delivery");
        Check(
            source.Contains("BrokerCanPreserveCreateProcessRequest(") &&
            source.Contains("actualCreationFlags |= CREATE_SUSPENDED") &&
            source.Contains("ResumeThread(lpProcessInformation->hThread)"),
            "Always UIAccess preserves unsupported CreateProcess semantics through suspended in-process creation");
        Check(
            source.Contains("WaitForSingleObject(g_pipeThread, INFINITE)") &&
            source.Contains("WaitForSingleObject(g_autoTopmostThread, INFINITE)"),
            "Always UIAccess drains worker threads before unload");
    }

    static MethodInfo PrivateMethod(Type type, string name)
    {
        MethodInfo method = type.GetMethod(
            name,
            BindingFlags.NonPublic | BindingFlags.Static);
        if (method == null)
            throw new MissingMethodException(type.FullName, name);
        return method;
    }

    static object InvokePrivate(Type type, string name, object[] arguments)
    {
        try
        {
            return PrivateMethod(type, name).Invoke(null, arguments);
        }
        catch (TargetInvocationException ex)
        {
            throw ex.InnerException ?? ex;
        }
    }

    static void SetField(object target, string name, object value)
    {
        FieldInfo field = target.GetType().GetField(
            name,
            BindingFlags.Public | BindingFlags.Instance);
        if (field == null)
            throw new MissingFieldException(target.GetType().FullName, name);
        field.SetValue(target, value);
    }

    static void CheckThrows<T>(Action action, string name) where T : Exception
    {
        try
        {
            action();
            Check(false, name);
        }
        catch (T)
        {
            Check(true, name);
        }
    }

    static void Check(bool condition, string name)
    {
        if (condition)
        {
            Console.WriteLine("ok - " + name);
            return;
        }

        Console.Error.WriteLine("not ok - " + name);
        failures++;
    }
}

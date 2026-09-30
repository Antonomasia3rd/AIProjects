using System;
using System.IO;
using System.Reflection;
using System.Text;

static class TaskSchedulerMigrationLocalTests
{
    const BindingFlags PrivateStatic = BindingFlags.NonPublic | BindingFlags.Static;
    static int failures;

    static int Main(string[] args)
    {
        string sourcePath = args.Length == 0
            ? @"..\..\dependencies\TaskSchedulerMigration\task_scheduler_migration_app.cs"
            : args[0];

        TestRegistrationFlags(sourcePath);
        TestLogonPolicy();
        TestWhatIfGate(sourcePath);
        TestAtomicBackupWrite(sourcePath);
        TestProfileConfiguration();
        TestInformationalOptions();

        if (failures != 0)
        {
            Console.Error.WriteLine("TaskSchedulerMigration local tests failed: " + failures);
            return 1;
        }

        Console.WriteLine("TaskSchedulerMigration local tests passed.");
        return 0;
    }

    static void TestRegistrationFlags(string sourcePath)
    {
        int flags = (int)GetConstant("TASK_REGISTRATION_FLAGS");
        Check(flags == 38, "registration combines create/update with trigger suppression");

        string source = File.ReadAllText(sourcePath);
        Check(
            source.Contains("TASK_CREATE_OR_UPDATE | TASK_IGNORE_REGISTRATION_TRIGGERS"),
            "registration flags explicitly suppress registration triggers");
        Check(
            source.Contains("TASK_REGISTRATION_FLAGS,") &&
            source.Contains("registrationLogonType,"),
            "RegisterTask receives the guarded flags and inspected logon type");
        Check(
            !source.Contains("TASK_CREATE_OR_UPDATE, null, null, TASK_LOGON_NONE"),
            "RegisterTask no longer forces TASK_LOGON_NONE");
    }

    static void TestLogonPolicy()
    {
        int[] passThrough = { 0, 3, 4, 5 };
        foreach (int logonType in passThrough)
        {
            int selected;
            string reason;
            bool accepted = SelectLogonType(logonType, false, out selected, out reason);
            Check(
                accepted && selected == logonType && reason.Length == 0,
                "known non-password LogonType " + logonType + " is preserved");
        }

        int ignored;
        string message;
        Check(
            !SelectLogonType(2, false, out ignored, out message) && message.Contains("explicit opt-in"),
            "S4U requires explicit opt-in");
        Check(
            SelectLogonType(2, true, out ignored, out message) && ignored == 2,
            "opted-in S4U is preserved");
        Check(
            !SelectLogonType(1, true, out ignored, out message) && message.Contains("does not expose"),
            "Password logon remains blocked even with credential-sensitive opt-in");
        Check(
            !SelectLogonType(6, true, out ignored, out message) && message.Contains("cannot be recovered"),
            "InteractiveOrPassword remains blocked even with opt-in");
        Check(
            !SelectLogonType(99, true, out ignored, out message) && message.Contains("Unknown"),
            "unknown logon types fail closed");
    }

    static void TestWhatIfGate(string sourcePath)
    {
        Type optionsType = typeof(TaskSchedulerMigration).GetNestedType(
            "Options",
            BindingFlags.NonPublic);
        object options = Activator.CreateInstance(optionsType, true);
        optionsType.GetField("WhatIf", BindingFlags.Public | BindingFlags.Instance)
            .SetValue(options, true);

        TextWriter originalOut = Console.Out;
        var captured = new StringWriter();
        bool result;
        try
        {
            Console.SetOut(captured);
            result = (bool)PrivateMethod("ShouldApply").Invoke(
                null,
                new[] { options, "\\Example", "Update" });
        }
        finally
        {
            Console.SetOut(originalOut);
        }

        Check(!result && captured.ToString().Contains("WHATIF:"), "WhatIf declines mutation and reports the action");

        string source = File.ReadAllText(sourcePath);
        int gate = source.IndexOf("if (!ShouldApply(", StringComparison.Ordinal);
        int backup = source.IndexOf("string backupPath = SaveBackupAtomically(", gate, StringComparison.Ordinal);
        int registration = source.IndexOf("object registeredTask =", gate, StringComparison.Ordinal);
        Check(
            gate >= 0 && backup > gate && registration > backup,
            "WhatIf/Confirm gate remains before backup and registration side effects");
    }

    static void TestAtomicBackupWrite(string sourcePath)
    {
        string root = Path.Combine(
            Path.GetTempPath(),
            "AIProjects-TaskSchedulerMigrationTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            string longBackupDirectory = Path.Combine(Path.GetPathRoot(root), new string('d', 150));
            string boundedPath = (string)PrivateMethod("BuildBackupPath").Invoke(
                null, new object[] { longBackupDirectory, "\\Folder\\" + new string('n', 150) });
            Check(boundedPath.Length < 260, "nonce backup names stay within the Framework path limit");
            string xml = "<Task><Principals><Principal><UserId>S-1-5-21-1</UserId></Principal></Principals></Task>";
            string first = (string)PrivateMethod("SaveBackupAtomically").Invoke(
                null,
                new object[] { root, "\\Folder\\Task:Name", xml });
            string second = (string)PrivateMethod("SaveBackupAtomically").Invoke(
                null,
                new object[] { root, "\\Folder\\Task:Name", xml });
            Check(
                File.Exists(first) && File.Exists(second) && first != second &&
                File.ReadAllText(first, Encoding.UTF8) == xml &&
                File.ReadAllText(second, Encoding.UTF8) == xml,
                "task backups use distinct completed files without overwriting earlier backups");
            Check(
                Directory.GetFiles(root, "*.tmp", SearchOption.TopDirectoryOnly).Length == 0,
                "task backup writes clean up same-directory temporary files");

            string source = File.ReadAllText(sourcePath);
            Check(
                source.Contains("File.Move(temporaryPath, backupPath);") &&
                source.Contains("MaximumNameReservations") &&
                !source.Contains("while (File.Exists(path))") &&
                !source.Contains("File.WriteAllText(backupPath, xml"),
                "task backup source uses bounded atomic name reservation instead of an unbounded collision loop");
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }

    static void TestProfileConfiguration()
    {
        string root = Path.Combine(
            Path.GetTempPath(),
            "AIProjects-TaskSchedulerMigrationProfileTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            string profile = Path.Combine(root, "migration.ini");
            int result;
            string output = CaptureMain(
                PrivateMethod("Main"),
                new[]
                {
                    "--ini", profile,
                    "--set", "Settings.OldSID=S-1-5-18",
                    "--set", "Settings.NewUser=DOMAIN\\MigratedUser",
                    "--set", "Settings.TaskPath=\\Migration",
                    "--configure-only"
                },
                out result);
            Check(
                result == 0 && File.Exists(profile) &&
                output.Contains("Configuration is valid:"),
                "profile configuration saves without activating Task Scheduler");

            object profileOptions = PrivateMethod("ParseArgs").Invoke(
                null,
                new object[] { new[] { "--ini", profile } });
            Check(
                (string)GetField(profileOptions, "OldSID") == "S-1-5-18" &&
                (string)GetField(profileOptions, "NewUser") == "DOMAIN\\MigratedUser" &&
                (string)GetField(profileOptions, "BackupDirectory") == Path.Combine(
                    root,
                    "TaskSchedulerMigrationBackup") &&
                (bool)GetField(profileOptions, "WhatIf") &&
                (bool)GetField(profileOptions, "Confirm"),
                "explicit profiles default to preview and confirmation with paths relative to the profile");

            output = CaptureMain(
                PrivateMethod("Main"),
                new[] { "--ini", profile, "--show-config" },
                out result);
            Check(
                result == 0 && output.Contains("WhatIf = true") &&
                output.Contains("Confirm = true"),
                "profile inspection does not activate Task Scheduler");

            object oneRunProfileOptions = PrivateMethod("ParseArgs").Invoke(
                null,
                new object[] { new[] { "--ini", profile, "--apply", "--no-confirm" } });
            Check(
                !(bool)GetField(oneRunProfileOptions, "WhatIf") &&
                !(bool)GetField(oneRunProfileOptions, "Confirm"),
                "profile preview and confirmation defaults accept explicit one-run overrides");

            string beforeInvalidUpdate = File.ReadAllText(profile);
            output = CaptureMain(
                PrivateMethod("Main"),
                new[]
                {
                    "--ini", profile,
                    "--set", "Settings.WhatIf=not-a-boolean",
                    "--configure-only"
                },
                out result);
            Check(
                result == 1 && output.Contains("WhatIf expects a boolean value") &&
                File.ReadAllText(profile) == beforeInvalidUpdate,
                "invalid profile changes do not alter the saved migration configuration");

            output = CaptureMain(
                PrivateMethod("Main"),
                new[] { "--ini", profile, "--set", "Settings.Confirm=0" },
                out result);
            Check(
                result == 1 && output.Contains("requires --configure-only"),
                "persistent migration settings require an explicit non-running configuration command");

            output = CaptureMain(
                PrivateMethod("Main"),
                new[] { "--ini", profile, "--old-sid", "S-1-5-18", "--configure-only" },
                out result);
            Check(
                result == 1 && output.Contains("persistent --set values"),
                "one-run migration options stay separate from profile mutation");

            object directOptions = PrivateMethod("ParseArgs").Invoke(
                null,
                new object[]
                {
                    new[] { "--old-sid", "S-1-5-18", "--new-user", "DOMAIN\\DirectUser" }
                });
            Check(
                !(bool)GetField(directOptions, "WhatIf") &&
                !(bool)GetField(directOptions, "Confirm") &&
                GetField(directOptions, "IniPath") == null,
                "direct migration commands retain their pre-profile apply-without-confirm defaults");
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }

    static void TestInformationalOptions()
    {
        MethodInfo main = PrivateMethod("Main");

        int helpResult;
        string help = CaptureMain(main, new[] { "--help", "--definitely-invalid" }, out helpResult);
        Check(helpResult == 0 && help.Contains("Usage:"), "help short-circuits invalid companion arguments");

        int versionResult;
        string version = CaptureMain(main, new[] { "--version", "--definitely-invalid" }, out versionResult);
        Check(
            versionResult == 0 && version.Contains("TaskSchedulerMigration"),
            "version short-circuits invalid companion arguments");
    }

    static string CaptureMain(MethodInfo main, string[] args, out int result)
    {
        TextWriter originalOut = Console.Out;
        TextWriter originalError = Console.Error;
        var captured = new StringWriter();
        try
        {
            Console.SetOut(captured);
            Console.SetError(captured);
            result = (int)main.Invoke(null, new object[] { args });
            return captured.ToString();
        }
        finally
        {
            Console.SetOut(originalOut);
            Console.SetError(originalError);
        }
    }

    static bool SelectLogonType(
        int original,
        bool includeCredentialSensitive,
        out int selected,
        out string reason)
    {
        object[] values = { original, includeCredentialSensitive, 0, null };
        bool result = (bool)PrivateMethod("TrySelectRegistrationLogonType")
            .Invoke(null, values);
        selected = (int)values[2];
        reason = (string)values[3];
        return result;
    }

    static object GetConstant(string name)
    {
        FieldInfo field = typeof(TaskSchedulerMigration).GetField(name, PrivateStatic);
        if (field == null)
            throw new MissingFieldException(typeof(TaskSchedulerMigration).FullName, name);
        return field.GetRawConstantValue();
    }

    static MethodInfo PrivateMethod(string name)
    {
        MethodInfo method = typeof(TaskSchedulerMigration).GetMethod(name, PrivateStatic);
        if (method == null)
            throw new MissingMethodException(typeof(TaskSchedulerMigration).FullName, name);
        return method;
    }

    static object GetField(object target, string name)
    {
        FieldInfo field = target.GetType().GetField(
            name,
            BindingFlags.Public | BindingFlags.Instance);
        if (field == null)
            throw new MissingFieldException(target.GetType().FullName, name);
        return field.GetValue(target);
    }

    static void Check(bool condition, string description)
    {
        if (condition)
        {
            Console.WriteLine("PASS: " + description);
            return;
        }

        failures++;
        Console.Error.WriteLine("FAIL: " + description);
    }
}

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using AIProjects.Dependencies;

static class TaskSchedulerMigration
{
    const int TASK_CREATE_OR_UPDATE = 6;
    const int TASK_IGNORE_REGISTRATION_TRIGGERS = 32;
    const int TASK_REGISTRATION_FLAGS =
        TASK_CREATE_OR_UPDATE | TASK_IGNORE_REGISTRATION_TRIGGERS;

    const int TASK_LOGON_NONE = 0;
    const int TASK_LOGON_PASSWORD = 1;
    const int TASK_LOGON_S4U = 2;
    const int TASK_LOGON_INTERACTIVE_TOKEN = 3;
    const int TASK_LOGON_GROUP = 4;
    const int TASK_LOGON_SERVICE_ACCOUNT = 5;
    const int TASK_LOGON_INTERACTIVE_TOKEN_OR_PASSWORD = 6;

    const string SettingsSection = "Settings";
    const string OldSidKey = "OldSID";
    const string NewUserKey = "NewUser";
    const string BackupDirectoryKey = "BackupDirectory";
    const string TaskPathKey = "TaskPath";
    const string IncludeCredentialSensitiveTasksKey = "IncludeCredentialSensitiveTasks";
    const string WhatIfKey = "WhatIf";
    const string ConfirmKey = "Confirm";
    const string DefaultBackupDirectoryName = "TaskSchedulerMigrationBackup";

    sealed class Options
    {
        public string OldSID;
        public string NewUser;
        public string BackupDirectory;
        public string TaskPath;
        public bool IncludeCredentialSensitiveTasks;
        public bool WhatIf;
        public bool Confirm;
        public bool Help;
        public bool Version;
        public string IniPath;
    }

    sealed class ParsedCommand
    {
        public string IniPath;
        public bool ProfileMode;
        public readonly Dictionary<string, string> DirectSettings =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public readonly Dictionary<string, string> PersistentSettings =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public bool ConfigureOnly;
        public bool ShowConfiguration;
    }

    static int Main(string[] args)
    {
        // Informational options intentionally short-circuit before strict parsing,
        // filesystem access, or Task Scheduler COM activation. This also makes
        // `--help <otherwise-invalid arguments>` safe and predictable.
        if (args.Any(IsHelpOption))
        {
            Usage();
            return 0;
        }
        if (args.Any(IsVersionOption))
        {
            Console.WriteLine("TaskSchedulerMigration " + ProductVersion());
            return 0;
        }

        try
        {
            ParsedCommand command = ParseCommandLine(args);
            Options options = BuildOptions(command);
            if (command.ConfigureOnly)
            {
                Console.WriteLine("Configuration is valid: " + options.IniPath);
                return 0;
            }
            if (command.ShowConfiguration)
            {
                PrintConfiguration(options);
                return 0;
            }
            if (String.IsNullOrWhiteSpace(options.OldSID) || String.IsNullOrWhiteSpace(options.NewUser))
            {
                Usage();
                return 2;
            }
            return Run(options);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR: " + ex.Message);
            return 1;
        }
    }

    static Options ParseArgs(string[] args)
    {
        return BuildOptions(ParseCommandLine(args));
    }

    static ParsedCommand ParseCommandLine(string[] args)
    {
        var parsed = new ParsedCommand();
        if (args == null)
            args = new string[0];
        for (int i = 0; i < args.Length; ++i)
        {
            string a = args[i];
            if (IsHelpOption(a) || IsVersionOption(a)) continue;
            if (Is(a, "-IncludeCredentialSensitiveTasks") || Is(a, "--include-credential-sensitive-tasks"))
                SetDirectSetting(parsed, IncludeCredentialSensitiveTasksKey, "1");
            else if (Is(a, "--no-include-credential-sensitive-tasks"))
                SetDirectSetting(parsed, IncludeCredentialSensitiveTasksKey, "0");
            else if (Is(a, "-WhatIf") || Is(a, "--what-if"))
                SetDirectSetting(parsed, WhatIfKey, "1");
            else if (Is(a, "--apply") || Is(a, "--no-what-if"))
                SetDirectSetting(parsed, WhatIfKey, "0");
            else if (Is(a, "-Confirm") || Is(a, "--confirm"))
                SetDirectSetting(parsed, ConfirmKey, "1");
            else if (Is(a, "--no-confirm"))
                SetDirectSetting(parsed, ConfirmKey, "0");
            else if (Is(a, "-OldSID") || Is(a, "--old-sid"))
                SetDirectSetting(parsed, OldSidKey, RequireValue(args, ref i, a));
            else if (Is(a, "-NewUser") || Is(a, "--new-user"))
                SetDirectSetting(parsed, NewUserKey, RequireValue(args, ref i, a));
            else if (Is(a, "-BackupDirectory") || Is(a, "--backup-directory"))
                SetDirectSetting(parsed, BackupDirectoryKey, RequireValue(args, ref i, a));
            else if (Is(a, "-TaskPath") || Is(a, "--task-path"))
                SetDirectSetting(parsed, TaskPathKey, RequireValue(args, ref i, a));
            else if (Is(a, "--ini") || Is(a, "-IniFile"))
                parsed.IniPath = RequireValue(args, ref i, a);
            else if (Is(a, "--set"))
                SetPersistentSetting(parsed, RequireValue(args, ref i, a));
            else if (Is(a, "--configure-only"))
                parsed.ConfigureOnly = true;
            else if (Is(a, "--show-config") || Is(a, "--print-config"))
                parsed.ShowConfiguration = true;
            else throw new ArgumentException("Unknown argument: " + a);
        }

        parsed.ProfileMode = parsed.IniPath != null ||
            parsed.PersistentSettings.Count != 0 ||
            parsed.ConfigureOnly ||
            parsed.ShowConfiguration;
        if (parsed.ConfigureOnly && parsed.DirectSettings.Count != 0)
            throw new ArgumentException("--configure-only accepts persistent --set values, not one-run migration options.");
        if (parsed.PersistentSettings.Count != 0 && !parsed.ConfigureOnly)
            throw new ArgumentException("--set requires --configure-only so a configuration change cannot also start a migration.");
        return parsed;
    }

    static void SetDirectSetting(ParsedCommand parsed, string key, string value)
    {
        parsed.DirectSettings[key] = NormalizeSettingValue(key, value);
    }

    static void SetPersistentSetting(ParsedCommand parsed, string assignment)
    {
        int equals = assignment.IndexOf('=');
        if (equals <= 0)
            throw new ArgumentException("--set requires Settings.Key=Value.");
        string key = assignment.Substring(0, equals).Trim();
        int sectionSeparator = key.IndexOf('.');
        if (sectionSeparator >= 0)
        {
            string section = key.Substring(0, sectionSeparator);
            if (!section.Equals(SettingsSection, StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("--set supports only the [Settings] section.");
            key = key.Substring(sectionSeparator + 1);
        }
        key = CanonicalSettingName(key);
        if (key == null)
            throw new ArgumentException("Unknown setting: " + assignment.Substring(0, equals).Trim());
        parsed.PersistentSettings[key] = NormalizeSettingValue(
            key,
            assignment.Substring(equals + 1));
    }

    static Options BuildOptions(ParsedCommand command)
    {
        if (!command.ProfileMode)
        {
            Options directOptions = OptionsFromSettings(
                DefaultSettingValues(false),
                AppDomain.CurrentDomain.BaseDirectory);
            ApplyDirectSettings(directOptions, command.DirectSettings);
            ValidateOptions(directOptions);
            return directOptions;
        }

        string iniPath = ResolveIniPath(command.IniPath);
        ManagedIniFileSpec iniFile = BuildIniFileSpec(iniPath);
        Dictionary<string, string> values = LoadEffectiveSettings(iniFile);
        foreach (KeyValuePair<string, string> setting in command.PersistentSettings)
            values[setting.Key] = setting.Value;

        Options options = OptionsFromSettings(values, Path.GetDirectoryName(iniPath));
        options.IniPath = iniPath;
        ValidateOptions(options);
        if (command.PersistentSettings.Count != 0)
        {
            string error;
            if (!ManagedIniFile.SaveSectionBatch(iniFile, command.PersistentSettings, out error))
                throw new IOException("Could not save TaskSchedulerMigration configuration: " + error);
        }
        ApplyDirectSettings(options, command.DirectSettings);
        ValidateOptions(options);
        return options;
    }

    static Dictionary<string, string> LoadEffectiveSettings(ManagedIniFileSpec iniFile)
    {
        Dictionary<string, string> values = DefaultSettingValues(true);
        foreach (KeyValuePair<string, string> rawSetting in ManagedIniFile.LoadSection(iniFile))
        {
            string key = CanonicalSettingName(rawSetting.Key);
            if (key == null)
                throw new InvalidDataException("Unknown [Settings] key: " + rawSetting.Key);
            try
            {
                values[key] = NormalizeSettingValue(key, rawSetting.Value);
            }
            catch (ArgumentException ex)
            {
                throw new InvalidDataException("Invalid [Settings] " + rawSetting.Key + ": " + ex.Message, ex);
            }
        }
        return values;
    }

    static Dictionary<string, string> DefaultSettingValues(bool profileDefaults)
    {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { OldSidKey, "" },
            { NewUserKey, "" },
            { BackupDirectoryKey, DefaultBackupDirectoryName },
            { TaskPathKey, "" },
            { IncludeCredentialSensitiveTasksKey, "0" },
            { WhatIfKey, profileDefaults ? "1" : "0" },
            { ConfirmKey, profileDefaults ? "1" : "0" }
        };
    }

    static Options OptionsFromSettings(Dictionary<string, string> values, string profileDirectory)
    {
        return new Options
        {
            OldSID = values[OldSidKey],
            NewUser = values[NewUserKey],
            BackupDirectory = ResolveProfilePath(
                profileDirectory,
                String.IsNullOrWhiteSpace(values[BackupDirectoryKey])
                    ? DefaultBackupDirectoryName
                    : values[BackupDirectoryKey]),
            TaskPath = values[TaskPathKey],
            IncludeCredentialSensitiveTasks = ParseBoolean(
                values[IncludeCredentialSensitiveTasksKey],
                IncludeCredentialSensitiveTasksKey),
            WhatIf = ParseBoolean(values[WhatIfKey], WhatIfKey),
            Confirm = ParseBoolean(values[ConfirmKey], ConfirmKey)
        };
    }

    static void ApplyDirectSettings(Options options, Dictionary<string, string> values)
    {
        string value;
        if (values.TryGetValue(OldSidKey, out value)) options.OldSID = value;
        if (values.TryGetValue(NewUserKey, out value)) options.NewUser = value;
        if (values.TryGetValue(BackupDirectoryKey, out value))
            options.BackupDirectory = ResolveExecutableDirectoryPath(
                String.IsNullOrWhiteSpace(value) ? DefaultBackupDirectoryName : value);
        if (values.TryGetValue(TaskPathKey, out value)) options.TaskPath = value;
        if (values.TryGetValue(IncludeCredentialSensitiveTasksKey, out value))
            options.IncludeCredentialSensitiveTasks = ParseBoolean(value, IncludeCredentialSensitiveTasksKey);
        if (values.TryGetValue(WhatIfKey, out value)) options.WhatIf = ParseBoolean(value, WhatIfKey);
        if (values.TryGetValue(ConfirmKey, out value)) options.Confirm = ParseBoolean(value, ConfirmKey);
    }

    static void ValidateOptions(Options options)
    {
        options.OldSID = NormalizeOldSid(options.OldSID);
        options.NewUser = NormalizeNewUser(options.NewUser);
        options.TaskPath = NormalizeTaskPath(options.TaskPath);
        if (String.IsNullOrEmpty(options.BackupDirectory))
            throw new InvalidDataException("BackupDirectory must not be empty.");
    }

    static void PrintConfiguration(Options options)
    {
        Console.WriteLine("Configuration: " + options.IniPath);
        Console.WriteLine("OldSID = " + (options.OldSID ?? ""));
        Console.WriteLine("NewUser = " + (options.NewUser ?? ""));
        Console.WriteLine("BackupDirectory = " + options.BackupDirectory);
        Console.WriteLine("TaskPath = " + (options.TaskPath ?? ""));
        Console.WriteLine("IncludeCredentialSensitiveTasks = " + options.IncludeCredentialSensitiveTasks.ToString().ToLowerInvariant());
        Console.WriteLine("WhatIf = " + options.WhatIf.ToString().ToLowerInvariant());
        Console.WriteLine("Confirm = " + options.Confirm.ToString().ToLowerInvariant());
    }

    static ManagedIniFileSpec BuildIniFileSpec(string iniPath)
    {
        return new ManagedIniFileSpec
        {
            FilePath = iniPath,
            SectionName = SettingsSection,
            DefaultContents =
                "[Settings]" + Environment.NewLine +
                "; Profile runs default to preview and confirmation. Direct CLI use keeps its legacy behavior." + Environment.NewLine +
                OldSidKey + "=" + Environment.NewLine +
                NewUserKey + "=" + Environment.NewLine +
                BackupDirectoryKey + "=" + DefaultBackupDirectoryName + Environment.NewLine +
                TaskPathKey + "=" + Environment.NewLine +
                IncludeCredentialSensitiveTasksKey + "=0" + Environment.NewLine +
                WhatIfKey + "=1" + Environment.NewLine +
                ConfirmKey + "=1" + Environment.NewLine,
            Log = delegate(string message)
            {
                Console.Error.WriteLine("TaskSchedulerMigration configuration: " + message);
            }
        };
    }

    static string ResolveIniPath(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "TaskSchedulerMigration.ini");
        return ResolveExecutableDirectoryPath(value);
    }

    static string ResolveProfilePath(string profileDirectory, string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return null;
        try
        {
            return Path.GetFullPath(
                Path.IsPathRooted(value) ? value : Path.Combine(profileDirectory, value));
        }
        catch (Exception ex)
        {
            throw new ArgumentException("Invalid profile path '" + value + "': " + ex.Message, ex);
        }
    }

    static string ResolveExecutableDirectoryPath(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            throw new ArgumentException("A file path must not be empty.");
        try
        {
            string path = Path.IsPathRooted(value)
                ? value
                : Path.Combine(AppDomain.CurrentDomain.BaseDirectory, value);
            return Path.GetFullPath(path);
        }
        catch (Exception ex)
        {
            throw new ArgumentException("Invalid file path '" + value + "': " + ex.Message, ex);
        }
    }

    static string CanonicalSettingName(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return null;
        string key = value.Trim();
        if (key.Equals(OldSidKey, StringComparison.OrdinalIgnoreCase)) return OldSidKey;
        if (key.Equals(NewUserKey, StringComparison.OrdinalIgnoreCase)) return NewUserKey;
        if (key.Equals(BackupDirectoryKey, StringComparison.OrdinalIgnoreCase)) return BackupDirectoryKey;
        if (key.Equals(TaskPathKey, StringComparison.OrdinalIgnoreCase)) return TaskPathKey;
        if (key.Equals(IncludeCredentialSensitiveTasksKey, StringComparison.OrdinalIgnoreCase)) return IncludeCredentialSensitiveTasksKey;
        if (key.Equals(WhatIfKey, StringComparison.OrdinalIgnoreCase)) return WhatIfKey;
        if (key.Equals(ConfirmKey, StringComparison.OrdinalIgnoreCase)) return ConfirmKey;
        return null;
    }

    static string NormalizeSettingValue(string key, string value)
    {
        if (value == null || value.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0)
            throw new ArgumentException("The setting value is invalid.");
        if (key.Equals(OldSidKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeOldSid(value);
        if (key.Equals(NewUserKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeNewUser(value);
        if (key.Equals(BackupDirectoryKey, StringComparison.OrdinalIgnoreCase))
        {
            if (String.IsNullOrWhiteSpace(value)) return "";
            ValidatePath(value, BackupDirectoryKey);
            return value;
        }
        if (key.Equals(TaskPathKey, StringComparison.OrdinalIgnoreCase))
            return NormalizeTaskPath(value);
        if (key.Equals(IncludeCredentialSensitiveTasksKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(WhatIfKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(ConfirmKey, StringComparison.OrdinalIgnoreCase))
        {
            return ParseBoolean(value, key) ? "1" : "0";
        }
        throw new ArgumentException("Unknown setting: " + key);
    }

    static string NormalizeOldSid(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return "";
        try
        {
            return new SecurityIdentifier(value).Value;
        }
        catch (ArgumentException)
        {
            throw new ArgumentException("OldSID is not a valid Windows SID: " + value);
        }
    }

    static string NormalizeNewUser(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return "";
        if (value.Any(ch => Char.IsControl(ch)))
            throw new ArgumentException("NewUser contains control characters.");
        return value;
    }

    static string NormalizeTaskPath(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return "";
        if (value.Any(ch => Char.IsControl(ch)))
            throw new ArgumentException("TaskPath contains control characters.");
        return value;
    }

    static bool ParseBoolean(string value, string settingName)
    {
        string normalized = (value ?? "").Trim().ToLowerInvariant();
        if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
            return true;
        if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
            return false;
        throw new ArgumentException(settingName + " expects a boolean value.");
    }

    static void ValidatePath(string value, string settingName)
    {
        try
        {
            Path.GetFullPath(value);
        }
        catch (Exception ex)
        {
            throw new ArgumentException("Invalid " + settingName + " path: " + ex.Message, ex);
        }
    }

    static void Usage()
    {
        Console.WriteLine("Usage:");
        Console.WriteLine("  TaskSchedulerMigration.exe [-OldSID S-1-5-21-... -NewUser DOMAIN\\User] [options]");
        Console.WriteLine();
        Console.WriteLine("One-run migration options:");
        Console.WriteLine("  -TaskPath \\Folder\\                 scan only one task folder");
        Console.WriteLine("  -BackupDirectory PATH              choose the XML backup directory");
        Console.WriteLine("  -IncludeCredentialSensitiveTasks   allow S4U tasks (password modes remain blocked)");
        Console.WriteLine("  -WhatIf | --what-if                preview without backups or registration");
        Console.WriteLine("  --apply | --no-what-if             allow registration for this invocation");
        Console.WriteLine("  -Confirm | --confirm               prompt before each registration");
        Console.WriteLine("  --no-confirm                       skip prompts for this invocation");
        Console.WriteLine();
        Console.WriteLine("Persistent profile:");
        Console.WriteLine("  --ini PATH                         use a profile (default profile values preview and confirm)");
        Console.WriteLine("  --set Settings.Key=Value           persist OldSID, NewUser, BackupDirectory, TaskPath,");
        Console.WriteLine("                                     IncludeCredentialSensitiveTasks, WhatIf, or Confirm");
        Console.WriteLine("  --configure-only                   validate/create the profile and save --set values without Task Scheduler access");
        Console.WriteLine("  --show-config                      print effective profile values without Task Scheduler access");
        Console.WriteLine("  --help                             show help without side effects");
        Console.WriteLine("  --version                          show version without side effects");
    }

    static int Run(Options o)
    {
        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine("=== Task Migration START (SID -> User) ===");
        Console.ResetColor();
        Console.WriteLine("Old SID : " + o.OldSID);
        Console.WriteLine("New User: " + o.NewUser);
        Console.WriteLine();

        Type serviceType = Type.GetTypeFromProgID("Schedule.Service");
        if (serviceType == null)
            throw new InvalidOperationException("Task Scheduler COM service is not available.");
        dynamic service = null;
        var taskRefs = new List<TaskRef>();
        var folders = new List<object>();
        try
        {
            service = Activator.CreateInstance(serviceType);
            service.Connect();

            int enumerationFailures = 0;
            if (!String.IsNullOrWhiteSpace(o.TaskPath))
            {
                dynamic folder = service.GetFolder(o.TaskPath);
                EnumerateFolders(folder, taskRefs, folders, false, ref enumerationFailures);
            }
            else
            {
                dynamic root = service.GetFolder("\\");
                EnumerateFolders(root, taskRefs, folders, true, ref enumerationFailures);
            }

            int updated = 0;
            int skippedLogonPolicy = 0;
            int skippedUninspectable = 0;
            int failed = 0;

            foreach (var taskRef in taskRefs)
            {
                string fullName = taskRef.FullName;
                try
                {
                    string xml = (string)taskRef.Task.Xml;
                    if (!TaskXmlContainsUserId(xml, o.OldSID))
                        continue;

                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("[MATCH] UserId: " + fullName);
                    Console.ResetColor();

                    int logonType;
                    string logonError;
                    if (!TryGetLogonType(taskRef, out logonType, out logonError))
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("[SKIP] Could not inspect task logon type safely: " + fullName);
                        Console.ResetColor();
                        Console.WriteLine(logonError);
                        Console.WriteLine();
                        skippedUninspectable++;
                        continue;
                    }

                    int registrationLogonType;
                    string logonPolicyReason;
                    if (!TrySelectRegistrationLogonType(
                        logonType,
                        o.IncludeCredentialSensitiveTasks,
                        out registrationLogonType,
                        out logonPolicyReason))
                    {
                        Console.ForegroundColor = ConsoleColor.Magenta;
                        Console.WriteLine("[SKIP] Cannot preserve task logon safely: " + fullName + " (LogonType=" + LogonTypeName(logonType) + ")");
                        Console.ResetColor();
                        Console.WriteLine(logonPolicyReason);
                        Console.WriteLine();
                        skippedLogonPolicy++;
                        continue;
                    }

                    if (logonType == TASK_LOGON_S4U)
                    {
                        Console.ForegroundColor = ConsoleColor.Magenta;
                        Console.WriteLine("[WARN] Updating opted-in S4U task: " + fullName);
                        Console.ResetColor();
                    }

                    string updatedXml = UpdateTaskXmlUserId(xml, o.OldSID, o.NewUser);
                    if (!ShouldApply(
                        o,
                        fullName,
                        "Re-register scheduled task with matching UserId values changed to '" +
                            o.NewUser + "', preserving LogonType=" +
                            LogonTypeName(registrationLogonType) +
                            " and suppressing registration triggers"))
                        continue;

                    string backupPath = SaveBackupAtomically(
                        o.BackupDirectory,
                        fullName,
                        xml);
                    Console.ForegroundColor = ConsoleColor.DarkCyan;
                    Console.WriteLine("[BACKUP] " + backupPath);
                    Console.ResetColor();

                    Console.ForegroundColor = ConsoleColor.Cyan;
                    Console.WriteLine("[ACTION] Re-registering: " + fullName);
                    Console.ResetColor();
                    object registeredTask = taskRef.Folder.RegisterTask(
                        taskRef.Name,
                        updatedXml,
                        TASK_REGISTRATION_FLAGS,
                        null,
                        null,
                        registrationLogonType,
                        null);
                    ReleaseComObject(registeredTask);

                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("[SUCCESS] Updated: " + fullName);
                    Console.ResetColor();
                    Console.WriteLine();
                    updated++;
                }
                catch (Exception ex)
                {
                    failed++;
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("[ERROR] Failed: " + fullName);
                    Console.ResetColor();
                    Console.WriteLine(Unwrap(ex).Message);
                    Console.WriteLine();
                }
            }

            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("========================================");
            Console.ResetColor();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("Total updated tasks: " + updated);
            Console.ResetColor();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("Tasks skipped by logon-preservation policy: " + skippedLogonPolicy);
            Console.WriteLine("Tasks skipped because logon type could not be inspected: " + skippedUninspectable);
            Console.WriteLine("Task folders/items that could not be enumerated: " + enumerationFailures);
            Console.ResetColor();
            Console.ForegroundColor =
                failed == 0 && skippedUninspectable == 0 && enumerationFailures == 0
                    ? ConsoleColor.Green
                    : ConsoleColor.Red;
            Console.WriteLine("Failed tasks: " + failed);
            Console.ResetColor();
            Console.WriteLine("=== Task Migration COMPLETE ===");
            return failed == 0 && skippedUninspectable == 0 && enumerationFailures == 0 ? 0 : 3;
        }
        finally
        {
            foreach (TaskRef taskRef in taskRefs)
            {
                ReleaseComObject(taskRef.Task);
            }
            foreach (object folder in folders)
                ReleaseComObject(folder);
            ReleaseComObject(service);
        }
    }

    sealed class TaskRef
    {
        public dynamic Folder;
        public dynamic Task;
        public string Name;
        public string FullName;
    }

    static void EnumerateFolders(
        dynamic initialFolder,
        List<TaskRef> tasks,
        List<object> folders,
        bool recursive,
        ref int failures)
    {
        var pending = new Stack<object>();
        pending.Push(initialFolder);
        while (pending.Count != 0)
        {
            dynamic folder = pending.Pop();
            folders.Add(folder);
            string folderPath = SafeFolderPath(folder);

            dynamic taskCollection = null;
            try
            {
                taskCollection = folder.GetTasks(1);
                int taskCount = (int)taskCollection.Count;
                for (int i = 1; i <= taskCount; ++i)
                {
                    dynamic task = null;
                    try
                    {
                        task = taskCollection.Item(i);
                        string name = (string)task.Name;
                        string path = (string)task.Path;
                        tasks.Add(new TaskRef
                        {
                            Folder = folder,
                            Task = task,
                            Name = name,
                            FullName = path
                        });
                        task = null;
                    }
                    catch (Exception ex)
                    {
                        ReleaseComObject(task);
                        ReportEnumerationFailure(
                            "task item " + i + " in " + folderPath,
                            ex);
                        failures++;
                    }
                }
            }
            catch (Exception ex)
            {
                ReportEnumerationFailure("tasks in " + folderPath, ex);
                failures++;
            }
            finally
            {
                ReleaseComObject(taskCollection);
            }

            if (!recursive)
                continue;

            dynamic folderCollection = null;
            try
            {
                folderCollection = folder.GetFolders(0);
                int folderCount = (int)folderCollection.Count;
                for (int i = 1; i <= folderCount; ++i)
                {
                    dynamic child = null;
                    try
                    {
                        child = folderCollection.Item(i);
                        pending.Push(child);
                        child = null;
                    }
                    catch (Exception ex)
                    {
                        ReleaseComObject(child);
                        ReportEnumerationFailure(
                            "child folder " + i + " in " + folderPath,
                            ex);
                        failures++;
                    }
                }
            }
            catch (Exception ex)
            {
                ReportEnumerationFailure("child folders in " + folderPath, ex);
                failures++;
            }
            finally
            {
                ReleaseComObject(folderCollection);
            }
        }
    }

    static string SafeFolderPath(dynamic folder)
    {
        try
        {
            return (string)folder.Path;
        }
        catch
        {
            return "<unknown task folder>";
        }
    }

    static void ReportEnumerationFailure(string target, Exception ex)
    {
        Console.ForegroundColor = ConsoleColor.Red;
        Console.WriteLine("[ERROR] Could not enumerate " + target + ".");
        Console.ResetColor();
        Console.WriteLine(Unwrap(ex).Message);
    }

    static bool TaskXmlContainsUserId(string xml, string oldSid)
    {
        XmlDocument doc = LoadTaskXml(xml);
        foreach (XmlNode node in doc.GetElementsByTagName("UserId"))
        {
            if (node.InnerText == oldSid)
                return true;
        }
        return false;
    }

    static string UpdateTaskXmlUserId(string xml, string from, string to)
    {
        XmlDocument doc = LoadTaskXml(xml);
        bool changed = false;
        foreach (XmlNode node in doc.GetElementsByTagName("UserId"))
        {
            if (node.InnerText == from)
            {
                node.InnerText = to;
                changed = true;
            }
        }
        if (!changed)
            throw new InvalidOperationException("Matched task XML did not contain a UserId node for " + from);
        return doc.OuterXml;
    }

    static XmlDocument LoadTaskXml(string xml)
    {
        var settings = new XmlReaderSettings
        {
            DtdProcessing = DtdProcessing.Prohibit,
            XmlResolver = null
        };
        var doc = new XmlDocument
        {
            PreserveWhitespace = true,
            XmlResolver = null
        };
        using (var text = new StringReader(xml ?? ""))
        using (XmlReader reader = XmlReader.Create(text, settings))
            doc.Load(reader);
        return doc;
    }

    static bool TryGetLogonType(TaskRef taskRef, out int logonType, out string error)
    {
        dynamic definition = null;
        dynamic principal = null;
        try
        {
            definition = taskRef.Task.Definition;
            principal = definition.Principal;
            logonType = (int)principal.LogonType;
            error = "";
            return true;
        }
        catch (Exception ex)
        {
            logonType = TASK_LOGON_NONE;
            error = Unwrap(ex).Message;
            return false;
        }
        finally
        {
            ReleaseComObject(principal);
            ReleaseComObject(definition);
        }
    }

    static bool ShouldApply(Options o, string target, string action)
    {
        if (o.WhatIf)
        {
            Console.WriteLine("WHATIF: " + action + " -> " + target);
            return false;
        }
        if (!o.Confirm)
            return true;
        Console.Write(action + " " + target + "? [y/N] ");
        string answer = Console.ReadLine() ?? "";
        return answer.Equals("y", StringComparison.OrdinalIgnoreCase) || answer.Equals("yes", StringComparison.OrdinalIgnoreCase);
    }

    static bool TrySelectRegistrationLogonType(
        int originalLogonType,
        bool includeCredentialSensitiveTasks,
        out int registrationLogonType,
        out string reason)
    {
        registrationLogonType = originalLogonType;
        reason = "";

        switch (originalLogonType)
        {
            case TASK_LOGON_NONE:
            case TASK_LOGON_INTERACTIVE_TOKEN:
            case TASK_LOGON_GROUP:
            case TASK_LOGON_SERVICE_ACCOUNT:
                return true;

            case TASK_LOGON_S4U:
                if (includeCredentialSensitiveTasks)
                    return true;
                reason =
                    "S4U tasks require explicit opt-in because their restricted token can change which resources are available to the migrated account. " +
                    "Re-run with -IncludeCredentialSensitiveTasks after reviewing the task.";
                return false;

            case TASK_LOGON_PASSWORD:
                reason =
                    "Password logon tasks are not migrated because Task Scheduler does not expose the saved password, and re-registering without it cannot preserve the task's credentials.";
                return false;

            case TASK_LOGON_INTERACTIVE_TOKEN_OR_PASSWORD:
                reason =
                    "InteractiveOrPassword tasks are not migrated because the existing password cannot be recovered and silently falling back to an interactive token would change logon behavior.";
                return false;

            default:
                reason =
                    "Unknown Task Scheduler logon type " + originalLogonType +
                    " is not safe to pass through without defined credential semantics.";
                return false;
        }
    }

    static string LogonTypeName(int logonType)
    {
        if (logonType == TASK_LOGON_NONE) return "None";
        if (logonType == TASK_LOGON_PASSWORD) return "Password";
        if (logonType == TASK_LOGON_S4U) return "S4U";
        if (logonType == TASK_LOGON_INTERACTIVE_TOKEN) return "InteractiveToken";
        if (logonType == TASK_LOGON_GROUP) return "Group";
        if (logonType == TASK_LOGON_SERVICE_ACCOUNT) return "ServiceAccount";
        if (logonType == TASK_LOGON_INTERACTIVE_TOKEN_OR_PASSWORD) return "InteractiveOrPassword";
        return logonType.ToString();
    }

    static string ProductVersion()
    {
        Version version = Assembly.GetExecutingAssembly().GetName().Version;
        return version == null ? "1.0.0.0" : version.ToString();
    }

    static Exception Unwrap(Exception ex)
    {
        while ((ex is TargetInvocationException || ex is COMException) && ex.InnerException != null)
            ex = ex.InnerException;
        return ex;
    }

    static string SafeFileName(string name)
    {
        string invalid = Regex.Escape(new string(Path.GetInvalidFileNameChars()));
        return Regex.Replace(name, "[" + invalid + "]", "_");
    }

    static string BuildBackupPath(string backupDirectory, string fullTaskName)
    {
        string trimmed = (fullTaskName ?? "").Trim('\\');
        string leaf = trimmed;
        int separator = trimmed.LastIndexOf('\\');
        if (separator >= 0 && separator + 1 < trimmed.Length)
            leaf = trimmed.Substring(separator + 1);
        leaf = SafeFileName(leaf);
        if (String.IsNullOrWhiteSpace(leaf))
            leaf = "task";
        if (leaf.Length > 80)
            leaf = leaf.Substring(0, 80);

        string hash;
        using (SHA256 sha = SHA256.Create())
        {
            byte[] digest = sha.ComputeHash(Encoding.UTF8.GetBytes(fullTaskName ?? ""));
            hash = BitConverter.ToString(digest, 0, 8).Replace("-", "").ToLowerInvariant();
        }

        string timestamp = DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff");
        string nonce = Guid.NewGuid().ToString("N");
        return Path.Combine(
            backupDirectory,
            leaf + "-" + hash + "-" + timestamp + "-" + nonce + ".xml");
    }

    static string SaveBackupAtomically(
        string backupDirectory,
        string fullTaskName,
        string xml)
    {
        Directory.CreateDirectory(backupDirectory);
        const int MaximumNameReservations = 16;
        for (int attempt = 0; attempt < MaximumNameReservations; ++attempt)
        {
            string backupPath = BuildBackupPath(backupDirectory, fullTaskName);
            string temporaryPath = Path.Combine(
                backupDirectory,
                ".TaskSchedulerMigration-" + Guid.NewGuid().ToString("N") + ".tmp");
            try
            {
                File.WriteAllText(temporaryPath, xml, new UTF8Encoding(true));
                try
                {
                    // The temporary file is in the destination directory, so this is
                    // a same-volume rename. File.Move refuses to overwrite an existing
                    // backup and therefore closes the name-reservation race.
                    File.Move(temporaryPath, backupPath);
                    return backupPath;
                }
                catch (IOException)
                {
                    if (!File.Exists(backupPath))
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
                catch
                {
                }
            }
        }
        throw new IOException(
            "Could not reserve a unique task-backup filename after " +
            MaximumNameReservations + " attempts.");
    }

    static void ReleaseComObject(object value)
    {
        if (value == null || !Marshal.IsComObject(value))
            return;
        try
        {
            Marshal.FinalReleaseComObject(value);
        }
        catch
        {
        }
    }

    static string RequireValue(string[] args, ref int index, string option)
    {
        if (index + 1 >= args.Length || IsKnownOption(args[index + 1]))
            throw new ArgumentException("Missing value for " + option + ".");
        return args[++index];
    }

    static bool IsKnownOption(string value)
    {
        string[] options =
        {
            "-h", "--help", "/?", "-v", "--version", "-Version",
            "-IncludeCredentialSensitiveTasks",
            "--include-credential-sensitive-tasks", "-WhatIf", "--what-if",
            "--no-include-credential-sensitive-tasks", "--apply", "--no-what-if",
            "-Confirm", "--confirm", "--no-confirm", "-OldSID", "--old-sid", "-NewUser",
            "--new-user", "-BackupDirectory", "--backup-directory", "-TaskPath",
            "--task-path", "--ini", "-IniFile", "--set", "--configure-only",
            "--show-config", "--print-config"
        };
        return options.Any(option => Is(value, option));
    }

    static bool IsHelpOption(string value)
    {
        return Is(value, "-h") || Is(value, "--help") || Is(value, "/?");
    }

    static bool IsVersionOption(string value)
    {
        return Is(value, "-v") || Is(value, "--version") || Is(value, "-Version");
    }

    static bool Is(string a, string b) { return String.Equals(a, b, StringComparison.OrdinalIgnoreCase); }
}

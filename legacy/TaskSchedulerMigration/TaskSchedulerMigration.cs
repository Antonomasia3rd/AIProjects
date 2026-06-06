using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;

static class TaskSchedulerMigration
{
    const int TASK_CREATE_OR_UPDATE = 6;
    const int TASK_LOGON_NONE = 0;

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
    }

    static int Main(string[] args)
    {
        try
        {
            var options = ParseArgs(args);
            if (options.Help)
            {
                Usage();
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
        var o = new Options();
        for (int i = 0; i < args.Length; ++i)
        {
            string a = args[i];
            if (Is(a, "-h") || Is(a, "--help") || Is(a, "/?")) { o.Help = true; continue; }
            if (Is(a, "-IncludeCredentialSensitiveTasks") || Is(a, "--include-credential-sensitive-tasks")) { o.IncludeCredentialSensitiveTasks = true; continue; }
            if (Is(a, "-WhatIf") || Is(a, "--what-if")) { o.WhatIf = true; continue; }
            if (Is(a, "-Confirm") || Is(a, "--confirm")) { o.Confirm = true; continue; }

            string value = i + 1 < args.Length ? args[++i] : "";
            if (Is(a, "-OldSID") || Is(a, "--old-sid")) o.OldSID = value;
            else if (Is(a, "-NewUser") || Is(a, "--new-user")) o.NewUser = value;
            else if (Is(a, "-BackupDirectory") || Is(a, "--backup-directory")) o.BackupDirectory = value;
            else if (Is(a, "-TaskPath") || Is(a, "--task-path")) o.TaskPath = value;
            else throw new ArgumentException("Unknown argument: " + a);
        }

        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        if (String.IsNullOrWhiteSpace(o.BackupDirectory))
            o.BackupDirectory = Path.Combine(baseDir, "TaskSchedulerMigrationBackup");
        else if (!Path.IsPathRooted(o.BackupDirectory))
            o.BackupDirectory = Path.Combine(baseDir, o.BackupDirectory);
        return o;
    }

    static void Usage()
    {
        Console.WriteLine("Usage:");
        Console.WriteLine("  TaskSchedulerMigration.exe -OldSID S-1-5-21-... -NewUser DOMAIN\\User [-TaskPath \\Folder\\] [-WhatIf]");
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
        dynamic service = Activator.CreateInstance(serviceType);
        service.Connect();

        var taskRefs = new List<TaskRef>();
        if (!String.IsNullOrWhiteSpace(o.TaskPath))
        {
            dynamic folder = service.GetFolder(o.TaskPath);
            EnumerateFolder(folder, taskRefs, false);
        }
        else
        {
            dynamic root = service.GetFolder("\\");
            EnumerateFolder(root, taskRefs, true);
        }

        int updated = 0;
        int skippedCredential = 0;
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

                int logonType = 0;
                try { logonType = (int)taskRef.Task.Definition.Principal.LogonType; } catch { }
                if (IsCredentialSensitive(logonType))
                {
                    string message = "[SKIP] Credential-sensitive task: " + fullName + " (LogonType=" + LogonTypeName(logonType) + "). Re-run with -IncludeCredentialSensitiveTasks only after confirming credentials/logon behavior.";
                    if (!o.IncludeCredentialSensitiveTasks)
                    {
                        Console.ForegroundColor = ConsoleColor.Magenta;
                        Console.WriteLine(message);
                        Console.ResetColor();
                        Console.WriteLine();
                        skippedCredential++;
                        continue;
                    }
                    Console.ForegroundColor = ConsoleColor.Magenta;
                    Console.WriteLine("[WARN] Updating credential-sensitive task: " + fullName + " (LogonType=" + LogonTypeName(logonType) + ")");
                    Console.ResetColor();
                }

                string updatedXml = UpdateTaskXmlUserId(xml, o.OldSID, o.NewUser);
                if (!ShouldApply(o, fullName, "Re-register scheduled task with matching UserId values changed to '" + o.NewUser + "'"))
                    continue;

                Directory.CreateDirectory(o.BackupDirectory);
                string backupPath = BuildBackupPath(o.BackupDirectory, fullName);
                File.WriteAllText(backupPath, xml, Encoding.UTF8);
                Console.ForegroundColor = ConsoleColor.DarkCyan;
                Console.WriteLine("[BACKUP] " + backupPath);
                Console.ResetColor();

                Console.ForegroundColor = ConsoleColor.Cyan;
                Console.WriteLine("[ACTION] Re-registering: " + fullName);
                Console.ResetColor();
                taskRef.Folder.RegisterTask(taskRef.Name, updatedXml, TASK_CREATE_OR_UPDATE, null, null, TASK_LOGON_NONE, null);

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
        Console.WriteLine("Credential-sensitive tasks skipped: " + skippedCredential);
        Console.ResetColor();
        Console.ForegroundColor = failed == 0 ? ConsoleColor.Green : ConsoleColor.Red;
        Console.WriteLine("Failed tasks: " + failed);
        Console.ResetColor();
        Console.WriteLine("=== Task Migration COMPLETE ===");
        return failed == 0 ? 0 : 3;
    }

    sealed class TaskRef
    {
        public dynamic Folder;
        public dynamic Task;
        public string Name;
        public string FullName;
    }

    static void EnumerateFolder(dynamic folder, List<TaskRef> tasks, bool recursive)
    {
        foreach (dynamic task in folder.GetTasks(1))
        {
            string name = (string)task.Name;
            string path = (string)task.Path;
            tasks.Add(new TaskRef { Folder = folder, Task = task, Name = name, FullName = path });
        }
        if (!recursive)
            return;
        foreach (dynamic child in folder.GetFolders(0))
            EnumerateFolder(child, tasks, true);
    }

    static bool TaskXmlContainsUserId(string xml, string oldSid)
    {
        var doc = new XmlDocument { PreserveWhitespace = true };
        doc.LoadXml(xml);
        foreach (XmlNode node in doc.GetElementsByTagName("UserId"))
        {
            if (node.InnerText == oldSid)
                return true;
        }
        return false;
    }

    static string UpdateTaskXmlUserId(string xml, string from, string to)
    {
        var doc = new XmlDocument { PreserveWhitespace = true };
        doc.LoadXml(xml);
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

    static bool IsCredentialSensitive(int logonType)
    {
        return logonType == 1 || logonType == 2 || logonType == 6;
    }

    static string LogonTypeName(int logonType)
    {
        if (logonType == 1) return "Password";
        if (logonType == 2) return "S4U";
        if (logonType == 6) return "InteractiveOrPassword";
        return logonType.ToString();
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
        string fileName = leaf + "-" + hash + "-" + timestamp + ".xml";
        string path = Path.Combine(backupDirectory, fileName);
        int suffix = 2;
        while (File.Exists(path))
        {
            fileName = leaf + "-" + hash + "-" + timestamp + "-" + suffix + ".xml";
            path = Path.Combine(backupDirectory, fileName);
            suffix++;
        }
        return path;
    }

    static bool Is(string a, string b) { return String.Equals(a, b, StringComparison.OrdinalIgnoreCase); }
}

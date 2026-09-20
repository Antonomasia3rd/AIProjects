using System;
using System.IO;

static class RegistryNotificationServiceSourceCheck
{
    private static int checks;

    static int Main(string[] args)
    {
        try
        {
            string root = args.Length > 0
                ? Path.GetFullPath(args[0])
                : Path.GetFullPath(Path.Combine(
                    AppDomain.CurrentDomain.BaseDirectory,
                    ".."));
            string shared = Read(root, "dependencies", "registry_notification_service.cs");
            string allow = Read(root, "legacy", "AllowContentAboveLock", "AllowContentAboveLock.cs");
            string phone = Read(root, "legacy", "YourPhoneHideBanner", "YourPhoneHideBanner.cs");
            string allowReadme = Read(root, "legacy", "AllowContentAboveLock", "README.md");
            string phoneReadme = Read(root, "legacy", "YourPhoneHideBanner", "README.md");

            Require(
                "AllowContent is a declarative registry policy overlay",
                allow,
                "new RegistryNotificationPolicy(");
            Require(
                "Phone Link is a declarative registry policy overlay",
                phone,
                "new RegistryNotificationPolicy(");
            Require(
                "both overlays consume the shared policy service",
                allow + phone,
                "new RegistryNotificationPolicyService(");
            Require(
                "watcher workers contain unhandled exceptions",
                shared,
                "Registry watcher terminated unexpectedly:");
            Require(
                "HKEY_USERS enumeration failures are contained",
                shared,
                "Could not enumerate HKEY_USERS:");
            Require(
                "deleted notification keys fall back to root watching",
                shared,
                "if (running)\n                EnsureRootWatcher(sid);");
            Require(
                "service stop uses one aggregate timeout",
                shared,
                "timeoutMilliseconds - (int)Math.Min(");
            Require(
                "invalid logging booleans fail the protected configuration",
                shared,
                "Invalid [Settings] LoggingEnabled value");
            Require(
                "shared event diagnostics are serialized",
                shared,
                "lock (logLock)");
            Require(
                "ServiceBase automatic registry-backed logging is disabled",
                shared,
                "AutoLog = false;");
            Require(
                "protected INI duplicates use the repository last-value rule",
                shared,
                "logging = parsed;");
            Require(
                "malformed protected Settings assignments fail closed",
                shared,
                "Malformed assignment in [Settings]");
            RequireOrderAfter(
                "HKEY_USERS notifications are armed before enumeration",
                shared,
                "private void WatchUsersRoot()",
                "int status = RegNotifyChangeKeyValue(",
                "AttachToExistingUsers();");
            RequireOrderAfter(
                "user-root notifications are armed before probing settings",
                shared,
                "private void WatchUserRoot(",
                "int status = RegNotifyChangeKeyValue(",
                "bool settingsExist;");
            RequireOrderAfter(
                "per-user notifications are armed before processing keys",
                shared,
                "private void WatchUserKey(",
                "int status = RegNotifyChangeKeyValue(",
                "ProcessAllKeys(baseKey, sid);");
            Require(
                "AllowContent declares its desired DWORD only",
                allow,
                "RegistryNotificationValuePolicy.Dword(\n                \"AllowContentAboveLock\",\n                1)");
            Require(
                "Phone Link declares its desired DWORD only",
                phone,
                "RegistryNotificationValuePolicy.Dword(\"ShowBanner\", 0)");
            Require(
                "Phone Link declares its desired string only",
                phone,
                "RegistryNotificationValuePolicy.String(\"SoundFile\", \"\")");
            Require(
                "shared policy values are applied independently",
                shared,
                "foreach (RegistryNotificationValuePolicy value in policy.Values)");
            Require(
                "shared policy value failures are contained independently",
                shared,
                "Could not update \" + value.ValueName + \" for ");
            Forbid(
                "AllowContent overlay does not enumerate registry keys",
                allow,
                "GetSubKeyNames");
            Forbid(
                "Phone Link overlay does not open registry keys",
                phone,
                "OpenSubKey");
            Require(
                "shared DWORD repair requires REG_DWORD",
                shared,
                "key.GetValueKind(valueName) == RegistryValueKind.DWord");
            Require(
                "shared string repair requires REG_SZ",
                shared,
                "key.GetValueKind(valueName) == RegistryValueKind.String");
            Require(
                "AllowContent uses the shared service command host",
                allow,
                "return ManagedPrivilegedServiceHost.Run(");
            Require(
                "Phone Link uses the shared service command host",
                phone,
                "return ManagedPrivilegedServiceHost.Run(");
            Require(
                "both service entry points accept explicit commands",
                allow + phone,
                "Main(string[] args)");
            RequireOrderAfter(
                "runtime trust is established before registry watcher startup",
                shared,
                "protected override void OnStart(string[] args)",
                "AcquireRuntimeTrust();",
                "StartWatcher(WatchUsersRoot)");
            RequireOrderAfter(
                "runtime identity is checked before privileged path opening",
                shared,
                "private void AcquireRuntimeTrust()",
                "IsCurrentProcessLocalSystem()",
                "TryOpenProgramFilesFile(");
            Require(
                "runtime requires the LocalSystem account",
                shared,
                "The service account is not LocalSystem.");
            Require(
                "known Program Files roots come from Windows known folders",
                shared,
                "SHGetKnownFolderPath(");
            Require(
                "path walking opens reparse points instead of following them",
                shared,
                "FileFlagOpenReparsePoint");
            Require(
                "privileged components reject reparse points",
                shared,
                "Refusing reparse-point ");
            Require(
                "privileged components are checked by final handle path",
                shared,
                "GetFinalPathNameByHandleW(");
            Require(
                "privileged components require a protected owner and DACL",
                shared,
                "HasProtectedOwnerAndDacl(");
            Require(
                "unprivileged dangerous allow ACEs are rejected",
                shared,
                "writable by a non-privileged principal");
            Require(
                "all validated path components remain pinned",
                shared,
                "private readonly List<SafeFileHandle> handles;");
            Require(
                "runtime retains executable and configuration pins",
                shared,
                "private ManagedPrivilegedPathPin executablePin;");
            Require(
                "configuration is read through the retained file handle",
                shared,
                "configuration.ReadAllText(64 * 1024)");
            RequireOrderAfter(
                "installer validates executable before opening the SCM",
                shared,
                "private static void Install(",
                "CurrentExecutablePath(),",
                "OpenSCManagerW(");
            RequireOrderAfter(
                "installer validates configuration before opening the SCM",
                shared,
                "private static void Install(",
                "\"service configuration\"",
                "OpenSCManagerW(");
            RequireOrderAfter(
                "installer parses protected configuration before creating a service",
                shared,
                "private static void Install(",
                "ReadLoggingSetting(configuration);",
                "CreateServiceW(");
            Require(
                "installer explicitly registers LocalSystem",
                shared,
                "\"LocalSystem\",\n                    null);");
            RequireOrderAfter(
                "post-create configuration failures attempt service rollback",
                shared,
                "private static void Install(",
                "catch (Exception configurationError)",
                "bool rollbackDeleted = DeleteService(service);");
            Require(
                "service rollback failures are explicitly reported",
                shared,
                "DeleteService also failed with error ");
            RequireOrderAfter(
                "uninstaller verifies registered path and account before deletion",
                shared,
                "private static void Uninstall(",
                "ValidateRegisteredService(service, executable.FinalPath);",
                "DeleteService(service)");
            Require(
                "diagnostics use native Windows Event Log reporting",
                shared,
                "ReportEventW(");
            Forbid(
                "privileged service engine does not append sidecar logs",
                shared,
                "File.AppendAllText");
            Forbid(
                "privileged service engine does not create sibling INI files",
                shared,
                "File.WriteAllText");
            Forbid(
                "event logging does not register a source in the registry",
                shared,
                "CreateEventSource");
            Forbid(
                "AllowContent documentation no longer installs a checkout build",
                allowReadme,
                "sc.exe create");
            Forbid(
                "Phone Link documentation no longer installs a checkout build",
                phoneReadme,
                "sc.exe create");

            Console.WriteLine(
                "Registry notification service source checks passed (" +
                checks + " checks).");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.Message);
            return 1;
        }
    }

    private static string Read(string root, params string[] parts)
    {
        string path = root;
        foreach (string part in parts)
            path = Path.Combine(path, part);
        if (!File.Exists(path))
            throw new InvalidOperationException("Missing source file: " + path);
        return File.ReadAllText(path).Replace("\r\n", "\n");
    }

    private static void Require(string name, string source, string needle)
    {
        checks++;
        if (!source.Contains(needle))
        {
            throw new InvalidOperationException(
                "Registry notification service regression: " +
                name + " missing " + needle);
        }
        Console.WriteLine("ok - " + name);
    }

    private static void Forbid(string name, string source, string needle)
    {
        checks++;
        if (source.Contains(needle))
        {
            throw new InvalidOperationException(
                "Registry notification service regression: " +
                name + " still contains " + needle);
        }
        Console.WriteLine("ok - " + name);
    }

    private static void RequireOrderAfter(
        string name,
        string source,
        string anchor,
        string earlier,
        string later)
    {
        checks++;
        int anchorIndex = source.IndexOf(anchor, StringComparison.Ordinal);
        int earlierIndex = anchorIndex < 0
            ? -1
            : source.IndexOf(earlier, anchorIndex, StringComparison.Ordinal);
        int laterIndex = earlierIndex < 0
            ? -1
            : source.IndexOf(later, earlierIndex + earlier.Length, StringComparison.Ordinal);
        if (anchorIndex < 0 || earlierIndex < 0 || laterIndex < 0)
        {
            throw new InvalidOperationException(
                "Registry notification service regression: " +
                name + " does not preserve the required source order.");
        }
        Console.WriteLine("ok - " + name);
    }
}

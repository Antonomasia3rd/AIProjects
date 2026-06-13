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

            Require(
                "services consume the shared registry watcher",
                allow,
                "AllowContentService : RegistryNotificationServiceBase");
            Require(
                "Phone Link service consumes the shared registry watcher",
                phone,
                "RegistryNotificationServiceBase");
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
                "invalid logging booleans use the configured fallback",
                shared,
                "? parsed\n                    : fallback;");
            Require(
                "shared log appends are serialized",
                shared,
                "lock (logLock)");

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
}

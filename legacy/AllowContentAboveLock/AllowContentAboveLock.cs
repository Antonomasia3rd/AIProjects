using System;
using System.Reflection;

[assembly: AssemblyTitle("AllowContentAboveLock")]
[assembly: AssemblyProduct("AIProjects AllowContentAboveLock")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

public static class AllowContentAboveLockProgram
{
    private static readonly RegistryNotificationPolicy Policy =
        new RegistryNotificationPolicy(
            null,
            RegistryNotificationValuePolicy.Dword(
                "AllowContentAboveLock",
                1));

    public static int Main(string[] args)
    {
        return ManagedPrivilegedServiceHost.Run(
            args,
            "AllowContentAboveLockService",
            "Allow content above the lock screen",
            "Keeps loaded-user notification entries configured to allow " +
                "content above the lock screen.",
            delegate()
            {
                return new RegistryNotificationPolicyService(
                    "AllowContentAboveLockService",
                    Policy);
            });
    }
}

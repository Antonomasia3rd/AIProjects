using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;

static class RegistryNotificationServiceRuntimeTests
{
    private static int checks;

    static int Main()
    {
        try
        {
            ExpectRejected("relative path", @"relative\service.exe");
            ExpectRejected(
                "alternate data stream",
                @"C:\Program Files\AIProjects\service.exe:payload");
            ExpectRejected(
                "UNC path",
                @"\\localhost\C$\Program Files\AIProjects\service.exe");
            ExpectRejected(
                "extended path spelling",
                @"\\?\C:\Program Files\AIProjects\service.exe");

            string currentExecutable =
                ManagedPrivilegedPathTrust.CurrentExecutablePath();
            ManagedPrivilegedPathPin currentPin;
            string currentError;
            checks++;
            if (ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
                currentExecutable,
                "test executable",
                out currentPin,
                out currentError))
            {
                currentPin.Dispose();
                throw new InvalidOperationException(
                    "The temporary test executable unexpectedly passed the " +
                    "Program Files-only boundary.");
            }
            if (String.IsNullOrEmpty(currentError) ||
                currentError.IndexOf(
                    "outside the Program Files directories",
                    StringComparison.OrdinalIgnoreCase) < 0)
            {
                throw new InvalidOperationException(
                    "The temporary test executable failed for an unexpected " +
                    "reason: " + currentError);
            }
            Console.WriteLine("ok - temporary executable is outside Program Files");

            VerifyProtectedProgramFilesCandidate();
            VerifyLoggingParser();
            VerifyServiceBaseLoggingPolicy();
            VerifyDeclarativePolicies();

            string ini = Path.ChangeExtension(currentExecutable, ".ini");
            string log = Path.ChangeExtension(currentExecutable, ".log");
            checks++;
            if (File.Exists(ini) || File.Exists(log))
            {
                throw new InvalidOperationException(
                    "Path validation created a sibling INI or log file.");
            }
            Console.WriteLine("ok - path validation creates no sidecars");

            Console.WriteLine(
                "Registry notification service runtime checks passed (" +
                checks + " checks).");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.Message);
            return 1;
        }
    }

    private static void VerifyLoggingParser()
    {
        ExpectLogging("default logging", "[Settings]\r\n", true);
        ExpectLogging(
            "disabled logging alias",
            "[Settings]\r\nLoggingEnabled=off\r\n",
            false);
        ExpectLogging(
            "last duplicate logging assignment wins",
            "[Settings]\r\nLoggingEnabled=0\r\nLoggingEnabled=yes\r\n",
            true);
        ExpectLoggingFailure(
            "invalid logging boolean",
            "[Settings]\r\nLoggingEnabled=maybe\r\n");
        ExpectLoggingFailure(
            "malformed Settings assignment",
            "[Settings]\r\nLoggingEnabled\r\n");
        ExpectLoggingFailure(
            "malformed section header",
            "[Settings\r\nLoggingEnabled=0\r\n");
    }

    private static void ExpectLogging(
        string name,
        string contents,
        bool expected)
    {
        checks++;
        bool actual = RegistryNotificationServiceBase.ParseLoggingSetting(
            contents);
        if (actual != expected)
        {
            throw new InvalidOperationException(
                "Unexpected protected INI result for " + name + ".");
        }
        Console.WriteLine("ok - " + name);
    }

    private static void ExpectLoggingFailure(string name, string contents)
    {
        checks++;
        try
        {
            RegistryNotificationServiceBase.ParseLoggingSetting(contents);
        }
        catch (InvalidOperationException)
        {
            Console.WriteLine("ok - rejects " + name);
            return;
        }
        throw new InvalidOperationException(
            "Protected INI unexpectedly accepted " + name + ".");
    }

    private static void VerifyServiceBaseLoggingPolicy()
    {
        checks++;
        using (RuntimeTestService service = new RuntimeTestService())
        {
            if (service.AutoLog)
            {
                throw new InvalidOperationException(
                    "ServiceBase automatic EventLog integration remains enabled.");
            }
        }
        Console.WriteLine("ok - ServiceBase automatic logging is disabled");
    }

    private static void VerifyDeclarativePolicies()
    {
        RegistryNotificationPolicy allKeys = new RegistryNotificationPolicy(
            null,
            RegistryNotificationValuePolicy.Dword("Allow", 1));
        RegistryNotificationPolicy prefixed = new RegistryNotificationPolicy(
            "Phone_",
            RegistryNotificationValuePolicy.Dword("ShowBanner", 0),
            RegistryNotificationValuePolicy.String("SoundFile", ""));

        checks++;
        if (!allKeys.Matches("anything") || allKeys.Matches(null))
            throw new InvalidOperationException("All-key policy matching failed.");
        Console.WriteLine("ok - declarative all-key policy matching");

        checks++;
        if (!prefixed.Matches("phone_example") ||
            prefixed.Matches("unrelated"))
        {
            throw new InvalidOperationException(
                "Prefix policy matching is not case-insensitive and exact.");
        }
        Console.WriteLine("ok - declarative prefix policy matching");

        string testPath =
            @"Software\AIProjectsRegistryNotificationPolicyTest_" +
            Guid.NewGuid().ToString("N");
        try
        {
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(testPath))
            {
                if (key == null)
                    throw new InvalidOperationException("Could not create policy test key.");

                key.SetValue("ShowBanner", "wrong type", RegistryValueKind.String);
                key.SetValue("SoundFile", 7, RegistryValueKind.DWord);
                RegistryNotificationValuePolicy banner = prefixed.Values[0];
                RegistryNotificationValuePolicy sound = prefixed.Values[1];

                checks++;
                if (!banner.Ensure(key) || !sound.Ensure(key))
                {
                    throw new InvalidOperationException(
                        "Declarative policies did not repair malformed values.");
                }
                Console.WriteLine("ok - declarative policies repair malformed values");

                checks++;
                if (key.GetValueKind("ShowBanner") != RegistryValueKind.DWord ||
                    (int)key.GetValue("ShowBanner") != 0 ||
                    key.GetValueKind("SoundFile") != RegistryValueKind.String ||
                    (string)key.GetValue("SoundFile") != "")
                {
                    throw new InvalidOperationException(
                        "Declarative policies wrote unexpected value kinds or data.");
                }
                Console.WriteLine("ok - declarative policies preserve exact kinds");

                checks++;
                if (banner.Ensure(key) || sound.Ensure(key))
                {
                    throw new InvalidOperationException(
                        "Declarative policies rewrote already-canonical values.");
                }
                Console.WriteLine("ok - declarative policies are idempotent");
            }
        }
        finally
        {
            Registry.CurrentUser.DeleteSubKeyTree(testPath, false);
        }
    }

    private static void ExpectRejected(string name, string path)
    {
        checks++;
        ManagedPrivilegedPathPin pin;
        string error;
        if (ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
            path,
            name,
            out pin,
            out error))
        {
            pin.Dispose();
            throw new InvalidOperationException(
                "Unsafe path unexpectedly passed: " + name);
        }
        if (String.IsNullOrEmpty(error))
            throw new InvalidOperationException("Missing rejection: " + name);
        Console.WriteLine("ok - rejects " + name);
    }

    private static void VerifyProtectedProgramFilesCandidate()
    {
        List<string> candidates = new List<string>();
        string programFiles = Environment.GetFolderPath(
            Environment.SpecialFolder.ProgramFiles);
        string programFilesX86 = Environment.GetFolderPath(
            Environment.SpecialFolder.ProgramFilesX86);
        AddCandidate(
            candidates,
            programFiles,
            @"Windows Defender\MpCmdRun.exe");
        AddCandidate(
            candidates,
            programFiles,
            @"Internet Explorer\iexplore.exe");
        AddCandidate(
            candidates,
            programFiles,
            @"Common Files\microsoft shared\ClickToRun\OfficeClickToRun.exe");
        AddCandidate(
            candidates,
            programFilesX86,
            @"Internet Explorer\iexplore.exe");

        string lastError = null;
        foreach (string candidate in candidates)
        {
            if (!File.Exists(candidate))
                continue;
            ManagedPrivilegedPathPin pin;
            string error;
            if (!ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
                candidate,
                "known protected Program Files file",
                out pin,
                out error))
            {
                lastError = candidate + ": " + error;
                continue;
            }
            using (pin)
            {
                checks++;
                if (!String.Equals(
                    pin.FinalPath,
                    Path.GetFullPath(candidate),
                    StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        "Protected candidate returned an unexpected final path.");
                }
            }
            Console.WriteLine(
                "ok - accepts and pins a protected Program Files file");
            return;
        }

        throw new InvalidOperationException(
            "No protected Program Files test candidate passed validation. " +
            (lastError ?? "No standard candidate exists on this host."));
    }

    private static void AddCandidate(
        List<string> candidates,
        string root,
        string relativePath)
    {
        if (String.IsNullOrEmpty(root))
            return;
        string path = Path.Combine(root, relativePath);
        if (!candidates.Exists(value => String.Equals(
            value,
            path,
            StringComparison.OrdinalIgnoreCase)))
        {
            candidates.Add(path);
        }
    }
}

sealed class RuntimeTestService : RegistryNotificationServiceBase
{
    internal RuntimeTestService()
        : base("AIProjectsRegistryNotificationRuntimeTest")
    {
    }

    protected override void ProcessAllKeys(
        Microsoft.Win32.RegistryKey baseKey,
        string sid)
    {
    }
}

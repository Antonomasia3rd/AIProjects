using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace AIProjects.Dependencies
{
    public sealed class ManagedStartupShortcutSpec
    {
        public string ProductName;
        public string ExecutablePath;
        public string WorkingDirectory;
        public string Arguments;
        public string IdentityPath;
        public string LegacyFileName;
        public string[] LegacyFileNames;
        public Action<string> Log;
    }

    public sealed class ManagedStartupShortcutInfo
    {
        public string TargetPath;
        public string Arguments;
        public string WorkingDirectory;
    }

    // These callbacks run while the profile-scoped Startup mutation lock is
    // held. The snapshot callback must also capture everything needed by the
    // restore callback. Persistence mutations must be failure-atomic (the
    // shared ManagedIniFile.SaveSectionBatch helper has that contract).
    public delegate bool ManagedStartupPersistenceSnapshot(
        out bool configuredStartup,
        out string error);

    public delegate bool ManagedStartupPersistenceMutation(out string error);

    internal delegate bool ManagedStartupShortcutMutation(
        bool desired,
        out string error);

    public sealed class ManagedStartupShortcutMutationLock : IDisposable
    {
        readonly Mutex mutex;
        readonly string identityHash;
        bool ownsMutex;

        internal ManagedStartupShortcutMutationLock(string identity, int waitMilliseconds)
        {
            identityHash = ManagedStartupShortcut.StableIdentityHash(identity);
            try
            {
                string name = ManagedNamedObjects.CurrentUserScopedName(
                    "ManagedStartup",
                    identity);
                mutex = new Mutex(false, name);
                try
                {
                    ownsMutex = mutex.WaitOne(waitMilliseconds);
                }
                catch (AbandonedMutexException)
                {
                    ownsMutex = true;
                }
                if (!ownsMutex)
                    Error = "Timed out waiting for the Startup shortcut mutation lock.";
            }
            catch (Exception ex)
            {
                Error = "Could not create the Startup shortcut mutation lock: " + ex.Message;
            }
        }

        internal ManagedStartupShortcutMutationLock(string error)
        {
            identityHash = "";
            Error = error;
        }

        public bool Acquired { get { return ownsMutex; } }
        public string Error { get; private set; }

        internal bool MatchesIdentity(string identity)
        {
            return ownsMutex && String.Equals(
                identityHash,
                ManagedStartupShortcut.StableIdentityHash(identity),
                StringComparison.Ordinal);
        }

        public void Dispose()
        {
            if (ownsMutex)
            {
                try { mutex.ReleaseMutex(); }
                catch (ApplicationException) { }
                ownsMutex = false;
            }
            if (mutex != null)
                mutex.Dispose();
        }
    }

    public static class ManagedStartupShortcut
    {
        const int DefaultMutationWaitMilliseconds = 10000;

        public static string StartupDirectory
        {
            get { return Environment.GetFolderPath(Environment.SpecialFolder.Startup); }
        }

        public static ManagedStartupShortcutMutationLock AcquireMutationLock(
            ManagedStartupShortcutSpec spec,
            int waitMilliseconds)
        {
            string error;
            if (!ValidateSpec(spec, false, out error))
                return new ManagedStartupShortcutMutationLock(error);
            return new ManagedStartupShortcutMutationLock(
                NormalizeIdentityPath(spec.IdentityPath),
                Math.Max(0, waitMilliseconds));
        }

        public static ManagedStartupShortcutMutationLock AcquireMutationLock(
            ManagedStartupShortcutSpec spec)
        {
            return AcquireMutationLock(spec, DefaultMutationWaitMilliseconds);
        }

        public static string ScopedShortcutPath(ManagedStartupShortcutSpec spec)
        {
            string product = SanitizeFileName(spec == null ? null : spec.ProductName);
            string identity = spec == null ? "" : NormalizeIdentityPath(spec.IdentityPath);
            return Path.Combine(
                StartupDirectory,
                product + "-" + StableIdentityHash(identity) + ".lnk");
        }

        public static string LegacyShortcutPath(ManagedStartupShortcutSpec spec)
        {
            List<string> paths = LegacyShortcutPaths(spec);
            return paths.Count == 0 ? null : paths[0];
        }

        public static bool IsInstalled(ManagedStartupShortcutSpec spec)
        {
            string ignored;
            return IsInstalled(spec, out ignored);
        }

        public static bool IsInstalled(
            ManagedStartupShortcutSpec spec,
            out string error)
        {
            error = null;
            if (!ValidateSpec(spec, false, out error))
                return false;

            ManagedStartupShortcutInfo info;
            string readError;
            string scopedPath = ScopedShortcutPath(spec);
            if (TryReadShortcut(scopedPath, out info, out readError) &&
                MatchesExactLaunch(info, spec))
                return true;
            if (File.Exists(scopedPath) && !String.IsNullOrEmpty(readError))
                error = readError;

            foreach (string legacyPath in LegacyShortcutPaths(spec))
            {
                if (!legacyPath.Equals(scopedPath, StringComparison.OrdinalIgnoreCase) &&
                    TryReadShortcut(legacyPath, out info, out readError) &&
                    MatchesExactLaunch(info, spec))
                    return true;
                if (File.Exists(legacyPath) && !String.IsNullOrEmpty(readError) &&
                    String.IsNullOrEmpty(error))
                    error = readError;
            }
            return false;
        }

        // Migration-only query: detects a shortcut owned by this executable
        // even when an older release embedded now-superseded arguments or a
        // stale working directory. Normal desired-state checks intentionally
        // remain strict through IsInstalled.
        public static bool HasOwnedTarget(
            ManagedStartupShortcutSpec spec,
            out string error)
        {
            error = null;
            if (!ValidateSpec(spec, false, out error))
                return false;

            var paths = new List<string> { ScopedShortcutPath(spec) };
            foreach (string legacyPath in LegacyShortcutPaths(spec))
            {
                if (!paths.Exists(path => path.Equals(legacyPath, StringComparison.OrdinalIgnoreCase)))
                    paths.Add(legacyPath);
            }
            foreach (string path in paths)
            {
                if (!File.Exists(path))
                    continue;
                ManagedStartupShortcutInfo info;
                string readError;
                if (TryReadShortcut(path, out info, out readError))
                {
                    if (MatchesTarget(info, spec))
                        return true;
                }
                else if (String.IsNullOrEmpty(error))
                    error = readError ?? "The Startup shortcut could not be inspected.";
            }
            return false;
        }

        public static bool SetDesiredState(
            ManagedStartupShortcutSpec spec,
            bool desired,
            out bool previousInstalled,
            out string error)
        {
            previousInstalled = false;
            error = null;
            using (ManagedStartupShortcutMutationLock mutationLock = AcquireMutationLock(spec))
            {
                if (!mutationLock.Acquired)
                {
                    error = mutationLock.Error ?? "Could not lock the Startup shortcut.";
                    return false;
                }
                previousInstalled = IsInstalled(spec, out error);
                if (!String.IsNullOrEmpty(error) && File.Exists(ScopedShortcutPath(spec)))
                    return false;
                return SetDesiredStateUnderLock(spec, desired, mutationLock, out error);
            }
        }

        // Commits an INI setting batch together with its Startup shortcut
        // without creating a power-loss state that can remain inconsistent.
        //
        // Enabling installs the shortcut before persisting true. If the
        // process stops between those operations, the old false setting lets
        // the one extra Startup launch remove the shortcut. Disabling uses the
        // reverse order: false is persisted before the shortcut is removed,
        // so an interrupted operation gets one extra launch which completes
        // the removal. This directional protocol preserves eventual
        // consistency without a separate recovery journal or another mutable
        // sidecar file. It does not promise to preserve an in-flight user's
        // intent across power loss.
        public static bool CommitIniCoupledState(
            ManagedStartupShortcutSpec spec,
            bool desired,
            ManagedStartupPersistenceSnapshot capturePrevious,
            ManagedStartupPersistenceMutation persistDesired,
            ManagedStartupPersistenceMutation restorePrevious,
            out string error)
        {
            error = null;
            if (capturePrevious == null || persistDesired == null || restorePrevious == null)
            {
                error = "The Startup/INI transaction callbacks are required.";
                return false;
            }
            if (!ValidateSpec(spec, desired, out error))
                return false;

            using (ManagedStartupShortcutMutationLock mutationLock = AcquireMutationLock(spec))
            {
                if (!mutationLock.Acquired)
                {
                    error = mutationLock.Error ?? "Could not lock the Startup/INI transaction.";
                    return false;
                }

                bool previousConfigured;
                if (!InvokeSnapshot(capturePrevious, out previousConfigured, out error))
                    return false;

                string queryError;
                bool installed = IsInstalled(spec, out queryError);
                if (!String.IsNullOrEmpty(queryError))
                {
                    error = "Could not inspect Startup before the INI transaction: " + queryError;
                    return false;
                }

                // Repair a mismatch left by an older release or an externally
                // edited shortcut before using the configured state as the
                // rollback point.
                if (installed != previousConfigured)
                {
                    string reconcileError;
                    if (!SetDesiredStateUnderLock(
                        spec,
                        previousConfigured,
                        mutationLock,
                        out reconcileError))
                    {
                        error = "Could not reconcile Startup before the INI transaction: " +
                            (reconcileError ?? "unknown error");
                        return false;
                    }
                }

                ManagedStartupShortcutMutation mutateShortcut =
                    delegate(bool state, out string mutationError)
                    {
                        return SetDesiredStateUnderLock(
                            spec,
                            state,
                            mutationLock,
                            out mutationError);
                    };
                return ExecuteCrashConsistentCoupledState(
                    desired,
                    previousConfigured,
                    mutateShortcut,
                    persistDesired,
                    restorePrevious,
                    out error);
            }
        }

        internal static bool ExecuteCrashConsistentCoupledState(
            bool desired,
            bool previousConfigured,
            ManagedStartupShortcutMutation mutateShortcut,
            ManagedStartupPersistenceMutation persistDesired,
            ManagedStartupPersistenceMutation restorePrevious,
            out string error)
        {
            error = null;
            if (mutateShortcut == null || persistDesired == null || restorePrevious == null)
            {
                error = "The Startup/INI transaction callbacks are required.";
                return false;
            }

            string operationError;
            if (desired)
            {
                if (!InvokeShortcutMutation(
                    mutateShortcut,
                    true,
                    "Could not enable Startup",
                    out operationError))
                {
                    string enableMutationRollbackError;
                    InvokeShortcutMutation(
                        mutateShortcut,
                        previousConfigured,
                        "Could not restore the previous Startup shortcut",
                        out enableMutationRollbackError);
                    error = AppendRollbackError(
                        operationError,
                        enableMutationRollbackError);
                    return false;
                }

                if (InvokePersistenceMutation(
                    persistDesired,
                    "Could not persist the desired settings",
                    out operationError))
                    return true;

                string shortcutRollbackError;
                InvokeShortcutMutation(
                    mutateShortcut,
                    previousConfigured,
                    "Could not restore the previous Startup shortcut",
                    out shortcutRollbackError);
                error = AppendRollbackError(operationError, shortcutRollbackError);
                return false;
            }

            if (!InvokePersistenceMutation(
                persistDesired,
                "Could not persist the desired settings",
                out operationError))
            {
                error = operationError;
                return false;
            }

            if (InvokeShortcutMutation(
                mutateShortcut,
                false,
                "Could not disable Startup",
                out operationError))
                return true;

            // Restore with the same safe direction ordering. In particular,
            // never restore INI=true until a launchable shortcut exists.
            string persistenceRollbackError;
            if (previousConfigured)
            {
                string enableShortcutRollbackError;
                if (!InvokeShortcutMutation(
                    mutateShortcut,
                    true,
                    "Could not restore the previous Startup shortcut",
                    out enableShortcutRollbackError))
                {
                    error = AppendRollbackError(
                        operationError,
                        enableShortcutRollbackError);
                    return false;
                }
                InvokePersistenceMutation(
                    restorePrevious,
                    "Could not restore the previous settings",
                    out persistenceRollbackError);
            }
            else
            {
                InvokePersistenceMutation(
                    restorePrevious,
                    "Could not restore the previous settings",
                    out persistenceRollbackError);
                string disableShortcutRollbackError;
                if (!InvokeShortcutMutation(
                    mutateShortcut,
                    false,
                    "Could not restore the previous Startup shortcut",
                    out disableShortcutRollbackError))
                    persistenceRollbackError = AppendRollbackError(
                        persistenceRollbackError,
                        disableShortcutRollbackError);
            }
            error = AppendRollbackError(operationError, persistenceRollbackError);
            return false;
        }

        static bool InvokeSnapshot(
            ManagedStartupPersistenceSnapshot snapshot,
            out bool configuredStartup,
            out string error)
        {
            configuredStartup = false;
            error = null;
            try
            {
                if (snapshot(out configuredStartup, out error))
                    return true;
                error = String.IsNullOrEmpty(error)
                    ? "Could not capture the previous INI settings."
                    : error;
                return false;
            }
            catch (Exception ex)
            {
                error = "Could not capture the previous INI settings: " + ex.Message;
                return false;
            }
        }

        static bool InvokePersistenceMutation(
            ManagedStartupPersistenceMutation mutation,
            string failurePrefix,
            out string error)
        {
            error = null;
            try
            {
                if (mutation(out error))
                    return true;
                error = String.IsNullOrEmpty(error)
                    ? failurePrefix + "."
                    : failurePrefix + ": " + error;
                return false;
            }
            catch (Exception ex)
            {
                error = failurePrefix + ": " + ex.Message;
                return false;
            }
        }

        static bool InvokeShortcutMutation(
            ManagedStartupShortcutMutation mutation,
            bool desired,
            string failurePrefix,
            out string error)
        {
            error = null;
            try
            {
                if (mutation(desired, out error))
                    return true;
                error = String.IsNullOrEmpty(error)
                    ? failurePrefix + "."
                    : failurePrefix + ": " + error;
                return false;
            }
            catch (Exception ex)
            {
                error = failurePrefix + ": " + ex.Message;
                return false;
            }
        }

        static string AppendRollbackError(string operationError, string rollbackError)
        {
            if (String.IsNullOrEmpty(rollbackError))
                return operationError;
            if (String.IsNullOrEmpty(operationError))
                return rollbackError;
            return operationError + " Rollback also failed: " + rollbackError;
        }

        public static bool SetDesiredStateUnderLock(
            ManagedStartupShortcutSpec spec,
            bool desired,
            ManagedStartupShortcutMutationLock mutationLock,
            out string error)
        {
            error = null;
            if (mutationLock == null || !mutationLock.Acquired)
            {
                error = "The Startup shortcut mutation lock is not held.";
                return false;
            }
            if (!ValidateSpec(spec, desired, out error))
                return false;
            if (!mutationLock.MatchesIdentity(NormalizeIdentityPath(spec.IdentityPath)))
            {
                error = "The Startup shortcut mutation lock belongs to another profile.";
                return false;
            }

            try
            {
                Directory.CreateDirectory(StartupDirectory);
                bool changed = desired
                    ? InstallUnderLock(spec, out error)
                    : RemoveUnderLock(spec, out error);
                if (!changed)
                    return false;

                string verifyError;
                bool installed = IsInstalled(spec, out verifyError);
                if (installed != desired)
                {
                    error = String.IsNullOrEmpty(verifyError)
                        ? "The Startup shortcut did not reach the requested state."
                        : verifyError;
                    return false;
                }
                return true;
            }
            catch (Exception ex)
            {
                error = "Startup shortcut operation failed: " + ex.Message;
                SafeLog(spec, error);
                return false;
            }
        }

        static bool InstallUnderLock(
            ManagedStartupShortcutSpec spec,
            out string error)
        {
            error = null;
            string destination = ScopedShortcutPath(spec);
            ManagedStartupShortcutInfo existing;
            string readError;
            if (File.Exists(destination))
            {
                if (!TryReadShortcut(destination, out existing, out readError))
                {
                    error = readError ?? "The existing Startup shortcut could not be inspected.";
                    return false;
                }
                if (!MatchesTarget(existing, spec))
                {
                    error = "Refusing to replace a same-named Startup shortcut that points to another executable.";
                    return false;
                }
                if (MatchesExactLaunch(existing, spec))
                {
                    return RemoveOwnedLegacyShortcuts(spec, out error);
                }
            }

            string startupParent = Path.GetDirectoryName(StartupDirectory);
            if (String.IsNullOrWhiteSpace(startupParent))
            {
                error = "The Startup folder has no usable parent for same-volume staging.";
                return false;
            }
            string stagingDirectory = Path.Combine(
                startupParent,
                ".AIProjects-StartupStaging");
            Directory.CreateDirectory(stagingDirectory);
            string temporary = Path.Combine(
                stagingDirectory,
                ".AIProjects-" + Guid.NewGuid().ToString("N") + ".tmp.lnk");
            try
            {
                WriteShortcut(temporary, spec);
                ManagedStartupShortcutInfo written;
                if (!TryReadShortcut(temporary, out written, out readError) ||
                    !MatchesExactLaunch(written, spec))
                {
                    error = readError ?? "The temporary Startup shortcut failed validation.";
                    return false;
                }

                if (File.Exists(destination))
                    File.Replace(temporary, destination, null);
                else
                    File.Move(temporary, destination);

                if (!RemoveOwnedLegacyShortcuts(spec, out error))
                    return false;
                SafeLog(spec, "Installed Startup shortcut: " + destination);
                return true;
            }
            finally
            {
                try
                {
                    if (File.Exists(temporary))
                        File.Delete(temporary);
                }
                catch { }
            }
        }

        static bool RemoveUnderLock(
            ManagedStartupShortcutSpec spec,
            out string error)
        {
            error = null;
            string scoped = ScopedShortcutPath(spec);
            if (!RemoveOwnedShortcut(scoped, spec, true, out error))
                return false;

            foreach (string legacy in LegacyShortcutPaths(spec))
            {
                if (!legacy.Equals(scoped, StringComparison.OrdinalIgnoreCase) &&
                    !RemoveOwnedShortcut(legacy, spec, false, out error))
                    return false;
            }
            return true;
        }

        static bool RemoveOwnedShortcut(
            string path,
            ManagedStartupShortcutSpec spec,
            bool refuseForeign,
            out string error)
        {
            error = null;
            if (!File.Exists(path))
                return true;

            ManagedStartupShortcutInfo info;
            string readError;
            if (!TryReadShortcut(path, out info, out readError))
            {
                error = readError ?? "The Startup shortcut could not be inspected.";
                return false;
            }
            if (!MatchesTarget(info, spec))
            {
                if (refuseForeign)
                {
                    error = "Refusing to remove a same-named Startup shortcut that points to another executable.";
                    return false;
                }
                return true;
            }

            File.Delete(path);
            SafeLog(spec, "Removed Startup shortcut: " + path);
            return true;
        }

        static bool RemoveOwnedLegacyShortcuts(
            ManagedStartupShortcutSpec spec,
            out string error)
        {
            error = null;
            string scoped = ScopedShortcutPath(spec);
            foreach (string legacy in LegacyShortcutPaths(spec))
            {
                if (legacy.Equals(scoped, StringComparison.OrdinalIgnoreCase))
                    continue;
                if (!RemoveOwnedShortcut(legacy, spec, false, out error))
                    return false;
            }
            return true;
        }

        static List<string> LegacyShortcutPaths(ManagedStartupShortcutSpec spec)
        {
            var fileNames = new List<string>();
            if (spec != null && !String.IsNullOrWhiteSpace(spec.LegacyFileName))
                fileNames.Add(spec.LegacyFileName);
            if (spec != null && spec.LegacyFileNames != null)
            {
                foreach (string fileName in spec.LegacyFileNames)
                {
                    if (!String.IsNullOrWhiteSpace(fileName))
                        fileNames.Add(fileName);
                }
            }
            if (fileNames.Count == 0)
                fileNames.Add(SanitizeFileName(spec == null ? null : spec.ProductName) + ".lnk");

            var paths = new List<string>();
            foreach (string rawName in fileNames)
            {
                string fileName = Path.GetFileName(rawName);
                if (!fileName.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase))
                    fileName += ".lnk";
                string path = Path.Combine(StartupDirectory, fileName);
                if (!paths.Exists(value => value.Equals(path, StringComparison.OrdinalIgnoreCase)))
                    paths.Add(path);
            }
            return paths;
        }

        static void WriteShortcut(
            string path,
            ManagedStartupShortcutSpec spec)
        {
            object shell = null;
            object shortcut = null;
            try
            {
                Type shellType = Type.GetTypeFromProgID("WScript.Shell");
                if (shellType == null)
                    throw new InvalidOperationException("WScript.Shell is unavailable.");
                shell = Activator.CreateInstance(shellType);
                shortcut = shellType.InvokeMember(
                    "CreateShortcut",
                    BindingFlags.InvokeMethod,
                    null,
                    shell,
                    new object[] { path });
                Type shortcutType = shortcut.GetType();
                SetShortcutProperty(shortcutType, shortcut, "TargetPath", spec.ExecutablePath);
                SetShortcutProperty(shortcutType, shortcut, "WorkingDirectory", spec.WorkingDirectory);
                SetShortcutProperty(shortcutType, shortcut, "Arguments", spec.Arguments ?? "");
                shortcutType.InvokeMember(
                    "Save",
                    BindingFlags.InvokeMethod,
                    null,
                    shortcut,
                    new object[] { });
            }
            finally
            {
                ReleaseComObject(shortcut);
                ReleaseComObject(shell);
            }
        }

        static void SetShortcutProperty(
            Type shortcutType,
            object shortcut,
            string property,
            string value)
        {
            shortcutType.InvokeMember(
                property,
                BindingFlags.SetProperty,
                null,
                shortcut,
                new object[] { value ?? "" });
        }

        static bool TryReadShortcut(
            string path,
            out ManagedStartupShortcutInfo info,
            out string error)
        {
            info = null;
            error = null;
            if (!File.Exists(path))
                return false;

            object shell = null;
            object shortcut = null;
            try
            {
                Type shellType = Type.GetTypeFromProgID("WScript.Shell");
                if (shellType == null)
                    throw new InvalidOperationException("WScript.Shell is unavailable.");
                shell = Activator.CreateInstance(shellType);
                shortcut = shellType.InvokeMember(
                    "CreateShortcut",
                    BindingFlags.InvokeMethod,
                    null,
                    shell,
                    new object[] { path });
                Type shortcutType = shortcut.GetType();
                info = new ManagedStartupShortcutInfo
                {
                    TargetPath = ReadShortcutProperty(shortcutType, shortcut, "TargetPath"),
                    Arguments = ReadShortcutProperty(shortcutType, shortcut, "Arguments"),
                    WorkingDirectory = ReadShortcutProperty(shortcutType, shortcut, "WorkingDirectory")
                };
                if (String.IsNullOrWhiteSpace(info.TargetPath))
                {
                    info = null;
                    error = "The Startup shortcut has no target: " + path;
                    return false;
                }
                return true;
            }
            catch (Exception ex)
            {
                error = "Could not inspect Startup shortcut '" + path + "': " + ex.Message;
                return false;
            }
            finally
            {
                ReleaseComObject(shortcut);
                ReleaseComObject(shell);
            }
        }

        static string ReadShortcutProperty(
            Type shortcutType,
            object shortcut,
            string property)
        {
            return Convert.ToString(
                shortcutType.InvokeMember(
                    property,
                    BindingFlags.GetProperty,
                    null,
                    shortcut,
                    new object[] { }),
                CultureInfo.InvariantCulture) ?? "";
        }

        static bool MatchesTarget(
            ManagedStartupShortcutInfo info,
            ManagedStartupShortcutSpec spec)
        {
            return info != null && PathsEqual(info.TargetPath, spec.ExecutablePath);
        }

        static bool MatchesExactLaunch(
            ManagedStartupShortcutInfo info,
            ManagedStartupShortcutSpec spec)
        {
            return MatchesTarget(info, spec) &&
                String.Equals(info.Arguments ?? "", spec.Arguments ?? "", StringComparison.Ordinal) &&
                PathsEqual(info.WorkingDirectory, spec.WorkingDirectory);
        }

        static bool PathsEqual(string left, string right)
        {
            if (String.IsNullOrWhiteSpace(left) || String.IsNullOrWhiteSpace(right))
                return false;
            try
            {
                return String.Equals(
                    Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                    Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                    StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return false;
            }
        }

        static bool ValidateSpec(
            ManagedStartupShortcutSpec spec,
            bool requireExistingTarget,
            out string error)
        {
            error = null;
            if (spec == null)
            {
                error = "The Startup shortcut specification is missing.";
                return false;
            }
            if (String.IsNullOrWhiteSpace(spec.ProductName) ||
                String.IsNullOrWhiteSpace(spec.ExecutablePath) ||
                String.IsNullOrWhiteSpace(spec.WorkingDirectory) ||
                String.IsNullOrWhiteSpace(spec.IdentityPath))
            {
                error = "The Startup shortcut specification is incomplete.";
                return false;
            }
            if (!Path.IsPathRooted(spec.ExecutablePath) ||
                !Path.IsPathRooted(spec.WorkingDirectory) ||
                !Path.IsPathRooted(spec.IdentityPath))
            {
                error = "Startup shortcut paths must be absolute.";
                return false;
            }
            if ((spec.Arguments ?? "").IndexOfAny(new[] { '\r', '\n' }) >= 0)
            {
                error = "Startup shortcut arguments cannot contain line breaks.";
                return false;
            }
            if (requireExistingTarget &&
                (!File.Exists(spec.ExecutablePath) || !Directory.Exists(spec.WorkingDirectory)))
            {
                error = "The Startup shortcut target or working directory does not exist.";
                return false;
            }
            if (String.IsNullOrWhiteSpace(StartupDirectory))
            {
                error = "The current user's Startup folder could not be resolved.";
                return false;
            }
            return true;
        }

        static string NormalizeIdentityPath(string value)
        {
            try
            {
                return Path.GetFullPath(value ?? "").TrimEnd(
                    Path.DirectorySeparatorChar,
                    Path.AltDirectorySeparatorChar).ToUpperInvariant();
            }
            catch
            {
                return (value ?? "").ToUpperInvariant();
            }
        }

        internal static string StableIdentityHash(string value)
        {
            using (SHA256 sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(Encoding.UTF8.GetBytes(
                    (value ?? "").ToUpperInvariant()));
                var text = new StringBuilder(16);
                for (int i = 0; i < 8; ++i)
                    text.Append(hash[i].ToString("X2", CultureInfo.InvariantCulture));
                return text.ToString();
            }
        }

        static string SanitizeFileName(string value)
        {
            string name = String.IsNullOrWhiteSpace(value) ? "AIProjects" : value.Trim();
            foreach (char invalid in Path.GetInvalidFileNameChars())
                name = name.Replace(invalid, '_');
            name = name.TrimEnd(' ', '.');
            if (name.Length == 0)
                name = "AIProjects";
            if (name.Length > 80)
                name = name.Substring(0, 80).TrimEnd(' ', '.');
            return name;
        }

        static void ReleaseComObject(object value)
        {
            if (value == null || !Marshal.IsComObject(value))
                return;
            try { Marshal.FinalReleaseComObject(value); }
            catch { }
        }

        static void SafeLog(ManagedStartupShortcutSpec spec, string message)
        {
            if (spec == null || spec.Log == null)
                return;
            try { spec.Log(message); }
            catch { }
        }
    }
}

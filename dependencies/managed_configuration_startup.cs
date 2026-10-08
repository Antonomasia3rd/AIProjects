using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace AIProjects.Dependencies
{
    public interface IManagedConfigurationStartupPlatform
    {
        bool IsInstalled(ManagedStartupShortcutSpec spec, out string error);
        bool Commit(ManagedStartupShortcutSpec spec, bool desired, Func<IDisposable> acquirePersistenceScope,
            ManagedStartupPersistenceSnapshot capturePrevious, ManagedStartupPersistenceMutation persistDesired,
            ManagedStartupPersistenceMutation restorePrevious, out string error);
    }

    public sealed class ManagedConfigurationStartupState
    {
        public bool Configured;
        public bool Installed;
        public string Error;
    }

    // INI/CLI/tray share this adapter. The platform seam keeps ordinary fixtures
    // away from the real Startup folder. Production always uses the existing
    // per-user shortcut helper, with no job arguments or registry Run keys.
    public sealed class ManagedConfigurationStartup
    {
        public const string Key = "RunAtStartup";
        internal static Func<IManagedConfigurationStartupPlatform> PlatformFactory =
            delegate { return new WindowsPlatform(); };
        readonly ManagedIniFileSpec ini;
        readonly Func<Dictionary<string, string>> read;
        readonly Action<IDictionary<string, string>> saveLocal;
        readonly Action<IDictionary<string, string>> validate;
        public ManagedStartupShortcutSpec Shortcut { get; private set; }
        public IManagedConfigurationStartupPlatform Platform { get; set; }

        public ManagedConfigurationStartup(ManagedIniFileSpec ini, ManagedStartupShortcutSpec shortcut,
            Func<Dictionary<string, string>> read, Action<IDictionary<string, string>> saveLocal,
            Action<IDictionary<string, string>> validate)
        {
            if (ini == null || shortcut == null || read == null || saveLocal == null || validate == null)
                throw new ArgumentException("The Startup profile, reader, validator and writer are required.");
            this.ini = ini; Shortcut = shortcut; this.read = read; this.saveLocal = saveLocal; this.validate = validate;
            Platform = PlatformFactory();
        }

        public static string NormalizeValue(string value)
        {
            if (value == null || value.IndexOfAny(new[] { '\0', '\r', '\n' }) >= 0)
                throw new ArgumentException("RunAtStartup must be a single-line boolean value.");
            switch ((value ?? "").Trim().ToLowerInvariant())
            {
                case "1": case "true": case "yes": case "on": return "1";
                case "0": case "false": case "no": case "off": return "0";
                default: throw new ArgumentException("RunAtStartup must be true/false or 1/0.");
            }
        }

        static bool Desired(IDictionary<string, string> values)
        {
            string value;
            return values.TryGetValue(Key, out value) && NormalizeValue(value) == "1";
        }

        public static string QuoteArgument(string value)
        {
            if (value == null || value.IndexOfAny(new[] { '\0', '\r', '\n' }) >= 0)
                throw new ArgumentException("A single-line Windows argument is required.", "value");
            var result = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { ++slashes; continue; }
                result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes);
                result.Append(c); slashes = 0;
            }
            result.Append('\\', slashes * 2);
            return result.Append('"').ToString();
        }

        public static ManagedStartupShortcutSpec BuildShortcut(string product, string executable, string profile)
        {
            string exe = Path.GetFullPath(executable);
            string iniPath = Path.GetFullPath(profile);
            return new ManagedStartupShortcutSpec
            {
                ProductName = product, ExecutablePath = exe,
                WorkingDirectory = Path.GetDirectoryName(exe), IdentityPath = iniPath,
                Arguments = "--tray --ini " + QuoteArgument(iniPath)
            };
        }

        public void Save(IDictionary<string, string> changes)
        {
            if (changes == null) throw new ArgumentNullException("changes");
            var batch = new Dictionary<string, string>(changes, StringComparer.OrdinalIgnoreCase);
            string startup;
            if (!batch.TryGetValue(Key, out startup))
            {
                // Keep prospective validation and publication in the same INI
                // scope even when this batch does not change Startup.
                using (ManagedIniFile.BeginTransaction(ini))
                {
                    validate(batch);
                    saveLocal(batch);
                }
                return;
            }
            batch[Key] = NormalizeValue(startup);
            bool desired = batch[Key] == "1";
            ManagedIniFile.Transaction transaction = null;
            string error;
            bool committed = Platform.Commit(Shortcut, desired,
                delegate
                {
                    transaction = ManagedIniFile.BeginTransaction(ini);
                    try { validate(batch); return transaction; }
                    catch { transaction.Dispose(); transaction = null; throw; }
                },
                delegate(out bool previous, out string snapshotError)
                {
                    snapshotError = null; previous = false;
                    try
                    {
                        var current = read();
                        try { previous = Desired(current); }
                        catch (ArgumentException)
                        {
                            // A malformed saved preference must remain repairable.
                            // Its exact bytes are in the transaction snapshot; use
                            // the observed shortcut as the rollback launch state.
                            previous = Platform.IsInstalled(Shortcut, out snapshotError);
                        }
                        return String.IsNullOrEmpty(snapshotError);
                    }
                    catch (Exception ex) { snapshotError = ex.Message; return false; }
                },
                delegate(out string persistenceError)
                {
                    try { saveLocal(batch); persistenceError = null; return true; }
                    catch (Exception ex) { persistenceError = ex.Message; return false; }
                },
                delegate(out string rollbackError) { return transaction.TryRestoreOriginal(out rollbackError); },
                out error);
            if (!committed) throw new IOException("Could not save Startup configuration: " + error);
        }

        public ManagedConfigurationStartupState ReadState()
        {
            var state = new ManagedConfigurationStartupState();
            try
            {
                state.Configured = Desired(read());
                string error;
                state.Installed = Platform.IsInstalled(Shortcut, out error);
                state.Error = error;
            }
            catch (Exception ex) { state.Error = ex.Message; }
            return state;
        }

        public void Reconcile()
        {
            var state = ReadState();
            if (!String.IsNullOrEmpty(state.Error)) throw new IOException(state.Error);
            // False from IsInstalled may mean an owned shortcut has obsolete
            // arguments, not that no launch path exists. Disabling must still
            // remove that owned entry; enabling an exact match is a true no-op.
            if (state.Configured && state.Installed) return;
            bool desired = state.Configured;
            string error;
            // Recheck after acquiring Startup -> INI locks; never apply a stale
            // preference that changed while this operation waited for its lock.
            bool reconciled = Platform.Commit(Shortcut, desired,
                delegate { return ManagedIniFile.BeginTransaction(ini); },
                delegate(out bool previous, out string snapshotError)
                {
                    previous = false; snapshotError = null;
                    try
                    {
                        previous = Desired(read());
                        if (previous == desired) return true;
                        snapshotError = "Startup preference changed; reload the profile and retry.";
                        return false;
                    }
                    catch (Exception ex) { snapshotError = ex.Message; return false; }
                },
                delegate(out string ignored) { ignored = null; return true; },
                delegate(out string ignored) { ignored = null; return true; },
                out error);
            if (!reconciled) throw new IOException("Could not apply the saved Startup preference: " + error);
        }

        sealed class WindowsPlatform : IManagedConfigurationStartupPlatform
        {
            public bool IsInstalled(ManagedStartupShortcutSpec spec, out string error)
            { return ManagedStartupShortcut.IsInstalled(spec, out error); }

            public bool Commit(ManagedStartupShortcutSpec spec, bool desired, Func<IDisposable> scope,
                ManagedStartupPersistenceSnapshot capture, ManagedStartupPersistenceMutation persist,
                ManagedStartupPersistenceMutation restore, out string error)
            { return ManagedStartupShortcut.CommitIniCoupledState(spec, desired, scope, capture, persist, restore, out error); }
        }
    }
}

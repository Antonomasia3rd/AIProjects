using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows.Forms;
using AIProjects.Dependencies;

public interface IRegistryNotificationUserRuntime : IDisposable
{
    void Start(bool logging, Action<string> report);
    void Stop();
}

// The two notification products supply only their policy and identity. The
// default host is per-user; the old privileged service is an explicit mode.
public static class ManagedRegistryNotificationApp
{
    internal static Func<Func<RegistryNotificationServiceBase>, IRegistryNotificationUserRuntime> RuntimeFactory =
        factory => new UserRuntime(factory);
    internal static Func<ManagedConfigurationTraySpec, int> TrayRunner = ManagedConfigurationTray.Run;

    sealed class UserRuntime : IRegistryNotificationUserRuntime
    {
        readonly Func<RegistryNotificationServiceBase> factory;
        RegistryNotificationServiceBase service;
        public UserRuntime(Func<RegistryNotificationServiceBase> factory) { this.factory = factory; }
        public void Start(bool logging, Action<string> report)
        {
            if (service == null) service = factory();
            service.StartForCurrentUser(logging, report);
        }
        public void Stop() { if (service != null) service.StopCurrentUser(); }
        public void Dispose()
        {
            Stop();
            if (service != null) { service.Dispose(); service = null; }
        }
    }

    sealed class Session : IDisposable
    {
        readonly IRegistryNotificationUserRuntime runtime;
        readonly Func<Dictionary<string, string>> read;
        bool active;
        bool logging;
        volatile string status = "Disabled";
        volatile bool faulted;
        public Session(IRegistryNotificationUserRuntime runtime, Func<Dictionary<string, string>> read)
        { this.runtime = runtime; this.read = read; }
        public string Status { get { return status; } }
        public void Reload()
        {
            try
            {
                var values = Resolve(read(), new Dictionary<string, string>());
                bool nextEnabled = values["Enabled"] == "1";
                bool nextLogging = values["LoggingEnabled"] == "1";
                if (active && nextEnabled && nextLogging == logging && !faulted) return;
                // Never start a replacement watcher until the old one stopped.
                runtime.Stop();
                active = false;
                if (nextEnabled)
                {
                    status = "Watching current user";
                    faulted = false;
                    runtime.Start(nextLogging, delegate(string message)
                    {
                        if (message.IndexOf("error", StringComparison.OrdinalIgnoreCase) >= 0 ||
                            message.IndexOf("failed", StringComparison.OrdinalIgnoreCase) >= 0 ||
                            message.IndexOf("could not", StringComparison.OrdinalIgnoreCase) >= 0 ||
                            message.IndexOf("warning", StringComparison.OrdinalIgnoreCase) >= 0)
                        { status = message; faulted = true; }
                        if (nextLogging) Console.Error.WriteLine(message);
                    });
                    active = true;
                    logging = nextLogging;
                }
                else { status = "Disabled"; faulted = false; }
            }
            catch (Exception ex) { status = "Error: " + ex.Message; faulted = true; throw; }
        }
        public void Dispose() { runtime.Dispose(); }
    }

    static string Canonical(string key)
    {
        foreach (string name in new[] { "Enabled", "LoggingEnabled", "RunAtStartup" })
            if (String.Equals(key, name, StringComparison.OrdinalIgnoreCase)) return name;
        return null;
    }
    static string Normalize(string key, string value)
    {
        if (Canonical(key) == null) throw new ArgumentException("Unknown setting: " + key);
        try { return ManagedConfigurationStartup.NormalizeValue(value); }
        catch (ArgumentException ex) { throw new ArgumentException(key + " must be true/false or 1/0.", ex); }
    }
    static Dictionary<string, string> Defaults()
    {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            { "Enabled", "1" }, { "LoggingEnabled", "1" }, { "RunAtStartup", "0" }
        };
    }
    static Dictionary<string, string> Resolve(IDictionary<string, string> stored, IDictionary<string, string> changes)
    { return ManagedProfile.ResolveSettings(Defaults(), stored, changes, Canonical, Normalize); }
    internal static ManagedIniFileSpec IniSpec(string ini)
    {
        return new ManagedIniFileSpec { FilePath = ini, SectionName = "Settings",
            DefaultContents = "[Settings]\r\nEnabled=1\r\nLoggingEnabled=1\r\nRunAtStartup=0\r\n" };
    }
    static void Save(string ini, IDictionary<string, string> changes)
    {
        string error;
        if (!ManagedIniFile.SaveSectionBatch(IniSpec(ini), changes,
            values => Resolve(values, new Dictionary<string, string>()), out error))
            throw new IOException(error);
    }

    internal static ManagedConfigurationTraySpec BuildSpec(string product, string ini,
        Func<RegistryNotificationServiceBase> factory)
    {
        var spec = new ManagedConfigurationTraySpec {
            ProductName = product, Version = "1.0.0.0", IniPath = ini,
            ReadSettings = () => ManagedConfigurationTray.ReadForEditing(Defaults(), IniSpec(ini))
        };
        spec.Startup = new ManagedConfigurationStartup(IniSpec(ini),
            ManagedConfigurationStartup.BuildShortcut(product, Assembly.GetExecutingAssembly().Location, ini),
            spec.ReadSettings, changes => Save(ini, changes), changes => Resolve(spec.ReadSettings(), changes));
        Session session = null;
        spec.OpenResidentSession = delegate
        {
            session = new Session(RuntimeFactory(factory), spec.ReadSettings);
            // A bad profile must still leave the settings editor available.
            try { session.Reload(); }
            catch (Exception ex) { Console.Error.WriteLine(ex.Message); }
            return session;
        };
        spec.ReloadResidentSession = delegate { if (session != null) session.Reload(); };
        spec.ResidentStatus = delegate { return session == null ? "Not running" : session.Status; };
        spec.SaveSettings = delegate(IDictionary<string, string> changes)
        {
            spec.Startup.Save(changes);
            if (session != null)
            {
                try { session.Reload(); }
                catch (Exception ex) { throw new IOException("Settings saved, but the policy could not be reloaded: " + ex.Message, ex); }
            }
        };
        return spec;
    }

    static bool Is(string value, string option) { return String.Equals(value, option, StringComparison.OrdinalIgnoreCase); }
    static string Next(string[] args, ref int index)
    { if (++index == args.Length) throw new ArgumentException("An option value is missing."); return args[index]; }

    public static int Run(string[] args, string serviceName, string displayName, string description,
        Func<RegistryNotificationServiceBase> createService)
    {
        args = args ?? new string[0];
        string product = serviceName.EndsWith("Service", StringComparison.Ordinal)
            ? serviceName.Substring(0, serviceName.Length - "Service".Length) : serviceName;
        if (args.Any(arg => Is(arg, "--help") || Is(arg, "-h") || Is(arg, "/?")))
        {
            Console.WriteLine(product + " - notification policy for the current user");
            Console.WriteLine("--tray [--ini FILE]     start the current-user policy tray (also the default)");
            Console.WriteLine("--set Settings.KEY=VALUE --configure-only | --show-config");
            Console.WriteLine("--enable | --disable | --startup | --no-startup");
            Console.WriteLine("Settings: Enabled, LoggingEnabled, RunAtStartup. Startup opens this profile's tray.");
            Console.WriteLine("--install | --uninstall: explicit legacy all-user service management; installation is manual-start.");
            return 0;
        }
        if (args.Any(arg => Is(arg, "--version") || Is(arg, "-v"))) { Console.WriteLine(product + " 1.0.0.0"); return 0; }
        if ((args.Length == 0 && !Environment.UserInteractive) ||
            (args.Length == 1 && new[] { "--install", "/install", "--uninstall", "/uninstall" }.Any(option => Is(args[0], option))))
            return ManagedPrivilegedServiceHost.Run(args, serviceName, displayName, description, createService);
        try
        {
            string suppliedIni = null;
            bool configure = false, show = false, startup = false, tray = false;
            var changes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            for (int index = 0; index < args.Length; ++index)
            {
                string arg = args[index];
                if (Is(arg, "--ini")) suppliedIni = Next(args, ref index);
                else if (Is(arg, "--set")) ManagedProfile.SetAssignment(changes, Next(args, ref index), "Settings", Canonical, Normalize);
                else if (Is(arg, "--configure-only")) configure = true;
                else if (Is(arg, "--show-config") || Is(arg, "--print-config")) show = true;
                else if (Is(arg, "--tray")) tray = true;
                else if (Is(arg, "--enable") || Is(arg, "--disable")) changes["Enabled"] = Is(arg, "--enable") ? "1" : "0";
                else if (Is(arg, "--startup") || Is(arg, "--no-startup"))
                {
                    if (startup) throw new ArgumentException("Choose one Startup management option.");
                    startup = true; changes["RunAtStartup"] = Is(arg, "--startup") ? "1" : "0";
                }
                else throw new ArgumentException("Unknown option: " + arg);
            }
            if (startup && (configure || show)) throw new ArgumentException("Offline configuration and inspection never change Startup.");
            if (!startup && changes.ContainsKey("RunAtStartup")) throw new ArgumentException("Use --startup or --no-startup to change Startup integration.");
            if (tray && (configure || show)) throw new ArgumentException("Choose tray or offline configuration/inspection.");
            if (show && changes.Count != 0 && !configure) throw new ArgumentException("Saving while showing requires --configure-only.");
            string ini = ManagedProfile.ResolveIniPath(suppliedIni, AppDomain.CurrentDomain.BaseDirectory, product + ".ini");
            var spec = BuildSpec(product, ini, createService);
            if (startup) { spec.SaveSettings(changes); return tray ? TrayRunner(spec) : 0; }
            if (configure)
            {
                spec.SaveSettings(changes);
                Console.WriteLine("Saved configuration: " + ini);
            }
            if (show)
                foreach (var setting in Resolve(spec.ReadSettings(), new Dictionary<string, string>()))
                    Console.WriteLine(setting.Key + "=" + setting.Value);
            if (configure || show) return 0;
            if (changes.Count != 0) spec.SaveSettings(changes);
            return TrayRunner(spec);
        }
        catch (Exception ex) { Console.Error.WriteLine(product + ": " + ex.Message); return 1; }
    }
}

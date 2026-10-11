using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using Microsoft.Win32;
using AIProjects.Dependencies;

internal static class SecureDesktopLauncherControl
{
    internal static Func<ManagedConfigurationTraySpec, int> TrayRunner = ManagedConfigurationTray.Run;
    internal static Action<ManagedConfigurationTraySpec> EditorRunner = ManagedConfigurationTray.ShowEditor;
    internal static Func<string, string, string, int> NativeCommand = ExecuteNative;
    internal static Func<string> InstalledImagePath = ReadInstalledImagePath;
    internal static Func<string, string, string, int> ElevatedCommand = LaunchElevated;
    static Dictionary<string, string> Empty() { return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase); }
    static bool Is(string value, string other) { return String.Equals(value, other, StringComparison.OrdinalIgnoreCase); }
    static string ResolvePath(string ini, string value)
    { return ManagedProfile.ResolvePath(Path.GetDirectoryName(ini), Environment.ExpandEnvironmentVariables(value)); }

    [STAThread]
    static int Main(string[] args)
    {
        if (Console.IsOutputRedirected) Console.OutputEncoding = new UTF8Encoding(false);
        return Run(args);
    }
    internal static int Run(string[] args)
    {
        args = args ?? new string[0];
        if (args.Any(arg => Is(arg, "--help") || Is(arg, "-h") || Is(arg, "/?"))) { Help(); return 0; }
        if (args.Any(arg => Is(arg, "--version"))) { Console.WriteLine("SecureDesktopLauncher Control 1.0.0.0"); return 0; }
        try
        {
            string path = null, passwordPath = null, nativeAction = null, addProgram = null;
            bool password = false, configure = false, show = false, tray = false, startup = false;
            var changes = Empty();
            for (int index = 0; index < args.Length; ++index)
            {
                string arg = args[index];
                if (Is(arg, "--ini")) path = Next(args, ref index, arg);
                else if (Is(arg, "--password-profile")) password = true;
                else if (Is(arg, "--password-ini")) { passwordPath = Next(args, ref index, arg); password = true; }
                else if (Is(arg, "--set"))
                {
                    string assignment = Next(args, ref index, arg);
                    int equals = assignment.IndexOf('=');
                    if (equals <= 0) throw new ArgumentException("--set requires Section.Key=Value.");
                    string key = assignment.Substring(0, equals).Trim();
                    if (Is(key, "Control.RunAtStartup")) key = "RunAtStartup";
                    changes[key] = assignment.Substring(equals + 1);
                }
                else if (Is(arg, "--configure-only")) configure = true;
                else if (Is(arg, "--show-config") || Is(arg, "--print-config")) show = true;
                else if (Is(arg, "--tray")) tray = true;
                else if (Is(arg, "--startup") || Is(arg, "--no-startup"))
                {
                    if (startup) throw new ArgumentException("Choose one Startup management option.");
                    startup = true; changes["RunAtStartup"] = Is(arg, "--startup") ? "1" : "0";
                }
                else if (Is(arg, "--add-program"))
                {
                    if (addProgram != null) throw new ArgumentException("Add one program per command.");
                    addProgram = Next(args, ref index, arg);
                }
                else if (new[] { "--install-service", "--uninstall-service", "--start-service", "--stop-service", "--set-password" }.Any(option => Is(arg, option)))
                {
                    if (nativeAction != null) throw new ArgumentException("Choose one explicit native action.");
                    nativeAction = arg.Substring(2).ToLowerInvariant();
                }
                else throw new ArgumentException("Unknown option: " + arg);
            }
            if (nativeAction != null && (configure || show || tray || startup || addProgram != null || changes.Count != 0))
                throw new ArgumentException("Native actions are separate from configuration, tray and Startup changes.");
            if (nativeAction != null && nativeAction != "set-password" && password)
                throw new ArgumentException("Password profile selection applies only to password configuration and the set-password action.");
            if (password && (startup || tray || addProgram != null)) throw new ArgumentException("Startup and tray belong to the service configuration profile; choose --password-profile only for editing.");
            if (startup && (configure || show)) throw new ArgumentException("Offline configuration and inspection do not change Startup integration.");
            if (changes.ContainsKey("RunAtStartup") && !startup) throw new ArgumentException("Use --startup or --no-startup to change Startup integration.");
            if (tray && (configure || show || addProgram != null || (changes.Count != 0 && !startup)))
                throw new ArgumentException("Tray mode accepts --ini and optional explicit Startup management.");
            if (changes.Count != 0 && !configure && !startup) throw new ArgumentException("--set requires --configure-only or explicit Startup management.");
            if (addProgram != null && !configure) throw new ArgumentException("Use --configure-only when adding a program from the CLI.");
            string serviceIni = ManagedProfile.ResolveIniPath(path, AppDomain.CurrentDomain.BaseDirectory, "SecureDesktopLauncher.ini");
            string selected = passwordPath == null ? serviceIni : ManagedProfile.ResolveIniPath(passwordPath, AppDomain.CurrentDomain.BaseDirectory, "SecureDesktopPasswordLauncher.ini");
            if (password && passwordPath == null) selected = PasswordIni(serviceIni);
            var profile = new SecureDesktopControlProfile(selected, password);
            if (nativeAction != null) return RunNative(serviceIni, nativeAction, nativeAction == "set-password" && password ? selected : null);
            var spec = BuildSpec(profile);
            if (startup)
            {
                spec.SaveSettings(changes);
                Console.WriteLine("Saved Startup configuration: " + serviceIni);
                return tray ? TrayRunner(spec) : 0;
            }
            if (configure)
            {
                if (addProgram == null) spec.SaveSettings(changes);
                else profile.AddProgram(addProgram, changes);
                Console.WriteLine("Saved configuration: " + selected);
            }
            if (show)
                foreach (var field in profile.Read().OrderBy(field => field.Key))
                    Console.WriteLine(field.Key + "=" + field.Value);
            if (configure || show) return 0;
            if (password) { EditorRunner(spec); return 0; }
            return TrayRunner(spec);
        }
        catch (Exception ex) { Console.Error.WriteLine("SecureDesktopLauncher Control: " + ex.Message); return 1; }
    }
    static string Next(string[] args, ref int index, string option)
    { if (++index == args.Length) throw new ArgumentException(option + " requires a value."); return args[index]; }
    static string PasswordIni(string serviceIni)
    {
        var values = new SecureDesktopControlProfile(serviceIni, false).Read();
        return ResolvePath(serviceIni, values["Control.PasswordIniPath"]);
    }
    internal static ManagedConfigurationTraySpec BuildSpec(SecureDesktopControlProfile profile)
    {
        var spec = new ManagedConfigurationTraySpec {
            ProductName = profile.Password ? "SecureDesktop password launcher" : "SecureDesktopLauncher Control",
            Version = "1.0.0.0", IniPath = profile.IniPath,
            ReadSettings = profile.Read, ReadOnlySetting = SecureDesktopControlProfile.ReadOnly
        };
        if (profile.Password) spec.SaveSettings = profile.Save;
        else
        {
            var preference = new ManagedIniFileSpec { FilePath = profile.IniPath, SectionName = "Control", DefaultContents = "" };
            spec.Startup = new ManagedConfigurationStartup(preference,
                ManagedConfigurationStartup.BuildShortcut("SecureDesktopLauncher Control", Assembly.GetExecutingAssembly().Location, profile.IniPath),
                () => ManagedIniFile.LoadSection(preference, false), profile.Save, profile.ValidateChanges);
            spec.SaveSettings = spec.Startup.Save;
            spec.Commands = new List<ManagedConfigurationCommand> {
                new ManagedConfigurationCommand { Text = "Password launcher settings...", Execute = () => EditorRunner(BuildSpec(new SecureDesktopControlProfile(PasswordIni(profile.IniPath), true))) },
                new ManagedConfigurationCommand { Text = "Add program...", Execute = () => AddProgram(profile, spec) },
                Command("Install/update manual service...", profile, "install-service"),
                Command("Start service (runs configured programs)...", profile, "start-service"),
                Command("Stop service...", profile, "stop-service"),
                Command("Uninstall service...", profile, "uninstall-service"),
                Command("Set password...", profile, "set-password")
            };
        }
        return spec;
    }
    static ManagedConfigurationCommand Command(string label, SecureDesktopControlProfile profile, string action)
    {
        return new ManagedConfigurationCommand { Text = label, Execute = delegate
        {
            int code = RunNative(profile.IniPath, action, null);
            if (code != 0) throw new IOException("The native action failed with exit code " + code + ". Check its native diagnostics; the configuration was not automatically changed.");
        } };
    }
    static void AddProgram(SecureDesktopControlProfile profile, ManagedConfigurationTraySpec spec)
    {
        string name;
        if (!ManagedTrayBaseline.TryPromptText(spec.ProductName, "Program name", "New program", out name)) return;
        SecureDesktopControlProfile.ValidateProgramName(name);
        profile.AddProgram(name, Empty());
        EditorRunner(spec);
    }
    internal static int RunNative(string ini, string action, string passwordOverride)
    {
        var values = new SecureDesktopControlProfile(ini, false).Read();
        bool password = action == "set-password";
        string executable = ResolvePath(ini, values[password ? "Control.PasswordExecutable" : "Control.ServiceExecutable"]);
        string nativeIni = password ? (passwordOverride == null ? ResolvePath(ini, values["Control.PasswordIniPath"]) : Path.GetFullPath(passwordOverride)) : Path.GetFullPath(ini);
        if (!Is(Path.ChangeExtension(executable, ".ini"), nativeIni))
            throw new ArgumentException("The native helper reads only its sibling INI. Choose the profile beside " + executable + "; alternate profiles are editable drafts and are not deployed automatically.");
        if (Is(executable, Assembly.GetExecutingAssembly().Location)) throw new ArgumentException("Choose the native helper, not this configuration companion.");
        return NativeCommand(action, executable, nativeIni);
    }
    internal static bool OwnsInstalledImage(string image, string executable)
    {
        if (image == null) return false;
        string expanded = Environment.ExpandEnvironmentVariables(image).Trim();
        return Is(expanded, ManagedConfigurationStartup.QuoteArgument(executable)) ||
            (executable.IndexOfAny(new[] { ' ', '\t', '"' }) < 0 && Is(expanded, executable));
    }
    static string ReadInstalledImagePath()
    {
        using (var key = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\SecureDesktopLauncher"))
            return key == null ? null : key.GetValue("ImagePath") as string;
    }
    internal static int ExecuteNative(string action, string executable, string ini)
    {
        if (!new[] { "install-service", "uninstall-service", "start-service", "stop-service", "set-password" }.Contains(action))
            throw new ArgumentException("Unknown explicit native action: " + action);
        string tool = executable;
        string arguments;
        if (action == "start-service" || action == "stop-service" || action == "uninstall-service")
        {
            // Verify which binary the fixed service name actually targets. The
            // controller must not start an unrelated installation or a draft.
            string image = InstalledImagePath();
            if (image == null && action == "uninstall-service") return 0;
            if (!OwnsInstalledImage(image, executable))
                throw new IOException("The installed service does not target the selected native executable. Select its actual sibling profile or explicitly install/update it first.");
            if (action == "uninstall-service") arguments = "uninstall";
            else
            {
                tool = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "sc.exe");
                arguments = (action == "start-service" ? "start" : "stop") + " SecureDesktopLauncher";
            }
        }
        else arguments = action == "install-service" ? "install" : action == "uninstall-service" ? "uninstall" : "set-password";
        if (action != "stop-service" && !File.Exists(executable)) throw new FileNotFoundException("The explicitly selected native helper must already exist.", executable);
        if ((action == "install-service" || action == "start-service") && !File.Exists(ini))
            throw new FileNotFoundException("Save the native helper's sibling configuration before installing or starting its service.", ini);
        return ElevatedCommand(tool, arguments, action == "set-password" ? "password" : "service");
    }
    static int LaunchElevated(string tool, string arguments, string mode)
    {
        var info = new ProcessStartInfo(tool, arguments) { UseShellExecute = true, Verb = "runas",
            WorkingDirectory = Path.GetDirectoryName(tool), WindowStyle = mode == "password" ? ProcessWindowStyle.Normal : ProcessWindowStyle.Hidden };
        using (var process = Process.Start(info))
        { if (process == null) throw new IOException("The native action did not start."); process.WaitForExit(); return process.ExitCode; }
    }
    static void Help()
    {
        Console.WriteLine("SecureDesktopLauncher Control: default / --tray opens configuration only.");
        Console.WriteLine("--ini FILE --show-config | --configure-only --set Section.Key=Value");
        Console.WriteLine("--password-profile [--password-ini FILE] selects the password-launcher editor.");
        Console.WriteLine("--add-program NAME --configure-only creates a disabled Program:NAME entry.");
        Console.WriteLine("--startup | --no-startup controls this profile's tray in the per-user Startup folder.");
        Console.WriteLine("Explicit actions: --install-service --start-service --stop-service --uninstall-service --set-password.");
        Console.WriteLine("Native actions use their executable's sibling INI and Windows elevation; drafts are never deployed automatically.");
        Console.WriteLine("Password hashes, salt, KDF settings and attempt counters use the native set-password lifecycle and are read-only here.");
    }
}

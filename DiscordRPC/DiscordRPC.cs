using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Net;
using System.Net.WebSockets;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

internal static class Program
{
    private static readonly TimeSpan CensorRegexTimeout = TimeSpan.FromMilliseconds(100);
    private static volatile bool stopping;
    private static CpuSampler cpuSampler = new CpuSampler();
    private static Mutex singleInstanceMutex;

    private static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        Console.CancelKeyPress += delegate(object sender, ConsoleCancelEventArgs e)
        {
            e.Cancel = true;
            stopping = true;
        };

        bool dryRun = false;
        bool verbose = false;
        bool once = false;
        bool noTray = false;
        bool forceTray = false;
        bool forceShowConsole = false;
        bool forceHideConsole = false;
        bool dryRunFull = false;
        string configPath = AppPaths.DefaultIniPath;
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].Equals("--dry-run", StringComparison.OrdinalIgnoreCase))
            {
                dryRun = true;
            }
            else if (args[i].Equals("--dry-run-full", StringComparison.OrdinalIgnoreCase) ||
                     args[i].Equals("--dry-run-unredacted", StringComparison.OrdinalIgnoreCase))
            {
                dryRun = true;
                dryRunFull = true;
            }
            else if (args[i].Equals("--verbose", StringComparison.OrdinalIgnoreCase))
            {
                verbose = true;
            }
            else if (args[i].Equals("--once", StringComparison.OrdinalIgnoreCase))
            {
                once = true;
            }
            else if (args[i].Equals("--no-tray", StringComparison.OrdinalIgnoreCase))
            {
                noTray = true;
            }
            else if (args[i].Equals("--tray", StringComparison.OrdinalIgnoreCase))
            {
                forceTray = true;
            }
            else if (args[i].Equals("--show-console", StringComparison.OrdinalIgnoreCase))
            {
                forceShowConsole = true;
            }
            else if (args[i].Equals("--hide-console", StringComparison.OrdinalIgnoreCase))
            {
                forceHideConsole = true;
            }
            else
            {
                configPath = args[i];
            }
        }
        AppState.CommandLineVerbose = verbose;
        Logger.Verbose = verbose;

        // .NET 4.x defaults to TLS 1.0; Discord requires TLS 1.2+.
        System.Net.ServicePointManager.SecurityProtocol =
            System.Net.SecurityProtocolType.Tls12 |
            (System.Net.SecurityProtocolType)0x3000; // 0x3000 = TLS 1.3

        // Enable visual styles early so any dialog (including SetupDialog) looks correct.
        // Guard against calling it more than once; TrayHost does it too on its own thread.
        System.Windows.Forms.Application.EnableVisualStyles();
        System.Windows.Forms.Application.SetCompatibleTextRenderingDefault(false);

        // ---- Load config (or create it via setup dialog) ----
        bool configExists = File.Exists(configPath);
        IniConfig config  = new IniConfig();   // empty fallback until loaded
        if (configExists)
        {
            try
            {
                config = IniConfig.Load(configPath);
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to load config: " + ex.Message);
                return 1;
            }
        }

        string startupTransportMode = NormalizeTransportMode(config.Get("general", "transport_mode", "ipc"));
        string token = ResolveDiscordToken(config);
        string startupClientId = config.Get("general", "client_id", "").Trim();

        if (dryRun)
        {
            if (!configExists)
            {
                Logger.Error("Config file not found: " + Path.GetFullPath(configPath));
                return 1;
            }

            TemplateContext dryCtx;
            Dictionary<string, object> dryActivity = BuildActivity(config, out dryCtx);
            bool redactDryRun = !dryRunFull && config.GetBool("app", "dry_run_redact_sensitive", true);
            if (redactDryRun)
            {
                dryActivity = RedactActivityForDryRun(dryActivity, dryCtx);
            }

            Console.WriteLine("=== Activity JSON ===");
            Console.WriteLine(Json.Serialize(dryActivity));
            Console.WriteLine();
            Console.WriteLine("=== Available template tokens ===");
            if (redactDryRun)
            {
                Console.WriteLine("  Sensitive local tokens are redacted. Use --dry-run-full to show raw values.");
            }
            foreach (KeyValuePair<string, string> kv in dryCtx.GetTokens(redactDryRun))
            {
                Console.WriteLine("  {" + kv.Key + "} = " + kv.Value);
            }
            return 0;
        }

        if (configExists)
        {
            try
            {
                config = ProtectDiscordTokenInConfig(configPath, config);
                startupTransportMode = NormalizeTransportMode(config.Get("general", "transport_mode", "ipc"));
                token = ResolveDiscordToken(config);
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to protect Discord token in config: " + ex.Message);
                return 1;
            }
        }

        bool tokenRequired = startupTransportMode == "gateway";
        bool setupRequired = startupClientId.Length == 0 || (tokenRequired && token.Length == 0);
        if (setupRequired)
        {
            // Headless modes get a plain error; interactive modes get a setup dialog.
            if (once)
            {
                if (!configExists)
                {
                    Logger.Error("Config file not found: " + Path.GetFullPath(configPath));
                }
                else
                {
                    if (startupClientId.Length == 0)
                    {
                        Logger.Error("Missing [general] client_id in " + Path.GetFullPath(configPath));
                    }
                    if (tokenRequired && token.Length == 0)
                    {
                        Logger.Error("Missing [general] token_protected, token, or token_env in " + Path.GetFullPath(configPath));
                    }
                }
                return 1;
            }

            string existingClientId = config.Get("general", "client_id", "").Trim();
            string setupExistingToken = config.Get("general", "token", "").Trim();
            if (setupExistingToken.Length == 0 && token.Length > 0)
            {
                string setupTokenEnv = config.Get("general", "token_env", "").Trim();
                if (setupTokenEnv.Length > 0)
                {
                    setupExistingToken = "env:" + setupTokenEnv;
                }
                else if (config.Get("general", "token_protected", "").Trim().Length > 0)
                {
                    setupExistingToken = token;
                }
            }
            if (!SetupDialog.TrySetup(configPath, setupExistingToken, existingClientId, startupTransportMode, out token))
            {
                return 1;   // user cancelled
            }

            // Reload config so the rest of startup picks up all written values.
            try
            {
                config = IniConfig.Load(configPath);
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to reload config after setup: " + ex.Message);
                return 1;
            }
        }

        try
        {
            config = ConfigDefaults.EnsureAndReload(configPath, config);
        }
        catch (Exception ex)
        {
            Logger.Error("Failed to write missing config defaults: " + ex.Message);
            return 1;
        }

        string fullConfigPath = Path.GetFullPath(configPath);
        AppState.ConfigPath = fullConfigPath;
        Logger.ConfigureFile(fullConfigPath, config);
        AppState.ApplyConfig(config);

        if (!once && !AcquireSingleInstance(fullConfigPath, config))
        {
            return 0;
        }

        bool trayEnabled = !once && !noTray && (forceTray || config.GetBool("app", "show_tray", true));
        bool showConsole = config.GetBool("app", "show_console", true);
        if (forceShowConsole)
        {
            showConsole = true;
        }
        if (forceHideConsole)
        {
            showConsole = false;
        }

        TrayHost trayHost = null;
        if (trayEnabled)
        {
            trayHost = TrayHost.Start(fullConfigPath);
        }

        if (trayEnabled && !showConsole)
        {
            ConsoleWindow.SetVisible(false, true);
        }

        Logger.Info("Starting DiscordRPC.exe");
        Logger.Info("Config: " + fullConfigPath);
        Logger.Info("Update interval: " + Math.Max(5, config.GetInt("general", "update_interval", 5)).ToString(CultureInfo.InvariantCulture) + "s");
        AppState.Notify("DiscordRPC started", "Rich Presence is running.", ToolTipIcon.Info, NotificationEventKind.Start);

        int exitCode = 0;
        try
        {
            exitCode = RunPresenceLoop(fullConfigPath, config, verbose, once);
        }
        finally
        {
            if (trayHost != null)
            {
                trayHost.Stop();
            }

            ReleaseSingleInstance();
        }

        Logger.Info("Stopped.");
        return exitCode;
    }

    internal static void RequestStop()
    {
        stopping = true;
    }

    private static bool AcquireSingleInstance(string configPath, IniConfig config)
    {
        if (!config.GetBool("app", "single_instance", true))
        {
            Logger.Info("Single-instance protection disabled.");
            return true;
        }

        string mutexName = "Local\\DiscordRPC." + StableHash(configPath.ToUpperInvariant());
        bool createdNew;
        try
        {
            singleInstanceMutex = new Mutex(true, mutexName, out createdNew);
        }
        catch (Exception ex)
        {
            Logger.Error("Single-instance protection failed: " + ex.Message);
            return true;
        }

        if (createdNew)
        {
            Logger.Info("Single-instance scope: " + mutexName);
            return true;
        }

        Logger.Error("DiscordRPC is already running for this config.");
        try
        {
            MessageBox.Show(
                "DiscordRPC is already running for this config.",
                "DiscordRPC",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }
        catch
        {
        }

        ReleaseSingleInstance();
        return false;
    }

    private static void ReleaseSingleInstance()
    {
        Mutex mutex = singleInstanceMutex;
        singleInstanceMutex = null;
        if (mutex == null)
        {
            return;
        }

        try { mutex.ReleaseMutex(); } catch { }
        try { mutex.Dispose(); } catch { }
    }

    private static string StableHash(string value)
    {
        unchecked
        {
            ulong hash = 1469598103934665603UL;
            string text = value ?? "";
            for (int i = 0; i < text.Length; i++)
            {
                hash ^= text[i];
                hash *= 1099511628211UL;
            }

            return hash.ToString("X16", CultureInfo.InvariantCulture);
        }
    }

    private static int RunPresenceLoop(string configPath, IniConfig config, bool verbose, bool once)
    {
        string token = ResolveDiscordToken(config);
        string clientId = config.Get("general", "client_id", "").Trim();
        string transportMode = NormalizeTransportMode(config.Get("general", "transport_mode", "ipc"));
        string lastStatus = NormalizeStatus(config.Get("general", "status", "online"));
        DiscordIpcClient ipc = null;
        DiscordGatewayClient gateway = null;

        // Apply verbose setting from config (verbose_logging in [app] was never
        // being read at startup because ApplyConfig wasn't called before the loop).
        AppState.ApplyConfig(config);
        verbose = AppState.IsVerboseLogging;

        try
        {
            bool reportedIpcFailure = false;
            bool reportedGatewayConnectFailure = false;

            while (!stopping)
            {
                if (AppState.ReloadRequested)
                {
                    AppState.ReloadRequested = false;
                    try
                    {
                        IniConfig reloadedConfig = ConfigDefaults.EnsureAndReload(configPath, IniConfig.Load(configPath));
                        reloadedConfig = ProtectDiscordTokenInConfig(configPath, reloadedConfig);
                        string reloadedToken = ResolveDiscordToken(reloadedConfig);
                        string reloadedClientId = reloadedConfig.Get("general", "client_id", "").Trim();
                        string reloadedTransportMode = NormalizeTransportMode(reloadedConfig.Get("general", "transport_mode", "ipc"));
                        bool reconnectNeeded =
                            !reloadedToken.Equals(token, StringComparison.Ordinal) ||
                            !reloadedClientId.Equals(clientId, StringComparison.Ordinal) ||
                            !reloadedTransportMode.Equals(transportMode, StringComparison.Ordinal);

                        if (reconnectNeeded)
                        {
                            if (ipc != null)
                            {
                                ipc.Close();
                                ipc.Dispose();
                                ipc = null;
                            }

                            if (gateway != null)
                            {
                                gateway.Close();
                                gateway.Dispose();
                                gateway = null;
                            }

                            reportedIpcFailure = false;
                            reportedGatewayConnectFailure = false;
                        }

                        token = reloadedToken;
                        clientId = reloadedClientId;
                        transportMode = reloadedTransportMode;
                        lastStatus = NormalizeStatus(reloadedConfig.Get("general", "status", "online"));

                        config = reloadedConfig;
                        AppState.ApplyConfig(config);
                        Logger.Info("Configuration reloaded. Transport mode=" + transportMode);

                        // Always reconnect on reload so new status/activity_name take effect.
                        if (ipc != null)
                        {
                            ipc.Close();
                            ipc.Dispose();
                            ipc = null;
                        }

                        if (gateway != null)
                        {
                            gateway.Close();
                            gateway.Dispose();
                            gateway = null;
                        }

                        verbose = AppState.IsVerboseLogging;
                        reportedIpcFailure = false;
                        reportedGatewayConnectFailure = false;
                        AppState.Notify("DiscordRPC", "Configuration reloaded.", ToolTipIcon.Info, NotificationEventKind.Reload);
                    }
                    catch (Exception ex)
                    {
                        Logger.Error("Configuration reload failed: " + ex.Message);
                        AppState.Notify("DiscordRPC reload failed", ex.Message, ToolTipIcon.Error, NotificationEventKind.Failure);
                    }
                }

                try
                {
                    TemplateContext activityContext;
                    Dictionary<string, object> activity = BuildActivity(config, out activityContext);
                    string status = NormalizeStatus(config.Get("general", "status", "online"));
                    lastStatus = status;

                    if (verbose)
                    {
                        Dictionary<string, object> logActivity = config.GetBool("app", "verbose_redact_sensitive", true)
                            ? RedactActivityForDryRun(activity, activityContext)
                            : activity;
                        Logger.Debug("Presence JSON: " + Json.Serialize(logActivity) + " status=" + status);
                    }

                    bool updated = false;
                    if (transportMode == "gateway")
                    {
                        if (token.Length == 0)
                        {
                            if (!reportedGatewayConnectFailure)
                            {
                                Logger.Error("Gateway skipped: missing [general] token_protected, token, or token_env.");
                                AppState.Notify("DiscordRPC Gateway skipped", "Missing Discord token.", ToolTipIcon.Warning, NotificationEventKind.Failure);
                                reportedGatewayConnectFailure = true;
                            }
                        }
                        else
                        {
                            try
                            {
                                if (gateway == null)
                                {
                                    gateway = new DiscordGatewayClient(token, verbose, GatewayOptions.FromConfig(config));
                                }

                                if (!gateway.IsConnected)
                                {
                                    gateway.Connect(activity, status);
                                    reportedGatewayConnectFailure = false;
                                    Logger.Info(config.Get("messages", "rpc_restarted_message", "Rich Presence has been restarted"));
                                    AppState.Notify("DiscordRPC connected", "Rich Presence is active (Gateway).", ToolTipIcon.Info, NotificationEventKind.Success);
                                }

                                gateway.SetPresence(activity, status);
                                updated = true;

                                if (ipc != null)
                                {
                                    ipc.Close();
                                    ipc.Dispose();
                                    ipc = null;
                                }
                            }
                            catch (Exception ex)
                            {
                                if (!reportedGatewayConnectFailure)
                                {
                                    Logger.Error("Discord Gateway update failed: " + ex.Message);
                                    if (ex.Message.IndexOf("4004", StringComparison.Ordinal) >= 0 ||
                                        ex.Message.IndexOf("Authentication failed", StringComparison.OrdinalIgnoreCase) >= 0)
                                    {
                                        Logger.Error("Invalid token. Check [general] token_protected, token, or token_env in your config.");
                                        Logger.Error("If you use token_env, verify that the environment variable is set for this process.");
                                        AppState.Notify("DiscordRPC auth error", "Invalid Discord token.", ToolTipIcon.Error, NotificationEventKind.Failure);
                                        return 1;
                                    }

                                    AppState.Notify("DiscordRPC Gateway failed", ex.Message, ToolTipIcon.Warning, NotificationEventKind.Failure);
                                    reportedGatewayConnectFailure = true;
                                }

                                if (gateway != null)
                                {
                                    gateway.Close();
                                    gateway.Dispose();
                                    gateway = null;
                                }
                            }
                        }
                    }

                    if (!updated && TransportAllowsIpc(transportMode))
                    {
                        if (clientId.Length > 0)
                        {
                            if (ipc == null)
                            {
                                ipc = new DiscordIpcClient(clientId, verbose, IpcOptions.FromConfig(config));
                            }

                            try
                            {
                                if (!ipc.IsConnected)
                                {
                                    ipc.Connect();
                                    Logger.Info(config.Get("messages", "rpc_restarted_message", "Rich Presence has been restarted"));
                                    AppState.Notify("DiscordRPC connected", "Rich Presence is active (Discord IPC).", ToolTipIcon.Info, NotificationEventKind.Success);
                                }

                                ipc.SetActivity(activity);
                                reportedIpcFailure = false;
                                updated = true;

                                if (gateway != null)
                                {
                                    gateway.Close();
                                    gateway.Dispose();
                                    gateway = null;
                                }
                            }
                            catch (Exception ex)
                            {
                                if (!reportedIpcFailure)
                                {
                                    Logger.Error("Discord IPC update failed: " + ex.Message);
                                    AppState.Notify("DiscordRPC IPC failed", ex.Message, ToolTipIcon.Warning, NotificationEventKind.Failure);
                                    reportedIpcFailure = true;
                                }

                                if (ipc != null)
                                {
                                    ipc.Close();
                                    ipc.Dispose();
                                    ipc = null;
                                }
                            }
                        }
                        else if (!reportedIpcFailure)
                        {
                            Logger.Error("Discord IPC skipped: missing [general] client_id.");
                            reportedIpcFailure = true;
                        }
                    }

                    if (!updated && transportMode == "auto")
                    {
                        if (token.Length == 0)
                        {
                            if (!reportedGatewayConnectFailure)
                            {
                                Logger.Error("Gateway fallback skipped: missing [general] token_protected, token, or token_env.");
                                AppState.Notify("DiscordRPC Gateway skipped", "Missing Discord token.", ToolTipIcon.Warning, NotificationEventKind.Failure);
                                reportedGatewayConnectFailure = true;
                            }
                        }
                        else
                        {
                            try
                            {
                                if (gateway == null)
                                {
                                    gateway = new DiscordGatewayClient(token, verbose, GatewayOptions.FromConfig(config));
                                }

                                if (!gateway.IsConnected)
                                {
                                    gateway.Connect(activity, status);
                                    reportedGatewayConnectFailure = false;
                                    Logger.Info(config.Get("messages", "rpc_restarted_message", "Rich Presence has been restarted"));
                                    AppState.Notify("DiscordRPC connected", "Rich Presence is active (Gateway fallback).", ToolTipIcon.Info, NotificationEventKind.Success);
                                }

                                gateway.SetPresence(activity, status);
                                updated = true;

                                if (ipc != null)
                                {
                                    ipc.Close();
                                    ipc.Dispose();
                                    ipc = null;
                                }
                            }
                            catch (Exception ex)
                            {
                                if (!reportedGatewayConnectFailure)
                                {
                                    Logger.Error("Discord Gateway fallback update failed: " + ex.Message);
                                    if (ex.Message.IndexOf("4004", StringComparison.Ordinal) >= 0 ||
                                        ex.Message.IndexOf("Authentication failed", StringComparison.OrdinalIgnoreCase) >= 0)
                                    {
                                        Logger.Error("Invalid token. Check [general] token_protected, token, or token_env in your config.");
                                        Logger.Error("If you use token_env, verify that the environment variable is set for this process.");
                                        AppState.Notify("DiscordRPC auth error", "Invalid Discord token.", ToolTipIcon.Error, NotificationEventKind.Failure);
                                    }
                                    else
                                    {
                                        AppState.Notify("DiscordRPC Gateway failed", ex.Message, ToolTipIcon.Warning, NotificationEventKind.Failure);
                                    }
                                    reportedGatewayConnectFailure = true;
                                }

                                if (gateway != null)
                                {
                                    gateway.Close();
                                    gateway.Dispose();
                                    gateway = null;
                                }
                            }
                        }
                    }

                    if (!updated)
                    {
                        if (transportMode == "gateway")
                        {
                            Logger.Error("Presence update skipped: Gateway mode is selected but Gateway is unavailable.");
                        }
                        else if (transportMode == "ipc")
                        {
                            Logger.Error("Presence update skipped: IPC mode is selected but Discord IPC is unavailable.");
                        }
                        else
                        {
                            Logger.Error("Presence update skipped: both Gateway and IPC are unavailable.");
                        }

                        if (once)
                        {
                            return 1;
                        }

                        SleepInterruptible(10000);
                        continue;
                    }
                }
                catch (Exception ex)
                {
                    Logger.Error("Presence update failed: " + ex.Message);
                    AppState.Notify("DiscordRPC update failed", ex.Message, ToolTipIcon.Error, NotificationEventKind.Failure);
                    if (ipc != null) ipc.Close();
                    if (gateway != null) gateway.Close();
                }

                if (once)
                {
                    Logger.Info("One-shot update sent; keeping it visible briefly before exit.");
                    SleepInterruptible(10000);
                    break;
                }

                int updateInterval = Math.Max(5, config.GetInt("general", "update_interval", 5));
                SleepInterruptible(updateInterval * 1000);
            }

            try
            {
                if (!once && ipc != null && ipc.IsConnected)
                {
                    ipc.ClearActivity();
                }

                if (!once && gateway != null && gateway.IsConnected)
                {
                    gateway.ClearPresence(lastStatus);
                }
            }
            catch
            {
            }
        }
        finally
        {
            if (ipc != null)
            {
                ipc.Dispose();
            }

            if (gateway != null)
            {
                gateway.Dispose();
            }
        }

        return 0;
    }

    internal static string NormalizeTransportMode(string value)
    {
        string mode = (value ?? "").Trim().ToLowerInvariant();
        if (mode == "gateway" || mode == "ipc" || mode == "auto")
        {
            return mode;
        }

        Logger.Error("Invalid [general] transport_mode \"" + value + "\"; defaulting to ipc. Valid: ipc, gateway, auto.");
        return "ipc";
    }

    internal static string NormalizeStatus(string value)
    {
        string status = (value ?? "").Trim().ToLowerInvariant();
        if (status == "online" || status == "idle" || status == "dnd" || status == "invisible")
        {
            return status;
        }

        Logger.Error("Invalid [general] status \"" + value + "\"; defaulting to \"online\". Valid: online, idle, dnd, invisible.");
        return "online";
    }

    internal static string ResolveDiscordToken(IniConfig config)
    {
        string tokenEnv = config.Get("general", "token_env", "").Trim();
        if (tokenEnv.Length > 0)
        {
            string envToken = Environment.GetEnvironmentVariable(tokenEnv);
            if (!string.IsNullOrEmpty(envToken))
            {
                return envToken.Trim();
            }

            Logger.Error("Configured Discord token environment variable is empty or missing: " + tokenEnv);
        }

        string protectedToken = config.Get("general", "token_protected", "").Trim();
        if (protectedToken.Length > 0)
        {
            try
            {
                return ProtectedSecrets.Unprotect(protectedToken).Trim();
            }
            catch (Exception ex)
            {
                Logger.Error("Could not decrypt [general] token_protected with DPAPI for this user: " + ex.Message);
            }
        }

        string token = config.Get("general", "token", "").Trim();
        if (IsTokenEnvironmentReference(token))
        {
            string inlineEnv = token.Substring(4).Trim();
            string envToken = Environment.GetEnvironmentVariable(inlineEnv);
            if (!string.IsNullOrEmpty(envToken))
            {
                return envToken.Trim();
            }

            Logger.Error("Configured Discord token environment variable is empty or missing: " + inlineEnv);
            return "";
        }

        return token;
    }

    internal static IniConfig ProtectDiscordTokenInConfig(string configPath, IniConfig config)
    {
        if (config == null)
        {
            return config;
        }

        string token = config.Get("general", "token", "").Trim();
        if (token.Length == 0 || IsTokenEnvironmentReference(token))
        {
            return config;
        }

        string protectedToken = config.Get("general", "token_protected", "").Trim();
        Dictionary<string, string> updates = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        updates[IniConfigFile.MakeKey("general", "token")] = "";

        if (protectedToken.Length == 0)
        {
            updates[IniConfigFile.MakeKey("general", "token_protected")] = ProtectedSecrets.Protect(token);
            Logger.Info("Migrating [general] token to DPAPI-protected [general] token_protected.");
        }
        else
        {
            Logger.Info("Clearing legacy plaintext [general] token because token_protected is already configured.");
        }

        IniConfigFile.WriteValues(configPath, updates);
        return IniConfig.Load(configPath);
    }

    internal static bool IsTokenEnvironmentReference(string token)
    {
        if (string.IsNullOrWhiteSpace(token))
        {
            return false;
        }

        string trimmed = token.Trim();
        return trimmed.StartsWith("env:", StringComparison.OrdinalIgnoreCase) &&
            trimmed.Substring(4).Trim().Length > 0;
    }

    internal static bool HasPlaintextDiscordToken(IniConfig config)
    {
        string token = config.Get("general", "token", "").Trim();
        return token.Length > 0 && !IsTokenEnvironmentReference(token);
    }

    internal static bool HasProtectedDiscordToken(IniConfig config)
    {
        return config.Get("general", "token_protected", "").Trim().Length > 0;
    }

    private static bool TransportAllowsIpc(string mode)
    {
        return mode == "ipc" || mode == "auto";
    }

    private static Dictionary<string, object> BuildActivity(IniConfig config)
    {
        TemplateContext unusedContext;
        return BuildActivity(config, out unusedContext);
    }

    private static Dictionary<string, object> RedactActivityForDryRun(Dictionary<string, object> activity, TemplateContext ctx)
    {
        Dictionary<string, string> sensitiveValues = ctx.GetSensitiveTokenValues();
        object redacted = RedactObjectForDryRun(activity, sensitiveValues);
        Dictionary<string, object> redactedActivity = redacted as Dictionary<string, object>;
        return redactedActivity ?? activity;
    }

    private static object RedactObjectForDryRun(object value, Dictionary<string, string> sensitiveValues)
    {
        string text = value as string;
        if (text != null)
        {
            return RedactStringForDryRun(text, sensitiveValues);
        }

        IDictionary<string, object> dict = value as IDictionary<string, object>;
        if (dict != null)
        {
            Dictionary<string, object> copy = new Dictionary<string, object>();
            foreach (KeyValuePair<string, object> item in dict)
            {
                copy[item.Key] = RedactObjectForDryRun(item.Value, sensitiveValues);
            }
            return copy;
        }

        IEnumerable enumerable = value as IEnumerable;
        if (enumerable != null && !(value is string))
        {
            List<object> copy = new List<object>();
            foreach (object item in enumerable)
            {
                copy.Add(RedactObjectForDryRun(item, sensitiveValues));
            }
            return copy;
        }

        return value;
    }

    private static string RedactStringForDryRun(string value, Dictionary<string, string> sensitiveValues)
    {
        string result = value ?? "";
        foreach (KeyValuePair<string, string> entry in sensitiveValues)
        {
            string sensitive = entry.Value;
            if (string.IsNullOrEmpty(sensitive))
            {
                continue;
            }

            result = result.Replace(sensitive, "<redacted:" + entry.Key + ">");
        }

        return result;
    }

    private static Dictionary<string, object> BuildActivity(IniConfig config, out TemplateContext ctx)
    {
        DateTime now        = DateTime.Now;
        SystemStats stats   = WindowsInfo.GetSystemStats(cpuSampler);
        BatteryInfo battery = WindowsInfo.GetBatteryInfo();
        int idleSeconds     = WindowsInfo.GetInputIdleSeconds();

        // Get foreground window info; apply censor rules to the title only
        string idleMessage = config.Get("general", "idle_message", "Idle");
        ForegroundWindowInfo rawWindow = WindowsInfo.GetForegroundWindowInfo(idleMessage);
        string censoredTitle = ApplyCensorRules(rawWindow.Title, config);

        // Rebuild a window info record with the censored title for use in templates
        bool windowDetectionEnabled = config.GetBool("general", "window_title_detection_enabled", true);
        string fallbackDetails = config.Get("general", "fallback_details", idleMessage).Trim();
        if (fallbackDetails.Length == 0)
        {
            fallbackDetails = idleMessage;
        }
        string effectiveTitle = windowDetectionEnabled ? censoredTitle : fallbackDetails;
        ForegroundWindowInfo window = new ForegroundWindowInfo(
            effectiveTitle,
            windowDetectionEnabled ? rawWindow.ClassName  : "",
            windowDetectionEnabled ? rawWindow.ExeName    : "",
            windowDetectionEnabled ? rawWindow.ExePath    : "",
            windowDetectionEnabled ? rawWindow.Pid        : 0);

        ctx = new TemplateContext(now, stats, battery, window, idleSeconds);

        Dictionary<string, object> activity = new Dictionary<string, object>();

        // Newer Discord API versions require a stable per-activity ID string.
        // We derive one from the client_id so it stays consistent across updates.
        string clientId = config.Get("general", "client_id", "").Trim();
        activity["id"] = clientId.Length > 0
            ? clientId.Substring(clientId.Length > 16 ? clientId.Length - 16 : 0)
            : "discordrpc0000001";

        // flags = 1 (INSTANCE) — kept for IPC path; NOT forwarded to Gateway (see BuildGatewayActivity).
        activity["flags"] = 1;

        // Gateway requires "name" and "type". type 0=Playing, 2=Listening, 3=Watching, 5=Competing
        string activityName = ctx.Format(config.Get("general", "activity_name", "a game").Trim());
        if (activityName.Length == 0) activityName = "a game";
        activity["name"] = activityName;

        string activityTypeStr = config.Get("general", "activity_type", "0").Trim();
        int activityType;
        if (!int.TryParse(activityTypeStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out activityType)
            || activityType < 0 || activityType > 5 || activityType == 4)
        {
            Logger.Error("Invalid [general] activity_type \"" + activityTypeStr + "\"; defaulting to 0 (Playing). Valid: 0=Playing, 2=Listening, 3=Watching, 5=Competing.");
            activityType = 0;
        }
        activity["type"] = activityType;

        // application_id lets Discord resolve image assets from the Developer Portal app
        if (clientId.Length > 0)
        {
            activity["application_id"] = clientId;
        }

        if (config.GetBool("layout", "details_field", true))
        {
            // Default reproduces original: show the (censored) foreground window title
            string detailsTemplate = config.Get("general", "details_template", "{win_title}");
            string details = ctx.Format(detailsTemplate);
            if (details.Length > 0)
            {
                activity["details"] = LimitDiscordText(details, 128);
            }
        }

        if (config.GetBool("layout", "state_field", true))
        {
            // Default reproduces original CPU/RAM line
            string stateTemplate = config.Get("general", "state_template", "CPU: {cpu}, RAM: {ram_used}/{ram_total} ({ram_pct})");
            string state = ctx.Format(stateTemplate);
            if (state.Length > 0)
            {
                activity["state"] = LimitDiscordText(state, 128);
            }
        }

        Dictionary<string, object> timestamps = new Dictionary<string, object>();
        timestamps["start"] = WindowsInfo.GetBootUnixTime();
        activity["timestamps"] = timestamps;

        AssetInfo large = ChooseTimedAsset(config, "large_time_ranges", "large_assets", ctx);
        AssetInfo small = ChooseSmallAsset(config, ctx);

        Dictionary<string, object> assets = new Dictionary<string, object>();
        if (config.GetBool("layout", "show_large_image", true))
        {
            AddIfNotEmpty(assets, "large_image", large.Image);
            AddIfNotEmpty(assets, "large_text", LimitDiscordText(large.Text, 128));
        }

        if (config.GetBool("layout", "show_small_image", true))
        {
            AddIfNotEmpty(assets, "small_image", small.Image);
            AddIfNotEmpty(assets, "small_text", LimitDiscordText(small.Text, 128));
        }

        if (assets.Count > 0)
        {
            activity["assets"] = assets;
        }

        if (config.GetBool("layout", "show_buttons", true))
        {
            List<object> buttons = new List<object>();
            AddButton(buttons, config.Get("buttons", "button_1_label", ""), config.Get("buttons", "button_1_url", ""), ctx);
            AddButton(buttons, config.Get("buttons", "button_2_label", ""), config.Get("buttons", "button_2_url", ""), ctx);
            if (buttons.Count > 0)
            {
                activity["buttons"] = buttons;
            }
        }

        return activity;
    }

    private static void AddButton(List<object> buttons, string labelTemplate, string url, TemplateContext ctx)
    {
        string label = ctx.Format(labelTemplate);
        if (label.Length == 0 || url.Trim().Length == 0)
        {
            return;
        }

        Dictionary<string, object> button = new Dictionary<string, object>();
        button["label"] = LimitDiscordText(label, 32);
        button["url"] = url.Trim();
        buttons.Add(button);
    }

    private static void AddIfNotEmpty(Dictionary<string, object> target, string key, string value)
    {
        if (value != null && value.Trim().Length > 0)
        {
            target[key] = value.Trim();
        }
    }

    private static AssetInfo ChooseSmallAsset(IniConfig config, TemplateContext ctx)
    {
        // idle_threshold = 0 means AFK detection disabled
        int threshold = config.GetInt("afk", "idle_threshold", 60);
        bool afkDetectionEnabled = threshold > 0;
        // Reuse the idle seconds already sampled when the TemplateContext was built
        // so that the threshold comparison is consistent with the {idle_*} tokens.
        int idleSeconds = afkDetectionEnabled ? ctx.IdleSeconds : 0;

        // Also trigger AFK display if the foreground window is the Windows lock screen
        bool lockScreen = string.Equals(ctx.Format("{win_title}"),
            "Windows Default Lock Screen", StringComparison.OrdinalIgnoreCase);

        if (afkDetectionEnabled && (idleSeconds >= threshold || lockScreen))
        {
            string idleText = "";
            if (config.GetBool("afk", "show_idle_time", true))
            {
                idleText = ctx.Format(config.Get("afk", "idle_text", "since {idle_hh}h {idle_mm}m {idle_ss}s"));
            }

            string message = ctx.Format(config.Get("afk", "afk_message", "AFK"));
            string combined = (message + " " + idleText).Trim();
            return new AssetInfo(
                ctx.Format(config.Get("afk", "afk_image", "zzz")),
                combined);
        }

        return ChooseTimedAsset(config, "small_time_ranges", "small_assets", ctx);
    }

    private static AssetInfo ChooseTimedAsset(IniConfig config, string rangeSection, string assetSection, TemplateContext ctx)
    {
        int hour = DateTime.Now.Hour;
        string slot = "default";

        if (HourInRange(hour, config.GetInt(rangeSection, "morning_start", 5), config.GetInt(rangeSection, "morning_end", 12)))
        {
            slot = "morning";
        }
        else if (HourInRange(hour, config.GetInt(rangeSection, "afternoon_start", 12), config.GetInt(rangeSection, "afternoon_end", 16)))
        {
            slot = "afternoon";
        }
        else if (HourInRange(hour, config.GetInt(rangeSection, "evening_start", 16), config.GetInt(rangeSection, "evening_end", 22)))
        {
            slot = "evening";
        }
        else if (HourInRange(hour, config.GetInt(rangeSection, "night_start", 22), config.GetInt(rangeSection, "night_end", 5)))
        {
            slot = "night";
        }

        string image = ctx.Format(config.Get(assetSection, slot + "_image", config.Get(assetSection, "default_image", "")));
        string text  = ctx.Format(config.Get(assetSection, slot + "_text",  config.Get(assetSection, "default_text",  "")));
        return new AssetInfo(image, text);
    }

    private static bool HourInRange(int hour, int start, int end)
    {
        start = NormalizeHour(start);
        end = NormalizeHour(end);

        if (start == end)
        {
            return true;
        }

        if (start < end)
        {
            return hour >= start && hour < end;
        }

        return hour >= start || hour < end;
    }

    private static int NormalizeHour(int hour)
    {
        hour %= 24;
        if (hour < 0)
        {
            hour += 24;
        }

        return hour;
    }

    private static string ApplyCensorRules(string title, IniConfig config)
    {
        string result = title ?? "";

        if (config.GetBool("censor_map", "apply_pattern_on_raw", false))
        {
            return ApplyPatternReplace(result, ParseKeyValueList(config.Get("censor_map", "pattern_replace", "")));
        }

        string[] rules = config.Get("censor_map", "rule_order", "full_replace, word_replace, pattern_replace").Split(',');
        for (int i = 0; i < rules.Length; i++)
        {
            string rule = rules[i].Trim().ToLowerInvariant();
            if (rule == "full_replace")
            {
                List<KeyValuePair<string, string>> replacements = ParseKeyValueList(config.Get("censor_map", "full_replace", ""));
                for (int r = 0; r < replacements.Count; r++)
                {
                    if (result.IndexOf(replacements[r].Key, StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        result = replacements[r].Value;
                        break;
                    }
                }
            }
            else if (rule == "word_replace")
            {
                List<KeyValuePair<string, string>> replacements = ParseKeyValueList(config.Get("censor_map", "word_replace", ""));
                for (int r = 0; r < replacements.Count; r++)
                {
                    result = ReplaceOrdinalIgnoreCase(result, replacements[r].Key, replacements[r].Value);
                }
            }
            else if (rule == "pattern_replace")
            {
                result = ApplyPatternReplace(result, ParseKeyValueList(config.Get("censor_map", "pattern_replace", "")));
            }
        }

        if (result.Length == 1)
        {
            result += " ";
        }

        return result;
    }

    private static string ApplyPatternReplace(string input, List<KeyValuePair<string, string>> replacements)
    {
        string result = input;
        for (int i = 0; i < replacements.Count; i++)
        {
            // An empty replacement string clears the match.
            // Users can write "pattern = " (nothing after =) for an empty replacement.
            string replacement = replacements[i].Value;
            try
            {
                result = Regex.Replace(result, replacements[i].Key, replacement, RegexOptions.None, CensorRegexTimeout);
            }
            catch (RegexMatchTimeoutException)
            {
                Logger.Error("Timed out regex in [censor_map] pattern_replace: " + replacements[i].Key);
            }
            catch (ArgumentException ex)
            {
                Logger.Error("Invalid regex in [censor_map] pattern_replace: " + replacements[i].Key + " (" + ex.Message + ")");
            }
        }

        return result;
    }

    private static string ReplaceOrdinalIgnoreCase(string input, string oldValue, string newValue)
    {
        if (string.IsNullOrEmpty(input) || string.IsNullOrEmpty(oldValue))
        {
            return input;
        }

        StringBuilder builder = null;
        int start = 0;
        for (;;)
        {
            int index = input.IndexOf(oldValue, start, StringComparison.OrdinalIgnoreCase);
            if (index < 0)
            {
                break;
            }

            if (builder == null)
            {
                builder = new StringBuilder(input.Length);
            }

            builder.Append(input, start, index - start);
            builder.Append(newValue ?? "");
            start = index + oldValue.Length;
        }

        if (builder == null)
        {
            return input;
        }

        builder.Append(input, start, input.Length - start);
        return builder.ToString();
    }

    private static List<KeyValuePair<string, string>> ParseKeyValueList(string value)
    {
        List<KeyValuePair<string, string>> result = new List<KeyValuePair<string, string>>();
        string[] lines = value.Replace("\r\n", "\n").Split('\n');
        for (int i = 0; i < lines.Length; i++)
        {
            string line = lines[i].Trim();
            if (line.Length == 0)
            {
                continue;
            }

            int separator = line.IndexOf(" = ", StringComparison.Ordinal);
            if (separator >= 0)
            {
                string key = line.Substring(0, separator).Trim();
                string replacement = line.Substring(separator + 3).Trim();
                result.Add(new KeyValuePair<string, string>(key, replacement));
                continue;
            }

            separator = line.IndexOf('=');
            if (separator < 0)
            {
                continue;
            }

            {
                string key = line.Substring(0, separator).Trim();
                string replacement = line.Substring(separator + 1).Trim();
                result.Add(new KeyValuePair<string, string>(key, replacement));
            }
        }

        return result;
    }


    private static string LimitDiscordText(string value, int maxLength)
    {
        if (value == null)
        {
            return "";
        }

        value = value.Trim();
        if (value.Length <= maxLength)
        {
            return value;
        }

        if (maxLength <= 1)
        {
            return value.Substring(0, maxLength);
        }

        return value.Substring(0, Math.Max(0, maxLength - 3)) + "...";
    }

    private static void SleepInterruptible(int milliseconds)
    {
        int remaining = milliseconds;
        while (!stopping && remaining > 0)
        {
            int slice = Math.Min(250, remaining);
            Thread.Sleep(slice);
            remaining -= slice;
        }
    }
}

internal static class ProtectedSecrets
{
    private const string Prefix = "dpapi:";

    public static string Protect(string secret)
    {
        byte[] plain = Encoding.UTF8.GetBytes(secret ?? "");
        byte[] cipher = ProtectedData.Protect(plain, null, DataProtectionScope.CurrentUser);
        return Prefix + ToHex(cipher);
    }

    public static string Unprotect(string value)
    {
        string text = (value ?? "").Trim();
        if (!text.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new FormatException("Protected value must start with " + Prefix);
        }

        byte[] cipher = FromHex(text.Substring(Prefix.Length));
        byte[] plain = ProtectedData.Unprotect(cipher, null, DataProtectionScope.CurrentUser);
        return Encoding.UTF8.GetString(plain);
    }

    private static string ToHex(byte[] bytes)
    {
        StringBuilder builder = new StringBuilder(bytes.Length * 2);
        for (int i = 0; i < bytes.Length; i++)
        {
            builder.Append(bytes[i].ToString("x2", CultureInfo.InvariantCulture));
        }
        return builder.ToString();
    }

    private static byte[] FromHex(string hex)
    {
        if (hex == null || (hex.Length % 2) != 0)
        {
            throw new FormatException("Protected value has invalid hex length.");
        }

        byte[] bytes = new byte[hex.Length / 2];
        for (int i = 0; i < bytes.Length; i++)
        {
            int hi = HexValue(hex[i * 2]);
            int lo = HexValue(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0)
            {
                throw new FormatException("Protected value contains non-hex characters.");
            }
            bytes[i] = (byte)((hi << 4) | lo);
        }
        return bytes;
    }

    private static int HexValue(char ch)
    {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }
}

internal static class AppState
{
    public static volatile bool ReloadRequested;
    public static volatile bool NotificationsEnabled = true;
    public static volatile bool NotifyOnStart;
    public static volatile bool NotifyOnSuccess;
    public static volatile bool NotifyOnFailure = true;
    public static volatile bool NotifyOnReload = true;
    public static bool CommandLineVerbose;
    public static string ConfigPath = AppPaths.DefaultIniPath;
    public static event Action<string, string, ToolTipIcon> NotificationRequested;

    public static bool IsVerboseLogging
    {
        get { return Logger.Verbose; }
    }

    public static void ApplyConfig(IniConfig config)
    {
        UiStrings.Load(config);
        Logger.ConfigureFile(ConfigPath, config);
        NotificationsEnabled = config.GetBool("app", "notifications_enabled", true);
        NotifyOnStart = config.GetBool("app", "notify_on_start", false);
        NotifyOnSuccess = config.GetBool("app", "notify_on_success", false);
        NotifyOnFailure = config.GetBool("app", "notify_on_failure", true);
        NotifyOnReload = config.GetBool("app", "notify_on_reload", true);
        Logger.LoggingEnabled = config.GetBool("app", "logging_enabled", true);
        Logger.Verbose = CommandLineVerbose || config.GetBool("app", "verbose_logging", false);
    }

    public static void RequestReload()
    {
        ReloadRequested = true;
        Logger.Info("Configuration reload requested.");
    }

    public static void Notify(string title, string message, ToolTipIcon icon, NotificationEventKind kind)
    {
        if (!NotificationsEnabled || !ShouldNotify(kind))
        {
            return;
        }

        Action<string, string, ToolTipIcon> handler = NotificationRequested;
        if (handler != null)
        {
            handler(title, message, icon);
        }
    }

    private static bool ShouldNotify(NotificationEventKind kind)
    {
        if (kind == NotificationEventKind.Start)
        {
            return NotifyOnStart;
        }
        if (kind == NotificationEventKind.Success)
        {
            return NotifyOnSuccess;
        }
        if (kind == NotificationEventKind.Failure)
        {
            return NotifyOnFailure;
        }
        if (kind == NotificationEventKind.Reload)
        {
            return NotifyOnReload;
        }

        return true;
    }
}

internal enum NotificationEventKind
{
    Start,
    Success,
    Failure,
    Reload
}

internal static class ConsoleWindow
{
    private const int SwHide = 0;
    private const int SwShow = 5;

    public static bool IsVisible()
    {
        IntPtr window = NativeMethods.GetConsoleWindow();
        return window != IntPtr.Zero && NativeMethods.IsWindowVisible(window);
    }

    public static void SetVisible(bool visible, bool safeOnly)
    {
        IntPtr window = NativeMethods.GetConsoleWindow();
        if (window == IntPtr.Zero)
        {
            return;
        }

        if (!visible && safeOnly && !OwnsConsole())
        {
            Logger.Info("Console hide skipped because this process appears to share the console with another process.");
            return;
        }

        NativeMethods.ShowWindow(window, visible ? SwShow : SwHide);
    }

    private static bool OwnsConsole()
    {
        uint[] processIds = new uint[8];
        uint count = NativeMethods.GetConsoleProcessList(processIds, (uint)processIds.Length);
        return count <= 1;
    }
}

internal static class UiStrings
{
    private static readonly object SyncRoot = new object();

    private static readonly string[,] Defaults = new string[,]
    {
        { "category_general", "General" },
        { "category_presence_layout", "Presence layout" },
        { "category_censoring", "Censoring" },
        { "category_assets_time", "Assets and time" },
        { "category_buttons", "Buttons" },
        { "category_afk", "AFK" },
        { "category_notifications", "Notifications" },
        { "category_logging", "Logging" },
        { "category_methods", "Methods" },
        { "category_advanced", "Advanced" },

        { "enable_window_detection", "Enable window detection" },
        { "application_id", "Application ID" },
        { "transport_mode", "Transport mode" },
        { "transport_mode_gateway", "Gateway fallback (no Discord desktop required)" },
        { "transport_mode_ipc", "Discord IPC (default)" },
        { "transport_mode_auto", "Auto (IPC, then Gateway fallback)" },
        { "token_env", "Discord token environment variable" },
        { "token_protected", "DPAPI-protected Discord token" },
        { "update_interval", "Update interval" },
        { "idle_message", "Idle message" },
        { "fallback_details", "Fallback details" },
        { "details_template", "Details template" },
        { "state_template", "State template" },
        { "details_field", "Details field" },
        { "state_field", "State field" },
        { "large_image", "Large image" },
        { "small_image", "Small image" },
        { "buttons", "Buttons" },
        { "apply_regex_raw", "Apply regex on raw titlebar" },
        { "rule_order", "Rule order" },
        { "full_replace_map", "Full replace map" },
        { "word_replace_map", "Word replace map" },
        { "regex_replace_map", "Regex replace map" },
        { "large_time_ranges", "Large time ranges" },
        { "small_time_ranges", "Small time ranges" },
        { "large_assets", "Large assets" },
        { "small_assets", "Small assets" },
        { "button_1_label", "Button 1 label" },
        { "button_1_url", "Button 1 URL" },
        { "button_2_label", "Button 2 label" },
        { "button_2_url", "Button 2 URL" },
        { "idle_threshold", "Idle threshold" },
        { "afk_message", "AFK message" },
        { "afk_image", "AFK image" },
        { "show_idle_time", "Show idle time" },
        { "idle_text", "Idle text" },
        { "enable_notifications", "Enable" },
        { "notify_on_start", "Notify on start" },
        { "notify_on_success", "Notify on success" },
        { "notify_on_failure", "Notify on failure" },
        { "notify_on_reload", "Notify on reload" },
        { "enable_logging", "Enable logging" },
        { "verbose_ipc_logging", "Verbose logging" },
        { "redact_verbose_log", "Redact sensitive verbose logs" },
        { "show_console", "Show console" },
        { "show_recent_log", "Show recent log" },
        { "file_logging", "File logging" },
        { "log_path", "Log path" },
        { "open_log_folder", "Open log folder" },
        { "backup_config_on_save", "Config backups on save" },
        { "tray_icon", "Tray icon" },
        { "show_menu_as_dropdown", "Show menu as dropdown" },
        { "single_instance", "Single-instance protection" },
        { "hide_disabled_entries", "Hide disabled entries" },
        { "redact_dry_run", "Redact sensitive dry-run output" },
        { "ipc_connect_timeout", "IPC connect timeout" },
        { "ipc_response_timeout", "IPC response timeout" },
        { "gateway_system_locale", "Gateway system locale" },
        { "gateway_browser_user_agent", "Gateway browser user agent" },
        { "gateway_browser_version", "Gateway browser version" },
        { "gateway_os_version", "Gateway OS version" },
        { "gateway_release_channel", "Gateway release channel" },
        { "gateway_client_build_number", "Gateway client build number" },
        { "gateway_capabilities", "Gateway capabilities" },
        { "gateway_connect_timeout", "Gateway connect timeout" },
        { "gateway_hello_timeout", "Gateway HELLO timeout" },
        { "gateway_ready_timeout", "Gateway READY timeout" },
        { "gateway_send_timeout", "Gateway send timeout" },
        { "gateway_close_timeout", "Gateway close timeout" },
        { "gateway_asset_fetch_timeout", "Gateway asset fetch timeout" },
        { "gateway_asset_fetch_user_agent", "Gateway asset fetch user agent" },
        { "restart_message", "Restart message" },
        { "reload_configuration", "Reload configuration" },
        { "exit", "Exit" },

        { "current_value_format", "Current: {0}" },
        { "change_menu_format", "Change {0}..." },
        { "dialog_label_format", "[{0}] {1}" },
        { "dialog_title_format", "{0}" },
        { "empty_value", "(empty)" },
        { "multiline_entries_format", "{0} entries" },
        { "multiline_preview_format", "{0} entries; {1}" },
        { "config_path_format", "Config: {0}" },
        { "console_state_format", "Console: {0}" },
        { "enabled_text", "enabled" },
        { "disabled_text", "disabled" },
        { "ok", "OK" },
        { "cancel", "Cancel" },
        { "number_validation", "Enter a non-negative whole number." },
        { "recent_log_title", "DiscordRPC recent log" },
        { "no_recent_log", "No log entries yet." },
        { "config_update_error_title", "DiscordRPC" },
        { "config_update_error_format", "Failed to update config:\n{0}" }
    };

    private static Dictionary<string, string> values = CreateDefaults();

    public static void Load(IniConfig config)
    {
        Dictionary<string, string> loaded = CreateDefaults();
        for (int i = 0; i < Defaults.GetLength(0); i++)
        {
            string key = Defaults[i, 0];
            loaded[key] = DecodeEscapes(config.Get("strings", key, Defaults[i, 1]));
        }

        lock (SyncRoot)
        {
            values = loaded;
        }
    }

    public static string Get(string key)
    {
        lock (SyncRoot)
        {
            string value;
            return values.TryGetValue(key, out value) ? value : key;
        }
    }

    public static string Format(string key, params object[] args)
    {
        string format = Get(key);
        try
        {
            return string.Format(CultureInfo.InvariantCulture, format, args);
        }
        catch (FormatException)
        {
            return string.Join(" ", Array.ConvertAll(args, delegate(object value)
            {
                return value == null ? "" : value.ToString();
            }));
        }
    }

    public static void AddDefaultConfigEntries(Dictionary<string, string> target)
    {
        for (int i = 0; i < Defaults.GetLength(0); i++)
        {
            target[IniConfigFile.MakeKey("strings", Defaults[i, 0])] = EncodeEscapes(Defaults[i, 1]);
        }
    }

    private static Dictionary<string, string> CreateDefaults()
    {
        Dictionary<string, string> defaults = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < Defaults.GetLength(0); i++)
        {
            defaults[Defaults[i, 0]] = Defaults[i, 1];
        }
        return defaults;
    }

    private static string EncodeEscapes(string value)
    {
        if (value == null)
        {
            return "";
        }

        return value.Replace("\\", "\\\\").Replace("\r\n", "\n").Replace("\r", "\n").Replace("\n", "\\n").Replace("\t", "\\t");
    }

    private static string DecodeEscapes(string value)
    {
        if (value == null)
        {
            return "";
        }

        return value.Replace("\\n", "\n").Replace("\\t", "\t");
    }
}

internal static class ConfigDefaults
{
    private const string MissingSentinel = "\u001e__discordrpc_missing_config__";

    internal const string DefaultDetailsTemplate = "{win_title}";
    internal const string DefaultStateTemplate = "CPU: {cpu}, RAM: {ram_used}/{ram_total} ({ram_pct})";
    internal const string DefaultFallbackDetails = "Foreground program detection is disabled.";
    internal const string DefaultStaticDetails = "Using my PC";
    internal const string DefaultStaticState = "Online";
    internal const string DefaultGatewayBrowserUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9181 Chrome/128.0.6613.186 Electron/32.2.7 Safari/537.36";

    public static IniConfig EnsureAndReload(string path, IniConfig config)
    {
        if (Ensure(path, config))
        {
            return IniConfig.Load(path);
        }

        return config;
    }

    public static bool Ensure(string path, IniConfig config)
    {
        if (config == null)
        {
            config = new IniConfig();
        }

        Dictionary<string, string> defaults = CreateDefaults();
        Dictionary<string, string> missing = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        foreach (KeyValuePair<string, string> entry in defaults)
        {
            string section;
            string key;
            IniConfigFile.SplitKey(entry.Key, out section, out key);
            if (config.Get(section, key, MissingSentinel) == MissingSentinel)
            {
                missing[entry.Key] = entry.Value;
            }
        }

        if (missing.Count == 0)
        {
            return false;
        }

        IniConfigFile.WriteValues(path, missing);
        Logger.Info("Added " + missing.Count.ToString(CultureInfo.InvariantCulture) + " missing config default(s).");
        return true;
    }

    public static Dictionary<string, string> CreateSetupPresenceUpdates(
        bool useWindowDetection,
        string staticDetails,
        bool useSystemStats,
        string staticState)
    {
        Dictionary<string, string> updates = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        string details = NormalizeSetupText(staticDetails, DefaultStaticDetails);
        string state = NormalizeSetupText(staticState, DefaultStaticState);

        Add(updates, "general", "window_title_detection_enabled", useWindowDetection ? "true" : "false");
        Add(updates, "general", "details_template", DefaultDetailsTemplate);
        Add(updates, "general", "fallback_details", useWindowDetection ? DefaultFallbackDetails : details);
        Add(updates, "layout", "details_field", "true");

        Add(updates, "general", "state_template", useSystemStats ? DefaultStateTemplate : state);
        Add(updates, "layout", "state_field", "true");

        return updates;
    }

    private static Dictionary<string, string> CreateDefaults()
    {
        Dictionary<string, string> defaults = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        Add(defaults, "general", "transport_mode", "ipc");
        Add(defaults, "general", "update_interval", "5");
        Add(defaults, "general", "idle_message", "Idle");
        Add(defaults, "general", "window_title_detection_enabled", "true");
        Add(defaults, "general", "fallback_details", DefaultFallbackDetails);
        Add(defaults, "general", "activity_name", "a game");
        Add(defaults, "general", "activity_type", "0");
        Add(defaults, "general", "status", "online");
        Add(defaults, "general", "details_template", DefaultDetailsTemplate);
        Add(defaults, "general", "state_template", DefaultStateTemplate);
        Add(defaults, "general", "token_env", "");
        Add(defaults, "general", "token_protected", "");

        Add(defaults, "censor_map", "rule_order", "full_replace, word_replace, pattern_replace");
        Add(defaults, "censor_map", "full_replace", "");
        Add(defaults, "censor_map", "word_replace", "");
        Add(defaults, "censor_map", "pattern_replace", "");
        Add(defaults, "censor_map", "apply_pattern_on_raw", "false");

        AddTimeRangeDefaults(defaults, "large_time_ranges");
        AddTimeRangeDefaults(defaults, "small_time_ranges");
        AddAssetDefaults(defaults, "large_assets");
        AddAssetDefaults(defaults, "small_assets");

        Add(defaults, "layout", "show_large_image", "false");
        Add(defaults, "layout", "show_small_image", "false");
        Add(defaults, "layout", "show_buttons", "false");
        Add(defaults, "layout", "details_field", "true");
        Add(defaults, "layout", "state_field", "true");

        Add(defaults, "buttons", "button_1_label", "Local time: {time}");
        Add(defaults, "buttons", "button_1_url", "https://time.is/");
        Add(defaults, "buttons", "button_2_label", "Discord Developer Portal");
        Add(defaults, "buttons", "button_2_url", "https://discord.com/developers/applications");

        Add(defaults, "afk", "idle_threshold", "60");
        Add(defaults, "afk", "afk_message", "AFK");
        Add(defaults, "afk", "afk_image", "");
        Add(defaults, "afk", "show_idle_time", "true");
        Add(defaults, "afk", "idle_text", "since {idle_hh}h {idle_mm}m {idle_ss}s");

        Add(defaults, "messages", "rpc_restarted_message", "Rich Presence has been restarted.");

        Add(defaults, "app", "show_tray", "true");
        Add(defaults, "app", "show_menu_as_dropdown", "true");
        Add(defaults, "app", "single_instance", "true");
        Add(defaults, "app", "show_console", "false");
        Add(defaults, "app", "notifications_enabled", "true");
        Add(defaults, "app", "notify_on_start", "false");
        Add(defaults, "app", "notify_on_success", "false");
        Add(defaults, "app", "notify_on_failure", "true");
        Add(defaults, "app", "notify_on_reload", "true");
        Add(defaults, "app", "logging_enabled", "true");
        Add(defaults, "app", "file_logging_enabled", "true");
        Add(defaults, "app", "log_path", "");
        Add(defaults, "app", "backup_config_on_save", "false");
        Add(defaults, "app", "verbose_logging", "false");
        Add(defaults, "app", "verbose_redact_sensitive", "true");
        Add(defaults, "app", "hide_disabled_entries", "false");
        Add(defaults, "app", "dry_run_redact_sensitive", "true");

        Add(defaults, "ipc", "connect_timeout_ms", "250");
        Add(defaults, "ipc", "response_timeout_ms", "5000");

        Add(defaults, "gateway", "system_locale", "en-US");
        Add(defaults, "gateway", "browser_user_agent", DefaultGatewayBrowserUserAgent);
        Add(defaults, "gateway", "browser_version", "32.2.7");
        Add(defaults, "gateway", "os_version", "10.0.26100");
        Add(defaults, "gateway", "release_channel", "stable");
        Add(defaults, "gateway", "client_build_number", "390000");
        Add(defaults, "gateway", "capabilities", "65");
        Add(defaults, "gateway", "connect_timeout_ms", "10000");
        Add(defaults, "gateway", "hello_timeout_ms", "10000");
        Add(defaults, "gateway", "ready_timeout_ms", "30000");
        Add(defaults, "gateway", "send_timeout_ms", "10000");
        Add(defaults, "gateway", "close_timeout_ms", "2000");
        Add(defaults, "gateway", "asset_fetch_timeout_ms", "10000");
        Add(defaults, "gateway", "asset_fetch_user_agent", "DiscordBot (https://github.com, 1)");

        UiStrings.AddDefaultConfigEntries(defaults);
        AddGuiDefaults(defaults);

        return defaults;
    }

    private static void AddTimeRangeDefaults(Dictionary<string, string> target, string section)
    {
        Add(target, section, "morning_start", "5");
        Add(target, section, "morning_end", "12");
        Add(target, section, "afternoon_start", "12");
        Add(target, section, "afternoon_end", "16");
        Add(target, section, "evening_start", "16");
        Add(target, section, "evening_end", "22");
        Add(target, section, "night_start", "22");
        Add(target, section, "night_end", "5");
    }

    private static void AddAssetDefaults(Dictionary<string, string> target, string section)
    {
        Add(target, section, "morning_text", "Morning");
        Add(target, section, "morning_image", "");
        Add(target, section, "afternoon_text", "Afternoon");
        Add(target, section, "afternoon_image", "");
        Add(target, section, "evening_text", "Evening");
        Add(target, section, "evening_image", "");
        Add(target, section, "night_text", "Night");
        Add(target, section, "night_image", "");
        Add(target, section, "default_text", "Online");
        Add(target, section, "default_image", "");
    }

    private static void AddGuiDefaults(Dictionary<string, string> target)
    {
        Add(target, "gui", "fallback_details", "text");
        Add(target, "gui", "transport_mode", "choice");
        Add(target, "gui", "token_env", "text");
        Add(target, "gui", "details_template", "text");
        Add(target, "gui", "state_template", "text");
        Add(target, "gui", "rule_order", "text");
        Add(target, "gui", "full_replace", "list");
        Add(target, "gui", "word_replace", "list");
        Add(target, "gui", "pattern_replace", "list");
        Add(target, "gui", "apply_pattern_on_raw", "bool");
        Add(target, "gui", "morning_start", "number");
        Add(target, "gui", "morning_end", "number");
        Add(target, "gui", "afternoon_start", "number");
        Add(target, "gui", "afternoon_end", "number");
        Add(target, "gui", "evening_start", "number");
        Add(target, "gui", "evening_end", "number");
        Add(target, "gui", "night_start", "number");
        Add(target, "gui", "night_end", "number");
        Add(target, "gui", "morning_text", "text");
        Add(target, "gui", "afternoon_text", "text");
        Add(target, "gui", "evening_text", "text");
        Add(target, "gui", "night_text", "text");
        Add(target, "gui", "default_text", "text");
        Add(target, "gui", "morning_image", "text");
        Add(target, "gui", "afternoon_image", "text");
        Add(target, "gui", "evening_image", "text");
        Add(target, "gui", "night_image", "text");
        Add(target, "gui", "default_image", "text");
        Add(target, "gui", "small_assets.morning_text", "text");
        Add(target, "gui", "small_assets.afternoon_text", "text");
        Add(target, "gui", "small_assets.evening_text", "text");
        Add(target, "gui", "small_assets.night_text", "text");
        Add(target, "gui", "small_assets.default_text", "text");
        Add(target, "gui", "small_assets.morning_image", "text");
        Add(target, "gui", "small_assets.afternoon_image", "text");
        Add(target, "gui", "small_assets.evening_image", "text");
        Add(target, "gui", "small_assets.night_image", "text");
        Add(target, "gui", "small_assets.default_image", "text");
        Add(target, "gui", "show_large_image", "bool");
        Add(target, "gui", "show_small_image", "bool");
        Add(target, "gui", "show_buttons", "bool");
        Add(target, "gui", "details_field", "bool");
        Add(target, "gui", "state_field", "bool");
        Add(target, "gui", "button_1_label", "text");
        Add(target, "gui", "button_1_url", "text");
        Add(target, "gui", "button_2_label", "text");
        Add(target, "gui", "button_2_url", "text");
        Add(target, "gui", "idle_threshold", "number");
        Add(target, "gui", "afk_message", "text");
        Add(target, "gui", "afk_image", "text");
        Add(target, "gui", "show_idle_time", "bool");
        Add(target, "gui", "idle_text", "text");
        Add(target, "gui", "rpc_restarted_message", "text");
        Add(target, "gui", "show_tray", "bool");
        Add(target, "gui", "show_menu_as_dropdown", "bool");
        Add(target, "gui", "single_instance", "bool");
        Add(target, "gui", "show_console", "bool");
        Add(target, "gui", "notifications_enabled", "bool");
        Add(target, "gui", "notify_on_start", "bool");
        Add(target, "gui", "notify_on_success", "bool");
        Add(target, "gui", "notify_on_failure", "bool");
        Add(target, "gui", "notify_on_reload", "bool");
        Add(target, "gui", "logging_enabled", "bool");
        Add(target, "gui", "file_logging_enabled", "bool");
        Add(target, "gui", "log_path", "text");
        Add(target, "gui", "backup_config_on_save", "bool");
        Add(target, "gui", "verbose_logging", "bool");
        Add(target, "gui", "verbose_redact_sensitive", "bool");
        Add(target, "gui", "hide_disabled_entries", "bool");
        Add(target, "gui", "dry_run_redact_sensitive", "bool");
        Add(target, "gui", "connect_timeout_ms", "number");
        Add(target, "gui", "response_timeout_ms", "number");
        Add(target, "gui", "system_locale", "text");
        Add(target, "gui", "browser_user_agent", "text");
        Add(target, "gui", "browser_version", "text");
        Add(target, "gui", "os_version", "text");
        Add(target, "gui", "release_channel", "text");
        Add(target, "gui", "client_build_number", "number");
        Add(target, "gui", "capabilities", "number");
        Add(target, "gui", "gateway.connect_timeout_ms", "number");
        Add(target, "gui", "hello_timeout_ms", "number");
        Add(target, "gui", "ready_timeout_ms", "number");
        Add(target, "gui", "send_timeout_ms", "number");
        Add(target, "gui", "close_timeout_ms", "number");
        Add(target, "gui", "asset_fetch_timeout_ms", "number");
        Add(target, "gui", "asset_fetch_user_agent", "text");
        Add(target, "gui", "save_shortcut", "<Control-s>");
        Add(target, "gui", "exit_shortcut", "<Control-q>");
        Add(target, "gui", "reload_shortcut", "<Control-r>");
    }

    private static string NormalizeSetupText(string value, string fallback)
    {
        string text = (value ?? "").Trim();
        return text.Length == 0 ? fallback : text;
    }

    private static void Add(Dictionary<string, string> target, string section, string key, string value)
    {
        target[IniConfigFile.MakeKey(section, key)] = value ?? "";
    }
}

internal sealed class TrayHost
{
    private readonly Thread thread;
    private TrayApplicationContext context;

    private TrayHost(string configPath)
    {
        thread = new Thread(delegate()
        {
            System.Windows.Forms.Application.EnableVisualStyles();
            System.Windows.Forms.Application.SetCompatibleTextRenderingDefault(false);
            context = new TrayApplicationContext(configPath);
            System.Windows.Forms.Application.Run(context);
        });
        thread.IsBackground = true;
        thread.SetApartmentState(ApartmentState.STA);
    }

    public static TrayHost Start(string configPath)
    {
        TrayHost host = new TrayHost(configPath);
        host.thread.Start();
        return host;
    }

    public void Stop()
    {
        TrayApplicationContext current = context;
        if (current != null)
        {
            current.ExitFromAnyThread();
        }

        if (thread.IsAlive)
        {
            thread.Join(3000);
        }
    }
}

internal sealed class TrayApplicationContext : ApplicationContext
{
    private readonly string configPath;
    private readonly Form owner;
    private readonly NotifyIcon notifyIcon;
    private readonly ContextMenuStrip menu;

    public TrayApplicationContext(string configPath)
    {
        this.configPath = configPath;

        owner = new Form();
        owner.ShowInTaskbar = false;
        owner.FormBorderStyle = FormBorderStyle.FixedToolWindow;
        owner.StartPosition = FormStartPosition.Manual;
        owner.Size = new Size(1, 1);
        owner.Location = new Point(-32000, -32000);
        owner.CreateControl();

        menu = new ContextMenuStrip();
        menu.Opening += delegate(object sender, System.ComponentModel.CancelEventArgs e)
        {
            BuildMenu();
        };

        notifyIcon = new NotifyIcon();
        notifyIcon.Text = "DiscordRPC";
        notifyIcon.Icon = SystemIcons.Application;
        notifyIcon.ContextMenuStrip = menu;
        notifyIcon.Visible = true;
        notifyIcon.MouseDoubleClick += delegate(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left)
            {
                ShowTrayMenu();
            }
        };

        AppState.NotificationRequested += OnNotificationRequested;
        Logger.Info("Tray icon started.");
    }

    public void ExitFromAnyThread()
    {
        try
        {
            if (owner.IsHandleCreated)
            {
                owner.BeginInvoke(new MethodInvoker(ExitThread));
            }
        }
        catch
        {
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            AppState.NotificationRequested -= OnNotificationRequested;
            notifyIcon.Visible = false;
            notifyIcon.Dispose();
            menu.Dispose();
            owner.Dispose();
        }

        base.Dispose(disposing);
    }

    private void BuildMenu()
    {
        menu.Items.Clear();
        IniConfig config = LoadConfigForMenu();
        UiStrings.Load(config);

        bool showDropdown = config.GetBool("app", "show_menu_as_dropdown", true);
        AddToggle(menu.Items, UiStrings.Get("show_menu_as_dropdown"), config, "app", "show_menu_as_dropdown", true, false);
        menu.Items.Add(new ToolStripSeparator());

        ToolStripItemCollection general = AddCategory(UiStrings.Get("category_general"), showDropdown);
        AddToggle(general, UiStrings.Get("enable_window_detection"), config, "general", "window_title_detection_enabled", true, true);
        AddEdit(general, UiStrings.Get("application_id"), config, "general", "client_id", UiStrings.Get("application_id"), false, false);
        AddTransportMode(general, config);
        AddEdit(general, UiStrings.Get("token_env"), config, "general", "token_env", UiStrings.Get("token_env"), false, false);
        AddEdit(general, UiStrings.Get("update_interval"), config, "general", "update_interval", UiStrings.Get("update_interval"), false, true);
        AddEdit(general, UiStrings.Get("idle_message"), config, "general", "idle_message", UiStrings.Get("idle_message"), false, false);
        AddEdit(general, UiStrings.Get("fallback_details"), config, "general", "fallback_details", UiStrings.Get("fallback_details"), false, false);

        ToolStripItemCollection layout = AddCategory(UiStrings.Get("category_presence_layout"), showDropdown);
        AddToggle(layout, UiStrings.Get("details_field"), config, "layout", "details_field", true, true);
        AddEdit(layout, UiStrings.Get("details_template"), config, "general", "details_template", UiStrings.Get("details_template"), false, false);
        AddToggle(layout, UiStrings.Get("state_field"), config, "layout", "state_field", true, true);
        AddEdit(layout, UiStrings.Get("state_template"), config, "general", "state_template", UiStrings.Get("state_template"), false, false);
        AddToggle(layout, UiStrings.Get("large_image"), config, "layout", "show_large_image", true, true);
        AddToggle(layout, UiStrings.Get("small_image"), config, "layout", "show_small_image", true, true);
        AddToggle(layout, UiStrings.Get("buttons"), config, "layout", "show_buttons", true, true);

        ToolStripItemCollection censoring = AddCategory(UiStrings.Get("category_censoring"), showDropdown);
        AddToggle(censoring, UiStrings.Get("apply_regex_raw"), config, "censor_map", "apply_pattern_on_raw", false, true);
        AddEdit(censoring, UiStrings.Get("rule_order"), config, "censor_map", "rule_order", UiStrings.Get("rule_order"), false, false);
        AddEdit(censoring, UiStrings.Get("full_replace_map"), config, "censor_map", "full_replace", UiStrings.Get("full_replace_map"), true, false);
        AddEdit(censoring, UiStrings.Get("word_replace_map"), config, "censor_map", "word_replace", UiStrings.Get("word_replace_map"), true, false);
        AddEdit(censoring, UiStrings.Get("regex_replace_map"), config, "censor_map", "pattern_replace", UiStrings.Get("regex_replace_map"), true, false);

        ToolStripItemCollection assets = AddCategory(UiStrings.Get("category_assets_time"), showDropdown);
        assets.Add(CreateTimeRangeMenu(UiStrings.Get("large_time_ranges"), config, "large_time_ranges"));
        assets.Add(CreateTimeRangeMenu(UiStrings.Get("small_time_ranges"), config, "small_time_ranges"));
        assets.Add(CreateAssetMenu(UiStrings.Get("large_assets"), config, "large_assets"));
        assets.Add(CreateAssetMenu(UiStrings.Get("small_assets"), config, "small_assets"));

        ToolStripItemCollection buttons = AddCategory(UiStrings.Get("category_buttons"), showDropdown);
        AddEdit(buttons, UiStrings.Get("button_1_label"), config, "buttons", "button_1_label", UiStrings.Get("button_1_label"), false, false);
        AddEdit(buttons, UiStrings.Get("button_1_url"), config, "buttons", "button_1_url", UiStrings.Get("button_1_url"), false, false);
        AddEdit(buttons, UiStrings.Get("button_2_label"), config, "buttons", "button_2_label", UiStrings.Get("button_2_label"), false, false);
        AddEdit(buttons, UiStrings.Get("button_2_url"), config, "buttons", "button_2_url", UiStrings.Get("button_2_url"), false, false);

        ToolStripItemCollection afk = AddCategory(UiStrings.Get("category_afk"), showDropdown);
        AddEdit(afk, UiStrings.Get("idle_threshold"), config, "afk", "idle_threshold", UiStrings.Get("idle_threshold"), false, true);
        AddEdit(afk, UiStrings.Get("afk_message"), config, "afk", "afk_message", UiStrings.Get("afk_message"), false, false);
        AddEdit(afk, UiStrings.Get("afk_image"), config, "afk", "afk_image", UiStrings.Get("afk_image"), false, false);
        AddToggle(afk, UiStrings.Get("show_idle_time"), config, "afk", "show_idle_time", true, true);
        AddEdit(afk, UiStrings.Get("idle_text"), config, "afk", "idle_text", UiStrings.Get("idle_text"), false, false);

        ToolStripItemCollection notifications = AddCategory(UiStrings.Get("category_notifications"), showDropdown);
        AddToggle(notifications, UiStrings.Get("enable_notifications"), config, "app", "notifications_enabled", true, false);
        bool notificationsEnabled = config.GetBool("app", "notifications_enabled", true);
        AddToggle(notifications, UiStrings.Get("notify_on_start"), config, "app", "notify_on_start", false, false, notificationsEnabled);
        AddToggle(notifications, UiStrings.Get("notify_on_success"), config, "app", "notify_on_success", false, false, notificationsEnabled);
        AddToggle(notifications, UiStrings.Get("notify_on_failure"), config, "app", "notify_on_failure", true, false, notificationsEnabled);
        AddToggle(notifications, UiStrings.Get("notify_on_reload"), config, "app", "notify_on_reload", true, false, notificationsEnabled);

        ToolStripItemCollection logging = AddCategory(UiStrings.Get("category_logging"), showDropdown);
        AddToggle(logging, UiStrings.Get("enable_logging"), config, "app", "logging_enabled", true, false);
        AddToggle(logging, UiStrings.Get("file_logging"), config, "app", "file_logging_enabled", true, false);
        AddToggle(logging, UiStrings.Get("verbose_ipc_logging"), config, "app", "verbose_logging", false, true, config.GetBool("app", "logging_enabled", true));
        AddToggle(logging, UiStrings.Get("redact_verbose_log"), config, "app", "verbose_redact_sensitive", true, false, config.GetBool("app", "verbose_logging", false));
        AddConsoleToggle(logging);
        AddStatus(logging, UiStrings.Format("console_state_format", ConsoleWindow.IsVisible() ? UiStrings.Get("enabled_text") : UiStrings.Get("disabled_text")));
        AddEdit(logging, UiStrings.Get("log_path"), config, "app", "log_path", UiStrings.Get("log_path"), false, false);
        AddOpenLogFolder(logging);
        AddRecentLog(logging);

        ToolStripItemCollection methods = AddCategory(UiStrings.Get("category_methods"), showDropdown);
        AddEdit(methods, UiStrings.Get("ipc_connect_timeout"), config, "ipc", "connect_timeout_ms", UiStrings.Get("ipc_connect_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("ipc_response_timeout"), config, "ipc", "response_timeout_ms", UiStrings.Get("ipc_response_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_connect_timeout"), config, "gateway", "connect_timeout_ms", UiStrings.Get("gateway_connect_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_hello_timeout"), config, "gateway", "hello_timeout_ms", UiStrings.Get("gateway_hello_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_ready_timeout"), config, "gateway", "ready_timeout_ms", UiStrings.Get("gateway_ready_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_send_timeout"), config, "gateway", "send_timeout_ms", UiStrings.Get("gateway_send_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_close_timeout"), config, "gateway", "close_timeout_ms", UiStrings.Get("gateway_close_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_asset_fetch_timeout"), config, "gateway", "asset_fetch_timeout_ms", UiStrings.Get("gateway_asset_fetch_timeout"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_system_locale"), config, "gateway", "system_locale", UiStrings.Get("gateway_system_locale"), false, false);
        AddEdit(methods, UiStrings.Get("gateway_browser_user_agent"), config, "gateway", "browser_user_agent", UiStrings.Get("gateway_browser_user_agent"), false, false);
        AddEdit(methods, UiStrings.Get("gateway_browser_version"), config, "gateway", "browser_version", UiStrings.Get("gateway_browser_version"), false, false);
        AddEdit(methods, UiStrings.Get("gateway_os_version"), config, "gateway", "os_version", UiStrings.Get("gateway_os_version"), false, false);
        AddEdit(methods, UiStrings.Get("gateway_release_channel"), config, "gateway", "release_channel", UiStrings.Get("gateway_release_channel"), false, false);
        AddEdit(methods, UiStrings.Get("gateway_client_build_number"), config, "gateway", "client_build_number", UiStrings.Get("gateway_client_build_number"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_capabilities"), config, "gateway", "capabilities", UiStrings.Get("gateway_capabilities"), false, true);
        AddEdit(methods, UiStrings.Get("gateway_asset_fetch_user_agent"), config, "gateway", "asset_fetch_user_agent", UiStrings.Get("gateway_asset_fetch_user_agent"), false, false);

        ToolStripItemCollection advanced = AddCategory(UiStrings.Get("category_advanced"), showDropdown);
        AddToggle(advanced, UiStrings.Get("tray_icon"), config, "app", "show_tray", true, false);
        AddToggle(advanced, UiStrings.Get("single_instance"), config, "app", "single_instance", true, false);
        AddToggle(advanced, UiStrings.Get("hide_disabled_entries"), config, "app", "hide_disabled_entries", false, false);
        AddToggle(advanced, UiStrings.Get("redact_dry_run"), config, "app", "dry_run_redact_sensitive", true, false);
        AddToggle(advanced, UiStrings.Get("backup_config_on_save"), config, "app", "backup_config_on_save", false, false);
        AddEdit(advanced, UiStrings.Get("restart_message"), config, "messages", "rpc_restarted_message", UiStrings.Get("restart_message"), false, false);
        AddStatus(advanced, UiStrings.Format("config_path_format", TrimForMenu(configPath, 120)));
        ToolStripMenuItem reload = new ToolStripMenuItem(UiStrings.Get("reload_configuration"));
        reload.Click += delegate { AppState.RequestReload(); };
        advanced.Add(reload);

        menu.Items.Add(new ToolStripSeparator());

        ToolStripMenuItem exit = new ToolStripMenuItem(UiStrings.Get("exit"));
        exit.Click += delegate
        {
            Program.RequestStop();
            ExitThread();
        };
        menu.Items.Add(exit);
    }

    private void ShowTrayMenu()
    {
        BuildMenu();
        menu.Show(Cursor.Position);
    }

    private IniConfig LoadConfigForMenu()
    {
        try
        {
            return ConfigDefaults.EnsureAndReload(configPath, IniConfig.Load(configPath));
        }
        catch (Exception ex)
        {
            Logger.Error("Failed to load tray menu config: " + ex.Message);
            IniConfig fallback = new IniConfig();
            UiStrings.Load(fallback);
            return fallback;
        }
    }

    private ToolStripItemCollection AddCategory(string text, bool showDropdown)
    {
        if (showDropdown)
        {
            ToolStripMenuItem parent = new ToolStripMenuItem(text);
            AddHeader(parent.DropDownItems, HeaderText(text));
            menu.Items.Add(parent);
            return parent.DropDownItems;
        }

        if (menu.Items.Count > 0 && !(menu.Items[menu.Items.Count - 1] is ToolStripSeparator))
        {
            menu.Items.Add(new ToolStripSeparator());
        }
        AddHeader(menu.Items, HeaderText(text));
        return menu.Items;
    }

    private void AddHeader(ToolStripItemCollection items, string text)
    {
        ToolStripMenuItem header = new ToolStripMenuItem(text);
        header.Enabled = false;
        items.Add(header);
    }

    private void AddStatus(ToolStripItemCollection items, string text)
    {
        ToolStripMenuItem status = new ToolStripMenuItem(text);
        status.Enabled = false;
        items.Add(status);
    }

    private void AddToggle(ToolStripItemCollection items, string text, IniConfig config, string section, string key, bool defaultValue, bool reload, bool enabled)
    {
        if (!enabled && config.GetBool("app", "hide_disabled_entries", false))
        {
            return;
        }

        bool current = config.GetBool(section, key, defaultValue);
        ToolStripMenuItem item = new ToolStripMenuItem(text);
        item.Checked = current;
        item.Enabled = enabled;
        item.Click += delegate
        {
            bool next = !current;
            SaveValue(section, key, next ? "true" : "false", reload);
            if (section.Equals("app", StringComparison.OrdinalIgnoreCase))
            {
                AppState.ApplyConfig(IniConfig.Load(configPath));
            }
            if (section.Equals("app", StringComparison.OrdinalIgnoreCase) && key.Equals("show_console", StringComparison.OrdinalIgnoreCase))
            {
                ConsoleWindow.SetVisible(next, false);
            }
        };
        items.Add(item);
    }

    private void AddToggle(ToolStripItemCollection items, string text, IniConfig config, string section, string key, bool defaultValue, bool reload)
    {
        AddToggle(items, text, config, section, key, defaultValue, reload, true);
    }

    private void AddTransportMode(ToolStripItemCollection items, IniConfig config)
    {
        string current = Program.NormalizeTransportMode(config.Get("general", "transport_mode", "ipc"));
        AddStatus(items, UiStrings.Format("current_value_format", current));

        ToolStripMenuItem parent = new ToolStripMenuItem(UiStrings.Get("transport_mode"));
        AddTransportModeOption(parent.DropDownItems, UiStrings.Get("transport_mode_gateway"), "gateway", current);
        AddTransportModeOption(parent.DropDownItems, UiStrings.Get("transport_mode_ipc"), "ipc", current);
        AddTransportModeOption(parent.DropDownItems, UiStrings.Get("transport_mode_auto"), "auto", current);
        items.Add(parent);
    }

    private void AddTransportModeOption(ToolStripItemCollection items, string text, string value, string current)
    {
        ToolStripMenuItem item = new ToolStripMenuItem(text);
        item.Checked = value.Equals(current, StringComparison.OrdinalIgnoreCase);
        item.Click += delegate
        {
            SaveValue("general", "transport_mode", value, true);
        };
        items.Add(item);
    }

    private void AddEdit(ToolStripItemCollection items, string text, IniConfig config, string section, string key, string title, bool multiline, bool numeric)
    {
        AddCurrentValue(items, config, section, key, multiline);

        ToolStripMenuItem item = new ToolStripMenuItem(UiStrings.Format("change_menu_format", text));
        item.Click += delegate
        {
            string value = config.Get(section, key, "");
            string edited;
            if (!InputDialog.TryGetValue(owner, UiStrings.Format("dialog_title_format", title), LabelFor(section, key), value, multiline, out edited))
            {
                return;
            }

            if (numeric)
            {
                int parsed;
                if (!int.TryParse(edited.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) || parsed < 0)
                {
                    MessageBox.Show(owner, UiStrings.Get("number_validation"), title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                edited = parsed.ToString(CultureInfo.InvariantCulture);
            }

            SaveValue(section, key, edited, true);
        };
        items.Add(item);
    }

    private ToolStripMenuItem CreateTimeRangeMenu(string title, IniConfig config, string section)
    {
        ToolStripMenuItem parent = new ToolStripMenuItem(title);
        string[] names = new string[] { "morning", "afternoon", "evening", "night" };
        for (int i = 0; i < names.Length; i++)
        {
            string startKey = names[i] + "_start";
            string endKey = names[i] + "_end";
            AddEdit(parent.DropDownItems, PrettyName(startKey), config, section, startKey, PrettyName(startKey), false, true);
            AddEdit(parent.DropDownItems, PrettyName(endKey), config, section, endKey, PrettyName(endKey), false, true);
            if (i != names.Length - 1)
            {
                parent.DropDownItems.Add(new ToolStripSeparator());
            }
        }
        return parent;
    }

    private ToolStripMenuItem CreateAssetMenu(string title, IniConfig config, string section)
    {
        ToolStripMenuItem parent = new ToolStripMenuItem(title);
        string[] slots = new string[] { "morning", "afternoon", "evening", "night", "default" };
        for (int i = 0; i < slots.Length; i++)
        {
            string textKey = slots[i] + "_text";
            string imageKey = slots[i] + "_image";
            AddEdit(parent.DropDownItems, PrettyName(textKey), config, section, textKey, PrettyName(textKey), false, false);
            AddEdit(parent.DropDownItems, PrettyName(imageKey), config, section, imageKey, PrettyName(imageKey), false, false);
            if (i != slots.Length - 1)
            {
                parent.DropDownItems.Add(new ToolStripSeparator());
            }
        }
        return parent;
    }

    private void AddConsoleToggle(ToolStripItemCollection items)
    {
        ToolStripMenuItem console = new ToolStripMenuItem(UiStrings.Get("show_console"));
        console.Checked = ConsoleWindow.IsVisible();
        console.Click += delegate
        {
            bool next = !ConsoleWindow.IsVisible();
            ConsoleWindow.SetVisible(next, false);
            SaveValue("app", "show_console", next ? "true" : "false", false);
        };
        items.Add(console);
    }

    private void AddRecentLog(ToolStripItemCollection items)
    {
        ToolStripMenuItem recentLog = new ToolStripMenuItem(UiStrings.Get("show_recent_log"));
        recentLog.Click += delegate
        {
            MessageBox.Show(owner, Logger.GetRecentText(), UiStrings.Get("recent_log_title"), MessageBoxButtons.OK, MessageBoxIcon.Information);
        };
        items.Add(recentLog);
    }

    private void AddOpenLogFolder(ToolStripItemCollection items)
    {
        ToolStripMenuItem openLogFolder = new ToolStripMenuItem(UiStrings.Get("open_log_folder"));
        openLogFolder.Click += delegate
        {
            string logPath = Logger.FilePath;
            if (logPath.Length == 0)
            {
                return;
            }

            try
            {
                string fullPath = Path.GetFullPath(logPath);
                string args = File.Exists(fullPath)
                    ? "/select,\"" + fullPath + "\""
                    : "\"" + (Path.GetDirectoryName(fullPath) ?? ".") + "\"";
                Process.Start("explorer.exe", args);
            }
            catch (Exception ex)
            {
                MessageBox.Show(owner, ex.Message, UiStrings.Get("config_update_error_title"), MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        };
        items.Add(openLogFolder);
    }

    private void SaveValue(string section, string key, string value, bool reload)
    {
        try
        {
            if (ShouldWriteConfigBackup())
            {
                File.Copy(configPath, configPath + ".bak", true);
            }
            IniConfigFile.WriteValue(configPath, section, key, value);
            Logger.Info("Config updated: [" + section + "] " + key);
            if (reload)
            {
                AppState.RequestReload();
            }
        }
        catch (Exception ex)
        {
            Logger.Error("Failed to update config: " + ex.Message);
            MessageBox.Show(owner, UiStrings.Format("config_update_error_format", ex.Message), UiStrings.Get("config_update_error_title"), MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private bool ShouldWriteConfigBackup()
    {
        try
        {
            return IniConfig.Load(configPath).GetBool("app", "backup_config_on_save", false);
        }
        catch
        {
            return false;
        }
    }

    private string LabelFor(string section, string key)
    {
        return UiStrings.Format("dialog_label_format", section, key);
    }

    private void AddCurrentValue(ToolStripItemCollection items, IniConfig config, string section, string key, bool multiline)
    {
        string value = config.Get(section, key, "");
        string summary = multiline ? SummarizeMultiline(value) : SummarizeSingleLine(value);
        AddStatus(items, UiStrings.Format("current_value_format", summary));
    }

    private string SummarizeSingleLine(string value)
    {
        if (value == null || value.Trim().Length == 0)
        {
            return UiStrings.Get("empty_value");
        }

        return TrimForMenu(value, 90);
    }

    private string SummarizeMultiline(string value)
    {
        if (value == null || value.Trim().Length == 0)
        {
            return UiStrings.Get("empty_value");
        }

        string normalized = value.Replace("\r\n", "\n").Replace('\r', '\n');
        string[] lines = normalized.Split('\n');
        int count = 0;
        string first = "";
        for (int i = 0; i < lines.Length; i++)
        {
            string line = lines[i].Trim();
            if (line.Length == 0)
            {
                continue;
            }
            count++;
            if (first.Length == 0)
            {
                first = line;
            }
        }

        if (count == 0)
        {
            return UiStrings.Get("empty_value");
        }
        if (first.Length == 0)
        {
            return UiStrings.Format("multiline_entries_format", count);
        }

        return UiStrings.Format("multiline_preview_format", count, TrimForMenu(first, 60));
    }

    private string HeaderText(string text)
    {
        string trimmed = (text ?? "").Trim();
        if (trimmed.EndsWith(":", StringComparison.Ordinal))
        {
            return trimmed;
        }

        return trimmed + ":";
    }

    private string TrimForMenu(string value, int maxLength)
    {
        if (value == null)
        {
            return "";
        }

        string text = value.Replace("\r\n", " ").Replace('\r', ' ').Replace('\n', ' ').Trim();
        while (text.IndexOf("  ", StringComparison.Ordinal) >= 0)
        {
            text = text.Replace("  ", " ");
        }

        if (maxLength > 3 && text.Length > maxLength)
        {
            return text.Substring(0, maxLength - 3) + "...";
        }

        return text;
    }

    private string PrettyName(string key)
    {
        string[] parts = key.Split('_');
        for (int i = 0; i < parts.Length; i++)
        {
            if (parts[i].Length > 0)
            {
                parts[i] = char.ToUpperInvariant(parts[i][0]) + parts[i].Substring(1);
            }
        }
        return string.Join(" ", parts);
    }

    private void OnNotificationRequested(string title, string message, ToolTipIcon icon)
    {
        try
        {
            if (owner.IsHandleCreated)
            {
                owner.BeginInvoke(new MethodInvoker(delegate
                {
                    ShowBalloon(title, message, icon);
                }));
            }
        }
        catch
        {
        }
    }

    private void ShowBalloon(string title, string message, ToolTipIcon icon)
    {
        if (!notifyIcon.Visible)
        {
            return;
        }

        notifyIcon.BalloonTipTitle = title;
        notifyIcon.BalloonTipText = message;
        notifyIcon.BalloonTipIcon = icon;
        notifyIcon.ShowBalloonTip(4000);
    }
}

internal sealed class SetupDialog : Form
{
    private readonly TextBox tokenBox;
    private readonly TextBox clientIdBox;
    private readonly Label tokenLabel;
    private readonly Label modeHelp;
    private readonly RadioButton ipcMode;
    private readonly RadioButton autoMode;
    private readonly RadioButton gatewayMode;
    private readonly CheckBox windowDetectionBox;
    private readonly Label staticDetailsLabel;
    private readonly TextBox staticDetailsBox;
    private readonly CheckBox systemStatsBox;
    private readonly Label staticStateLabel;
    private readonly TextBox staticStateBox;

    private SetupDialog(string existingToken, string existingClientId, string initialTransportMode)
    {
        Text              = "DiscordRPC — Setup";
        StartPosition     = FormStartPosition.CenterScreen;
        FormBorderStyle   = FormBorderStyle.FixedDialog;
        MinimizeBox       = false;
        MaximizeBox       = false;
        ShowInTaskbar     = true;
        Width             = 660;
        Height            = 660;

        int x = 12, w = ClientSize.Width - 24, y = 14;

        Label heading = new Label();
        heading.Text   = "Welcome to DiscordRPC!";
        heading.Font   = new Font(Font.FontFamily, Font.Size + 2, FontStyle.Bold);
        heading.Left   = x; heading.Top = y;
        heading.Width  = w; heading.Height = 22;
        heading.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(heading);
        y += 26;

        Label intro = new Label();
        intro.Text   = "Select your preferred working mode, then enter the Discord Application ID required for Rich Presence.";
        intro.Left   = x; intro.Top = y;
        intro.Width  = w; intro.Height = 34;
        intro.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(intro);
        y += 42;

        GroupBox modeGroup = new GroupBox();
        modeGroup.Text = "Working mode";
        modeGroup.Left = x; modeGroup.Top = y;
        modeGroup.Width = w; modeGroup.Height = 112;
        modeGroup.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(modeGroup);

        ipcMode = new RadioButton();
        ipcMode.Text = "Discord IPC (recommended) - standard mode, uses the Discord desktop app";
        ipcMode.Left = 12; ipcMode.Top = 22;
        ipcMode.Width = modeGroup.ClientSize.Width - 24; ipcMode.Height = 20;
        ipcMode.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        modeGroup.Controls.Add(ipcMode);

        autoMode = new RadioButton();
        autoMode.Text = "Auto - try IPC first, then use Gateway if a token is saved";
        autoMode.Left = 12; autoMode.Top = 48;
        autoMode.Width = modeGroup.ClientSize.Width - 24; autoMode.Height = 20;
        autoMode.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        modeGroup.Controls.Add(autoMode);

        gatewayMode = new RadioButton();
        gatewayMode.Text = "Gateway only - works without Discord desktop, requires a token";
        gatewayMode.Left = 12; gatewayMode.Top = 74;
        gatewayMode.Width = modeGroup.ClientSize.Width - 24; gatewayMode.Height = 20;
        gatewayMode.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        modeGroup.Controls.Add(gatewayMode);

        modeHelp = new Label();
        modeHelp.Left = x; modeHelp.Top = y + modeGroup.Height + 8;
        modeHelp.Width = w; modeHelp.Height = 34;
        modeHelp.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(modeHelp);
        y += modeGroup.Height + 50;

        GroupBox presenceGroup = new GroupBox();
        presenceGroup.Text = "Presence content";
        presenceGroup.Left = x; presenceGroup.Top = y;
        presenceGroup.Width = w; presenceGroup.Height = 136;
        presenceGroup.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(presenceGroup);

        windowDetectionBox = new CheckBox();
        windowDetectionBox.Text = "Details: show the active window title";
        windowDetectionBox.Left = 12; windowDetectionBox.Top = 22;
        windowDetectionBox.Width = presenceGroup.ClientSize.Width - 24; windowDetectionBox.Height = 20;
        windowDetectionBox.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        windowDetectionBox.Checked = true;
        presenceGroup.Controls.Add(windowDetectionBox);

        staticDetailsLabel = new Label();
        staticDetailsLabel.Text = "Static details:";
        staticDetailsLabel.Left = 32; staticDetailsLabel.Top = 50;
        staticDetailsLabel.Width = 88; staticDetailsLabel.Height = 18;
        presenceGroup.Controls.Add(staticDetailsLabel);

        staticDetailsBox = new TextBox();
        staticDetailsBox.Left = 126; staticDetailsBox.Top = 46;
        staticDetailsBox.Width = presenceGroup.ClientSize.Width - 138; staticDetailsBox.Height = 24;
        staticDetailsBox.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        staticDetailsBox.Text = ConfigDefaults.DefaultStaticDetails;
        presenceGroup.Controls.Add(staticDetailsBox);

        systemStatsBox = new CheckBox();
        systemStatsBox.Text = "State: show CPU and RAM usage";
        systemStatsBox.Left = 12; systemStatsBox.Top = 78;
        systemStatsBox.Width = presenceGroup.ClientSize.Width - 24; systemStatsBox.Height = 20;
        systemStatsBox.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        systemStatsBox.Checked = true;
        presenceGroup.Controls.Add(systemStatsBox);

        staticStateLabel = new Label();
        staticStateLabel.Text = "Static state:";
        staticStateLabel.Left = 32; staticStateLabel.Top = 106;
        staticStateLabel.Width = 88; staticStateLabel.Height = 18;
        presenceGroup.Controls.Add(staticStateLabel);

        staticStateBox = new TextBox();
        staticStateBox.Left = 126; staticStateBox.Top = 102;
        staticStateBox.Width = presenceGroup.ClientSize.Width - 138; staticStateBox.Height = 24;
        staticStateBox.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        staticStateBox.Text = ConfigDefaults.DefaultStaticState;
        presenceGroup.Controls.Add(staticStateBox);

        EventHandler presenceChanged = delegate { UpdatePresenceUi(); };
        windowDetectionBox.CheckedChanged += presenceChanged;
        systemStatsBox.CheckedChanged += presenceChanged;
        y += presenceGroup.Height + 12;

        // ---- Application ID row ----
        Label clientLabel = new Label();
        clientLabel.Text   = "Application ID\r\n(required):";
        clientLabel.Left   = x; clientLabel.Top = y + 3;
        clientLabel.Width  = 120; clientLabel.Height = 34;
        Controls.Add(clientLabel);

        clientIdBox = new TextBox();
        clientIdBox.Left   = x + 126;
        clientIdBox.Top    = y;
        clientIdBox.Width  = w - 126;
        clientIdBox.Height = 24;
        clientIdBox.Text   = existingClientId ?? "";
        clientIdBox.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(clientIdBox);
        y += 30;

        Label appHint = new Label();
        appHint.Text = "Create an app, then copy its Application ID. Required for IPC, Auto, and Gateway.";
        appHint.Left = x + 126; appHint.Top = y;
        appHint.Width = w - 126; appHint.Height = 18;
        appHint.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(appHint);
        y += 20;

        LinkLabel developerLink = new LinkLabel();
        developerLink.Text = "Open Discord Developer Portal";
        developerLink.Left = x + 126; developerLink.Top = y;
        developerLink.Width = 220; developerLink.Height = 18;
        developerLink.LinkClicked += delegate
        {
            try
            {
                Process.Start("https://discord.com/developers/applications");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this,
                    "Could not open the Developer Portal:\n" + ex.Message,
                    "DiscordRPC - Setup",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
        };
        Controls.Add(developerLink);
        y += 26;

        // ---- Token row ----
        tokenLabel = new Label();
        tokenLabel.Left   = x; tokenLabel.Top = y + 3;
        tokenLabel.Width  = 120; tokenLabel.Height = 34;
        Controls.Add(tokenLabel);

        tokenBox = new TextBox();
        tokenBox.Left                = x + 126;
        tokenBox.Top                 = y;
        tokenBox.Width               = w - 126;
        tokenBox.Height              = 24;
        tokenBox.UseSystemPasswordChar = true;
        tokenBox.Text                = existingToken ?? "";
        tokenBox.Anchor              = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(tokenBox);
        y += 30;

        CheckBox showTok = new CheckBox();
        showTok.Text   = "Show token";
        showTok.Left   = x + 126; showTok.Top = y;
        showTok.Width  = 120; showTok.Height = 20;
        showTok.CheckedChanged += (s, e) => { tokenBox.UseSystemPasswordChar = !showTok.Checked; };
        Controls.Add(showTok);

        y += 24;

        Label tokenHint = new Label();
        tokenHint.Text = "Direct tokens are saved with DPAPI.\r\nUse env:VARIABLE_NAME to keep one in the environment.";
        tokenHint.Left = x + 126; tokenHint.Top = y;
        tokenHint.Width = w - 126; tokenHint.Height = 36;
        tokenHint.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(tokenHint);
        y += 44;

        // ---- Buttons ----
        Button save = new Button();
        save.Text          = "Save && Start";
        save.Width         = 110; save.Height = 28;
        save.Left          = ClientSize.Width - 228;
        save.Top           = ClientSize.Height - 44;
        save.Anchor        = AnchorStyles.Right | AnchorStyles.Bottom;
        save.DialogResult  = DialogResult.OK;
        Controls.Add(save);
        AcceptButton = save;

        Button cancel = new Button();
        cancel.Text         = "Cancel";
        cancel.Width        = 82; cancel.Height = 28;
        cancel.Left         = ClientSize.Width - 106;
        cancel.Top          = ClientSize.Height - 44;
        cancel.Anchor       = AnchorStyles.Right | AnchorStyles.Bottom;
        cancel.DialogResult = DialogResult.Cancel;
        Controls.Add(cancel);
        CancelButton = cancel;

        string mode = Program.NormalizeTransportMode(initialTransportMode);
        if (mode == "gateway")
        {
            gatewayMode.Checked = true;
        }
        else if (mode == "auto")
        {
            autoMode.Checked = true;
        }
        else
        {
            ipcMode.Checked = true;
        }

        EventHandler modeChanged = delegate { UpdateModeUi(); };
        ipcMode.CheckedChanged += modeChanged;
        autoMode.CheckedChanged += modeChanged;
        gatewayMode.CheckedChanged += modeChanged;
        UpdateModeUi();
        UpdatePresenceUi();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (DialogResult == DialogResult.OK && clientIdBox.Text.Trim().Length == 0)
        {
            MessageBox.Show(this,
                "Please enter your Discord application ID before continuing.",
                "DiscordRPC — Setup",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            e.Cancel = true;
            return;
        }

        string tokenText = tokenBox.Text.Trim();
        if (DialogResult == DialogResult.OK && IsGatewaySelected() && tokenText.Length == 0)
        {
            MessageBox.Show(this,
                "Please enter a Discord token or env:VARIABLE_NAME before continuing in Gateway mode.",
                "DiscordRPC — Setup",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            e.Cancel = true;
            return;
        }
        base.OnFormClosing(e);
    }

    private void UpdateModeUi()
    {
        if (gatewayMode.Checked)
        {
            modeHelp.Text = "Gateway still requires an Application ID and a Discord token.";
            tokenLabel.Text = "Token:";
        }
        else if (autoMode.Checked)
        {
            modeHelp.Text = "Auto still requires an Application ID. It uses IPC first, then falls back to Gateway if a token is configured.";
            tokenLabel.Text = "Token\r\n(optional):";
        }
        else
        {
            modeHelp.Text = "IPC is recommended and still requires an Application ID. It does not need your Discord account token.";
            tokenLabel.Text = "Token\r\n(optional):";
        }
    }

    private void UpdatePresenceUi()
    {
        bool detailsStatic = !windowDetectionBox.Checked;
        staticDetailsLabel.Enabled = detailsStatic;
        staticDetailsBox.Enabled = detailsStatic;

        bool stateStatic = !systemStatsBox.Checked;
        staticStateLabel.Enabled = stateStatic;
        staticStateBox.Enabled = stateStatic;
    }

    private bool IsGatewaySelected()
    {
        return gatewayMode.Checked;
    }

    private bool UseWindowDetection()
    {
        return windowDetectionBox.Checked;
    }

    private bool UseSystemStats()
    {
        return systemStatsBox.Checked;
    }

    private string SelectedTransportMode()
    {
        if (gatewayMode.Checked)
        {
            return "gateway";
        }
        if (autoMode.Checked)
        {
            return "auto";
        }
        return "ipc";
    }

    /// <summary>
    /// Shows the setup dialog, writes client_id and optional token to
    /// <paramref name="configPath"/>, then returns the entered token.
    /// Returns false if the user cancelled.
    /// </summary>
    public static bool TrySetup(string configPath, string existingToken, string existingClientId, string initialTransportMode, out string token)
    {
        using (SetupDialog dlg = new SetupDialog(existingToken, existingClientId, initialTransportMode))
        {
            if (dlg.ShowDialog() != DialogResult.OK)
            {
                token = null;
                return false;
            }

            token = dlg.tokenBox.Text.Trim();
            string clientId = dlg.clientIdBox.Text.Trim();
            string transportMode = dlg.SelectedTransportMode();

            Dictionary<string, string> updates = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            updates[IniConfigFile.MakeKey("general", "client_id")] = clientId;
            updates[IniConfigFile.MakeKey("general", "transport_mode")] = transportMode;
            updates[IniConfigFile.MakeKey("general", "token_env")] = "";
            if (Program.IsTokenEnvironmentReference(token))
            {
                updates[IniConfigFile.MakeKey("general", "token")] = token;
                updates[IniConfigFile.MakeKey("general", "token_protected")] = "";
            }
            else if (token.Length > 0)
            {
                updates[IniConfigFile.MakeKey("general", "token")] = "";
                updates[IniConfigFile.MakeKey("general", "token_protected")] = ProtectedSecrets.Protect(token);
            }
            else
            {
                updates[IniConfigFile.MakeKey("general", "token")] = "";
                updates[IniConfigFile.MakeKey("general", "token_protected")] = "";
            }

            Dictionary<string, string> presenceUpdates = ConfigDefaults.CreateSetupPresenceUpdates(
                dlg.UseWindowDetection(),
                dlg.staticDetailsBox.Text,
                dlg.UseSystemStats(),
                dlg.staticStateBox.Text);
            foreach (KeyValuePair<string, string> presenceUpdate in presenceUpdates)
            {
                updates[presenceUpdate.Key] = presenceUpdate.Value;
            }

            try
            {
                IniConfigFile.WriteValues(configPath, updates);
            }
            catch (Exception ex)
            {
                MessageBox.Show(null,
                    "Could not save config:\n" + ex.Message,
                    "DiscordRPC — Setup",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                token = null;
                return false;
            }

            return true;
        }
    }
}

internal sealed class InputDialog : Form
{
    private readonly TextBox textBox;
    private string result;

    private InputDialog(string title, string label, string initialValue, bool multiline)
    {
        Text = title;
        StartPosition = FormStartPosition.CenterParent;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MinimizeBox = false;
        MaximizeBox = false;
        ShowInTaskbar = false;
        Width = multiline ? 560 : 460;
        Height = multiline ? 360 : 160;

        Label prompt = new Label();
        prompt.Text = label;
        prompt.Left = 12;
        prompt.Top = 12;
        prompt.Width = ClientSize.Width - 24;
        prompt.Height = 22;
        prompt.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
        Controls.Add(prompt);

        textBox = new TextBox();
        textBox.Left = 12;
        textBox.Top = 40;
        textBox.Width = ClientSize.Width - 24;
        textBox.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right | (multiline ? AnchorStyles.Bottom : 0);
        textBox.Text = (initialValue ?? "").Replace("\r\n", "\n").Replace("\n", Environment.NewLine);
        if (multiline)
        {
            textBox.Multiline = true;
            textBox.AcceptsReturn = true;
            textBox.AcceptsTab = true;
            textBox.ScrollBars = ScrollBars.Both;
            textBox.WordWrap = false;
            textBox.Height = ClientSize.Height - 92;
        }
        else
        {
            textBox.Height = 24;
        }
        Controls.Add(textBox);

        Button ok = new Button();
        ok.Text = UiStrings.Get("ok");
        ok.Width = 82;
        ok.Height = 26;
        ok.Left = ClientSize.Width - 184;
        ok.Top = ClientSize.Height - 38;
        ok.Anchor = AnchorStyles.Right | AnchorStyles.Bottom;
        ok.DialogResult = DialogResult.OK;
        Controls.Add(ok);

        Button cancel = new Button();
        cancel.Text = UiStrings.Get("cancel");
        cancel.Width = 82;
        cancel.Height = 26;
        cancel.Left = ClientSize.Width - 94;
        cancel.Top = ClientSize.Height - 38;
        cancel.Anchor = AnchorStyles.Right | AnchorStyles.Bottom;
        cancel.DialogResult = DialogResult.Cancel;
        Controls.Add(cancel);

        AcceptButton = ok;
        CancelButton = cancel;
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (DialogResult == DialogResult.OK)
        {
            result = textBox.Text.Replace("\r\n", "\n").TrimEnd();
        }
        base.OnFormClosing(e);
    }

    public static bool TryGetValue(IWin32Window owner, string title, string label, string initialValue, bool multiline, out string value)
    {
        using (InputDialog dialog = new InputDialog(title, label, initialValue, multiline))
        {
            if (dialog.ShowDialog(owner) == DialogResult.OK)
            {
                value = dialog.result ?? "";
                return true;
            }
        }

        value = null;
        return false;
    }
}

internal static class IniConfigFile
{
    public static string MakeKey(string section, string key)
    {
        return section + "\u001f" + key;
    }

    public static void SplitKey(string combined, out string section, out string key)
    {
        int index = combined.IndexOf('\u001f');
        if (index < 0)
        {
            section = "";
            key = combined;
            return;
        }

        section = combined.Substring(0, index);
        key = combined.Substring(index + 1);
    }

    public static void WriteValue(string path, string section, string key, string value)
    {
        Dictionary<string, string> updates = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        updates[MakeKey(section, key)] = value;
        WriteValues(path, updates);
    }

    public static void WriteValues(string path, IDictionary<string, string> updates)
    {
        TextFileData file = ReadTextFile(path);
        string[] lines = file.Lines;
        List<string> output = new List<string>();
        HashSet<string> written = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        HashSet<string> seenSections = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string currentSection = null;
        bool skipContinuation = false;

        for (int i = 0; i < lines.Length; i++)
        {
            string raw = lines[i];
            string trimmed = raw.Trim();

            if (skipContinuation && IsContinuationLine(raw))
            {
                continue;
            }
            // A blank line always ends the continuation scan for a multiline value.
            if (skipContinuation && raw.Length == 0)
            {
                skipContinuation = false;
            }
            skipContinuation = false;

            string sectionHeaderName;
            if (TryParseSectionHeader(raw, out sectionHeaderName))
            {
                if (currentSection != null)
                {
                    AppendMissingForSection(output, updates, written, currentSection);
                }

                currentSection = sectionHeaderName;
                seenSections.Add(currentSection);
                output.Add(raw);
                continue;
            }

            int separator = FindAssignmentSeparator(raw);
            if (currentSection != null && separator >= 0 && !trimmed.StartsWith("#", StringComparison.Ordinal) && !trimmed.StartsWith(";", StringComparison.Ordinal))
            {
                string key = ParseIniName(raw.Substring(0, separator));
                string combined = MakeKey(currentSection, key);
                string value;
                if (updates.TryGetValue(combined, out value))
                {
                    WriteEntry(output, key, value);
                    written.Add(combined);
                    skipContinuation = true;
                    continue;
                }
            }

            output.Add(raw);
        }

        if (currentSection != null)
        {
            AppendMissingForSection(output, updates, written, currentSection);
        }

        foreach (KeyValuePair<string, string> update in updates)
        {
            if (written.Contains(update.Key))
            {
                continue;
            }

            string section;
            string key;
            SplitKey(update.Key, out section, out key);
            if (!seenSections.Contains(section))
            {
                if (output.Count > 0 && output[output.Count - 1].Trim().Length > 0)
                {
                    output.Add("");
                }
                output.Add("[" + section + "]");
                seenSections.Add(section);
            }

            WriteEntry(output, key, update.Value);
            written.Add(update.Key);
        }

        TrimTrailingEmptyLines(output);
        WriteTextFile(path, output.ToArray(), file);
    }

    private static void WriteEntry(List<string> output, string keyPrefix, string value)
    {
        keyPrefix = ParseIniName(keyPrefix);
        string normalized = (value ?? "").Replace("\r\n", "\n").Replace('\r', '\n');
        if (normalized.IndexOf('\n') >= 0)
        {
            output.Add(FormatIniAssignment(keyPrefix, ""));
            string[] parts = normalized.Split('\n');
            for (int i = 0; i < parts.Length; i++)
            {
                output.Add("    " + QuoteIniString(parts[i]));
            }
        }
        else
        {
            output.Add(FormatIniAssignment(keyPrefix, normalized));
        }
    }

    private static void AppendMissingForSection(List<string> output, IDictionary<string, string> updates, HashSet<string> written, string section)
    {
        foreach (KeyValuePair<string, string> update in updates)
        {
            if (written.Contains(update.Key))
            {
                continue;
            }

            string updateSection;
            string updateKey;
            SplitKey(update.Key, out updateSection, out updateKey);
            if (!updateSection.Equals(section, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            WriteEntry(output, updateKey, update.Value);
            written.Add(update.Key);
        }
    }

    public static bool IsContinuationLine(string raw)
    {
        if (raw == null || raw.Length == 0 || !char.IsWhiteSpace(raw[0]))
        {
            return false;
        }

        string trimmed = raw.Trim();
        return trimmed.Length > 0 &&
            !trimmed.StartsWith("#", StringComparison.Ordinal) &&
            !trimmed.StartsWith(";", StringComparison.Ordinal);
    }

    private static bool IsInlineCommentStart(string value, int pos, bool requireLeadingWhitespace)
    {
        char ch = value[pos];
        if (ch != ';' && ch != '#')
        {
            return false;
        }

        return !requireLeadingWhitespace || pos == 0 || char.IsWhiteSpace(value[pos - 1]);
    }

    private static string StripInlineComment(string value, bool requireLeadingWhitespace)
    {
        if (value == null)
        {
            return "";
        }

        bool inQuote = false;
        char quoteChar = '\0';
        bool escaped = false;
        for (int i = 0; i < value.Length; i++)
        {
            char ch = value[i];
            if (escaped)
            {
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (ch == '"' || ch == '\'')
            {
                if (!inQuote)
                {
                    inQuote = true;
                    quoteChar = ch;
                }
                else if (quoteChar == ch)
                {
                    inQuote = false;
                    quoteChar = '\0';
                }
                continue;
            }

            if (!inQuote && IsInlineCommentStart(value, i, requireLeadingWhitespace))
            {
                return value.Substring(0, i).TrimEnd();
            }
        }

        return value.TrimEnd();
    }

    public static string ParseIniName(string text)
    {
        return UnquoteIniString(text);
    }

    public static string ParseIniValue(string text)
    {
        string s = (text ?? "").Trim();
        if (s.Length == 0)
        {
            return "";
        }

        char quote = s[0];
        if (quote != '"' && quote != '\'')
        {
            return UnquoteIniString(StripInlineComment(text ?? "", true));
        }

        StringBuilder builder = new StringBuilder();
        for (int i = 1; i < s.Length; i++)
        {
            char ch = s[i];
            if (ch == '\\' && i + 1 < s.Length && (s[i + 1] == quote || s[i + 1] == '\\'))
            {
                if (s[i + 1] == quote && !HasLaterQuote(s, i + 2, quote))
                {
                    builder.Append(ch);
                    continue;
                }

                builder.Append(s[i + 1]);
                i++;
                continue;
            }

            if (ch == quote)
            {
                return builder.ToString();
            }

            builder.Append(ch);
        }

        return UnquoteIniString(StripInlineComment(text ?? "", true));
    }

    public static bool TryParseSectionHeader(string line, out string name)
    {
        name = null;
        string s = StripLeadingBom(line ?? "").Trim();
        if (s.Length == 0 || s[0] == ';' || s[0] == '#' || s[0] != '[')
        {
            return false;
        }

        bool inQuote = false;
        char quoteChar = '\0';
        bool escaped = false;
        for (int i = 1; i < s.Length; i++)
        {
            char ch = s[i];
            if (escaped)
            {
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (ch == '"' || ch == '\'')
            {
                if (!inQuote)
                {
                    inQuote = true;
                    quoteChar = ch;
                }
                else if (quoteChar == ch)
                {
                    inQuote = false;
                    quoteChar = '\0';
                }
                continue;
            }

            if (!inQuote && ch == ']')
            {
                string rest = s.Substring(i + 1).Trim();
                if (rest.Length > 0 && rest[0] != ';' && rest[0] != '#')
                {
                    return false;
                }

                name = ParseIniName(s.Substring(1, i - 1));
                return name.Length > 0;
            }
        }

        return false;
    }

    public static int FindAssignmentSeparator(string line)
    {
        if (line == null)
        {
            return -1;
        }

        bool inQuote = false;
        char quoteChar = '\0';
        bool escaped = false;
        for (int i = 0; i < line.Length; i++)
        {
            char ch = line[i];
            if (escaped)
            {
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (ch == '"' || ch == '\'')
            {
                if (!inQuote)
                {
                    inQuote = true;
                    quoteChar = ch;
                }
                else if (quoteChar == ch)
                {
                    inQuote = false;
                    quoteChar = '\0';
                }
                continue;
            }

            if (!inQuote && ch == '=')
            {
                return i;
            }
        }

        return -1;
    }

    private static string UnquoteIniString(string text)
    {
        string s = (text ?? "").Trim();
        if (s.Length < 2)
        {
            return s;
        }

        char quote = s[0];
        if ((quote != '"' && quote != '\'') || s[s.Length - 1] != quote)
        {
            return s;
        }

        StringBuilder builder = new StringBuilder();
        for (int i = 1; i + 1 < s.Length; i++)
        {
            if (s[i] == '\\' && i + 2 < s.Length && (s[i + 1] == quote || s[i + 1] == '\\'))
            {
                builder.Append(s[i + 1]);
                i++;
            }
            else
            {
                builder.Append(s[i]);
            }
        }

        return builder.ToString();
    }

    private static bool HasLaterQuote(string text, int start, char quote)
    {
        for (int i = start; i < text.Length; i++)
        {
            if (text[i] == quote)
            {
                return true;
            }
        }

        return false;
    }

    private static string QuoteIniString(string text)
    {
        StringBuilder builder = new StringBuilder();
        builder.Append('"');
        string value = text ?? "";
        for (int i = 0; i < value.Length; i++)
        {
            char ch = value[i];
            if (ch == '"' || ch == '\\')
            {
                builder.Append('\\');
            }
            builder.Append(ch);
        }
        builder.Append('"');
        return builder.ToString();
    }

    private static string FormatIniAssignment(string key, string value)
    {
        return QuoteIniString(key) + " = " + QuoteIniString(value);
    }

    private static string StripLeadingBom(string value)
    {
        if (!string.IsNullOrEmpty(value) && value[0] == '\ufeff')
        {
            return value.Substring(1);
        }

        return value ?? "";
    }

    private static void TrimTrailingEmptyLines(List<string> lines)
    {
        while (lines.Count > 0 && lines[lines.Count - 1].Trim().Length == 0)
        {
            lines.RemoveAt(lines.Count - 1);
        }
    }

    public static string[] ReadAllLines(string path)
    {
        return ReadTextFile(path).Lines;
    }

    private static TextFileData ReadTextFile(string path)
    {
        TextFileData data = new TextFileData();
        data.Encoding = new UTF8Encoding(true);
        data.NewLine = Environment.NewLine;
        data.Lines = new string[0];

        if (!File.Exists(path))
        {
            return data;
        }

        byte[] bytes = File.ReadAllBytes(path);
        int offset;
        data.Encoding = DetectEncoding(bytes, out offset);
        string text = data.Encoding.GetString(bytes, offset, bytes.Length - offset);
        data.NewLine = DetectNewLine(text);
        data.Lines = SplitLines(text);
        return data;
    }

    private static void WriteTextFile(string path, string[] lines, TextFileData template)
    {
        string newLine = template.NewLine;
        if (newLine == null || newLine.Length == 0)
        {
            newLine = Environment.NewLine;
        }

        string text = string.Join(newLine, lines);
        if (lines.Length > 0)
        {
            text += newLine;
        }

        // Write atomically: stage to a temp file in the same directory, then
        // replace the real file. This prevents a half-written config on crash/kill.
        string dir  = Path.GetDirectoryName(Path.GetFullPath(path));
        string tmp  = Path.Combine(dir ?? ".", Path.GetRandomFileName() + ".tmp");
        try
        {
            File.WriteAllText(tmp, text, template.Encoding);
            if (File.Exists(path))
            {
                // File.Replace atomically swaps tmp -> path and removes the old file.
                File.Replace(tmp, path, null);
            }
            else
            {
                File.Move(tmp, path);
            }
        }
        catch
        {
            try { File.Delete(tmp); } catch { /* best-effort cleanup */ }
            throw;
        }
    }

    private static Encoding DetectEncoding(byte[] bytes, out int offset)
    {
        offset = 0;
        if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        {
            offset = 3;
            return new UTF8Encoding(true, true);
        }
        if (bytes.Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
        {
            offset = 2;
            return Encoding.Unicode;
        }
        if (bytes.Length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
        {
            offset = 2;
            return Encoding.BigEndianUnicode;
        }

        try
        {
            UTF8Encoding strictUtf8 = new UTF8Encoding(false, true);
            strictUtf8.GetString(bytes);
            return new UTF8Encoding(false, true);
        }
        catch (DecoderFallbackException)
        {
            return Encoding.Default;
        }
    }

    private static string DetectNewLine(string text)
    {
        int crlf = text.IndexOf("\r\n", StringComparison.Ordinal);
        if (crlf >= 0)
        {
            return "\r\n";
        }
        int lf = text.IndexOf('\n');
        if (lf >= 0)
        {
            return "\n";
        }
        int cr = text.IndexOf('\r');
        if (cr >= 0)
        {
            return "\r";
        }

        return Environment.NewLine;
    }

    private static string[] SplitLines(string text)
    {
        string normalized = text.Replace("\r\n", "\n").Replace('\r', '\n');
        if (normalized.EndsWith("\n", StringComparison.Ordinal))
        {
            normalized = normalized.Substring(0, normalized.Length - 1);
        }
        if (normalized.Length == 0)
        {
            return new string[0];
        }

        return normalized.Split('\n');
    }

    private sealed class TextFileData
    {
        public Encoding Encoding;
        public string NewLine;
        public string[] Lines;
    }
}

internal sealed class IpcOptions
{
    public readonly int ConnectTimeoutMs;
    public readonly int ResponseTimeoutMs;

    private IpcOptions(int connectTimeoutMs, int responseTimeoutMs)
    {
        ConnectTimeoutMs = connectTimeoutMs;
        ResponseTimeoutMs = responseTimeoutMs;
    }

    public static IpcOptions FromConfig(IniConfig config)
    {
        return new IpcOptions(
            ClampMs(config.GetInt("ipc", "connect_timeout_ms", 250), 50, 60000),
            ClampMs(config.GetInt("ipc", "response_timeout_ms", 5000), 250, 120000));
    }

    private static int ClampMs(int value, int min, int max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
}

internal sealed class GatewayOptions
{
    public readonly string SystemLocale;
    public readonly string BrowserUserAgent;
    public readonly string BrowserVersion;
    public readonly string OsVersion;
    public readonly string ReleaseChannel;
    public readonly int ClientBuildNumber;
    public readonly int Capabilities;
    public readonly int ConnectTimeoutMs;
    public readonly int HelloTimeoutMs;
    public readonly int ReadyTimeoutMs;
    public readonly int SendTimeoutMs;
    public readonly int CloseTimeoutMs;
    public readonly int AssetFetchTimeoutMs;
    public readonly string AssetFetchUserAgent;

    private GatewayOptions(IniConfig config)
    {
        SystemLocale = NonEmpty(config.Get("gateway", "system_locale", "en-US"), "en-US");
        BrowserUserAgent = NonEmpty(config.Get("gateway", "browser_user_agent", ConfigDefaults.DefaultGatewayBrowserUserAgent), ConfigDefaults.DefaultGatewayBrowserUserAgent);
        BrowserVersion = NonEmpty(config.Get("gateway", "browser_version", "32.2.7"), "32.2.7");
        OsVersion = NonEmpty(config.Get("gateway", "os_version", "10.0.26100"), "10.0.26100");
        ReleaseChannel = NonEmpty(config.Get("gateway", "release_channel", "stable"), "stable");
        ClientBuildNumber = ClampInt(config.GetInt("gateway", "client_build_number", 390000), 1, int.MaxValue);
        Capabilities = ClampInt(config.GetInt("gateway", "capabilities", 65), 0, int.MaxValue);
        ConnectTimeoutMs = ClampMs(config.GetInt("gateway", "connect_timeout_ms", 10000), 1000, 120000);
        HelloTimeoutMs = ClampMs(config.GetInt("gateway", "hello_timeout_ms", 10000), 1000, 120000);
        ReadyTimeoutMs = ClampMs(config.GetInt("gateway", "ready_timeout_ms", 30000), 1000, 180000);
        SendTimeoutMs = ClampMs(config.GetInt("gateway", "send_timeout_ms", 10000), 1000, 120000);
        CloseTimeoutMs = ClampMs(config.GetInt("gateway", "close_timeout_ms", 2000), 0, 30000);
        AssetFetchTimeoutMs = ClampMs(config.GetInt("gateway", "asset_fetch_timeout_ms", 10000), 1000, 120000);
        AssetFetchUserAgent = NonEmpty(config.Get("gateway", "asset_fetch_user_agent", "DiscordBot (https://github.com, 1)"), "DiscordBot (https://github.com, 1)");
    }

    public static GatewayOptions FromConfig(IniConfig config)
    {
        return new GatewayOptions(config);
    }

    private static string NonEmpty(string value, string fallback)
    {
        string text = (value ?? "").Trim();
        return text.Length == 0 ? fallback : text;
    }

    private static int ClampMs(int value, int min, int max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    private static int ClampInt(int value, int min, int max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
}

internal sealed class TimeoutWebClient : WebClient
{
    private readonly int timeoutMs;

    public TimeoutWebClient(int timeoutMs)
    {
        this.timeoutMs = timeoutMs;
    }

    protected override WebRequest GetWebRequest(Uri address)
    {
        WebRequest request = base.GetWebRequest(address);
        if (request != null)
        {
            request.Timeout = timeoutMs;
            HttpWebRequest http = request as HttpWebRequest;
            if (http != null)
            {
                http.ReadWriteTimeout = timeoutMs;
            }
        }

        return request;
    }
}

internal sealed class DiscordGatewayClient : IDisposable
{
    // Discord Gateway v10 endpoint
    private const string GatewayUrl = "wss://gateway.discord.gg/?v=10&encoding=json";

    // Gateway opcodes
    private const int OpDispatch       = 0;
    private const int OpHeartbeat      = 1;
    private const int OpIdentify       = 2;
    private const int OpPresenceUpdate = 3;
    private const int OpHello          = 10;
    private const int OpHeartbeatAck   = 11;

    private readonly string token;
    private readonly bool verbose;
    private readonly GatewayOptions options;
    private readonly object sendLock = new object();

    // Asset name -> asset ID cache, populated once per application_id at startup.
    // IPC accepts Developer Portal asset names, but Gateway activity assets need
    // the application asset ID.
    private readonly Dictionary<string, string> assetIdCache =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    private string cachedAssetApplicationId;

    private ClientWebSocket ws;
    private Thread readThread;
    private Thread heartbeatThread;
    private volatile bool connected;
    private volatile bool identified;
    private volatile bool heartbeatAcked = true;
    private int heartbeatIntervalMs;
    private int? lastSequence;
    private string lastConnectError;

    public DiscordGatewayClient(string token, bool verbose, GatewayOptions options)
    {
        this.token = token;
        this.verbose = verbose;
        this.options = options ?? GatewayOptions.FromConfig(new IniConfig());
    }

    public bool IsConnected
    {
        get { return connected && ws != null && ws.State == WebSocketState.Open; }
    }

    // ------------------------------------------------------------------ Connect

    public void Connect(Dictionary<string, object> initialActivity, string initialStatus)
    {
        Close();
        lastConnectError = null;

        ClientWebSocket candidate = new ClientWebSocket();
        // Note: "User-Agent" is a restricted header on ClientWebSocket in .NET 4.x
        // and cannot be set via SetRequestHeader — omit it entirely.

        try
        {
            Logger.Info("Connecting to Discord Gateway...");
            System.Threading.Tasks.Task connectTask = candidate.ConnectAsync(new Uri(GatewayUrl), CancellationToken.None);
            if (!connectTask.Wait(options.ConnectTimeoutMs))
            {
                throw new TimeoutException("Gateway connect timed out after " + options.ConnectTimeoutMs.ToString(CultureInfo.InvariantCulture) + " ms.");
            }
            connectTask.Wait();
        }
        catch (Exception ex)
        {
            candidate.Dispose();
            // .Wait() wraps exceptions in AggregateException — unwrap to the real cause.
            Exception inner = ex;
            AggregateException ae = ex as AggregateException;
            if (ae != null)
            {
                ae = ae.Flatten();
                if (ae.InnerExceptions.Count > 0) inner = ae.InnerExceptions[0];
            }
            throw new IOException("WebSocket connect failed: " + inner.Message, inner);
        }

        ws = candidate;
        connected = true;

        // Start reader first so we can receive HELLO
        readThread = new Thread(ReadLoop);
        readThread.IsBackground = true;
        readThread.Name = "GatewayReader";
        readThread.Start();

        // Wait for HELLO (sets heartbeatIntervalMs)
        Stopwatch sw = Stopwatch.StartNew();
        while (heartbeatIntervalMs == 0 && sw.ElapsedMilliseconds < options.HelloTimeoutMs && connected)
        {
            Thread.Sleep(50);
        }

        if (heartbeatIntervalMs == 0)
        {
            Close();
            throw new IOException("Did not receive Gateway HELLO within " + options.HelloTimeoutMs.ToString(CultureInfo.InvariantCulture) + " ms.");
        }

        // Send first heartbeat immediately then start the loop
        SendHeartbeat();
        heartbeatThread = new Thread(HeartbeatLoop);
        heartbeatThread.IsBackground = true;
        heartbeatThread.Name = "GatewayHeartbeat";
        heartbeatThread.Start();

        // Send IDENTIFY. Discord supports an initial presence in IDENTIFY, and
        // some clients only apply activity fields during session creation.
        Identify(initialActivity, initialStatus);

        // Wait for READY dispatch. User sessions can receive a large READY payload,
        // so allow enough time for slower accounts/connections.
        sw.Restart();
        while (!identified && sw.ElapsedMilliseconds < options.ReadyTimeoutMs && connected)
        {
            Thread.Sleep(50);
        }

        if (lastConnectError != null)
        {
            string err = lastConnectError;
            Close();
            throw new IOException("Gateway authentication error: " + err);
        }

        if (!identified)
        {
            Close();
            throw new IOException("Did not receive Gateway READY within " + options.ReadyTimeoutMs.ToString(CultureInfo.InvariantCulture) + " ms.");
        }

        Logger.Info("Connected to Discord Gateway and identified.");
    }

    // ------------------------------------------------------------------ Identify

    private void Identify(Dictionary<string, object> initialActivity, string initialStatus)
    {
        // Properties must match what the Discord desktop client sends for user accounts.
        // "device" is intentionally empty string (not "Discord Client") for user sessions.
        Dictionary<string, object> properties = new Dictionary<string, object>();
        properties["os"]                        = "Windows";
        properties["browser"]                   = "Discord Client";
        properties["device"]                    = "";
        properties["system_locale"]             = options.SystemLocale;
        properties["browser_user_agent"]        = options.BrowserUserAgent;
        properties["browser_version"]           = options.BrowserVersion;
        properties["os_version"]                = options.OsVersion;
        properties["release_channel"]           = options.ReleaseChannel;
        properties["client_build_number"]       = options.ClientBuildNumber;
        properties["client_event_source"]       = null;

        // capabilities is a feature-support bitmask expected by Discord for user-account
        // connections. Without it Discord may classify the session as legacy and silently
        // drop activities in PRESENCE_UPDATE.
        // client_state is required for proper session initialisation.
        Dictionary<string, object> clientState = new Dictionary<string, object>();
        clientState["guild_versions"]               = new Dictionary<string, object>();
        clientState["highest_last_message_id"]      = "0";
        clientState["read_state_version"]           = 0;
        clientState["user_guild_settings_version"]  = -1;
        clientState["user_settings_version"]        = -1;
        clientState["private_channels_version"]     = "0";
        clientState["api_code_version"]             = 0;

        Dictionary<string, object> d = new Dictionary<string, object>();
        d["token"]        = token;
        d["capabilities"] = options.Capabilities;
        d["properties"]   = properties;
        d["compress"]     = false;
        d["client_state"] = clientState;
        if (initialActivity != null)
        {
            object appIdObj;
            if (initialActivity.TryGetValue("application_id", out appIdObj) && appIdObj != null)
            {
                FetchAssetUrls(appIdObj.ToString());
            }
            d["presence"] = BuildPresenceData(initialActivity, initialStatus);
        }

        SendPayload(OpIdentify, d);
        Logger.Info("IDENTIFY sent.");
    }

    // ------------------------------------------------------------------ Presence

    public void SetPresence(Dictionary<string, object> activity, string status)
    {
        if (!IsConnected)
        {
            throw new IOException("Gateway is not connected.");
        }

        // Resolve asset names to asset IDs before building the payload (no-op if already cached).
        object appIdObj;
        if (activity.TryGetValue("application_id", out appIdObj) && appIdObj != null)
        {
            FetchAssetUrls(appIdObj.ToString());
        }

        Dictionary<string, object> d = BuildPresenceData(activity, status);

        if (verbose)
        {
            Logger.Debug("Sending PRESENCE_UPDATE: " + Json.Serialize(d));
        }

        SendPayload(OpPresenceUpdate, d);
        Logger.Info("Presence updated. Status=" + status);
    }

    private Dictionary<string, object> BuildPresenceData(Dictionary<string, object> activity, string status)
    {
        // "since" must be null when not idle; 0 is misread by Discord as "idle since epoch".
        object since = null;
        if (status == "idle")
        {
            since = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        Dictionary<string, object> gatewayActivity = BuildGatewayActivity(activity);

        List<object> activities = new List<object>();
        activities.Add(gatewayActivity);

        Dictionary<string, object> d = new Dictionary<string, object>();
        d["since"]      = since;
        d["activities"] = activities;
        d["status"]     = status;
        d["afk"]        = status == "idle";
        return d;
    }

    private Dictionary<string, object> BuildGatewayActivity(Dictionary<string, object> activity)
    {
        Dictionary<string, object> result = new Dictionary<string, object>();

        // NOTE: "id" and "created_at" are intentionally omitted.
        // "id" — Discord generates a session-scoped activity ID server-side; providing
        //   our own (derived from application_id) causes the activity to be rejected.
        // "created_at" — SDK-side metadata; Discord sets this server-side when absent.
        CopyIfPresent(activity, result, "name");
        CopyIfPresent(activity, result, "type");
        CopyIfPresent(activity, result, "url");
        // application_id is required so Discord can associate application asset IDs
        // with the Rich Presence app.
        CopyIfPresent(activity, result, "application_id");
        CopyIfPresent(activity, result, "status_display_type");
        CopyIfPresent(activity, result, "details");
        CopyIfPresent(activity, result, "details_url");
        CopyIfPresent(activity, result, "state");
        CopyIfPresent(activity, result, "state_url");
        CopyIfPresent(activity, result, "emoji");
        CopyIfPresent(activity, result, "party");

        // Resolve Developer Portal asset names to asset IDs for Gateway. If lookup
        // fails, preserve the configured value so URLs and already-resolved IDs still
        // pass through instead of silently removing the image from the payload.
        object assetsObj;
        if (activity.TryGetValue("assets", out assetsObj))
        {
            IDictionary<string, object> srcAssets = assetsObj as IDictionary<string, object>;
            if (srcAssets != null && srcAssets.Count > 0)
            {
                Dictionary<string, object> outAssets = new Dictionary<string, object>();
                ResolveAssetImageKey(srcAssets, outAssets, "large_image");
                ResolveAssetImageKey(srcAssets, outAssets, "small_image");
                object largeText;
                if (srcAssets.TryGetValue("large_text", out largeText) && largeText != null)
                    outAssets["large_text"] = largeText;
                object smallText;
                if (srcAssets.TryGetValue("small_text", out smallText) && smallText != null)
                    outAssets["small_text"] = smallText;
                if (outAssets.Count > 0)
                    result["assets"] = outAssets;
            }
        }

        CopyIfPresent(activity, result, "secrets");
        CopyGatewayButtons(activity, result);

        object timestampsValue;
        if (activity.TryGetValue("timestamps", out timestampsValue))
        {
            IDictionary<string, object> timestamps = timestampsValue as IDictionary<string, object>;
            if (timestamps != null)
            {
                Dictionary<string, object> gatewayTimestamps = new Dictionary<string, object>();
                CopyGatewayTimestamp(timestamps, gatewayTimestamps, "start");
                CopyGatewayTimestamp(timestamps, gatewayTimestamps, "end");
                if (gatewayTimestamps.Count > 0)
                {
                    result["timestamps"] = gatewayTimestamps;
                }
            }
        }

        return result;
    }

    // Copies one image key from srcAssets to outAssets, converting a bare Developer
    // Portal asset name (e.g. "emu_07") to its asset ID via the cache. Already-URL
    // and already-ID values are forwarded unchanged.
    private void ResolveAssetImageKey(
        IDictionary<string, object> srcAssets,
        Dictionary<string, object> outAssets,
        string key)
    {
        object val;
        if (!srcAssets.TryGetValue(key, out val)) return;
        string s = val as string;
        if (s == null || s.Length == 0) return;

        if (s.StartsWith("https://", StringComparison.OrdinalIgnoreCase)
            || s.StartsWith("http://",  StringComparison.OrdinalIgnoreCase)
            || s.StartsWith("mp:",      StringComparison.OrdinalIgnoreCase))
        {
            // Already a URL — forward as-is.
            outAssets[key] = s;
            return;
        }

        string assetId;
        if (assetIdCache.TryGetValue(s, out assetId))
        {
            outAssets[key] = assetId;
            return;
        }

        outAssets[key] = s;
    }

    private static void CopyGatewayButtons(Dictionary<string, object> activity, Dictionary<string, object> result)
    {
        object buttonsObj;
        if (!activity.TryGetValue("buttons", out buttonsObj) || buttonsObj == null)
        {
            return;
        }

        IEnumerable buttons = buttonsObj as IEnumerable;
        if (buttons == null || buttonsObj is string)
        {
            return;
        }

        List<object> labels = new List<object>();
        List<object> urls = new List<object>();
        foreach (object item in buttons)
        {
            IDictionary<string, object> button = item as IDictionary<string, object>;
            if (button == null)
            {
                continue;
            }

            object labelObj;
            object urlObj;
            if (!button.TryGetValue("label", out labelObj) || !button.TryGetValue("url", out urlObj))
            {
                continue;
            }

            string label = labelObj == null ? "" : labelObj.ToString().Trim();
            string url = urlObj == null ? "" : urlObj.ToString().Trim();
            if (label.Length == 0 || url.Length == 0)
            {
                continue;
            }

            labels.Add(label);
            urls.Add(url);
            if (labels.Count == 2)
            {
                break;
            }
        }

        if (labels.Count == 0)
        {
            return;
        }

        // Discord echoes Gateway activity buttons as label strings. For user-session
        // Gateway updates, URLs ride in activity metadata and are not echoed back.
        Dictionary<string, object> metadata = new Dictionary<string, object>();
        metadata["button_urls"] = urls;
        result["buttons"] = labels;
        result["metadata"] = metadata;
    }

    // Fetches the asset list for applicationId from Discord's API and caches the
    // name -> asset ID mapping. Called once per unique applicationId before the first
    // presence update. Failures are logged but non-fatal because explicit asset IDs
    // and URLs can still be sent without the cache.
    private void FetchAssetUrls(string applicationId)
    {
        if (string.IsNullOrEmpty(applicationId)) return;
        if (applicationId == cachedAssetApplicationId) return;

        try
        {
            // /oauth2/applications/{id}/assets is the PUBLIC endpoint for reading rich
            // presence assets and requires no authentication.  The plain
            // /applications/{id}/assets requires a bot token (with "Bot " prefix) and
            // returns [] for user tokens, which is why we were getting 0 assets.
            string apiUrl = "https://discord.com/api/v10/oauth2/applications/" + applicationId + "/assets";
            string responseJson;
            using (TimeoutWebClient wc = new TimeoutWebClient(options.AssetFetchTimeoutMs))
            {
                wc.Headers["User-Agent"] = options.AssetFetchUserAgent;
                responseJson = wc.DownloadString(apiUrl);
            }
            // Log the raw response so we can see exactly what Discord returns.
            Logger.Debug("Asset API response: " + responseJson);

            // Parse a JSON array: [{"id":"...","name":"...","type":1}, ...]
            // Uses the same minimal extractor pattern already in this class.
            assetIdCache.Clear();
            int search = 0;
            while (true)
            {
                int objStart = responseJson.IndexOf('{', search);
                if (objStart < 0) break;
                int objEnd = responseJson.IndexOf('}', objStart);
                if (objEnd < 0) break;

                string entry = responseJson.Substring(objStart, objEnd - objStart + 1);
                string assetId   = ExtractString(entry, "\"id\"");
                string assetName = ExtractString(entry, "\"name\"");

                if (assetId != null && assetId.Length > 0 && assetName != null && assetName.Length > 0)
                {
                    assetIdCache[assetName] = assetId;
                }

                search = objEnd + 1;
            }

            cachedAssetApplicationId = applicationId;
            Logger.Info("Asset IDs cached: " + assetIdCache.Count
                + " asset(s) for application " + applicationId);
        }
        catch (Exception ex)
        {
            Logger.Error("Could not fetch asset IDs for application " + applicationId
                + " — configured asset names may not resolve in Gateway mode. (" + ex.Message + ")");
        }
    }

    private static void CopyIfPresent(Dictionary<string, object> source, Dictionary<string, object> target, string key)
    {
        object value;
        if (source.TryGetValue(key, out value))
        {
            target[key] = value;
        }
    }

    private static void CopyGatewayTimestamp(IDictionary<string, object> source, Dictionary<string, object> target, string key)
    {
        object value;
        if (!source.TryGetValue(key, out value) || value == null)
        {
            return;
        }

        long timestamp;
        if (!TryGetInt64(value, out timestamp) || timestamp <= 0)
        {
            return;
        }

        // Gateway activity timestamps are milliseconds. Existing config/runtime
        // values are seconds for local RPC compatibility, so normalize them here.
        if (timestamp < 100000000000L)
        {
            timestamp *= 1000L;
        }

        target[key] = timestamp;
    }

    private static bool TryGetInt64(object value, out long result)
    {
        if (value is long)
        {
            result = (long)value;
            return true;
        }
        if (value is int)
        {
            result = (int)value;
            return true;
        }
        if (value is double)
        {
            result = (long)(double)value;
            return true;
        }
        if (value is float)
        {
            result = (long)(float)value;
            return true;
        }

        string text = value as string;
        if (text != null)
        {
            return long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out result);
        }

        result = 0;
        return false;
    }

    public void ClearPresence(string status)
    {
        if (!IsConnected) return;

        status = Program.NormalizeStatus(status);
        Dictionary<string, object> d = new Dictionary<string, object>();
        d["since"]      = status == "idle" ? (object)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() : (object)null;
        d["activities"] = new List<object>();
        d["status"]     = status;
        d["afk"]        = status == "idle";

        SendPayload(OpPresenceUpdate, d);
    }

    // ------------------------------------------------------------------ Close / Dispose

    public void Close()
    {
        connected = false;
        identified = false;
        heartbeatIntervalMs = 0;
        heartbeatAcked = true;

        ClientWebSocket oldWs = ws;
        ws = null;

        if (oldWs != null)
        {
            try
            {
                if (oldWs.State == WebSocketState.Open)
                {
                    oldWs.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "Closing", CancellationToken.None).Wait(options.CloseTimeoutMs);
                }
            }
            catch { }

            try { oldWs.Dispose(); } catch { }
        }
    }

    public void Dispose()
    {
        Close();
    }

    // ------------------------------------------------------------------ Send

    private void SendPayload(int op, object d)
    {
        Dictionary<string, object> payload = new Dictionary<string, object>();
        payload["op"] = op;
        payload["d"]  = d;

        string json = Json.Serialize(payload);
        byte[] bytes = Encoding.UTF8.GetBytes(json);

        lock (sendLock)
        {
            if (!IsConnected)
            {
                throw new IOException("Gateway WebSocket is not connected.");
            }

            System.Threading.Tasks.Task sendTask = ws.SendAsync(
                new ArraySegment<byte>(bytes),
                WebSocketMessageType.Text,
                true,
                CancellationToken.None
            );
            if (!sendTask.Wait(options.SendTimeoutMs))
            {
                Close();
                throw new IOException("Gateway send timed out after " + options.SendTimeoutMs.ToString(CultureInfo.InvariantCulture) + " ms.");
            }
            sendTask.Wait();
        }
    }

    // ------------------------------------------------------------------ Heartbeat

    private void SendHeartbeat()
    {
        object seq = lastSequence.HasValue ? (object)lastSequence.Value : (object)null;
        Dictionary<string, object> payload = new Dictionary<string, object>();
        payload["op"] = OpHeartbeat;
        payload["d"]  = seq;

        string json = Json.Serialize(payload);
        byte[] bytes = Encoding.UTF8.GetBytes(json);

        lock (sendLock)
        {
            if (!IsConnected) return;

            System.Threading.Tasks.Task sendTask = ws.SendAsync(
                new ArraySegment<byte>(bytes),
                WebSocketMessageType.Text,
                true,
                CancellationToken.None
            );
            if (!sendTask.Wait(options.SendTimeoutMs))
            {
                Close();
                throw new IOException("Gateway heartbeat timed out after " + options.SendTimeoutMs.ToString(CultureInfo.InvariantCulture) + " ms.");
            }
            sendTask.Wait();
        }

        if (verbose) Logger.Debug("Heartbeat sent (seq=" + (lastSequence.HasValue ? lastSequence.Value.ToString() : "null") + ")");
    }

    private void HeartbeatLoop()
    {
        try
        {
            while (connected)
            {
                Thread.Sleep(heartbeatIntervalMs > 0 ? heartbeatIntervalMs : 41250);

                if (!connected) break;

                if (!heartbeatAcked)
                {
                    Logger.Error("Heartbeat ACK not received; reconnecting.");
                    connected = false;
                    break;
                }

                heartbeatAcked = false;
                try
                {
                    SendHeartbeat();
                }
                catch (Exception ex)
                {
                    if (connected) Logger.Error("Heartbeat send failed: " + ex.Message);
                    break;
                }
            }
        }
        finally
        {
            connected = false;
        }
    }

    // ------------------------------------------------------------------ Read loop

    private void ReadLoop()
    {
        try
        {
            byte[] buf = new byte[65536];

            while (connected && ws != null && ws.State == WebSocketState.Open)
            {
                // Accumulate raw bytes before decoding so that multi-byte UTF-8
                // sequences are never split across fragment boundaries.
                MemoryStream messageBytes = new MemoryStream();
                WebSocketReceiveResult result;

                do
                {
                    result = ws.ReceiveAsync(new ArraySegment<byte>(buf), CancellationToken.None).Result;

                    if (result.MessageType == WebSocketMessageType.Close)
                    {
                        string closeDesc = result.CloseStatusDescription ?? "";
                        string closeCode = result.CloseStatus.HasValue
                            ? ((int)result.CloseStatus.Value).ToString(CultureInfo.InvariantCulture)
                            : "unknown";

                        Logger.Error("Gateway closed: " + closeCode + " " + closeDesc);

                        // Code 4004 = Authentication failed (bad token) — fatal
                        if (closeCode == "4004")
                        {
                            lastConnectError = "4004 Authentication failed. Check [general] token.";
                        }

                        connected = false;
                        return;
                    }

                    messageBytes.Write(buf, 0, result.Count);
                }
                while (!result.EndOfMessage);

                // Decode the complete message in one shot; no risk of splitting a
                // multi-byte UTF-8 character that straddles two receive buffers.
                string json = Encoding.UTF8.GetString(messageBytes.ToArray());
                if (verbose) Logger.Debug("Gateway recv: " + SummarizeGatewayPayload(json));

                HandleMessage(json);
            }
        }
        catch (Exception ex)
        {
            if (connected)
            {
                Logger.Error("Gateway read error: " + ex.Message);
            }
        }
        finally
        {
            Logger.Info("Gateway reader exited.");
            connected = false;
        }
    }

    // ------------------------------------------------------------------ Message handler

    private void HandleMessage(string json)
    {
        // Minimal inline JSON extraction — avoids a full JSON parser dependency
        int op = ExtractInt(json, "\"op\"");
        int? s = ExtractIntOrNull(json, "\"s\"");
        if (s.HasValue) lastSequence = s;

        if (op == OpHello)
        {
            int interval = ExtractInt(json, "\"heartbeat_interval\"");
            if (interval > 0)
            {
                heartbeatIntervalMs = interval;
                Logger.Info("Gateway HELLO received. Heartbeat interval: " + interval + "ms");
            }
            return;
        }

        if (op == OpHeartbeatAck)
        {
            heartbeatAcked = true;
            if (verbose) Logger.Debug("Heartbeat ACK received.");
            return;
        }

        if (op == OpHeartbeat)
        {
            // Server-side heartbeat request
            try { SendHeartbeat(); } catch { }
            return;
        }

        if (op == OpDispatch)
        {
            string t = ExtractString(json, "\"t\"");
            if (t == "READY")
            {
                identified = true;
                Logger.Info("Gateway READY received. Presence is now live.");
            }
            else if (t == "RESUMED")
            {
                identified = true;
                Logger.Info("Gateway RESUMED.");
            }
            else if (t == "SESSIONS_REPLACE")
            {
                if (verbose)
                {
                    Logger.Debug("Gateway SESSIONS_REPLACE raw: " + json);
                    Logger.Debug("Gateway session echo: " + SummarizeSessionsReplace(json));
                }
            }
            else if (verbose)
            {
                Logger.Debug("Gateway dispatch: " + t);
            }
        }
    }

    // ------------------------------------------------------------------ Tiny JSON extractors

    private static string SummarizeGatewayPayload(string json)
    {
        int op = ExtractInt(json, "\"op\"");
        string t = ExtractString(json, "\"t\"");
        StringBuilder builder = new StringBuilder();
        builder.Append("op=").Append(op.ToString(CultureInfo.InvariantCulture));
        if (t != null)
        {
            builder.Append(" t=").Append(t);
        }

        builder.Append(" bytes=").Append(json.Length.ToString(CultureInfo.InvariantCulture));
        return builder.ToString();
    }

    private static string SummarizeSessionsReplace(string json)
    {
        int sessions = CountOccurrences(json, "\"session_id\"");
        int emptyActivities = 0;
        int sessionsWithActivities = 0;
        string firstName = null;
        int? firstType = null;

        string marker = "\"activities\":[";
        int search = 0;
        while (true)
        {
            int idx = json.IndexOf(marker, search, StringComparison.Ordinal);
            if (idx < 0)
            {
                break;
            }

            int start = idx + marker.Length;
            while (start < json.Length && char.IsWhiteSpace(json[start]))
            {
                start++;
            }

            if (start < json.Length && json[start] == ']')
            {
                emptyActivities++;
            }
            else
            {
                sessionsWithActivities++;
                if (firstName == null)
                {
                    firstName = ExtractStringAfter(json, "\"name\"", start);
                    int parsedType = ExtractIntAfter(json, "\"type\"", start);
                    if (parsedType >= 0)
                    {
                        firstType = parsedType;
                    }
                }
            }

            search = start + 1;
        }

        StringBuilder builder = new StringBuilder();
        builder.Append("sessions=").Append(sessions.ToString(CultureInfo.InvariantCulture));
        builder.Append(" with_activities=").Append(sessionsWithActivities.ToString(CultureInfo.InvariantCulture));
        builder.Append(" empty_activities=").Append(emptyActivities.ToString(CultureInfo.InvariantCulture));
        if (firstName != null)
        {
            builder.Append(" first_activity=\"").Append(firstName).Append("\"");
        }
        if (firstType.HasValue)
        {
            builder.Append(" type=").Append(firstType.Value.ToString(CultureInfo.InvariantCulture));
        }

        return builder.ToString();
    }

    private static int CountOccurrences(string text, string value)
    {
        int count = 0;
        int index = 0;
        while (true)
        {
            index = text.IndexOf(value, index, StringComparison.Ordinal);
            if (index < 0)
            {
                return count;
            }

            count++;
            index += value.Length;
        }
    }

    // Extracts the integer value of a key from a flat JSON string, e.g. "op":3
    private static int ExtractInt(string json, string key)
    {
        int idx = json.IndexOf(key, StringComparison.Ordinal);
        if (idx < 0) return -1;
        int colon = json.IndexOf(':', idx + key.Length);
        if (colon < 0) return -1;
        int start = colon + 1;
        while (start < json.Length && json[start] == ' ') start++;
        int end = start;
        while (end < json.Length && (char.IsDigit(json[end]) || json[end] == '-')) end++;
        int value;
        if (int.TryParse(json.Substring(start, end - start), NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
            return value;
        return -1;
    }

    private static int ExtractIntAfter(string json, string key, int offset)
    {
        int idx = json.IndexOf(key, offset, StringComparison.Ordinal);
        if (idx < 0) return -1;
        int colon = json.IndexOf(':', idx + key.Length);
        if (colon < 0) return -1;
        int start = colon + 1;
        while (start < json.Length && json[start] == ' ') start++;
        int end = start;
        while (end < json.Length && (char.IsDigit(json[end]) || json[end] == '-')) end++;
        int value;
        if (int.TryParse(json.Substring(start, end - start), NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
            return value;
        return -1;
    }

    private static int? ExtractIntOrNull(string json, string key)
    {
        int idx = json.IndexOf(key, StringComparison.Ordinal);
        if (idx < 0) return null;
        int colon = json.IndexOf(':', idx + key.Length);
        if (colon < 0) return null;
        int start = colon + 1;
        while (start < json.Length && json[start] == ' ') start++;
        if (start < json.Length && json[start] == 'n') return null; // null
        int end = start;
        while (end < json.Length && (char.IsDigit(json[end]) || json[end] == '-')) end++;
        int value;
        if (int.TryParse(json.Substring(start, end - start), NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
            return value;
        return null;
    }

    // Extracts a quoted string value for a key, e.g. "t":"READY"
    private static string ExtractString(string json, string key)
    {
        int idx = json.IndexOf(key, StringComparison.Ordinal);
        if (idx < 0) return null;
        return ExtractStringAt(json, idx, key);
    }

    private static string ExtractStringAfter(string json, string key, int offset)
    {
        int idx = json.IndexOf(key, offset, StringComparison.Ordinal);
        if (idx < 0) return null;
        return ExtractStringAt(json, idx, key);
    }

    private static string ExtractStringAt(string json, int idx, string key)
    {
        int colon = json.IndexOf(':', idx + key.Length);
        if (colon < 0) return null;
        int start = colon + 1;
        while (start < json.Length && json[start] == ' ') start++;
        if (start >= json.Length || json[start] != '"') return null;
        return DecodeJsonStringAt(json, start);
    }

    private static string DecodeJsonStringAt(string json, int quoteIndex)
    {
        if (quoteIndex < 0 || quoteIndex >= json.Length || json[quoteIndex] != '"')
        {
            return null;
        }

        StringBuilder builder = new StringBuilder();
        for (int i = quoteIndex + 1; i < json.Length; i++)
        {
            char ch = json[i];
            if (ch == '"')
            {
                return builder.ToString();
            }

            if (ch != '\\')
            {
                builder.Append(ch);
                continue;
            }

            if (++i >= json.Length)
            {
                return null;
            }

            char escaped = json[i];
            switch (escaped)
            {
                case '"': builder.Append('"'); break;
                case '\\': builder.Append('\\'); break;
                case '/': builder.Append('/'); break;
                case 'b': builder.Append('\b'); break;
                case 'f': builder.Append('\f'); break;
                case 'n': builder.Append('\n'); break;
                case 'r': builder.Append('\r'); break;
                case 't': builder.Append('\t'); break;
                case 'u':
                    if (i + 4 >= json.Length)
                    {
                        return null;
                    }
                    string hex = json.Substring(i + 1, 4);
                    int codePoint;
                    if (!int.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out codePoint))
                    {
                        return null;
                    }
                    builder.Append((char)codePoint);
                    i += 4;
                    break;
                default:
                    builder.Append(escaped);
                    break;
            }
        }

        return null;
    }
}


internal sealed class IniConfig
{
    private readonly Dictionary<string, Dictionary<string, string>> sections =
        new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
    private readonly List<string> sectionOrder = new List<string>();
    private readonly Dictionary<string, List<string>> keyOrder =
        new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);

    public static IniConfig Load(string path)
    {
        IniConfig config = new IniConfig();
        string currentSection = null;
        string currentKey = null;

        string[] lines = IniConfigFile.ReadAllLines(path);
        for (int i = 0; i < lines.Length; i++)
        {
            string raw = lines[i];
            string trimmed = raw.Trim();

            if (trimmed.Length == 0 || trimmed.StartsWith("#", StringComparison.Ordinal) || trimmed.StartsWith(";", StringComparison.Ordinal))
            {
                continue;
            }

            string sectionHeaderName;
            if (IniConfigFile.TryParseSectionHeader(raw, out sectionHeaderName))
            {
                currentSection = sectionHeaderName;
                config.EnsureSection(currentSection);
                currentKey = null;
                continue;
            }

            if (currentSection != null && currentKey != null && IniConfigFile.IsContinuationLine(raw))
            {
                Dictionary<string, string> section = config.sections[currentSection];
                section[currentKey] = section[currentKey] + "\n" + IniConfigFile.ParseIniValue(trimmed);
                continue;
            }

            int separator = IniConfigFile.FindAssignmentSeparator(raw);
            if (currentSection != null && separator >= 0)
            {
                string key = IniConfigFile.ParseIniName(raw.Substring(0, separator));
                string value = IniConfigFile.ParseIniValue(raw.Substring(separator + 1));
                config.Set(currentSection, key, value);
                currentKey = key;
            }
        }

        return config;
    }

    public string Get(string section, string key, string defaultValue)
    {
        Dictionary<string, string> sectionData;
        if (!sections.TryGetValue(section, out sectionData))
        {
            return defaultValue;
        }

        string value;
        return sectionData.TryGetValue(key, out value) ? value : defaultValue;
    }

    public int GetInt(string section, string key, int defaultValue)
    {
        int value;
        return int.TryParse(Get(section, key, ""), NumberStyles.Integer, CultureInfo.InvariantCulture, out value)
            ? value
            : defaultValue;
    }

    public bool GetBool(string section, string key, bool defaultValue)
    {
        string value = Get(section, key, "").Trim();
        if (value.Length == 0)
        {
            return defaultValue;
        }

        return value.Equals("true", StringComparison.OrdinalIgnoreCase)
            || value.Equals("1", StringComparison.OrdinalIgnoreCase)
            || value.Equals("yes", StringComparison.OrdinalIgnoreCase)
            || value.Equals("on", StringComparison.OrdinalIgnoreCase);
    }

    public IEnumerable<string> GetSections()
    {
        return sectionOrder.ToArray();
    }

    public IEnumerable<string> GetKeys(string section)
    {
        List<string> keys;
        if (!keyOrder.TryGetValue(section, out keys))
        {
            return new string[0];
        }

        return keys.ToArray();
    }

    private void Set(string section, string key, string value)
    {
        EnsureSection(section);
        if (!sections[section].ContainsKey(key))
        {
            keyOrder[section].Add(key);
        }
        sections[section][key] = value;
    }

    private void EnsureSection(string section)
    {
        if (!sections.ContainsKey(section))
        {
            sections[section] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            keyOrder[section] = new List<string>();
            sectionOrder.Add(section);
        }
    }
}

internal sealed class DiscordIpcClient : IDisposable
{
    private const int OpHandshake = 0;
    private const int OpFrame = 1;
    private const int OpClose = 2;
    private const int OpPing = 3;
    private const int OpPong = 4;
    private const int PipeCount = 10;

    private readonly string clientId;
    private readonly bool verbose;
    private readonly IpcOptions options;
    private readonly object pipeLock = new object();
    private NamedPipeClientStream pipe;
    private string pipeName;

    public DiscordIpcClient(string clientId, bool verbose, IpcOptions options)
    {
        this.clientId = clientId;
        this.verbose = verbose;
        this.options = options ?? IpcOptions.FromConfig(new IniConfig());
    }

    public bool IsConnected
    {
        get { return pipe != null && pipe.IsConnected; }
    }

    public void Connect()
    {
        Close();

        if (clientId == null || clientId.Trim().Length == 0)
        {
            throw new IOException("Missing [general] client_id for Discord IPC.");
        }

        Exception lastError = null;
        for (int i = 0; i < PipeCount; i++)
        {
            string candidateName = "discord-ipc-" + i.ToString(CultureInfo.InvariantCulture);
            NamedPipeClientStream candidate = new NamedPipeClientStream(
                ".",
                candidateName,
                PipeDirection.InOut,
                PipeOptions.Asynchronous);

            try
            {
                candidate.Connect(options.ConnectTimeoutMs);
                pipe = candidate;
                pipeName = candidateName;

                Dictionary<string, object> handshake = new Dictionary<string, object>();
                handshake["v"] = 1;
                handshake["client_id"] = clientId.Trim();
                SendFrame(OpHandshake, handshake);

                string response = ReadFrame(options.ResponseTimeoutMs);
                if (response.IndexOf("\"evt\":\"READY\"", StringComparison.Ordinal) < 0 &&
                    response.IndexOf("\"cmd\":\"DISPATCH\"", StringComparison.Ordinal) < 0)
                {
                    throw new IOException("Unexpected Discord IPC handshake response: " + SummarizeJson(response));
                }

                Logger.Info("Connected to Discord IPC pipe " + pipeName + ".");
                return;
            }
            catch (Exception ex)
            {
                lastError = Unwrap(ex);
                try { candidate.Dispose(); } catch { }
                pipe = null;
                pipeName = null;
            }
        }

        string message = lastError != null ? lastError.Message : "no Discord IPC pipe found";
        throw new IOException("Discord IPC is unavailable. Make sure the Discord desktop app is running. Last error: " + message);
    }

    public void SetActivity(Dictionary<string, object> activity)
    {
        if (!IsConnected)
        {
            throw new IOException("Discord IPC is not connected.");
        }

        Dictionary<string, object> args = new Dictionary<string, object>();
        args["pid"] = Process.GetCurrentProcess().Id;
        args["activity"] = BuildIpcActivity(activity);

        Dictionary<string, object> payload = new Dictionary<string, object>();
        payload["cmd"] = "SET_ACTIVITY";
        payload["args"] = args;
        payload["nonce"] = Guid.NewGuid().ToString("D");

        SendFrame(OpFrame, payload);
        string response = ReadFrame(options.ResponseTimeoutMs);
        ThrowIfError(response);
        Logger.Info("Presence updated via Discord IPC.");
    }

    public void ClearActivity()
    {
        if (!IsConnected) return;

        Dictionary<string, object> args = new Dictionary<string, object>();
        args["pid"] = Process.GetCurrentProcess().Id;
        args["activity"] = null;

        Dictionary<string, object> payload = new Dictionary<string, object>();
        payload["cmd"] = "SET_ACTIVITY";
        payload["args"] = args;
        payload["nonce"] = Guid.NewGuid().ToString("D");

        SendFrame(OpFrame, payload);
    }

    public void Close()
    {
        NamedPipeClientStream oldPipe = pipe;
        pipe = null;
        pipeName = null;

        if (oldPipe != null)
        {
            try { oldPipe.Dispose(); } catch { }
        }
    }

    public void Dispose()
    {
        Close();
    }

    private void SendFrame(int opcode, object payload)
    {
        string json = Json.Serialize(payload);
        byte[] bytes = Encoding.UTF8.GetBytes(json);
        SendRawFrame(opcode, bytes);

        if (verbose && opcode == OpFrame)
        {
            Logger.Debug("IPC send: " + SummarizeRpcPayload(json));
        }
    }

    private void SendRawFrame(int opcode, byte[] bytes)
    {
        lock (pipeLock)
        {
            if (!IsConnected)
            {
                throw new IOException("Discord IPC pipe is not connected.");
            }

            byte[] header = new byte[8];
            Array.Copy(BitConverter.GetBytes(opcode), 0, header, 0, 4);
            Array.Copy(BitConverter.GetBytes(bytes.Length), 0, header, 4, 4);
            pipe.Write(header, 0, header.Length);
            pipe.Write(bytes, 0, bytes.Length);
            pipe.Flush();
        }
    }

    private string ReadFrame(int timeoutMs)
    {
        while (true)
        {
            byte[] header = new byte[8];
            ReadExact(header, 0, header.Length, timeoutMs);
            int opcode = BitConverter.ToInt32(header, 0);
            int length = BitConverter.ToInt32(header, 4);
            if (length < 0 || length > 1024 * 1024)
            {
                throw new IOException("Invalid Discord IPC frame length: " + length.ToString(CultureInfo.InvariantCulture));
            }

            byte[] payload = new byte[length];
            ReadExact(payload, 0, payload.Length, timeoutMs);
            string json = Encoding.UTF8.GetString(payload);

            if (verbose)
            {
                Logger.Debug("IPC recv: " + SummarizeRpcPayload(json));
            }

            if (opcode == OpPing)
            {
                SendRawFrame(OpPong, payload);
                continue;
            }

            if (opcode == OpClose)
            {
                throw new IOException("Discord IPC closed: " + SummarizeJson(json));
            }

            if (opcode != OpFrame)
            {
                throw new IOException("Unexpected Discord IPC opcode: " + opcode.ToString(CultureInfo.InvariantCulture));
            }

            return json;
        }
    }

    private void ReadExact(byte[] buffer, int offset, int count, int timeoutMs)
    {
        int totalRead = 0;
        Stopwatch sw = Stopwatch.StartNew();
        while (totalRead < count)
        {
            int remaining = timeoutMs - (int)sw.ElapsedMilliseconds;
            if (remaining <= 0)
            {
                throw new IOException("Timed out waiting for Discord IPC response.");
            }

            int read = ReadWithTimeout(buffer, offset + totalRead, count - totalRead, remaining);
            if (read <= 0)
            {
                throw new EndOfStreamException("Discord IPC pipe closed.");
            }

            totalRead += read;
        }
    }

    private int ReadWithTimeout(byte[] buffer, int offset, int count, int timeoutMs)
    {
        IAsyncResult asyncResult = pipe.BeginRead(buffer, offset, count, null, null);
        try
        {
            if (!asyncResult.AsyncWaitHandle.WaitOne(timeoutMs))
            {
                Close();
                throw new IOException("Timed out waiting for Discord IPC response.");
            }

            return pipe.EndRead(asyncResult);
        }
        finally
        {
            asyncResult.AsyncWaitHandle.Close();
        }
    }

    private static Dictionary<string, object> BuildIpcActivity(Dictionary<string, object> activity)
    {
        Dictionary<string, object> result = new Dictionary<string, object>();
        CopyIfPresent(activity, result, "name");
        CopyIfPresent(activity, result, "type");
        CopyIfPresent(activity, result, "details");
        CopyIfPresent(activity, result, "details_url");
        CopyIfPresent(activity, result, "state");
        CopyIfPresent(activity, result, "state_url");
        CopyIfPresent(activity, result, "timestamps");
        CopyIfPresent(activity, result, "assets");
        CopyIfPresent(activity, result, "party");
        CopyIfPresent(activity, result, "secrets");
        CopyIfPresent(activity, result, "buttons");

        if (!result.ContainsKey("instance") && activity.ContainsKey("flags"))
        {
            result["instance"] = true;
        }

        return result;
    }

    private static void CopyIfPresent(Dictionary<string, object> source, Dictionary<string, object> target, string key)
    {
        object value;
        if (source.TryGetValue(key, out value))
        {
            target[key] = value;
        }
    }

    private static void ThrowIfError(string response)
    {
        if (response.IndexOf("\"evt\":\"ERROR\"", StringComparison.Ordinal) >= 0)
        {
            throw new IOException("Discord IPC error: " + SummarizeJson(response));
        }
    }

    private static Exception Unwrap(Exception ex)
    {
        AggregateException ae = ex as AggregateException;
        if (ae != null)
        {
            ae = ae.Flatten();
            if (ae.InnerExceptions.Count > 0)
            {
                return ae.InnerExceptions[0];
            }
        }

        return ex;
    }

    private static string SummarizeRpcPayload(string json)
    {
        string cmd = ExtractString(json, "\"cmd\"");
        string evt = ExtractString(json, "\"evt\"");
        string nonce = ExtractString(json, "\"nonce\"");
        StringBuilder builder = new StringBuilder();
        if (cmd != null) builder.Append("cmd=").Append(cmd).Append(' ');
        if (evt != null) builder.Append("evt=").Append(evt).Append(' ');
        if (nonce != null) builder.Append("nonce=").Append(nonce).Append(' ');
        builder.Append("bytes=").Append(json.Length.ToString(CultureInfo.InvariantCulture));
        return builder.ToString().Trim();
    }

    private static string SummarizeJson(string json)
    {
        string message = ExtractString(json, "\"message\"");
        if (message != null && message.Length > 0)
        {
            return message;
        }

        return SummarizeRpcPayload(json);
    }

    private static string ExtractString(string json, string key)
    {
        int idx = json.IndexOf(key, StringComparison.Ordinal);
        if (idx < 0) return null;
        int colon = json.IndexOf(':', idx + key.Length);
        if (colon < 0) return null;
        int start = colon + 1;
        while (start < json.Length && json[start] == ' ') start++;
        if (start >= json.Length || json[start] != '"') return null;
        start++;
        StringBuilder builder = new StringBuilder();
        bool escaped = false;
        for (int i = start; i < json.Length; i++)
        {
            char ch = json[i];
            if (escaped)
            {
                switch (ch)
                {
                    case '"': builder.Append('"'); break;
                    case '\\': builder.Append('\\'); break;
                    case '/': builder.Append('/'); break;
                    case 'b': builder.Append('\b'); break;
                    case 'f': builder.Append('\f'); break;
                    case 'n': builder.Append('\n'); break;
                    case 'r': builder.Append('\r'); break;
                    case 't': builder.Append('\t'); break;
                    case 'u':
                        if (i + 4 >= json.Length)
                        {
                            return null;
                        }
                        string hex = json.Substring(i + 1, 4);
                        int codePoint;
                        if (!int.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out codePoint))
                        {
                            return null;
                        }
                        builder.Append((char)codePoint);
                        i += 4;
                        break;
                    default:
                        builder.Append(ch);
                        break;
                }
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (ch == '"')
            {
                return builder.ToString();
            }

            builder.Append(ch);
        }

        return null;
    }
}

internal sealed class ForegroundWindowInfo
{
    public readonly string Title;
    public readonly string ClassName;
    public readonly string ExeName;
    public readonly string ExePath;
    public readonly int Pid;

    public ForegroundWindowInfo(string title, string className, string exeName, string exePath, int pid)
    {
        Title     = title     ?? "";
        ClassName = className ?? "";
        ExeName   = exeName   ?? "";
        ExePath   = exePath   ?? "";
        Pid       = pid;
    }
}

internal static class WindowsInfo
{
    public static int GetInputIdleSeconds()
    {
        NativeMethods.LASTINPUTINFO info = new NativeMethods.LASTINPUTINFO();
        info.cbSize = (uint)Marshal.SizeOf(typeof(NativeMethods.LASTINPUTINFO));
        if (!NativeMethods.GetLastInputInfo(ref info))
        {
            return 0;
        }

        uint now = unchecked((uint)NativeMethods.GetTickCount64());
        uint idleMs = unchecked(now - info.dwTime);
        return (int)(idleMs / 1000);
    }

    public static SystemStats GetSystemStats(CpuSampler sampler)
    {
        double cpu = sampler.GetCpuPercent();

        NativeMethods.MEMORYSTATUSEX memory = new NativeMethods.MEMORYSTATUSEX();
        memory.dwLength = (uint)Marshal.SizeOf(typeof(NativeMethods.MEMORYSTATUSEX));
        double ramPercent = 0;
        double totalRamGb = 0;
        double usedRamGb  = 0;
        double availRamGb = 0;
        if (NativeMethods.GlobalMemoryStatusEx(ref memory))
        {
            ramPercent = memory.dwMemoryLoad;
            totalRamGb = Math.Round(memory.ullTotalPhys  / 1073741824.0, 1);
            availRamGb = Math.Round(memory.ullAvailPhys  / 1073741824.0, 1);
            usedRamGb  = Math.Round((memory.ullTotalPhys - memory.ullAvailPhys) / 1073741824.0, 1);
        }

        return new SystemStats(cpu, ramPercent, totalRamGb, usedRamGb, availRamGb);
    }

    public static ForegroundWindowInfo GetForegroundWindowInfo(string titleFallback)
    {
        IntPtr hwnd = NativeMethods.GetForegroundWindow();
        string title = titleFallback;
        string className = "";
        string exeName = "";
        string exePath = "";
        int pid = 0;

        if (hwnd != IntPtr.Zero)
        {
            // Title
            StringBuilder sb = new StringBuilder(512);
            if (NativeMethods.GetWindowTextW(hwnd, sb, sb.Capacity) > 0)
            {
                string t = sb.ToString();
                if (t.Trim().Length > 0) title = t;
            }

            // Class name
            sb.Clear();
            if (NativeMethods.GetClassNameW(hwnd, sb, 256) > 0)
            {
                className = sb.ToString();
            }

            // Process ID -> exe path
            uint procId = 0;
            NativeMethods.GetWindowThreadProcessId(hwnd, out procId);
            if (procId != 0)
            {
                pid = (int)procId;
                const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;
                IntPtr hProc = NativeMethods.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, procId);
                if (hProc != IntPtr.Zero)
                {
                    try
                    {
                        sb = new StringBuilder(1024);
                        uint size = (uint)sb.Capacity;
                        if (NativeMethods.QueryFullProcessImageNameW(hProc, 0, sb, ref size))
                        {
                            exePath = sb.ToString();
                            exeName = System.IO.Path.GetFileName(exePath);
                        }
                    }
                    finally
                    {
                        NativeMethods.CloseHandle(hProc);
                    }
                }
            }
        }

        return new ForegroundWindowInfo(title, className, exeName, exePath, pid);
    }

    public static BatteryInfo GetBatteryInfo()
    {
        NativeMethods.SYSTEM_POWER_STATUS status = new NativeMethods.SYSTEM_POWER_STATUS();
        if (!NativeMethods.GetSystemPowerStatus(out status))
        {
            return BatteryInfo.NoBattery();
        }

        bool hasBattery = status.BatteryLifePercent != 255 && (status.BatteryFlag & 128) == 0;
        if (!hasBattery)
        {
            return BatteryInfo.NoBattery();
        }

        bool plugged = status.ACLineStatus == 1;
        string remaining = "";
        if (!plugged && status.BatteryLifeTime > 0 && status.BatteryLifeTime <= 24 * 3600)
        {
            int hours = status.BatteryLifeTime / 3600;
            int minutes = (status.BatteryLifeTime % 3600) / 60;
            remaining = string.Format(CultureInfo.InvariantCulture, " ({0}h {1}m)", hours, minutes);
        }

        return new BatteryInfo(true, status.BatteryLifePercent, plugged, remaining);
    }

    public static long GetBootUnixTime()
    {
        DateTime bootUtc = DateTime.UtcNow.AddMilliseconds(-(double)NativeMethods.GetTickCount64());
        DateTime epoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        return (long)(bootUtc - epoch).TotalSeconds;
    }
}

internal sealed class CpuSampler
{
    private ulong lastIdle;
    private ulong lastKernel;
    private ulong lastUser;
    private bool hasSample;

    public double GetCpuPercent()
    {
        NativeMethods.FILETIME idleTime;
        NativeMethods.FILETIME kernelTime;
        NativeMethods.FILETIME userTime;
        if (!NativeMethods.GetSystemTimes(out idleTime, out kernelTime, out userTime))
        {
            return 0;
        }

        ulong idle = ToUInt64(idleTime);
        ulong kernel = ToUInt64(kernelTime);
        ulong user = ToUInt64(userTime);

        if (!hasSample)
        {
            lastIdle = idle;
            lastKernel = kernel;
            lastUser = user;
            hasSample = true;
            return 0;
        }

        ulong idleDelta = idle - lastIdle;
        ulong kernelDelta = kernel - lastKernel;
        ulong userDelta = user - lastUser;
        ulong totalDelta = kernelDelta + userDelta;

        lastIdle = idle;
        lastKernel = kernel;
        lastUser = user;

        if (totalDelta == 0)
        {
            return 0;
        }

        return Math.Max(0, Math.Min(100, (totalDelta - idleDelta) * 100.0 / totalDelta));
    }

    private static ulong ToUInt64(NativeMethods.FILETIME value)
    {
        return ((ulong)value.dwHighDateTime << 32) | value.dwLowDateTime;
    }
}

internal static class NativeMethods
{
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder text, int count);

    [DllImport("user32.dll")]
    public static extern bool GetLastInputInfo(ref LASTINPUTINFO info);

    [DllImport("kernel32.dll")]
    public static extern bool GetSystemTimes(out FILETIME idleTime, out FILETIME kernelTime, out FILETIME userTime);

    [DllImport("kernel32.dll")]
    public static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX buffer);

    [DllImport("kernel32.dll")]
    public static extern bool GetSystemPowerStatus(out SYSTEM_POWER_STATUS status);

    [DllImport("kernel32.dll")]
    public static extern ulong GetTickCount64();

    [DllImport("kernel32.dll")]
    public static extern IntPtr GetConsoleWindow();

    [DllImport("kernel32.dll")]
    public static extern uint GetConsoleProcessList([Out] uint[] processList, uint processCount);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [DllImport("kernel32.dll")]
    public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern bool QueryFullProcessImageNameW(IntPtr hProcess, uint dwFlags, StringBuilder lpExeName, ref uint lpdwSize);

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr hObject);

    [StructLayout(LayoutKind.Sequential)]
    public struct LASTINPUTINFO
    {
        public uint cbSize;
        public uint dwTime;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FILETIME
    {
        public uint dwLowDateTime;
        public uint dwHighDateTime;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORYSTATUSEX
    {
        public uint dwLength;
        public uint dwMemoryLoad;
        public ulong ullTotalPhys;
        public ulong ullAvailPhys;
        public ulong ullTotalPageFile;
        public ulong ullAvailPageFile;
        public ulong ullTotalVirtual;
        public ulong ullAvailVirtual;
        public ulong ullAvailExtendedVirtual;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SYSTEM_POWER_STATUS
    {
        public byte ACLineStatus;
        public byte BatteryFlag;
        public byte BatteryLifePercent;
        public byte SystemStatusFlag;
        public int BatteryLifeTime;
        public int BatteryFullLifeTime;
    }
}

internal sealed class SystemStats
{
    public readonly double CpuPercent;
    public readonly double RamPercent;
    public readonly double TotalRamGb;
    public readonly double UsedRamGb;
    public readonly double AvailRamGb;

    public SystemStats(double cpuPercent, double ramPercent, double totalRamGb, double usedRamGb, double availRamGb)
    {
        CpuPercent = cpuPercent;
        RamPercent = ramPercent;
        TotalRamGb = totalRamGb;
        UsedRamGb  = usedRamGb;
        AvailRamGb = availRamGb;
    }
}

internal sealed class BatteryInfo
{
    public readonly bool HasBattery;
    public readonly int Percent;
    public readonly bool Plugged;
    public readonly string RemainingText;

    public BatteryInfo(bool hasBattery, int percent, bool plugged, string remainingText)
    {
        HasBattery = hasBattery;
        Percent = percent;
        Plugged = plugged;
        RemainingText = remainingText ?? "";
    }

    public static BatteryInfo NoBattery()
    {
        return new BatteryInfo(false, 0, false, "");
    }
}

internal sealed class TemplateContext
{
    private readonly Dictionary<string, string> tokens;

    /// <summary>Raw idle seconds captured at construction time.</summary>
    public readonly int IdleSeconds;

    public TemplateContext(
        DateTime now,
        SystemStats stats,
        BatteryInfo battery,
        ForegroundWindowInfo window,
        int idleSeconds)
    {
        IdleSeconds = idleSeconds;
        tokens = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        // ---- Time ----
        tokens["time"]     = now.ToString("hh:mm tt",            CultureInfo.InvariantCulture);
        tokens["time_24"]  = now.ToString("HH:mm",               CultureInfo.InvariantCulture);
        tokens["time_h12"] = now.ToString("hh",                  CultureInfo.InvariantCulture);
        tokens["time_h24"] = now.ToString("HH",                  CultureInfo.InvariantCulture);
        tokens["time_min"] = now.ToString("mm",                  CultureInfo.InvariantCulture);
        tokens["time_sec"] = now.ToString("ss",                  CultureInfo.InvariantCulture);
        tokens["time_ampm"]= now.ToString("tt",                  CultureInfo.InvariantCulture);
        tokens["date"]     = now.ToString("yyyy-MM-dd",          CultureInfo.InvariantCulture);
        tokens["date_day"] = now.ToString("dd",                  CultureInfo.InvariantCulture);
        tokens["date_mon"] = now.ToString("MM",                  CultureInfo.InvariantCulture);
        tokens["date_yr"]  = now.ToString("yyyy",                CultureInfo.InvariantCulture);
        tokens["datetime"] = now.ToString("yyyy-MM-dd hh:mm tt", CultureInfo.InvariantCulture);

        // ---- CPU / RAM ----
        tokens["cpu"]       = stats.CpuPercent.ToString("0.#", CultureInfo.InvariantCulture) + "%";
        tokens["ram_pct"]   = stats.RamPercent.ToString("0.#", CultureInfo.InvariantCulture) + "%";
        tokens["ram_total"] = stats.TotalRamGb.ToString("0.#", CultureInfo.InvariantCulture) + "GB";
        tokens["ram_used"]  = stats.UsedRamGb.ToString("0.#",  CultureInfo.InvariantCulture) + "GB";
        tokens["ram_avail"] = stats.AvailRamGb.ToString("0.#", CultureInfo.InvariantCulture) + "GB";

        // ---- Uptime ----
        long upSec = (long)(NativeMethods.GetTickCount64() / 1000UL);
        tokens["uptime"]   = string.Format(CultureInfo.InvariantCulture, "{0}h {1}m", upSec / 3600, (upSec % 3600) / 60);
        tokens["uptime_h"] = (upSec / 3600).ToString(CultureInfo.InvariantCulture);
        tokens["uptime_m"] = ((upSec % 3600) / 60).ToString(CultureInfo.InvariantCulture);
        tokens["uptime_s"] = (upSec % 60).ToString(CultureInfo.InvariantCulture);

        // ---- Battery ----
        if (battery.HasBattery)
        {
            tokens["battery_pct"]       = battery.Percent.ToString(CultureInfo.InvariantCulture) + "%";
            tokens["battery_plug"]      = battery.Plugged ? "🔌" : "🔋";
            tokens["battery_remaining"] = battery.RemainingText;
        }
        else
        {
            tokens["battery_pct"]       = "";
            tokens["battery_plug"]      = "🖥️";
            tokens["battery_remaining"] = "";
        }

        // ---- Foreground window ----
        tokens["win_title"]    = window.Title;
        tokens["win_class"]    = window.ClassName;
        tokens["win_exe"]      = window.ExeName;
        tokens["win_exe_path"] = window.ExePath;
        tokens["win_pid"]      = window.Pid > 0 ? window.Pid.ToString(CultureInfo.InvariantCulture) : "";

        // ---- System info ----
        tokens["computer"] = Environment.MachineName;
        tokens["username"] = Environment.UserName;
        tokens["os"]       = Environment.OSVersion.VersionString;
        tokens["screen_w"] = SystemInformation.PrimaryMonitorSize.Width.ToString(CultureInfo.InvariantCulture);
        tokens["screen_h"] = SystemInformation.PrimaryMonitorSize.Height.ToString(CultureInfo.InvariantCulture);

        // ---- Idle / AFK ----
        // Component tokens (h:mm:ss decomposition — use these for display)
        tokens["idle_hh"] = (idleSeconds / 3600).ToString(CultureInfo.InvariantCulture);
        tokens["idle_mm"] = ((idleSeconds % 3600) / 60).ToString(CultureInfo.InvariantCulture);
        tokens["idle_ss"] = (idleSeconds % 60).ToString(CultureInfo.InvariantCulture);
        // Total tokens (raw totals — useful for arithmetic comparisons in templates)
        tokens["idle_total_h"] = (idleSeconds / 3600).ToString(CultureInfo.InvariantCulture);
        tokens["idle_total_m"] = (idleSeconds / 60).ToString(CultureInfo.InvariantCulture);
        tokens["idle_total_s"] = idleSeconds.ToString(CultureInfo.InvariantCulture);

        // ---- Escape / special ----
        tokens["empty"] = "";
    }

    /// <summary>
    /// Expands {token} placeholders. Use {{ and }} for literal braces.
    /// Unknown tokens are left as-is.
    /// </summary>
    public string Format(string template)
    {
        if (template == null) return "";
        return Regex.Replace(template, @"\{\{|\}\}|\{([^{}]*)\}", delegate(Match m)
        {
            if (m.Value == "{{") return "{";
            if (m.Value == "}}") return "}";
            string key = m.Groups[1].Value;
            string val;
            return tokens.TryGetValue(key, out val) ? val : m.Value;
        }).Trim();
    }

    /// <summary>
    /// Returns a read-only snapshot of all tokens (for debug / dry-run listing).
    /// </summary>
    public IEnumerable<KeyValuePair<string, string>> GetTokens()
    {
        return tokens;
    }

    public IEnumerable<KeyValuePair<string, string>> GetTokens(bool redactSensitive)
    {
        if (!redactSensitive)
        {
            return GetTokens();
        }

        Dictionary<string, string> copy = new Dictionary<string, string>(tokens, StringComparer.OrdinalIgnoreCase);
        foreach (string key in SensitiveTokenKeys())
        {
            if (copy.ContainsKey(key) && copy[key].Length > 0)
            {
                copy[key] = "<redacted:" + key + ">";
            }
        }

        return copy;
    }

    public Dictionary<string, string> GetSensitiveTokenValues()
    {
        Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (string key in SensitiveTokenKeys())
        {
            string value;
            if (tokens.TryGetValue(key, out value) && !string.IsNullOrEmpty(value))
            {
                values[key] = value;
            }
        }

        return values;
    }

    private static string[] SensitiveTokenKeys()
    {
        return new string[]
        {
            "win_title",
            "win_class",
            "win_exe",
            "win_exe_path",
            "win_pid",
            "computer",
            "username",
            "os"
        };
    }
}

internal sealed class AssetInfo
{
    public readonly string Image;
    public readonly string Text;

    public AssetInfo(string image, string text)
    {
        Image = image ?? "";
        Text = text ?? "";
    }
}

internal static class AppPaths
{
    public static readonly string ExecutablePath = Assembly.GetEntryAssembly().Location;
    public static readonly string ExecutableDirectory = GetExecutableDirectory();
    public static readonly string ExecutableBaseName = GetExecutableBaseName();
    public static readonly string DefaultIniPath = Path.Combine(ExecutableDirectory, ExecutableBaseName + ".ini");
    public static readonly string DefaultLogPath = Path.Combine(ExecutableDirectory, ExecutableBaseName + ".log");

    private static string GetExecutableDirectory()
    {
        string dir = Path.GetDirectoryName(ExecutablePath);
        return string.IsNullOrEmpty(dir) ? AppDomain.CurrentDomain.BaseDirectory : dir;
    }

    private static string GetExecutableBaseName()
    {
        string name = Path.GetFileNameWithoutExtension(ExecutablePath);
        return string.IsNullOrEmpty(name) ? "DiscordRPC" : name;
    }
}

internal static class Logger
{
    private static readonly object SyncRoot = new object();
    private static readonly Queue<string> RecentLines = new Queue<string>();
    private const int RecentLimit = 250;
    private static string filePath;
    private static bool fileLoggingEnabled;

    public static bool Verbose;
    public static bool LoggingEnabled = true;

    public static string FilePath
    {
        get
        {
            lock (SyncRoot)
            {
                return filePath ?? "";
            }
        }
    }

    public static void ConfigureFile(string configPath, IniConfig config)
    {
        if (config == null)
        {
            return;
        }

        bool enabled = config.GetBool("app", "file_logging_enabled", true);
        string configured = config.Get("app", "log_path", "").Trim();
        string path = configured;
        if (path.Length == 0)
        {
            path = AppPaths.DefaultLogPath;
        }
        else if (!Path.IsPathRooted(path))
        {
            path = Path.Combine(AppPaths.ExecutableDirectory, path);
        }

        lock (SyncRoot)
        {
            fileLoggingEnabled = enabled;
            filePath = path;
        }
    }

    public static void Info(string message)
    {
        Write(Console.Out, "INFO", message);
    }

    public static void Error(string message)
    {
        Write(Console.Error, "ERROR", message);
    }

    public static void Debug(string message)
    {
        if (Verbose)
        {
            Write(Console.Out, "DEBUG", message);
        }
    }

    public static string GetRecentText()
    {
        lock (SyncRoot)
        {
            if (RecentLines.Count == 0)
            {
                return UiStrings.Get("no_recent_log");
            }

            StringBuilder builder = new StringBuilder();
            foreach (string line in RecentLines)
            {
                builder.AppendLine(line);
            }

            return builder.ToString();
        }
    }

    private static void Write(TextWriter writer, string level, string message)
    {
        if (!LoggingEnabled && !level.Equals("ERROR", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        lock (SyncRoot)
        {
            string line = string.Format(
                CultureInfo.InvariantCulture,
                "[{0}] [{1}] {2}",
                DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture),
                level,
                message);
            RecentLines.Enqueue(line);
            while (RecentLines.Count > RecentLimit)
            {
                RecentLines.Dequeue();
            }

            writer.WriteLine(line);

            if (fileLoggingEnabled && filePath != null && filePath.Length > 0)
            {
                try
                {
                    string dir = Path.GetDirectoryName(filePath);
                    if (dir != null && dir.Length > 0)
                    {
                        Directory.CreateDirectory(dir);
                    }

                    File.AppendAllText(filePath, line + Environment.NewLine, Encoding.UTF8);
                }
                catch
                {
                }
            }
        }
    }
}

internal static class Json
{
    public static string Serialize(object value)
    {
        StringBuilder builder = new StringBuilder();
        WriteValue(builder, value);
        return builder.ToString();
    }

    private static void WriteValue(StringBuilder builder, object value)
    {
        if (value == null)
        {
            builder.Append("null");
            return;
        }

        string text = value as string;
        if (text != null)
        {
            WriteString(builder, text);
            return;
        }

        if (value is bool)
        {
            builder.Append((bool)value ? "true" : "false");
            return;
        }

        if (value is byte || value is sbyte || value is short || value is ushort || value is int || value is uint
            || value is long || value is ulong || value is float || value is double || value is decimal)
        {
            IFormattable formattable = (IFormattable)value;
            builder.Append(formattable.ToString(null, CultureInfo.InvariantCulture));
            return;
        }

        IDictionary<string, object> dictionary = value as IDictionary<string, object>;
        if (dictionary != null)
        {
            WriteDictionary(builder, dictionary);
            return;
        }

        IEnumerable enumerable = value as IEnumerable;
        if (enumerable != null)
        {
            WriteArray(builder, enumerable);
            return;
        }

        WriteString(builder, value.ToString());
    }

    private static void WriteDictionary(StringBuilder builder, IDictionary<string, object> dictionary)
    {
        builder.Append('{');
        bool first = true;
        foreach (KeyValuePair<string, object> item in dictionary)
        {
            if (!first)
            {
                builder.Append(',');
            }

            first = false;
            WriteString(builder, item.Key);
            builder.Append(':');
            WriteValue(builder, item.Value);
        }

        builder.Append('}');
    }

    private static void WriteArray(StringBuilder builder, IEnumerable values)
    {
        builder.Append('[');
        bool first = true;
        foreach (object value in values)
        {
            if (!first)
            {
                builder.Append(',');
            }

            first = false;
            WriteValue(builder, value);
        }

        builder.Append(']');
    }

    private static void WriteString(StringBuilder builder, string value)
    {
        builder.Append('"');
        for (int i = 0; i < value.Length; i++)
        {
            char ch = value[i];
            switch (ch)
            {
                case '"':
                    builder.Append("\\\"");
                    break;
                case '\\':
                    builder.Append("\\\\");
                    break;
                case '\b':
                    builder.Append("\\b");
                    break;
                case '\f':
                    builder.Append("\\f");
                    break;
                case '\n':
                    builder.Append("\\n");
                    break;
                case '\r':
                    builder.Append("\\r");
                    break;
                case '\t':
                    builder.Append("\\t");
                    break;
                default:
                    if (ch < 32)
                    {
                        builder.Append("\\u");
                        builder.Append(((int)ch).ToString("x4", CultureInfo.InvariantCulture));
                    }
                    else
                    {
                        builder.Append(ch);
                    }
                    break;
            }
        }

        builder.Append('"');
    }
}

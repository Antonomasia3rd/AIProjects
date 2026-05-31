using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.RegularExpressions;
using System.Threading;

static class DNSAutoUpdate
{
    sealed class Options
    {
        public string ZoneName = "server.local";
        public List<string> SubFolder = new List<string>();
        public List<string> ManagedRecordName = new List<string>();
        public bool NoRootRecord;
        public string LogFile;
        public int MaxLogMegabytes = 10;
        public int LogRetentionCount = 5;
        public int SleepSeconds = 20;
        public List<string> IncludeInterfaceAlias = new List<string>();
        public List<string> ExcludeInterfaceAlias = new List<string> { "Loopback*", "vEthernet*", "VMware*", "VirtualBox*", "Bluetooth*" };
        public List<string> IncludeIPAddress = new List<string>();
        public bool IncludeUnpreferred;
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
            Run(options);
            return 0;
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
        bool subFolderProvided = false;
        for (int i = 0; i < args.Length; ++i)
        {
            string a = args[i];
            if (Is(a, "-h") || Is(a, "--help") || Is(a, "/?")) { o.Help = true; continue; }
            if (Is(a, "-NoRootRecord") || Is(a, "--no-root-record")) { o.NoRootRecord = true; continue; }
            if (Is(a, "-IncludeUnpreferred") || Is(a, "--include-unpreferred")) { o.IncludeUnpreferred = true; continue; }
            if (Is(a, "-WhatIf") || Is(a, "--what-if")) { o.WhatIf = true; continue; }
            if (Is(a, "-Confirm") || Is(a, "--confirm")) { o.Confirm = true; continue; }

            string value = i + 1 < args.Length ? args[++i] : "";
            if (Is(a, "-ZoneName") || Is(a, "--zone-name")) o.ZoneName = value;
            else if (Is(a, "-SubFolder") || Is(a, "--sub-folder")) { AddList(o.SubFolder, value); subFolderProvided = true; }
            else if (Is(a, "-ManagedRecordName") || Is(a, "--managed-record-name")) AddList(o.ManagedRecordName, value);
            else if (Is(a, "-LogFile") || Is(a, "--log-file")) o.LogFile = value;
            else if (Is(a, "-MaxLogMegabytes") || Is(a, "--max-log-megabytes")) o.MaxLogMegabytes = Clamp(ParseInt(value, a), 0, 1048576);
            else if (Is(a, "-LogRetentionCount") || Is(a, "--log-retention-count")) o.LogRetentionCount = Clamp(ParseInt(value, a), 0, 100);
            else if (Is(a, "-SleepSeconds") || Is(a, "--sleep-seconds")) o.SleepSeconds = Clamp(ParseInt(value, a), 1, 86400);
            else if (Is(a, "-IncludeInterfaceAlias") || Is(a, "--include-interface-alias")) AddList(o.IncludeInterfaceAlias, value);
            else if (Is(a, "-ExcludeInterfaceAlias") || Is(a, "--exclude-interface-alias")) { o.ExcludeInterfaceAlias.Clear(); AddList(o.ExcludeInterfaceAlias, value); }
            else if (Is(a, "-IncludeIPAddress") || Is(a, "--include-ip-address")) AddList(o.IncludeIPAddress, value);
            else throw new ArgumentException("Unknown argument: " + a);
        }

        if (!subFolderProvided)
            o.SubFolder.Add("");

        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        if (String.IsNullOrWhiteSpace(o.LogFile))
            o.LogFile = Path.Combine(baseDir, "DNSAutoUpdate.log");
        else if (!Path.IsPathRooted(o.LogFile))
            o.LogFile = Path.Combine(baseDir, o.LogFile);
        return o;
    }

    static void Usage()
    {
        Console.WriteLine("Usage:");
        Console.WriteLine("  DNSAutoUpdate.exe -ZoneName server.local [-ManagedRecordName @,app] [-SleepSeconds 60] [-WhatIf]");
        Console.WriteLine("Requires dnscmd.exe and permission to update the DNS zone.");
    }

    static void Run(Options o)
    {
        var managed = BuildManagedRecordNames(o);
        if (managed.Count == 0)
            throw new InvalidOperationException("No managed DNS owner names were selected. Remove -NoRootRecord, pass -SubFolder, or pass -ManagedRecordName.");

        Log(o, "=============================================");
        Log(o, " DNS Auto-Update Background Service Starting ");
        Log(o, " Zone: " + o.ZoneName);
        Log(o, " Managed exact A record owner names: " + String.Join(", ", managed));
        Log(o, " Records outside this allowlist are ignored.");
        Log(o, "=============================================");

        while (true)
        {
            Log(o, "Starting scan cycle...");
            var serverIps = GetEligibleServerIPv4(o);
            if (serverIps.Count == 0)
            {
                Log(o, "No eligible server IPv4 addresses detected; skipping DNS changes this cycle.");
                Sleep(o);
                continue;
            }

            Log(o, "Eligible server IPs detected: " + String.Join(", ", serverIps));
            foreach (string recordName in managed)
                SyncARecords(o, recordName, serverIps);
            Sleep(o);
        }
    }

    static List<string> BuildManagedRecordNames(Options o)
    {
        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        if (o.ManagedRecordName.Count > 0)
        {
            foreach (string name in o.ManagedRecordName.Select(NormalizeRecordName).Where(s => s.Length > 0))
                set.Add(name);
        }
        else
        {
            if (!o.NoRootRecord)
                set.Add("@");
            foreach (string name in o.SubFolder.Select(NormalizeRecordName).Where(s => s.Length > 0 && s != "@"))
                set.Add(name);
        }
        return set.ToList();
    }

    static void SyncARecords(Options o, string recordName, List<string> serverIps)
    {
        List<string> existing;
        try
        {
            existing = GetExactARecords(o, recordName);
        }
        catch (Exception ex)
        {
            Log(o, "ERROR reading A records for '" + recordName + "'; skipping this owner name this cycle: " + ex.Message);
            return;
        }

        Log(o, "Found " + existing.Count + " A records for '" + recordName + "'");
        foreach (string ip in existing)
        {
            Log(o, " Checking '" + recordName + "' -> " + ip);
            if (!serverIps.Contains(ip))
            {
                Log(o, "   OUTDATED IP detected, removing " + ip);
                if (ShouldApply(o, o.ZoneName + "/" + recordName + " " + ip, "Remove stale A record"))
                    RunDnsCmd(o, "/RecordDelete " + Quote(o.ZoneName) + " " + Quote(recordName) + " A " + Quote(ip) + " /f", "   Removed successfully.");
                else
                    Log(o, "   Removal skipped.");
            }
            else
            {
                Log(o, "   IP is valid.");
            }
        }

        try
        {
            existing = GetExactARecords(o, recordName);
        }
        catch (Exception ex)
        {
            Log(o, "ERROR re-reading A records for '" + recordName + "' after removals; skipping additions this cycle: " + ex.Message);
            return;
        }

        Log(o, "Ensuring correct IP set for '" + recordName + "'");
        foreach (string ip in serverIps)
        {
            if (!existing.Contains(ip))
            {
                Log(o, "   Adding missing IP " + ip);
                if (ShouldApply(o, o.ZoneName + "/" + recordName + " " + ip, "Add missing A record"))
                    RunDnsCmd(o, "/RecordAdd " + Quote(o.ZoneName) + " " + Quote(recordName) + " A " + Quote(ip), "   Added successfully.");
                else
                    Log(o, "   Add skipped.");
            }
        }
    }

    static List<string> GetExactARecords(Options o, string recordName)
    {
        var result = RunProcess("dnscmd.exe", ". /EnumRecords " + Quote(o.ZoneName) + " " + Quote(recordName) + " /Type A", 60000);
        string combined = result.Output + "\n" + result.Error;
        if (result.ExitCode != 0)
        {
            if (Regex.IsMatch(combined, "(?i)DNS_ERROR_(RECORD|NAME)_DOES_NOT_EXIST|not found|does not exist|9701|9714|9003"))
                return new List<string>();
            throw new InvalidOperationException(combined.Trim());
        }

        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (Match m in Regex.Matches(combined, @"\b(?:\d{1,3}\.){3}\d{1,3}\b"))
        {
            string ip = NormalizeIP(m.Value);
            IPAddress parsed;
            if (IPAddress.TryParse(ip, out parsed))
                set.Add(ip);
        }
        return set.ToList();
    }

    static void RunDnsCmd(Options o, string args, string success)
    {
        var result = RunProcess("dnscmd.exe", ". " + args, 60000);
        if (result.ExitCode == 0)
            Log(o, success);
        else
            Log(o, "   ERROR: " + (result.Output + " " + result.Error).Trim());
    }

    static List<string> GetEligibleServerIPv4(Options o)
    {
        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        if (o.IncludeIPAddress.Count > 0)
        {
            foreach (string raw in o.IncludeIPAddress)
            {
                string ip = NormalizeIP(raw);
                if (IsUsableIPv4(ip))
                    set.Add(ip);
            }
            return set.ToList();
        }

        foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            string alias = ni.Name ?? "";
            if (o.IncludeInterfaceAlias.Count > 0 && !WildcardAny(alias, o.IncludeInterfaceAlias))
                continue;
            if (o.ExcludeInterfaceAlias.Count > 0 && WildcardAny(alias, o.ExcludeInterfaceAlias))
                continue;

            foreach (UnicastIPAddressInformation addr in ni.GetIPProperties().UnicastAddresses)
            {
                if (addr.Address.AddressFamily != AddressFamily.InterNetwork)
                    continue;
                if (!o.IncludeUnpreferred && addr.DuplicateAddressDetectionState != DuplicateAddressDetectionState.Preferred)
                    continue;
                string ip = NormalizeIP(addr.Address.ToString());
                if (IsUsableIPv4(ip))
                    set.Add(ip);
            }
        }
        return set.ToList();
    }

    static bool ShouldApply(Options o, string target, string action)
    {
        if (o.WhatIf)
        {
            Log(o, "WHATIF: " + action + " -> " + target);
            return false;
        }
        if (!o.Confirm)
            return true;
        Console.Write(action + " " + target + "? [y/N] ");
        string answer = Console.ReadLine() ?? "";
        return answer.Equals("y", StringComparison.OrdinalIgnoreCase) || answer.Equals("yes", StringComparison.OrdinalIgnoreCase);
    }

    static void Sleep(Options o)
    {
        Log(o, "Cycle complete. Sleeping for " + o.SleepSeconds + " seconds.");
        Log(o, "---------------------------------------------");
        Thread.Sleep(TimeSpan.FromSeconds(o.SleepSeconds));
    }

    static void Log(Options o, string msg)
    {
        string line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + msg;
        try
        {
            RotateLogIfNeeded(o);
            string dir = Path.GetDirectoryName(o.LogFile);
            if (!String.IsNullOrWhiteSpace(dir))
                Directory.CreateDirectory(dir);
            File.AppendAllText(o.LogFile, line + Environment.NewLine);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("WARNING: Could not write DNSAutoUpdate log '" + o.LogFile + "': " + ex.Message);
        }
        Console.WriteLine(line);
    }

    static void RotateLogIfNeeded(Options o)
    {
        if (o.MaxLogMegabytes <= 0 || String.IsNullOrWhiteSpace(o.LogFile) || !File.Exists(o.LogFile))
            return;
        long maxBytes = (long)o.MaxLogMegabytes * 1024L * 1024L;
        if (new FileInfo(o.LogFile).Length < maxBytes)
            return;
        if (o.LogRetentionCount <= 0)
        {
            File.Delete(o.LogFile);
            return;
        }
        for (int i = o.LogRetentionCount; i >= 1; --i)
        {
            string source = i == 1 ? o.LogFile : o.LogFile + "." + (i - 1);
            string dest = o.LogFile + "." + i;
            if (!File.Exists(source))
                continue;
            if (i == o.LogRetentionCount && File.Exists(dest))
                File.Delete(dest);
            File.Move(source, dest);
        }
    }

    static ProcessResult RunProcess(string file, string args, int timeoutMs)
    {
        var psi = new ProcessStartInfo(file, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        using (var p = Process.Start(psi))
        {
            string output = p.StandardOutput.ReadToEnd();
            string error = p.StandardError.ReadToEnd();
            if (!p.WaitForExit(timeoutMs))
            {
                try { p.Kill(); } catch { }
                throw new TimeoutException(file + " timed out.");
            }
            return new ProcessResult { ExitCode = p.ExitCode, Output = output, Error = error };
        }
    }

    sealed class ProcessResult
    {
        public int ExitCode;
        public string Output;
        public string Error;
    }

    static string NormalizeIP(string ip) { return Regex.Replace((ip ?? "").Trim(), @"/\d+$", ""); }
    static string NormalizeRecordName(string name) { string n = (name ?? "").Trim(); return n.Length == 0 ? "@" : n; }
    static bool IsUsableIPv4(string ip) { return ip.Length > 0 && !ip.StartsWith("127.") && !ip.StartsWith("169.254.") && ip != "0.0.0.0"; }
    static bool WildcardAny(string value, List<string> patterns) { return patterns.Any(p => Wildcard(value, p)); }
    static bool Wildcard(string value, string pattern) { return Regex.IsMatch(value ?? "", "^" + Regex.Escape(pattern ?? "").Replace("\\*", ".*").Replace("\\?", ".") + "$", RegexOptions.IgnoreCase); }
    static string Quote(string value) { return "\"" + (value ?? "").Replace("\"", "\\\"") + "\""; }
    static bool Is(string a, string b) { return String.Equals(a, b, StringComparison.OrdinalIgnoreCase); }
    static int ParseInt(string value, string name) { int n; if (!Int32.TryParse(value, out n)) throw new ArgumentException("Invalid integer for " + name + ": " + value); return n; }
    static int Clamp(int value, int min, int max) { return Math.Max(min, Math.Min(max, value)); }
    static void AddList(List<string> list, string value) { foreach (string item in (value ?? "").Split(',')) if (!String.IsNullOrWhiteSpace(item)) list.Add(item.Trim()); }
}

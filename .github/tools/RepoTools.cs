using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

sealed class Project
{
    public string key { get; set; }
    public string label { get; set; }
    public string folder { get; set; }
    public string buildOutput { get; set; }
    public string artifactName { get; set; }
    public string artifactPath { get; set; }
    public string[] artifactPaths { get; set; }
    public string smokePath { get; set; }
    public string skipKey { get; set; }
    public string[] dependencyPaths { get; set; }
    public string releaseTagEnvironment { get; set; }
}

sealed class GitHubReleaseAssetInfo
{
    public int downloadCount { get; set; }
}

sealed class ProcessResult
{
    public int ExitCode;
    public string Output = "";
    public string Error = "";
}

sealed class TileTextSmokeRegion
{
    public string Size;
    public int Mask;
    public int Source;
    public double X;
    public double Y;
    public double Width;
    public double Height;
}

sealed class TilePixelSnapshot
{
    public int Width;
    public int Height;
    public int[] Pixels;
}

sealed class GitHubReleaseInfo
{
    public string tagName { get; set; }
    public bool isDraft { get; set; }
    public List<GitHubReleaseAssetInfo> assets { get; set; }
}

static class RepoTools
{
    // Intentionally retained until process exit. Closing the last handle kills
    // this job, including this process, so normal managed disposal is unsuitable.
    static IntPtr smokeLifetimeJob;
    static Timer smokeDeadline;
    static bool allowPackageIntegration;

    [StructLayout(LayoutKind.Sequential)]
    struct BasicJobLimits {
        public long ProcessTime, JobTime;
        public uint Flags;
        public UIntPtr MinimumWorkingSet, MaximumWorkingSet;
        public uint ActiveProcesses;
        public UIntPtr Affinity;
        public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct JobIoCounters { public ulong ReadOperations, WriteOperations, OtherOperations, ReadBytes, WriteBytes, OtherBytes; }
    [StructLayout(LayoutKind.Sequential)]
    struct ExtendedJobLimits {
        public BasicJobLimits Basic;
        public JobIoCounters Io;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedJobLimits info, uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    static void InstallSmokeLifetime(int deadlineMs)
    {
        if (smokeLifetimeJob != IntPtr.Zero) return;
        IntPtr job = CreateJobObject(IntPtr.Zero, null); // non-inheritable handle
        if (job == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot create smoke lifetime job.");
        var limits = new ExtendedJobLimits();
        limits.Basic.Flags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(limits)) ||
            !AssignProcessToJobObject(job, GetCurrentProcess())) {
            int error = Marshal.GetLastWin32Error();
            CloseHandle(job);
            throw new Win32Exception(error, "Cannot isolate smoke children; no applications will be launched.");
        }
        smokeLifetimeJob = job;
        // Children inherit membership from birth. Even abrupt parent death
        // closes the sole job handle and kills descendants without finally.
        smokeDeadline = new Timer(delegate {
            Console.Error.WriteLine("Smoke runner exceeded its total deadline.");
            Environment.Exit(124);
        }, null, deadlineMs, Timeout.Infinite);
    }

    static Process StartInertHelper(params string[] args)
    {
        string exe = Process.GetCurrentProcess().MainModule.FileName;
        var process = new Process { StartInfo = new ProcessStartInfo(exe, JoinArgs(args)) {
            UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = Path.GetDirectoryName(exe)
        }};
        if (!process.Start()) throw new InvalidOperationException("Cannot start inert smoke fixture.");
        return process;
    }

    static int SmokeInertChild(string[] args)
    {
        string marker = Option(args, "--marker");
        if (String.IsNullOrWhiteSpace(marker)) return 2;
        string grandchildMarker = Option(args, "--grandchild");
        if (!String.IsNullOrWhiteSpace(grandchildMarker))
            using (StartInertHelper("smoke-inert-child", "--marker", grandchildMarker)) { }
        File.WriteAllText(marker, "ready:" + Process.GetCurrentProcess().Id.ToString(CultureInfo.InvariantCulture));
        Thread.Sleep(20000); // independent upper bound even if launched by hand
        return 0;
    }

    static int SmokeCleanupFixture(string[] args)
    {
        InstallSmokeLifetime(25000);
        string root = Option(args, "--fixture-root");
        if (String.IsNullOrWhiteSpace(root) || !Directory.Exists(root)) return 2;
        using (StartInertHelper("smoke-inert-child", "--marker", Path.Combine(root, "child.pid"),
            "--grandchild", Path.Combine(root, "grandchild.pid"))) { }
        WaitForFileText(Path.Combine(root, "release"), "release", 15000, "inert fixture release");
        return 0;
    }

    static Process ReadInertProcess(string marker)
    {
        WaitForFileText(marker, "ready:", 5000, "inert child marker");
        int id = Int32.Parse(File.ReadAllText(marker).Substring(6).Trim(), CultureInfo.InvariantCulture);
        var process = Process.GetProcessById(id);
        if (!String.Equals(process.MainModule.FileName, Process.GetCurrentProcess().MainModule.FileName, StringComparison.OrdinalIgnoreCase)) {
            process.Dispose();
            throw new InvalidOperationException("Inert fixture PID did not identify this helper executable.");
        }
        return process;
    }

    static int SmokeCleanupTests()
    {
        InstallSmokeLifetime(60000);
        string root = Path.Combine(Path.GetTempPath(), "AIProjects-SmokeCleanup-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try {
            foreach (bool interrupt in new[] { false, true }) {
                string fixtureRoot = Path.Combine(root, interrupt ? "interrupted" : "normal");
                Directory.CreateDirectory(fixtureRoot);
                using (var owner = StartInertHelper("smoke-cleanup-fixture", "--fixture-root", fixtureRoot))
                using (var child = ReadInertProcess(Path.Combine(fixtureRoot, "child.pid")))
                using (var grandchild = ReadInertProcess(Path.Combine(fixtureRoot, "grandchild.pid"))) {
                    if (interrupt) owner.Kill();
                    else File.WriteAllText(Path.Combine(fixtureRoot, "release"), "release");
                    if (!owner.WaitForExit(5000) || !child.WaitForExit(5000) || !grandchild.WaitForExit(5000))
                        throw new InvalidOperationException("Job cleanup left an inert child or grandchild running.");
                    Console.WriteLine("ok - " + (interrupt ? "forced runner termination" : "normal runner exit") + " reaps child and grandchild");
                }
            }
            return 0;
        } finally {
            try { Directory.Delete(root, true); } catch { }
            GC.KeepAlive(smokeDeadline);
        }
    }
    static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0 || args[0] == "--help" || args[0] == "/?")
            {
                Usage();
                return args.Length == 0 ? 2 : 0;
            }

            string command = args[0].ToLowerInvariant();
            string[] rest = args.Skip(1).ToArray();
            if (command == "smoke-cleanup-tests") return SmokeCleanupTests();
            if (command == "smoke-cleanup-fixture") return SmokeCleanupFixture(rest);
            if (command == "smoke-inert-child") return SmokeInertChild(rest);
            if (command == "validate-project-map") return ValidateProjectMap(rest);
            if (command == "test-workflow-project-selection") return TestWorkflowProjectSelection(rest);
            if (command == "policy-warnings") return InvokePolicyWarnings(rest);
            if (command == "readme-consistency") return TestReadmeConsistency(rest);
            if (command == "detect-projects") return DetectProjects(rest);
            if (command == "checksum-summary") return ChecksumSummary(rest);
            if (command == "smoke-windows-build") return SmokeWindowsBuild(rest);
            if (command == "prepare-release-versions") return PrepareReleaseVersions(rest);
            if (command == "publish-project-releases") return PublishProjectReleases(rest);
            if (command == "version")
            {
                Console.WriteLine("RepoTools native CI helper");
                return 0;
            }

            throw new InvalidOperationException("Unknown command: " + args[0]);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR: " + ex.Message);
            return 1;
        }
    }

    static void Usage()
    {
        Console.WriteLine("RepoTools commands:");
        Console.WriteLine("  validate-project-map");
        Console.WriteLine("  test-workflow-project-selection");
        Console.WriteLine("  policy-warnings");
        Console.WriteLine("  readme-consistency");
        Console.WriteLine("  detect-projects");
        Console.WriteLine("  checksum-summary");
        Console.WriteLine("  smoke-windows-build [--projects All|Project1,Project2] [--allow-package-integration]");
        Console.WriteLine("    [--desktopstub-binary path] [--desktopstub-broker path]");
        Console.WriteLine("  smoke-cleanup-tests (temporary inert processes only)");
        Console.WriteLine("  prepare-release-versions");
        Console.WriteLine("  publish-project-releases");
    }

    static string Option(string[] args, string name, string fallback = "")
    {
        for (int i = 0; i < args.Length; ++i)
        {
            string arg = args[i];
            if (arg.Equals(name, StringComparison.OrdinalIgnoreCase) ||
                arg.Equals("-" + name.TrimStart('-'), StringComparison.OrdinalIgnoreCase) ||
                arg.Equals("/" + name.TrimStart('-'), StringComparison.OrdinalIgnoreCase))
            {
                return i + 1 < args.Length ? args[i + 1] : fallback;
            }

            string prefix = name + "=";
            if (arg.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                return arg.Substring(prefix.Length);
        }
        return fallback;
    }

    static string RepositoryRoot(string[] args)
    {
        string explicitRoot = Option(args, "--repository-root");
        if (!String.IsNullOrWhiteSpace(explicitRoot))
            return Path.GetFullPath(explicitRoot);

        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        return Path.GetFullPath(Path.Combine(baseDir, "..", ".."));
    }

    static string ReadAll(string path)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("Missing file", path);
        return File.ReadAllText(path, Encoding.UTF8);
    }

    static List<Project> LoadProjectMap(string root)
    {
        string path = Path.Combine(root, ".github", "project-map.json");
        var serializer = new JavaScriptSerializer();
        var projects = serializer.Deserialize<List<Project>>(ReadAll(path));
        if (projects == null || projects.Count == 0)
            throw new InvalidOperationException("Project map must contain at least one project.");
        return projects;
    }

    static string Rel(string root, string fullPath)
    {
        string r = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string f = Path.GetFullPath(fullPath);
        return f.StartsWith(r, StringComparison.OrdinalIgnoreCase) ? f.Substring(r.Length) : f;
    }

    static void AppendSummary(IEnumerable<string> lines)
    {
        string summary = Environment.GetEnvironmentVariable("GITHUB_STEP_SUMMARY");
        if (String.IsNullOrWhiteSpace(summary))
            return;
        File.AppendAllText(summary, String.Join(Environment.NewLine, lines) + Environment.NewLine, Encoding.UTF8);
    }

    static void AppendOutput(IEnumerable<string> lines)
    {
        string output = Environment.GetEnvironmentVariable("GITHUB_OUTPUT");
        if (String.IsNullOrWhiteSpace(output))
            return;
        File.AppendAllText(output, String.Join(Environment.NewLine, lines) + Environment.NewLine, Encoding.UTF8);
    }

    static int ValidateProjectMap(string[] args)
    {
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        string buildScript = ReadAll(Path.Combine(root, ".github", "scripts", "build-windows.cmd"));
        string workflow = ReadAll(Path.Combine(root, ".github", "workflows", "build-windows.yml"));
        string readme = ReadAll(Path.Combine(root, "README.md"));
        string[] required = { "key", "label", "folder", "buildOutput", "artifactName", "artifactPath", "skipKey" };
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var p in projects)
        {
            foreach (string field in required)
            {
                string value = ProjectField(p, field);
                if (String.IsNullOrWhiteSpace(value))
                    throw new InvalidOperationException("Project map entry is missing '" + field + "'.");
            }

            foreach (string field in new[] { "key", "buildOutput", "artifactName", "artifactPath", "skipKey" })
            {
                string unique = field + "=" + ProjectField(p, field);
                if (!seen.Add(unique))
                    throw new InvalidOperationException("Duplicate project map value for " + field + ": " + ProjectField(p, field));
            }

            if (!String.IsNullOrWhiteSpace(p.releaseTagEnvironment))
            {
                if (!Regex.IsMatch(p.releaseTagEnvironment, "^[A-Z][A-Z0-9_]*$"))
                    throw new InvalidOperationException("Invalid releaseTagEnvironment for " + p.key + ": " + p.releaseTagEnvironment);
                if (!seen.Add("releaseTagEnvironment=" + p.releaseTagEnvironment))
                    throw new InvalidOperationException("Duplicate project map releaseTagEnvironment: " + p.releaseTagEnvironment);
            }

            if (!Directory.Exists(Path.Combine(root, p.folder)))
                throw new InvalidOperationException("Project folder does not exist: " + p.folder);

            foreach (string dependencyPath in p.dependencyPaths ?? new string[0])
            {
                string normalizedDependencyPath =
                    dependencyPath.Replace('\\', '/');
                string declaredOwner = ProductOwnedDependencyOwner(
                    normalizedDependencyPath);
                if (!IsSharedEngineHostDeclaration(p.key, normalizedDependencyPath) && declaredOwner != null && !declaredOwner.Equals(
                    p.key,
                    StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        "Product-owned dependency for " + p.key +
                        " is declared under another product (" +
                        declaredOwner + "): " + dependencyPath);
                }

                string fullDependencyPath = Path.Combine(
                    root,
                    normalizedDependencyPath.Replace(
                        '/',
                        Path.DirectorySeparatorChar));
                if (!File.Exists(fullDependencyPath) &&
                    !Directory.Exists(fullDependencyPath))
                {
                    throw new InvalidOperationException(
                        "Project dependency does not exist for " +
                        p.key + ": " + dependencyPath);
                }
            }

            if (!buildScript.Contains(":Build" + p.skipKey) &&
                !buildScript.Contains("call :IsSkipped " + p.skipKey))
                throw new InvalidOperationException("Build script does not appear to handle skip key: " + p.skipKey);

            if (!workflow.Contains("needs.detect-projects.outputs." + p.buildOutput))
                throw new InvalidOperationException("Workflow upload condition does not mention build output for " + p.key + ": " + p.buildOutput);

            if (!workflow.Contains("name: " + p.artifactName))
                throw new InvalidOperationException("Workflow upload step does not mention artifact name for " + p.key + ": " + p.artifactName);

            foreach (string artifactSpec in ProjectArtifactPathSpecs(p))
            {
                string normalizedArtifactSpec = artifactSpec.Replace('\\', '/');
                if (!workflow.Contains(normalizedArtifactSpec))
                    throw new InvalidOperationException("Workflow upload paths do not mention declared artifact path for " + p.key + ": " + artifactSpec);

                string buildScriptArtifactSpec = normalizedArtifactSpec.Replace('/', '\\');
                string buildScriptLegacySpec = normalizedArtifactSpec.StartsWith("legacy/", StringComparison.OrdinalIgnoreCase)
                    ? normalizedArtifactSpec.Substring("legacy/".Length).Replace('/', '\\')
                    : buildScriptArtifactSpec;
                string artifactFileName = Path.GetFileName(normalizedArtifactSpec.Replace('/', Path.DirectorySeparatorChar));
                bool buildScriptMentionsArtifact =
                    buildScript.Contains(buildScriptArtifactSpec) ||
                    buildScript.Contains(normalizedArtifactSpec) ||
                    buildScript.Contains(buildScriptLegacySpec) ||
                    (!String.IsNullOrWhiteSpace(artifactFileName) && buildScript.Contains(artifactFileName));

                if (!buildScriptMentionsArtifact)
                    throw new InvalidOperationException("Build script artifact recording does not mention declared artifact path for " + p.key + ": " + artifactSpec);
            }

            if (!readme.Contains("`" + p.folder + "`"))
                throw new InvalidOperationException("README project table does not appear to mention folder: " + p.folder);
        }

        string dependencyRoot = Path.Combine(root, "dependencies");
        foreach (string productDirectory in Directory.EnumerateDirectories(
            dependencyRoot,
            "*",
            SearchOption.TopDirectoryOnly))
        {
            string owner = ProductOwnedDependencyOwner("dependencies/" + Path.GetFileName(productDirectory));
            if (owner == null)
                continue;
            Project project = projects.FirstOrDefault(p => p.key.Equals(
                owner,
                StringComparison.OrdinalIgnoreCase));
            if (project == null)
            {
                throw new InvalidOperationException(
                    "Dependency subfolder has no matching project-map owner: dependencies/" +
                    owner);
            }

            string expectedPath = "dependencies/" + owner;
            bool declared = (project.dependencyPaths ?? new string[0]).Any(
                path => path.Replace('\\', '/').TrimEnd('/').Equals(
                    expectedPath,
                    StringComparison.OrdinalIgnoreCase));
            if (!declared)
            {
                throw new InvalidOperationException(
                    "Product dependency directory must be declared as one owned unit for " +
                    project.key + ": " + expectedPath);
            }
        }

        var mappedBuildOutputs = new HashSet<string>(
            projects.Select(p => p.buildOutput),
            StringComparer.OrdinalIgnoreCase);
        mappedBuildOutputs.Add("build_all");
        foreach (Match match in Regex.Matches(
            workflow,
            @"steps\.detect\.outputs\.(build_[a-z0-9_]+)",
            RegexOptions.IgnoreCase))
        {
            string workflowOutput = match.Groups[1].Value;
            if (!mappedBuildOutputs.Contains(workflowOutput))
                throw new InvalidOperationException(
                    "Workflow exports an unmapped/stale build output: " + workflowOutput);
        }

        Console.WriteLine("Project map validation passed (" + projects.Count + " projects).");
        return 0;
    }

    static bool IsSharedDiscordServiceFile(string dependencyPath)
    {
        string path = dependencyPath.Replace('\\', '/');
        const string prefix = "dependencies/DiscordRPC/";
        if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return false;
        return new[] { "service.h", "service.cpp", "service_cancel.h", "drpc_environment.inc", "drpc_types.inc", "drpc_core.inc",
            "drpc_config_defaults.inc", "drpc_presence.inc", "drpc_ipc.inc", "drpc_gateway.inc" }
            .Contains(path.Substring(prefix.Length), StringComparer.OrdinalIgnoreCase);
    }

    static bool IsSharedDiscordHostDeclaration(string project, string dependencyPath)
    {
        return project.Equals("DesktopStub", StringComparison.OrdinalIgnoreCase) &&
            dependencyPath.Replace('\\', '/').TrimEnd('/').Equals("dependencies/DiscordRPC", StringComparison.OrdinalIgnoreCase);
    }

    static bool IsSharedAsusEngineFile(string dependencyPath)
    {
        string path = dependencyPath.Replace('\\', '/');
        const string prefix = "dependencies/asusblink/";
        return path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) &&
            new[] { "pattern.h", "service.h", "service.cpp", "protocol.h", "native_backend.cpp" }
                .Contains(path.Substring(prefix.Length), StringComparer.OrdinalIgnoreCase);
    }

    static bool IsSharedAsusHostDeclaration(string project, string dependencyPath)
    {
        return project.Equals("DesktopStub", StringComparison.OrdinalIgnoreCase) &&
            dependencyPath.Replace('\\', '/').TrimEnd('/').Equals("dependencies/asusblink", StringComparison.OrdinalIgnoreCase);
    }

    static bool IsSharedEngineHostDeclaration(string project, string dependencyPath)
    {
        return IsSharedDiscordHostDeclaration(project, dependencyPath) || IsSharedAsusHostDeclaration(project, dependencyPath);
    }

    static string ProductOwnedDependencyOwner(string dependencyPath)
    {
        if (String.IsNullOrWhiteSpace(dependencyPath))
            return null;
        string normalized = dependencyPath.Replace('\\', '/').TrimEnd('/');
        if (IsSharedDiscordServiceFile(normalized) || IsSharedAsusEngineFile(normalized)) return null;
        const string prefix = "dependencies/";
        if (!normalized.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            return null;
        string relative = normalized.Substring(prefix.Length);
        // Providers are reusable across products. A dependency subdirectory
        // is not necessarily a product overlay.
        if (relative.Equals("content_sources", StringComparison.OrdinalIgnoreCase) ||
            relative.StartsWith("content_sources/", StringComparison.OrdinalIgnoreCase) ||
            relative.Equals("hardware", StringComparison.OrdinalIgnoreCase) ||
            relative.StartsWith("hardware/", StringComparison.OrdinalIgnoreCase))
            return null;
        int separator = relative.IndexOf('/');
        if (separator > 0)
            return relative.Substring(0, separator);
        return separator < 0 && !Path.HasExtension(relative) ? relative : null;
    }

    static string ProjectField(Project p, string field)
    {
        if (field == "key") return p.key;
        if (field == "label") return p.label;
        if (field == "folder") return p.folder;
        if (field == "buildOutput") return p.buildOutput;
        if (field == "artifactName") return p.artifactName;
        if (field == "artifactPath") return p.artifactPath;
        if (field == "smokePath") return p.smokePath;
        if (field == "skipKey") return p.skipKey;
        return "";
    }

    static int TestWorkflowProjectSelection(string[] args)
    {
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        string workflow = ReadAll(Path.Combine(root, ".github", "workflows", "build-windows.yml"));
        string repoTools = ReadAll(Path.Combine(root, ".github", "tools", "RepoTools.cs"));
        foreach (var p in projects)
        {
            if (!Regex.IsMatch(workflow, @"(?m)^\s{10}- " + Regex.Escape(p.key) + @"\s*$"))
                throw new InvalidOperationException("workflow_dispatch project selector is missing option: " + p.key);

            int matches = projects.Count(x => x.key == p.key);
            if (matches != 1)
                throw new InvalidOperationException("Project selector simulation failed for " + p.key + ".");

            var skip = projects.Where(x => x.key != p.key).Select(x => "/skip:" + x.skipKey).ToList();
            if (skip.Contains("/skip:" + p.skipKey, StringComparer.OrdinalIgnoreCase))
                throw new InvalidOperationException("Project selector would skip selected project: " + p.key);
        }

        if (Regex.IsMatch(repoTools, @"Shared workflow/repository files changed; using a full build\.""\);\s*forceAll\s*=\s*true;\s*releaseSelected\s*=\s*projects\.ToList\(\);", RegexOptions.Singleline))
            throw new InvalidOperationException("Shared changes must not force every project into the release list.");

        if (!Regex.IsMatch(
                repoTools,
                @"release edit .* --draft .* --target .*release create .* --draft .* --target .*release upload .* --clobber.*release edit .* --draft=false",
                RegexOptions.Singleline))
            throw new InvalidOperationException("Automatic releases must remain retargeted drafts until every asset upload succeeds.");

        if (!repoTools.Contains("RunRequiredWithRetry(\"gh\", \"release upload "))
            throw new InvalidOperationException("Automatic release uploads must retry transient GitHub/GH API failures.");

        if (!repoTools.Contains("RunCaptureRequiredWithRetry(") || !repoTools.Contains("release list --repo "))
            throw new InvalidOperationException("Automatic release listing must retry transient GitHub/GH API failures.");

        if (!repoTools.Contains("TryViewGitHubRelease(repository, tag, root)") ||
            !repoTools.Contains("release create reported failure, but draft release "))
            throw new InvalidOperationException("Automatic release creation must recover if a transient failure still created a draft.");

        if (!repoTools.Contains("prepare-release-versions") ||
            !repoTools.Contains("FindReplaceableLatestRelease") ||
            !repoTools.Contains("--cleanup-tag"))
            throw new InvalidOperationException("Automatic releases must reuse the latest zero-download version before building and clean up its old tag when replacing it.");

        if (!Regex.IsMatch(
                workflow,
                @"Prepare reusable release versions.*Prepare-ReleaseVersions\.cmd.*- name: Build",
                RegexOptions.Singleline))
        {
            throw new InvalidOperationException(
                "The workflow must prepare reusable zero-download release versions before compiling binaries.");
        }

        if (!repoTools.Contains("Release-pending projects also selected for build:"))
            throw new InvalidOperationException("Project selection must build release-pending projects so follow-up CI fixes do not skip previously changed consumers.");

        var releaseNames = new[] { "DesktopStub-v2", "DesktopStub-v5", "DesktopStub-v4" };
        if (NextReleaseTag("DesktopStub", releaseNames) != "DesktopStub-v6")
            throw new InvalidOperationException("Release version selection must include draft release names.");

        var zeroDownloadRelease = new GitHubReleaseInfo
        {
            tagName = "DesktopStub-v5",
            assets = new List<GitHubReleaseAssetInfo>
            {
                new GitHubReleaseAssetInfo { downloadCount = 0 },
                new GitHubReleaseAssetInfo { downloadCount = 0 }
            }
        };
        var downloadedRelease = new GitHubReleaseInfo
        {
            tagName = "DesktopStub-v5",
            assets = new List<GitHubReleaseAssetInfo>
            {
                new GitHubReleaseAssetInfo { downloadCount = 1 },
                new GitHubReleaseAssetInfo { downloadCount = 0 }
            }
        };
        var releaseSummaries = new[]
        {
            new GitHubReleaseInfo { tagName = "DesktopStub-v4" },
            new GitHubReleaseInfo { tagName = "DesktopStub-v5" }
        };
        if (!IsReplaceableLatestRelease("DesktopStub", releaseSummaries, zeroDownloadRelease))
            throw new InvalidOperationException("The highest published release with zero aggregate downloads must be replaceable.");
        if (IsReplaceableLatestRelease("DesktopStub", releaseSummaries, downloadedRelease))
            throw new InvalidOperationException("A release with any asset download must never be replaced.");
        if (IsReplaceableLatestRelease(
                "DesktopStub",
                releaseSummaries.Concat(new[]
                {
                    new GitHubReleaseInfo { tagName = "DesktopStub-v6", isDraft = true }
                }),
                zeroDownloadRelease))
        {
            throw new InvalidOperationException("A published release must not be replaced while a newer draft version exists.");
        }

        var commandLineConsumers = ProjectsUsingChangedDependencies(root, projects, new List<string> { "dependencies/command_line.inc" })
            .Select(p => p.key)
            .OrderBy(k => k, StringComparer.OrdinalIgnoreCase)
            .ToList();
        var expectedConsumers = new[]
        {
            "ADBController",
            "CharmTray",
            "DesktopStub",
            "DiscordRPC",
            "NowPlayingTile",
            "RealTimeNotesDeskband",
            "SecureDesktopLauncher"
        };
        if (!commandLineConsumers.SequenceEqual(expectedConsumers.OrderBy(k => k, StringComparer.OrdinalIgnoreCase), StringComparer.OrdinalIgnoreCase))
            throw new InvalidOperationException("Dependency consumer detection for dependencies/command_line.inc returned " + String.Join(", ", commandLineConsumers) + ".");

        var registryServiceConsumers = ProjectsUsingChangedDependencies(
                root,
                projects,
                new List<string> { "dependencies/registry_notification_service.cs" })
            .Select(p => p.key)
            .OrderBy(k => k, StringComparer.OrdinalIgnoreCase)
            .ToList();
        var expectedRegistryServiceConsumers =
            new[] { "AllowContentAboveLock", "YourPhoneHideBanner" };
        if (!registryServiceConsumers.SequenceEqual(
                expectedRegistryServiceConsumers.OrderBy(
                    key => key,
                    StringComparer.OrdinalIgnoreCase),
                StringComparer.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException(
                "Dependency consumer detection for registry_notification_service.cs returned " +
                String.Join(", ", registryServiceConsumers) + ".");
        }

        ValidateBatchStatusPropagation(root);
        ValidateSmokeProjectSelection(projects);
        var discordServiceConsumers = ProjectsUsingChangedDependencies(root, projects,
            new List<string> { "dependencies/DiscordRPC/service.cpp" }).Select(p => p.key).OrderBy(k => k).ToArray();
        if (!discordServiceConsumers.SequenceEqual(new[] { "DesktopStub", "DiscordRPC" }))
            throw new InvalidOperationException("Shared Discord service consumers: " + String.Join(", ", discordServiceConsumers));
        var discordTrayConsumers = ProjectsUsingChangedDependencies(root, projects,
            new List<string> { "dependencies/DiscordRPC/drpc_tray.inc" }).Select(p => p.key).ToArray();
        if (discordTrayConsumers.Length != 1 || discordTrayConsumers[0] != "DiscordRPC")
            throw new InvalidOperationException("Standalone Discord tray changes must stay owned by DiscordRPC.");
        var asusConsumers = ProjectsUsingChangedDependencies(root, projects,
            new List<string> { "dependencies/asusblink/service.cpp" }).Select(p => p.key);
        if (!new HashSet<string>(asusConsumers, StringComparer.OrdinalIgnoreCase).SetEquals(new[] { "DesktopStub", "asusblink" }))
            throw new InvalidOperationException("Shared ASUS engine consumers are incorrect.");
        if (ProductOwnedDependencyOwner("dependencies/content_sources") != null ||
            ProductOwnedDependencyOwner("dependencies/content_sources/smtc.inc") != null ||
            ProductOwnedDependencyOwner("dependencies/content_sources_other/module.inc") != "content_sources_other")
            throw new InvalidOperationException("Shared content-source namespace classification is incorrect.");

        Console.WriteLine("Workflow project selector validation passed (" + projects.Count + " projects plus All).");
        return 0;
    }

    static void ValidateBatchStatusPropagation(string root)
    {
        var staleCompoundExit = new Regex(
            @"\|\|\s*exit\s+/b\s+%errorlevel%",
            RegexOptions.IgnoreCase);
        foreach (string file in Directory.EnumerateFiles(root, "*.cmd", SearchOption.AllDirectories))
        {
            if (IsExcludedPath(root, file))
                continue;

            int lineNumber = 0;
            foreach (string line in File.ReadLines(file))
            {
                ++lineNumber;
                if (staleCompoundExit.IsMatch(line))
                {
                    throw new InvalidOperationException(
                        "Batch command can expand ERRORLEVEL before the preceding command runs: " +
                        Rel(root, file) + ":" + lineNumber);
                }
            }
        }

        foreach (string relativePath in new[]
        {
            "legacy/NowPlayingTile/BuildNowPlayingTile.cmd"
        })
        {
            string script = ReadAll(Path.Combine(
                root,
                relativePath.Replace('/', Path.DirectorySeparatorChar)));
            if (!script.Contains("set \"STATUS=!ERRORLEVEL!\""))
            {
                throw new InvalidOperationException(
                    relativePath +
                    " check mode must capture compiler failure with delayed expansion.");
            }
        }

        string windowsBuild = ReadAll(
            Path.Combine(root, ".github", "scripts", "build-windows.cmd"));
        if (!windowsBuild.Contains("if \"!STATUS!\"==\"0\" (") ||
            !windowsBuild.Contains("set \"STATUS=!ERRORLEVEL!\""))
        {
            throw new InvalidOperationException(
                "SecureDesktopLauncher's two-stage workflow build must propagate the second build failure.");
        }

        foreach (string relativePath in new[]
        {
            "legacy/SecureDesktopLauncher/build_launcher.cmd",
            "legacy/SecureDesktopLauncher/build_password_launcher.cmd",
            "legacy/CharmTray/BuildCharmTray.cmd"
        })
        {
            string script = ReadAll(Path.Combine(
                root,
                relativePath.Replace('/', Path.DirectorySeparatorChar)));
            if (Regex.IsMatch(
                    script,
                    @"(?is)if\s+/i\s+""%~?1""==""(?:check|new)""\s*\(.*?set\s+""STATUS=%ERRORLEVEL%"""))
            {
                throw new InvalidOperationException(
                    relativePath +
                    " captures ERRORLEVEL inside a pre-expanded mode block.");
            }
        }
    }

    static int TestReadmeConsistency(string[] args)
    {
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        string rootReadme = ReadAll(Path.Combine(root, "README.md"));
        var warnings = new List<string>();

        foreach (var p in projects)
        {
            if (!rootReadme.Contains("`" + p.folder + "`"))
                warnings.Add("Root README project table does not mention " + p.folder + ".");

            string path = Path.Combine(root, p.folder, "README.md");
            if (!File.Exists(path))
            {
                warnings.Add("Project README is missing: " + p.folder + "/README.md");
                continue;
            }

            string text = ReadAll(path);
            if (!Regex.IsMatch(text, @"(?im)^##?\s+(build|building)\b") && !Regex.IsMatch(text, "(?i)build"))
                warnings.Add(p.folder + "/README.md may be missing build instructions.");
            if (!Regex.IsMatch(text, @"(?im)^##?\s+(usage|running|install|quick start|how to)\b") && !Regex.IsMatch(text, "(?i)(usage|run|install|start)"))
                warnings.Add(p.folder + "/README.md may be missing usage instructions.");
            if (!Regex.IsMatch(text, "(?i)(admin|elevat|service|registry|secure desktop|appx|com|scheduled task|warning|safety|privilege)"))
                warnings.Add(p.folder + "/README.md may be missing safety/privilege notes.");
        }

        if (warnings.Count == 0)
        {
            Console.WriteLine("README consistency scan found no warnings.");
            return 0;
        }

        Warn("README consistency scan found " + warnings.Count + " warning(s). These are warnings only and do not fail CI.");
        foreach (string warning in warnings)
            Warn(warning);
        AppendWarningSummary("README consistency warnings", warnings);
        return 0;
    }

    static int InvokePolicyWarnings(string[] args)
    {
        string root = RepositoryRoot(args);
        var rules = new[]
        {
            Tuple.Create("Profile storage", LiteralPattern(
                "%APP" + "DATA%",
                "%LOCAL" + "APPDATA%",
                "%PROGRAM" + "DATA%",
                "Application" + "Data",
                "Local" + "Application" + "Data",
                "Common" + "Application" + "Data",
                "Special" + "Folder.Application" + "Data",
                "Special" + "Folder.LocalApplication" + "Data",
                "Special" + "Folder.CommonApplication" + "Data")),
            Tuple.Create("Access-control mutation", LiteralPattern(
                "SetAccess" + "Control",
                "File" + "Security",
                "Directory" + "Security",
                "ic" + "acls",
                "take" + "own",
                "SetNamed" + "Security" + "Info",
                "Set" + "Security" + "Info")),
            Tuple.Create("Maintenance marker", LiteralPattern(
                "TO" + "DO",
                "FIX" + "ME",
                "HA" + "CK",
                "obso" + "lete",
                "depre" + "cated",
                "work" + "around"))
        };
        var extensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            ".cs", ".cpp", ".h", ".hpp", ".inc", ".cmd", ".bat", ".md", ".json", ".yml", ".yaml"
        };
        var warnings = new List<string>();
        string exe = Path.GetFullPath(Process.GetCurrentProcess().MainModule.FileName);

        foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
        {
            if (!extensions.Contains(Path.GetExtension(file)) || IsExcludedPath(root, file))
                continue;
            if (Path.GetFullPath(file).Equals(exe, StringComparison.OrdinalIgnoreCase))
                continue;

            int lineNo = 0;
            foreach (string line in File.ReadLines(file))
            {
                ++lineNo;
                foreach (var rule in rules)
                {
                    if (rule.Item2.IsMatch(line))
                    {
                        string relPath = Rel(root, file);
                        if (!IsSuppressedPolicyWarning(relPath, rule.Item1, line))
                            warnings.Add(relPath + ":" + lineNo + " [" + rule.Item1 + "] " + line.Trim());
                    }
                }
            }
        }

        warnings.AddRange(TrackedGeneratedFileWarnings(root));

        if (warnings.Count == 0)
        {
            Console.WriteLine("Policy warning scan found no suspicious patterns.");
            return 0;
        }

        Warn("Policy warning scan found " + warnings.Count + " item(s). These are warnings only and do not fail CI.");
        foreach (string warning in warnings)
            Warn(warning);
        AppendWarningSummary("Policy warning scan", warnings);
        return 0;
    }

    static Regex LiteralPattern(params string[] tokens)
    {
        return new Regex(String.Join("|", tokens.Select(Regex.Escape)));
    }

    static string JoinLiteral(params string[] parts)
    {
        return String.Concat(parts);
    }

    static bool IsSuppressedPolicyWarning(string relPath, string ruleName, string line)
    {
        string rel = relPath.Replace('\\', '/');
        string trimmed = line.Trim();
        var suppressions = new[]
        {
            Tuple.Create("README.md", "Profile storage", "If a non-INI configuration format is unavoidable"),
            Tuple.Create("DesktopStub/README.md", "Maintenance marker", "live-wallpaper capture path does not look for a specific process name"),
            Tuple.Create("dependencies/DesktopStub/ga_app.inc", "Maintenance marker", JoinLiteral("ob", "solete") + " AppX launch-forwarding fallback kept for diagnostics/rollback"),
            Tuple.Create("dependencies/DesktopStub/ga_livetile_broker_app.inc", "Profile storage", JoinLiteral("Application", "Data::Current().LocalFolder()")),
            Tuple.Create("DesktopStub/tools/DesktopStubSourceCheck.cpp", "Maintenance marker", "INI template does not expose " + JoinLiteral("ob", "solete") + " Manifest section"),
            Tuple.Create("DesktopStub/tools/DesktopStubSourceCheck.cpp", "Maintenance marker", JoinLiteral("Ob", "solete") + " Manifest INI section is preserved but blocked"),
            Tuple.Create("DesktopStub/tools/DesktopStubSourceCheck.cpp", "Maintenance marker", JoinLiteral("Ob", "solete") + " Manifest INI section has no remover"),
            Tuple.Create("dependencies/managed_startup_shortcut.cs", "Profile storage", "Environment.GetFolderPath(Environment." + JoinLiteral("Special", "Folder") + ".Startup)"),
            Tuple.Create("tools/LegacyUtilitiesTests.cs", "Profile storage", "Environment." + JoinLiteral("Special", "Folder") + ".Startup"),
            Tuple.Create("tools/SharedBaselineSourceCheck.cpp", "Profile storage", "Environment.GetFolderPath(Environment." + JoinLiteral("Special", "Folder") + ".Startup)")
        };

        foreach (var suppression in suppressions)
        {
            if (rel.Equals(suppression.Item1, StringComparison.OrdinalIgnoreCase) &&
                ruleName.Equals(suppression.Item2, StringComparison.OrdinalIgnoreCase) &&
                trimmed.IndexOf(suppression.Item3, StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return true;
            }
        }

        return false;
    }

    static bool IsExcludedPath(string root, string file)
    {
        string rel = Rel(root, file).Replace('\\', '/');
        foreach (string part in rel.Split('/'))
        {
            if (part.Equals(".git", StringComparison.OrdinalIgnoreCase) ||
                part.Equals("build", StringComparison.OrdinalIgnoreCase) ||
                part.Equals("bin", StringComparison.OrdinalIgnoreCase) ||
                part.Equals("obj", StringComparison.OrdinalIgnoreCase) ||
                part.Equals("references", StringComparison.OrdinalIgnoreCase))
                return true;
        }
        return false;
    }

    static IEnumerable<string> TrackedGeneratedFileWarnings(string root)
    {
        var result = RunCapture("git", "ls-files", root, 120000);
        if (result.ExitCode != 0)
            yield break;

        foreach (string raw in result.Output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
        {
            string rel = raw.Replace('\\', '/');
            bool generatedDir = rel.IndexOf("/build/", StringComparison.OrdinalIgnoreCase) >= 0 ||
                rel.StartsWith("build/", StringComparison.OrdinalIgnoreCase) ||
                rel.IndexOf("/references/", StringComparison.OrdinalIgnoreCase) >= 0 ||
                rel.StartsWith("references/", StringComparison.OrdinalIgnoreCase);
            if (generatedDir)
                yield return "Tracked generated/reference file should be removed from Git: " + rel;
        }
    }

    static void Warn(string text)
    {
        Console.Error.WriteLine("WARNING: " + text);
    }

    static void AppendWarningSummary(string title, List<string> warnings)
    {
        var lines = new List<string>
        {
            "## " + title,
            "",
            "Found " + warnings.Count + " warning item(s). These are informational and do not fail CI.",
            "",
            "<details><summary>Warnings</summary>",
            "",
            "```text"
        };
        lines.AddRange(warnings);
        lines.Add("```");
        lines.Add("");
        lines.Add("</details>");
        AppendSummary(lines);
    }

    static int DetectProjects(string[] args)
    {
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        string eventName = Option(args, "--event", Environment.GetEnvironmentVariable("GITHUB_EVENT_NAME") ?? "");
        string manualProject = Option(args, "--manual-project", "");
        string head = Option(args, "--sha", Environment.GetEnvironmentVariable("GITHUB_SHA") ?? "");
        bool forceAll = false;
        bool sharedChange = false;
        var changed = new List<string>();
        var releaseSelected = new List<Project>();
        List<Project> selected;

        if (eventName == "workflow_dispatch")
        {
            if (String.IsNullOrWhiteSpace(manualProject) || manualProject == "All")
            {
                forceAll = true;
                selected = projects.ToList();
                releaseSelected = selected.ToList();
            }
            else
            {
                selected = projects.Where(p => p.key == manualProject).ToList();
                if (selected.Count != 1)
                    throw new InvalidOperationException("Unknown workflow_dispatch project: " + manualProject);
                releaseSelected = selected.ToList();
            }
        }
        else
        {
            string baseSha;
            if (eventName == "pull_request")
            {
                baseSha = Option(args, "--pull-base");
                head = Option(args, "--pull-head", head);
            }
            else
            {
                baseSha = Option(args, "--before");
                if (String.IsNullOrWhiteSpace(baseSha) || Regex.IsMatch(baseSha, "^0+$"))
                    baseSha = head + "^";
            }

            var diff = RunCapture("git", "diff --name-only " + QuoteArg(baseSha) + " " + QuoteArg(head), root, 120000);
            if (diff.ExitCode != 0)
            {
                Warn("Could not diff changed files; falling back to a full build.");
                forceAll = true;
            }
            else
            {
                changed = diff.Output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).ToList();
                releaseSelected = ProjectsNeedingRelease(root, head, projects, changed);
            }

            if (!forceAll)
            {
                sharedChange = changed.Any(p => p.StartsWith(".github/", StringComparison.OrdinalIgnoreCase) ||
                    p.StartsWith("dependencies/", StringComparison.OrdinalIgnoreCase) ||
                    p == ".gitattributes" ||
                    p == "README.md");
                if (sharedChange)
                {
                    Console.WriteLine("Shared workflow/repository files changed; using a full build.");
                    forceAll = true;
                }
            }

            if (forceAll)
            {
                selected = projects.ToList();
            }
            else
            {
                selected = projects.Where(p =>
                {
                    string prefix = p.folder.Replace('\\', '/') + "/";
                    return changed.Any(c => c.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
                }).ToList();

                if (selected.Count == 0)
                {
                    Console.WriteLine("No project-specific changes detected; using a full build to keep CI meaningful.");
                    forceAll = true;
                    selected = projects.ToList();
                }
            }
        }

        if (forceAll)
            selected = projects.ToList();
        else if (releaseSelected.Count > 0)
        {
            var selectedKeysForRelease = new HashSet<string>(selected.Select(p => p.key), StringComparer.OrdinalIgnoreCase);
            var addedReleaseProjects = new List<Project>();
            foreach (var project in releaseSelected)
            {
                if (selectedKeysForRelease.Add(project.key))
                {
                    selected.Add(project);
                    addedReleaseProjects.Add(project);
                }
            }
            if (addedReleaseProjects.Count > 0)
            {
                Console.WriteLine("Release-pending projects also selected for build: " +
                    String.Join(", ", addedReleaseProjects.Select(p => p.label)));
            }
        }

        var selectedKeys = new HashSet<string>(selected.Select(p => p.key), StringComparer.OrdinalIgnoreCase);
        releaseSelected = releaseSelected
            .Where(p => selectedKeys.Contains(p.key))
            .ToList();

        var skip = new List<string>();
        var outputs = new List<string>();
        foreach (var p in projects)
        {
            bool build = selectedKeys.Contains(p.key);
            outputs.Add(p.buildOutput + "=" + build.ToString().ToLowerInvariant());
            if (!build)
                skip.Add("/skip:" + p.skipKey);
        }
        if (forceAll)
            skip.Clear();

        string projectList = String.Join(", ", selected.Select(p => p.label));
        string releaseProjectList = String.Join(", ", releaseSelected.Select(p => p.label));
        string releaseBase = selected.Count == 1 && !forceAll ? selected[0].key : "All";
        outputs.Add("build_all=" + forceAll.ToString().ToLowerInvariant());
        outputs.Add("skip_args=" + String.Join(" ", skip));
        outputs.Add("project_list=" + projectList);
        outputs.Add("release_project_list=" + releaseProjectList);
        outputs.Add("release_base=" + releaseBase);
        AppendOutput(outputs);

        var summary = new List<string>
        {
            "## Project selection",
            "",
            "Build mode: **" + (forceAll ? "full" : "selected/changed projects only") + "**",
            "",
            "Projects: " + projectList,
            "",
            "Release families: " + (releaseSelected.Count == 0 ? "none" : String.Join(", ", releaseSelected.Select(p => "`" + p.key + "-vN`")))
        };
        if (changed.Count > 0)
        {
            summary.Add("");
            summary.Add("Changed files:");
            summary.AddRange(changed.Select(f => "- `" + f + "`"));
        }
        AppendSummary(summary);
        Console.WriteLine("Selected projects: " + projectList);
        return 0;
    }

    static List<Project> ProjectsTouchedBy(List<string> changed, List<Project> projects)
    {
        return projects.Where(p =>
        {
            string prefix = p.folder.Replace('\\', '/') + "/";
            return changed.Any(c => c.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
        }).ToList();
    }

    static List<Project> ProjectsNeedingRelease(string root, string head, List<Project> projects, List<string> currentChanged)
    {
        var currentTouched = new HashSet<string>(ProjectsTouchedBy(currentChanged, projects).Select(p => p.key), StringComparer.OrdinalIgnoreCase);
        var currentDependencyConsumers = new HashSet<string>(ProjectsUsingChangedDependencies(root, projects, currentChanged).Select(p => p.key), StringComparer.OrdinalIgnoreCase);
        var selected = new List<Project>();
        foreach (var p in projects)
        {
            string latestTag = RunCapture("git", "tag --list " + QuoteArg(p.key + "-v*") + " --sort=-v:refname", root, 120000)
                .Output
                .Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                .FirstOrDefault();
            if (latestTag == null)
            {
                if (currentTouched.Contains(p.key) || currentDependencyConsumers.Contains(p.key))
                    selected.Add(p);
                continue;
            }

            string files = RunCapture("git", "diff --name-only " + QuoteArg(latestTag) + " " + QuoteArg(head), root, 120000).Output;
            var changedSinceLatestRelease = files.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).ToList();
            string prefix = p.folder.Replace('\\', '/') + "/";
            bool touchedSinceLatestRelease = changedSinceLatestRelease
                .Any(c => c.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
            bool dependencyTouchedSinceLatestRelease = ProjectUsesAnyChangedDependency(root, p, changedSinceLatestRelease);
            if (touchedSinceLatestRelease || dependencyTouchedSinceLatestRelease)
                selected.Add(p);
        }
        return selected;
    }

    static List<Project> ProjectsUsingChangedDependencies(string root, List<Project> projects, List<string> changed)
    {
        return projects.Where(p => ProjectUsesAnyChangedDependency(root, p, changed)).ToList();
    }

    static bool ContainsDependencyFileName(string text, string name)
    {
        if (String.IsNullOrWhiteSpace(name) || text.IndexOf(name, StringComparison.OrdinalIgnoreCase) < 0) return false;
        // service.cpp must not also match SecureDesktopLauncherService.cpp.
        return Regex.IsMatch(text, @"(?<![A-Za-z0-9_.-])" + Regex.Escape(name) + @"(?![A-Za-z0-9_.-])", RegexOptions.IgnoreCase);
    }

    static bool ProjectUsesAnyChangedDependency(string root, Project project, List<string> changed)
    {
        var normalizedChanged = changed
            .Select(p => p.Replace('\\', '/'))
            .ToList();
        foreach (string declaredDependency in
            project.dependencyPaths ?? new string[0])
        {
            string normalizedDependency = declaredDependency
                .Replace('\\', '/')
                .TrimEnd('/');
            string fullDependency = Path.Combine(
                root,
                normalizedDependency.Replace(
                    '/',
                    Path.DirectorySeparatorChar));
            bool isDirectory = Directory.Exists(fullDependency);
            if (IsSharedDiscordHostDeclaration(project.key, normalizedDependency))
            {
                if (normalizedChanged.Any(IsSharedDiscordServiceFile)) return true;
                continue;
            }
            if (IsSharedAsusHostDeclaration(project.key, normalizedDependency))
            {
                if (normalizedChanged.Any(IsSharedAsusEngineFile)) return true;
                continue;
            }
            if (normalizedChanged.Any(change =>
                change.Equals(
                    normalizedDependency,
                    StringComparison.OrdinalIgnoreCase) ||
                (isDirectory && change.StartsWith(
                    normalizedDependency + "/",
                    StringComparison.OrdinalIgnoreCase))))
            {
                return true;
            }
        }

        var dependencies = normalizedChanged
            .Where(p => p.StartsWith("dependencies/", StringComparison.OrdinalIgnoreCase))
            // Known engine consumers are declared above. Do not infer another
            // engine from a same-named service.cpp in a different directory.
            .Where(p => !IsSharedDiscordServiceFile(p) || project.key == "DiscordRPC" || project.key == "DesktopStub")
            .Where(p => !IsSharedAsusEngineFile(p) || project.key == "asusblink" || project.key == "DesktopStub")
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
        if (dependencies.Count == 0)
            return false;

        var dependencyNames = new HashSet<string>(
            dependencies.Select(Path.GetFileName).Where(name => !String.IsNullOrWhiteSpace(name)),
            StringComparer.OrdinalIgnoreCase);
        string dependencyRoot = Path.Combine(root, "dependencies");
        bool expanded;
        do
        {
            expanded = false;
            foreach (string file in Directory.EnumerateFiles(dependencyRoot, "*", SearchOption.AllDirectories))
            {
                string ext = Path.GetExtension(file);
                if (!new[] { ".h", ".hpp", ".inc", ".cs" }.Contains(ext, StringComparer.OrdinalIgnoreCase))
                    continue;

                string fileName = Path.GetFileName(file);
                if (dependencyNames.Contains(fileName))
                    continue;

                string text = ReadAll(file);
                if (dependencyNames.Any(name => ContainsDependencyFileName(text, name)))
                {
                    dependencyNames.Add(fileName);
                    expanded = true;
                }
            }
        }
        while (expanded);

        string projectRoot = Path.Combine(root, project.folder.Replace('/', Path.DirectorySeparatorChar));
        if (!Directory.Exists(projectRoot))
            return false;

        foreach (string file in Directory.EnumerateFiles(projectRoot, "*", SearchOption.AllDirectories))
        {
            string ext = Path.GetExtension(file);
            if (!new[] { ".cpp", ".c", ".h", ".hpp", ".inc", ".rc", ".cs" }.Contains(ext, StringComparer.OrdinalIgnoreCase))
                continue;

            string text = ReadAll(file);
            if (text.IndexOf("dependencies", StringComparison.OrdinalIgnoreCase) < 0)
                continue;

            foreach (string name in dependencyNames)
            {
                if (ContainsDependencyFileName(text, name))
                    return true;
            }
        }
        return false;
    }

    static IEnumerable<string> ProjectArtifactPathSpecs(Project p)
    {
        if (p.artifactPaths != null && p.artifactPaths.Length > 0)
            return p.artifactPaths;
        if (!String.IsNullOrWhiteSpace(p.artifactPath))
            return new[] { p.artifactPath };
        return Enumerable.Empty<string>();
    }

    static IEnumerable<string> ExpandProjectArtifactPaths(string root, Project p)
    {
        foreach (string spec in ProjectArtifactPathSpecs(p))
        {
            if (String.IsNullOrWhiteSpace(spec))
                continue;
            string normalized = spec.Replace('\\', '/');
            if (normalized.EndsWith("/**", StringComparison.Ordinal))
            {
                string dirRel = normalized.Substring(0, normalized.Length - 3);
                string dir = Path.Combine(root, dirRel.Replace('/', Path.DirectorySeparatorChar));
                if (Directory.Exists(dir))
                {
                    foreach (string file in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
                        yield return file;
                }
            }
            else
            {
                yield return Path.Combine(root, normalized.Replace('/', Path.DirectorySeparatorChar));
            }
        }
    }

    static int ChecksumSummary(string[] args)
    {
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        var lines = new List<string>
        {
            "## Windows build outputs",
            "",
            "GitHub Actions downloads workflow artifacts as ZIP archives. The hashes below are for each declared payload file inside those artifacts.",
            "",
            "| Project | File | SHA256 |",
            "| --- | --- | --- |"
        };
        int count = 0;
        foreach (var p in projects)
        {
            foreach (string path in ExpandProjectArtifactPaths(root, p))
            {
                if (!File.Exists(path) || path.EndsWith(".sha256", StringComparison.OrdinalIgnoreCase))
                    continue;
                lines.Add("| " + p.label + " | `" + Rel(root, path).Replace('\\', '/') + "` | `" + Sha256File(path).ToLowerInvariant() + "` |");
                ++count;
            }
        }
        if (count == 0)
            throw new InvalidOperationException("No build outputs were found.");
        AppendSummary(lines);
        Console.WriteLine("Checksum summary wrote " + count + " entr" + (count == 1 ? "y." : "ies."));
        return 0;
    }

    static List<Project> SelectSmokeProjects(List<Project> projects, string selection)
    {
        if (String.IsNullOrWhiteSpace(selection))
            throw new InvalidOperationException("--projects requires All or a comma-separated project list.");
        if (selection.Trim().Equals("All", StringComparison.OrdinalIgnoreCase))
            return projects.ToList();

        var requested = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string item in selection.Split(','))
        {
            string key = item.Trim();
            if (key.Length == 0 || !projects.Any(p => p.key.Equals(key, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException("Unknown smoke-test project: " + item);
            requested.Add(key);
        }
        return projects.Where(p => requested.Contains(p.key)).ToList();
    }

    static void ValidateSmokeProjectSelection(List<Project> projects)
    {
        var selected = SelectSmokeProjects(projects, "desktopstub, DiscordRPC,DesktopStub");
        if (selected.Count != 2 || !selected.Any(p => p.key == "DesktopStub") ||
            !selected.Any(p => p.key == "DiscordRPC"))
            throw new InvalidOperationException("Smoke project selection must trim, deduplicate and match case-insensitively.");
        if (SelectSmokeProjects(projects, "All").Count != projects.Count)
            throw new InvalidOperationException("Smoke project selection must support All.");
        foreach (string invalid in new[] { "", "DoesNotExist", "DesktopStub,", "All,DiscordRPC" })
        {
            bool rejected = false;
            try { SelectSmokeProjects(projects, invalid); }
            catch (InvalidOperationException) { rejected = true; }
            if (!rejected)
                throw new InvalidOperationException("Invalid smoke selection was accepted: " + invalid);
        }
    }

    static int SmokeWindowsBuild(string[] args)
    {
        InstallSmokeLifetime(15 * 60 * 1000);
        allowPackageIntegration = args.Any(arg => arg.Equals("--allow-package-integration", StringComparison.OrdinalIgnoreCase));
        Console.WriteLine(allowPackageIntegration
            ? "Explicit package integration enabled: use a disposable Windows test environment."
            : "Default smoke is local-only: package registration, activation and publishing are disabled.");
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        string desktopStubBinaryOverride = Option(args, "--desktopstub-binary");
        string desktopStubBrokerOverride = Option(args, "--desktopstub-broker");
        bool explicitSelection = args.Any(arg => arg.Equals("--projects", StringComparison.OrdinalIgnoreCase) ||
            arg.StartsWith("--projects=", StringComparison.OrdinalIgnoreCase));
        if (explicitSelection)
        {
            projects = SelectSmokeProjects(projects, Option(args, "--projects"));
            // Validate the complete selected build before starting any smoke
            // process. An omitted artifact must not produce a green CI run.
            foreach (Project project in projects)
            {
                if (project.key == "DesktopStub" && !String.IsNullOrWhiteSpace(desktopStubBinaryOverride)) {
                    if (!File.Exists(desktopStubBinaryOverride) || new FileInfo(desktopStubBinaryOverride).Length == 0)
                        throw new InvalidOperationException("Selected DesktopStub binary is missing or empty: " + desktopStubBinaryOverride);
                    continue;
                }
                foreach (string artifact in project.artifactPaths ?? new[] { project.artifactPath })
                {
                    string fullPath = Path.Combine(root, artifact.Replace('/', Path.DirectorySeparatorChar));
                    if (!File.Exists(fullPath) || new FileInfo(fullPath).Length == 0)
                        throw new InvalidOperationException("Selected smoke artifact is missing or empty: " + artifact);
                }
            }
        }
        Console.WriteLine("Running Windows build smoke tests...");

        Project asusBlink = projects.FirstOrDefault(p => p.key == "asusblink");
        if (asusBlink != null)
        {
            string sourceExe = Path.Combine(root, asusBlink.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                VerifyBinaryVersion(sourceExe, "asusblink");
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "asusblinkSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                bool deleteTemp = false;
                try
                {
                    string exe = Path.Combine(tempRoot, Path.GetFileName(sourceExe));
                    File.Copy(sourceExe, exe, true);
                    string ini = Path.Combine(tempRoot, "asusblink.ini");

                    ProcessResult helpResult = SmokeProcess(
                        exe,
                        new[] { "--help", "--startup", "--set", "Options.run-at-startup=maybe" },
                        new[] { 0 },
                        30,
                        "asusblink help");
                    if (helpResult.Output.IndexOf("--startup | --no-startup", StringComparison.Ordinal) < 0 ||
                        helpResult.Output.IndexOf("--set Settings.Key=Value", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("asusblink --help did not expose the common persistent setting surface.");
                    AssertFileDoesNotExist(ini, "asusblink --help must be side-effect-free");

                    ProcessResult versionResult = SmokeProcess(
                        exe,
                        new[] { "--version", "--startup" },
                        new[] { 0 },
                        30,
                        "asusblink version");
                    if (versionResult.Output.IndexOf("asusblink 1", StringComparison.OrdinalIgnoreCase) < 0)
                        throw new InvalidOperationException("asusblink --version did not report version 1.");
                    AssertFileDoesNotExist(ini, "asusblink --version must be side-effect-free");

                    SmokeProcess(
                        exe,
                        new[]
                        {
                            "--set", "Settings.ShowTrayIcon=false",
                            "--set", "Settings.RunAtStartup=maybe"
                        },
                        new[] { 2 },
                        30,
                        "asusblink invalid Startup boolean batch");
                    AssertFileDoesNotExist(ini, "asusblink must reject an invalid typed batch before creating an INI or changing Startup state");

                    SmokeProcess(
                        exe,
                        new[]
                        {
                            "--configure-only",
                            "--set", "Settings.ShowTrayIcon=false",
                            "--flat-menu",
                            "--set", "Settings.ErrorRetry=5"
                        },
                        new[] { 0 },
                        30,
                        "asusblink typed setting batch");
                    AssertFileContains(ini, "[Settings]", "asusblink must write the canonical Settings section");
                    AssertFileContains(ini, "ShowTrayIcon=false", "asusblink must persist the canonical tray setting");
                    AssertFileContains(ini, "ShowMenuAsDropdown=false", "asusblink must persist the canonical tray layout setting");
                    AssertFileContains(ini, "ErrorRetry=5", "asusblink must persist a typed retry setting in the same batch");

                    SmokeProcess(
                        exe,
                        new[]
                        {
                            "--configure-only",
                            "--tray",
                            "--set", "Settings.ErrorAction=pause"
                        },
                        new[] { 0 },
                        30,
                        "asusblink valid cross-setting profile");
                    string validProfile = File.ReadAllText(ini);
                    SmokeProcess(
                        exe,
                        new[] { "--configure-only", "--no-tray" },
                        new[] { 1 },
                        30,
                        "asusblink rejected cross-setting profile");
                    if (!String.Equals(validProfile, File.ReadAllText(ini), StringComparison.Ordinal))
                        throw new InvalidOperationException("asusblink persisted an invalid pause-without-tray profile before rejecting it.");

                    File.WriteAllText(
                        ini,
                        "[Options]" + Environment.NewLine +
                        "error-log=off" + Environment.NewLine +
                        "error-retry=4" + Environment.NewLine +
                        "no-tray=" + Environment.NewLine,
                        new UTF8Encoding(false));
                    SmokeProcess(
                        exe,
                        new[] { "--configure-only" },
                        new[] { 0 },
                        30,
                        "asusblink legacy Options migration");
                    AssertFileContains(ini, "[Settings]", "asusblink must create the canonical section during legacy migration");
                    AssertFileContains(ini, "ErrorLog=off", "asusblink must migrate the legacy log setting");
                    AssertFileContains(ini, "ErrorRetry=4", "asusblink must migrate the legacy retry setting");
                    AssertFileContains(ini, "ShowTrayIcon=false", "asusblink must preserve the legacy empty no-tray flag");
                    deleteTemp = true;
                }
                finally
                {
                    if (deleteTemp)
                    {
                        try { Directory.Delete(tempRoot, true); } catch { }
                    }
                    else
                    {
                        Warn("asusblink smoke temp directory preserved for diagnostics: " + tempRoot);
                    }
                }
            }
            else
            {
                Console.WriteLine("skip - asusblink command-line smoke tests not run; artifact not built");
            }
        }

        if (ProductOwnedDependencyOwner(
                "dependencies/DNSAutoUpdate/dns_auto_update_app.cs") !=
                "DNSAutoUpdate" ||
            ProductOwnedDependencyOwner("dependencies/DNSAutoUpdate") !=
                "DNSAutoUpdate" ||
            ProductOwnedDependencyOwner("dependencies/managed_ini.cs") != null)
        {
            throw new InvalidOperationException(
                "Product-owned dependency classification is incorrect.");
        }
        foreach (var project in projects)
        {
            foreach (string dependencyPath in
                project.dependencyPaths ?? new string[0])
            {
                string owner = ProductOwnedDependencyOwner(dependencyPath);
                if (!IsSharedEngineHostDeclaration(project.key, dependencyPath) && owner != null && !owner.Equals(
                    project.key,
                    StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        "Workflow dependency ownership test found " +
                        dependencyPath + " under " + project.key + ".");
                }

                string normalizedDependency = dependencyPath
                    .Replace('\\', '/')
                    .TrimEnd('/');
                string fullDependency = Path.Combine(
                    root,
                    normalizedDependency.Replace(
                        '/',
                        Path.DirectorySeparatorChar));
                if (owner != null && Directory.Exists(fullDependency))
                {
                    string probeFile = IsSharedEngineHostDeclaration(project.key, dependencyPath)
                        ? Path.Combine(fullDependency, "service.cpp") : Directory.EnumerateFiles(
                        fullDependency,
                        "*",
                        SearchOption.AllDirectories).FirstOrDefault();
                    if (probeFile == null)
                    {
                        throw new InvalidOperationException(
                            "Product dependency directory is empty: " +
                            dependencyPath);
                    }

                    string changedPath = Rel(root, probeFile)
                        .Replace('\\', '/');
                    List<Project> consumers =
                        ProjectsUsingChangedDependencies(
                            root,
                            projects,
                            new List<string> { changedPath });
                    var expectedConsumers = projects.Where(p => p.key.Equals(owner, StringComparison.OrdinalIgnoreCase) ||
                        ((IsSharedDiscordServiceFile(changedPath) || IsSharedAsusEngineFile(changedPath)) && p.key == "DesktopStub"))
                        .Select(p => p.key).OrderBy(key => key).ToArray();
                    if (!consumers.Select(p => p.key).OrderBy(key => key).SequenceEqual(expectedConsumers))
                    {
                        throw new InvalidOperationException(
                            "Dependency consumer selection does not match the declared owners for " + project.key + ": " +
                            dependencyPath);
                    }
                }
            }
        }

        Project capsBlink = projects.FirstOrDefault(p => p.key == "capsblink");
        if (capsBlink != null)
        {
            string sourceExe = Path.Combine(root, capsBlink.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                VerifyBinaryVersion(sourceExe, "capsblink");
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "capsblinkSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                bool deleteTemp = false;
                try
                {
                    string exe = Path.Combine(tempRoot, Path.GetFileName(sourceExe));
                    File.Copy(sourceExe, exe, true);
                    string ini = Path.Combine(tempRoot, "capsblink.ini");
                    string log = Path.Combine(tempRoot, "capsblink.log");

                    ProcessResult helpResult = SmokeProcess(
                        exe,
                        new[] { "--help", "--startup", "--set", "Settings.BlinkIntervalMs=invalid" },
                        new[] { 0 },
                        30,
                        "capsblink help");
                    if (helpResult.Output.IndexOf("--set Settings.Key=Value", StringComparison.Ordinal) < 0 ||
                        helpResult.Output.IndexOf("--configure-only", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("capsblink --help did not expose its persistent setting surface.");
                    AssertFileDoesNotExist(ini, "capsblink --help must be side-effect-free");
                    AssertFileDoesNotExist(log, "capsblink --help must not create a log");

                    ProcessResult versionResult = SmokeProcess(
                        exe,
                        new[] { "--version", "--startup" },
                        new[] { 0 },
                        30,
                        "capsblink version");
                    if (versionResult.Output.IndexOf("capsblink 1", StringComparison.OrdinalIgnoreCase) < 0)
                        throw new InvalidOperationException("capsblink --version did not report version 1.");
                    AssertFileDoesNotExist(ini, "capsblink --version must be side-effect-free");
                    AssertFileDoesNotExist(log, "capsblink --version must not create a log");

                    SmokeProcess(
                        exe,
                        new[] { "--configure-only", "--no-tray", "--blink-interval-ms", "49" },
                        new[] { 2 },
                        30,
                        "capsblink invalid typed setting batch");
                    AssertFileDoesNotExist(ini, "capsblink must reject an invalid typed batch before creating its INI");
                    AssertFileDoesNotExist(log, "capsblink invalid syntax must not create a log");

                    SmokeProcess(
                        exe,
                        new[]
                        {
                            "--configure-only",
                            "--no-startup",
                            "--no-tray",
                            "--flat-menu",
                            "--blink-interval-ms", "750"
                        },
                        new[] { 0 },
                        30,
                        "capsblink configure-only setting batch");
                    AssertFileContains(ini, "BlinkIntervalMs=750", "capsblink must persist its typed interval");
                    AssertFileContains(ini, "RunAtStartup=false", "capsblink must persist its Startup preference");
                    AssertFileContains(ini, "ShowTrayIcon=false", "capsblink must persist its tray preference");
                    AssertFileContains(ini, "ShowMenuAsDropdown=false", "capsblink must persist its tray layout");
                    deleteTemp = true;
                }
                finally
                {
                    if (deleteTemp)
                    {
                        try { Directory.Delete(tempRoot, true); } catch { }
                    }
                    else
                    {
                        Warn("capsblink smoke temp directory preserved for diagnostics: " + tempRoot);
                    }
                }
            }
            else
            {
                Console.WriteLine("skip - capsblink command-line smoke tests not run; artifact not built");
            }
        }

        Project dnsAutoUpdate = projects.FirstOrDefault(p => p.key == "DNSAutoUpdate");
        if (dnsAutoUpdate != null)
        {
            string sourceExe = Path.Combine(root, dnsAutoUpdate.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                VerifyBinaryVersion(sourceExe, "DNSAutoUpdate");
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "DNSAutoUpdateSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                bool deleteTemp = false;
                Process resident = null;
                try
                {
                    string exe = Path.Combine(tempRoot, Path.GetFileName(sourceExe));
                    File.Copy(sourceExe, exe, true);
                    string ini = Path.Combine(tempRoot, "DNSAutoUpdate.ini");
                    string log = Path.Combine(tempRoot, "DNSAutoUpdate.log");

                    ProcessResult helpResult = SmokeProcess(
                        exe,
                        new[] { "--help", "--enabled", "--set", "Settings.SleepSeconds=invalid" },
                        new[] { 0 },
                        30,
                        "DNSAutoUpdate help");
                    if (helpResult.Output.IndexOf("--set Settings.Key=Value", StringComparison.Ordinal) < 0 ||
                        helpResult.Output.IndexOf("--configure-only", StringComparison.Ordinal) < 0 ||
                        helpResult.Output.IndexOf("shell:startup", StringComparison.OrdinalIgnoreCase) < 0)
                        throw new InvalidOperationException("DNSAutoUpdate --help did not expose the shared persistent/control surface.");
                    AssertFileDoesNotExist(ini, "DNSAutoUpdate --help must be side-effect-free");
                    AssertFileDoesNotExist(log, "DNSAutoUpdate --help must not create a log");

                    ProcessResult versionResult = SmokeProcess(
                        exe,
                        new[] { "--version", "--enabled" },
                        new[] { 0 },
                        30,
                        "DNSAutoUpdate version");
                    if (versionResult.Output.IndexOf("DNSAutoUpdate 1", StringComparison.OrdinalIgnoreCase) < 0)
                        throw new InvalidOperationException("DNSAutoUpdate --version did not report version 1.");
                    AssertFileDoesNotExist(ini, "DNSAutoUpdate --version must be side-effect-free");

                    SmokeProcess(
                        exe,
                        new[] { "--enabled", "--configure-only" },
                        new[] { 1 },
                        30,
                        "DNSAutoUpdate rejects enabled profile without an owner allowlist");
                    AssertFileDoesNotExist(ini, "DNSAutoUpdate must reject an unsafe enabled profile before creating its INI");
                    AssertFileDoesNotExist(log, "DNSAutoUpdate invalid configuration must not create a log");

                    SmokeProcess(
                        exe,
                        new[] { "--include-ip-address", "192.168.1", "--configure-only" },
                        new[] { 2 },
                        30,
                        "DNSAutoUpdate rejects non-dotted-decimal IPv4");
                    AssertFileDoesNotExist(ini, "DNSAutoUpdate must reject invalid IP syntax before creating its INI");

                    SmokeProcess(
                        exe,
                        new[]
                        {
                            "--zone-name", "example.test",
                            "--managed-record-name", "@,app",
                            "--disabled",
                            "--apply",
                            "--no-confirm",
                            "--no-startup",
                            "--tray",
                            "--flat-menu",
                            "--sleep-seconds", "60",
                            "--configure-only"
                        },
                        new[] { 0 },
                        30,
                        "DNSAutoUpdate safe configure-only batch");
                    AssertFileContains(ini, "ZoneName=example.test", "DNSAutoUpdate must persist its zone");
                    AssertFileContains(ini, "ManagedRecordName=@,app", "DNSAutoUpdate must persist its exact owner allowlist");
                    AssertFileContains(ini, "Enabled=false", "DNSAutoUpdate smoke profile must remain inert");
                    AssertFileContains(ini, "ShowTrayIcon=true", "DNSAutoUpdate must persist tray visibility");
                    AssertFileContains(ini, "ShowMenuAsDropdown=false", "DNSAutoUpdate must persist tray layout");
                    AssertFileDoesNotExist(log, "DNSAutoUpdate configure-only must not create a log");

                    var psi = new ProcessStartInfo(exe, "")
                    {
                        WorkingDirectory = tempRoot,
                        UseShellExecute = false,
                        CreateNoWindow = true
                    };
                    resident = new Process { StartInfo = psi };
                    if (!resident.Start())
                        throw new InvalidOperationException("Failed to start DNSAutoUpdate inert tray resident.");
                    WaitForFileText(log, "Resident DNS updater starting", 15000,
                        "DNSAutoUpdate inert tray resident startup");
                    if (resident.HasExited)
                        throw new InvalidOperationException("DNSAutoUpdate inert tray resident exited unexpectedly.");
                    SmokeProcess(exe, new[] { "--run-now" }, new[] { 0 }, 10,
                        "DNSAutoUpdate resident run-now control");
                    SmokeProcess(exe, new[] { "--reload" }, new[] { 0 }, 10,
                        "DNSAutoUpdate resident reload control");
                    SmokeProcess(exe, new[] { "--exit" }, new[] { 0 }, 10,
                        "DNSAutoUpdate resident exit control");
                    if (!resident.WaitForExit(15000))
                        throw new TimeoutException("DNSAutoUpdate resident did not stop after --exit.");
                    if (resident.ExitCode != 0)
                        throw new InvalidOperationException("DNSAutoUpdate resident exited " + resident.ExitCode + ".");
                    Console.WriteLine("ok - DNSAutoUpdate inert tray/resident controls without DNS access");
                    deleteTemp = true;
                }
                finally
                {
                    if (resident != null)
                    {
                        if (!resident.HasExited)
                        {
                            try { resident.Kill(); } catch { }
                            try { resident.WaitForExit(5000); } catch { }
                        }
                        resident.Dispose();
                    }
                    if (deleteTemp)
                    {
                        try { Directory.Delete(tempRoot, true); } catch { }
                    }
                    else
                    {
                        Warn("DNSAutoUpdate smoke temp directory preserved for diagnostics: " + tempRoot);
                    }
                }
            }
            else
            {
                Console.WriteLine("skip - DNSAutoUpdate command-line/tray smoke tests not run; artifact not built");
            }
        }

        Project nowPlayingTile = projects.FirstOrDefault(p => p.key == "NowPlayingTile");
        if (nowPlayingTile != null)
        {
            string sourceExe = Path.Combine(root, nowPlayingTile.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "NowPlayingTileSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                bool deleteTemp = false;
                try
                {
                    string exe = Path.Combine(tempRoot, Path.GetFileName(sourceExe));
                    File.Copy(sourceExe, exe, true);
                    string ini = Path.Combine(tempRoot, "NowPlayingTile.ini");

                    ProcessResult helpResult = SmokeProcess(
                        exe,
                        new[] { "--help", "--startup", "--set", "Settings.TileLayout=invalid" },
                        new[] { 0 },
                        30,
                        "NowPlayingTile help");
                    if (helpResult.Output.IndexOf("--startup", StringComparison.Ordinal) < 0 ||
                        helpResult.Output.IndexOf("--set Settings.Key=Value", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("NowPlayingTile --help did not expose the common typed setting surface.");
                    AssertFileDoesNotExist(ini, "NowPlayingTile --help must be side-effect-free");

                    ProcessResult versionResult = SmokeProcess(
                        exe,
                        new[] { "--version", "--startup" },
                        new[] { 0 },
                        30,
                        "NowPlayingTile version");
                    if (versionResult.Output.IndexOf("NowPlayingTile 1", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("NowPlayingTile --version did not report version 1.");
                    AssertFileDoesNotExist(ini, "NowPlayingTile --version must be side-effect-free");

                    SmokeProcess(
                        exe,
                        new[] { "--set", "Settings.RunAtStartup=maybe", "--exit" },
                        new[] { 2 },
                        30,
                        "NowPlayingTile invalid Startup boolean");
                    AssertFileDoesNotExist(ini, "NowPlayingTile must reject invalid Startup values before creating an INI or changing StartupTask state");

                    SmokeProcess(
                        exe,
                        new[]
                        {
                            "--set", "Settings.TileLayout=combined",
                            "--tray",
                            "--dropdown",
                            "--update-interval", "5",
                            "--tile-refresh", "120",
                            "--exit"
                        },
                        new[] { 2 },
                        30,
                        "NowPlayingTile typed setting batch");
                    AssertFileContains(ini, "\"TileLayout\" = \"Combined\"", "NowPlayingTile must canonicalize and persist TileLayout");
                    AssertFileContains(ini, "\"ShowTrayIcon\" = \"true\"", "NowPlayingTile --tray must persist the tray setting");
                    AssertFileContains(ini, "\"ShowMenuAsDropdown\" = \"true\"", "NowPlayingTile --dropdown must persist the tray layout setting");
                    AssertFileContains(ini, "\"UpdateIntervalSeconds\" = \"5\"", "NowPlayingTile must persist its polling interval");
                    AssertFileContains(ini, "\"TileRefreshSeconds\" = \"120\"", "NowPlayingTile must persist its tile refresh interval");
                    deleteTemp = true;
                }
                finally
                {
                    if (deleteTemp)
                    {
                        try { Directory.Delete(tempRoot, true); } catch { }
                    }
                    else
                    {
                        Warn("NowPlayingTile smoke temp directory preserved for diagnostics: " + tempRoot);
                    }
                }
            }
            else
            {
                Console.WriteLine("skip - NowPlayingTile command-line smoke tests not run; artifact not built");
            }
        }

        Project desktopStub = projects.FirstOrDefault(p => p.key == "DesktopStub");
        if (desktopStub != null)
        {
            string smokePath = String.IsNullOrWhiteSpace(desktopStub.smokePath) ? desktopStub.artifactPath : desktopStub.smokePath;
            string sourceExe = String.IsNullOrWhiteSpace(desktopStubBinaryOverride)
                ? Path.Combine(root, smokePath.Replace('/', Path.DirectorySeparatorChar))
                : Path.GetFullPath(desktopStubBinaryOverride);
            if (File.Exists(sourceExe))
            {
                string desktopStubVersion = VerifyBinaryVersion(sourceExe, "DesktopStub");
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "DesktopStubSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                bool deleteTemp = false;
                string smokeIdentity = null;
                try
                {
                    string exe = Path.Combine(tempRoot, "DesktopStub.exe");
                    File.Copy(sourceExe, exe, true);
                    string sourceBuildDir = Path.GetDirectoryName(sourceExe);
                    // Manifest validation needs the helper files present. Copying
                    // them is inert; default smoke never starts either helper.
                    foreach (string helper in new[] { "DesktopStubLiveTileBroker.exe", "DesktopStubAppxStub.exe" })
                    {
                        string sourceHelper = helper == "DesktopStubLiveTileBroker.exe" && !String.IsNullOrWhiteSpace(desktopStubBrokerOverride)
                            ? Path.GetFullPath(desktopStubBrokerOverride) : Path.Combine(sourceBuildDir, helper);
                        if (File.Exists(sourceHelper))
                        {
                            if (helper == "DesktopStubLiveTileBroker.exe")
                            {
                                string helperVersion = VerifyBinaryVersion(sourceHelper, helper);
                                if (!String.Equals(helperVersion, desktopStubVersion, StringComparison.Ordinal))
                                    throw new InvalidOperationException("DesktopStub helper version does not match DesktopStub.exe: " + helperVersion + " != " + desktopStubVersion);
                            }
                            File.Copy(sourceHelper, Path.Combine(tempRoot, helper), true);
                        }
                    }

                    string ini = Path.Combine(tempRoot, "DesktopStub.ini");
                    string helpIni = Path.Combine(tempRoot, "HelpSideEffect.ini");
                    string customHelpIni = Path.Combine(tempRoot, "CustomHelp.ini");
                    string concurrentIni = Path.Combine(tempRoot, "ConcurrentWrites.ini");
                    string invalidBrandingIni = Path.Combine(tempRoot, "InvalidBranding.ini");
                    string invalidStartupIni = Path.Combine(tempRoot, "InvalidStartup.ini");
                    string invalidRssOptionIni = Path.Combine(tempRoot, "InvalidRssOption.ini");
                    string invalidRssProfileIni = Path.Combine(tempRoot, "InvalidRssProfile.ini");
                    string helperManifestIni = Path.Combine(tempRoot, "HelperManifest.ini");
                    string legacyManifestIni = Path.Combine(tempRoot, "LegacyManifest.ini");
                    string wallpaper = Path.Combine(tempRoot, "wallpaper.bmp");
                    string trailingSpaces = new string(' ', 2);
                    WriteTestBmp(wallpaper);

                    ProcessResult versionResult = SmokeProcess(exe, new[] { "--version" }, new[] { 0 }, 30, "DesktopStub version");
                    string expectedTag = "DesktopStub-v" + desktopStubVersion.Split('.')[0];
                    if (versionResult.Output.IndexOf(expectedTag + " (" + desktopStubVersion + ")", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DesktopStub --version did not report the expected release tag/version: " + expectedTag + " (" + desktopStubVersion + ")");
                    ProcessResult helpResult = SmokeProcess(exe, new[] { "--help", "--ini", helpIni, "--startup", "--set", "Settings.TrayIcon=0" }, new[] { 0 }, 30, "DesktopStub help");
                    if (helpResult.Output.IndexOf("--startup", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DesktopStub --help did not expose the Startup setting.");
                    if (helpResult.Output.IndexOf("--content-source", StringComparison.Ordinal) < 0 ||
                        helpResult.Output.IndexOf("--rss-feed-url", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DesktopStub --help did not expose the RSS content-source settings.");
                    if (!allowPackageIntegration && (helpResult.Output.IndexOf("--render-only", StringComparison.Ordinal) < 0 ||
                        helpResult.Output.IndexOf("--configure-only", StringComparison.Ordinal) < 0))
                        throw new InvalidOperationException("The selected DesktopStub binary lacks the offline smoke entry points; rebuild it first.");
                    AssertFileDoesNotExist(helpIni, "DesktopStub --help must be side-effect-free");
                    if (!allowPackageIntegration) VerifyDesktopStubOfflineGuards(exe, tempRoot);
                    string customHelpText = "[CommandLineHelp]\r\n\"Template\" = \"CUSTOM_HELP_MARKER {exe}\"\r\n";
                    File.WriteAllText(customHelpIni, customHelpText, new UTF8Encoding(true));
                    byte[] customHelpBefore = File.ReadAllBytes(customHelpIni);
                    ProcessResult customHelpResult = SmokeProcess(exe, new[] { "--help", "--ini", customHelpIni, "--set", "Settings.TrayIcon=0" }, new[] { 0 }, 30, "DesktopStub custom help");
                    if (customHelpResult.Output.IndexOf("CUSTOM_HELP_MARKER", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DesktopStub --help did not use the configured INI template.");
                    if (!customHelpBefore.SequenceEqual(File.ReadAllBytes(customHelpIni)))
                        throw new InvalidOperationException("DesktopStub --help modified its configured INI.");
                    SmokeProcess(exe, new[] { "--ini", customHelpIni, "--regenerate-manifest" }, new[] { 0 }, 30, "DesktopStub custom help preservation");
                    AssertFileContains(customHelpIni, "CUSTOM_HELP_MARKER {exe}", "DesktopStub startup migration must preserve customized command-line help");
                    string legacyManifestText =
                        "[Manifest]\r\n" +
                        "; Preserve user comments and unknown legacy values.\r\n" +
                        "\"CustomLegacyValue\" = \"keep-me\"\r\n";
                    File.WriteAllText(legacyManifestIni, legacyManifestText, new UTF8Encoding(true));
                    SmokeProcess(exe, new[] { "--ini", legacyManifestIni, "--regenerate-manifest" }, new[] { 0 }, 30, "DesktopStub legacy Manifest preservation");
                    AssertFileContains(legacyManifestIni, "[Manifest]", "DesktopStub must preserve an existing legacy Manifest section");
                    AssertFileContains(legacyManifestIni, "; Preserve user comments and unknown legacy values.", "DesktopStub must preserve comments in legacy configuration");
                    AssertFileContains(legacyManifestIni, "\"CustomLegacyValue\" = \"keep-me\"", "DesktopStub must preserve unknown legacy configuration values");
                    SmokeProcess(exe, new[] { "--ini", ini, "--no-tray", "--console", "--logging", "--notifications", "--live-tile-mode", "Auto", "--live-tile-template", "Windows81Preset", "--live-tile-branding", "NameAndLogo", "--rss-feed-url", "https://example.com/feed.xml", "--rss-user-agent", "DesktopStub-Smoke/1.0", "--rss-update-interval", "300", "--rss-max-items", "10", "--rss-http-timeout", "30", "--rss-max-feed-bytes", "1048576", "--scales", "auto", "--asset", "MediumTile=1", "--set", "Strings.TrayTip=DesktopStub" + trailingSpaces, "--regenerate-manifest" }, new[] { 0 }, 30, "DesktopStub settings and manifest");
                    AssertFileContains(ini, "\"TrayTip\" = \"DesktopStub" + trailingSpaces + "\"", "DesktopStub --set must preserve trailing value spaces");
                    AssertFileContains(ini, "\"LiveTileTemplateStyle\" = \"Windows81Preset\"", "DesktopStub must persist the selected Windows 10 Live Tile template style");
                    AssertFileContains(ini, "\"LiveTileBranding\" = \"NameAndLogo\"", "DesktopStub must persist the selected Windows 10 Live Tile branding");
                    AssertFileContains(ini, "\"ContentSource\" = \"Wallpaper\"", "DesktopStub must generate the canonical wallpaper content-source default");
                    AssertFileContains(ini, "\"FeedUrl\" = \"https://example.com/feed.xml\"", "DesktopStub must persist the dedicated RSS URL option");
                    AssertFileContains(ini, "\"UpdateIntervalSeconds\" = \"300\"", "DesktopStub must persist typed RSS interval values");
                    AssertFileContains(ini, "\"MaxFeedBytes\" = \"1048576\"", "DesktopStub must persist typed RSS response limits");
                    SmokeProcess(exe, new[] { "--ini", invalidBrandingIni, "--live-tile-branding", "invalid" }, new[] { 2 }, 30, "DesktopStub invalid Live Tile branding");
                    AssertFileDoesNotExist(invalidBrandingIni, "DesktopStub must reject invalid Live Tile branding without creating an INI");
                    SmokeProcess(exe, new[] { "--ini", invalidStartupIni, "--set", "Settings.RunAtStartup=maybe" }, new[] { 2 }, 30, "DesktopStub invalid Startup boolean");
                    AssertFileDoesNotExist(invalidStartupIni, "DesktopStub must reject invalid Settings.RunAtStartup without creating an INI or Startup shortcut");
                    SmokeProcess(exe, new[] { "--ini", invalidRssOptionIni, "--rss-update-interval", "59" }, new[] { 2 }, 30, "DesktopStub invalid RSS interval");
                    AssertFileDoesNotExist(invalidRssOptionIni, "DesktopStub must reject invalid typed RSS options before creating an INI");
                    File.WriteAllText(
                        invalidRssProfileIni,
                        "[Settings]\r\nContentSource=RssFeed\r\n[RssFeed]\r\nFeedUrl=ftp://example.com/feed.xml\r\n",
                        new UTF8Encoding(true));
                    SmokeProcess(exe, new[] { "--ini", invalidRssProfileIni, "--regenerate-manifest" }, new[] { 2 }, 30, "DesktopStub invalid RSS INI profile");
                    string manifestPath = Path.Combine(Path.GetDirectoryName(exe), "AppxManifest.xml");
                    SmokeProcess(exe, new[] { "--ini", helperManifestIni, "--manifest-win81", "--win8-broker" }, new[] { 0 }, 30, "DesktopStub Win8 broker manifest");
                    AssertFileContains(manifestPath, "DesktopStubLiveTileBroker.exe", "DesktopStub Win8 broker switch must regenerate the manifest with the broker executable");
                    SmokeProcess(exe, new[] { "--ini", helperManifestIni, "--no-win8-broker" }, new[] { 0 }, 30, "DesktopStub Win8 activation stub manifest");
                    AssertFileContains(manifestPath, "DesktopStubAppxStub.exe", "DesktopStub Win8 broker switch must regenerate the manifest with the activation stub");
                    SmokeProcess(exe, new[] { "--ini", helperManifestIni, "--win8-oop-helper" }, new[] { 0 }, 30, "DesktopStub Win8 OOP helper manifest");
                    AssertFileContains(manifestPath, "windows.activatableClass.outOfProcessServer", "DesktopStub Win8 OOP helper switch must regenerate the manifest extension");
                    smokeIdentity = "dev.local.desktopstubsmoke." + Guid.NewGuid().ToString("N").Substring(0, 16);
                    SetDesktopStubManifestIdentity(manifestPath, smokeIdentity);
                    SmokeProcess(exe, new[]
                        {
                            "--ini", ini,
                            "--no-live-tile",
                            "--asset", "MediumTile=1",
                            "--asset", "WideTile=1",
                            "--asset", "LargeTile=1",
                            "--set", "TileText.Enabled=0",
                            "--wallpaper", wallpaper,
                            allowPackageIntegration ? "--once" : "--render-only", "--no-tray", "--no-monitor"
                        }, new[] { 0 }, 30, "DesktopStub static-tile generation with Live Tile disabled");
                    AssertFileContains(ini, "\"ExperimentalLiveTileUpdate\" = \"Registration\"", "DesktopStub --no-live-tile must persist the static Registration mode used by the TileText smoke test");
                    Dictionary<string, TilePixelSnapshot> tilesWithoutText = CaptureDesktopStubTileAssets(tempRoot, "TileText-disabled baseline");
                    VerifyDesktopStubTileTextRendering(root, exe, ini, wallpaper, tempRoot, tilesWithoutText);
                    VerifyDesktopStubComposedContent(exe, ini, wallpaper, tempRoot);
                    SmokeProcess(exe, new[] { "--ini", ini, "--wallpaper", Path.Combine(tempRoot, "missing.png"), "--no-tray" }, new[] { 2 }, 30, "DesktopStub invalid wallpaper guard");
                    if (allowPackageIntegration) VerifyDesktopStubSecondLaunchActions(exe, tempRoot);
                    VerifyDesktopStubConcurrentIniWrites(exe, concurrentIni);

                    if (!File.Exists(manifestPath))
                        throw new InvalidOperationException("DesktopStub smoke test did not create AppxManifest.xml.");
                    string manifestAfterSmoke = File.ReadAllText(manifestPath, Encoding.UTF8);
                    VerifyManifestVersion(manifestAfterSmoke, desktopStubVersion, "DesktopStub");
                    if (!Regex.IsMatch(manifestAfterSmoke, @"<\s*Identity\b[^>]*\bName\s*=\s*[""']" + Regex.Escape(smokeIdentity) + @"[""']", RegexOptions.IgnoreCase))
                        throw new InvalidOperationException("DesktopStub smoke test did not preserve the custom AppxManifest.xml package identity.");
                    deleteTemp = true;
                }
                finally
                {
                    if (allowPackageIntegration && !String.IsNullOrWhiteSpace(smokeIdentity))
                        UnregisterDesktopStubSmokePackage(smokeIdentity, tempRoot);

                    if (deleteTemp)
                    {
                        try { Directory.Delete(tempRoot, true); } catch { }
                    }
                    else
                    {
                        Warn("DesktopStub smoke temp directory preserved for diagnostics: " + tempRoot);
                    }
                }
            }
            else
            {
                Console.WriteLine("skip - DesktopStub UI and command-line smoke tests not run; artifact not built");
            }
        }

        Project charmTray = projects.FirstOrDefault(p => p.key == "CharmTray");
        if (charmTray != null)
        {
            string sourceExe = Path.Combine(root, charmTray.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "CharmTraySmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                try
                {
                    string exe = Path.Combine(tempRoot, Path.GetFileName(sourceExe));
                    File.Copy(sourceExe, exe, true);
                    string helpIni = Path.Combine(tempRoot, "HelpSideEffect.ini");
                    ProcessResult helpResult = SmokeProcess(
                        exe,
                        new[] { "--help", "--ini", helpIni, "--startup" },
                        new[] { 0 },
                        30,
                        "CharmTray help");
                    if (helpResult.Output.IndexOf("Usage: CharmTray.exe", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("CharmTray --help did not emit its usage text.");
                    AssertFileDoesNotExist(helpIni, "CharmTray --help must be side-effect-free");

                    ProcessResult versionResult = SmokeProcess(
                        exe,
                        new[] { "--version", "--ini", helpIni, "--startup" },
                        new[] { 0 },
                        30,
                        "CharmTray version");
                    if (versionResult.Output.IndexOf("CharmTray 1", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("CharmTray --version did not report version 1.");
                    AssertFileDoesNotExist(helpIni, "CharmTray --version must be side-effect-free");

                    SmokeProcess(
                        exe,
                        new[] { "--ini", helpIni, "--bool", "Settings.RunAtStartup=maybe" },
                        new[] { 2 },
                        30,
                        "CharmTray typed boolean validation");
                    AssertFileDoesNotExist(helpIni, "CharmTray must reject invalid booleans before creating an INI");
                }
                finally
                {
                    try { Directory.Delete(tempRoot, true); } catch { }
                }
            }
            else
            {
                Console.WriteLine("skip - CharmTray command-line smoke tests not run; artifact not built");
            }
        }

        Project discordRpc = projects.FirstOrDefault(p => p.key == "DiscordRPC");
        if (discordRpc != null)
        {
            string sourceExe = Path.Combine(root, discordRpc.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                string discordVersion = VerifyBinaryVersion(sourceExe, "DiscordRPC");
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "DiscordRPCSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                bool deleteTemp = false;
                try
                {
                    string exe = Path.Combine(tempRoot, Path.GetFileName(sourceExe));
                    File.Copy(sourceExe, exe, true);
                    string helpIni = Path.Combine(tempRoot, "HelpSideEffect.ini");
                    string customHelpIni = Path.Combine(tempRoot, "CustomHelp.ini");
                    string versionIni = Path.Combine(tempRoot, "VersionSideEffect.ini");
                    string redactIni = Path.Combine(tempRoot, "Redact.ini");
                    string redactLog = Path.Combine(tempRoot, "Redact.log");
                    string dottedIni = Path.Combine(tempRoot, "DottedSet.ini");
                    string typedBooleanIni = Path.Combine(tempRoot, "TypedBoolean.ini");
                    string invalidBooleanIni = Path.Combine(tempRoot, "InvalidBoolean.ini");
                    string trailingSpaces = new string(' ', 2);

                    SmokeProcess(exe, new[] { "--help", "--ini", helpIni, "--startup", "--set", "general.token=should-not-be-written" }, new[] { 0 }, 30, "DiscordRPC help");
                    AssertFileDoesNotExist(helpIni, "DiscordRPC --help must be side-effect-free");
                    File.WriteAllText(
                        customHelpIni,
                        "[CommandLineHelp]\r\n\"Template\" = \"CUSTOM_DISCORD_HELP {exe}\\\\nSecond line\"\r\n",
                        new UnicodeEncoding(false, true));
                    byte[] customHelpBefore = File.ReadAllBytes(customHelpIni);
                    ProcessResult customHelpResult = SmokeProcess(exe, new[] { "--help", "--ini", customHelpIni }, new[] { 0 }, 30, "DiscordRPC custom help");
                    if (customHelpResult.Output.IndexOf("CUSTOM_DISCORD_HELP " + Path.GetFileName(exe), StringComparison.Ordinal) < 0 ||
                        customHelpResult.Output.IndexOf("Second line", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DiscordRPC --help did not use the configured INI template.");
                    if (!customHelpBefore.SequenceEqual(File.ReadAllBytes(customHelpIni)))
                        throw new InvalidOperationException("DiscordRPC --help modified its configured INI.");
                    ProcessResult versionResult = SmokeProcess(exe, new[] { "--version", "--ini", versionIni, "--startup", "--set", "general.token=should-not-be-written" }, new[] { 0 }, 30, "DiscordRPC version");
                    string expectedTag = "DiscordRPC-v" + discordVersion.Split('.')[0];
                    if (versionResult.Output.IndexOf(expectedTag + " (" + discordVersion + ")", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DiscordRPC --version did not report the expected release tag/version: " + expectedTag + " (" + discordVersion + ")");
                    AssertFileDoesNotExist(versionIni, "DiscordRPC --version must be side-effect-free");
                    SmokeProcess(exe, new[] { "--tokenXYZ", "abc" }, new[] { 2 }, 30, "DiscordRPC strict option parsing");
                    SmokeProcess(exe, new[] { "--ini", redactIni, "--client-id", "123456789012345678", "--token", "super-secret-smoke-token", "--dry-run", "--no-tray" }, new[] { 0 }, 30, "DiscordRPC token redaction");
                    AssertFileDoesNotContain(redactLog, "super-secret-smoke-token", "DiscordRPC command-line token must not be written to the log");
                    SmokeProcess(exe, new[] { "--ini", tempRoot, "--set", "general.client_id=123456789012345678", "--dry-run", "--no-tray" }, new[] { 2 }, 30, "DiscordRPC directory config path validation");
                    SmokeProcess(exe, new[] { "--ini", dottedIni, "--set", "section.with.dot.key=value" + trailingSpaces, "--dry-run", "--no-tray" }, new[] { 0 }, 30, "DiscordRPC dotted --set parsing");
                    AssertFileContains(dottedIni, "[section.with.dot]", "DiscordRPC --set must split Section.Key at the last dot before '='");
                    AssertFileContains(dottedIni, "\"key\" = \"value" + trailingSpaces + "\"", "DiscordRPC --set must preserve dotted section names and trailing value spaces");
                    SmokeProcess(exe, new[] { "--ini", typedBooleanIni, "--set", "app.show_tray=YES", "--dry-run", "--no-tray" }, new[] { 0 }, 30, "DiscordRPC typed boolean canonicalization");
                    AssertFileContains(typedBooleanIni, "\"show_tray\" = \"true\"", "DiscordRPC must canonicalize known boolean --set values");
                    SmokeProcess(exe, new[] { "--ini", invalidBooleanIni, "--set", "app.run_at_startup=maybe", "--dry-run", "--no-tray" }, new[] { 2 }, 30, "DiscordRPC invalid known boolean rejection");
                    AssertFileDoesNotExist(invalidBooleanIni, "DiscordRPC must reject invalid known booleans before creating an INI or Startup shortcut");
                    SmokeProcess(exe, new[] { "--dry-run", "--no-tray" }, new[] { 0 }, 30, "DiscordRPC dry-run");
                    if (allowPackageIntegration) VerifyDiscordRpcNoTrayControl(exe, tempRoot);
                    else Console.WriteLine("skip - Discord IPC resident integration is excluded from default offline smoke");
                    deleteTemp = true;
                }
                finally
                {
                    if (deleteTemp)
                    {
                        try { Directory.Delete(tempRoot, true); } catch { }
                    }
                    else
                    {
                        Warn("DiscordRPC smoke temp directory preserved for diagnostics: " + tempRoot);
                    }
                }
            }
        }

        foreach (var p in projects.Where(p => p.key != "DesktopStub"))
        {
            string path = Path.Combine(root, p.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(path))
            {
                var item = new FileInfo(path);
                if (item.Length <= 0)
                    throw new InvalidOperationException("Smoke test failed: empty artifact " + p.artifactPath);
                Console.WriteLine("ok - " + p.label + " artifact exists (" + item.Length + " bytes)");
            }
            else
            {
                Console.WriteLine("skip - " + p.label + " not built");
            }
        }

        Console.WriteLine("Windows build smoke tests completed.");
        return 0;
    }

    static string VerifyBinaryVersion(string file, string label)
    {
        var info = FileVersionInfo.GetVersionInfo(file);
        string fileVersion = NormalizeFourPartVersion(info.FileVersion);
        string productVersion = NormalizeFourPartVersion(info.ProductVersion);
        if (fileVersion == null)
            throw new InvalidOperationException(label + " has no valid four-part FileVersion resource: " + file);
        if (productVersion == null)
            throw new InvalidOperationException(label + " has no valid four-part ProductVersion resource: " + file);
        if (!String.Equals(fileVersion, productVersion, StringComparison.Ordinal))
            throw new InvalidOperationException(label + " FileVersion and ProductVersion differ: " + fileVersion + " != " + productVersion);
        return fileVersion;
    }

    static string EmbeddedBinaryReleaseTag(Project project, List<string> files)
    {
        if (project == null ||
            !(project.key == "DesktopStub" ||
              project.key == "DiscordRPC"))
            return null;

        string expectedName = Path.GetFileName(project.artifactPath);
        string binary = files.FirstOrDefault(
            file => Path.GetFileName(file).Equals(expectedName, StringComparison.OrdinalIgnoreCase));
        if (binary == null)
            throw new InvalidOperationException(project.label + " release payload is missing its versioned executable.");

        string version = VerifyBinaryVersion(binary, project.label);
        int major;
        if (!Int32.TryParse(version.Split('.')[0], out major) || major < 1)
            throw new InvalidOperationException(project.label + " has an invalid release-major FileVersion: " + version);
        return project.key + "-v" + major;
    }

    static string NormalizeFourPartVersion(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return null;
        var match = Regex.Match(value, @"\b(?<version>\d+\.\d+\.\d+\.\d+)\b");
        return match.Success ? match.Groups["version"].Value : null;
    }

    static void VerifyManifestVersion(string manifestXml, string expectedVersion, string label)
    {
        if (!Regex.IsMatch(
            manifestXml,
            @"<\s*Identity\b[^>]*\bVersion\s*=\s*[""']" + Regex.Escape(expectedVersion) + @"[""']",
            RegexOptions.IgnoreCase))
            throw new InvalidOperationException(label + " smoke test AppxManifest.xml version does not match the binary version: " + expectedVersion);
    }

    static void SetDesktopStubManifestIdentity(string manifestPath, string identityName)
    {
        if (!File.Exists(manifestPath))
            throw new InvalidOperationException("DesktopStub smoke test did not create AppxManifest.xml.");

        string xml = File.ReadAllText(manifestPath, Encoding.UTF8);
        string updated = Regex.Replace(
            xml,
            @"(<\s*Identity\b[^>]*\bName\s*=\s*)[""'][^""']*[""']",
            "$1\"" + identityName + "\"",
            RegexOptions.IgnoreCase,
            TimeSpan.FromSeconds(5));
        if (String.Equals(xml, updated, StringComparison.Ordinal))
            throw new InvalidOperationException("DesktopStub smoke test could not update AppxManifest.xml identity.");
        File.WriteAllText(manifestPath, updated, new UTF8Encoding(true));
    }

    static void UnregisterDesktopStubSmokePackage(string identityName, string workingDirectory)
    {
        string escaped = identityName.Replace("'", "''");
        string script =
            "$pkg = Get-AppxPackage -Name '" + escaped + "' -ErrorAction SilentlyContinue; " +
            "if ($pkg) { Remove-AppxPackage -Package $pkg.PackageFullName -ErrorAction Stop }";
        var result = RunCapture(
            "powershell.exe",
            "-NoProfile -ExecutionPolicy Bypass -Command " + QuoteArg(script),
            workingDirectory,
            120000);
        if (result.ExitCode != 0)
            Warn("DesktopStub smoke package cleanup failed for " + identityName + ": " + result.Error + result.Output);
    }

    static List<TileTextSmokeRegion> LoadTileTextSmokeRegions(string repositoryRoot)
    {
        string specPath = Path.Combine(repositoryRoot, "dependencies", "DesktopStub", "tile_text_layout_spec.inc");
        if (!File.Exists(specPath))
            throw new InvalidOperationException("DesktopStub TileText layout specification is missing: " + specPath);

        var rowPattern = new Regex(
            @"^\s*TILE_TEXT_REGION\(\s*(?<size>Medium|Wide|Large)\s*,\s*(?<mask>[1-7])\s*,\s*(?:Title|Body|Badge)\s*,\s*(?<source>[0-2])\s*,\s*(?<x>[0-9.]+)\s*,\s*(?<y>[0-9.]+)\s*,\s*(?<width>[0-9.]+)\s*,\s*(?<height>[0-9.]+)\s*,\s*(?:Near|Far)\s*,\s*(?:Character|Word)\s*,\s*[1-9][0-9]*\s*\)\s*$",
            RegexOptions.CultureInvariant);
        var regions = new List<TileTextSmokeRegion>();
        int lineNumber = 0;
        foreach (string line in File.ReadLines(specPath))
        {
            ++lineNumber;
            string trimmed = line.Trim();
            if (trimmed.Length == 0 || trimmed.StartsWith("//", StringComparison.Ordinal))
                continue;

            Match match = rowPattern.Match(line);
            if (!match.Success)
                throw new InvalidOperationException("Unrecognized DesktopStub TileText layout row at " + specPath + ":" + lineNumber + ": " + line);

            regions.Add(new TileTextSmokeRegion
            {
                Size = match.Groups["size"].Value,
                Mask = Int32.Parse(match.Groups["mask"].Value, CultureInfo.InvariantCulture),
                Source = Int32.Parse(match.Groups["source"].Value, CultureInfo.InvariantCulture),
                X = Double.Parse(match.Groups["x"].Value, CultureInfo.InvariantCulture),
                Y = Double.Parse(match.Groups["y"].Value, CultureInfo.InvariantCulture),
                Width = Double.Parse(match.Groups["width"].Value, CultureInfo.InvariantCulture),
                Height = Double.Parse(match.Groups["height"].Value, CultureInfo.InvariantCulture)
            });
        }

        if (regions.Count == 0)
            throw new InvalidOperationException("DesktopStub TileText layout specification contains no regions: " + specPath);
        return regions;
    }

    static Dictionary<string, TilePixelSnapshot> CaptureDesktopStubTileAssets(string tempRoot, string label)
    {
        var dimensions = new Dictionary<string, int[]>(StringComparer.Ordinal)
        {
            { "Medium", new[] { 150, 150 } },
            { "Wide", new[] { 310, 150 } },
            { "Large", new[] { 310, 310 } }
        };
        var snapshots = new Dictionary<string, TilePixelSnapshot>(StringComparer.Ordinal);
        foreach (var entry in dimensions)
        {
            string assetPath = Path.Combine(tempRoot, "Assets", entry.Key + "Tile.png");
            if (!File.Exists(assetPath))
                throw new InvalidOperationException("DesktopStub " + label + " did not create Assets\\" + entry.Key + "Tile.png.");

            using (var bitmap = new Bitmap(assetPath))
            {
                if (bitmap.Width != entry.Value[0] || bitmap.Height != entry.Value[1])
                    throw new InvalidOperationException("DesktopStub " + label + " generated " + entry.Key + "Tile.png at " + bitmap.Width + "x" + bitmap.Height + "; expected " + entry.Value[0] + "x" + entry.Value[1] + ".");

                int[] pixels = new int[bitmap.Width * bitmap.Height];
                for (int y = 0; y < bitmap.Height; ++y)
                {
                    for (int x = 0; x < bitmap.Width; ++x)
                        pixels[y * bitmap.Width + x] = bitmap.GetPixel(x, y).ToArgb();
                }
                snapshots.Add(entry.Key, new TilePixelSnapshot
                {
                    Width = bitmap.Width,
                    Height = bitmap.Height,
                    Pixels = pixels
                });
            }
        }
        return snapshots;
    }

    static Dictionary<string, TilePixelSnapshot> RenderDesktopStubTileTextVariant(
        string exe,
        string ini,
        string wallpaper,
        string tempRoot,
        string label,
        string primary,
        string secondary,
        string badge)
    {
        SmokeProcess(exe, new[]
            {
                "--ini", ini,
                // Registration is the explicit "Live Tile disabled" mode. In
                // this mode TileText must be baked into static PNG assets.
                "--no-live-tile",
                "--asset", "MediumTile=1",
                "--asset", "WideTile=1",
                "--asset", "LargeTile=1",
                "--set", "TileText.Enabled=1",
                "--set", "TileText.ApplyToMediumTile=1",
                "--set", "TileText.ApplyToWideTile=1",
                "--set", "TileText.ApplyToLargeTile=1",
                "--set", "TileText.Text=" + primary,
                "--set", "TileText.SecondaryText=" + secondary,
                "--set", "TileText.BadgeText=" + badge,
                "--wallpaper", wallpaper,
                allowPackageIntegration ? "--once" : "--render-only", "--no-tray", "--no-monitor"
            }, new[] { 0 }, 30, "DesktopStub static TileText " + label);
        return CaptureDesktopStubTileAssets(tempRoot, label);
    }

    static int CountPixelDifferences(TilePixelSnapshot first, TilePixelSnapshot second)
    {
        if (first.Width != second.Width || first.Height != second.Height || first.Pixels.Length != second.Pixels.Length)
            throw new InvalidOperationException("DesktopStub TileText comparison received mismatched image dimensions.");

        int changed = 0;
        for (int i = 0; i < first.Pixels.Length; ++i)
        {
            if (first.Pixels[i] != second.Pixels[i])
                ++changed;
        }
        return changed;
    }

    static bool PixelFallsInTileTextRegions(int x, int y, IEnumerable<TileTextSmokeRegion> regions)
    {
        // GDI+ can antialias a glyph at a layout edge. Permit a small halo,
        // while still catching a field routed into the wrong part of a tile.
        const double padding = 3.0;
        foreach (TileTextSmokeRegion region in regions)
        {
            if (x >= region.X - padding && y >= region.Y - padding &&
                x < region.X + region.Width + padding && y < region.Y + region.Height + padding)
                return true;
        }
        return false;
    }

    static void VerifyTileTextFieldPixelDiff(
        string tileSize,
        int source,
        TilePixelSnapshot baseline,
        TilePixelSnapshot changed,
        List<TileTextSmokeRegion> allRegions)
    {
        List<TileTextSmokeRegion> expectedRegions = allRegions
            .Where(region => region.Size == tileSize && region.Mask == 7 && region.Source == source)
            .ToList();
        int changedPixels = 0;
        int pixelsOutsideExpectedRegions = 0;
        for (int y = 0; y < baseline.Height; ++y)
        {
            for (int x = 0; x < baseline.Width; ++x)
            {
                int index = y * baseline.Width + x;
                if (baseline.Pixels[index] == changed.Pixels[index])
                    continue;
                ++changedPixels;
                if (!PixelFallsInTileTextRegions(x, y, expectedRegions))
                    ++pixelsOutsideExpectedRegions;
            }
        }

        string fieldName = source == 0 ? "primary" : source == 1 ? "secondary" : "badge";
        if (expectedRegions.Count == 0)
        {
            if (changedPixels != 0)
                throw new InvalidOperationException("DesktopStub " + tileSize + " TileText pixel output changed after the unsupported " + fieldName + " input changed (" + changedPixels + " pixels). Expected identical output; inspect the saved assets to identify the cause.");
            Console.WriteLine("ok - DesktopStub " + tileSize + " TileText omits unsupported " + fieldName + " field");
            return;
        }

        if (changedPixels == 0)
            throw new InvalidOperationException("DesktopStub " + tileSize + " TileText did not visibly render the " + fieldName + " field.");
        if (pixelsOutsideExpectedRegions != 0)
            throw new InvalidOperationException("DesktopStub " + tileSize + " TileText rendered " + pixelsOutsideExpectedRegions + " of " + changedPixels + " changed " + fieldName + " pixels outside its shared layout region.");
        Console.WriteLine("ok - DesktopStub " + tileSize + " TileText " + fieldName + " field rendered inside its shared layout region (" + changedPixels + " changed pixels)");
    }

    static void VerifyDesktopStubTileTextRendering(
        string repositoryRoot,
        string exe,
        string ini,
        string wallpaper,
        string tempRoot,
        Dictionary<string, TilePixelSnapshot> tilesWithoutText)
    {
        List<TileTextSmokeRegion> regions = LoadTileTextSmokeRegions(repositoryRoot);
        Dictionary<string, TilePixelSnapshot> baseline = RenderDesktopStubTileTextVariant(
            exe, ini, wallpaper, tempRoot, "all-fields baseline", "AAAA", "IIII", "7");

        foreach (string tileSize in new[] { "Medium", "Wide", "Large" })
        {
            int changedPixels = CountPixelDifferences(tilesWithoutText[tileSize], baseline[tileSize]);
            if (changedPixels == 0)
                throw new InvalidOperationException("DesktopStub static TileText did not change " + tileSize + "Tile.png while Live Tile mode was disabled.");
            Console.WriteLine("ok - DesktopStub static TileText changes " + tileSize + "Tile.png with Live Tile disabled (" + changedPixels + " pixels)");
        }

        var variants = new[]
        {
            new { Source = 0, Label = "primary-field variant", Primary = "WWWW", Secondary = "IIII", Badge = "7" },
            new { Source = 1, Label = "secondary-field variant", Primary = "AAAA", Secondary = "MMMM", Badge = "7" },
            new { Source = 2, Label = "badge-field variant", Primary = "AAAA", Secondary = "IIII", Badge = "8" }
        };
        foreach (var variant in variants)
        {
            Dictionary<string, TilePixelSnapshot> changed = RenderDesktopStubTileTextVariant(
                exe, ini, wallpaper, tempRoot, variant.Label, variant.Primary, variant.Secondary, variant.Badge);
            foreach (string tileSize in new[] { "Medium", "Wide", "Large" })
                VerifyTileTextFieldPixelDiff(tileSize, variant.Source, baseline[tileSize], changed[tileSize], regions);
        }

        // Both strings deliberately share a first word that consumes most of
        // each one-line region. With wrapping enabled, GDI+ moves the differing
        // second word to a clipped second line and the images become identical.
        // NoWrap keeps the second word on the visible line, so this comparison
        // pins the regression where multi-word captions disappeared/wrapped.
        Dictionary<string, TilePixelSnapshot> noWrapPrimaryA = RenderDesktopStubTileTextVariant(
            exe, ini, wallpaper, tempRoot, "one-line primary A", "DESKTOPSTUB TEST", "SECONDARY", "8");
        Dictionary<string, TilePixelSnapshot> noWrapPrimaryB = RenderDesktopStubTileTextVariant(
            exe, ini, wallpaper, tempRoot, "one-line primary B", "DESKTOPSTUB WIDE", "SECONDARY", "8");
        foreach (string tileSize in new[] { "Medium", "Large" })
            VerifyTileTextFieldPixelDiff(tileSize, 0, noWrapPrimaryA[tileSize], noWrapPrimaryB[tileSize], regions);
    }

    static void VerifyDesktopStubComposedContent(string exe, string ini, string wallpaper, string tempRoot)
    {
        Func<string, string, Dictionary<string, TilePixelSnapshot>> render = (mode, text) => {
            SmokeProcess(exe, new[] {
                "--ini", ini, "--no-live-tile", "--no-tray", "--no-monitor",
                "--set", "Content.Count=1", "--set", "Content.CycleEnabled=0",
                "--set", "Content.1.Background=Image", "--set", "Content.1.ImagePath=" + wallpaper,
                "--set", "Content.1.TextSources=CustomText", "--set", "Content.1.Text=" + text,
                "--set", "Content.1.SecondaryText=", "--set", "Content.1.BadgeText=",
                "--set", "Content.TextMode=" + mode, "--set", "Content.Enabled=true", allowPackageIntegration ? "--once" : "--render-only"
            }, new[] { 0 }, 30, "DesktopStub composed " + mode + " text=" + text);
            return CaptureDesktopStubTileAssets(tempRoot, "composition " + mode);
        };
        var imageOnly = render("Off", "ALPHA");
        var alpha = render("Overlay", "ALPHA");
        var beta = render("Overlay", "BETA");
        var hidden = render("Off", "BETA");
        foreach (string size in new[] { "Medium", "Wide", "Large" }) {
            if (CountPixelDifferences(imageOnly[size], alpha[size]) == 0)
                throw new InvalidOperationException("Composed overlay is invisible on " + size + ".");
            if (CountPixelDifferences(alpha[size], beta[size]) == 0)
                throw new InvalidOperationException("Composed text cache did not invalidate on " + size + ".");
            if (CountPixelDifferences(imageOnly[size], hidden[size]) != 0)
                throw new InvalidOperationException("Composed TextMode=Off retained stale text on " + size + ".");
            Console.WriteLine("ok - DesktopStub composed " + size + " text, changed-text cache and hidden-text restore");
        }
        SmokeProcess(exe, new[] { "--ini", ini, "--set", "Content.Enabled=0", "--exit" },
            new[] { 0 }, 30, "DesktopStub leave composition");
    }

    static void VerifyDesktopStubOfflineGuards(string exe, string tempRoot)
    {
        int index = 0;
        foreach (string startup in new[] { "--startup", "--no-startup", "--packaged-startup", "--no-packaged-startup" }) {
            string ini = Path.Combine(tempRoot, "OfflineRejected" + index++ + ".ini");
            SmokeProcess(exe, new[] { "--ini", ini, "--configure-only", startup }, new[] { 2 }, 10,
                "DesktopStub offline startup operation rejected");
            AssertFileDoesNotExist(ini, "Offline startup rejection must precede INI creation");
        }
        string external = Path.Combine(tempRoot, "OfflineExternal.ini");
        SmokeProcess(exe, new[] { "--ini", external, "--render-only", "--set", "Content.Enabled=1",
            "--set", "Content.1.Background=None", "--set", "Content.1.TextSources=SMTC" }, new[] { 2 }, 10,
            "DesktopStub offline external provider rejected");
        AssertFileDoesNotExist(external, "Offline provider rejection must precede INI creation");
        string missingInput = Path.Combine(tempRoot, "OfflineNoInput.ini");
        SmokeProcess(exe, new[] { "--ini", missingInput, "--render-only" }, new[] { 2 }, 10,
            "DesktopStub offline requires an explicit image or None content");
        AssertFileDoesNotExist(missingInput, "Offline input rejection must precede INI creation");
        string existing = Path.Combine(tempRoot, "OfflineStartupPreferences.ini");
        File.WriteAllText(existing, "[Settings]\r\nRunAtStartup=1\r\nRunAtStartupPackaged=1\r\n", new UTF8Encoding(true));
        SmokeProcess(exe, new[] { "--ini", existing, "--configure-only", "--set", "TileText.Text=Offline" },
            new[] { 0 }, 10, "DesktopStub offline preserves pre-existing startup preferences without applying them");
        AssertFileContains(existing, "RunAtStartup=1", "Offline editing must preserve startup preference");
        AssertFileContains(existing, "RunAtStartupPackaged=1", "Offline editing must preserve packaged startup preference");
        string blank = Path.Combine(tempRoot, "OfflineNone.ini");
        SmokeProcess(exe, new[] { "--ini", blank, "--render-only", "--no-live-tile", "--asset", "MediumTile=1",
            "--asset", "WideTile=1", "--asset", "LargeTile=1", "--set", "Content.Enabled=1",
            "--set", "Content.1.Background=None", "--set", "Content.1.TextSources=CustomText",
            "--set", "Content.1.Text=OFFLINE TILE", "--set", "Content.TextMode=Overlay" },
            new[] { 0 }, 30, "DesktopStub offline None background renders without desktop capture");
        CaptureDesktopStubTileAssets(tempRoot, "offline None background");
    }

    static void AssertFileDoesNotExist(string file, string reason)
    {
        if (File.Exists(file))
            throw new InvalidOperationException(reason + ": " + file);
    }

    static void AssertFileDoesNotContain(string file, string needle, string reason)
    {
        if (!File.Exists(file))
            return;
        string text = File.ReadAllText(file, Encoding.UTF8);
        if (text.IndexOf(needle, StringComparison.Ordinal) >= 0)
            throw new InvalidOperationException(reason + ": " + file);
    }

    static void AssertFileContains(string file, string needle, string reason)
    {
        if (!File.Exists(file))
            throw new InvalidOperationException(reason + ": missing " + file);
        string text = File.ReadAllText(file, Encoding.UTF8);
        if (text.IndexOf(needle, StringComparison.Ordinal) < 0)
            throw new InvalidOperationException(reason + ": " + file);
    }

    static void VerifyDesktopStubConcurrentIniWrites(string exe, string ini)
    {
        const int processCount = 12;
        var processes = new List<Process>();
        try
        {
            for (int i = 1; i <= processCount; ++i)
            {
                var psi = new ProcessStartInfo(
                    exe,
                    JoinArgs(new[] { "--ini", ini, "--set", "Concurrent.Key" + i + "=Value" + i,
                        allowPackageIntegration ? "--exit" : "--configure-only" }))
                {
                    WorkingDirectory = Path.GetDirectoryName(exe),
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                var process = new Process { StartInfo = psi };
                if (!process.Start())
                    throw new InvalidOperationException("Failed to start DesktopStub concurrent INI writer " + i + ".");
                processes.Add(process);
            }

            foreach (Process process in processes)
            {
                if (!process.WaitForExit(60000))
                {
                    try { process.Kill(); } catch { }
                    throw new TimeoutException("DesktopStub concurrent INI writer timed out.");
                }
                if (process.ExitCode != 0 && process.ExitCode != 2)
                    throw new InvalidOperationException("DesktopStub concurrent INI writer exited " + process.ExitCode + ".");
            }

            for (int i = 1; i <= processCount; ++i)
                AssertFileContains(ini, "\"Key" + i + "\" = \"Value" + i + "\"", "DesktopStub concurrent INI writes must not lose settings");
            Console.WriteLine("ok - DesktopStub concurrent INI writes");
        }
        finally
        {
            foreach (Process process in processes)
            {
                try { if (!process.HasExited) process.Kill(); } catch { }
                try { process.WaitForExit(5000); } catch { }
                process.Dispose();
            }
        }
    }

    static void VerifyDesktopStubSecondLaunchActions(string exe, string tempRoot)
    {
        string ini = Path.Combine(tempRoot, "SecondLaunch.ini");
        string log = Path.Combine(tempRoot, "SecondLaunch.log");
        string config =
            "[Settings]\r\n" +
            "\"TrayIcon\" = \"0\"\r\n" +
            "\"ShowConsole\" = \"0\"\r\n" +
            "\"Logging\" = \"1\"\r\n" +
            "\"LogPath\" = \"" + log.Replace("\\", "\\\\") + "\"\r\n" +
            "\"NotificationsEnabled\" = \"0\"\r\n" +
            "\"GenerateOnStartup\" = \"0\"\r\n" +
            "\"AlreadyRunningAction\" = \"Ignore\"\r\n";
        File.WriteAllText(ini, config, new UTF8Encoding(true));

        var psi = new ProcessStartInfo(
            exe,
            JoinArgs(new[] { "--ini", ini, "--no-tray", "--no-monitor" }))
        {
            WorkingDirectory = Path.GetDirectoryName(exe),
            UseShellExecute = false,
            CreateNoWindow = true
        };

        using (var resident = new Process { StartInfo = psi })
        {
            if (!resident.Start())
                throw new InvalidOperationException("Failed to start DesktopStub second-launch resident.");

            try
            {
                WaitForFileText(log, "Program starting...", 30000, "DesktopStub second-launch resident startup");
                SmokeProcess(exe, new[] { "--ini", ini }, new[] { 0 }, 10, "DesktopStub second-launch ignore");
                if (resident.HasExited)
                    throw new InvalidOperationException("DesktopStub Ignore second-launch action stopped the resident.");

                SmokeProcess(
                    exe,
                    new[] { "--ini", ini, "--set", "Settings.AlreadyRunningAction=ShowConsole" },
                    new[] { 0 },
                    10,
                    "DesktopStub second-launch action reload");
                SmokeProcess(exe, new[] { "--ini", ini }, new[] { 0 }, 10, "DesktopStub second-launch show console");
                WaitForFileText(log, "Console on.", 10000, "DesktopStub second-launch console delivery");
                AssertFileContains(ini, "\"ShowConsole\" = \"0\"", "DesktopStub second-launch console action must remain session-only");

                SmokeProcess(exe, new[] { "--ini", ini, "--exit" }, new[] { 0 }, 10, "DesktopStub explicit exit override");
                if (!resident.WaitForExit(10000))
                    throw new TimeoutException("DesktopStub resident did not stop after explicit --exit.");
                Console.WriteLine("ok - DesktopStub configurable second-launch actions");
            }
            finally
            {
                if (!resident.HasExited)
                {
                    try { resident.Kill(); } catch { }
                    try { resident.WaitForExit(5000); } catch { }
                }
            }
        }
    }

    static void VerifyDiscordRpcNoTrayControl(string exe, string tempRoot)
    {
        string ini = Path.Combine(tempRoot, "NoTrayResident.ini");
        string log = Path.Combine(tempRoot, "NoTrayResident.log");
        string config =
            "[general]\r\n" +
            "\"client_id\" = \"123456789012345678\"\r\n" +
            "\"update_interval\" = \"60\"\r\n" +
            "\"transport_mode\" = \"ipc\"\r\n" +
            "[app]\r\n" +
            "\"show_tray\" = \"false\"\r\n" +
            "\"single_instance\" = \"true\"\r\n" +
            "\"show_console\" = \"false\"\r\n" +
            "\"notifications_enabled\" = \"false\"\r\n" +
            "\"logging_enabled\" = \"true\"\r\n" +
            "\"file_logging_enabled\" = \"true\"\r\n" +
            "\"log_path\" = \"" + log.Replace("\\", "\\\\") + "\"\r\n" +
            "[ipc]\r\n" +
            "\"connect_timeout_ms\" = \"50\"\r\n" +
            "\"response_timeout_ms\" = \"250\"\r\n";
        File.WriteAllText(ini, config, new UTF8Encoding(true));

        var psi = new ProcessStartInfo(exe, JoinArgs(new[] { "--ini", ini, "--no-tray" }))
        {
            WorkingDirectory = Path.GetDirectoryName(exe),
            UseShellExecute = false,
            CreateNoWindow = true
        };

        using (var resident = new Process { StartInfo = psi })
        {
            if (!resident.Start())
                throw new InvalidOperationException("Failed to start DiscordRPC no-tray resident.");

            try
            {
                WaitForFileText(log, "Resident control window ready.", 10000, "DiscordRPC no-tray resident startup");
                SmokeProcess(
                    exe,
                    new[] { "--ini", ini, "--set", "app.verbose_logging=true" },
                    new[] { 0 },
                    10,
                    "DiscordRPC no-tray reload");
                WaitForFileText(log, "Configuration reloaded.", 30000, "DiscordRPC no-tray reload delivery");

                SmokeProcess(exe, new[] { "--ini", ini, "--exit" }, new[] { 0 }, 10, "DiscordRPC no-tray exit");
                if (!resident.WaitForExit(10000))
                    throw new TimeoutException("DiscordRPC no-tray resident did not stop after --exit.");

                SmokeProcess(exe, new[] { "--ini", ini, "--exit" }, new[] { 2 }, 10, "DiscordRPC missing resident exit");
                Console.WriteLine("ok - DiscordRPC no-tray resident control");
            }
            finally
            {
                if (!resident.HasExited)
                {
                    try { resident.Kill(); } catch { }
                    try { resident.WaitForExit(5000); } catch { }
                }
            }
        }
    }

    static void WaitForFileText(string file, string needle, int timeoutMs, string name)
    {
        Stopwatch timer = Stopwatch.StartNew();
        Exception lastReadError = null;
        while (timer.ElapsedMilliseconds < timeoutMs)
        {
            if (File.Exists(file))
            {
                try
                {
                    string text;
                    using (var stream = new FileStream(
                        file,
                        FileMode.Open,
                        FileAccess.Read,
                        FileShare.ReadWrite | FileShare.Delete))
                    using (var reader = new StreamReader(stream, Encoding.UTF8, true))
                        text = reader.ReadToEnd();

                    if (text.IndexOf(needle, StringComparison.Ordinal) >= 0)
                        return;
                    lastReadError = null;
                }
                catch (IOException ex)
                {
                    // App loggers append concurrently and may briefly deny sharing.
                    lastReadError = ex;
                }
                catch (UnauthorizedAccessException ex)
                {
                    // Antivirus and indexing filters can also make an existing log
                    // transiently unavailable; the timeout still bounds the retry.
                    lastReadError = ex;
                }
            }
            System.Threading.Thread.Sleep(100);
        }

        string detail = lastReadError == null ? "" : " Last read error: " + lastReadError.Message;
        throw new TimeoutException(name + " timed out waiting for '" + needle + "'." + detail);
    }

    static int PrepareReleaseVersions(string[] args)
    {
        string root = RepositoryRoot(args);
        string repository = Option(args, "--repository", Environment.GetEnvironmentVariable("REPO") ?? "");
        string releaseProjectList = Option(
            args,
            "--release-project-list",
            Environment.GetEnvironmentVariable("RELEASE_PROJECT_LIST") ?? "");
        string environmentFile = Option(
            args,
            "--environment-file",
            Environment.GetEnvironmentVariable("GITHUB_ENV") ?? "");
        if (String.IsNullOrWhiteSpace(releaseProjectList))
        {
            Console.WriteLine("No project releases selected for version preparation.");
            return 0;
        }
        if (String.IsNullOrWhiteSpace(repository))
            throw new InvalidOperationException("Repository was not provided and REPO is empty.");
        if (String.IsNullOrWhiteSpace(environmentFile))
            throw new InvalidOperationException("Environment file was not provided and GITHUB_ENV is empty.");

        var projects = LoadProjectMap(root);
        var selected = ResolveReleaseProjects(projects, releaseProjectList);
        var releases = ListGitHubReleases(repository, root);
        var environmentLines = new List<string>();
        foreach (Project project in selected)
        {
            if (String.IsNullOrWhiteSpace(project.releaseTagEnvironment))
                continue;

            GitHubReleaseInfo replaceable = FindReplaceableLatestRelease(
                repository,
                project.key,
                releases.Where(r => IsReleaseFamilyTag(project.key, r.tagName)),
                root);
            if (replaceable == null)
                continue;

            environmentLines.Add(project.releaseTagEnvironment + "=" + replaceable.tagName);
            Console.WriteLine(
                "Reusing zero-download release version for " +
                project.label + ": " + replaceable.tagName);
        }

        if (environmentLines.Count == 0)
        {
            Console.WriteLine("No zero-download release versions need to be reused.");
            return 0;
        }

        File.AppendAllText(
            environmentFile,
            String.Join(Environment.NewLine, environmentLines) + Environment.NewLine,
            new UTF8Encoding(false));
        return 0;
    }

    static ProcessResult SmokeProcess(string file, string[] args, int[] allowedExitCodes, int timeoutSeconds, string name)
    {
        if (!File.Exists(file))
        {
            Console.WriteLine("skip - " + name + " not built: " + file);
            return new ProcessResult { ExitCode = 0 };
        }

        if (!allowPackageIntegration && Path.GetFileName(file).Equals("DesktopStub.exe", StringComparison.OrdinalIgnoreCase) &&
            !args.Contains("--help") && !args.Contains("--version") &&
            !args.Contains("--render-only") && !args.Contains("--configure-only"))
        {
            // Every default DesktopStub invocation takes a real offline entry
            // point. No harmless-looking settings command may reach residency.
            bool manifest = args.Any(arg => new[] { "--regenerate-manifest", "--manifest-win81", "--win8-broker",
                "--no-win8-broker", "--win8-oop-helper" }.Contains(arg));
            args = args.Where(arg => arg != "--exit" && arg != "--once").Concat(
                manifest ? new[] { "--render-only", "--regenerate-manifest" } : new[] { "--configure-only" }).ToArray();
        }
        var result = RunCapture(file, JoinArgs(args), Path.GetDirectoryName(file), timeoutSeconds * 1000);
        if (!allowedExitCodes.Contains(result.ExitCode))
            throw new InvalidOperationException("Smoke test failed: " + name + " " + String.Join(" ", args) + " exited " + result.ExitCode + Environment.NewLine + "STDOUT:" + Environment.NewLine + result.Output + Environment.NewLine + "STDERR:" + Environment.NewLine + result.Error);
        Console.WriteLine("ok - " + name + " " + String.Join(" ", args));
        return result;
    }

    static void WriteTestBmp(string path)
    {
        const int w = 16, h = 16;
        int rowStride = ((w * 3 + 3) / 4) * 4;
        int pixelBytes = rowStride * h;
        int fileSize = 54 + pixelBytes;
        using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write))
        using (var bw = new BinaryWriter(fs))
        {
            bw.Write((byte)'B');
            bw.Write((byte)'M');
            bw.Write(fileSize);
            bw.Write(0);
            bw.Write(54);
            bw.Write(40);
            bw.Write(w);
            bw.Write(h);
            bw.Write((short)1);
            bw.Write((short)24);
            bw.Write(0);
            bw.Write(pixelBytes);
            bw.Write(2835);
            bw.Write(2835);
            bw.Write(0);
            bw.Write(0);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    bool white = x >= 2 && x < 14 && y >= 2 && y < 14;
                    bw.Write((byte)(white ? 255 : 80));
                    bw.Write((byte)(white ? 255 : 40));
                    bw.Write((byte)(white ? 255 : 20));
                }
                for (int p = w * 3; p < rowStride; ++p)
                    bw.Write((byte)0);
            }
        }
    }

    static int PublishProjectReleases(string[] args)
    {
        string root = RepositoryRoot(args);
        string artifactsRoot = Option(args, "--artifacts-root", Option(args, "-ArtifactsRoot", "artifacts"));
        string fullSha = Option(args, "--full-sha", Environment.GetEnvironmentVariable("FULL_SHA") ?? "");
        string repository = Option(args, "--repository", Environment.GetEnvironmentVariable("REPO") ?? "");
        string projectList = Option(args, "--project-list", Environment.GetEnvironmentVariable("PROJECT_LIST") ?? "");
        string releaseProjectList = Option(args, "--release-project-list", Environment.GetEnvironmentVariable("RELEASE_PROJECT_LIST") ?? projectList);
        if (String.IsNullOrWhiteSpace(fullSha)) throw new InvalidOperationException("FullSha was not provided and FULL_SHA is empty.");
        if (String.IsNullOrWhiteSpace(repository)) throw new InvalidOperationException("Repository was not provided and REPO is empty.");
        if (String.IsNullOrWhiteSpace(releaseProjectList))
        {
            Console.WriteLine("No project releases selected for this run.");
            return 0;
        }

        if (!Path.IsPathRooted(artifactsRoot))
            artifactsRoot = Path.Combine(root, artifactsRoot);
        if (!Directory.Exists(artifactsRoot))
            throw new InvalidOperationException("Artifacts directory not found: " + artifactsRoot);

        RunRequired("git", "fetch --force --tags", root);
        var projects = LoadProjectMap(root);
        var releaseKeys = new HashSet<string>(
            ResolveReleaseProjects(projects, releaseProjectList).Select(p => p.key),
            StringComparer.OrdinalIgnoreCase);
        var entries = new List<Tuple<Project, string, List<string>, string>>();
        var missingArtifacts = new List<Project>();
        foreach (var p in projects)
        {
            if (!releaseKeys.Contains(p.key))
                continue;
            var entry = ResolveDownloadedProjectArtifact(p, artifactsRoot);
            if (entry != null)
            {
                Console.WriteLine("Found release artifact payload for " + p.label + ": " + entry.Item3);
                entries.Add(Tuple.Create(p, entry.Item1, entry.Item2, entry.Item3));
            }
            else
            {
                missingArtifacts.Add(p);
            }
        }

        if (missingArtifacts.Count > 0)
        {
            Console.WriteLine("Downloaded artifact tree:");
            foreach (string line in ArtifactTreeSummary(artifactsRoot, 160))
                Console.WriteLine("  " + line);
            throw new InvalidOperationException("Missing downloaded release artifact payload for selected project(s): " +
                String.Join(", ", missingArtifacts.Select(p => p.label + " [" + p.artifactName + "]")));
        }

        if (entries.Count == 0)
        {
            Console.WriteLine("Downloaded artifact tree:");
            foreach (string line in ArtifactTreeSummary(artifactsRoot, 120))
                Console.WriteLine("  " + line);
            throw new InvalidOperationException("No downloaded project artifacts were found for release upload.");
        }

        var summary = new List<string>
        {
            "## Automatic project releases",
            "",
            "Each built project is released in its own `Project-vN` tag family. Workflow artifact payload files are uploaded to the matching project release as individual assets.",
            "",
            "| Project | Release | Uploaded assets |",
            "| --- | --- | --- |"
        };

        foreach (var entry in entries)
        {
            Project p = entry.Item1;
            string artifactRoot = entry.Item2;
            List<string> files = entry.Item3;
            string releaseBase = p.key;
            string matching = RunCaptureRequired("git", "tag --list " + QuoteArg(releaseBase + "-v*") + " --sort=-v:refname", root);
            var matchingTags = matching.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).ToList();
            var releases = ListGitHubReleases(repository, root)
                .Where(r => IsReleaseFamilyTag(releaseBase, r.tagName))
                .ToList();
            GitHubReleaseInfo replaceableRelease = FindReplaceableLatestRelease(
                repository,
                releaseBase,
                releases,
                root);
            string embeddedBinaryTag = EmbeddedBinaryReleaseTag(p, files);
            string tag = null;
            bool tagPointsAtFullSha = false;
            foreach (string candidate in matchingTags)
            {
                string sha = RunCaptureRequired("git", "rev-list -n 1 " + QuoteArg(candidate), root).Trim();
                if (sha == fullSha)
                {
                    tag = candidate;
                    tagPointsAtFullSha = true;
                    break;
                }
            }

            if (tag == null && embeddedBinaryTag != null)
                tag = embeddedBinaryTag;

            if (tag == null)
            {
                GitHubReleaseInfo reusableDraft = releases
                    .Where(r => r.isDraft)
                    .OrderByDescending(r => ReleaseVersion(releaseBase, r.tagName))
                    .FirstOrDefault();
                int maxPublishedVersion = releases
                    .Where(r => !r.isDraft)
                    .Select(r => ReleaseVersion(releaseBase, r.tagName))
                    .DefaultIfEmpty(0)
                    .Max();
                if (reusableDraft != null &&
                    ReleaseVersion(releaseBase, reusableDraft.tagName) > maxPublishedVersion)
                {
                    tag = reusableDraft.tagName;
                }
                else if (replaceableRelease != null)
                {
                    tag = replaceableRelease.tagName;
                }
                else
                {
                    tag = NextReleaseTag(
                        releaseBase,
                        matchingTags.Concat(releases.Select(r => r.tagName)));
                }
            }
            if (embeddedBinaryTag != null &&
                !tag.Equals(embeddedBinaryTag, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    p.label + " release tag does not match the executable FileVersion: " +
                    tag + " != " + embeddedBinaryTag);
            string previousTag = matchingTags.FirstOrDefault(t => t != tag);

            string workRoot = Path.Combine(Path.GetTempPath(), "release-assets-" + releaseBase + "-" + Guid.NewGuid().ToString("N"));
            try
            {
                string uploadRoot = Path.Combine(workRoot, "upload");
                Directory.CreateDirectory(uploadRoot);
                var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                var releaseAssets = new List<string>();
                foreach (string file in files.OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
                    AddReleaseAsset(file, RelativeUnder(artifactRoot, file), uploadRoot, used, releaseAssets);

                if (releaseAssets.Count == 0)
                    throw new InvalidOperationException("No release assets were prepared for " + p.label + ".");

                string gitLogFormat = QuoteArg("--format=%h %s");
                string logArgs = previousTag != null
                    ? "log " + gitLogFormat + " " + QuoteArg(previousTag + ".." + fullSha)
                    : "log " + gitLogFormat + " -n 10 " + QuoteArg(fullSha);
                string commitText = RunCaptureRequired("git", logArgs, root);
                var commitSubjects = commitText.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).ToList();
                if (commitSubjects.Count == 0)
                    commitSubjects.Add(fullSha);

                var checksumLines = new List<string> { "## SHA256 checksums", "", "| File | SHA256 |", "| --- | --- |" };
                foreach (string asset in releaseAssets.OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
                    checksumLines.Add("| `" + Path.GetFileName(asset) + "` | `" + Sha256File(asset).ToLowerInvariant() + "` |");

                var changeLines = new List<string> { "## Changes", "" };
                changeLines.AddRange(commitSubjects.Select(s => "- " + s));

                string notes = String.Join(Environment.NewLine, new[]
                {
                    "Automated Windows build for " + p.label + " on main.",
                    "",
                    "Commit: `" + fullSha + "`",
                    "Built projects: " + projectList,
                    "Release family: `" + releaseBase + "-vN`",
                    "",
                    "Release assets are direct files from the workflow artifact payload for this project.",
                    "",
                    String.Join(Environment.NewLine, changeLines),
                    "",
                    String.Join(Environment.NewLine, checksumLines)
                });

                string notesPath = Path.Combine(workRoot, "release-notes.md");
                File.WriteAllText(notesPath, notes, Encoding.UTF8);
                GitHubReleaseInfo existingRelease = releases.FirstOrDefault(
                    r => r.tagName.Equals(tag, StringComparison.OrdinalIgnoreCase));
                if (existingRelease != null && !existingRelease.isDraft)
                {
                    GitHubReleaseInfo currentReplaceable = FindReplaceableLatestRelease(
                        repository,
                        releaseBase,
                        releases,
                        root);
                    if (currentReplaceable != null &&
                        currentReplaceable.tagName.Equals(tag, StringComparison.OrdinalIgnoreCase))
                    {
                        RunRequiredWithRetry(
                            "gh",
                            "release delete " + QuoteArg(tag) +
                            " --repo " + QuoteArg(repository) +
                            " --yes --cleanup-tag",
                            root,
                            120000,
                            3);
                        Console.WriteLine(
                            "Deleted zero-download release " + tag +
                            " so the version can be republished for " + fullSha + ".");
                        existingRelease = null;
                    }
                    else if (tagPointsAtFullSha)
                    {
                        Console.WriteLine("Release " + tag + " is already published; leaving it unchanged.");
                        summary.Add("| " + p.label + " | `" + tag + "` | already published |");
                        continue;
                    }
                    else
                    {
                        throw new InvalidOperationException(
                            "Release " + tag +
                            " can no longer be replaced because it is not the latest zero-download release. " +
                            "Rerun the workflow so the project receives a new version.");
                    }
                }

                bool createdDraft = false;
                try
                {
                    if (existingRelease != null)
                    {
                        RunRequiredWithRetry("gh", "release edit " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --draft --target " + QuoteArg(fullSha) + " --title " + QuoteArg(tag) + " --notes-file " + QuoteArg(notesPath), root, 120000, 3);
                    }
                    else
                    {
                        try
                        {
                            RunRequired("gh", "release create " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --draft --target " + QuoteArg(fullSha) + " --title " + QuoteArg(tag) + " --notes-file " + QuoteArg(notesPath), root);
                            createdDraft = true;
                        }
                        catch
                        {
                            GitHubReleaseInfo createdRelease = TryViewGitHubRelease(repository, tag, root);
                            if (createdRelease == null || !createdRelease.isDraft)
                            {
                                throw;
                            }

                            createdDraft = true;
                            Console.Error.WriteLine("WARNING: release create reported failure, but draft release " + tag + " exists; continuing with upload.");
                        }
                    }

                    RunRequiredWithRetry("gh", "release upload " + QuoteArg(tag) + " " + JoinArgs(releaseAssets.ToArray()) + " --repo " + QuoteArg(repository) + " --clobber", root, 120000, 4);
                    RunRequiredWithRetry("gh", "release edit " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --draft=false", root, 120000, 3);
                }
                catch
                {
                    if (createdDraft)
                    {
                        ProcessResult cleanup = RunCapture(
                            "gh",
                            "release delete " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --yes --cleanup-tag",
                            root,
                            120000);
                        if (cleanup.ExitCode != 0)
                            Console.Error.WriteLine("WARNING: Could not remove failed draft release " + tag + ": " + cleanup.Error);
                    }
                    throw;
                }

                summary.Add("| " + p.label + " | `" + tag + "` | " + String.Join(", ", releaseAssets.Select(Path.GetFileName)) + " |");
            }
            finally
            {
                if (Directory.Exists(workRoot))
                    Directory.Delete(workRoot, true);
            }
        }

        AppendSummary(summary);
        return 0;
    }

    static List<GitHubReleaseInfo> ListGitHubReleases(string repository, string root)
    {
        string json = RunCaptureRequiredWithRetry(
            "gh",
            "release list --repo " + QuoteArg(repository) + " --limit 1000 --json tagName,isDraft",
            root,
            120000,
            3);
        var serializer = new JavaScriptSerializer();
        return serializer.Deserialize<List<GitHubReleaseInfo>>(json) ?? new List<GitHubReleaseInfo>();
    }

    static GitHubReleaseInfo TryViewGitHubRelease(string repository, string tag, string root)
    {
        ProcessResult result = RunWithRetry(
            "gh",
            "release view " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --json tagName,isDraft,assets",
            root,
            120000,
            3,
            false);
        if (result.ExitCode != 0)
        {
            return null;
        }

        try
        {
            var serializer = new JavaScriptSerializer();
            return serializer.Deserialize<GitHubReleaseInfo>(result.Output);
        }
        catch
        {
            return null;
        }
    }

    static List<Project> ResolveReleaseProjects(List<Project> projects, string releaseProjectList)
    {
        var selected = new List<Project>();
        var selectedKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string label in releaseProjectList
            .Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(s => s.Trim())
            .Where(s => s.Length > 0))
        {
            Project project = projects.FirstOrDefault(
                p => p.label.Equals(label, StringComparison.OrdinalIgnoreCase) ||
                    p.key.Equals(label, StringComparison.OrdinalIgnoreCase));
            if (project == null)
                throw new InvalidOperationException("Unknown release project: " + label);
            if (selectedKeys.Add(project.key))
                selected.Add(project);
        }
        return selected;
    }

    static GitHubReleaseInfo FindReplaceableLatestRelease(
        string repository,
        string releaseBase,
        IEnumerable<GitHubReleaseInfo> releases,
        string root)
    {
        var family = releases
            .Where(r => IsReleaseFamilyTag(releaseBase, r.tagName))
            .ToList();
        GitHubReleaseInfo latestPublished = family
            .Where(r => !r.isDraft)
            .OrderByDescending(r => ReleaseVersion(releaseBase, r.tagName))
            .FirstOrDefault();
        if (latestPublished == null)
            return null;

        GitHubReleaseInfo detailed = TryViewGitHubRelease(
            repository,
            latestPublished.tagName,
            root);
        return IsReplaceableLatestRelease(releaseBase, family, detailed)
            ? detailed
            : null;
    }

    static bool IsReplaceableLatestRelease(
        string releaseBase,
        IEnumerable<GitHubReleaseInfo> releases,
        GitHubReleaseInfo detailedRelease)
    {
        if (detailedRelease == null ||
            detailedRelease.isDraft ||
            detailedRelease.assets == null)
        {
            return false;
        }

        int version = ReleaseVersion(releaseBase, detailedRelease.tagName);
        int highestVersion = releases
            .Select(r => ReleaseVersion(releaseBase, r.tagName))
            .DefaultIfEmpty(0)
            .Max();
        return version > 0 &&
            version == highestVersion &&
            detailedRelease.assets.Sum(asset => asset.downloadCount) == 0;
    }

    static bool IsReleaseFamilyTag(string releaseBase, string tag)
    {
        return ReleaseVersion(releaseBase, tag) > 0;
    }

    static int ReleaseVersion(string releaseBase, string tag)
    {
        if (String.IsNullOrWhiteSpace(tag))
            return 0;
        Match match = Regex.Match(
            tag,
            "^" + Regex.Escape(releaseBase) + "-v(?<version>\\d+)$",
            RegexOptions.IgnoreCase);
        int version;
        return match.Success && Int32.TryParse(match.Groups["version"].Value, out version)
            ? version
            : 0;
    }

    static string NextReleaseTag(string releaseBase, IEnumerable<string> names)
    {
        int maxVersion = names
            .Select(name => ReleaseVersion(releaseBase, name))
            .DefaultIfEmpty(0)
            .Max();
        return releaseBase + "-v" + (maxVersion + 1);
    }

    static Tuple<string, List<string>, string> ResolveDownloadedProjectArtifact(Project p, string artifactsRoot)
    {
        string namedRoot = Path.Combine(artifactsRoot, p.artifactName);
        if (Directory.Exists(namedRoot))
        {
            var files = Directory.EnumerateFiles(namedRoot, "*", SearchOption.AllDirectories).ToList();
            if (files.Count > 0)
                return Tuple.Create(namedRoot, files, "named artifact directory: " + p.artifactName);
        }

        string zipPath = Path.Combine(artifactsRoot, p.artifactName + ".zip");
        if (File.Exists(zipPath))
        {
            string expandedRoot = Path.Combine(Path.GetTempPath(), "release-artifact-" + p.artifactName + "-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(expandedRoot);
            ZipFile.ExtractToDirectory(zipPath, expandedRoot);
            var files = Directory.EnumerateFiles(expandedRoot, "*", SearchOption.AllDirectories).ToList();
            if (files.Count > 0)
                return Tuple.Create(expandedRoot, files, "zip artifact: " + p.artifactName + ".zip");
        }

        string expectedLeaf = Path.GetFileName(p.artifactPath ?? "");
        if (!String.IsNullOrWhiteSpace(expectedLeaf))
        {
            var allFiles = Directory.EnumerateFiles(artifactsRoot, "*", SearchOption.AllDirectories).ToList();
            var matching = allFiles.Where(f => Path.GetFileName(f).Equals(expectedLeaf, StringComparison.OrdinalIgnoreCase)).ToList();
            if (matching.Count > 0)
            {
                bool hasRootFiles = Directory.EnumerateFiles(artifactsRoot).Any();
                bool hasChildDirs = Directory.EnumerateDirectories(artifactsRoot).Any();
                if (hasRootFiles && !hasChildDirs)
                    return Tuple.Create(artifactsRoot, Directory.EnumerateFiles(artifactsRoot).ToList(), "direct artifact payload at download root");
                return Tuple.Create(artifactsRoot, matching, "matched project artifact files in unexpected layout");
            }
        }
        return null;
    }

    static IEnumerable<string> ArtifactTreeSummary(string root, int maxEntries)
    {
        if (!Directory.Exists(root))
            return new[] { "<missing: " + root + ">" };
        var items = Directory.EnumerateFileSystemEntries(root, "*", SearchOption.AllDirectories)
            .OrderBy(x => x, StringComparer.OrdinalIgnoreCase)
            .ToList();
        var lines = new List<string>();
        foreach (string item in items.Take(maxEntries))
        {
            string rel = RelativeUnder(root, item);
            lines.Add((Directory.Exists(item) ? "DIR  " : "FILE ") + rel + (File.Exists(item) ? " (" + new FileInfo(item).Length + " bytes)" : ""));
        }
        if (items.Count > maxEntries)
            lines.Add("... " + (items.Count - maxEntries) + " more item(s) omitted");
        return lines;
    }

    static void AddReleaseAsset(string sourcePath, string fallbackName, string uploadRoot, HashSet<string> used, List<string> assets)
    {
        string name = Path.GetFileName(sourcePath);
        if (String.IsNullOrWhiteSpace(name))
            name = Path.GetFileName(fallbackName);
        if (used.Contains(name))
        {
            string candidate = Path.GetFileName((fallbackName ?? name).Replace('/', '-').Replace('\\', '-'));
            if (!String.IsNullOrWhiteSpace(candidate) && !used.Contains(candidate))
                name = candidate;
            else
            {
                string stem = Path.GetFileNameWithoutExtension(name);
                string ext = Path.GetExtension(name);
                int index = 2;
                do { candidate = stem + "-" + index++ + ext; } while (used.Contains(candidate));
                name = candidate;
            }
        }
        string destination = Path.Combine(uploadRoot, name);
        File.Copy(sourcePath, destination, true);
        used.Add(name);
        assets.Add(destination);
    }

    static string RelativeUnder(string root, string path)
    {
        string r = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string p = Path.GetFullPath(path);
        return p.StartsWith(r, StringComparison.OrdinalIgnoreCase) ? p.Substring(r.Length) : Path.GetFileName(path);
    }

    static string Sha256File(string path)
    {
        using (var sha = SHA256.Create())
        using (var stream = File.OpenRead(path))
            return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "");
    }

    static ProcessResult RunCapture(string file, string arguments, string workingDirectory, int timeoutMs)
    {
        var psi = new ProcessStartInfo(file, arguments)
        {
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        using (var process = new Process())
        {
            process.StartInfo = psi;

            var output = new StringBuilder();
            var error = new StringBuilder();
            var outputLock = new object();
            var errorLock = new object();
            bool outputComplete = false, errorComplete = false;

            process.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data == null)
                {
                    lock (outputLock) { outputComplete = true; Monitor.PulseAll(outputLock); }
                    return;
                }
                lock (outputLock)
                    output.AppendLine(e.Data);
            };
            process.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data == null)
                {
                    lock (errorLock) { errorComplete = true; Monitor.PulseAll(errorLock); }
                    return;
                }
                lock (errorLock)
                    error.AppendLine(e.Data);
            };

            if (!process.Start())
                throw new InvalidOperationException("Failed to start process: " + file + " " + arguments);

            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            if (!process.WaitForExit(timeoutMs))
            {
                try { process.Kill(); } catch { }
                try { process.WaitForExit(5000); } catch { }
                throw new TimeoutException("Process timed out: " + file + " " + arguments);
            }

            // A descendant may inherit an output pipe after the direct child
            // exits. Never turn a bounded smoke timeout into an infinite EOF wait.
            lock (outputLock) if (!outputComplete) Monitor.Wait(outputLock, 2000);
            lock (errorLock) if (!errorComplete) Monitor.Wait(errorLock, 2000);
            if (!outputComplete) { try { process.CancelOutputRead(); } catch { } }
            if (!errorComplete) { try { process.CancelErrorRead(); } catch { } }
            string capturedOutput, capturedError;
            lock (outputLock) capturedOutput = output.ToString();
            lock (errorLock) capturedError = error.ToString();
            return new ProcessResult { ExitCode = process.ExitCode, Output = capturedOutput, Error = capturedError };
        }
    }

    static string RunCaptureRequired(string file, string arguments, string workingDirectory)
    {
        var result = RunCapture(file, arguments, workingDirectory, 120000);
        if (result.ExitCode != 0)
            throw new InvalidOperationException(ProcessFailureMessage(file, arguments, result));
        return result.Output;
    }

    static string RunCaptureRequiredWithRetry(string file, string arguments, string workingDirectory, int timeoutMs, int maxAttempts)
    {
        ProcessResult result = RunWithRetry(file, arguments, workingDirectory, timeoutMs, maxAttempts, false);
        return result.Output;
    }

    static void RunRequired(string file, string arguments, string workingDirectory)
    {
        var result = RunCapture(file, arguments, workingDirectory, 120000);
        if (result.ExitCode != 0)
            throw new InvalidOperationException(ProcessFailureMessage(file, arguments, result));
        PrintProcessOutput(result);
    }

    static void RunRequiredWithRetry(string file, string arguments, string workingDirectory, int timeoutMs, int maxAttempts)
    {
        ProcessResult result = RunWithRetry(file, arguments, workingDirectory, timeoutMs, maxAttempts, true);
        PrintProcessOutput(result);
    }

    static ProcessResult RunWithRetry(string file, string arguments, string workingDirectory, int timeoutMs, int maxAttempts, bool printRetryWarnings)
    {
        if (maxAttempts < 1)
            maxAttempts = 1;

        ProcessResult result = null;
        for (int attempt = 1; attempt <= maxAttempts; attempt++)
        {
            result = RunCapture(file, arguments, workingDirectory, timeoutMs);
            if (result.ExitCode == 0)
                return result;

            if (attempt >= maxAttempts || !IsRetryableProcessFailure(file, arguments, result))
                break;

            int delayMs = Math.Min(30000, 2000 * attempt);
            if (printRetryWarnings)
            {
                Console.Error.WriteLine(
                    "WARNING: transient command failure; retrying attempt " +
                    (attempt + 1).ToString() + "/" + maxAttempts.ToString() +
                    " after " + delayMs.ToString() + " ms: " + file + " " + arguments);
            }
            System.Threading.Thread.Sleep(delayMs);
        }

        throw new InvalidOperationException(ProcessFailureMessage(file, arguments, result));
    }

    static bool IsRetryableProcessFailure(string file, string arguments, ProcessResult result)
    {
        if (result == null)
            return false;
        if (!Path.GetFileNameWithoutExtension(file).Equals("gh", StringComparison.OrdinalIgnoreCase))
            return false;
        if (!Regex.IsMatch(arguments ?? "", @"(^|\s)release\s+(list|view|edit|upload|delete)\b", RegexOptions.IgnoreCase))
            return false;

        string text = (result.Output ?? "") + "\n" + (result.Error ?? "");
        return Regex.IsMatch(
            text,
            @"(We had issues producing the response|GitHub Status|HTTP\s+5\d\d|5\d\d\s+(Internal Server Error|Bad Gateway|Service Unavailable|Gateway Timeout)|temporarily unavailable|connection (reset|refused)|TLS handshake timeout|timed? out|EOF)",
            RegexOptions.IgnoreCase);
    }

    static string ProcessFailureMessage(string file, string arguments, ProcessResult result)
    {
        if (result == null)
            return file + " " + arguments + " failed without process result.";
        return file + " " + arguments + " failed with exit code " + result.ExitCode + Environment.NewLine + result.Output + Environment.NewLine + result.Error;
    }

    static void PrintProcessOutput(ProcessResult result)
    {
        if (!String.IsNullOrWhiteSpace(result.Output))
            Console.Write(result.Output);
        if (!String.IsNullOrWhiteSpace(result.Error))
            Console.Error.Write(result.Error);
    }

    static string JoinArgs(string[] args)
    {
        return String.Join(" ", args.Select(QuoteArg));
    }

    static string QuoteArg(string value)
    {
        if (value == null)
            return "\"\"";
        if (value.Length == 0)
            return "\"\"";
        bool needs = value.Any(ch => Char.IsWhiteSpace(ch) || ch == '"');
        if (!needs)
            return value;
        var sb = new StringBuilder();
        sb.Append('"');
        int backslashes = 0;
        foreach (char ch in value)
        {
            if (ch == '\\')
            {
                backslashes++;
                continue;
            }
            if (ch == '"')
            {
                sb.Append('\\', backslashes * 2 + 1);
                sb.Append('"');
                backslashes = 0;
                continue;
            }
            sb.Append('\\', backslashes);
            backslashes = 0;
            sb.Append(ch);
        }
        sb.Append('\\', backslashes * 2);
        sb.Append('"');
        return sb.ToString();
    }
}

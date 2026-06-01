using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
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
    public string smokePath { get; set; }
    public string skipKey { get; set; }
}

sealed class ProcessResult
{
    public int ExitCode;
    public string Output = "";
    public string Error = "";
}

static class RepoTools
{
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
            if (command == "validate-project-map") return ValidateProjectMap(rest);
            if (command == "test-workflow-project-selection") return TestWorkflowProjectSelection(rest);
            if (command == "policy-warnings") return InvokePolicyWarnings(rest);
            if (command == "readme-consistency") return TestReadmeConsistency(rest);
            if (command == "detect-projects") return DetectProjects(rest);
            if (command == "checksum-summary") return ChecksumSummary(rest);
            if (command == "smoke-windows-build") return SmokeWindowsBuild(rest);
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
        Console.WriteLine("  smoke-windows-build");
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

            if (!Directory.Exists(Path.Combine(root, p.folder)))
                throw new InvalidOperationException("Project folder does not exist: " + p.folder);

            if (!buildScript.Contains(":Build" + p.skipKey) &&
                !buildScript.Contains("call :IsSkipped " + p.skipKey))
                throw new InvalidOperationException("Build script does not appear to handle skip key: " + p.skipKey);

            if (!readme.Contains("`" + p.folder + "`"))
                throw new InvalidOperationException("README project table does not appear to mention folder: " + p.folder);
        }

        Console.WriteLine("Project map validation passed (" + projects.Count + " projects).");
        return 0;
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
        Console.WriteLine("Workflow project selector validation passed (" + projects.Count + " projects plus All).");
        return 0;
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
                "Special" + "Folder")),
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
                        warnings.Add(Rel(root, file) + ":" + lineNo + " [" + rule.Item1 + "] " + line.Trim());
                }
            }
        }

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
                bool sharedChange = changed.Any(p => p.StartsWith(".github/", StringComparison.OrdinalIgnoreCase) ||
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

        var selectedKeys = new HashSet<string>(selected.Select(p => p.key), StringComparer.OrdinalIgnoreCase);
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
        var selected = new List<Project>();
        foreach (var p in projects)
        {
            string latestTag = RunCapture("git", "tag --list " + QuoteArg(p.key + "-v*") + " --sort=-v:refname", root, 120000)
                .Output
                .Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                .FirstOrDefault();
            if (latestTag == null)
            {
                if (currentTouched.Contains(p.key))
                    selected.Add(p);
                continue;
            }

            string files = RunCapture("git", "diff --name-only " + QuoteArg(latestTag) + " " + QuoteArg(head), root, 120000).Output;
            string prefix = p.folder.Replace('\\', '/') + "/";
            bool touchedSinceLatestRelease = files.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                .Any(c => c.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
            if (touchedSinceLatestRelease)
                selected.Add(p);
        }
        return selected;
    }

    static int ChecksumSummary(string[] args)
    {
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        var lines = new List<string>
        {
            "## Windows build outputs",
            "",
            "GitHub Actions downloads workflow artifacts as ZIP archives. The hashes below are for the primary payload files inside those artifacts.",
            "",
            "| Project | File | SHA256 |",
            "| --- | --- | --- |"
        };
        int count = 0;
        foreach (var p in projects)
        {
            string path = Path.Combine(root, p.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(path))
                continue;
            lines.Add("| " + p.label + " | `" + Path.GetFileName(path) + "` | `" + Sha256File(path).ToLowerInvariant() + "` |");
            ++count;
        }
        if (count == 0)
            throw new InvalidOperationException("No build outputs were found.");
        AppendSummary(lines);
        Console.WriteLine("Checksum summary wrote " + count + " entr" + (count == 1 ? "y." : "ies."));
        return 0;
    }

    static int SmokeWindowsBuild(string[] args)
    {
        string root = RepositoryRoot(args);
        var projects = LoadProjectMap(root);
        Console.WriteLine("Running Windows build smoke tests...");

        Project desktopStub = projects.FirstOrDefault(p => p.key == "DesktopStub");
        if (desktopStub != null)
        {
            string smokePath = String.IsNullOrWhiteSpace(desktopStub.smokePath) ? desktopStub.artifactPath : desktopStub.smokePath;
            string sourceExe = Path.Combine(root, smokePath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                string desktopStubVersion = VerifyDesktopStubBinaryVersion(sourceExe, "DesktopStub");
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "DesktopStubSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
                bool deleteTemp = false;
                string smokeIdentity = null;
                try
                {
                    string exe = Path.Combine(tempRoot, Path.GetFileName(sourceExe));
                    File.Copy(sourceExe, exe, true);
                    string sourceBuildDir = Path.GetDirectoryName(sourceExe);
                    foreach (string helper in new[] { "DesktopStubLiveTileBroker.exe", "DesktopStubAppxStub.exe" })
                    {
                        string sourceHelper = Path.Combine(sourceBuildDir, helper);
                        if (File.Exists(sourceHelper))
                        {
                            if (helper == "DesktopStubLiveTileBroker.exe")
                            {
                                string helperVersion = VerifyDesktopStubBinaryVersion(sourceHelper, helper);
                                if (!String.Equals(helperVersion, desktopStubVersion, StringComparison.Ordinal))
                                    throw new InvalidOperationException("DesktopStub helper version does not match DesktopStub.exe: " + helperVersion + " != " + desktopStubVersion);
                            }
                            File.Copy(sourceHelper, Path.Combine(tempRoot, helper), true);
                        }
                    }

                    string ini = Path.Combine(tempRoot, "DesktopStub.ini");
                    string wallpaper = Path.Combine(tempRoot, "wallpaper.bmp");
                    WriteTestBmp(wallpaper);

                    SmokeProcess(exe, new[] { "--version" }, new[] { 0 }, 30, "DesktopStub version");
                    SmokeProcess(exe, new[] { "--help" }, new[] { 0 }, 30, "DesktopStub help");
                    SmokeProcess(exe, new[] { "--ini", ini, "--no-tray", "--console", "--logging", "--notifications", "--live-tile-mode", "Auto", "--scales", "auto", "--asset", "MediumTile=1", "--regenerate-manifest" }, new[] { 0 }, 30, "DesktopStub settings and manifest");
                    string manifestPath = Path.Combine(Path.GetDirectoryName(exe), "AppxManifest.xml");
                    smokeIdentity = "dev.local.desktopstubsmoke." + Guid.NewGuid().ToString("N").Substring(0, 16);
                    SetDesktopStubManifestIdentity(manifestPath, smokeIdentity);
                    SmokeProcess(exe, new[] { "--ini", ini, "--wallpaper", wallpaper, "--once", "--no-tray", "--no-monitor" }, new[] { 0 }, 30, "DesktopStub one-shot generation");
                    SmokeProcess(exe, new[] { "--ini", ini, "--wallpaper", Path.Combine(tempRoot, "missing.png"), "--no-tray" }, new[] { 2 }, 30, "DesktopStub invalid wallpaper guard");

                    if (!File.Exists(manifestPath))
                        throw new InvalidOperationException("DesktopStub smoke test did not create AppxManifest.xml.");
                    string manifestAfterSmoke = File.ReadAllText(manifestPath, Encoding.UTF8);
                    VerifyDesktopStubManifestVersion(manifestAfterSmoke, desktopStubVersion);
                    if (!Regex.IsMatch(manifestAfterSmoke, @"<\s*Identity\b[^>]*\bName\s*=\s*[""']" + Regex.Escape(smokeIdentity) + @"[""']", RegexOptions.IgnoreCase))
                        throw new InvalidOperationException("DesktopStub smoke test did not preserve the custom AppxManifest.xml package identity.");
                    deleteTemp = true;
                }
                finally
                {
                    if (!String.IsNullOrWhiteSpace(smokeIdentity))
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

    static string VerifyDesktopStubBinaryVersion(string file, string label)
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

    static string NormalizeFourPartVersion(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return null;
        var match = Regex.Match(value, @"\b(?<version>\d+\.\d+\.\d+\.\d+)\b");
        return match.Success ? match.Groups["version"].Value : null;
    }

    static void VerifyDesktopStubManifestVersion(string manifestXml, string expectedVersion)
    {
        if (!Regex.IsMatch(
            manifestXml,
            @"<\s*Identity\b[^>]*\bVersion\s*=\s*[""']" + Regex.Escape(expectedVersion) + @"[""']",
            RegexOptions.IgnoreCase))
            throw new InvalidOperationException("DesktopStub smoke test AppxManifest.xml version does not match the binary version: " + expectedVersion);
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

    static void SmokeProcess(string file, string[] args, int[] allowedExitCodes, int timeoutSeconds, string name)
    {
        if (!File.Exists(file))
        {
            Console.WriteLine("skip - " + name + " not built: " + file);
            return;
        }

        var result = RunCapture(file, JoinArgs(args), Path.GetDirectoryName(file), timeoutSeconds * 1000);
        if (!allowedExitCodes.Contains(result.ExitCode))
            throw new InvalidOperationException("Smoke test failed: " + name + " " + String.Join(" ", args) + " exited " + result.ExitCode + Environment.NewLine + "STDOUT:" + Environment.NewLine + result.Output + Environment.NewLine + "STDERR:" + Environment.NewLine + result.Error);
        Console.WriteLine("ok - " + name + " " + String.Join(" ", args));
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
        var releaseLabels = releaseProjectList.Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(s => s.Trim())
            .Where(s => s.Length > 0)
            .ToList();
        var releaseKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string label in releaseLabels)
        {
            Project p = projects.FirstOrDefault(x => x.label.Equals(label, StringComparison.OrdinalIgnoreCase) ||
                x.key.Equals(label, StringComparison.OrdinalIgnoreCase));
            if (p == null)
                throw new InvalidOperationException("Unknown release project: " + label);
            releaseKeys.Add(p.key);
        }
        var entries = new List<Tuple<Project, string, List<string>, string>>();
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
            string tag = null;
            foreach (string candidate in matchingTags)
            {
                string sha = RunCaptureRequired("git", "rev-list -n 1 " + QuoteArg(candidate), root).Trim();
                if (sha == fullSha)
                {
                    tag = candidate;
                    break;
                }
            }

            string previousTag = matchingTags.FirstOrDefault(t => t != tag);
            if (tag == null)
            {
                int maxVersion = 0;
                var re = new Regex("^" + Regex.Escape(releaseBase) + "-v(?<version>\\d+)$");
                foreach (string candidate in matchingTags)
                {
                    var m = re.Match(candidate);
                    if (m.Success)
                        maxVersion = Math.Max(maxVersion, Int32.Parse(m.Groups["version"].Value));
                }
                tag = releaseBase + "-v" + (maxVersion + 1);
            }
            if (previousTag == null)
                previousTag = matchingTags.FirstOrDefault(t => t != tag);

            string workRoot = Path.Combine(Path.GetTempPath(), "release-assets-" + releaseBase + "-" + Guid.NewGuid().ToString("N"));
            string uploadRoot = Path.Combine(workRoot, "upload");
            Directory.CreateDirectory(uploadRoot);
            var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var releaseAssets = new List<string>();
            foreach (string file in files.OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
                AddReleaseAsset(file, RelativeUnder(artifactRoot, file), uploadRoot, used, releaseAssets);

            if (releaseAssets.Count == 0)
                throw new InvalidOperationException("No release assets were prepared for " + p.label + ".");

            string logArgs = previousTag != null
                ? "log --format=%h %s " + QuoteArg(previousTag + ".." + fullSha)
                : "log --format=%h %s -n 10 " + QuoteArg(fullSha);
            string commitText = RunCapture("git", logArgs, root, 120000).Output;
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
            bool releaseExists = RunCapture("gh", "release view " + QuoteArg(tag) + " --repo " + QuoteArg(repository), root, 120000).ExitCode == 0;
            if (releaseExists)
                RunRequired("gh", "release edit " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --title " + QuoteArg(tag) + " --notes-file " + QuoteArg(notesPath), root);
            else
                RunRequired("gh", "release create " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --target " + QuoteArg(fullSha) + " --title " + QuoteArg(tag) + " --notes-file " + QuoteArg(notesPath), root);

            RunRequired("gh", "release upload " + QuoteArg(tag) + " " + JoinArgs(releaseAssets.ToArray()) + " --repo " + QuoteArg(repository) + " --clobber", root);
            summary.Add("| " + p.label + " | `" + tag + "` | " + String.Join(", ", releaseAssets.Select(Path.GetFileName)) + " |");
        }

        AppendSummary(summary);
        return 0;
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

            process.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data == null)
                    return;
                lock (outputLock)
                    output.AppendLine(e.Data);
            };
            process.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data == null)
                    return;
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
                try { process.WaitForExit(); } catch { }
                throw new TimeoutException("Process timed out: " + file + " " + arguments);
            }

            process.WaitForExit();
            return new ProcessResult { ExitCode = process.ExitCode, Output = output.ToString(), Error = error.ToString() };
        }
    }

    static string RunCaptureRequired(string file, string arguments, string workingDirectory)
    {
        var result = RunCapture(file, arguments, workingDirectory, 120000);
        if (result.ExitCode != 0)
            throw new InvalidOperationException(file + " " + arguments + " failed with exit code " + result.ExitCode + Environment.NewLine + result.Output + Environment.NewLine + result.Error);
        return result.Output;
    }

    static void RunRequired(string file, string arguments, string workingDirectory)
    {
        var result = RunCapture(file, arguments, workingDirectory, 120000);
        if (result.ExitCode != 0)
            throw new InvalidOperationException(file + " " + arguments + " failed with exit code " + result.ExitCode + Environment.NewLine + result.Output + Environment.NewLine + result.Error);
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

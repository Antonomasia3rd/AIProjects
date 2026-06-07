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
    public string[] artifactPaths { get; set; }
    public string smokePath { get; set; }
    public string skipKey { get; set; }
}

sealed class ProcessResult
{
    public int ExitCode;
    public string Output = "";
    public string Error = "";
}

sealed class GitHubReleaseInfo
{
    public string tagName { get; set; }
    public bool isDraft { get; set; }
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

            if (!Directory.Exists(Path.Combine(root, p.folder)))
                throw new InvalidOperationException("Project folder does not exist: " + p.folder);

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

        var releaseNames = new[] { "DesktopStub-v2", "DesktopStub-v5", "DesktopStub-v4" };
        if (NextReleaseTag("DesktopStub", releaseNames) != "DesktopStub-v6")
            throw new InvalidOperationException("Release version selection must include draft release names.");

        var commandLineConsumers = ProjectsUsingChangedDependencies(root, projects, new List<string> { "dependencies/command_line.inc" })
            .Select(p => p.key)
            .OrderBy(k => k, StringComparer.OrdinalIgnoreCase)
            .ToList();
        var expectedConsumers = new[] { "DesktopStub", "DiscordRPC" };
        if (!commandLineConsumers.SequenceEqual(expectedConsumers.OrderBy(k => k, StringComparer.OrdinalIgnoreCase), StringComparer.OrdinalIgnoreCase))
            throw new InvalidOperationException("Dependency consumer detection for dependencies/command_line.inc returned " + String.Join(", ", commandLineConsumers) + ".");

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
            Tuple.Create("DesktopStub/src/ga_app.inc", "Maintenance marker", JoinLiteral("ob", "solete") + " AppX launch-forwarding fallback kept for diagnostics/rollback"),
            Tuple.Create("DesktopStub/src/ga_livetile_broker_app.inc", "Profile storage", JoinLiteral("Application", "Data::Current().LocalFolder()")),
            Tuple.Create("DesktopStub/tools/DesktopStubSourceCheck.cpp", "Maintenance marker", "INI template does not expose " + JoinLiteral("ob", "solete") + " Manifest section"),
            Tuple.Create("DesktopStub/tools/DesktopStubSourceCheck.cpp", "Maintenance marker", JoinLiteral("Ob", "solete") + " Manifest INI section is removed and blocked"),
            Tuple.Create("legacy/asusblink/asusblink.cs", "Profile storage", "Environment.GetFolderPath(Environment." + JoinLiteral("Special", "Folder") + ".Startup)")
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

    static bool ProjectUsesAnyChangedDependency(string root, Project project, List<string> changed)
    {
        var dependencies = changed
            .Select(p => p.Replace('\\', '/'))
            .Where(p => p.StartsWith("dependencies/", StringComparison.OrdinalIgnoreCase))
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
                if (!new[] { ".h", ".hpp", ".inc" }.Contains(ext, StringComparer.OrdinalIgnoreCase))
                    continue;

                string fileName = Path.GetFileName(file);
                if (dependencyNames.Contains(fileName))
                    continue;

                string text = ReadAll(file);
                if (dependencyNames.Any(name => text.IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0))
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
            if (!new[] { ".cpp", ".c", ".h", ".hpp", ".inc", ".rc" }.Contains(ext, StringComparer.OrdinalIgnoreCase))
                continue;

            string text = ReadAll(file);
            if (text.IndexOf("dependencies", StringComparison.OrdinalIgnoreCase) < 0)
                continue;

            foreach (string name in dependencyNames)
            {
                if (!String.IsNullOrWhiteSpace(name) && text.IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0)
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
                    string helpIni = Path.Combine(tempRoot, "HelpSideEffect.ini");
                    string customHelpIni = Path.Combine(tempRoot, "CustomHelp.ini");
                    string concurrentIni = Path.Combine(tempRoot, "ConcurrentWrites.ini");
                    string wallpaper = Path.Combine(tempRoot, "wallpaper.bmp");
                    string trailingSpaces = new string(' ', 2);
                    WriteTestBmp(wallpaper);

                    ProcessResult versionResult = SmokeProcess(exe, new[] { "--version" }, new[] { 0 }, 30, "DesktopStub version");
                    string expectedTag = "DesktopStub-v" + desktopStubVersion.Split('.')[0];
                    if (versionResult.Output.IndexOf(expectedTag + " (" + desktopStubVersion + ")", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DesktopStub --version did not report the expected release tag/version: " + expectedTag + " (" + desktopStubVersion + ")");
                    SmokeProcess(exe, new[] { "--help", "--ini", helpIni, "--set", "Settings.TrayIcon=0" }, new[] { 0 }, 30, "DesktopStub help");
                    AssertFileDoesNotExist(helpIni, "DesktopStub --help must be side-effect-free");
                    string customHelpText = "[CommandLineHelp]\r\n\"Template\" = \"CUSTOM_HELP_MARKER {exe}\"\r\n";
                    File.WriteAllText(customHelpIni, customHelpText, new UTF8Encoding(true));
                    byte[] customHelpBefore = File.ReadAllBytes(customHelpIni);
                    ProcessResult customHelpResult = SmokeProcess(exe, new[] { "--help", "--ini", customHelpIni, "--set", "Settings.TrayIcon=0" }, new[] { 0 }, 30, "DesktopStub custom help");
                    if (customHelpResult.Output.IndexOf("CUSTOM_HELP_MARKER", StringComparison.Ordinal) < 0)
                        throw new InvalidOperationException("DesktopStub --help did not use the configured INI template.");
                    if (!customHelpBefore.SequenceEqual(File.ReadAllBytes(customHelpIni)))
                        throw new InvalidOperationException("DesktopStub --help modified its configured INI.");
                    SmokeProcess(exe, new[] { "--ini", ini, "--no-tray", "--console", "--logging", "--notifications", "--live-tile-mode", "Auto", "--scales", "auto", "--asset", "MediumTile=1", "--set", "Strings.TrayTip=DesktopStub" + trailingSpaces, "--regenerate-manifest" }, new[] { 0 }, 30, "DesktopStub settings and manifest");
                    AssertFileContains(ini, "\"TrayTip\" = \"DesktopStub" + trailingSpaces + "\"", "DesktopStub --set must preserve trailing value spaces");
                    string manifestPath = Path.Combine(Path.GetDirectoryName(exe), "AppxManifest.xml");
                    smokeIdentity = "dev.local.desktopstubsmoke." + Guid.NewGuid().ToString("N").Substring(0, 16);
                    SetDesktopStubManifestIdentity(manifestPath, smokeIdentity);
                    SmokeProcess(exe, new[] { "--ini", ini, "--wallpaper", wallpaper, "--once", "--no-tray", "--no-monitor" }, new[] { 0 }, 30, "DesktopStub one-shot generation");
                    SmokeProcess(exe, new[] { "--ini", ini, "--wallpaper", Path.Combine(tempRoot, "missing.png"), "--no-tray" }, new[] { 2 }, 30, "DesktopStub invalid wallpaper guard");
                    VerifyDesktopStubConcurrentIniWrites(exe, concurrentIni);

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

        Project discordRpc = projects.FirstOrDefault(p => p.key == "DiscordRPC");
        if (discordRpc != null)
        {
            string sourceExe = Path.Combine(root, discordRpc.artifactPath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(sourceExe))
            {
                string tempBase = Environment.GetEnvironmentVariable("RUNNER_TEMP");
                if (String.IsNullOrWhiteSpace(tempBase))
                    tempBase = Path.GetTempPath();
                string tempRoot = Path.Combine(tempBase, "DiscordRPCSmoke-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(tempRoot);
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
                    string trailingSpaces = new string(' ', 2);

                    SmokeProcess(exe, new[] { "--help", "--ini", helpIni, "--set", "general.token=should-not-be-written" }, new[] { 0 }, 30, "DiscordRPC help");
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
                    SmokeProcess(exe, new[] { "--version", "--ini", versionIni, "--set", "general.token=should-not-be-written" }, new[] { 0 }, 30, "DiscordRPC version");
                    AssertFileDoesNotExist(versionIni, "DiscordRPC --version must be side-effect-free");
                    SmokeProcess(exe, new[] { "--tokenXYZ", "abc" }, new[] { 2 }, 30, "DiscordRPC strict option parsing");
                    SmokeProcess(exe, new[] { "--ini", redactIni, "--client-id", "123456789012345678", "--token", "super-secret-smoke-token", "--dry-run", "--no-tray" }, new[] { 0 }, 30, "DiscordRPC token redaction");
                    AssertFileDoesNotContain(redactLog, "super-secret-smoke-token", "DiscordRPC command-line token must not be written to the log");
                    SmokeProcess(exe, new[] { "--ini", tempRoot, "--set", "general.client_id=123456789012345678", "--dry-run", "--no-tray" }, new[] { 1 }, 30, "DiscordRPC config write failure");
                    SmokeProcess(exe, new[] { "--ini", dottedIni, "--set", "section.with.dot.key=value" + trailingSpaces, "--dry-run", "--no-tray" }, new[] { 0 }, 30, "DiscordRPC dotted --set parsing");
                    AssertFileContains(dottedIni, "[section.with.dot]", "DiscordRPC --set must split Section.Key at the last dot before '='");
                    AssertFileContains(dottedIni, "\"key\" = \"value" + trailingSpaces + "\"", "DiscordRPC --set must preserve dotted section names and trailing value spaces");
                    SmokeProcess(exe, new[] { "--dry-run", "--no-tray" }, new[] { 0 }, 30, "DiscordRPC dry-run");
                    VerifyDiscordRpcNoTrayControl(exe, tempRoot);
                }
                finally
                {
                    try { Directory.Delete(tempRoot, true); } catch { }
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
                    JoinArgs(new[] { "--ini", ini, "--set", "Concurrent.Key" + i + "=Value" + i, "--exit" }))
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
                process.Dispose();
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
            "\"log_path\" = \"" + log.Replace("\\", "\\\\") + "\"\r\n";
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
                WaitForFileText(log, "Starting DiscordRPC C++.", 10000, "DiscordRPC no-tray resident startup");
                SmokeProcess(
                    exe,
                    new[] { "--ini", ini, "--set", "app.verbose_logging=true" },
                    new[] { 0 },
                    10,
                    "DiscordRPC no-tray reload");
                WaitForFileText(log, "Configuration reloaded.", 10000, "DiscordRPC no-tray reload delivery");

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
                    try { resident.WaitForExit(); } catch { }
                }
            }
        }
    }

    static void WaitForFileText(string file, string needle, int timeoutMs, string name)
    {
        Stopwatch timer = Stopwatch.StartNew();
        while (timer.ElapsedMilliseconds < timeoutMs)
        {
            if (File.Exists(file))
            {
                string text = File.ReadAllText(file, Encoding.UTF8);
                if (text.IndexOf(needle, StringComparison.Ordinal) >= 0)
                    return;
            }
            System.Threading.Thread.Sleep(100);
        }
        throw new TimeoutException(name + " timed out waiting for '" + needle + "'.");
    }

    static ProcessResult SmokeProcess(string file, string[] args, int[] allowedExitCodes, int timeoutSeconds, string name)
    {
        if (!File.Exists(file))
        {
            Console.WriteLine("skip - " + name + " not built: " + file);
            return new ProcessResult { ExitCode = 0 };
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
                else
                {
                    tag = NextReleaseTag(
                        releaseBase,
                        matchingTags.Concat(releases.Select(r => r.tagName)));
                }
            }
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
                    Console.WriteLine("Release " + tag + " is already published; leaving it unchanged.");
                    summary.Add("| " + p.label + " | `" + tag + "` | already published |");
                    continue;
                }

                bool createdDraft = false;
                try
                {
                    if (existingRelease != null)
                    {
                        RunRequired("gh", "release edit " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --draft --target " + QuoteArg(fullSha) + " --title " + QuoteArg(tag) + " --notes-file " + QuoteArg(notesPath), root);
                    }
                    else
                    {
                        RunRequired("gh", "release create " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --draft --target " + QuoteArg(fullSha) + " --title " + QuoteArg(tag) + " --notes-file " + QuoteArg(notesPath), root);
                        createdDraft = true;
                    }

                    RunRequired("gh", "release upload " + QuoteArg(tag) + " " + JoinArgs(releaseAssets.ToArray()) + " --repo " + QuoteArg(repository) + " --clobber", root);
                    RunRequired("gh", "release edit " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --draft=false", root);
                }
                catch
                {
                    if (createdDraft)
                    {
                        ProcessResult cleanup = RunCapture(
                            "gh",
                            "release delete " + QuoteArg(tag) + " --repo " + QuoteArg(repository) + " --yes",
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
        string json = RunCaptureRequired(
            "gh",
            "release list --repo " + QuoteArg(repository) + " --limit 1000 --json tagName,isDraft",
            root);
        var serializer = new JavaScriptSerializer();
        return serializer.Deserialize<List<GitHubReleaseInfo>>(json) ?? new List<GitHubReleaseInfo>();
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

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security;
using AIProjects.Dependencies;

static class PhotoCollage
{
    const string SettingsSection = "Settings";
    const string InputFolderKey = "InputFolder";
    const string OutputFileKey = "OutputFile";
    const string ColsKey = "Cols";
    const string MaxImagesKey = "MaxImages";
    const string JpegQualityKey = "JpegQuality";
    const string MaxCanvasMegapixelsKey = "MaxCanvasMegapixels";
    const string LogFileKey = "LogFile";
    const string DefaultLogFileName = "PhotoCollage.log";

    [STAThread]
    static int Main(string[] args)
    {
        // Information commands never parse companion arguments or touch the
        // filesystem. This keeps help/version useful even while diagnosing a
        // malformed operational command.
        if (args.Any(IsHelpOption))
        {
            Usage();
            return 0;
        }
        if (args.Any(IsVersionOption))
        {
            Console.WriteLine("PhotoCollage " + ProductVersion());
            return 0;
        }

        try
        {
            ParsedCommand command = ParseCommandLine(args);
            if (command.StartupManagement)
            {
                var traySpec = BuildConfigurationTraySpec(ResolveIniPath(command.IniPath));
                traySpec.SaveSettings(command.PersistentSettings);
                if (command.Tray) return ManagedConfigurationTray.Run(traySpec);
                Console.WriteLine("Saved Startup configuration for: " + traySpec.IniPath);
                return 0;
            }
            if (command.Tray)
                return ManagedConfigurationTray.Run(BuildConfigurationTraySpec(ResolveIniPath(command.IniPath)));
            Options options = BuildOptions(command);
            if (command.ConfigureOnly)
            {
                if (command.ShowConfiguration) PrintConfiguration(options);
                Console.WriteLine("Configuration is valid: " + options.IniPath);
                return 0;
            }
            if (command.ShowConfiguration)
            {
                PrintConfiguration(options);
                return 0;
            }
            if (String.IsNullOrWhiteSpace(options.InputFolder) || String.IsNullOrWhiteSpace(options.OutputFile))
            {
                Usage();
                return 2;
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

    sealed class Options
    {
        public string InputFolder;
        public string OutputFile;
        public int Cols = 5;
        public int MaxImages = 25;
        public long JpegQuality = 80;
        public long MaxCanvasMegapixels = 100;
        public string LogFile;
        public string IniPath;
        public bool RunAtStartup;
    }

    sealed class ParsedCommand
    {
        public string IniPath;
        public readonly Dictionary<string, string> DirectSettings =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public readonly Dictionary<string, string> PersistentSettings =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public bool ConfigureOnly;
        public bool ShowConfiguration;
        public bool Tray;
        public bool StartupManagement;
    }

    static Options ParseArgs(string[] args)
    {
        return BuildOptions(ParseCommandLine(args));
    }

    static ParsedCommand ParseCommandLine(string[] args)
    {
        var parsed = new ParsedCommand();
        if (args == null)
            args = new string[0];
        for (int i = 0; i < args.Length; ++i)
        {
            string a = args[i];
            if (Is(a, "-InputFolder") || Is(a, "--input-folder"))
                SetDirectSetting(parsed, InputFolderKey, RequireValue(args, ref i, a));
            else if (Is(a, "-OutputFile") || Is(a, "--output-file"))
                SetDirectSetting(parsed, OutputFileKey, RequireValue(args, ref i, a));
            else if (Is(a, "-Cols") || Is(a, "--cols"))
                SetDirectSetting(parsed, ColsKey, RequireValue(args, ref i, a));
            else if (Is(a, "-MaxImages") || Is(a, "--max-images"))
                SetDirectSetting(parsed, MaxImagesKey, RequireValue(args, ref i, a));
            else if (Is(a, "-JpegQuality") || Is(a, "--jpeg-quality"))
                SetDirectSetting(parsed, JpegQualityKey, RequireValue(args, ref i, a));
            else if (Is(a, "-MaxCanvasMegapixels") || Is(a, "--max-canvas-megapixels"))
                SetDirectSetting(parsed, MaxCanvasMegapixelsKey, RequireValue(args, ref i, a));
            else if (Is(a, "-LogFile") || Is(a, "--log-file"))
                SetDirectSetting(parsed, LogFileKey, RequireValue(args, ref i, a));
            else if (Is(a, "--ini") || Is(a, "-IniFile"))
                parsed.IniPath = RequireValue(args, ref i, a);
            else if (Is(a, "--set"))
                SetPersistentSetting(parsed, RequireValue(args, ref i, a));
            else if (Is(a, "--configure-only"))
                parsed.ConfigureOnly = true;
            else if (Is(a, "--tray"))
                parsed.Tray = true;
            else if (Is(a, "--startup") || Is(a, "--no-startup"))
            {
                parsed.StartupManagement = true;
                SetPersistentSetting(parsed, ManagedConfigurationStartup.Key + "=" + (Is(a, "--startup") ? "1" : "0"));
            }
            else if (Is(a, "--show-config") || Is(a, "--print-config"))
                parsed.ShowConfiguration = true;
            else throw new ArgumentException("Unknown argument: " + a);
        }

        if (parsed.Tray && (parsed.ConfigureOnly || parsed.ShowConfiguration ||
            parsed.DirectSettings.Count != 0 || (parsed.PersistentSettings.Count != 0 && !parsed.StartupManagement)))
            throw new ArgumentException("--tray accepts --ini and optional Startup management, but no image-job or offline configuration options.");
        if (parsed.StartupManagement && (parsed.ConfigureOnly || parsed.ShowConfiguration || parsed.DirectSettings.Count != 0))
            throw new ArgumentException("Startup management cannot be combined with image jobs or offline configuration/inspection.");
        if (parsed.PersistentSettings.ContainsKey(ManagedConfigurationStartup.Key) && !parsed.StartupManagement)
            throw new ArgumentException("Use --startup or --no-startup to change Startup; --configure-only does not perform Startup operations.");
        if (parsed.ConfigureOnly && parsed.DirectSettings.Count != 0)
            throw new ArgumentException("--configure-only accepts persistent --set values, not one-run options.");
        if (parsed.PersistentSettings.Count != 0 && !parsed.ConfigureOnly && !parsed.StartupManagement)
            throw new ArgumentException("--set requires --configure-only so a configuration change cannot also create a collage.");
        return parsed;
    }

    static ManagedConfigurationTraySpec BuildConfigurationTraySpec(string iniPath)
    {
        var spec = new ManagedConfigurationTraySpec
        {
            ProductName = "PhotoCollage",
            Version = ProductVersion(),
            IniPath = iniPath,
            ReadSettings = () => ManagedConfigurationTray.ReadForEditing(DefaultSettingValues(), BuildIniFileSpec(iniPath))
        };
        spec.Startup = new ManagedConfigurationStartup(BuildIniFileSpec(iniPath),
            ManagedConfigurationStartup.BuildShortcut(spec.ProductName, Assembly.GetExecutingAssembly().Location, iniPath),
            spec.ReadSettings, changes => ApplyTraySettings(iniPath, changes, true),
            changes => ApplyTraySettings(iniPath, changes, false));
        spec.SaveSettings = spec.Startup.Save;
        return spec;
    }

    static void ApplyTraySettings(string iniPath, IDictionary<string, string> changes, bool persist)
    {
        var command = new ParsedCommand { IniPath = iniPath, ConfigureOnly = persist };
        foreach (var entry in changes) SetPersistentSetting(command, entry.Key + "=" + entry.Value);
        BuildOptions(command);
    }

    static void SetDirectSetting(ParsedCommand parsed, string key, string value)
    {
        parsed.DirectSettings[key] = NormalizeSettingValue(key, value);
    }

    static void SetPersistentSetting(ParsedCommand parsed, string assignment)
    {
        ManagedProfile.SetAssignment(parsed.PersistentSettings, assignment,
            SettingsSection, CanonicalSettingName, NormalizeSettingValue);
    }

    static Options BuildOptions(ParsedCommand command)
    {
        string iniPath = ResolveIniPath(command.IniPath);
        ManagedIniFileSpec iniFile = BuildIniFileSpec(iniPath);
        var overrides = new Dictionary<string, string>(command.PersistentSettings, StringComparer.OrdinalIgnoreCase);
        foreach (var setting in command.DirectSettings) overrides[setting.Key] = setting.Value;
        var values = ManagedProfile.ResolveSettings(DefaultSettingValues(),
            ManagedIniFile.LoadSection(iniFile, false), overrides, CanonicalSettingName, NormalizeSettingValue);
        Options options = OptionsFromSettings(values, Path.GetDirectoryName(iniPath));
        options.IniPath = iniPath;
        ApplyDirectSettings(options, command.DirectSettings);
        ValidateConfiguration(options);
        if (command.ConfigureOnly)
        {
            string error;
            if (!ManagedIniFile.SaveSectionBatch(iniFile, command.PersistentSettings, prospective =>
            {
                var fresh = ManagedProfile.ResolveSettings(DefaultSettingValues(), prospective,
                    new Dictionary<string, string>(), CanonicalSettingName, NormalizeSettingValue);
                var validated = OptionsFromSettings(fresh, Path.GetDirectoryName(iniPath));
                validated.IniPath = iniPath;
                ValidateConfiguration(validated);
                options = validated;
            }, out error))
                throw new IOException("Could not save PhotoCollage configuration: " + error);
        }
        return options;
    }

    static Dictionary<string, string> DefaultSettingValues()
    {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { ManagedConfigurationStartup.Key, "0" },
            { InputFolderKey, "" },
            { OutputFileKey, "" },
            { ColsKey, "5" },
            { MaxImagesKey, "25" },
            { JpegQualityKey, "80" },
            { MaxCanvasMegapixelsKey, "100" },
            { LogFileKey, DefaultLogFileName }
        };
    }

    static Options OptionsFromSettings(Dictionary<string, string> values, string profileDirectory)
    {
        return new Options
        {
            RunAtStartup = values[ManagedConfigurationStartup.Key] == "1",
            InputFolder = ResolveProfilePath(profileDirectory, values[InputFolderKey]),
            OutputFile = ResolveProfilePath(profileDirectory, values[OutputFileKey]),
            Cols = ParseIntInRange(values[ColsKey], ColsKey, 1, 1000),
            MaxImages = ParseIntInRange(values[MaxImagesKey], MaxImagesKey, 1, 10000),
            JpegQuality = ParseLongInRange(values[JpegQualityKey], JpegQualityKey, 1, 100),
            MaxCanvasMegapixels = ParseLongInRange(
                values[MaxCanvasMegapixelsKey],
                MaxCanvasMegapixelsKey,
                1,
                1024),
            LogFile = ResolveProfilePath(
                profileDirectory,
                String.IsNullOrWhiteSpace(values[LogFileKey])
                    ? DefaultLogFileName
                    : values[LogFileKey])
        };
    }

    static void ApplyDirectSettings(Options options, Dictionary<string, string> values)
    {
        string value;
        if (values.TryGetValue(InputFolderKey, out value))
            options.InputFolder = ResolveWorkingDirectoryPath(value);
        if (values.TryGetValue(OutputFileKey, out value))
            options.OutputFile = ResolveWorkingDirectoryPath(value);
        if (values.TryGetValue(ColsKey, out value))
            options.Cols = ParseIntInRange(value, ColsKey, 1, 1000);
        if (values.TryGetValue(MaxImagesKey, out value))
            options.MaxImages = ParseIntInRange(value, MaxImagesKey, 1, 10000);
        if (values.TryGetValue(JpegQualityKey, out value))
            options.JpegQuality = ParseLongInRange(value, JpegQualityKey, 1, 100);
        if (values.TryGetValue(MaxCanvasMegapixelsKey, out value))
            options.MaxCanvasMegapixels = ParseLongInRange(value, MaxCanvasMegapixelsKey, 1, 1024);
        if (values.TryGetValue(LogFileKey, out value))
            options.LogFile = ResolveExecutableDirectoryPath(
                String.IsNullOrWhiteSpace(value) ? DefaultLogFileName : value);
    }

    static void ValidateConfiguration(Options options)
    {
        if (!String.IsNullOrEmpty(options.OutputFile))
            ValidateOutputExtension(options.OutputFile);
        if (String.IsNullOrEmpty(options.LogFile))
            throw new InvalidDataException("LogFile must not be empty.");
        if (String.Equals(options.OutputFile, options.LogFile, StringComparison.OrdinalIgnoreCase) ||
            String.Equals(options.LogFile, options.IniPath, StringComparison.OrdinalIgnoreCase) ||
            (!String.IsNullOrEmpty(options.OutputFile) &&
             String.Equals(options.OutputFile, options.IniPath, StringComparison.OrdinalIgnoreCase)))
            throw new InvalidDataException("The output image, log, and INI must use different file paths.");
    }

    static void PrintConfiguration(Options options)
    {
        Console.WriteLine("Configuration: " + options.IniPath);
        Console.WriteLine("RunAtStartup = " + (options.RunAtStartup ? "1" : "0"));
        Console.WriteLine("InputFolder = " + (options.InputFolder ?? ""));
        Console.WriteLine("OutputFile = " + (options.OutputFile ?? ""));
        Console.WriteLine("Cols = " + options.Cols.ToString(CultureInfo.InvariantCulture));
        Console.WriteLine("MaxImages = " + options.MaxImages.ToString(CultureInfo.InvariantCulture));
        Console.WriteLine("JpegQuality = " + options.JpegQuality.ToString(CultureInfo.InvariantCulture));
        Console.WriteLine("MaxCanvasMegapixels = " + options.MaxCanvasMegapixels.ToString(CultureInfo.InvariantCulture));
        Console.WriteLine("LogFile = " + options.LogFile);
    }

    static ManagedIniFileSpec BuildIniFileSpec(string iniPath)
    {
        return new ManagedIniFileSpec
        {
            FilePath = iniPath,
            SectionName = SettingsSection,
            DefaultContents =
                "[Settings]" + Environment.NewLine +
                "RunAtStartup=0" + Environment.NewLine +
                "; Persistent defaults. Command-line image options override these for one run." + Environment.NewLine +
                InputFolderKey + "=" + Environment.NewLine +
                OutputFileKey + "=" + Environment.NewLine +
                ColsKey + "=5" + Environment.NewLine +
                MaxImagesKey + "=25" + Environment.NewLine +
                JpegQualityKey + "=80" + Environment.NewLine +
                MaxCanvasMegapixelsKey + "=100" + Environment.NewLine +
                LogFileKey + "=" + DefaultLogFileName + Environment.NewLine,
            Log = delegate(string message)
            {
                Console.Error.WriteLine("PhotoCollage configuration: " + message);
            }
        };
    }

    static string ResolveIniPath(string value)
    {
        return ManagedProfile.ResolveIniPath(value, AppDomain.CurrentDomain.BaseDirectory, "PhotoCollage.ini");
    }

    static string ResolveProfilePath(string profileDirectory, string value)
    {
        return ManagedProfile.ResolvePath(profileDirectory, value);
    }

    static string ResolveWorkingDirectoryPath(string value)
    {
        return ManagedProfile.ResolvePath(Directory.GetCurrentDirectory(), value);
    }

    static string ResolveExecutableDirectoryPath(string value)
    {
        return ManagedProfile.ResolvePath(AppDomain.CurrentDomain.BaseDirectory, value);
    }

    static string CanonicalSettingName(string value)
    {
        if (String.IsNullOrWhiteSpace(value))
            return null;
        string key = value.Trim();
        if (key.Equals(ManagedConfigurationStartup.Key, StringComparison.OrdinalIgnoreCase)) return ManagedConfigurationStartup.Key;
        if (key.Equals(InputFolderKey, StringComparison.OrdinalIgnoreCase)) return InputFolderKey;
        if (key.Equals(OutputFileKey, StringComparison.OrdinalIgnoreCase)) return OutputFileKey;
        if (key.Equals(ColsKey, StringComparison.OrdinalIgnoreCase)) return ColsKey;
        if (key.Equals(MaxImagesKey, StringComparison.OrdinalIgnoreCase)) return MaxImagesKey;
        if (key.Equals(JpegQualityKey, StringComparison.OrdinalIgnoreCase)) return JpegQualityKey;
        if (key.Equals(MaxCanvasMegapixelsKey, StringComparison.OrdinalIgnoreCase)) return MaxCanvasMegapixelsKey;
        if (key.Equals(LogFileKey, StringComparison.OrdinalIgnoreCase)) return LogFileKey;
        return null;
    }

    static string NormalizeSettingValue(string key, string value)
    {
        if (key.Equals(ManagedConfigurationStartup.Key, StringComparison.OrdinalIgnoreCase)) return ManagedConfigurationStartup.NormalizeValue(value);
        if (value == null || value.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0)
            throw new ArgumentException("The setting value is invalid.");
        if (key.Equals(InputFolderKey, StringComparison.OrdinalIgnoreCase) ||
            key.Equals(OutputFileKey, StringComparison.OrdinalIgnoreCase))
        {
            if (String.IsNullOrWhiteSpace(value))
                return "";
            ValidatePath(value, key);
            return value;
        }
        if (key.Equals(LogFileKey, StringComparison.OrdinalIgnoreCase))
        {
            if (String.IsNullOrWhiteSpace(value))
                return "";
            ValidatePath(value, key);
            return value;
        }
        if (key.Equals(ColsKey, StringComparison.OrdinalIgnoreCase))
            return ParseIntInRange(value, key, 1, 1000).ToString(CultureInfo.InvariantCulture);
        if (key.Equals(MaxImagesKey, StringComparison.OrdinalIgnoreCase))
            return ParseIntInRange(value, key, 1, 10000).ToString(CultureInfo.InvariantCulture);
        if (key.Equals(JpegQualityKey, StringComparison.OrdinalIgnoreCase))
            return ParseLongInRange(value, key, 1, 100).ToString(CultureInfo.InvariantCulture);
        if (key.Equals(MaxCanvasMegapixelsKey, StringComparison.OrdinalIgnoreCase))
            return ParseLongInRange(value, key, 1, 1024).ToString(CultureInfo.InvariantCulture);
        throw new ArgumentException("Unknown setting: " + key);
    }

    static void ValidatePath(string value, string settingName)
    {
        ManagedProfile.ValidatePath(value, settingName);
    }

    static void Usage()
    {
        Console.WriteLine("Usage:");
        Console.WriteLine("  PhotoCollage.exe [--ini PATH] -InputFolder C:\\Photos -OutputFile C:\\Photos\\collage.jpg [options]");
        Console.WriteLine();
        Console.WriteLine("One-run options (they override the profile without changing it):");
        Console.WriteLine("  -Cols N                  columns (default 5)");
        Console.WriteLine("  -MaxImages N             maximum readable images (default 25)");
        Console.WriteLine("  -JpegQuality N           JPEG quality from 1 through 100");
        Console.WriteLine("  -MaxCanvasMegapixels N   canvas safety limit (default 100)");
        Console.WriteLine("  -LogFile PATH            log path relative to the executable by default");
        Console.WriteLine();
        Console.WriteLine("Persistent profile:");
        Console.WriteLine("  --ini PATH               profile path (default: PhotoCollage.ini beside the executable)");
        Console.WriteLine("  --set Settings.Key=Value persist InputFolder, OutputFile, Cols, MaxImages,");
        Console.WriteLine("                           JpegQuality, MaxCanvasMegapixels, or LogFile");
        Console.WriteLine("  --configure-only         validate/create the profile and save --set values without creating a collage");
        Console.WriteLine("  --tray                   open a configuration tray; no image job runs (optionally use --ini)");
        Console.WriteLine("  --startup | --no-startup  save Startup-folder preference; sign-in opens --tray, never an image job");
        Console.WriteLine("  --show-config            print effective profile values without creating a collage");
        Console.WriteLine("  --help                   show help without side effects");
        Console.WriteLine("  --version                show version without side effects");
    }

    static void Run(Options o)
    {
        string ext = ValidateOutputExtension(o.OutputFile);
        if (!Directory.Exists(o.InputFolder))
            throw new DirectoryNotFoundException(o.InputFolder);

        string outputPath = Path.GetFullPath(o.OutputFile);
        List<string> candidates = DiscoverImageFiles(o, outputPath);
        int photoW;
        int photoH;
        List<string> files = SelectReadableImages(
            o,
            candidates,
            out photoW,
            out photoH);
        if (files.Count == 0)
            throw new InvalidOperationException("No readable supported images found in " + o.InputFolder);

        int rows = (files.Count + o.Cols - 1) / o.Cols;
        Log(o, "Detected photo size: " + photoW + "x" + photoH + ", grid: " + o.Cols + "x" + rows);

        long canvasWidth;
        long canvasHeight;
        long pixels;
        try
        {
            checked
            {
                canvasWidth = (long)o.Cols * photoW;
                canvasHeight = (long)rows * photoH;
                pixels = canvasWidth * canvasHeight;
            }
        }
        catch (OverflowException)
        {
            throw new InvalidOperationException("Canvas dimensions overflowed the supported size.");
        }
        long maxPixels = o.MaxCanvasMegapixels * 1000000L;
        if (canvasWidth <= 0 || canvasHeight <= 0 || pixels <= 0 ||
            canvasWidth > Int32.MaxValue || canvasHeight > Int32.MaxValue || pixels > maxPixels)
            throw new InvalidOperationException("Canvas would be " + canvasWidth + "x" + canvasHeight + " (" + pixels + " pixels), above the limit of " + o.MaxCanvasMegapixels + " megapixels.");

        using (var canvas = new Bitmap((int)canvasWidth, (int)canvasHeight))
        using (var g = Graphics.FromImage(canvas))
        {
            g.Clear(Color.White);
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            int rendered = 0;
            for (int i = 0; i < files.Count; ++i)
            {
                try
                {
                    using (var img = Image.FromFile(files[i]))
                    {
                        int x = (rendered % o.Cols) * photoW;
                        int y = (rendered / o.Cols) * photoH;
                        g.DrawImage(img, x, y, photoW, photoH);
                    }
                    rendered++;
                    Log(o, "[" + rendered + "/" + files.Count + "] " + Path.GetFileName(files[i]));
                }
                catch (Exception ex)
                {
                    if (!IsRecoverableImageException(ex))
                        throw;
                    Log(o, "WARNING: Skipped image that became unreadable during rendering: " + files[i] + " (" + ex.Message + ")");
                }
            }

            if (rendered == 0)
                throw new InvalidOperationException("Every selected image became unreadable before rendering.");

            string outDir = Path.GetDirectoryName(Path.GetFullPath(o.OutputFile));
            if (!String.IsNullOrEmpty(outDir))
                Directory.CreateDirectory(outDir);

            SaveCanvasAtomically(canvas, outputPath, ext, o.JpegQuality);
        }

        Log(o, "Done! Saved to " + outputPath);
    }

    static string ValidateOutputExtension(string outputFile)
    {
        string ext = Path.GetExtension(outputFile).ToLowerInvariant();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp")
            throw new InvalidOperationException("Unsupported output extension '" + ext + "'. Use .jpg, .jpeg, .png, or .bmp.");
        return ext;
    }

    static void SaveCanvasAtomically(Bitmap canvas, string outputPath, string extension, long jpegQuality)
    {
        string directory = Path.GetDirectoryName(outputPath);
        if (String.IsNullOrEmpty(directory))
            throw new InvalidOperationException("Output path does not have a parent directory.");

        string temporaryPath = Path.Combine(
            directory,
            "." + Path.GetFileName(outputPath) + "." + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            if (extension == ".jpg" || extension == ".jpeg")
            {
                ImageCodecInfo encoder = ImageCodecInfo.GetImageEncoders().FirstOrDefault(e => e.MimeType == "image/jpeg");
                if (encoder == null)
                    throw new InvalidOperationException("JPEG encoder was not found.");
                using (var encParams = new EncoderParameters(1))
                {
                    encParams.Param[0] = new EncoderParameter(Encoder.Quality, jpegQuality);
                    canvas.Save(temporaryPath, encoder, encParams);
                }
            }
            else if (extension == ".png")
            {
                canvas.Save(temporaryPath, ImageFormat.Png);
            }
            else
            {
                canvas.Save(temporaryPath, ImageFormat.Bmp);
            }

            if (File.Exists(outputPath))
                File.Replace(temporaryPath, outputPath, null);
            else
                File.Move(temporaryPath, outputPath);
        }
        finally
        {
            if (File.Exists(temporaryPath))
                File.Delete(temporaryPath);
        }
    }

    static bool IsSupportedImage(string path)
    {
        string ext = Path.GetExtension(path).ToLowerInvariant();
        return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
    }

    static List<string> DiscoverImageFiles(Options o, string outputPath)
    {
        var results = new List<string>();
        var pending = new Stack<string>();
        pending.Push(Path.GetFullPath(o.InputFolder));

        while (pending.Count != 0)
        {
            string directory = pending.Pop();
            string[] files = new string[0];
            try
            {
                files = Directory.GetFiles(directory, "*", SearchOption.TopDirectoryOnly);
            }
            catch (Exception ex)
            {
                if (!IsRecoverableEnumerationException(ex))
                    throw;
                Log(o, "WARNING: Skipped files in inaccessible directory: " + directory + " (" + ex.Message + ")");
            }

            foreach (string file in files)
            {
                if (!IsSupportedImage(file))
                    continue;
                try
                {
                    string fullPath = Path.GetFullPath(file);
                    if (!fullPath.Equals(outputPath, StringComparison.OrdinalIgnoreCase))
                        results.Add(fullPath);
                }
                catch (Exception ex)
                {
                    if (!IsRecoverableEnumerationException(ex) && !(ex is ArgumentException))
                        throw;
                    Log(o, "WARNING: Skipped invalid image path: " + file + " (" + ex.Message + ")");
                }
            }

            string[] directories = new string[0];
            try
            {
                directories = Directory.GetDirectories(
                    directory,
                    "*",
                    SearchOption.TopDirectoryOnly);
            }
            catch (Exception ex)
            {
                if (!IsRecoverableEnumerationException(ex))
                    throw;
                Log(o, "WARNING: Could not enumerate subdirectories of: " + directory + " (" + ex.Message + ")");
            }

            foreach (string child in directories)
            {
                try
                {
                    if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0)
                    {
                        Log(o, "WARNING: Skipped reparse-point directory: " + child);
                        continue;
                    }
                    pending.Push(child);
                }
                catch (Exception ex)
                {
                    if (!IsRecoverableEnumerationException(ex))
                        throw;
                    Log(o, "WARNING: Skipped inaccessible subdirectory: " + child + " (" + ex.Message + ")");
                }
            }
        }

        results.Sort(StringComparer.OrdinalIgnoreCase);
        return results;
    }

    static List<string> SelectReadableImages(
        Options o,
        IEnumerable<string> candidates,
        out int photoWidth,
        out int photoHeight)
    {
        var selected = new List<string>();
        photoWidth = 0;
        photoHeight = 0;
        foreach (string path in candidates)
        {
            if (selected.Count >= o.MaxImages)
                break;
            try
            {
                using (Image probe = Image.FromFile(path))
                {
                    if (probe.Width <= 0 || probe.Height <= 0)
                        throw new InvalidOperationException("The decoded image has invalid dimensions.");
                    if (selected.Count == 0)
                    {
                        photoWidth = probe.Width;
                        photoHeight = probe.Height;
                    }
                }
                selected.Add(path);
            }
            catch (Exception ex)
            {
                if (!IsRecoverableImageException(ex) && !(ex is InvalidOperationException))
                    throw;
                Log(o, "WARNING: Skipped unreadable image: " + path + " (" + ex.Message + ")");
            }
        }
        return selected;
    }

    static bool IsRecoverableEnumerationException(Exception ex)
    {
        return ex is IOException ||
            ex is UnauthorizedAccessException ||
            ex is SecurityException ||
            ex is NotSupportedException;
    }

    static bool IsRecoverableImageException(Exception ex)
    {
        return ex is ArgumentException ||
            ex is ExternalException ||
            ex is IOException ||
            ex is UnauthorizedAccessException ||
            ex is OutOfMemoryException;
    }

    static void Log(Options o, string message)
    {
        Console.WriteLine(message);
        string error;
        if (!ManagedLogFile.AppendLine(
            new ManagedLogFileSpec
            {
                FilePath = o.LogFile,
                LockWaitMilliseconds = 5000,
                WriteUtf8Bom = true
            },
            DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + message,
            out error))
        {
            Console.Error.WriteLine(
                "WARNING: Could not write PhotoCollage log '" + o.LogFile +
                "': " + error);
        }
    }

    static bool Is(string a, string b) { return String.Equals(a, b, StringComparison.OrdinalIgnoreCase); }
    static int ParseInt(string value, string name) { int n; if (!Int32.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out n)) throw new ArgumentException("Invalid integer for " + name + ": " + value); return n; }
    static long ParseLong(string value, string name) { long n; if (!Int64.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out n)) throw new ArgumentException("Invalid integer for " + name + ": " + value); return n; }
    static int ParseIntInRange(string value, string name, int min, int max)
    {
        int parsed = ParseInt(value, name);
        if (parsed < min || parsed > max)
            throw new ArgumentOutOfRangeException(name, value, "Value must be between " + min + " and " + max + ".");
        return parsed;
    }
    static long ParseLongInRange(string value, string name, long min, long max)
    {
        long parsed = ParseLong(value, name);
        if (parsed < min || parsed > max)
            throw new ArgumentOutOfRangeException(name, value, "Value must be between " + min + " and " + max + ".");
        return parsed;
    }
    static string RequireValue(string[] args, ref int index, string option)
    {
        if (index + 1 >= args.Length || IsKnownOption(args[index + 1]))
            throw new ArgumentException("Missing value for " + option + ".");
        return args[++index];
    }
    static bool IsKnownOption(string value)
    {
        string[] options =
        {
            "-h", "--help", "/?", "-v", "--version", "-Version",
            "-InputFolder", "--input-folder",
            "-OutputFile", "--output-file", "-Cols", "--cols",
            "-MaxImages", "--max-images", "-JpegQuality", "--jpeg-quality",
            "-MaxCanvasMegapixels", "--max-canvas-megapixels", "-LogFile", "--log-file",
            "--ini", "-IniFile", "--set", "--configure-only", "--show-config", "--print-config", "--tray", "--startup", "--no-startup"
        };
        return options.Any(option => Is(value, option));
    }

    static bool IsHelpOption(string value)
    {
        return Is(value, "-h") || Is(value, "--help") || Is(value, "/?");
    }

    static bool IsVersionOption(string value)
    {
        return Is(value, "-v") || Is(value, "--version") || Is(value, "-Version");
    }

    static string ProductVersion()
    {
        Version version = Assembly.GetExecutingAssembly().GetName().Version;
        return version == null ? "1.0.0.0" : version.ToString();
    }
}

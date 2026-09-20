using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security;
using AIProjects.Dependencies;

static class PhotoCollage
{
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
            var options = ParseArgs(args);
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
        public bool Help;
        public bool Version;
    }

    static Options ParseArgs(string[] args)
    {
        var o = new Options();
        for (int i = 0; i < args.Length; ++i)
        {
            string a = args[i];
            if (IsHelpOption(a))
            {
                o.Help = true;
                continue;
            }
            if (IsVersionOption(a))
            {
                o.Version = true;
                continue;
            }

            if (Is(a, "-InputFolder") || Is(a, "--input-folder")) o.InputFolder = RequireValue(args, ref i, a);
            else if (Is(a, "-OutputFile") || Is(a, "--output-file")) o.OutputFile = RequireValue(args, ref i, a);
            else if (Is(a, "-Cols") || Is(a, "--cols")) o.Cols = ParseIntInRange(RequireValue(args, ref i, a), a, 1, 1000);
            else if (Is(a, "-MaxImages") || Is(a, "--max-images")) o.MaxImages = ParseIntInRange(RequireValue(args, ref i, a), a, 1, 10000);
            else if (Is(a, "-JpegQuality") || Is(a, "--jpeg-quality")) o.JpegQuality = ParseLongInRange(RequireValue(args, ref i, a), a, 1, 100);
            else if (Is(a, "-MaxCanvasMegapixels") || Is(a, "--max-canvas-megapixels")) o.MaxCanvasMegapixels = ParseLongInRange(RequireValue(args, ref i, a), a, 1, 1024);
            else if (Is(a, "-LogFile") || Is(a, "--log-file")) o.LogFile = RequireValue(args, ref i, a);
            else throw new ArgumentException("Unknown argument: " + a);
        }

        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        if (String.IsNullOrWhiteSpace(o.LogFile))
            o.LogFile = Path.Combine(baseDir, "PhotoCollage.log");
        else if (!Path.IsPathRooted(o.LogFile))
            o.LogFile = Path.Combine(baseDir, o.LogFile);
        return o;
    }

    static void Usage()
    {
        Console.WriteLine("Usage:");
        Console.WriteLine("  PhotoCollage.exe -InputFolder C:\\Photos -OutputFile C:\\Photos\\collage.jpg [options]");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  -Cols N                  columns (default 5)");
        Console.WriteLine("  -MaxImages N             maximum readable images (default 25)");
        Console.WriteLine("  -JpegQuality N           JPEG quality from 1 through 100");
        Console.WriteLine("  -MaxCanvasMegapixels N   canvas safety limit (default 100)");
        Console.WriteLine("  -LogFile PATH            log path relative to the executable by default");
        Console.WriteLine("  --help                   show help without side effects");
        Console.WriteLine("  --version                show version without side effects");
    }

    static void Run(Options o)
    {
        string ext = Path.GetExtension(o.OutputFile).ToLowerInvariant();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp")
            throw new InvalidOperationException("Unsupported output extension '" + ext + "'. Use .jpg, .jpeg, .png, or .bmp.");
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
    static int ParseInt(string value, string name) { int n; if (!Int32.TryParse(value, out n)) throw new ArgumentException("Invalid integer for " + name + ": " + value); return n; }
    static long ParseLong(string value, string name) { long n; if (!Int64.TryParse(value, out n)) throw new ArgumentException("Invalid integer for " + name + ": " + value); return n; }
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
            "-MaxCanvasMegapixels", "--max-canvas-megapixels", "-LogFile", "--log-file"
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

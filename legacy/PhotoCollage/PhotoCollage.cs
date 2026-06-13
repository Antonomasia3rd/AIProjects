using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;

static class PhotoCollage
{
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
    }

    static Options ParseArgs(string[] args)
    {
        var o = new Options();
        for (int i = 0; i < args.Length; ++i)
        {
            string a = args[i];
            if (Is(a, "-h") || Is(a, "--help") || Is(a, "/?"))
            {
                o.Help = true;
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
        Console.WriteLine("  PhotoCollage.exe -InputFolder C:\\Photos -OutputFile C:\\Photos\\collage.jpg [-Cols 5] [-MaxImages 25] [-JpegQuality 80]");
    }

    static void Run(Options o)
    {
        string ext = Path.GetExtension(o.OutputFile).ToLowerInvariant();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp")
            throw new InvalidOperationException("Unsupported output extension '" + ext + "'. Use .jpg, .jpeg, .png, or .bmp.");
        if (!Directory.Exists(o.InputFolder))
            throw new DirectoryNotFoundException(o.InputFolder);

        string outputPath = Path.GetFullPath(o.OutputFile);
        var files = Directory.EnumerateFiles(o.InputFolder, "*", SearchOption.AllDirectories)
            .Where(IsSupportedImage)
            .Where(path => !Path.GetFullPath(path).Equals(outputPath, StringComparison.OrdinalIgnoreCase))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .Take(o.MaxImages)
            .ToList();
        if (files.Count == 0)
            throw new InvalidOperationException("No supported images found in " + o.InputFolder);

        int photoW;
        int photoH;
        using (var probe = Image.FromFile(files[0]))
        {
            photoW = probe.Width;
            photoH = probe.Height;
        }

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
            for (int i = 0; i < files.Count; ++i)
            {
                using (var img = Image.FromFile(files[i]))
                {
                    int x = (i % o.Cols) * photoW;
                    int y = (i / o.Cols) * photoH;
                    g.DrawImage(img, x, y, photoW, photoH);
                }
                Log(o, "[" + (i + 1) + "/" + files.Count + "] " + Path.GetFileName(files[i]));
            }

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

    static void Log(Options o, string message)
    {
        Console.WriteLine(message);
        try
        {
            string dir = Path.GetDirectoryName(o.LogFile);
            if (!String.IsNullOrWhiteSpace(dir))
                Directory.CreateDirectory(dir);
            File.AppendAllText(o.LogFile, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + message + Environment.NewLine);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("WARNING: Could not write PhotoCollage log '" + o.LogFile + "': " + ex.Message);
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
            "-h", "--help", "/?", "-InputFolder", "--input-folder",
            "-OutputFile", "--output-file", "-Cols", "--cols",
            "-MaxImages", "--max-images", "-JpegQuality", "--jpeg-quality",
            "-MaxCanvasMegapixels", "--max-canvas-megapixels", "-LogFile", "--log-file"
        };
        return options.Any(option => Is(value, option));
    }
}

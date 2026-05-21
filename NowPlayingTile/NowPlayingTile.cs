using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Windows.Data.Xml.Dom;
using Windows.Foundation;
using Windows.Media.Control;
using Windows.Storage.Streams;
using Windows.UI.Notifications;

internal static class Program
{
    private static Mutex singleInstance;
    private static bool ownsSingleInstance;

    [STAThread]
    private static void Main(string[] args)
    {
        var options = AppOptions.Parse(args);
        if (!options.AllowMultiple)
        {
            bool createdNew;
            singleInstance = new Mutex(true, "Local\\NowPlayingTile.App", out createdNew);
            ownsSingleInstance = createdNew;
            if (!createdNew)
            {
                singleInstance.Dispose();
                singleInstance = null;
                return;
            }
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        try
        {
            if (options.UpdateOnce)
            {
                UpdateOnce();
                return;
            }

            var settings = AppSettings.Load();
            if (options.ShowWidget)
            {
                Application.Run(new TileForm(settings));
            }
            else
            {
                Application.Run(new BackgroundTileContext(settings));
            }
        }
        finally
        {
            if (singleInstance != null)
            {
                if (ownsSingleInstance)
                {
                    singleInstance.ReleaseMutex();
                }

                singleInstance.Dispose();
                singleInstance = null;
                ownsSingleInstance = false;
            }
        }
    }

    private static void UpdateOnce()
    {
        var settings = AppSettings.Load();
        var snapshot = MediaSnapshot.Empty("Starting...");
        try
        {
            snapshot = MediaReader.Read();
            LiveTileUpdater.TryUpdate(snapshot, settings);
        }
        catch
        {
        }
        finally
        {
            if (snapshot.Artwork != null)
            {
                snapshot.Artwork.Dispose();
            }
        }
    }
}

internal sealed class AppOptions
{
    public readonly bool ShowWidget;
    public readonly bool UpdateOnce;
    public readonly bool AllowMultiple;

    private AppOptions(bool showWidget, bool updateOnce, bool allowMultiple)
    {
        ShowWidget = showWidget;
        UpdateOnce = updateOnce;
        AllowMultiple = allowMultiple;
    }

    public static AppOptions Parse(string[] args)
    {
        bool showWidget = false;
        bool updateOnce = false;
        bool allowMultiple = false;

        foreach (var arg in args ?? new string[0])
        {
            if (EqualsAny(arg, "--widget", "--show", "/widget", "/show"))
            {
                showWidget = true;
            }
            else if (EqualsAny(arg, "--once", "/once"))
            {
                updateOnce = true;
            }
            else if (EqualsAny(arg, "--allow-multiple", "/allow-multiple"))
            {
                allowMultiple = true;
            }
        }

        return new AppOptions(showWidget, updateOnce, allowMultiple);
    }

    private static bool EqualsAny(string value, params string[] options)
    {
        foreach (var option in options)
        {
            if (string.Equals(value, option, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }
}

internal enum TileLayout
{
    Cycle,
    Text,
    Artwork,
    Combined
}

internal sealed class AppSettings
{
    public static readonly string DataDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NowPlayingTile");

    public static readonly string SettingsPath = Path.Combine(DataDirectory, "settings.ini");

    public readonly int UpdateIntervalSeconds;
    public readonly int TileRefreshSeconds;
    public readonly TileLayout TileLayout;
    public readonly bool ShowTrayIcon;

    private AppSettings(int updateIntervalSeconds, int tileRefreshSeconds, TileLayout tileLayout, bool showTrayIcon)
    {
        UpdateIntervalSeconds = updateIntervalSeconds;
        TileRefreshSeconds = tileRefreshSeconds;
        TileLayout = tileLayout;
        ShowTrayIcon = showTrayIcon;
    }

    public static AppSettings Load()
    {
        EnsureSettingsFile();
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (File.Exists(SettingsPath))
        {
            try
            {
                foreach (var rawLine in File.ReadAllLines(SettingsPath))
                {
                    var line = rawLine.Trim();
                    if (line.Length == 0 || line.StartsWith("#"))
                    {
                        continue;
                    }

                    var separator = line.IndexOf('=');
                    if (separator <= 0)
                    {
                        continue;
                    }

                    values[line.Substring(0, separator).Trim()] = line.Substring(separator + 1).Trim();
                }
            }
            catch
            {
            }
        }

        return new AppSettings(
            ReadInt(values, "UpdateIntervalSeconds", 2, 1, 60),
            ReadInt(values, "TileRefreshSeconds", 60, 1, 300),
            ReadLayout(values, "TileLayout", TileLayout.Cycle),
            ReadBool(values, "ShowTrayIcon", false));
    }

    private static void EnsureSettingsFile()
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            if (File.Exists(SettingsPath))
            {
                return;
            }

            File.WriteAllText(SettingsPath,
                "# NowPlayingTile settings\r\n" +
                "# TileLayout: Cycle, Text, Artwork, Combined\r\n" +
                "TileLayout=Cycle\r\n" +
                "UpdateIntervalSeconds=2\r\n" +
                "TileRefreshSeconds=60\r\n" +
                "ShowTrayIcon=false\r\n");
        }
        catch
        {
        }
    }

    private static int ReadInt(Dictionary<string, string> values, string key, int fallback, int min, int max)
    {
        string value;
        int parsed;
        if (!values.TryGetValue(key, out value) || !int.TryParse(value, out parsed))
        {
            return fallback;
        }

        if (parsed < min)
        {
            return min;
        }

        if (parsed > max)
        {
            return max;
        }

        return parsed;
    }

    private static bool ReadBool(Dictionary<string, string> values, string key, bool fallback)
    {
        string value;
        bool parsed;
        if (!values.TryGetValue(key, out value) || !bool.TryParse(value, out parsed))
        {
            return fallback;
        }

        return parsed;
    }

    private static TileLayout ReadLayout(Dictionary<string, string> values, string key, TileLayout fallback)
    {
        string value;
        TileLayout parsed;
        if (!values.TryGetValue(key, out value) || !Enum.TryParse(value, true, out parsed))
        {
            return fallback;
        }

        return parsed;
    }
}

internal sealed class BackgroundTileContext : ApplicationContext
{
    private readonly System.Windows.Forms.Timer timer;
    private NotifyIcon trayIcon;
    private ContextMenuStrip trayMenu;
    private bool updateRunning;
    private string lastTileKey = string.Empty;
    private DateTime lastTileUpdateUtc = DateTime.MinValue;
    private AppSettings settings;

    public BackgroundTileContext(AppSettings settings)
    {
        this.settings = settings;
        if (settings.ShowTrayIcon)
        {
            CreateTrayIcon();
        }

        timer = new System.Windows.Forms.Timer();
        timer.Interval = Math.Max(1, settings.UpdateIntervalSeconds) * 1000;
        timer.Tick += delegate { BeginUpdate(); };
        timer.Start();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            timer.Dispose();
            if (trayIcon != null)
            {
                trayIcon.Visible = false;
                trayIcon.Dispose();
            }

            if (trayMenu != null)
            {
                trayMenu.Dispose();
            }
        }

        base.Dispose(disposing);
    }

    private void CreateTrayIcon()
    {
        trayMenu = new ContextMenuStrip();
        trayMenu.Items.Add("Refresh now", null, delegate { BeginUpdate(); });
        trayMenu.Items.Add("Open widget", null, delegate { new TileForm(AppSettings.Load()).Show(); });
        trayMenu.Items.Add(new ToolStripSeparator());
        trayMenu.Items.Add("Exit", null, delegate { ExitThread(); });

        trayIcon = new NotifyIcon();
        trayIcon.Icon = SystemIcons.Application;
        trayIcon.Text = "Now Playing Tile";
        trayIcon.ContextMenuStrip = trayMenu;
        trayIcon.Visible = true;
        trayIcon.DoubleClick += delegate { new TileForm(AppSettings.Load()).Show(); };
    }

    private void BeginUpdate()
    {
        if (updateRunning)
        {
            return;
        }

        settings = AppSettings.Load();
        timer.Interval = Math.Max(1, settings.UpdateIntervalSeconds) * 1000;
        updateRunning = true;

        Func<MediaSnapshot> read = MediaReader.Read;
        Task.Factory.StartNew(read, CancellationToken.None, TaskCreationOptions.None, TaskScheduler.Default)
            .ContinueWith(UpdateFinished, TaskScheduler.FromCurrentSynchronizationContext());
    }

    private void UpdateFinished(Task<MediaSnapshot> task)
    {
        updateRunning = false;

        MediaSnapshot next;
        if (task.IsFaulted)
        {
            var ex = task.Exception == null ? null : task.Exception.GetBaseException();
            next = MediaSnapshot.Empty(ex == null ? "SMTC read failed" : ex.Message);
        }
        else
        {
            next = task.Result;
        }

        UpdateLiveTileIfNeeded(next);
        if (trayIcon != null)
        {
            trayIcon.Text = TrimForTray(next.Title);
        }

        if (next.Artwork != null)
        {
            next.Artwork.Dispose();
        }
    }

    private void UpdateLiveTileIfNeeded(MediaSnapshot next)
    {
        var key = next.Source + "\n" + next.Title + "\n" + next.Artist + "\n" + next.Status + "\n" + settings.TileLayout;
        if (key == lastTileKey && (DateTime.UtcNow - lastTileUpdateUtc).TotalSeconds < settings.TileRefreshSeconds)
        {
            return;
        }

        if (LiveTileUpdater.TryUpdate(next, settings))
        {
            lastTileKey = key;
            lastTileUpdateUtc = DateTime.UtcNow;
        }
    }

    private static string TrimForTray(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return "Now Playing Tile";
        }

        return text.Length > 63 ? text.Substring(0, 60) + "..." : text;
    }
}

internal sealed class TileForm : Form
{
    private readonly System.Windows.Forms.Timer timer;
    private readonly ContextMenuStrip menu;
    private AppSettings settings;
    private bool updateRunning;
    private bool alwaysOnTop = true;
    private bool dragging;
    private Point dragOffset;
    private string lastTileKey = string.Empty;
    private DateTime lastTileUpdateUtc = DateTime.MinValue;
    private MediaSnapshot snapshot = MediaSnapshot.Empty("Starting...");

    public TileForm()
        : this(AppSettings.Load())
    {
    }

    public TileForm(AppSettings settings)
    {
        this.settings = settings;
        Text = "Now Playing Tile";
        ClientSize = new Size(420, 180);
        MinimumSize = new Size(300, 140);
        FormBorderStyle = FormBorderStyle.SizableToolWindow;
        StartPosition = FormStartPosition.CenterScreen;
        TopMost = true;
        DoubleBuffered = true;
        BackColor = Color.FromArgb(18, 18, 18);

        menu = new ContextMenuStrip();
        var topMostItem = (ToolStripMenuItem)menu.Items.Add("Always on top", null, ToggleAlwaysOnTop);
        topMostItem.Checked = true;
        menu.Items.Add("Refresh now", null, delegate { BeginUpdate(); });
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, delegate { Close(); });
        ContextMenuStrip = menu;

        timer = new System.Windows.Forms.Timer();
        timer.Interval = Math.Max(1, settings.UpdateIntervalSeconds) * 1000;
        timer.Tick += delegate { BeginUpdate(); };
        timer.Start();

        MouseDown += TileForm_MouseDown;
        MouseMove += TileForm_MouseMove;
        MouseUp += delegate { dragging = false; };

    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        BeginUpdate();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            timer.Dispose();
            menu.Dispose();
            if (snapshot.Artwork != null)
            {
                snapshot.Artwork.Dispose();
            }
        }

        base.Dispose(disposing);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);

        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;

        using (var background = new LinearGradientBrush(ClientRectangle, Color.FromArgb(28, 31, 36), Color.FromArgb(11, 13, 17), 35f))
        {
            e.Graphics.FillRectangle(background, ClientRectangle);
        }

        var artRect = new Rectangle(18, 18, Math.Min(138, ClientSize.Height - 36), Math.Min(138, ClientSize.Height - 36));
        DrawArtwork(e.Graphics, artRect);

        var textLeft = artRect.Right + 18;
        var textWidth = Math.Max(40, ClientSize.Width - textLeft - 18);
        var top = 20;

        using (var sourceFont = new Font("Segoe UI", 8.5f, FontStyle.Regular))
        using (var titleFont = new Font("Segoe UI", 18f, FontStyle.Bold))
        using (var artistFont = new Font("Segoe UI", 11.5f, FontStyle.Regular))
        using (var statusFont = new Font("Segoe UI", 9f, FontStyle.Regular))
        using (var sourceBrush = new SolidBrush(Color.FromArgb(160, 220, 226, 235)))
        using (var titleBrush = new SolidBrush(Color.White))
        using (var artistBrush = new SolidBrush(Color.FromArgb(210, 226, 232, 241)))
        using (var statusBrush = new SolidBrush(Color.FromArgb(145, 220, 226, 235)))
        {
            DrawTrimmedText(e.Graphics, snapshot.Source, sourceFont, sourceBrush, new RectangleF(textLeft, top, textWidth, 18));
            DrawTrimmedText(e.Graphics, snapshot.Title, titleFont, titleBrush, new RectangleF(textLeft, top + 25, textWidth, 52));
            DrawTrimmedText(e.Graphics, snapshot.Artist, artistFont, artistBrush, new RectangleF(textLeft, top + 84, textWidth, 28));

            var status = snapshot.Status;
            if (!string.IsNullOrEmpty(snapshot.LastUpdated))
            {
                status += "  |  " + snapshot.LastUpdated;
            }

            DrawTrimmedText(e.Graphics, status, statusFont, statusBrush, new RectangleF(textLeft, ClientSize.Height - 34, textWidth, 18));
        }
    }

    private void DrawArtwork(Graphics graphics, Rectangle rect)
    {
        using (var path = RoundedRect(rect, 10))
        {
            graphics.SetClip(path);
            if (snapshot.Artwork != null)
            {
                graphics.DrawImage(snapshot.Artwork, rect);
            }
            else
            {
                using (var brush = new LinearGradientBrush(rect, Color.FromArgb(43, 130, 116), Color.FromArgb(122, 82, 150), 45f))
                {
                    graphics.FillRectangle(brush, rect);
                }

                using (var font = new Font("Segoe UI Symbol", rect.Height * 0.38f, FontStyle.Regular, GraphicsUnit.Pixel))
                using (var brush = new SolidBrush(Color.FromArgb(230, 255, 255, 255)))
                using (var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                {
                    graphics.DrawString("♪", font, brush, rect, format);
                }
            }

            graphics.ResetClip();
        }

        using (var pen = new Pen(Color.FromArgb(70, 255, 255, 255), 1f))
        using (var path = RoundedRect(rect, 10))
        {
            graphics.DrawPath(pen, path);
        }
    }

    private static GraphicsPath RoundedRect(Rectangle rect, int radius)
    {
        int diameter = radius * 2;
        var path = new GraphicsPath();
        path.AddArc(rect.Left, rect.Top, diameter, diameter, 180, 90);
        path.AddArc(rect.Right - diameter, rect.Top, diameter, diameter, 270, 90);
        path.AddArc(rect.Right - diameter, rect.Bottom - diameter, diameter, diameter, 0, 90);
        path.AddArc(rect.Left, rect.Bottom - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
        return path;
    }

    private static void DrawTrimmedText(Graphics graphics, string text, Font font, Brush brush, RectangleF rect)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return;
        }

        using (var format = new StringFormat(StringFormatFlags.LineLimit))
        {
            format.Trimming = StringTrimming.EllipsisCharacter;
            graphics.DrawString(text, font, brush, rect, format);
        }
    }

    private void BeginUpdate()
    {
        if (updateRunning)
        {
            return;
        }

        settings = AppSettings.Load();
        timer.Interval = Math.Max(1, settings.UpdateIntervalSeconds) * 1000;
        updateRunning = true;
        Func<MediaSnapshot> read = MediaReader.Read;
        Task.Factory.StartNew(read, CancellationToken.None, TaskCreationOptions.None, TaskScheduler.Default)
            .ContinueWith(UpdateFinished, TaskScheduler.FromCurrentSynchronizationContext());
    }

    private void UpdateFinished(Task<MediaSnapshot> task)
    {
        updateRunning = false;

        MediaSnapshot next;
        if (task.IsFaulted)
        {
            var ex = task.Exception == null ? null : task.Exception.GetBaseException();
            next = MediaSnapshot.Empty(ex == null ? "SMTC read failed" : ex.Message);
        }
        else
        {
            next = task.Result;
        }

        UpdateLiveTileIfNeeded(next);

        var oldArtwork = snapshot.Artwork;
        snapshot = next;
        if (oldArtwork != null && !object.ReferenceEquals(oldArtwork, snapshot.Artwork))
        {
            oldArtwork.Dispose();
        }

        Invalidate();
    }

    private void UpdateLiveTileIfNeeded(MediaSnapshot next)
    {
        var key = next.Source + "\n" + next.Title + "\n" + next.Artist + "\n" + next.Status + "\n" + settings.TileLayout;
        if (key == lastTileKey && (DateTime.UtcNow - lastTileUpdateUtc).TotalSeconds < settings.TileRefreshSeconds)
        {
            return;
        }

        if (LiveTileUpdater.TryUpdate(next, settings))
        {
            lastTileKey = key;
            lastTileUpdateUtc = DateTime.UtcNow;
        }
    }

    private void ToggleAlwaysOnTop(object sender, EventArgs e)
    {
        alwaysOnTop = !alwaysOnTop;
        TopMost = alwaysOnTop;
        ((ToolStripMenuItem)menu.Items[0]).Checked = alwaysOnTop;
    }

    private void TileForm_MouseDown(object sender, MouseEventArgs e)
    {
        if (e.Button != MouseButtons.Left)
        {
            return;
        }

        dragging = true;
        dragOffset = e.Location;
    }

    private void TileForm_MouseMove(object sender, MouseEventArgs e)
    {
        if (!dragging)
        {
            return;
        }

        Location = new Point(Left + e.X - dragOffset.X, Top + e.Y - dragOffset.Y);
    }
}

internal static class LiveTileUpdater
{
    public static bool TryUpdate(MediaSnapshot snapshot, AppSettings settings)
    {
        try
        {
            Directory.CreateDirectory(AppSettings.DataDirectory);
            var imageSource = SaveArtwork(snapshot.Artwork);
            var payloads = BuildTileXmlPayloads(snapshot, imageSource, settings.TileLayout);
            var updater = TileUpdateManager.CreateTileUpdaterForApplication();
            updater.Clear();
            updater.EnableNotificationQueue(payloads.Count > 1);

            foreach (var payload in payloads)
            {
                var xml = new XmlDocument();
                xml.LoadXml(payload);
                var notification = new TileNotification(xml);
                notification.ExpirationTime = DateTimeOffset.UtcNow.AddMinutes(5);
                updater.Update(notification);
            }

            return true;
        }
        catch
        {
            return false;
        }
    }

    private static string SaveArtwork(Image artwork)
    {
        if (artwork == null)
        {
            return string.Empty;
        }

        var path = Path.Combine(AppSettings.DataDirectory, "tile-artwork.jpg");
        using (var bitmap = new Bitmap(artwork, 310, 310))
        {
            bitmap.Save(path, System.Drawing.Imaging.ImageFormat.Jpeg);
        }

        return new Uri(path).AbsoluteUri;
    }

    private static List<string> BuildTileXmlPayloads(MediaSnapshot snapshot, string imageSource, TileLayout layout)
    {
        var payloads = new List<string>();
        var hasArtwork = !string.IsNullOrEmpty(imageSource);

        if (layout == TileLayout.Text || !hasArtwork)
        {
            payloads.Add(BuildTextTileXml(snapshot));
            return payloads;
        }

        if (layout == TileLayout.Artwork)
        {
            payloads.Add(BuildArtworkTileXml(snapshot, imageSource));
            return payloads;
        }

        if (layout == TileLayout.Combined)
        {
            payloads.Add(BuildCombinedTileXml(snapshot, imageSource));
            return payloads;
        }

        payloads.Add(BuildTextTileXml(snapshot));
        payloads.Add(BuildArtworkTileXml(snapshot, imageSource));
        return payloads;
    }

    private static string BuildTextTileXml(MediaSnapshot snapshot)
    {
        var title = Escape(snapshot.Title);
        var artist = Escape(snapshot.Artist);
        var source = Escape(snapshot.Source);
        var status = Escape(snapshot.Status);

        return
            "<tile>" +
            "  <visual branding=\"nameAndLogo\">" +
            "    <binding template=\"TileMedium\">" +
            "      <text hint-style=\"caption\" hint-wrap=\"true\">" + title + "</text>" +
            "      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + "</text>" +
            "      <text hint-style=\"captionSubtle\">" + status + "</text>" +
            "    </binding>" +
            "    <binding template=\"TileWide\">" +
            "      <text hint-style=\"title\" hint-wrap=\"true\">" + title + "</text>" +
            "      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + artist + "</text>" +
            "      <text hint-style=\"captionSubtle\">" + status + " | " + source + "</text>" +
            "    </binding>" +
            "    <binding template=\"TileLarge\">" +
            "      <text hint-style=\"title\" hint-wrap=\"true\">" + title + "</text>" +
            "      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + artist + "</text>" +
            "      <text hint-style=\"captionSubtle\">" + status + " | " + source + "</text>" +
            "    </binding>" +
            "  </visual>" +
            "</tile>";
    }

    private static string BuildArtworkTileXml(MediaSnapshot snapshot, string imageSource)
    {
        var title = Escape(snapshot.Title);
        var artist = Escape(snapshot.Artist);
        var image = Escape(imageSource);

        return
            "<tile>" +
            "  <visual branding=\"nameAndLogo\">" +
            "    <binding template=\"TileMedium\">" +
            "      <image src=\"" + image + "\" placement=\"background\" hint-overlay=\"25\"/>" +
            "      <text hint-style=\"caption\" hint-wrap=\"true\">" + title + "</text>" +
            "    </binding>" +
            "    <binding template=\"TileWide\">" +
            "      <image src=\"" + image + "\" placement=\"background\" hint-overlay=\"30\"/>" +
            "      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + title + "</text>" +
            "      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + "</text>" +
            "    </binding>" +
            "    <binding template=\"TileLarge\">" +
            "      <image src=\"" + image + "\" placement=\"background\" hint-overlay=\"35\"/>" +
            "      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + title + "</text>" +
            "      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + "</text>" +
            "    </binding>" +
            "  </visual>" +
            "</tile>";
    }

    private static string BuildCombinedTileXml(MediaSnapshot snapshot, string imageSource)
    {
        var title = Escape(snapshot.Title);
        var artist = Escape(snapshot.Artist);
        var source = Escape(snapshot.Source);
        var status = Escape(snapshot.Status);
        var image = Escape(imageSource);

        return
            "<tile>" +
            "  <visual branding=\"nameAndLogo\">" +
            "    <binding template=\"TileMedium\">" +
            "      <text hint-style=\"caption\" hint-wrap=\"true\">" + title + "</text>" +
            "      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + "</text>" +
            "    </binding>" +
            "    <binding template=\"TileWide\">" +
            "      <group>" +
            "        <subgroup hint-weight=\"30\">" +
            "          <image src=\"" + image + "\" hint-crop=\"square\"/>" +
            "        </subgroup>" +
            "        <subgroup>" +
            "          <text hint-style=\"subtitle\" hint-wrap=\"true\">" + title + "</text>" +
            "          <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + "</text>" +
            "          <text hint-style=\"captionSubtle\">" + status + " | " + source + "</text>" +
            "        </subgroup>" +
            "      </group>" +
            "    </binding>" +
            "    <binding template=\"TileLarge\">" +
            "      <image src=\"" + image + "\" hint-crop=\"square\"/>" +
            "      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + title + "</text>" +
            "      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + "</text>" +
            "      <text hint-style=\"captionSubtle\">" + status + " | " + source + "</text>" +
            "    </binding>" +
            "  </visual>" +
            "</tile>";
    }

    private static string Escape(string value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return string.Empty;
        }

        return value
            .Replace("&", "&amp;")
            .Replace("<", "&lt;")
            .Replace(">", "&gt;")
            .Replace("\"", "&quot;")
            .Replace("'", "&apos;");
    }
}

internal static class MediaReader
{
    private const int AsyncTimeoutMs = 10000;
    private static GlobalSystemMediaTransportControlsSessionManager manager;

    public static MediaSnapshot Read()
    {
        if (manager == null)
        {
            manager = WaitFor(GlobalSystemMediaTransportControlsSessionManager.RequestAsync());
        }

        var session = manager.GetCurrentSession();
        if (session == null)
        {
            return MediaSnapshot.Empty("No current SMTC session");
        }

        var props = WaitFor(session.TryGetMediaPropertiesAsync());
        var playback = session.GetPlaybackInfo();
        var title = string.IsNullOrWhiteSpace(props.Title) ? "(untitled media)" : props.Title;
        var artist = FirstNonEmpty(props.Artist, props.AlbumArtist, props.Subtitle, session.SourceAppUserModelId);
        var source = ShortSourceName(session.SourceAppUserModelId);
        var artwork = TryLoadArtwork(props.Thumbnail);

        return new MediaSnapshot(
            source,
            title,
            artist,
            playback.PlaybackStatus.ToString(),
            DateTime.Now.ToString("HH:mm:ss"),
            artwork);
    }

    private static Image TryLoadArtwork(IRandomAccessStreamReference thumbnail)
    {
        if (thumbnail == null)
        {
            return null;
        }

        try
        {
            using (var stream = WaitFor(thumbnail.OpenReadAsync()))
            {
                if (stream == null || stream.Size == 0 || stream.Size > 10 * 1024 * 1024)
                {
                    return null;
                }

                using (var reader = new DataReader(stream))
                {
                    var loaded = WaitFor(reader.LoadAsync((uint)stream.Size));
                    if (loaded == 0)
                    {
                        return null;
                    }

                    var bytes = new byte[loaded];
                    reader.ReadBytes(bytes);
                    using (var memory = new MemoryStream(bytes))
                    using (var image = Image.FromStream(memory))
                    {
                        return new Bitmap(image);
                    }
                }
            }
        }
        catch
        {
            return null;
        }
    }

    private static string ShortSourceName(string source)
    {
        if (string.IsNullOrWhiteSpace(source))
        {
            return "Unknown source";
        }

        if (source.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
        {
            return source.Substring(0, source.Length - 4);
        }

        var bang = source.IndexOf('!');
        if (bang >= 0 && bang < source.Length - 1)
        {
            source = source.Substring(0, bang);
        }

        var underscore = source.IndexOf('_');
        if (underscore > 0)
        {
            source = source.Substring(0, underscore);
        }

        return source;
    }

    private static string FirstNonEmpty(params string[] values)
    {
        foreach (var value in values)
        {
            if (!string.IsNullOrWhiteSpace(value))
            {
                return value;
            }
        }

        return "Unknown artist";
    }

    private static T WaitFor<T>(IAsyncOperation<T> operation)
    {
        int started = Environment.TickCount;
        while (operation.Status == AsyncStatus.Started)
        {
            if (TimedOut(started, AsyncTimeoutMs))
            {
                operation.Cancel();
                throw new TimeoutException("WinRT operation timed out.");
            }

            Thread.Sleep(25);
        }

        if (operation.Status == AsyncStatus.Error)
        {
            throw operation.ErrorCode;
        }

        if (operation.Status == AsyncStatus.Canceled)
        {
            throw new OperationCanceledException();
        }

        return operation.GetResults();
    }

    private static T WaitFor<T, TProgress>(IAsyncOperationWithProgress<T, TProgress> operation)
    {
        int started = Environment.TickCount;
        while (operation.Status == AsyncStatus.Started)
        {
            if (TimedOut(started, AsyncTimeoutMs))
            {
                operation.Cancel();
                throw new TimeoutException("WinRT operation timed out.");
            }

            Thread.Sleep(25);
        }

        if (operation.Status == AsyncStatus.Error)
        {
            throw operation.ErrorCode;
        }

        if (operation.Status == AsyncStatus.Canceled)
        {
            throw new OperationCanceledException();
        }

        return operation.GetResults();
    }

    private static bool TimedOut(int startedTick, int timeoutMs)
    {
        return unchecked(Environment.TickCount - startedTick) >= timeoutMs;
    }
}

internal sealed class MediaSnapshot
{
    public readonly string Source;
    public readonly string Title;
    public readonly string Artist;
    public readonly string Status;
    public readonly string LastUpdated;
    public readonly Image Artwork;

    public MediaSnapshot(string source, string title, string artist, string status, string lastUpdated, Image artwork)
    {
        Source = source;
        Title = title;
        Artist = artist;
        Status = status;
        LastUpdated = lastUpdated;
        Artwork = artwork;
    }

    public static MediaSnapshot Empty(string status)
    {
        return new MediaSnapshot("SMTC", "Nothing playing", "Start media in Spotify, browser, VLC, etc.", status, DateTime.Now.ToString("HH:mm:ss"), null);
    }
}

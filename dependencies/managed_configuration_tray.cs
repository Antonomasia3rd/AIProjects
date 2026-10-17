using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;
using System.Drawing;
using System.Diagnostics;
using System.IO.Pipes;
using System.Threading.Tasks;

namespace AIProjects.Dependencies
{
    // Products provide configuration data and their existing validated writer.
    // Utility products leave the resident callbacks unset, so opening their
    // configuration never runs a utility job. Resident products explicitly
    // declare a disposable session after the profile mutex has been acquired.
    public sealed class ManagedConfigurationTraySpec
    {
        public string ProductName;
        public string Version;
        public string IniPath;
        public Func<Dictionary<string, string>> ReadSettings;
        public Action<IDictionary<string, string>> SaveSettings;
        public ManagedConfigurationStartup Startup;
        public Func<IDisposable> OpenResidentSession;
        public Action ReloadResidentSession;
        // Set only for a non-offline invocation which saved live settings.
        // Ordinary --tray and utility configuration trays keep their behavior.
        public bool ReloadExistingInstance;
        public Func<string> ResidentStatus;
        public Func<string, bool> ReadOnlySetting;
        public IList<ManagedConfigurationCommand> Commands;
    }

    public sealed class ManagedConfigurationCommand
    {
        public string Text;
        public Action Execute;
    }

    public sealed class ManagedConfigurationEditor
    {
        readonly ManagedConfigurationTraySpec spec;
        readonly Dictionary<string, string> original;

        public ManagedConfigurationEditor(ManagedConfigurationTraySpec spec)
        {
            if (spec == null || spec.ReadSettings == null || spec.SaveSettings == null)
                throw new ArgumentException("The configuration reader and writer are required.", "spec");
            this.spec = spec;
            original = new Dictionary<string, string>(spec.ReadSettings(), StringComparer.OrdinalIgnoreCase);
        }

        public Dictionary<string, string> Values
        {
            get { return new Dictionary<string, string>(original, StringComparer.OrdinalIgnoreCase); }
        }

        public void Save(IDictionary<string, string> edited)
        {
            if (edited == null || edited.Count != original.Count || edited.Keys.Any(key => !original.ContainsKey(key)))
                throw new ArgumentException("The edited configuration must keep its setting keys.", "edited");
            var changes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var entry in edited)
                if (!String.Equals(entry.Value, original[entry.Key], StringComparison.Ordinal))
                {
                    if (spec.ReadOnlySetting != null && spec.ReadOnlySetting(entry.Key))
                        throw new ArgumentException("This value is managed by the application: " + entry.Key);
                    changes[entry.Key] = entry.Value ?? "";
                }
            // Save only actual edits. The product validates the prospective
            // profile under its existing INI lock; unrelated external edits
            // are preserved, and rejected edits never update this snapshot.
            spec.SaveSettings(changes);
            Dictionary<string, string> fresh;
            try { fresh = spec.ReadSettings(); }
            catch (Exception ex) { throw new IOException("The configuration was saved, but could not be reloaded: " + ex.Message, ex); }
            original.Clear();
            foreach (var entry in fresh) original.Add(entry.Key, entry.Value);
        }
    }

    // One private connection carries one reload and one acknowledgment, so a
    // delayed reply cannot satisfy a later caller. No settings or credentials
    // cross the pipe: the owner reloads its already-selected profile from disk.
    internal sealed class ManagedConfigurationReload : IDisposable
    {
        const int Protocol = 0x41505231;
        const int MaximumWait = 5000;
        readonly string pipeName;
        readonly Action<Action> dispatch;
        readonly Action reload;
        readonly CancellationTokenSource stopped = new CancellationTokenSource();
        readonly object pipeLock = new object();
        readonly Thread worker;
        NamedPipeServerStream current;
        int disposed;

        internal static string PipeName(string identity)
        {
            // Pipe namespaces are machine-wide. Reuse the user/SID and exact
            // executable/profile identity of the shared tray mutex.
            return ManagedNamedObjects.CurrentUserScopedName("ConfigurationReload", identity).Replace('\\', '.');
        }

        internal ManagedConfigurationReload(string identity, Action<Action> dispatch, Action reload)
        {
            if (dispatch == null || reload == null) throw new ArgumentNullException("reload");
            pipeName = PipeName(identity); this.dispatch = dispatch; this.reload = reload;
            current = NewPipe(); // Fail before the tray loop if IPC cannot open.
            worker = new Thread(Listen) { IsBackground = true, Name = "Configuration reload" };
            try { worker.Start(); }
            catch { current.Dispose(); stopped.Dispose(); throw; }
        }

        NamedPipeServerStream NewPipe()
        {
            return new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
                PipeTransmissionMode.Byte, PipeOptions.Asynchronous, 256, 256);
        }

        static void ObserveFailure(Task task)
        {
            task.ContinueWith(failed => { GC.KeepAlive(failed.Exception); },
                CancellationToken.None, TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }

        static void Complete(Task task, int milliseconds, CancellationToken cancel)
        {
            ObserveFailure(task);
            if (!task.Wait(milliseconds, cancel)) throw new TimeoutException("The resident reload acknowledgment timed out.");
        }

        static int Remaining(Stopwatch clock, int maximum)
        { return Math.Max(0, maximum - (int)Math.Min(Int32.MaxValue, clock.ElapsedMilliseconds)); }

        static void Write(PipeStream pipe, byte[] bytes, int timeout, CancellationToken cancel)
        {
            Task task = Task.Factory.FromAsync(
                (callback, state) => pipe.BeginWrite(bytes, 0, bytes.Length, callback, state), pipe.EndWrite, null);
            Complete(task, timeout, cancel);
        }

        static void Read(PipeStream pipe, byte[] bytes, int timeout, CancellationToken cancel)
        {
            var clock = Stopwatch.StartNew();
            int offset = 0;
            while (offset < bytes.Length)
            {
                int begin = offset;
                Task<int> task = Task<int>.Factory.FromAsync(
                    (callback, state) => pipe.BeginRead(bytes, begin, bytes.Length - begin, callback, state), pipe.EndRead, null);
                Complete(task, Remaining(clock, timeout), cancel);
                int count = task.Result;
                if (count == 0) throw new EndOfStreamException("The resident closed the reload connection.");
                offset += count;
            }
        }

        sealed class Work
        {
            readonly object gate = new object();
            readonly DateTime expires;
            readonly CancellationToken stopped;
            readonly Action reload;
            bool canceled;
            public readonly TaskCompletionSource<byte> Completion = new TaskCompletionSource<byte>();
            public Work(DateTime expires, CancellationToken stopped, Action reload)
            { this.expires = expires; this.stopped = stopped; this.reload = reload; }
            public void Cancel() { lock (gate) canceled = true; }
            public void Execute()
            {
                lock (gate)
                {
                    if (canceled || stopped.IsCancellationRequested || DateTime.UtcNow >= expires)
                    { Completion.TrySetResult(3); return; }
                }
                try { reload(); Completion.TrySetResult(1); }
                catch { Completion.TrySetResult(2); }
            }
        }

        void Listen()
        {
            try
            {
                while (!stopped.IsCancellationRequested)
                {
                    NamedPipeServerStream pipe;
                    lock (pipeLock) pipe = current;
                    if (pipe == null) break;
                    try
                    {
                        Task accepted = Task.Factory.FromAsync(pipe.BeginWaitForConnection, pipe.EndWaitForConnection, null);
                        Complete(accepted, Timeout.Infinite, stopped.Token);
                        var request = new byte[12];
                        Read(pipe, request, MaximumWait, stopped.Token);
                        DateTime expires = new DateTime(BitConverter.ToInt64(request, 4), DateTimeKind.Utc);
                        int remaining = (int)Math.Max(0, Math.Min(MaximumWait, (expires - DateTime.UtcNow).TotalMilliseconds));
                        if (BitConverter.ToInt32(request, 0) != Protocol || remaining == 0)
                            Write(pipe, new byte[] { 3 }, 250, stopped.Token);
                        else
                        {
                            var work = new Work(expires, stopped.Token, reload);
                            try
                            {
                                dispatch(work.Execute);
                                Complete(work.Completion.Task, remaining, stopped.Token);
                                Write(pipe, new[] { work.Completion.Task.Result }, Math.Max(1, Math.Min(250, remaining)), stopped.Token);
                            }
                            finally { work.Cancel(); }
                        }
                    }
                    catch (Exception ex)
                    {
                        if (!(ex is IOException || ex is TimeoutException || ex is OperationCanceledException ||
                            ex is AggregateException || ex is ObjectDisposedException || ex is ArgumentException ||
                            ex is InvalidOperationException)) throw;
                        // A client timeout/disconnect must not stop the owner.
                    }
                    finally { pipe.Dispose(); }
                    lock (pipeLock)
                    {
                        current = null;
                        if (!stopped.IsCancellationRequested) current = NewPipe();
                    }
                }
            }
            catch (Exception ex) { Console.Error.WriteLine("Configuration reload receiver stopped: " + ex.Message); }
            finally
            {
                lock (pipeLock) { if (current != null) current.Dispose(); current = null; }
            }
        }

        internal static bool Request(string identity, int timeoutMilliseconds, out string error)
        {
            error = null;
            if (timeoutMilliseconds < 1 || timeoutMilliseconds > MaximumWait)
            { error = "The reload deadline must be between 1 and 5000 milliseconds."; return false; }
            var clock = Stopwatch.StartNew();
            try
            {
                using (var pipe = new NamedPipeClientStream(".", PipeName(identity), PipeDirection.InOut, PipeOptions.Asynchronous))
                {
                    pipe.Connect(timeoutMilliseconds);
                    byte[] request = new byte[12];
                    Buffer.BlockCopy(BitConverter.GetBytes(Protocol), 0, request, 0, 4);
                    Buffer.BlockCopy(BitConverter.GetBytes(DateTime.UtcNow.AddMilliseconds(Remaining(clock, timeoutMilliseconds)).Ticks), 0, request, 4, 8);
                    Write(pipe, request, Remaining(clock, timeoutMilliseconds), CancellationToken.None);
                    var response = new byte[1];
                    Read(pipe, response, Remaining(clock, timeoutMilliseconds), CancellationToken.None);
                    if (response[0] == 1) return true;
                    error = response[0] == 2 ? "The resident could not reload the saved profile; see its status." :
                        "The resident did not apply the request before its deadline.";
                }
            }
            catch (Exception ex)
            {
                error = ex is TimeoutException ? "The resident reload acknowledgment timed out." :
                    "The resident reload connection failed or closed before acknowledgment.";
            }
            return false;
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref disposed, 1) != 0) return;
            stopped.Cancel();
            lock (pipeLock) { if (current != null) current.Dispose(); }
            if (worker.Join(1500)) stopped.Dispose();
            // The background worker owns any timed-out callback state. It
            // cannot keep the process alive or call disposed UI after cancel.
        }
    }

    public static class ManagedConfigurationTray
    {
        internal static string InstanceIdentity(ManagedConfigurationTraySpec spec)
        {
            return Path.GetFullPath(Application.ExecutablePath).ToUpperInvariant() + "|" +
                Path.GetFullPath(spec.IniPath).ToUpperInvariant();
        }

        internal static string InstanceMutexName(ManagedConfigurationTraySpec spec)
        { return ManagedNamedObjects.CurrentUserScopedName("ConfigurationTray", InstanceIdentity(spec)); }

        internal static int ReloadExisting(ManagedConfigurationTraySpec spec, int timeoutMilliseconds = 5000)
        {
            string error;
            if (ManagedConfigurationReload.Request(InstanceIdentity(spec), timeoutMilliseconds, out error))
            {
                Console.WriteLine("Saved settings were applied by the running " + spec.ProductName + " tray.");
                return 0;
            }
            Console.Error.WriteLine("Settings were saved, but the running " + spec.ProductName +
                " tray did not confirm applying them: " + error + " Use Reload configuration or restart that tray.");
            return 1;
        }

        public static int ReloadExistingIfRunning(ManagedConfigurationTraySpec spec)
        {
            if (!spec.ReloadExistingInstance || spec.ReloadResidentSession == null) return 0;
            try
            {
                using (Mutex existing = Mutex.OpenExisting(InstanceMutexName(spec)))
                    return ReloadExisting(spec);
            }
            catch (WaitHandleCannotBeOpenedException) { return 0; }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Settings were saved, but the running tray could not be inspected: " + ex.Message);
                return 1;
            }
        }

        public static Dictionary<string, string> ReadForEditing(
            IDictionary<string, string> defaults, ManagedIniFileSpec file)
        {
            var values = new Dictionary<string, string>(defaults, StringComparer.OrdinalIgnoreCase);
            foreach (var entry in ManagedIniFile.LoadSection(file, false)) values[entry.Key] = entry.Value;
            // Keep invalid saved values visible so a single batch can repair
            // interdependent settings instead of failing before the editor opens.
            return values;
        }

        public static int Run(ManagedConfigurationTraySpec spec)
        {
            if (spec == null || String.IsNullOrWhiteSpace(spec.ProductName) ||
                String.IsNullOrWhiteSpace(spec.IniPath) || spec.ReadSettings == null || spec.SaveSettings == null)
                throw new ArgumentException("A complete tray configuration specification is required.", "spec");
            string identity = InstanceIdentity(spec);
            bool created;
            using (var mutex = new Mutex(true, ManagedNamedObjects.CurrentUserScopedName("ConfigurationTray", identity), out created))
            {
                if (!created)
                {
                    if (spec.ReloadExistingInstance && spec.ReloadResidentSession != null)
                        return ReloadExisting(spec);
                    Console.WriteLine(spec.ProductName + " configuration tray is already running for this profile.");
                    return 0;
                }
                try
                {
                    Application.EnableVisualStyles();
                    using (IDisposable resident = spec.OpenResidentSession == null ? null : spec.OpenResidentSession())
                    using (var context = new ApplicationContext())
                    using (var menu = new ContextMenu())
                    using (var dispatcher = new Control())
                    {
                        // The receiver applies settings on the same UI thread
                        // as tray edits. Only real resident hosts open an IPC
                        // endpoint; utility trays have no listener at all.
                        if (dispatcher.Handle == IntPtr.Zero) throw new InvalidOperationException("Could not create the reload dispatcher.");
                        using (var reload = spec.ReloadResidentSession == null ? null :
                            new ManagedConfigurationReload(identity,
                                action => dispatcher.BeginInvoke(action), spec.ReloadResidentSession))
                        {
                            bool editorOpen = false;
                            Action edit = delegate
                            {
                                if (editorOpen) return;
                                editorOpen = true;
                                try { ShowEditor(spec); }
                                finally { editorOpen = false; }
                            };
                            ManagedTrayBaseline.AppendHeader(menu, spec.ProductName, "Version: " + spec.Version);
                            menu.MenuItems.Add(new MenuItem("Settings...", delegate { edit(); }) { DefaultItem = true });
                            if (spec.Commands != null)
                                foreach (var command in spec.Commands)
                                {
                                    if (command == null || String.IsNullOrWhiteSpace(command.Text) || command.Execute == null)
                                        throw new ArgumentException("Configuration commands require a label and handler.");
                                    var selected = command;
                                    menu.MenuItems.Add(new MenuItem(selected.Text, delegate
                                    {
                                        try { selected.Execute(); }
                                        catch (Exception ex) { MessageBox.Show(ex.Message, spec.ProductName, MessageBoxButtons.OK, MessageBoxIcon.Error); }
                                    }));
                                }
                            if (spec.Startup != null)
                            {
                                var startup = new MenuItem("Start tray at sign-in (Startup folder)");
                                var status = new MenuItem { Enabled = false, Visible = false };
                                Action<bool> refresh = delegate(bool showError)
                                {
                                    try
                                    {
                                        if (showError && spec.ReloadResidentSession != null) spec.ReloadResidentSession();
                                        spec.Startup.Reconcile();
                                        var state = spec.Startup.ReadState();
                                        if (!String.IsNullOrEmpty(state.Error)) throw new IOException(state.Error);
                                        startup.Checked = state.Installed;
                                        startup.Enabled = true;
                                        status.Visible = false;
                                    }
                                    catch (Exception ex)
                                    {
                                        startup.Checked = false;
                                        startup.Enabled = false;
                                        status.Text = "Startup status unavailable; use Settings or Reload";
                                        status.Visible = true;
                                        if (showError) MessageBox.Show(ex.Message, spec.ProductName, MessageBoxButtons.OK, MessageBoxIcon.Error);
                                        else Console.Error.WriteLine("Startup: " + ex.Message);
                                    }
                                };
                                startup.Click += delegate
                                {
                                    try
                                    {
                                        var state = spec.Startup.ReadState();
                                        if (!String.IsNullOrEmpty(state.Error)) throw new IOException(state.Error);
                                        spec.SaveSettings(new Dictionary<string, string> {
                                            { ManagedConfigurationStartup.Key, state.Configured ? "0" : "1" }
                                        });
                                    }
                                    catch (Exception ex) { MessageBox.Show(ex.Message, spec.ProductName, MessageBoxButtons.OK, MessageBoxIcon.Error); }
                                    refresh(false);
                                };
                                menu.MenuItems.Add(startup);
                                menu.MenuItems.Add(status);
                                menu.MenuItems.Add(new MenuItem("Reload configuration", delegate { refresh(true); }));
                                menu.Popup += delegate { refresh(false); };
                                refresh(false);
                            }
                            menu.MenuItems.Add(new MenuItem("Profile: " + Path.GetFileName(spec.IniPath).Replace("&", "&&")) { Enabled = false });
                            menu.MenuItems.Add("-");
                            menu.MenuItems.Add(new MenuItem("Exit", delegate { context.ExitThread(); }));
                            NotifyIcon tray = ManagedTrayBaseline.CreateNotifyIcon(
                                SystemIcons.Application, spec.ProductName + " - " + Path.GetFileName(spec.IniPath), spec.ProductName, menu);
                            using (var statusTimer = new System.Windows.Forms.Timer { Interval = 1000 })
                            try
                            {
                                tray.DoubleClick += delegate { edit(); };
                                if (spec.ResidentStatus != null)
                                {
                                    Action updateStatus = delegate
                                    {
                                        try { tray.Text = ManagedTrayBaseline.NormalizeTooltip(spec.ProductName + " - " + spec.ResidentStatus(), spec.ProductName); }
                                        catch (Exception ex) { Console.Error.WriteLine("Tray status: " + ex.Message); }
                                    };
                                    statusTimer.Tick += delegate { updateStatus(); };
                                    updateStatus();
                                    statusTimer.Start();
                                }
                                Application.Run(context);
                            }
                            finally { ManagedTrayBaseline.DisposeNotifyIcon(ref tray); }
                        }
                    }
                }
                finally { mutex.ReleaseMutex(); }
            }
            return 0;
        }

        public static void ShowEditor(ManagedConfigurationTraySpec spec)
        {
            try
            {
                using (var form = CreateEditor(spec, delegate(Form owner, string message)
                {
                    MessageBox.Show(owner, message, "Could not save configuration", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }))
                    form.ShowDialog();
            }
            catch (Exception ex) { MessageBox.Show(ex.Message, spec.ProductName, MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        // Construction is separate from display so the real layout and button
        // handlers can be tested without a window, tray icon, or message loop.
        // The caller owns the returned form and all its child controls.
        internal static Form CreateEditor(ManagedConfigurationTraySpec spec, Action<Form, string> reportError)
        {
            if (reportError == null) throw new ArgumentNullException("reportError");
            var editor = new ManagedConfigurationEditor(spec);
            var form = new Form();
            try
            {
                var grid = new DataGridView { Name = "ConfigurationSettingsGrid" };
                var path = new Label { Name = "ConfigurationProfilePath" };
                var buttons = new FlowLayoutPanel { Name = "ConfigurationButtons" };
                var save = new Button { Name = "ConfigurationSaveButton" };
                var cancel = new Button { Name = "ConfigurationCancelButton" };
                buttons.Controls.Add(cancel);
                buttons.Controls.Add(save);
                form.Controls.Add(grid);
                form.Controls.Add(path);
                form.Controls.Add(buttons);

                form.SuspendLayout();
                form.Name = "ConfigurationEditor";
                form.Text = spec.ProductName + " settings";
                form.StartPosition = FormStartPosition.CenterScreen;
                form.ClientSize = new Size(700, 340);
                form.MinimumSize = new Size(500, 260);
                form.MinimizeBox = false;
                path.Text = spec.IniPath;
                path.AutoEllipsis = true;
                path.Dock = DockStyle.Top;
                path.Height = 34;
                grid.Dock = DockStyle.Fill;
                grid.AllowUserToAddRows = false;
                grid.AllowUserToDeleteRows = false;
                grid.RowHeadersVisible = false;
                grid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
                grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "Setting", ReadOnly = true, FillWeight = 40 });
                grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "Value", FillWeight = 60 });
                foreach (var entry in editor.Values.OrderBy(entry => entry.Key, StringComparer.OrdinalIgnoreCase))
                {
                    int row = grid.Rows.Add(entry.Key, entry.Value);
                    if (spec.ReadOnlySetting != null && spec.ReadOnlySetting(entry.Key))
                    {
                        grid.Rows[row].Cells[1].ReadOnly = true;
                        grid.Rows[row].Cells[1].ToolTipText = "Managed by the application; use its dedicated action to change this value.";
                    }
                }
                buttons.Dock = DockStyle.Bottom;
                buttons.Height = 42;
                buttons.FlowDirection = FlowDirection.RightToLeft;
                save.Text = "Save";
                cancel.Text = "Cancel";
                cancel.DialogResult = DialogResult.Cancel;
                cancel.CausesValidation = false;
                save.Click += delegate
                {
                    try
                    {
                        if (!grid.EndEdit())
                            throw new InvalidOperationException("Finish editing the current setting before saving.");
                        var edited = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                        foreach (DataGridViewRow row in grid.Rows)
                            edited.Add((string)row.Cells[0].Value, Convert.ToString(row.Cells[1].Value));
                        editor.Save(edited);
                        form.DialogResult = DialogResult.OK;
                        form.Close();
                    }
                    catch (Exception ex) { reportError(form, ex.Message); }
                };
                cancel.Click += delegate
                {
                    form.DialogResult = DialogResult.Cancel;
                    form.Close();
                };
                form.AcceptButton = save;
                form.CancelButton = cancel;
                form.ResumeLayout(true);
                return form;
            }
            catch
            {
                form.Dispose();
                throw;
            }
        }
    }
}

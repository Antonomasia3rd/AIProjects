using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;
using System.Drawing;

namespace AIProjects.Dependencies
{
    // Products provide configuration data and their existing validated writer.
    // This host has no job callback: opening it cannot run a utility operation.
    public sealed class ManagedConfigurationTraySpec
    {
        public string ProductName;
        public string Version;
        public string IniPath;
        public Func<Dictionary<string, string>> ReadSettings;
        public Action<IDictionary<string, string>> SaveSettings;
        public ManagedConfigurationStartup Startup;
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
                    changes[entry.Key] = entry.Value ?? "";
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

    public static class ManagedConfigurationTray
    {
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
            string identity = Path.GetFullPath(Application.ExecutablePath).ToUpperInvariant() + "|" +
                Path.GetFullPath(spec.IniPath).ToUpperInvariant();
            bool created;
            using (var mutex = new Mutex(true, ManagedNamedObjects.CurrentUserScopedName("ConfigurationTray", identity), out created))
            {
                if (!created)
                {
                    Console.WriteLine(spec.ProductName + " configuration tray is already running for this profile.");
                    return 0;
                }
                try
                {
                    Application.EnableVisualStyles();
                    using (var context = new ApplicationContext())
                    using (var menu = new ContextMenu())
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
                        if (spec.Startup != null)
                        {
                            var startup = new MenuItem("Start tray at sign-in (Startup folder)");
                            var status = new MenuItem { Enabled = false, Visible = false };
                            Action<bool> refresh = delegate(bool showError)
                            {
                                try
                                {
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
                        try
                        {
                            tray.DoubleClick += delegate { edit(); };
                            Application.Run(context);
                        }
                        finally { ManagedTrayBaseline.DisposeNotifyIcon(ref tray); }
                    }
                }
                finally { mutex.ReleaseMutex(); }
            }
            return 0;
        }

        static void ShowEditor(ManagedConfigurationTraySpec spec)
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
                    grid.Rows.Add(entry.Key, entry.Value);
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

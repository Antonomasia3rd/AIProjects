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
                var editor = new ManagedConfigurationEditor(spec);
                using (var form = new Form())
                using (var grid = new DataGridView())
                using (var path = new Label())
                using (var buttons = new FlowLayoutPanel())
                using (var save = new Button())
                using (var cancel = new Button())
                {
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
                    save.Click += delegate
                    {
                        try
                        {
                            grid.EndEdit();
                            var edited = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                            foreach (DataGridViewRow row in grid.Rows)
                                edited.Add((string)row.Cells[0].Value, Convert.ToString(row.Cells[1].Value));
                            editor.Save(edited);
                            form.DialogResult = DialogResult.OK;
                            form.Close();
                        }
                        catch (Exception ex) { MessageBox.Show(form, ex.Message, "Could not save configuration", MessageBoxButtons.OK, MessageBoxIcon.Error); }
                    };
                    buttons.Controls.Add(cancel);
                    buttons.Controls.Add(save);
                    form.Controls.Add(grid);
                    form.Controls.Add(path);
                    form.Controls.Add(buttons);
                    form.AcceptButton = save;
                    form.CancelButton = cancel;
                    form.ShowDialog();
                }
            }
            catch (Exception ex) { MessageBox.Show(ex.Message, spec.ProductName, MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }
    }
}

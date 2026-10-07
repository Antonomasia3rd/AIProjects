using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;
using AIProjects.Dependencies;

// Tests the actual editor controls and handlers without Show, ShowDialog,
// Application.Run, NotifyIcon, a utility job, or any Startup operation.
static class ConfigurationTrayUiTests
{
    static int checks;

    [STAThread]
    static int Main(string[] args)
    {
        if (args.Length != 0) return 2;
        using (var deadline = new System.Threading.Timer(delegate(object ignored)
        {
            Console.Error.WriteLine("Configuration tray UI tests exceeded their 45-second deadline.");
            Environment.Exit(124);
        }, null, 45000, Timeout.Infinite))
        {
            try
            {
                TestLayoutAndCancel();
                TestSaveAndRetry();
                TestReloadFailure();
                TestTemporaryProfile();
                Console.WriteLine("Configuration tray offscreen UI checks passed: " + checks);
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine(ex);
                return 1;
            }
        }
    }

    static ManagedConfigurationTraySpec Spec(Dictionary<string, string> values,
        Action<IDictionary<string, string>> save)
    {
        return new ManagedConfigurationTraySpec
        {
            ProductName = "Offscreen fixture",
            Version = "test",
            IniPath = Path.Combine(Path.GetTempPath(), "UI fixture profile.ini"),
            ReadSettings = delegate { return new Dictionary<string, string>(values, StringComparer.OrdinalIgnoreCase); },
            SaveSettings = save
        };
    }

    static void TestLayoutAndCancel()
    {
        var values = new Dictionary<string, string> { { "Zulu", "last" }, { "Alpha", "first" } };
        int saves = 0;
        int errors = 0;
        var spec = Spec(values, delegate { ++saves; });
        DataGridView grid;
        using (Form form = ManagedConfigurationTray.CreateEditor(spec, delegate { ++errors; }))
        {
            grid = Find<DataGridView>(form, "ConfigurationSettingsGrid");
            Check(!form.Visible, "factory leaves the form hidden");
            Check(form.Text == "Offscreen fixture settings", "product title appears on the real form");
            Check(Find<Label>(form, "ConfigurationProfilePath").Text == spec.IniPath,
                "editor identifies the exact profile");
            Check(Find<Label>(form, "ConfigurationProfilePath").AutoEllipsis, "long profile paths use ellipsis");
            Check(grid.Rows.Count == 2 && (string)grid.Rows[0].Cells[0].Value == "Alpha",
                "real grid lists setting names in stable order");
            Check(grid.Columns[0].ReadOnly && !grid.Columns[1].ReadOnly &&
                !grid.AllowUserToAddRows && !grid.AllowUserToDeleteRows,
                "users can edit values without changing the setting keys");
            Check(form.AcceptButton == Find<Button>(form, "ConfigurationSaveButton") &&
                form.CancelButton == Find<Button>(form, "ConfigurationCancelButton"),
                "Enter and Escape target the real Save and Cancel buttons");
            Check(!Find<Button>(form, "ConfigurationCancelButton").CausesValidation,
                "Cancel can leave an invalid edit");
            CheckLayout(form, "normal size");
            form.Size = form.MinimumSize;
            CheckLayout(form, "minimum size");
            form.ClientSize = new Size(1100, 600);
            CheckLayout(form, "expanded size");
            grid.Rows[0].Cells[1].Value = "unsaved value";
            Click(Find<Button>(form, "ConfigurationCancelButton"));
            Check(form.DialogResult == DialogResult.Cancel && saves == 0 && errors == 0,
                "the real Cancel handler discards edits without saving");
            Check(values["Alpha"] == "first", "Cancel preserves the stored value");
        }
        Check(grid.IsDisposed, "disposing the editor also disposes its grid");
    }

    static void CheckLayout(Form form, string label)
    {
        form.PerformLayout();
        var grid = Find<DataGridView>(form, "ConfigurationSettingsGrid");
        var path = Find<Label>(form, "ConfigurationProfilePath");
        var buttons = Find<FlowLayoutPanel>(form, "ConfigurationButtons");
        buttons.PerformLayout();
        var save = Find<Button>(form, "ConfigurationSaveButton");
        var cancel = Find<Button>(form, "ConfigurationCancelButton");
        Check(form.ClientRectangle.Contains(path.Bounds) && form.ClientRectangle.Contains(grid.Bounds) &&
            form.ClientRectangle.Contains(buttons.Bounds), label + ": controls stay inside the client area");
        Check(path.Bottom <= grid.Top && grid.Bottom <= buttons.Top && grid.Width > 0 && grid.Height > 0,
            label + ": profile, grid and button panel do not overlap");
        Check(buttons.ClientRectangle.Contains(save.Bounds) && buttons.ClientRectangle.Contains(cancel.Bounds) &&
            !save.Bounds.IntersectsWith(cancel.Bounds), label + ": Save and Cancel remain separate and reachable");
        Check(!form.Visible, label + ": layout never displays a window");
    }

    static void TestSaveAndRetry()
    {
        var values = new Dictionary<string, string> { { "Count", "3" }, { "Other", "original" } };
        var errors = new List<string>();
        int writes = 0;
        Dictionary<string, string> submitted = null;
        var spec = Spec(values, delegate(IDictionary<string, string> changes)
        {
            if (changes.ContainsKey("Count") && changes["Count"] == "invalid")
                throw new ArgumentException("Count rejected by fixture.");
            submitted = new Dictionary<string, string>(changes);
            foreach (var entry in changes) values[entry.Key] = entry.Value;
            ++writes;
        });
        using (Form form = ManagedConfigurationTray.CreateEditor(spec, delegate(Form owner, string message) { errors.Add(message); }))
        {
            var grid = Find<DataGridView>(form, "ConfigurationSettingsGrid");
            SetValue(grid, "Count", "invalid");
            Click(Find<Button>(form, "ConfigurationSaveButton"));
            Check(errors.Count == 1 && errors[0] == "Count rejected by fixture.",
                "Save reports the validator's exact error through the injected callback");
            Check(!form.IsDisposed && form.DialogResult == DialogResult.None && writes == 0 && values["Count"] == "3",
                "failed Save retains the editor and unchanged persisted values");
            values["Other"] = "external change";
            SetValue(grid, "Count", "4");
            Click(Find<Button>(form, "ConfigurationSaveButton"));
            Check(form.DialogResult == DialogResult.OK && writes == 1 && values["Count"] == "4",
                "retry through the same real Save button persists the corrected edit");
            Check(submitted.Count == 1 && submitted["Count"] == "4" && values["Other"] == "external change",
                "Save submits only edits and preserves unrelated external changes");
        }
    }

    static void TestReloadFailure()
    {
        int reads = 0;
        int writes = 0;
        string error = null;
        var values = new Dictionary<string, string> { { "Value", "before" } };
        var spec = Spec(values, delegate(IDictionary<string, string> changes) { ++writes; values["Value"] = changes["Value"]; });
        spec.ReadSettings = delegate
        {
            if (++reads > 1) throw new IOException("fixture reload failure");
            return new Dictionary<string, string>(values);
        };
        using (Form form = ManagedConfigurationTray.CreateEditor(spec, delegate(Form owner, string message) { error = message; }))
        {
            SetValue(Find<DataGridView>(form, "ConfigurationSettingsGrid"), "Value", "after");
            Click(Find<Button>(form, "ConfigurationSaveButton"));
            Check(writes == 1 && values["Value"] == "after" && error != null &&
                error.Contains("was saved, but could not be reloaded") && error.Contains("fixture reload failure"),
                "Save distinguishes successful persistence from reload failure");
            Check(!form.IsDisposed && form.DialogResult == DialogResult.None,
                "reload failure leaves the editor available without claiming success");
            Click(Find<Button>(form, "ConfigurationCancelButton"));
            Check(writes == 1 && form.DialogResult == DialogResult.Cancel,
                "Cancel after a reload failure does not perform a second write");
        }
    }

    static void TestTemporaryProfile()
    {
        string temporary = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string root = Path.GetFullPath(Path.Combine(temporary, "AIProjects-OffscreenUi-" + Guid.NewGuid().ToString("N")));
        if (!String.Equals(Path.GetDirectoryName(root), temporary, StringComparison.OrdinalIgnoreCase))
            throw new IOException("The fixture path is outside its temporary parent.");
        Directory.CreateDirectory(root);
        try
        {
            string ini = Path.Combine(root, "fixture.ini");
            File.WriteAllText(ini, "; preserve comment\r\n[Settings]\r\nCount=3\r\nOther=keep\r\n");
            var file = new ManagedIniFileSpec { FilePath = ini, SectionName = "Settings" };
            int errors = 0;
            var spec = new ManagedConfigurationTraySpec
            {
                ProductName = "Temporary profile fixture", Version = "test", IniPath = ini,
                ReadSettings = delegate { return ManagedIniFile.LoadSection(file, false); },
                SaveSettings = delegate(IDictionary<string, string> changes)
                {
                    string message;
                    if (!ManagedIniFile.SaveSectionBatch(file, changes, delegate(IDictionary<string, string> prospective)
                    {
                        int value;
                        if (!Int32.TryParse(prospective["Count"], out value) || value < 1)
                            throw new ArgumentException("Count must be positive.");
                    }, out message)) throw new IOException(message);
                }
            };
            using (Form form = ManagedConfigurationTray.CreateEditor(spec, delegate { ++errors; }))
            {
                var grid = Find<DataGridView>(form, "ConfigurationSettingsGrid");
                SetValue(grid, "Count", "0");
                byte[] before = File.ReadAllBytes(ini);
                Click(Find<Button>(form, "ConfigurationSaveButton"));
                Check(errors == 1 && before.SequenceEqual(File.ReadAllBytes(ini)),
                    "real Save preserves temporary INI bytes after rejected prospective validation");
                SetValue(grid, "Count", "7");
                Click(Find<Button>(form, "ConfigurationSaveButton"));
                var saved = ManagedIniFile.LoadSection(file, false);
                Check(form.DialogResult == DialogResult.OK && saved["Count"] == "7" && saved["Other"] == "keep" &&
                    File.ReadAllText(ini).Contains("; preserve comment"),
                    "real Save uses atomic INI persistence while retaining other values and comments");
            }
            Check(Directory.GetFiles(root).Length == 1, "successful fixture save leaves no temporary siblings");
        }
        finally
        {
            if (!String.Equals(Path.GetDirectoryName(Path.GetFullPath(root)), temporary, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Refusing cleanup outside the fixture's temporary parent.");
            Directory.Delete(root, true);
        }
    }

    static T Find<T>(Form form, string name) where T : Control
    {
        Control[] found = form.Controls.Find(name, true);
        if (found.Length != 1 || !(found[0] is T)) throw new InvalidOperationException("Missing fixture control: " + name);
        return (T)found[0];
    }

    static void SetValue(DataGridView grid, string key, string value)
    {
        foreach (DataGridViewRow row in grid.Rows)
            if (String.Equals(Convert.ToString(row.Cells[0].Value), key, StringComparison.Ordinal))
            {
                row.Cells[1].Value = value;
                return;
            }
        throw new InvalidOperationException("Missing fixture setting: " + key);
    }

    static void Click(Button button)
    {
        // PerformClick refuses hidden controls. Invoke the actual Button.OnClick
        // implementation so event wiring runs without displaying the form.
        typeof(Button).GetMethod("OnClick", BindingFlags.Instance | BindingFlags.NonPublic)
            .Invoke(button, new object[] { EventArgs.Empty });
    }

    static void Check(bool passed, string message)
    {
        if (!passed) throw new InvalidOperationException("FAIL: " + message);
        ++checks;
        Console.WriteLine("ok - " + message);
    }
}

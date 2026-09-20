using System;
using System.Drawing;
using System.Windows.Forms;

namespace AIProjects.Dependencies
{
    public static class ManagedTrayBaseline
    {
        public const int NotifyIconTooltipLimit = 63;

        public static string NormalizeTooltip(string tooltip, string fallback)
        {
            string text = (tooltip ?? "").Replace('\0', ' ').Trim();
            if (text.Length == 0)
                text = (fallback ?? "").Replace('\0', ' ').Trim();
            if (String.IsNullOrWhiteSpace(text))
                text = "AIProjects";
            if (text.Length <= NotifyIconTooltipLimit)
                return text;
            if (Char.IsHighSurrogate(text[NotifyIconTooltipLimit - 1]) &&
                Char.IsLowSurrogate(text[NotifyIconTooltipLimit]))
                return text.Substring(0, NotifyIconTooltipLimit - 1);
            return text.Substring(0, NotifyIconTooltipLimit);
        }

        public static NotifyIcon CreateNotifyIcon(
            Icon icon,
            string tooltip,
            string fallbackTooltip,
            ContextMenu contextMenu)
        {
            if (icon == null)
                throw new ArgumentNullException("icon");
            if (contextMenu == null)
                throw new ArgumentNullException("contextMenu");

            NotifyIcon tray = new NotifyIcon();
            try
            {
                tray.Icon = icon;
                tray.Text = NormalizeTooltip(tooltip, fallbackTooltip);
                tray.ContextMenu = contextMenu;
                tray.Visible = true;
                return tray;
            }
            catch
            {
                tray.Dispose();
                throw;
            }
        }

        public static void AppendHeader(
            ContextMenu menu,
            string productName,
            string versionText)
        {
            if (menu == null)
                throw new ArgumentNullException("menu");
            menu.MenuItems.Add(new MenuItem(
                String.IsNullOrWhiteSpace(productName) ? "AIProjects" : productName)
                { Enabled = false });
            if (!String.IsNullOrWhiteSpace(versionText))
                menu.MenuItems.Add(new MenuItem(versionText) { Enabled = false });
            menu.MenuItems.Add("-");
        }

        public static void UpdateTooltip(
            NotifyIcon tray,
            string tooltip,
            string fallbackTooltip)
        {
            if (tray == null)
                return;
            tray.Text = NormalizeTooltip(tooltip, fallbackTooltip);
        }

        public static void DisposeNotifyIcon(ref NotifyIcon tray)
        {
            NotifyIcon disposing = tray;
            tray = null;
            if (disposing == null)
                return;
            try { disposing.Visible = false; }
            finally { disposing.Dispose(); }
        }

        public static bool TryPromptText(
            string title,
            string prompt,
            string initialValue,
            out string value)
        {
            value = initialValue ?? "";
            using (Form form = new Form())
            using (Label label = new Label())
            using (TextBox text = new TextBox())
            using (Button accept = new Button())
            using (Button cancel = new Button())
            {
                form.Text = String.IsNullOrWhiteSpace(title) ? "AIProjects" : title;
                form.FormBorderStyle = FormBorderStyle.FixedDialog;
                form.StartPosition = FormStartPosition.CenterScreen;
                form.ShowInTaskbar = false;
                form.MinimizeBox = false;
                form.MaximizeBox = false;
                form.ClientSize = new Size(430, 126);

                label.AutoSize = false;
                label.Left = 12;
                label.Top = 12;
                label.Width = 406;
                label.Height = 32;
                label.Text = prompt ?? "Value:";

                text.Left = 12;
                text.Top = 48;
                text.Width = 406;
                text.Text = initialValue ?? "";
                text.SelectAll();

                accept.Text = "OK";
                accept.DialogResult = DialogResult.OK;
                accept.Left = 262;
                accept.Top = 86;
                accept.Width = 75;

                cancel.Text = "Cancel";
                cancel.DialogResult = DialogResult.Cancel;
                cancel.Left = 343;
                cancel.Top = 86;
                cancel.Width = 75;

                form.Controls.Add(label);
                form.Controls.Add(text);
                form.Controls.Add(accept);
                form.Controls.Add(cancel);
                form.AcceptButton = accept;
                form.CancelButton = cancel;

                if (form.ShowDialog() != DialogResult.OK)
                    return false;
                value = text.Text;
                return true;
            }
        }
    }
}

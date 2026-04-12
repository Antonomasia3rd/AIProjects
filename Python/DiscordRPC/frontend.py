import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import configparser
import os
import sys
import shutil  # for backup

CONFIG_FILE = "config.ini"
BACKUP_FILE = "config.ini.bak"

# --------------------------
# Custom Key/Value Dialog
# --------------------------
class KeyValueDialog(simpledialog.Dialog):
    def __init__(self, parent, title, key="", value=""):
        self.key = key
        self.value = value
        super().__init__(parent, title)

    def body(self, master):
        tk.Label(master, text="Keyword:").grid(row=0, column=0, sticky="w")
        self.key_entry = tk.Entry(master, width=40)
        self.key_entry.grid(row=0, column=1, padx=5, pady=5)
        self.key_entry.insert(0, self.key)

        tk.Label(master, text="Replacement:").grid(row=1, column=0, sticky="w")
        self.value_entry = tk.Entry(master, width=40)
        self.value_entry.grid(row=1, column=1, padx=5, pady=5)
        self.value_entry.insert(0, self.value)

        return self.key_entry  # focus on keyword first

    def apply(self):
        self.key = self.key_entry.get().strip()
        self.value = self.value_entry.get().strip()


# --------------------------
# Special List Editor Widget
# --------------------------
class ListEditor(tk.Frame):
    def __init__(self, parent, section, key, initial_value):
        super().__init__(parent)
        self.section = section
        self.key = key

        self.items = []
        for line in initial_value.strip().splitlines():
            if " = " in line:
                k, v = line.split(" = ", 1)
                self.items.append((k.strip(), v.strip()))

        # Main Listbox
        self.listbox = tk.Listbox(self, width=60, height=6)
        self.listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # Double-click edit
        self.listbox.bind("<Double-1>", lambda e: self.edit_item())

        # Buttons
        btns = tk.Frame(self)
        btns.pack(side=tk.RIGHT, fill=tk.Y)

        tk.Button(btns, text="Add", command=self.add_item).pack(fill=tk.X)
        tk.Button(btns, text="Edit", command=self.edit_item).pack(fill=tk.X)
        tk.Button(btns, text="Delete", command=self.delete_item).pack(fill=tk.X)
        tk.Button(btns, text="↑ Up", command=self.move_up).pack(fill=tk.X)
        tk.Button(btns, text="↓ Down", command=self.move_down).pack(fill=tk.X)

        self.refresh()

    def refresh(self):
        self.listbox.delete(0, tk.END)
        for k, v in self.items:
            self.listbox.insert(tk.END, f"{k} → {v}")

    def add_item(self):
        dlg = KeyValueDialog(self, "Add Item")
        if dlg.key:  # only add if not empty
            self.items.append((dlg.key, dlg.value))
            self.refresh()
            self.mark_dirty()

    def edit_item(self):
        sel = self.listbox.curselection()
        if not sel:
            return
        idx = sel[0]
        k, v = self.items[idx]
        dlg = KeyValueDialog(self, "Edit Item", k, v)
        if dlg.key and (dlg.key != k or dlg.value != v):
            self.items[idx] = (dlg.key, dlg.value)
            self.refresh()
            self.listbox.selection_set(idx)
            self.mark_dirty()

    def delete_item(self):
        sel = self.listbox.curselection()
        if not sel:
            return
        idx = sel[0]
        if messagebox.askyesno("Confirm Delete", f"Delete '{self.items[idx][0]}'?"):
            del self.items[idx]
            self.refresh()
            self.mark_dirty()

    def move_up(self):
        sel = self.listbox.curselection()
        if not sel or sel[0] == 0:
            return
        idx = sel[0]
        self.items[idx - 1], self.items[idx] = self.items[idx], self.items[idx - 1]
        self.refresh()
        self.listbox.selection_set(idx - 1)
        self.mark_dirty()

    def move_down(self):
        sel = self.listbox.curselection()
        if not sel or sel[0] == len(self.items) - 1:
            return
        idx = sel[0]
        self.items[idx], self.items[idx + 1] = self.items[idx + 1], self.items[idx]
        self.refresh()
        self.listbox.selection_set(idx + 1)
        self.mark_dirty()

    def mark_dirty(self):
        root = self.winfo_toplevel()
        if hasattr(root, "mark_dirty"):
            root.mark_dirty()

    def get_value(self):
        if not self.items:
            return ""
        return "\n    " + "\n    ".join(f"{k} = {v}" for k, v in self.items)


# --------------------------
# Main Config Editor
# --------------------------
class ConfigEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.base_title = "Config.ini Editor"
        self.title(self.base_title)
        self.geometry("640x480")

        self.dirty = False

        if not os.path.exists(CONFIG_FILE):
            messagebox.showerror("Error", f"{CONFIG_FILE} not found!")
            self.destroy()
            return

        with open(CONFIG_FILE, "r", encoding="utf-8") as f:
            self.raw_lines = f.readlines()

        self.cfg = configparser.ConfigParser()
        self.cfg.optionxform = str
        self.cfg.read(CONFIG_FILE, encoding="utf-8")

        if "gui" not in self.cfg:
            messagebox.showerror("Error", "[gui] section not found in config.ini")
            self.destroy()
            return
        self.gui_settings = dict(self.cfg["gui"])

        self.entries = {}

        # Shortcuts
        self.save_shortcut = self.gui_settings.get("save_shortcut", "<Control-s>")
        self.exit_shortcut = self.gui_settings.get("exit_shortcut", "<Control-q>")
        self.reload_shortcut = self.gui_settings.get("reload_shortcut", "<Control-r>")

        # Toolbar with tooltips
        toolbar = tk.Frame(self, relief=tk.RAISED, bd=1)
        toolbar.pack(side=tk.TOP, fill=tk.X)

        btn_save = tk.Button(toolbar, text=f"Save ({self.save_shortcut})", command=self.save_config)
        btn_save.pack(side=tk.LEFT, padx=2, pady=2)

        btn_reload = tk.Button(toolbar, text=f"Reload Config ({self.reload_shortcut})", command=self.reload_config)
        btn_reload.pack(side=tk.LEFT, padx=2, pady=2)

        btn_exit = tk.Button(toolbar, text=f"Exit ({self.exit_shortcut})", command=self.on_close)
        btn_exit.pack(side=tk.LEFT, padx=2, pady=2)

        self.build_gui()

        # Bind shortcuts
        self.bind(self.save_shortcut, lambda e: self.save_config())
        self.bind(self.exit_shortcut, lambda e: self.on_close())
        self.bind(self.reload_shortcut, lambda e: self.reload_config())

        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def mark_dirty(self, *args):
        if not self.dirty:
            self.title(self.base_title + " *")
        self.dirty = True

    def clear_dirty(self):
        self.dirty = False
        self.title(self.base_title)

    def build_gui(self):
        notebook = ttk.Notebook(self)
        notebook.pack(fill=tk.BOTH, expand=True)

        def make_scrollable_frame(parent):
            container = ttk.Frame(parent)
            canvas = tk.Canvas(container)
            scrollbar = ttk.Scrollbar(container, orient="vertical", command=canvas.yview)
            scrollable_frame = ttk.Frame(canvas)

            scrollable_frame.bind(
                "<Configure>",
                lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
            )

            canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
            canvas.configure(yscrollcommand=scrollbar.set)

            canvas.pack(side="left", fill="both", expand=True)
            scrollbar.pack(side="right", fill="y")
            return container, scrollable_frame

        comments_map = {}
        current_section = None
        last_comment = []

        for line in self.raw_lines:
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                current_section = stripped.strip("[]")
                last_comment = []
            elif stripped.startswith("#"):
                last_comment.append(stripped.lstrip("#").strip())
            elif "=" in line and current_section:
                key = line.split("=", 1)[0].strip()
                if last_comment:
                    comments_map[(current_section, key)] = "\n".join(last_comment)
                last_comment = []

        for section in self.cfg.sections():
            if section == "gui":
                continue

            frame = ttk.Frame(notebook)
            notebook.add(frame, text=section)
            container, inner_frame = make_scrollable_frame(frame)
            container.pack(fill=tk.BOTH, expand=True)

            row = 0
            for key, val in self.cfg[section].items():
                field_type = self.gui_settings.get(key, "text")

                tk.Label(inner_frame, text=key, anchor="w").grid(row=row, column=0, sticky="w", padx=5, pady=5)

                if field_type == "bool":
                    var = tk.BooleanVar(value=(val.lower() == "true"))
                    var.trace_add("write", self.mark_dirty)
                    entry = tk.Checkbutton(inner_frame, variable=var)
                    entry.var = var
                    entry.grid(row=row, column=1, sticky="w", padx=5, pady=5)

                elif field_type == "number":
                    var = tk.StringVar(value=val)
                    var.trace_add("write", self.mark_dirty)
                    entry = tk.Entry(inner_frame, textvariable=var, width=15)
                    entry.var = var
                    entry.grid(row=row, column=1, sticky="w", padx=5, pady=5)

                elif field_type == "list":
                    entry = ListEditor(inner_frame, section, key, val)
                    entry.grid(row=row, column=1, sticky="w", padx=5, pady=5)

                else:
                    var = tk.StringVar(value=val)
                    var.trace_add("write", self.mark_dirty)
                    entry = tk.Entry(inner_frame, textvariable=var, width=50)
                    entry.var = var
                    entry.grid(row=row, column=1, sticky="w", padx=5, pady=5)

                comment = comments_map.get((section, key))
                if comment:
                    lbl = tk.Label(inner_frame, text=comment, anchor="w", justify="left", fg="gray")
                    lbl.grid(row=row+1, column=0, columnspan=2, sticky="w", padx=20, pady=2)
                    row += 1

                self.entries[(section, key)] = entry
                row += 1

    def save_config(self):
        try:
            new_lines = []
            current_section = None
            skip_multiline = False

            for line in self.raw_lines:
                stripped = line.strip()
                if stripped.startswith("[") and stripped.endswith("]"):
                    current_section = stripped.strip("[]")
                    new_lines.append(line)
                    skip_multiline = False
                    continue

                if "=" in line and current_section:
                    key = line.split("=", 1)[0].strip()
                    entry_widget = self.entries.get((current_section, key))

                    if isinstance(entry_widget, ListEditor):
                        val = entry_widget.get_value()
                        new_lines.append(f"{key} = {val}\n")
                        skip_multiline = True
                        continue

                    elif entry_widget:
                        val = entry_widget.var.get()
                        if isinstance(val, bool):
                            val = "true" if val else "false"
                        else:
                            val = str(val)

                        prefix, sep, _ = line.partition("=")
                        new_lines.append(f"{prefix}{sep} {val}\n")
                        skip_multiline = False
                        continue

                if skip_multiline and (line.startswith(" ") or line.strip() == ""):
                    continue
                else:
                    skip_multiline = False

                new_lines.append(line)

            if os.path.exists(CONFIG_FILE):
                shutil.copy(CONFIG_FILE, BACKUP_FILE)

            with open(CONFIG_FILE, "w", encoding="utf-8") as f:
                f.writelines(new_lines)

            self.clear_dirty()
            messagebox.showinfo("Success", f"Saved changes to {CONFIG_FILE}\nBackup created: {BACKUP_FILE}")

        except Exception as e:
            messagebox.showerror("Error", f"Failed to save: {e}")

    def reload_config(self):
        if self.dirty and not messagebox.askyesno("Discard changes?", "Reloading will discard unsaved changes. Continue?"):
            return
        self.destroy()
        new_app = ConfigEditor()
        new_app.mainloop()

    def on_close(self):
        if self.dirty:
            if messagebox.askyesno("Unsaved Changes", "Save changes before exiting?"):
                self.save_config()
        self.destroy()


if __name__ == "__main__":
    app = ConfigEditor()
    app.mainloop()

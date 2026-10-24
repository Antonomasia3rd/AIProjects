# ExplorerPatcher input-dialog reference

The inspected ExplorerPatcher checkout is commit `0a88a6e0ef6b1752fea36e581cffff1097e862b0`. Its `ep_gui/GUI.c` text-setting action calls `InputBox(FALSE, ...)`; `ExplorerPatcher/utility.c` implements that wrapper by asking the installed Windows VBScript engine to evaluate its native InputBox. The ExplorerPatcher hook adds a modal frame and, for sensitive input, requests a black-circle password mask. ExplorerPatcher does not define the visible prompt, edit and button geometry in `GUI.c`.

The installed reference was inspected as PE data and disassembly only. No script engine, upstream executable or native InputBox was executed. The module is `C:\Windows\System32\vbscript.dll`, version `10.0.26100.4768`, SHA-256 `49DB739DB59D04D5B878D67623F3C8248C8813857A63BD31E813B71B2CFFBAE7`.

Its English dialog resource 102 declares a 238 × 79 dialog-unit window with an 8-point `MS Shell Dlg` font. Its visible controls are:

| Control | x | y | width | height |
|---|---:|---:|---:|---:|
| Edit, first tab stop | 7 | 58 | 224 | 14 |
| OK, default button | 181 | 6 | 50 | 14 |
| Cancel | 181 | 24 | 50 | 14 |
| Wrapped prompt | 7 | 6 | 159 | 48 |

A Help control is also present in the resource. The native setup path at preferred-image VA `0x1800767D8`–`0x18007680A` hides and disables it when the caller does not provide help information, as in ExplorerPatcher's text-setting call. The InputBox construction call at `0x180075FE8` selects resource 102. Its setup routine returns the ordinary dialog-manager initialization result, preserving the first edit control's default-response selection. The placement routine at `0x180076A8C`–`0x180076B2D` centers the default box horizontally and positions it approximately one-third down the desktop. A long prompt increases its static control's height and moves the bottom edit control down.

DesktopStub now implements those visible layout facts independently in `dependencies/DesktopStub/ga_text_input_dialog.inc`. It does not embed or execute ExplorerPatcher's scripting wrapper. The existing public input functions, translations, empty-value validation and longer text support remain part of DesktopStub. Callback state is now owned by each dialog through `DWLP_USER`; opening or destroying another dialog cannot replace a surviving dialog's target string.

The native module has additional font branches for selected non-Latin script/localization cases. This pass establishes the installed English reference and default visible arrangement; it does not claim identical pixels for every language, system-font substitution, theme, accessibility setting or shell modification. Windows maps the retained dialog units using the actual dialog font and DPI.

Local inspection receipts are under the ignored `build/review-validation/` folder: `vbscript-input-dialog-contract-oct24.json`, `vbscript-input-dialog-resources-oct24.json`, `vbscript-input-proc-oct24.txt` and `vbscript-disasm-oct24.txt`. The focused hidden-window fixture checks template dimensions, default selection, tab order, font-based geometry, password masking, long-prompt expansion, DPI-independent placement arithmetic and parallel-dialog state isolation. It neither shows a modal prompt nor modifies the clipboard.

The related documented default placement is also described by Microsoft's [InputBox reference](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/inputbox-function). The installed VBScript module's own placement branch is the evidence used here.

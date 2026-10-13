# Original-binary renderer observation, 2026-10-13

The exact four-pixel raster variation remains unresolved. The current normal
product's full offline smoke passed the unchanged comparisons once; that is not
proof that the historical intermittency is repaired.

An independent code review found no retained Font/FontFamily/Brush/StringFormat,
uninitialized layout region, unsafe GDI+ lifetime, or unsupported secondary text
being drawn in the opaque medium path. A diagnostic probe therefore observed
native GDI+ arguments in unchanged original executable copies rather than
recompiling renderer logging.

The copied children used only synthetic bitmap/INI inputs and `--render-only`.
Both originals and the helper were scanned first. Four hardware execution
breakpoints observed font-family/font/brush creation and GdipDrawString; target
instructions were compared against their original bytes. There were no injected
calls, memory patches, native provider operations, package operations, or
attachments to existing user processes. Each fresh child was held in a
kill-on-close Job, with a 30-second internal and 45-second outer deadline.

Two initial controls stopped before GDI+ observation because the debug engine's
process handle lacked PROCESS_SET_QUOTA. Its actual GrantedAccess was
`0x0012367b`, while Job assignment requires SET_QUOTA (`0x100`) and TERMINATE
(`0x1`). A revised callback opened only the newly created debuggee's PID with
those rights, checked its image against the unique fixture executable, and
assigned it successfully. It did not weaken containment or break out of a Job.
The setup failures are excluded from renderer results; their children ended.

Four successful controls tested `IIII`/`MMMM` secondary text on both:

- Original settings build: SHA-256
  `974D3FB8566C6AD574A5D548663B0B268B895ABA6D58082DA53324925ED56E83`.
- Historical recurrence build: SHA-256
  `CBE389F196A9CC4798B579F42C33F6B4479B1648F94888FE71083E567E09C4CF`.

All four generated identical medium PNGs, SHA-256
`3A5DD82F98E761156AEC6FDD6326694F8724C18E76A7A38193B0B1412AE7F3C1`.
Both pairs have zero different decoded pixels. The historical positions
`(20,93)/(30,93)/(40,93)/(50,93)` all contain the darker
`156,203,239,255` value; the lighter failing state was not captured.

The observed primary draws use Segoe UI, 15-pixel size (`0x41700000`), regular
style, pixel unit, solid-white brush, rectangle bits
`41200000,42a00000,43020000,419fb07b`, MXCSR `0x1f80` and x87 word `0x027f`.
Object addresses vary as expected across processes. Every child completed in
3.1–4.4 seconds with four draw events and unchanged export instructions.

Native font identity/parameters are now observed on the darker state without
changing the executable. DebugPort, timing and hardware trap registers still
change under a debugger; these controls did not reproduce the entire full-smoke
prehistory. No causal fix or tolerance change follows from this result.

The ignored helper, design review, bounded logs, fixture PNGs and pixel inventory
remain in `build/review-validation/render-debug-probe-oct13`. Further work should
capture the lighter state with matching native arguments before changing opaque
renderer behavior. The original artifacts remain preserved.

# DesktopStub text layout audit

Updated: 2026-09-08. Renderer and test changes completed; remaining OS/UI validation is listed below.

## Scope and current state

The working tree already contained uncommitted tile layout extraction, declarative region data, and portable tests. Those changes are preserved. This audit owns the renderer, its portable layout model, and isolated render tests; source composition and configuration changes belong to the main repository audit.

- Extracted the existing production GDI+ drawing functions from `dependencies/DesktopStub/ga_image.inc` to `dependencies/DesktopStub/tile_text_render.inc`. The application and the new isolated render test include the same functions.
- Added `DesktopStub/tools/TileTextRenderTests.cpp`. It creates in-memory bitmaps and a PNG contact sheet without starting DesktopStub, changing its INI, registering a package, or publishing notifications.
- **Fixed missing glyphs:** the previous renderer produced 110 field-visibility failures in the new matrix. Medium/wide badge text disappeared at 150%, 180%, 200%, and 400% scale because its 67-pixel rectangle was smaller than the actual line metrics of its 54-pixel font. The 19-pixel wide secondary-text rectangle also suppressed long and multilingual text. The rectangles now provide enough height; every selected field has visible glyphs throughout the matrix.
- **Fixed line limits:** the old `DrawSystemTileText` used `maxLines` only to toggle wrapping. Drawing now caps height using `Font::GetHeight`, including explicit newlines. Large title/body regions were enlarged to accommodate their two/three declared lines. Tests render excess lines into every declared region at every tested scale and verify the exact number of visible lines.
- **Shared font and drawing settings:** production fonts now use the portable model's sizes. Production and tests use the same graphics-configuration function. PNG text uses grayscale antialiasing, avoiding baked RGB ClearType fringes when the image is resized.
- **Explicit content overlays retain their background:** `Content.TextMode=Overlay` now passes an optional `preserveBackground` flag through the production renderer. It skips preset background replacement/darkening, so wallpaper/custom images remain under the text at all three sizes. The default flag is false, preserving the historical preset simulation for Auto/legacy callers.
- **Portable coverage:** the layout suite now includes empty input, invalid field routing, duplicate fields, and positive region dimensions. `TileTextTemplateTests.cpp` includes the actual preset XML generator with in-memory host getters; it compares its field routing with the static layout for all input combinations, checks image requirements, disabled-size behavior, fallback names, documented block IDs, and failure without partial XML output. It does not substitute a mock implementation of the generator.
- **Test runner error propagation:** the PowerShell 7 branch in `tools/TestTileTextLayout.cmd` expanded `%ERRORLEVEL%` before running PowerShell. It now branches to a separate label so a failing layout check propagates its exit code.

## Build environment

Native Windows with Visual Studio 2019 Build Tools and Windows SDK 10.0.19041.0 is available at `C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools`. PowerShell 7 is available. No WSL is required for portable tests. Linux/macOS execution has not been verified here; no platform tools were installed for this audit.

## Validation results

- `DesktopStub\TestDesktopStubSource.cmd` passed after integration: 1,716 source checks, 236 compiled portable layout checks, and 494 compiled preset XML checks. Its portable PowerShell specification check also passed. This source-suite run preceded the parent agent's later source-composition edits; the parent audit owns final integration validation.
- The final native renderer test passed **18,778 checks, zero failures** after adding the line-capacity, stress, and explicit-overlay tests. It includes 441 full-tile renders: three tile sizes, all seven nonempty input masks, seven scales (80%, 100%, 125%, 150%, 180%, 200%, 400%), and short/long/multilingual text. Multilingual checks establish glyph visibility, not language-specific typographic correctness.
- 294 additional preset/explicit-overlay renders cover every size, nonempty mask, and scale in both modes. Colored image samples along both full vertical edges verify that Overlay preserves the image, Auto keeps historical fill/darkening behavior, and all selected text remains visible. Latest output is `DesktopStub/build/tile-render-final.log`.
- All 35 declarative regions render their advertised line count at all seven scales. Oversized-rectangle tests independently check limits one through four.
- 600 additional changing-source render cycles passed. GDI handles remained **3 → 3** and USER handles **4 → 4**. This verifies those handle counters; it is not a full heap-leak or long-running application stability audit.
- The generated `DesktopStub/build/tile-render-smoke/tile-text-render-matrix.png` was visually inspected: all seven input combinations, readable wrapped/truncated strings, both wide lines, and block captions. The artifact and logs live under the ignored build directory and can be regenerated.
- `git diff --check` passed for the modified tracked layout/test files.
- An isolated copy of the `.cmd` wrapper correctly returned **37** from an intentionally failing PowerShell script, confirming its corrected failure propagation without modifying the production specification.

Run the integrated Windows suite with:

```bat
DesktopStub\TestDesktopStubSource.cmd
```

The layout/actual-template tests are standard C++17 without Windows headers. From the repository root on a system with CMake and a compiler:

```sh
cmake -S tools/tile-tests -B build/tile-tests
cmake --build build/tile-tests --config Release
ctest --test-dir build/tile-tests -C Release --output-on-failure
```

On Windows that CMake project also adds the native GDI+ renderer test. The CMake entry point itself was not executed in this environment; the same C++ files were compiled and executed with MSVC through the Windows suite. Without CMake, compile `DesktopStub/tools/TileTextLayoutTests.cpp` and `DesktopStub/tools/TileTextTemplateTests.cpp` individually using `g++ -std=c++17` or `clang++ -std=c++17`. For compiler-free geometry checks on any system with PowerShell 7, run `pwsh -NoProfile -File tools/TestTileTextLayout.ps1`.

## Authoritative references

- [Microsoft tile template catalog](https://learn.microsoft.com/en-us/previous-versions/windows/apps/hh761491(v=win.10)): template field structure and example XML.
- [Microsoft TileTemplateType reference](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tiletemplatetype): confirms header/body wrapping limits, block text slots, background images, and that branding can change visible text. The simulated layout is an approximation, not a claim of exact OS pixel placement. Windows Phone behavior differs and is outside the desktop target.
- [Microsoft notification queue API](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tileupdater.enablenotificationqueue): up to five queued tile notifications; Windows times peek animation so all content can be shown. This page does not promise a ten-second application-controlled cycle. Parent audit handles cycle scheduling and warnings.

## Remaining limits and quirks

- This is actual offscreen UI-render coverage, not a Start screen, tray, configuration-dialog, or installed-package UI test. Native notification rendering, Windows branding/logo placement, right-to-left shell mirroring, and Windows 8/8.1/10 visual comparison still need tests on those OS versions. No installed application or user settings were changed by this test.
- The static simulation follows the selected Windows 8/8.1 preset structures. It does not emulate every Windows 10 adaptive template. Exact pixel parity with the OS is not claimed.
- Medium block templates have only a block plus one caption. With primary, secondary, and badge configured, secondary is intentionally omitted there; wide and large can show all three. This is covered by both layout and actual-template tests.
- In Auto/legacy preset simulation, medium text/block and wide block presets replace the wallpaper with a solid background, as those templates do. Wide image-plus-text retains the image above a bottom band, and large image-overlay templates retain/darken the image. Explicit `Content.TextMode=Overlay` preserves the original image instead; it is an application composition choice, not native preset pixel parity.
- Native tile branding can hide or narrow text in ways this bitmap simulation does not reproduce. An OS comparison should include branding disabled/enabled and all three supported sizes.
- Generated-asset state already incorporates the executable fingerprint (`BuildGenerationStateKey`), so a rebuilt executable invalidates renderer caches without deleting user cache files. Parent source-composition changes also add their own state.

## Read-only content engine review, sent to parent audit

These findings concern the parent agent's new files; this subtask did not edit them. The parent reported fixing all three on 2026-09-08; the main audit owns integration validation of those fixes.

- **Boolean inconsistency:** `CompositionRequested` used `IniReadI` for `[Content] Enabled`, while the new validator accepts true/yes/on/enabled. `IniReadI` uses integer parsing only. Therefore `Enabled=true` could validate successfully but never activate composition or its render snapshot. Use the shared content boolean parser at the routing boundary.
- **Refresh interval changes can retain an old long deadline:** SMTC `nextRefresh` and background `nextBackgroundRead` did not reset when `Content.RefreshSeconds` changes. A change from 86400 to 2 seconds can keep old data for almost a day. Include the interval in scheduling invalidation.
- **Disable/re-enable transition retains the old frame:** normal polling bypasses `ContentEngineTick` when composition is disabled, so the tick's disabled-state `lastFrame.clear()` branch does not run. Re-enabling unchanged static content can suppress generation after an ordinary wallpaper update because the old frame key remains recorded. Reset scheduling state when composition is disabled or on the next enabled transition.
- Snapshot publication/copying uses a mutex and the actual generation scope keeps one copy for text/background consistency. Initial main-thread tick runs before the poll thread starts; normal ticking is serialized by that lifecycle. No definite concurrent tick race was found in the inspected call sites.

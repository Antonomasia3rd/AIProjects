"""Assemble schema 4 dumper from the supplied 0.3.0 mod. Output is standalone."""
import argparse
import json
import re
from pathlib import Path
from settings_formats import export_settings

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, default=ROOT / 'sources')
    args = parser.parse_args()
    source = (args.reference_dir / "lockapp-xaml-dumper.wh.cpp").read_text(encoding="utf-8-sig")
    source = source.replace("// @version         0.3.0", "// @version         0.5.0")
    source = source.replace("#include <cwchar>", "#include <cwchar>\n#include <iomanip>\n#include <locale>\n#include <memory>\n#include <deque>\n#include <utility>")
    source = source.replace("#include <winrt/Windows.UI.Xaml.Media.h>", """#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>""")
    source = source.replace("- logFallback: false", """- interactionTraceSeconds: 0
  $name: Interaction trace duration (seconds)
  $description: Optional. After the first delayed snapshot, sample selected clock, Spotlight and media properties. Only changed samples are written. Use 60 for hover and swipe investigations; 0 disables.
- interactionTraceIntervalMs: 100
  $name: Interaction sample interval (ms)
  $description: 100 is recommended. This is a sampled trace, not a frame-exact animation recording.
- periodicSnapshotIntervalMs: 0
  $name: Additional snapshot interval (ms)
  $description: Optional periodic snapshots after startup, within the existing maximum snapshot cap. Use 10000 to inspect loaded hover templates.
- logFallback: false""", 1)
    source = source.replace("    bool logFallback = false;", """    int interactionTraceSeconds = 0;
    int interactionTraceIntervalMs = 100;
    int periodicSnapshotIntervalMs = 0;
    bool logFallback = false;""", 1)
    source = source.replace("    g_settings.logFallback =", """    // Keep the user's cold-start minimum authoritative, even if the timeout
    // setting is accidentally lower than that minimum.
    g_settings.maxSnapshotWaitMs = std::max(g_settings.maxSnapshotWaitMs, g_settings.initialSnapshotDelayMs);
    g_settings.interactionTraceSeconds = std::clamp(Wh_GetIntSetting(L"interactionTraceSeconds"), 0, 300);
    g_settings.interactionTraceIntervalMs = std::clamp(Wh_GetIntSetting(L"interactionTraceIntervalMs"), 50, 1000);
    g_settings.periodicSnapshotIntervalMs = std::max(0, Wh_GetIntSetting(L"periodicSnapshotIntervalMs"));
    g_settings.logFallback =""", 1)
    source = source.replace("    std::string result(static_cast<size_t>(bytes), '\\0');", "    std::string result(static_cast<size_t>(bytes), '\\0');")
    details = (ROOT / "tools/dumper_details.inc").read_text(encoding="utf-8")
    source = source.replace("std::wstring FormatPropertyValue(", details + "\n\nstd::wstring FormatPropertyValue(", 1)
    source = source.replace("ss << L\" {Color=\" << color << L\"}\";",
                            "ss << L\" {Color=\" << color << L\", Opacity=\" << brush.Opacity() << L\"}\";", 1)
    source = source.replace("        } else if (auto grid = obj.try_as<wuxc::Grid>()) {", """        } else if (obj.try_as<wuxm::Brush>() || obj.try_as<wuxm::Transform>()) {
            ss << L" {details=" << DescribeDumpValue(obj) << L"}";
        } else if (auto grid = obj.try_as<wuxc::Grid>()) {""", 1)
    source = source.replace('L"Content", L"HorizontalOffset", L"VerticalOffset"',
                            'L"Content", L"HorizontalOffset", L"VerticalOffset", L"OpticalMarginAlignment",\n        L"TextLineBounds", L"IsTextScaleFactorEnabled", L"BaselineOffset", L"Orientation",\n        L"Spacing", L"IsEnabled", L"UseLayoutRounding", L"TextReadingOrder"')
    source = source.replace('\\"schema\\":3', '\\"schema\\":4')
    source = source.replace('           << L"}}";', '''           << L",\\"interactionTraceSeconds\\":" << g_settings.interactionTraceSeconds
           << L",\\"interactionTraceIntervalMs\\":" << g_settings.interactionTraceIntervalMs
           << L",\\"periodicSnapshotIntervalMs\\":" << g_settings.periodicSnapshotIntervalMs
           << L"}}";''', 1)
    # Index unnamed siblings and protect a damaged diagnostic ancestry from loops.
    source = source.replace("        while (current) {", "        while (current && components.size() < 256) {", 1)
    source = source.replace("            components.push_back(std::move(component));", """            if (meta.name.empty()) {
                component += L"[" + std::to_wstring(meta.childIndex + 1) + L"]";
            }
            components.push_back(std::move(component));""", 1)
    begin = source.index("            auto p = uiElement.TransformToVisual(nullptr).TransformPoint({0, 0});")
    end = source.index("        } catch (...) {", begin)
    source = source[:begin] + '''            auto bounds = uiElement.TransformToVisual(nullptr).TransformBounds(
                {0, 0, static_cast<float>(frameworkElement.ActualWidth()),
                 static_cast<float>(frameworkElement.ActualHeight())});
            ss << L"{\\"x\\":"; AppendDumpNumber(ss, bounds.X);
            ss << L",\\"y\\":"; AppendDumpNumber(ss, bounds.Y);
            ss << L",\\"width\\":"; AppendDumpNumber(ss, bounds.Width);
            ss << L",\\"height\\":"; AppendDumpNumber(ss, bounds.Height);
            ss << L"}";
''' + source[end:]
    source = source.replace('            ss << L",\\"source\\":" << static_cast<int>(source)',
                            '            ss << L",\\"overridden\\":" << (p.Overridden ? L"true" : L"false")\n               << L",\\"source\\":" << static_cast<int>(source)', 1)
    # Serialize complete optional sections transactionally. An exception during
    # group traversal must not leave malformed JSON with half an open object.
    begin = source.index("    void AppendVisualStates(")
    end = source.index("    void RecordAdd(", begin)
    source = source[:begin] + '''    void AppendVisualStates(std::wostringstream& ss, wf::IInspectable const& obj) {
        ss << L",\\"visualStateGroups\\":";
        try {
            auto fe = obj.try_as<wux::FrameworkElement>();
            auto groups = fe && g_settings.includeVisualStates ? VisualStateDumpDetails(fe) : L"[]";
            ss << groups;
        } catch (...) {
            ss << L"[],\\"visualStatesHresult\\":" << static_cast<long>(winrt::to_hresult());
        }
    }

''' + source[end:]
    source = source.replace("    void SnapshotSchedulerLoop() {", (ROOT / "tools/dumper_trace.inc").read_text(encoding="utf-8") + "\n\n    void SnapshotSchedulerLoop() {", 1)
    source = source.replace("WaitForSingleObject(m_stopEvent, 250)", "WaitForSingleObject(m_stopEvent, g_settings.interactionTraceSeconds ? std::min(250, g_settings.interactionTraceIntervalMs) : 250)", 1)
    source = source.replace("            const uint64_t generation =", """            const ULONGLONG traceNow = GetTickCount64();
            if (m_traceStartTick && g_settings.interactionTraceSeconds &&
                traceNow - m_traceStartTick < static_cast<ULONGLONG>(g_settings.interactionTraceSeconds) * 1000 &&
                traceNow >= m_nextTraceTick) {
                TakeInteractionTrace();
                m_nextTraceTick = GetTickCount64() + g_settings.interactionTraceIntervalMs;
            }
            const uint64_t generation =""", 1)
    source = source.replace("            if (shouldSnapshot) {", """            if (!shouldSnapshot && m_snapshotCount && g_settings.periodicSnapshotIntervalMs > 0 &&
                now - m_lastSnapshotTick >= static_cast<ULONGLONG>(g_settings.periodicSnapshotIntervalMs)) {
                shouldSnapshot = true;
                reason = L"periodic";
            }
            if (shouldSnapshot) {""", 1)
    source = source.replace("        ++m_snapshotCount;", "        if (!m_snapshotCount) m_traceStartTick = GetTickCount64();\n        ++m_snapshotCount;", 1)
    source = source.replace("        request.attempted = static_cast<unsigned int>(handles.size());", """        RefreshSnapshotRelations(handles);
        m_traceHandles.clear();
        request.attempted = static_cast<unsigned int>(handles.size());""", 1)
    source = source.replace("                AppendRectangle(ss, obj);", """                if (IsInteractionTarget(meta, path)) m_traceHandles.push_back(handle);
                AppendRectangle(ss, obj);
                try {
                    auto details = RuntimeDumpDetails(obj);
                    ss << L",\\"effective\\":" << details;
                } catch (...) {
                    ss << L",\\"effective\\":null,\\"effectiveHresult\\":" << static_cast<long>(winrt::to_hresult());
                }""", 1)
    source = source.replace("void Wh_ModSettingsChanged() {\n    LoadSettings();", "void Wh_ModSettingsChanged() {\n    // Settings remain immutable while the UI and scheduler threads use them.")
    source = source.replace("    // Snapshot timing and filtering settings are read live, but restart/reload\n    // the mod for a fresh, self-consistent capture file.", "    // Reload the mod after changing settings to start a new consistent file.")
    source = source.replace("This mod does not change XAML properties.", """Schema 4 adds brush opacity, evaluated text, optical margins, transform values,
DPI scale, storyboard/setter details and refreshed sibling indices. Optional
interaction traces sample only selected controls after the initial delay; they
do not run full property-chain snapshots on every sample. A 100 ms trace is not
a frame-exact recording. Native code-only animations may not appear as XAML
storyboards. Changing settings requires reloading the mod.

This mod does not change XAML properties.""")
    source = source.replace("Include XAML Diagnostics properties for each added element.", "Include XAML Diagnostics properties in settled snapshots.")
    # Continuous recording is event-driven after the initial settled baseline.
    source = source.replace('- logFallback: false\n  $name: Chunk records to Windhawk log if file output fails\n  $description: Debug fallback only. Leave off for normal lock-screen captures because logging thousands of property characters can stall LockApp.', '''- continuousCapture: true
  $name: Continuously record XAML changes to the file
  $description: After the delayed baseline, subscribe to public appearance/input properties, sizes and visual states. No periodic full-tree scan; composition-only frames are not guaranteed notifications. Reload after changing settings.
- maxFileSizeMB: 256
  $name: Maximum capture file size (MiB)
  $description: Stop recording at this size. Records never fall back to the Windhawk log; only errors are logged.''')
    source = source.replace('    bool logFallback = false;', '    bool continuousCapture = true;\n    int maxFileSizeMB = 256;')
    source = source.replace('    g_settings.logFallback = Wh_GetIntSetting(L"logFallback") != 0;',
        '    g_settings.continuousCapture = Wh_GetIntSetting(L"continuousCapture") != 0;\n    g_settings.maxFileSizeMB = std::clamp(Wh_GetIntSetting(L"maxFileSizeMB"), 8, 4096);')
    start=source.index('class DumpWriter {')
    end=source.index('DumpWriter g_writer;',start)+len('DumpWriter g_writer;')
    source=source[:start]+(ROOT/'tools/dumper_writer.inc').read_text(encoding='utf-8')+source[end:]
    source=source.replace('class VisualTreeWatcher\n',(ROOT/'tools/dumper_live.inc').read_text(encoding='utf-8')+'\nclass VisualTreeWatcher\n',1)
    source=source.replace('    std::vector<InstanceHandle> m_traceHandles;',
        (ROOT/'tools/dumper_live_watcher.inc').read_text(encoding='utf-8')+'\n    std::vector<InstanceHandle> m_traceHandles;',1)
    source=source.replace('        std::lock_guard<std::mutex> lock(m_elementsMutex);\n        m_elements.erase(element.Handle);',
        '        RemoveLiveElement(element.Handle);\n        std::lock_guard<std::mutex> lock(m_elementsMutex);\n        m_elements.erase(element.Handle);',1)
    # Append subscriptions only after initial delay. Construction-time adds stay cheap.
    source=source.replace('            RecordAdd(relation, element);','            RecordAdd(relation, element);\n            TrackLiveElement(element.Handle);',1)
    source=source.replace('        StopSnapshotScheduler();\n\n        HRESULT hr',
        '        StopSnapshotScheduler();\n        StopLiveCapture();\n\n        HRESULT hr',1)
    source=source.replace('    ~VisualTreeWatcher() {\n        StopSnapshotScheduler();\n    }',
        '    ~VisualTreeWatcher() {\n        StopSnapshotScheduler();\n        StopLiveCapture();\n    }',1)
    source=source.replace('        g_writer.WriteBatch(batch);','        g_writer.WriteBatch(std::move(batch));\n        if (!m_snapshotCount) StartLiveCapture(coreWindow);',1)
    source=source.replace('                TakeScheduledSnapshot(generation, reason);','                TakeScheduledSnapshot(generation, reason);\n                if (g_settings.continuousCapture && m_snapshotCount) break;',1)
    source=source.replace('\\"schema\\":4','\\"schema\\":5')
    for event in ['add', 'remove']:
        old='L"{\\"event\\":\\"'+event+'\\",\\"handle\\":"'
        new='L"{\\"event\\":\\"'+event+'\\",\\"elapsedMs\\":" << GetTickCount64()-m_startedTick << L",\\"handle\\":"'
        source=source.replace(old,new)
    for event in ['snapshotBegin', 'snapshotEnd']:
        old='L"{\\"event\\":\\"'+event+'\\",\\"sequence\\":"'
        new='L"{\\"event\\":\\"'+event+'\\",\\"elapsedMs\\":" << GetTickCount64()-m_startedTick << L",\\"sequence\\":"'
        source=source.replace(old,new)
    source=source.replace('           << L"}}";', '''           << L",\\"continuousCapture\\":" << (g_settings.continuousCapture ? L"true" : L"false")
           << L",\\"maxFileSizeMB\\":" << g_settings.maxFileSizeMB
           << L"}}";''',1)
    source=source.replace('        Wh_Log(L"XAML snapshot %u complete:', '        if (request.failures) Wh_Log(L"XAML snapshot %u completed with errors:',1)
    for line in ['        Wh_Log(L"LockApp CoreWindow created: %p", hWnd);',
                 '    Wh_Log(L"LockApp XAML Dumper initializing");',
                 '        Wh_Log(L"Existing LockApp CoreWindow: %p", hWnd);',
                 '    Wh_Log(L"LockApp XAML Dumper uninitializing");',
                 '    Wh_Log(L"LockApp XAML Dumper settings changed; reload LockApp/mod for a fresh dump");']:
        source=source.replace(line,'')
    source=source.replace('    UninitializeTap();\n}', '    UninitializeTap();\n    g_writer.Close();\n}',1)
    readme_start=source.index('// ==WindhawkModReadme==')
    readme_end=source.index('// ==/WindhawkModReadme==',readme_start)
    source=source[:readme_start]+'''// ==WindhawkModReadme==
/*
# LockApp XAML Dumper 0.5.0

Schema 5 records one delayed full baseline, then public appearance/input property
changes, brush/transform changes, size events, visual-state transitions and tree
add/remove events continuously. Changes are queued to a background file writer.
There is no repeating full-tree scan in continuous mode and no dump-to-log
fallback. Only errors go to the Windhawk log. A 32-MiB queue and configurable file
limit bound output; dropped records and file-limit stops are marked in JSONL.

Output: the newest LockApp-XamlDump-*.jsonl in LockApp's ApplicationData LocalState
folder. Use the supplied capture preferences for the intentional 10-second minimum
initial delay. Reload the mod/process after changing settings. Disable
continuousCapture to use the earlier scheduled snapshot/optional sampling mode.

This is a change stream, not a per-frame renderer capture. Composition-only
animations, private CLR properties and every descendant's movement do not all
have public change notifications. No XAML properties or native actions are changed.
*/
'''+source[readme_end:]
    source = re.sub(r"(?m)^( +)std::wostringstream (ss|begin|end|record);$",
                    r"\1std::wostringstream \2;\n\1\2.imbue(std::locale::classic());\n\1\2 << std::setprecision(9);", source)
    (ROOT / "builds").mkdir(exist_ok=True)
    (ROOT / "builds/lockapp-xaml-dumper.wh.cpp").write_text(source, encoding="utf-8")
    settings = {"includeProperties": True, "styleRelevantPropertiesOnly": True,
                "includeVisualStates": True, "includeRectangle": True, "settleDelayMs": 3000,
                "initialSnapshotDelayMs": 10000, "maxSnapshotWaitMs": 20000,
                "verificationSnapshotDelayMs": 0, "maxSnapshots": 1,
                "interactionTraceSeconds": 0, "interactionTraceIntervalMs": 100,
                "periodicSnapshotIntervalMs": 0, "continuousCapture": True, "maxFileSizeMB": 256}
    export_settings(settings, ROOT / 'preferences', 'lockapp-xaml-dumper.capture', 'lockapp-xaml-dumper.capture.preferences.yaml')
    print("Prepared dumper 0.5.0 and continuous file-only capture profile")


if __name__ == "__main__":
    main()

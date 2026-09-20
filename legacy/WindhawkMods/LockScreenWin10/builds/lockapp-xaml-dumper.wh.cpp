// ==WindhawkMod==
// @id              lockapp-xaml-dumper
// @name            LockApp XAML Dumper
// @description     Dump LockApp.exe's Windows.UI.Xaml visual tree, properties, geometry and visual states to JSONL for offline comparison.
// @version         0.5.0
// @author          generated with ChatGPT, based on UWPSpy and the Windows 11 Lock Screen Styler TAP approach
// @include         LockApp.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -Wl,--export-all-symbols
// ==/WindhawkMod==

// Derived in part from GPLv3 code in UWPSpy and the Windows 11 Lock Screen
// Styler / Notification Center Styler. Keep this mod under GPLv3 if you
// redistribute a version containing those derived portions.

// ==WindhawkModReadme==
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
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- includeProperties: true
  $name: Include properties
  $description: Include XAML Diagnostics properties in settled snapshots.
- styleRelevantPropertiesOnly: true
  $name: Only style-relevant properties
  $description: Recommended. Filters out automation, input, diagnostic and other noisy properties while keeping layout, typography, brushes, geometry and common Styler properties. Turn this off only for a full UWPSpy-like property-chain dump.
- includeVisualStates: true
  $name: Include visual states
  $description: Enumerate VisualStateManager groups and mark the current state.
- includeRectangle: true
  $name: Include rectangle
  $description: Record the element's rectangle relative to the visual root when available.
- settleDelayMs: 3000
  $name: Settle delay (ms)
  $description: Wait this long after the most recent tree/state change before taking a snapshot.
- initialSnapshotDelayMs: 6000
  $name: Minimum initial snapshot delay (ms)
  $description: Never take the first full snapshot earlier than this after the XAML diagnostics watcher starts.
- maxSnapshotWaitMs: 15000
  $name: Maximum snapshot wait (ms)
  $description: Force a pending snapshot after this long even if LockApp never becomes fully quiet.
- verificationSnapshotDelayMs: 5000
  $name: Verification snapshot delay (ms)
  $description: Take one extra snapshot this long after the first snapshot even without tree changes, to catch late bindings/layout. Set 0 to disable.
- maxSnapshots: 4
  $name: Maximum snapshots
  $description: Safety cap for full snapshots per LockApp process. Includes the verification snapshot.
- interactionTraceSeconds: 0
  $name: Interaction trace duration (seconds)
  $description: Optional. After the first delayed snapshot, sample selected clock, Spotlight and media properties. Only changed samples are written. Use 60 for hover and swipe investigations; 0 disables.
- interactionTraceIntervalMs: 100
  $name: Interaction sample interval (ms)
  $description: 100 is recommended. This is a sampled trace, not a frame-exact animation recording.
- periodicSnapshotIntervalMs: 0
  $name: Additional snapshot interval (ms)
  $description: Optional periodic snapshots after startup, within the existing maximum snapshot cap. Use 10000 to inspect loaded hover templates.
- continuousCapture: true
  $name: Continuously record XAML changes to the file
  $description: After the delayed baseline, subscribe to public appearance/input properties, sizes and visual states. No periodic full-tree scan; composition-only frames are not guaranteed notifications. Reload after changing settings.
- maxFileSizeMB: 256
  $name: Maximum capture file size (MiB)
  $description: Stop recording at this size. Records never fall back to the Windhawk log; only errors are logged.
*/
// ==/WindhawkModSettings==

#include <xamlom.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <locale>
#include <memory>
#include <deque>
#include <utility>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <appmodel.h>
#include <ocidl.h>
#include <winternl.h>

// Windows headers define GetCurrentTime() as a macro for GetTickCount().
// C++/WinRT declares an actual GetCurrentTime method in the XAML animation ABI,
// so remove the Win32 macro before including the WinRT XAML headers.
#undef GetCurrentTime

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>

namespace wf = winrt::Windows::Foundation;
namespace wfc = winrt::Windows::Foundation::Collections;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxm = winrt::Windows::UI::Xaml::Media;

namespace {

struct Settings {
    bool includeProperties = true;
    bool styleRelevantPropertiesOnly = true;
    bool includeVisualStates = true;
    bool includeRectangle = true;
    int settleDelayMs = 3000;
    int initialSnapshotDelayMs = 6000;
    int maxSnapshotWaitMs = 15000;
    int verificationSnapshotDelayMs = 5000;
    int maxSnapshots = 4;
    int interactionTraceSeconds = 0;
    int interactionTraceIntervalMs = 100;
    int periodicSnapshotIntervalMs = 0;
    bool continuousCapture = true;
    int maxFileSizeMB = 256;
};

Settings g_settings;
std::atomic<bool> g_initialized = false;

void LoadSettings() {
    g_settings.includeProperties = Wh_GetIntSetting(L"includeProperties") != 0;
    g_settings.styleRelevantPropertiesOnly =
        Wh_GetIntSetting(L"styleRelevantPropertiesOnly") != 0;
    g_settings.includeVisualStates =
        Wh_GetIntSetting(L"includeVisualStates") != 0;
    g_settings.includeRectangle = Wh_GetIntSetting(L"includeRectangle") != 0;
    g_settings.settleDelayMs = std::max(0, Wh_GetIntSetting(L"settleDelayMs"));
    g_settings.initialSnapshotDelayMs =
        std::max(0, Wh_GetIntSetting(L"initialSnapshotDelayMs"));
    g_settings.maxSnapshotWaitMs =
        std::max(1000, Wh_GetIntSetting(L"maxSnapshotWaitMs"));
    g_settings.verificationSnapshotDelayMs =
        std::max(0, Wh_GetIntSetting(L"verificationSnapshotDelayMs"));
    g_settings.maxSnapshots =
        std::max(1, Wh_GetIntSetting(L"maxSnapshots"));
    // Keep the user's cold-start minimum authoritative, even if the timeout
    // setting is accidentally lower than that minimum.
    g_settings.maxSnapshotWaitMs = std::max(g_settings.maxSnapshotWaitMs, g_settings.initialSnapshotDelayMs);
    g_settings.interactionTraceSeconds = std::clamp(Wh_GetIntSetting(L"interactionTraceSeconds"), 0, 300);
    g_settings.interactionTraceIntervalMs = std::clamp(Wh_GetIntSetting(L"interactionTraceIntervalMs"), 50, 1000);
    g_settings.periodicSnapshotIntervalMs = std::max(0, Wh_GetIntSetting(L"periodicSnapshotIntervalMs"));
    g_settings.continuousCapture = Wh_GetIntSetting(L"continuousCapture") != 0;
    g_settings.maxFileSizeMB = std::clamp(Wh_GetIntSetting(L"maxFileSizeMB"), 8, 4096);
}

std::wstring JsonEscape(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size() + 16);

    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (wchar_t ch : value) {
        switch (ch) {
            case L'\\': out += L"\\\\"; break;
            case L'\"': out += L"\\\""; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (ch < 0x20) {
                    out += L"\\u00";
                    out += hex[(ch >> 4) & 0xF];
                    out += hex[ch & 0xF];
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

void AppendJsonString(std::wostringstream& ss, std::wstring_view value) {
    ss << L'\"' << JsonEscape(value) << L'\"';
}

std::string Utf8FromWide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0,
                                    nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), bytes, nullptr, nullptr);
    return result;
}

std::wstring ShortenClassName(std::wstring_view className) {
    if (className == L"Windows.UI.Xaml.Shapes.Rectangle") {
        return L"Rectangle";
    }

    constexpr std::wstring_view prefix = L"Windows.UI.Xaml.Controls.";
    if (className.size() >= prefix.size() &&
        className.substr(0, prefix.size()) == prefix) {
        auto shortName = className.substr(prefix.size());
        if (shortName.find_first_of(L".:") == std::wstring_view::npos) {
            return std::wstring(shortName);
        }
    }

    return std::wstring(className);
}

std::wstring GetOsVersionString() {
    using RtlGetVersion_t = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtlGetVersion = reinterpret_cast<RtlGetVersion_t>(
        ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr);

    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!rtlGetVersion || rtlGetVersion(&version) != 0) {
        return L"unknown";
    }

    std::wostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(9);
    ss << version.dwMajorVersion << L'.' << version.dwMinorVersion << L'.'
       << version.dwBuildNumber;
    return ss.str();
}

std::wstring MakeTimestampForFileName() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth,
               st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// File I/O and UTF-8 conversion run on one worker. Producers only queue text.
class DumpWriter {
public:
    ~DumpWriter() { Close(); }
    void Initialize() {
        if (m_initialized) return;
        m_initialized=true;
        std::wstring folder;
        try { folder=winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str(); }
        catch (...) { Wh_Log(L"Cannot resolve LockApp LocalState folder: %08X",static_cast<DWORD>(winrt::to_hresult())); return; }
        std::wstring stem=folder+L"\\LockApp-XamlDump-"+MakeTimestampForFileName()+L"-pid"+std::to_wstring(GetCurrentProcessId());
        for (unsigned i=0;i<100;++i) {
            auto path=stem+(i ? L"-"+std::to_wstring(i) : L"")+L".jsonl";
            if (Open(path,static_cast<uint64_t>(g_settings.maxFileSizeMB)*1024*1024)) return;
            if (GetLastError()!=ERROR_FILE_EXISTS) break;
        }
        Wh_Log(L"Cannot create LockApp dump file: %u",GetLastError());
    }
    // Also used by the isolated writer test; never overwrites an existing file.
    bool Open(std::wstring const& path,uint64_t byteLimit,size_t queueLimit=32*1024*1024) {
        if (m_thread) return false;
        m_file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                          nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (m_file==INVALID_HANDLE_VALUE) return false;
        m_path=path; m_byteLimit=byteLimit; m_queueLimit=queueLimit;
        m_wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (m_wake) m_thread=CreateThread(nullptr,0,[](void* ptr)->DWORD {
            static_cast<DumpWriter*>(ptr)->Worker(); return 0;
        },this,0,nullptr);
        if (!m_thread) {
            DWORD error=GetLastError(); if (m_wake) CloseHandle(m_wake); m_wake=nullptr;
            CloseHandle(m_file); m_file=INVALID_HANDLE_VALUE; SetLastError(error); return false;
        }
        m_accepting.store(true); return true;
    }
    void Close() noexcept {
        m_accepting.store(false);
        { std::lock_guard lock(m_mutex); m_stopping=true; if (m_wake) SetEvent(m_wake); }
        if (m_thread) { WaitForSingleObject(m_thread,INFINITE); CloseHandle(m_thread); m_thread=nullptr; }
        if (m_wake) { CloseHandle(m_wake); m_wake=nullptr; }
        if (m_file!=INVALID_HANDLE_VALUE) { CloseHandle(m_file); m_file=INVALID_HANDLE_VALUE; }
    }
    const std::wstring& Path() const { return m_path; }
    bool HasOutput() const { return m_accepting.load(); }
    void Write(std::wstring_view json) { if (HasOutput()) WriteBatch({std::wstring(json)}); }
    void WriteBatch(std::vector<std::wstring> records) {
        if (!HasOutput()) return;
        size_t bytes=0; for (auto const& record:records) bytes+=sizeof(wchar_t)*record.size();
        std::lock_guard lock(m_mutex);
        if (!HasOutput() || m_stopping) return;
        if (bytes>m_queueLimit || m_queuedBytes>m_queueLimit-bytes) {
            m_dropped+=records.size(); SetEvent(m_wake); return;
        }
        m_queuedBytes+=bytes; m_queue.push_back(std::move(records)); SetEvent(m_wake);
    }
private:
    bool WriteBytes(std::string const& data) noexcept {
        size_t offset=0;
        while (offset<data.size()) {
            DWORD written=0;
            auto count=static_cast<DWORD>(std::min<size_t>(data.size()-offset,1024*1024));
            if (!WriteFile(m_file,data.data()+offset,count,&written,nullptr) || !written) {
                m_accepting.store(false);
                Wh_Log(L"Dump file write failed: %u",GetLastError()); return false;
            }
            offset+=written;
        }
        m_written+=data.size(); return true;
    }
    void Worker() noexcept {
        try {
            for (;;) {
                WaitForSingleObject(m_wake,INFINITE);
                std::deque<std::vector<std::wstring>> batches;
                uint64_t dropped; bool stopping;
                { std::lock_guard lock(m_mutex); batches.swap(m_queue); m_queuedBytes=0;
                  dropped=std::exchange(m_dropped,0); stopping=m_stopping; }
                if (dropped) {
                    std::string gap="{\"event\":\"outputGap\",\"droppedRecords\":"+std::to_string(dropped)+"}\n";
                    if (m_written+gap.size()+128>m_byteLimit) {
                        m_accepting.store(false);
                        WriteBytes("{\"event\":\"captureStopped\",\"reason\":\"fileSizeLimit\"}\n");
                        FlushFileBuffers(m_file); return;
                    }
                    if (!WriteBytes(gap)) break;
                    if (!m_reportedOverflow) { m_reportedOverflow=true;
                        Wh_Log(L"Dump queue overflow; dropped records are marked in the file"); }
                }
                for (auto const& batch:batches) {
                    std::string utf8;
                    for (auto const& record:batch) { utf8+=Utf8FromWide(record); utf8+='\n'; }
                    // Reserve a small ending record. A snapshot batch is kept whole.
                    if (m_written+utf8.size()+128>m_byteLimit) {
                        m_accepting.store(false);
                        WriteBytes("{\"event\":\"captureStopped\",\"reason\":\"fileSizeLimit\"}\n");
                        Wh_Log(L"Dump capture reached its file-size limit; recording stopped");
                        FlushFileBuffers(m_file); return;
                    }
                    if (!WriteBytes(utf8)) return;
                }
                if (stopping) break;
            }
        } catch (...) { m_accepting.store(false); Wh_Log(L"Dump writer failed"); }
        FlushFileBuffers(m_file);
    }
    std::atomic<bool> m_accepting{false};
    bool m_initialized=false,m_stopping=false,m_reportedOverflow=false;
    std::mutex m_mutex;
    std::deque<std::vector<std::wstring>> m_queue;
    size_t m_queuedBytes=0,m_queueLimit=0;
    uint64_t m_dropped=0,m_written=0,m_byteLimit=0;
    HANDLE m_file=INVALID_HANDLE_VALUE,m_thread=nullptr,m_wake=nullptr;
    std::wstring m_path;
};

DumpWriter g_writer;


template <typename GridLengthType>
std::wstring FormatGridLength(GridLengthType const& length) {
    std::wostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(9);
    if (length.GridUnitType == decltype(length.GridUnitType)::Auto) {
        ss << L"Auto";
    } else if (length.GridUnitType == decltype(length.GridUnitType)::Star) {
        if (length.Value == 1.0) {
            ss << L'*';
        } else {
            ss << length.Value << L'*';
        }
    } else {
        ss << length.Value << L"px";
    }
    return ss.str();
}

std::wstring FormatColumnDefinitions(
    wuxc::ColumnDefinitionCollection const& collection) {
    std::wostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(9);
    ss << L"[";
    for (uint32_t i = 0; i < collection.Size(); ++i) {
        if (i) ss << L", ";
        auto def = collection.GetAt(i);
        ss << L"{Width=" << FormatGridLength(def.Width());
        if (def.MinWidth() != 0.0) ss << L", MinWidth=" << def.MinWidth();
        if (!std::isinf(def.MaxWidth())) ss << L", MaxWidth=" << def.MaxWidth();
        ss << L"}";
    }
    ss << L"]";
    return ss.str();
}

std::wstring FormatRowDefinitions(
    wuxc::RowDefinitionCollection const& collection) {
    std::wostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(9);
    ss << L"[";
    for (uint32_t i = 0; i < collection.Size(); ++i) {
        if (i) ss << L", ";
        auto def = collection.GetAt(i);
        ss << L"{Height=" << FormatGridLength(def.Height());
        if (def.MinHeight() != 0.0) ss << L", MinHeight=" << def.MinHeight();
        if (!std::isinf(def.MaxHeight())) ss << L", MaxHeight=" << def.MaxHeight();
        ss << L"}";
    }
    ss << L"]";
    return ss.str();
}

// Schema 4 supplements the diagnostic property chain with evaluated UI values.
namespace wuxa = winrt::Windows::UI::Xaml::Media::Animation;

std::wstring DumpColor(winrt::Windows::UI::Color c) {
    wchar_t color[16];
    swprintf_s(color, L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
    return color;
}

std::wstring DescribeDumpValue(wf::IInspectable const& obj) {
    if (!obj) return L"(null)";
    std::wostringstream out;
    out.imbue(std::locale::classic()); out << std::setprecision(9);
    if (auto v = obj.try_as<wf::IPropertyValue>()) {
        switch (v.Type()) {
        case wf::PropertyType::String: return std::wstring(v.GetString());
        case wf::PropertyType::Double: out << v.GetDouble(); return out.str();
        case wf::PropertyType::Single: out << v.GetSingle(); return out.str();
        case wf::PropertyType::Boolean: return v.GetBoolean() ? L"true" : L"false";
        case wf::PropertyType::Int32: out << v.GetInt32(); return out.str();
        case wf::PropertyType::UInt32: out << v.GetUInt32(); return out.str();
        default: break;
        }
    }
    if (auto value = obj.try_as<wf::IReference<wux::Thickness>>()) {
        auto v=value.Value(); out<<v.Left<<L','<<v.Top<<L','<<v.Right<<L','<<v.Bottom; return out.str();
    }
    if (auto value = obj.try_as<wf::IReference<wux::CornerRadius>>()) {
        auto v=value.Value(); out<<v.TopLeft<<L','<<v.TopRight<<L','<<v.BottomRight<<L','<<v.BottomLeft; return out.str();
    }
    if (auto value = obj.try_as<wf::IReference<winrt::Windows::UI::Text::FontWeight>>()) {
        out<<value.Value().Weight; return out.str();
    }
    if (auto family=obj.try_as<wuxm::FontFamily>()) return std::wstring(family.Source());
    out << winrt::get_class_name(obj).c_str();
    if (auto brush = obj.try_as<wuxm::Brush>()) {
        out << L" {Opacity=" << brush.Opacity();
        if (auto solid = obj.try_as<wuxm::SolidColorBrush>()) out << L", Color=" << DumpColor(solid.Color());
        if (auto acrylic = obj.try_as<wuxm::AcrylicBrush>()) {
            out << L", TintColor=" << DumpColor(acrylic.TintColor())
                << L", TintOpacity=" << acrylic.TintOpacity()
                << L", FallbackColor=" << DumpColor(acrylic.FallbackColor())
                << L", BackgroundSource=" << static_cast<int>(acrylic.BackgroundSource())
                << L", AlwaysUseFallback=" << (acrylic.AlwaysUseFallback() ? L"true" : L"false");
            if (auto lum = acrylic.TintLuminosityOpacity()) out << L", TintLuminosityOpacity=" << lum.Value();
            else out << L", TintLuminosityOpacity=Auto";
        }
        out << L"}";
    } else if (auto transform = obj.try_as<wuxm::TranslateTransform>()) {
        out << L" {X=" << transform.X() << L", Y=" << transform.Y() << L"}";
    } else if (auto transform = obj.try_as<wuxm::ScaleTransform>()) {
        out << L" {ScaleX=" << transform.ScaleX() << L", ScaleY=" << transform.ScaleY()
            << L", CenterX=" << transform.CenterX() << L", CenterY=" << transform.CenterY() << L"}";
    } else if (auto transform = obj.try_as<wuxm::CompositeTransform>()) {
        out << L" {TranslateX=" << transform.TranslateX() << L", TranslateY=" << transform.TranslateY()
            << L", ScaleX=" << transform.ScaleX() << L", ScaleY=" << transform.ScaleY()
            << L", Rotation=" << transform.Rotation() << L", CenterX=" << transform.CenterX()
            << L", CenterY=" << transform.CenterY() << L"}";
    } else if (auto bitmap = obj.try_as<wuxm::Imaging::BitmapImage>()) {
        auto uri = bitmap.UriSource();
        out << L" {Uri=" << (uri ? uri.RawUri().c_str() : L"") << L", PixelWidth=" << bitmap.PixelWidth()
            << L", PixelHeight=" << bitmap.PixelHeight() << L"}";
    }
    return out.str();
}

void AppendDumpNumber(std::wostringstream& out, double value) {
    if (std::isfinite(value)) out << value;
    else out << L"null";
}

std::wstring RuntimeDumpDetails(wf::IInspectable const& obj) {
    auto node = obj.try_as<wux::FrameworkElement>();
    if (!node) return L"null";
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9) << L"{\"Opacity\":";
    AppendDumpNumber(out, node.Opacity());
    out << L",\"Visibility\":" << static_cast<int>(node.Visibility());
    out << L",\"IsHitTestVisible\":" << (node.IsHitTestVisible() ? L"true" : L"false");
    out << L",\"RenderTransform\":";
    AppendJsonString(out, DescribeDumpValue(node.RenderTransform()));
    if (auto root = node.XamlRoot()) {
        out << L",\"rasterizationScale\":";
        AppendDumpNumber(out, root.RasterizationScale());
        out << L",\"rootWidth\":";
        AppendDumpNumber(out, root.Size().Width);
        out << L",\"rootHeight\":";
        AppendDumpNumber(out, root.Size().Height);
    }
    if (auto text = obj.try_as<wuxc::TextBlock>()) {
        out << L",\"Text\":"; AppendJsonString(out, text.Text().c_str());
        out << L",\"OpticalMarginAlignment\":" << static_cast<int>(text.OpticalMarginAlignment())
            << L",\"TextLineBounds\":" << static_cast<int>(text.TextLineBounds())
            << L",\"LineStackingStrategy\":" << static_cast<int>(text.LineStackingStrategy())
            << L",\"IsTextScaleFactorEnabled\":" << (text.IsTextScaleFactorEnabled() ? L"true" : L"false")
            << L",\"BaselineOffset\":";
        AppendDumpNumber(out, text.BaselineOffset());
    }
    if (auto grid = obj.try_as<wuxc::Grid>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(grid.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(grid.BorderBrush()));
        auto radius = grid.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto border = obj.try_as<wuxc::Border>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(border.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(border.BorderBrush()));
        auto radius = border.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto control = obj.try_as<wuxc::Control>()) {
        out << L",\"IsEnabled\":" << (control.IsEnabled() ? L"true" : L"false");
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(control.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(control.BorderBrush()));
        auto radius = control.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto presenter = obj.try_as<wuxc::ContentPresenter>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(presenter.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(presenter.BorderBrush()));
        auto radius = presenter.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto panel = obj.try_as<wuxc::Panel>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(panel.Background()));
    } else if (auto shape = obj.try_as<winrt::Windows::UI::Xaml::Shapes::Shape>()) {
        out << L",\"Fill\":"; AppendJsonString(out, DescribeDumpValue(shape.Fill()));
    } else if (auto image = obj.try_as<wuxc::Image>()) {
        out << L",\"Source\":"; AppendJsonString(out, DescribeDumpValue(image.Source()));
    }
    try {
        auto visual = wux::Hosting::ElementCompositionPreview::GetElementVisual(node);
        auto opacity = visual.Opacity();
        auto offset = visual.Offset();
        out << L",\"compositionOpacity\":"; AppendDumpNumber(out, opacity);
        out << L",\"compositionOffset\":[";
        AppendDumpNumber(out, offset.x); out << L',';
        AppendDumpNumber(out, offset.y); out << L',';
        AppendDumpNumber(out, offset.z); out << L']';
    } catch (...) {}
    out << L"}";
    return out.str();
}

void DumpTimeline(std::wostringstream& out, wuxa::Timeline const& timeline, unsigned depth = 0) {
    if (!timeline || depth > 12) { out << L"null"; return; }
    out << L"{\"type\":"; AppendJsonString(out, winrt::get_class_name(timeline).c_str());
    out << L",\"targetName\":"; AppendJsonString(out, wuxa::Storyboard::GetTargetName(timeline).c_str());
    out << L",\"targetProperty\":"; AppendJsonString(out, wuxa::Storyboard::GetTargetProperty(timeline).c_str());
    auto duration = timeline.Duration();
    out << L",\"durationType\":" << static_cast<int>(duration.Type);
    if (duration.Type == wux::DurationType::TimeSpan)
        out << L",\"durationMs\":" << duration.TimeSpan.count() / 10000.0;
    if (auto begin = timeline.BeginTime()) out << L",\"beginMs\":" << begin.Value().count() / 10000.0;
    if (auto animation = timeline.try_as<wuxa::DoubleAnimation>()) {
        if (auto value = animation.From()) { out << L",\"from\":"; AppendDumpNumber(out, value.Value()); }
        if (auto value = animation.To()) { out << L",\"to\":"; AppendDumpNumber(out, value.Value()); }
        if (auto value = animation.By()) { out << L",\"by\":"; AppendDumpNumber(out, value.Value()); }
    }
    if (auto animation = timeline.try_as<wuxa::ColorAnimation>()) {
        if (auto value = animation.To()) { out << L",\"to\":"; AppendJsonString(out, DumpColor(value.Value())); }
    }
    if (auto animation = timeline.try_as<wuxa::DoubleAnimationUsingKeyFrames>()) {
        out << L",\"keyFrames\":[";
        bool first = true;
        for (auto const& key : animation.KeyFrames()) {
            if (!first) out << L','; first = false;
            out << L"{\"ms\":" << key.KeyTime().TimeSpan.count() / 10000.0 << L",\"value\":";
            AppendDumpNumber(out, key.Value()); out << L'}';
        }
        out << L']';
    }
    if (auto animation = timeline.try_as<wuxa::ObjectAnimationUsingKeyFrames>()) {
        out << L",\"keyFrames\":[";
        bool first = true;
        for (auto const& key : animation.KeyFrames()) {
            if (!first) out << L','; first = false;
            out << L"{\"ms\":" << key.KeyTime().TimeSpan.count() / 10000.0 << L",\"value\":";
            AppendJsonString(out, DescribeDumpValue(key.Value())); out << L'}';
        }
        out << L']';
    }
    if (auto storyboard = timeline.try_as<wuxa::Storyboard>()) {
        out << L",\"children\":[";
        bool first = true;
        for (auto const& child : storyboard.Children()) {
            if (!first) out << L','; first = false;
            DumpTimeline(out, child, depth + 1);
        }
        out << L']';
    }
    out << L'}';
}

std::wstring VisualStateDumpDetails(wux::FrameworkElement const& node) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << L'[';
    bool firstGroup = true;
    for (auto const& group : wux::VisualStateManager::GetVisualStateGroups(node)) {
        if (!firstGroup) out << L','; firstGroup = false;
        out << L"{\"name\":"; AppendJsonString(out, group.Name().c_str());
        out << L",\"current\":";
        AppendJsonString(out, group.CurrentState() ? group.CurrentState().Name().c_str() : L"");
        out << L",\"states\":[";
        bool first = true;
        for (auto const& state : group.States()) {
            if (!first) out << L','; first = false;
            AppendJsonString(out, state.Name().c_str());
        }
        out << L"],\"stateDetails\":[";
        first = true;
        for (auto const& state : group.States()) {
            if (!first) out << L','; first = false;
            out << L"{\"name\":"; AppendJsonString(out, state.Name().c_str());
            out << L",\"storyboard\":"; DumpTimeline(out, state.Storyboard());
            out << L",\"setters\":[";
            bool firstSetter = true;
            for (auto const& base : state.Setters()) {
                auto setter = base.try_as<wux::Setter>();
                if (!setter) continue;
                if (!firstSetter) out << L','; firstSetter = false;
                auto target = setter.Target();
                out << L"{\"targetPath\":";
                AppendJsonString(out, target && target.Path() ? target.Path().Path().c_str() : L"");
                out << L",\"target\":";
                auto fe = target && target.Target() ? target.Target().try_as<wux::FrameworkElement>() : nullptr;
                AppendJsonString(out, fe ? fe.Name().c_str() : L"");
                out << L",\"value\":"; AppendJsonString(out, DescribeDumpValue(setter.Value()));
                out << L'}';
            }
            out << L"]}";
        }
        out << L"],\"transitions\":[";
        first = true;
        for (auto const& transition : group.Transitions()) {
            if (!first) out << L','; first = false;
            out << L"{\"from\":"; AppendJsonString(out, transition.From().c_str());
            out << L",\"to\":"; AppendJsonString(out, transition.To().c_str());
            auto d = transition.GeneratedDuration();
            if (d.Type == wux::DurationType::TimeSpan) out << L",\"generatedDurationMs\":" << d.TimeSpan.count() / 10000.0;
            out << L",\"storyboard\":"; DumpTimeline(out, transition.Storyboard());
            out << L'}';
        }
        out << L"]}";
    }
    out << L']';
    return out.str();
}


std::wstring FormatPropertyValue(const PropertyChainValue& value,
                                 IXamlDiagnostics* diagnostics) {
    if (value.MetadataBits & IsValueNull) {
        return L"(null)";
    }

    if (!(value.MetadataBits & IsValueHandle)) {
        return value.Value ? value.Value : L"";
    }

    InstanceHandle valueHandle = static_cast<InstanceHandle>(
        std::wcstoll(value.Value ? value.Value : L"0", nullptr, 10));

    wf::IInspectable obj{nullptr};
    HRESULT hr = diagnostics->GetIInspectableFromHandle(
        valueHandle,
        reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
    if (FAILED(hr) || !obj) {
        std::wostringstream ss;
        ss.imbue(std::locale::classic());
        ss << std::setprecision(9);
        ss << L"(handle=" << valueHandle << L", HRESULT=0x" << std::hex
           << static_cast<unsigned long>(hr) << L")";
        return ss.str();
    }

    std::wstring className;
    try {
        className = winrt::get_class_name(obj);
    } catch (...) {
        className = L"(unknown class)";
    }

    std::wostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(9);
    ss << L"(" << ((value.MetadataBits & IsValueCollection) ? L"collection" : L"data")
       << L"; " << className;

    try {
        if (auto brush = obj.try_as<wuxm::SolidColorBrush>()) {
            auto c = brush.Color();
            wchar_t color[16];
            if (c.A == 0xFF) {
                swprintf_s(color, L"#%02X%02X%02X", c.R, c.G, c.B);
            } else {
                swprintf_s(color, L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
            }
            ss << L" {Color=" << color << L", Opacity=" << brush.Opacity() << L"}";
        } else if (obj.try_as<wuxm::Brush>() || obj.try_as<wuxm::Transform>()) {
            ss << L" {details=" << DescribeDumpValue(obj) << L"}";
        } else if (auto grid = obj.try_as<wuxc::Grid>()) {
            ss << L" {Columns=" << grid.ColumnDefinitions().Size()
               << L", Rows=" << grid.RowDefinitions().Size() << L"}";
        } else if (auto cols = obj.try_as<wuxc::ColumnDefinitionCollection>()) {
            ss << L" " << FormatColumnDefinitions(cols);
        } else if (auto rows = obj.try_as<wuxc::RowDefinitionCollection>()) {
            ss << L" " << FormatRowDefinitions(rows);
        } else if (auto vector = obj.try_as<wfc::IVector<wf::IInspectable>>()) {
            ss << L" {Size=" << vector.Size() << L"}";
        }
    } catch (...) {
        // Extra diagnostic formatting is optional.
    }

    ss << L")";
    return ss.str();
}

struct ElementMeta {
    InstanceHandle parent = 0;
    unsigned int childIndex = 0;
    std::wstring declaredType;
    std::wstring runtimeType;
    std::wstring name;
};

bool IsStyleRelevantProperty(std::wstring_view name) {
    // Properties that are commonly useful in Windhawk XAML Styler rules or for
    // geometric/visual comparison. GetPropertyValuesChain still gives us the
    // effective/source information; we simply avoid formatting hundreds of
    // unrelated automation/input/runtime properties.
    static constexpr std::wstring_view kNames[] = {
        L"Width", L"Height", L"MinWidth", L"MinHeight", L"MaxWidth",
        L"MaxHeight", L"ActualWidth", L"ActualHeight", L"Margin", L"Padding",
        L"HorizontalAlignment", L"VerticalAlignment", L"HorizontalContentAlignment",
        L"VerticalContentAlignment", L"Opacity", L"Visibility", L"FlowDirection",
        L"IsHitTestVisible", L"RequestedTheme", L"ActualTheme", L"Background",
        L"BackgroundSizing", L"BorderBrush", L"BorderThickness", L"CornerRadius",
        L"Foreground", L"FontFamily", L"FontSize", L"FontStretch", L"FontStyle",
        L"FontWeight", L"CharacterSpacing", L"Text", L"TextAlignment",
        L"TextWrapping", L"TextTrimming", L"LineHeight", L"LineStackingStrategy",
        L"Grid.Row", L"Grid.Column", L"Grid.RowSpan", L"Grid.ColumnSpan",
        L"Canvas.Left", L"Canvas.Top", L"Canvas.ZIndex", L"RowDefinitions",
        L"ColumnDefinitions", L"RowSpacing", L"ColumnSpacing", L"Stretch",
        L"Fill", L"Stroke", L"StrokeThickness", L"RadiusX", L"RadiusY",
        L"RenderTransform", L"RenderTransformOrigin", L"Transform3D", L"Translation",
        L"Scale", L"Rotation", L"RotationAxis", L"CenterPoint", L"TransformMatrix",
        L"Clip", L"Shadow", L"Source", L"Glyph", L"Icon", L"PlaceholderText",
        L"Content", L"HorizontalOffset", L"VerticalOffset", L"OpticalMarginAlignment",
        L"TextLineBounds", L"IsTextScaleFactorEnabled", L"BaselineOffset", L"Orientation",
        L"Spacing", L"IsEnabled", L"UseLayoutRounding", L"TextReadingOrder"
    };

    for (auto candidate : kNames) {
        if (name == candidate) {
            return true;
        }
    }
    return false;
}

HMODULE GetCurrentModuleHandle() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&GetCurrentModuleHandle),
                            &module)) {
        return nullptr;
    }
    return module;
}

// Implemented below. The snapshot scheduler runs on a worker thread, but all
// XAML object/property access is marshalled back to LockApp's CoreWindow thread.
bool RunFromWindowThread(HWND hWnd, void(WINAPI* proc)(PVOID), PVOID param);
std::vector<HWND> GetCoreWindows();

// All subscriptions and callbacks belong to the element's XAML thread.
// Callback work is bounded to one value and a queue insertion, never a property
// chain query or disk operation. No polling/render callback is used here.
struct LiveElement : std::enable_shared_from_this<LiveElement> {
    struct Hook {
        winrt::weak_ref<wux::DependencyObject> source;
        wux::DependencyProperty property{nullptr};
        int64_t token=0;
        void Revoke() noexcept { try { if (auto node=source.get()) node.UnregisterPropertyChangedCallback(property,token); } catch (...) {} }
    };
    struct StateHook {
        winrt::weak_ref<wux::VisualStateGroup> source;
        winrt::event_token changing{},changed{};
    };
    winrt::weak_ref<wux::FrameworkElement> element;
    InstanceHandle handle=0;
    DWORD threadId=0;
    std::atomic<bool> active{true};
    std::vector<Hook> hooks;
    std::unordered_map<std::wstring,std::vector<Hook>> nested;
    std::vector<StateHook> states;
    std::vector<std::pair<std::wstring,wux::DependencyProperty>> properties;
    winrt::event_token sizeToken{},loadedToken{},viewToken{};
    uint64_t startTick=0;

    std::wostringstream Record(const wchar_t* event) const {
        std::wostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(9);
        out<<L"{\"event\":\""<<event<<L"\",\"elapsedMs\":"<<GetTickCount64()-startTick<<L",\"handle\":"<<handle;
        return out;
    }
    void Stop() noexcept {
        if (!active.exchange(false)) return;
        for (auto& hook:hooks) hook.Revoke(); hooks.clear();
        for (auto& [_,list]:nested) for (auto& hook:list) hook.Revoke(); nested.clear();
        for (auto& state:states) try { if (auto group=state.source.get()) {
            group.CurrentStateChanging(state.changing); group.CurrentStateChanged(state.changed);
        }} catch (...) {}
        states.clear();
        try { if (auto node=element.get()) {
            if (sizeToken.value) node.SizeChanged(sizeToken);
            if (loadedToken.value) node.Loaded(loadedToken);
            if (viewToken.value) node.as<wuxc::ScrollViewer>().ViewChanged(viewToken);
        }} catch (...) {}
    }
    ~LiveElement() { Stop(); }
    void GeometryChanged() noexcept {
        if (!active || !g_writer.HasOutput() || !g_settings.includeRectangle) return;
        try {
            auto node=element.get(); if (!node) return;
            auto bounds=node.TransformToVisual(nullptr).TransformBounds({0,0,static_cast<float>(node.ActualWidth()),static_cast<float>(node.ActualHeight())});
            auto out=Record(L"geometryChanged");
            out<<L",\"rectangle\":{\"x\":"; AppendDumpNumber(out,bounds.X);
            out<<L",\"y\":"; AppendDumpNumber(out,bounds.Y);
            out<<L",\"width\":"; AppendDumpNumber(out,bounds.Width);
            out<<L",\"height\":"; AppendDumpNumber(out,bounds.Height); out<<L"}}";
            g_writer.Write(out.str());
        } catch (...) {}
    }
    void Changed(std::wstring const& name,wux::DependencyProperty const& property,bool replaceNested) noexcept {
        if (!active || !g_writer.HasOutput()) return;
        try {
            auto node=element.get(); if (!node) return;
            auto value=node.GetValue(property);
            auto out=Record(L"propertyChanged"); out<<L",\"property\":"; AppendJsonString(out,name);
            out<<L",\"value\":"; AppendJsonString(out,DescribeDumpValue(value)); out<<L'}';
            g_writer.Write(out.str());
            if (replaceNested) WatchNested(name,property,value);
        } catch (...) {}
    }
    void WatchNested(std::wstring const& name,wux::DependencyProperty const& ownerProperty,wf::IInspectable const& value) {
        auto old=nested.extract(name); if (old) for (auto& hook:old.mapped()) hook.Revoke();
        auto object=value ? value.try_as<wux::DependencyObject>() : nullptr;
        if (!object) return;
        std::vector<wux::DependencyProperty> list;
        if (object.try_as<wuxm::Brush>()) list.push_back(wuxm::Brush::OpacityProperty());
        if (object.try_as<wuxm::SolidColorBrush>()) list.push_back(wuxm::SolidColorBrush::ColorProperty());
        if (object.try_as<wuxm::AcrylicBrush>()) {
            list.insert(list.end(),{wuxm::AcrylicBrush::TintColorProperty(),wuxm::AcrylicBrush::TintOpacityProperty(),
                wuxm::AcrylicBrush::TintLuminosityOpacityProperty(),wuxm::XamlCompositionBrushBase::FallbackColorProperty(),
                wuxm::AcrylicBrush::BackgroundSourceProperty(),wuxm::AcrylicBrush::AlwaysUseFallbackProperty()});
        }
        if (object.try_as<wuxm::TranslateTransform>()) list.insert(list.end(),{wuxm::TranslateTransform::XProperty(),wuxm::TranslateTransform::YProperty()});
        if (object.try_as<wuxm::CompositeTransform>()) list.insert(list.end(),{wuxm::CompositeTransform::TranslateXProperty(),
            wuxm::CompositeTransform::TranslateYProperty(),wuxm::CompositeTransform::ScaleXProperty(),
            wuxm::CompositeTransform::ScaleYProperty(),wuxm::CompositeTransform::RotationProperty()});
        std::weak_ptr<LiveElement> weak=shared_from_this();
        for (auto const& property:list) {
            auto token=object.RegisterPropertyChangedCallback(property,[weak,name,ownerProperty](auto const&,auto const&) noexcept {
                if (auto self=weak.lock()) self->Changed(name,ownerProperty,false);
            });
            nested[name].push_back({object,property,token});
        }
    }
    void Watch(std::wstring name,wux::DependencyProperty const& property) {
        auto node=element.get(); if (!node) return;
        std::weak_ptr<LiveElement> weak=shared_from_this();
        auto token=node.RegisterPropertyChangedCallback(property,[weak,name](auto const&,auto const& dp) noexcept {
            if (auto self=weak.lock()) self->Changed(name,dp,true);
        });
        hooks.push_back({node,property,token}); properties.emplace_back(name,property);
        WatchNested(name,property,node.GetValue(property));
    }
    void WatchStates() {
        if (!active || !g_settings.includeVisualStates) return;
        auto node=element.get(); if (!node) return;
        std::weak_ptr<LiveElement> weak=shared_from_this();
        for (auto group:wux::VisualStateManager::GetVisualStateGroups(node)) {
            if (std::any_of(states.begin(),states.end(),[&](auto const& old){return old.source.get()==group;})) continue;
            auto callback=[weak,name=std::wstring(group.Name())](bool changing,wux::VisualStateChangedEventArgs const& args) noexcept {
                auto self=weak.lock(); if (!self || !self->active || !g_writer.HasOutput()) return;
                try {
                    auto out=self->Record(changing ? L"stateChanging" : L"stateChanged");
                    out<<L",\"group\":"; AppendJsonString(out,name);
                    out<<L",\"oldState\":"; AppendJsonString(out,args.OldState() ? args.OldState().Name().c_str() : L"");
                    out<<L",\"newState\":"; AppendJsonString(out,args.NewState() ? args.NewState().Name().c_str() : L"");
                    out<<L'}'; g_writer.Write(out.str());
                } catch (...) {}
            };
            StateHook hook; hook.source=group;
            hook.changing=group.CurrentStateChanging([callback](auto const&,auto const& args){callback(true,args);});
            hook.changed=group.CurrentStateChanged([callback](auto const&,auto const& args){callback(false,args);});
            states.push_back(hook);
        }
    }
    static std::shared_ptr<LiveElement> Create(wux::FrameworkElement const& node,InstanceHandle handle,uint64_t startTick) {
        if (!node.Dispatcher().HasThreadAccess()) throw winrt::hresult_wrong_thread();
        auto self=std::make_shared<LiveElement>(); self->element=node; self->handle=handle;
        self->threadId=GetCurrentThreadId(); self->startTick=startTick;
        // Watch inherited public appearance/input properties; private CLR or
        // composition-only properties have no general DP notification contract.
        #define WATCH(type, property) self->Watch(L## #property,type::property##Property())
        WATCH(wux::UIElement,Opacity); WATCH(wux::UIElement,Visibility); WATCH(wux::UIElement,IsHitTestVisible);
        WATCH(wux::UIElement,RenderTransform); WATCH(wux::FrameworkElement,Width); WATCH(wux::FrameworkElement,Height);
        self->Watch(L"Grid.Row",wuxc::Grid::RowProperty()); self->Watch(L"Grid.Column",wuxc::Grid::ColumnProperty());
        self->Watch(L"Grid.RowSpan",wuxc::Grid::RowSpanProperty()); self->Watch(L"Grid.ColumnSpan",wuxc::Grid::ColumnSpanProperty());
        WATCH(wux::FrameworkElement,Margin); WATCH(wux::FrameworkElement,HorizontalAlignment); WATCH(wux::FrameworkElement,VerticalAlignment);
        if (node.try_as<wuxc::TextBlock>()) {
            WATCH(wuxc::TextBlock,Text); WATCH(wuxc::TextBlock,FontFamily); WATCH(wuxc::TextBlock,FontSize);
            WATCH(wuxc::TextBlock,FontWeight); WATCH(wuxc::TextBlock,Foreground); WATCH(wuxc::TextBlock,TextAlignment);
            WATCH(wuxc::TextBlock,TextWrapping); WATCH(wuxc::TextBlock,LineHeight);
        }
        if (node.try_as<wuxc::Control>()) {
            WATCH(wuxc::Control,IsEnabled); WATCH(wuxc::Control,Background); WATCH(wuxc::Control,Foreground);
            WATCH(wuxc::Control,FontFamily); WATCH(wuxc::Control,FontSize); WATCH(wuxc::Control,FontWeight);
            WATCH(wuxc::Control,Padding); WATCH(wuxc::Control,CornerRadius); WATCH(wuxc::Control,BorderBrush);
            WATCH(wuxc::Control,BorderThickness);
        } else if (node.try_as<wuxc::Grid>()) {
            WATCH(wuxc::Panel,Background); WATCH(wuxc::Grid,CornerRadius); WATCH(wuxc::Grid,BorderBrush);
            WATCH(wuxc::Grid,BorderThickness); WATCH(wuxc::Grid,Padding);
        } else if (node.try_as<wuxc::Border>()) {
            WATCH(wuxc::Border,Background); WATCH(wuxc::Border,CornerRadius); WATCH(wuxc::Border,BorderBrush);
            WATCH(wuxc::Border,BorderThickness); WATCH(wuxc::Border,Padding);
        } else if (node.try_as<wuxc::ContentPresenter>()) {
            WATCH(wuxc::ContentPresenter,Background); WATCH(wuxc::ContentPresenter,CornerRadius);
            WATCH(wuxc::ContentPresenter,BorderBrush); WATCH(wuxc::ContentPresenter,Padding);
        } else if (node.try_as<wuxc::Panel>()) WATCH(wuxc::Panel,Background);
        if (node.try_as<wuxc::Image>()) WATCH(wuxc::Image,Source);
        if (node.try_as<wuxc::TextBox>()) WATCH(wuxc::TextBox,Text);
        if (node.try_as<wuxc::ContentControl>()) WATCH(wuxc::ContentControl,Content);
        if (node.try_as<wuxc::PathIcon>()) WATCH(wuxc::PathIcon,Data);
        if (node.try_as<winrt::Windows::UI::Xaml::Shapes::Shape>()) {
            WATCH(winrt::Windows::UI::Xaml::Shapes::Shape,Fill); WATCH(winrt::Windows::UI::Xaml::Shapes::Shape,Stroke);
        }
        #undef WATCH
        self->WatchStates();
        std::weak_ptr<LiveElement> weak=self;
        self->sizeToken=node.SizeChanged([weak](auto const&,auto const&) noexcept { if (auto self=weak.lock()) self->GeometryChanged(); });
        self->loadedToken=node.Loaded([weak](auto const&,auto const&) noexcept {
            if (auto self=weak.lock()) try { self->WatchStates(); self->GeometryChanged(); } catch (...) {}
        });
        if (auto scroll=node.try_as<wuxc::ScrollViewer>())
            self->viewToken=scroll.ViewChanged([weak](auto const& sender,auto const& args) noexcept {
                auto self=weak.lock(); if (!self || !self->active || !g_writer.HasOutput()) return;
                try {
                    auto view=sender.template as<wuxc::ScrollViewer>();
                    auto out=self->Record(L"viewChanged");
                    out<<L",\"horizontalOffset\":"; AppendDumpNumber(out,view.HorizontalOffset());
                    out<<L",\"verticalOffset\":"; AppendDumpNumber(out,view.VerticalOffset());
                    out<<L",\"zoomFactor\":"; AppendDumpNumber(out,view.ZoomFactor());
                    out<<L",\"intermediate\":"<<(args.IsIntermediate() ? L"true" : L"false")<<L'}';
                    g_writer.Write(out.str());
                } catch (...) {}
            });
        return self;
    }
    std::wstring Baseline(InstanceHandle parent,unsigned childIndex,std::wstring const& path) {
        auto node=element.get(); auto out=Record(L"liveElement");
        out<<L",\"parent\":"<<parent<<L",\"childIndex\":"<<childIndex;
        out<<L",\"name\":"; AppendJsonString(out,node.Name().c_str());
        out<<L",\"runtimeType\":"; AppendJsonString(out,winrt::get_class_name(node).c_str());
        out<<L",\"path\":"; AppendJsonString(out,path);
        out<<L",\"properties\":[";
        bool first=true; for (auto const& [name,property]:properties) {
            if (!first) out<<L','; first=false;
            out<<L"{\"name\":"; AppendJsonString(out,name);
            out<<L",\"value\":"; AppendJsonString(out,DescribeDumpValue(node.GetValue(property))); out<<L'}';
        }
        out<<L"],\"effective\":"<<RuntimeDumpDetails(node);
        // Runtime setter targets may not be publicly resolvable. Live baseline
        // needs current group/state names, not another storyboard traversal.
        if (g_settings.includeVisualStates) {
            out<<L",\"visualStateGroups\":[";
            bool firstGroup=true;
            for (auto const& state:states) if (auto group=state.source.get()) {
                if (!firstGroup) out<<L','; firstGroup=false;
                out<<L"{\"name\":"; AppendJsonString(out,group.Name().c_str());
                out<<L",\"current\":"; AppendJsonString(out,group.CurrentState() ? group.CurrentState().Name().c_str() : L"");
                out<<L",\"states\":["; bool firstState=true;
                for (auto entry:group.States()) {
                    if (!firstState) out<<L','; firstState=false; AppendJsonString(out,entry.Name().c_str());
                }
                out<<L"]}";
            }
            out<<L']';
        }
        out<<L'}'; return out.str();
    }
};

class VisualTreeWatcher
    : public winrt::implements<VisualTreeWatcher, IVisualTreeServiceCallback2,
                               winrt::non_agile> {
public:
    explicit VisualTreeWatcher(winrt::com_ptr<IUnknown> site)
        : m_xamlDiagnostics(site.as<IXamlDiagnostics>()),
          m_visualTreeService(site.as<IVisualTreeService3>()) {
        g_writer.Initialize();
        m_startedTick = GetTickCount64();
        m_lastMutationTick.store(m_startedTick);
        WriteHeader();
        StartSnapshotScheduler();

        // This is copied from the Styler/UWPSpy pattern: advising from a new
        // thread avoids occasional hangs inside the XAML diagnostics service.
        AddRef();
        HANDLE thread = CreateThread(
            nullptr, 0,
            [](LPVOID param) -> DWORD {
                auto watcher = reinterpret_cast<VisualTreeWatcher*>(param);
                HRESULT hr = watcher->m_visualTreeService->AdviseVisualTreeChange(watcher);
                if (FAILED(hr)) {
                    Wh_Log(L"AdviseVisualTreeChange failed: %08X", hr);
                }
                watcher->Release();
                return 0;
            },
            this, 0, nullptr);

        if (thread) {
            CloseHandle(thread);
        } else {
            Wh_Log(L"CreateThread for AdviseVisualTreeChange failed: %u",
                   GetLastError());
            Release();
        }
    }

    ~VisualTreeWatcher() {
        StopSnapshotScheduler();
        StopLiveCapture();
    }

    void UnadviseVisualTreeChange() {
        // Stop future snapshot scheduling first. LockApp can suspend as soon as
        // it leaves the visible lock screen, so never rely on post-unlock work.
        StopSnapshotScheduler();
        StopLiveCapture();

        HRESULT hr = m_visualTreeService->UnadviseVisualTreeChange(this);
        if (FAILED(hr)) {
            Wh_Log(L"UnadviseVisualTreeChange failed: %08X", hr);
        }
    }

private:
    struct SnapshotRequest {
        VisualTreeWatcher* watcher = nullptr;
        unsigned int sequence = 0;
        uint64_t generation = 0;
        const wchar_t* reason = L"";
        std::vector<std::wstring> elementRecords;
        unsigned int attempted = 0;
        unsigned int captured = 0;
        unsigned int failures = 0;
        ULONGLONG durationMs = 0;
    };

    wf::IInspectable FromHandle(InstanceHandle handle) {
        wf::IInspectable obj{nullptr};
        winrt::check_hresult(m_xamlDiagnostics->GetIInspectableFromHandle(
            handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(obj))));
        return obj;
    }

    void WriteHeader() {
        std::wostringstream ss;
        ss.imbue(std::locale::classic());
        ss << std::setprecision(9);
        ss << L"{\"event\":\"header\",\"schema\":5,\"process\":\"LockApp.exe\","
           << L"\"pid\":" << GetCurrentProcessId() << L",\"osVersion\":\""
           << JsonEscape(GetOsVersionString()) << L"\",\"settings\":{"
           << L"\"properties\":" << (g_settings.includeProperties ? L"true" : L"false")
           << L",\"styleRelevantPropertiesOnly\":"
           << (g_settings.styleRelevantPropertiesOnly ? L"true" : L"false")
           << L",\"visualStates\":" << (g_settings.includeVisualStates ? L"true" : L"false")
           << L",\"rectangle\":" << (g_settings.includeRectangle ? L"true" : L"false")
           << L",\"settleDelayMs\":" << g_settings.settleDelayMs
           << L",\"initialSnapshotDelayMs\":" << g_settings.initialSnapshotDelayMs
           << L",\"maxSnapshotWaitMs\":" << g_settings.maxSnapshotWaitMs
           << L",\"verificationSnapshotDelayMs\":" << g_settings.verificationSnapshotDelayMs
           << L",\"maxSnapshots\":" << g_settings.maxSnapshots
           << L",\"interactionTraceSeconds\":" << g_settings.interactionTraceSeconds
           << L",\"interactionTraceIntervalMs\":" << g_settings.interactionTraceIntervalMs
           << L",\"periodicSnapshotIntervalMs\":" << g_settings.periodicSnapshotIntervalMs
           << L",\"continuousCapture\":" << (g_settings.continuousCapture ? L"true" : L"false")
           << L",\"maxFileSizeMB\":" << g_settings.maxFileSizeMB
           << L"}}";
        g_writer.Write(ss.str());
    }

    std::wstring BuildPath(InstanceHandle handle) {
        std::vector<std::wstring> components;
        InstanceHandle current = handle;

        std::lock_guard<std::mutex> lock(m_elementsMutex);
        while (current && components.size() < 256) {
            auto it = m_elements.find(current);
            if (it == m_elements.end()) {
                break;
            }

            const ElementMeta& meta = it->second;
            if (!meta.parent) {
                break; // Match UWPSpy: don't include the root object itself.
            }

            const std::wstring& type =
                meta.runtimeType.empty() ? meta.declaredType : meta.runtimeType;
            std::wstring component = ShortenClassName(type);
            if (!meta.name.empty()) {
                component += L"#";
                component += meta.name;
            }
            if (meta.name.empty()) {
                component += L"[" + std::to_wstring(meta.childIndex + 1) + L"]";
            }
            components.push_back(std::move(component));
            current = meta.parent;
        }

        std::reverse(components.begin(), components.end());
        std::wstring path;
        for (size_t i = 0; i < components.size(); ++i) {
            if (i) path += L" > ";
            path += components[i];
        }
        return path;
    }

    void AppendRectangle(std::wostringstream& ss, wf::IInspectable const& obj) {
        ss << L",\"rectangle\":";
        if (!g_settings.includeRectangle) {
            ss << L"null";
            return;
        }

        try {
            auto uiElement = obj.try_as<wux::UIElement>();
            auto frameworkElement = obj.try_as<wux::FrameworkElement>();
            if (!uiElement || !frameworkElement) {
                ss << L"null";
                return;
            }

            auto bounds = uiElement.TransformToVisual(nullptr).TransformBounds(
                {0, 0, static_cast<float>(frameworkElement.ActualWidth()),
                 static_cast<float>(frameworkElement.ActualHeight())});
            ss << L"{\"x\":"; AppendDumpNumber(ss, bounds.X);
            ss << L",\"y\":"; AppendDumpNumber(ss, bounds.Y);
            ss << L",\"width\":"; AppendDumpNumber(ss, bounds.Width);
            ss << L",\"height\":"; AppendDumpNumber(ss, bounds.Height);
            ss << L"}";
        } catch (...) {
            ss << L"null";
        }
    }

    void AppendProperties(std::wostringstream& ss, InstanceHandle handle) {
        ss << L",\"properties\":[";
        if (!g_settings.includeProperties) {
            ss << L"]";
            return;
        }

        unsigned int sourceCount = 0;
        PropertyChainSource* sources = nullptr;
        unsigned int propertyCount = 0;
        PropertyChainValue* values = nullptr;

        HRESULT hr = m_visualTreeService->GetPropertyValuesChain(
            handle, &sourceCount, &sources, &propertyCount, &values);
        if (FAILED(hr)) {
            ss << L"],\"propertiesHresult\":" << static_cast<long>(hr);
            return;
        }

        bool firstProperty = true;
        for (unsigned int i = 0; i < propertyCount; ++i) {
            const auto& p = values[i];
            std::wstring_view propertyName = p.PropertyName ? p.PropertyName : L"";
            if (g_settings.styleRelevantPropertiesOnly &&
                !IsStyleRelevantProperty(propertyName)) {
                continue;
            }

            if (!firstProperty) ss << L',';
            firstProperty = false;

            BaseValueSource source = BaseValueSourceUnknown;
            if (p.PropertyChainIndex < sourceCount) {
                source = sources[p.PropertyChainIndex].Source;
            }

            ss << L"{\"name\":";
            AppendJsonString(ss, propertyName);
            ss << L",\"value\":";
            AppendJsonString(ss, FormatPropertyValue(p, m_xamlDiagnostics.get()));
            ss << L",\"overridden\":" << (p.Overridden ? L"true" : L"false")
               << L",\"source\":" << static_cast<int>(source)
               << L",\"local\":"
               << (source == BaseValueSourceLocal ? L"true" : L"false")
               << L",\"metadataBits\":"
               << static_cast<unsigned long>(p.MetadataBits) << L"}";
        }

        CoTaskMemFree(sources);
        CoTaskMemFree(values);
        ss << L"]";
    }

    void AppendVisualStates(std::wostringstream& ss, wf::IInspectable const& obj) {
        ss << L",\"visualStateGroups\":";
        try {
            auto fe = obj.try_as<wux::FrameworkElement>();
            auto groups = fe && g_settings.includeVisualStates ? VisualStateDumpDetails(fe) : L"[]";
            ss << groups;
        } catch (...) {
            ss << L"[],\"visualStatesHresult\":" << static_cast<long>(winrt::to_hresult());
        }
    }

    void RecordAdd(ParentChildRelation relation, VisualElement element) {
        ElementMeta meta;
        meta.parent = relation.Parent;
        meta.childIndex = relation.ChildIndex;
        meta.declaredType = element.Type ? element.Type : L"";
        meta.name = element.Name ? element.Name : L"";

        {
            std::lock_guard<std::mutex> lock(m_elementsMutex);
            m_elements[element.Handle] = std::move(meta);
        }

        if (g_writer.HasOutput()) {
            // Keep construction-time events intentionally cheap. Runtime class,
            // rectangle and properties are resolved from the settled snapshot.
            std::wostringstream ss;
            ss.imbue(std::locale::classic());
            ss << std::setprecision(9);
            ss << L"{\"event\":\"add\",\"elapsedMs\":" << GetTickCount64()-m_startedTick << L",\"handle\":"
               << static_cast<unsigned long long>(element.Handle)
               << L",\"parent\":" << static_cast<unsigned long long>(relation.Parent)
               << L",\"childIndex\":" << relation.ChildIndex
               << L",\"declaredType\":";
            AppendJsonString(ss, element.Type ? element.Type : L"");
            ss << L",\"name\":";
            AppendJsonString(ss, element.Name ? element.Name : L"");
            ss << L"}";
            g_writer.Write(ss.str());
        }
    }

    void RecordRemove(VisualElement element) {
        if (g_writer.HasOutput()) {
            std::wostringstream ss;
            ss.imbue(std::locale::classic());
            ss << std::setprecision(9);
            ss << L"{\"event\":\"remove\",\"elapsedMs\":" << GetTickCount64()-m_startedTick << L",\"handle\":"
               << static_cast<unsigned long long>(element.Handle) << L"}";
            g_writer.Write(ss.str());
        }

        RemoveLiveElement(element.Handle);
        std::lock_guard<std::mutex> lock(m_elementsMutex);
        m_elements.erase(element.Handle);
    }

    void MarkMutation() {
        m_lastMutationTick.store(GetTickCount64(), std::memory_order_relaxed);
        m_mutationGeneration.fetch_add(1, std::memory_order_relaxed);
    }

    void StartSnapshotScheduler() {
        m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_stopEvent) {
            Wh_Log(L"CreateEvent for snapshot scheduler failed: %u", GetLastError());
            return;
        }

        AddRef();
        m_schedulerThread = CreateThread(
            nullptr, 0,
            [](LPVOID param) -> DWORD {
                auto watcher = reinterpret_cast<VisualTreeWatcher*>(param);
                watcher->SnapshotSchedulerLoop();
                watcher->Release();
                return 0;
            },
            this, 0, nullptr);

        if (!m_schedulerThread) {
            Wh_Log(L"CreateThread for snapshot scheduler failed: %u", GetLastError());
            Release();
            CloseHandle(m_stopEvent);
            m_stopEvent = nullptr;
        }
    }

    void StopSnapshotScheduler() {
        HANDLE thread = m_schedulerThread;
        HANDLE stopEvent = m_stopEvent;
        if (!stopEvent) {
            return;
        }

        SetEvent(stopEvent);
        if (thread && GetCurrentThreadId() != GetThreadId(thread)) {
            WaitForSingleObject(thread, INFINITE);
        }

        if (thread) {
            CloseHandle(thread);
            m_schedulerThread = nullptr;
        }
        CloseHandle(stopEvent);
        m_stopEvent = nullptr;
    }

    std::mutex m_liveMutex;
    std::unordered_map<InstanceHandle,std::shared_ptr<LiveElement>> m_liveElements;
    std::atomic<bool> m_liveReady{false},m_liveStopping{false},m_liveErrorReported{false};
    HWND m_liveWindow=nullptr;

    static void PinFailedCleanup() noexcept {
        // A remaining WinRT delegate contains executable code in this module.
        // In the exceptional case that its UI thread cannot revoke it, keep
        // that code mapped until LockApp exits rather than leaving a stale thunk.
        HMODULE module=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&PinFailedCleanup),&module);
        Wh_Log(L"XAML callback cleanup could not reach its UI thread; module retained until LockApp exits");
    }

    void LiveError(InstanceHandle handle) noexcept {
        if (m_liveErrorReported.exchange(true)) return;
        g_writer.Write(L"{\"event\":\"monitorError\",\"handle\":"+std::to_wstring(handle)+
            L",\"message\":\"An element could not be monitored on its XAML thread; coverage is incomplete\"}");
        Wh_Log(L"Continuous XAML monitoring could not attach to an element; see monitorError in the file");
    }
    void TrackLiveElement(InstanceHandle handle) noexcept {
        if (!m_liveReady.load() || m_liveStopping.load() || !g_writer.HasOutput()) return;
        try {
            { std::lock_guard lock(m_liveMutex); if (m_liveElements.count(handle)) return; }
            auto node=FromHandle(handle).try_as<wux::FrameworkElement>(); if (!node) return;
            ElementMeta meta;
            { std::lock_guard lock(m_elementsMutex); auto it=m_elements.find(handle); if (it==m_elements.end()) return; meta=it->second; }
            auto state=LiveElement::Create(node,handle,m_startedTick);
            g_writer.Write(state->Baseline(meta.parent,meta.childIndex,BuildPath(handle)));
            state->GeometryChanged();
            std::lock_guard lock(m_liveMutex); m_liveElements.emplace(handle,std::move(state));
        } catch (...) { LiveError(handle); }
    }
    void RemoveLiveElement(InstanceHandle handle) noexcept {
        try {
            std::shared_ptr<LiveElement> state;
            { std::lock_guard lock(m_liveMutex);
              auto it=m_liveElements.find(handle); if (it==m_liveElements.end()) return;
              // Retain subscriptions for normal UI-thread teardown if diagnostics
              // unexpectedly delivers removal from a different apartment.
              if (it->second->threadId!=GetCurrentThreadId()) { LiveError(handle); return; }
              state=std::move(it->second); m_liveElements.erase(it); }
            state->Stop();
        } catch (...) { LiveError(handle); }
    }
    void StartLiveCapture(HWND window) {
        if (!g_settings.continuousCapture || m_liveStopping.load()) return;
        m_liveWindow=window;
        if (!RunFromWindowThread(window,[](PVOID param) {
            auto self=static_cast<VisualTreeWatcher*>(param);
            if (self->m_liveStopping.load()) return;
            self->m_liveReady.store(true);
            g_writer.Write(L"{\"event\":\"liveStart\",\"elapsedMs\":"+std::to_wstring(GetTickCount64()-self->m_startedTick)+L"}");
            std::vector<InstanceHandle> handles;
            { std::lock_guard lock(self->m_elementsMutex); for (auto const& [handle,_]:self->m_elements) handles.push_back(handle); }
            for (auto handle:handles) self->TrackLiveElement(handle);
            g_writer.Write(L"{\"event\":\"liveReady\",\"elapsedMs\":"+std::to_wstring(GetTickCount64()-self->m_startedTick)+
                L",\"monitoredElements\":"+std::to_wstring(self->m_liveElements.size())+L"}");
        },this)) LiveError(0);
    }
    void StopLiveCapture() noexcept {
        if (m_liveStopping.exchange(true)) return;
        m_liveReady.store(false);
        auto stop=[](PVOID param) {
            auto self=static_cast<VisualTreeWatcher*>(param);
            std::unordered_map<InstanceHandle,std::shared_ptr<LiveElement>> old;
            { std::lock_guard lock(self->m_liveMutex); old.swap(self->m_liveElements); }
            for (auto& [_,state]:old) state->Stop();
        };
        try {
            if (m_liveWindow && IsWindow(m_liveWindow)) {
                if (!RunFromWindowThread(m_liveWindow,stop,this)) {
                    std::lock_guard lock(m_liveMutex);
                    for (auto& [_,state]:m_liveElements) state->active.store(false);
                    PinFailedCleanup();
                }
            } else {
                bool sameThread=true;
                { std::lock_guard lock(m_liveMutex);
                  for (auto const& [_,state]:m_liveElements)
                      sameThread&=state->threadId==GetCurrentThreadId(); }
                if (sameThread) stop(this);
                else {
                    // The HWND can disappear before retained XAML objects do.
                    std::lock_guard lock(m_liveMutex);
                    for (auto& [_,state]:m_liveElements) state->active.store(false);
                    PinFailedCleanup();
                }
            }
            g_writer.Write(L"{\"event\":\"liveStop\",\"elapsedMs\":"+std::to_wstring(GetTickCount64()-m_startedTick)+L"}");
        } catch (...) { LiveError(0); }
    }

    std::vector<InstanceHandle> m_traceHandles;
    std::unordered_map<InstanceHandle, std::wstring> m_lastTraceValues;
    ULONGLONG m_traceStartTick = 0;
    ULONGLONG m_nextTraceTick = 0;

    struct TraceRequest {
        VisualTreeWatcher* watcher;
        std::vector<std::wstring> records;
    };

    static bool IsInteractionTarget(ElementMeta const& meta, std::wstring const& path) {
        for (auto name : {L"LockScreenTextBadgeContent", L"TimeAndDatePanel", L"Time", L"Date",
                          L"WidgetCanvasPanel", L"MediaControlsContainer", L"MediaTransportControls",
                          L"LockCreativeOverlay", L"LockScrollViewer", L"SpotRectangle",
                          L"ExpandoBackground", L"Win10SpotlightBadge", L"Win10MediaButtonRoot", L"Backdrop"})
            if (meta.name == name) return true;
        if (meta.runtimeType == L"Windows.UI.Xaml.Shapes.Ellipse" && path.find(L"Hotspot") != std::wstring::npos)
            return true;
        // Original Win10 media hover states live on this Grid, not the button.
        return meta.name == L"RootGrid" && path.find(L"MediaTransportControls") != std::wstring::npos;
    }

    void CollectInteractionTrace(TraceRequest& request) {
        for (auto handle : m_traceHandles) {
            try {
                auto obj = FromHandle(handle);
                std::wostringstream data;
                data.imbue(std::locale::classic());
                AppendRectangle(data, obj);
                data << L",\"effective\":" << RuntimeDumpDetails(obj);
                auto value = data.str();
                auto it = m_lastTraceValues.find(handle);
                if (it != m_lastTraceValues.end() && it->second == value) continue;
                m_lastTraceValues[handle] = value;
                std::wostringstream record;
                record.imbue(std::locale::classic());
                record << std::setprecision(9);
                record << L"{\"event\":\"interaction\",\"elapsedMs\":" << GetTickCount64() - m_startedTick
                       << L",\"handle\":" << handle << L",\"path\":";
                AppendJsonString(record, BuildPath(handle));
                record << value << L'}';
                request.records.push_back(record.str());
            } catch (...) {
                // A control can disappear at any point while dismissing LockApp.
            }
        }
    }

    void TakeInteractionTrace() {
        auto windows = GetCoreWindows();
        if (windows.empty()) return;
        TraceRequest request{this, {}};
        if (RunFromWindowThread(windows.front(), [](PVOID param) {
                auto& request = *static_cast<TraceRequest*>(param);
                request.watcher->CollectInteractionTrace(request);
            }, &request)) {
            g_writer.WriteBatch(request.records);
        }
    }

    void RefreshSnapshotRelations(std::vector<InstanceHandle> const& handles) {
        // Diagnostic add indices can be stale after inserting siblings. Refresh
        // from the actual visual tree before building copyable indexed selectors.
        for (auto handle : handles) {
            try {
                auto obj = FromHandle(handle);
                auto node = obj.try_as<wux::DependencyObject>();
                std::wstring type(winrt::get_class_name(obj));
                auto fe = obj.try_as<wux::FrameworkElement>();
                std::wstring name = fe ? std::wstring(fe.Name()) : L"";
                InstanceHandle parentHandle = 0;
                unsigned childIndex = 0;
                bool liveRelation = false;
                if (node) {
                    auto parent = wuxm::VisualTreeHelper::GetParent(node);
                    if (parent && SUCCEEDED(m_xamlDiagnostics->GetHandleFromIInspectable(
                            reinterpret_cast<::IInspectable*>(winrt::get_abi(parent)), &parentHandle))) {
                        for (int i = 0; i < wuxm::VisualTreeHelper::GetChildrenCount(parent); ++i) {
                            if (wuxm::VisualTreeHelper::GetChild(parent, i) == node) {
                                childIndex = static_cast<unsigned>(i);
                                liveRelation = true;
                                break;
                            }
                        }
                    }
                }
                std::lock_guard lock(m_elementsMutex);
                auto it = m_elements.find(handle);
                if (it == m_elements.end()) continue;
                it->second.runtimeType = std::move(type);
                it->second.name = std::move(name);
                if (liveRelation) {
                    it->second.parent = parentHandle;
                    it->second.childIndex = childIndex;
                }
            } catch (...) {}
        }
    }


    void SnapshotSchedulerLoop() {
        while (WaitForSingleObject(m_stopEvent, g_settings.interactionTraceSeconds ? std::min(250, g_settings.interactionTraceIntervalMs) : 250) == WAIT_TIMEOUT) {
            if (!g_writer.HasOutput()) {
                continue;
            }

            const ULONGLONG traceNow = GetTickCount64();
            if (m_traceStartTick && g_settings.interactionTraceSeconds &&
                traceNow - m_traceStartTick < static_cast<ULONGLONG>(g_settings.interactionTraceSeconds) * 1000 &&
                traceNow >= m_nextTraceTick) {
                TakeInteractionTrace();
                m_nextTraceTick = GetTickCount64() + g_settings.interactionTraceIntervalMs;
            }
            const uint64_t generation =
                m_mutationGeneration.load(std::memory_order_relaxed);
            if (generation == 0 || m_snapshotCount >=
                                       static_cast<unsigned int>(g_settings.maxSnapshots)) {
                continue;
            }

            const ULONGLONG now = GetTickCount64();
            const ULONGLONG lastMutation =
                m_lastMutationTick.load(std::memory_order_relaxed);
            const bool quiet = now - lastMutation >=
                               static_cast<ULONGLONG>(g_settings.settleDelayMs);

            bool shouldSnapshot = false;
            const wchar_t* reason = L"";

            if (m_snapshotCount == 0) {
                const ULONGLONG sinceStart = now - m_startedTick;
                if (sinceStart >= static_cast<ULONGLONG>(g_settings.maxSnapshotWaitMs)) {
                    shouldSnapshot = true;
                    reason = L"initial-timeout";
                } else if (sinceStart >=
                               static_cast<ULONGLONG>(g_settings.initialSnapshotDelayMs) &&
                           quiet) {
                    shouldSnapshot = true;
                    reason = L"initial-settled";
                }
            } else if (generation != m_lastSnapshottedGeneration) {
                const ULONGLONG sinceSnapshot = now - m_lastSnapshotTick;
                if (quiet) {
                    shouldSnapshot = true;
                    reason = L"late-settled";
                } else if (sinceSnapshot >=
                           static_cast<ULONGLONG>(g_settings.maxSnapshotWaitMs)) {
                    shouldSnapshot = true;
                    reason = L"late-timeout";
                }
            } else if (!m_verificationSnapshotTaken &&
                       g_settings.verificationSnapshotDelayMs > 0 &&
                       now - m_lastSnapshotTick >=
                           static_cast<ULONGLONG>(g_settings.verificationSnapshotDelayMs)) {
                // Tree callbacks can stop before bindings/layout finish. One
                // unconditional verification snapshot catches that class of change.
                shouldSnapshot = true;
                reason = L"verification";
            }

            if (!shouldSnapshot && m_snapshotCount && g_settings.periodicSnapshotIntervalMs > 0 &&
                now - m_lastSnapshotTick >= static_cast<ULONGLONG>(g_settings.periodicSnapshotIntervalMs)) {
                shouldSnapshot = true;
                reason = L"periodic";
            }
            if (shouldSnapshot) {
                TakeScheduledSnapshot(generation, reason);
                if (g_settings.continuousCapture && m_snapshotCount) break;
            }
        }
    }

    static void WINAPI SnapshotUiThreadProc(PVOID param) {
        auto request = reinterpret_cast<SnapshotRequest*>(param);
        request->watcher->CollectSnapshotOnUiThread(*request);
    }

    void TakeScheduledSnapshot(uint64_t generation, const wchar_t* reason) {
        auto windows = GetCoreWindows();
        HWND coreWindow = nullptr;
        for (HWND hWnd : windows) {
            if (IsWindow(hWnd) && IsWindowVisible(hWnd)) {
                coreWindow = hWnd;
                break;
            }
        }
        if (!coreWindow) {
            return;
        }

        SnapshotRequest request;
        request.watcher = this;
        request.sequence = m_snapshotCount + 1;
        request.generation = generation;
        request.reason = reason;

        if (!RunFromWindowThread(coreWindow, SnapshotUiThreadProc, &request)) {
            Wh_Log(L"Failed to marshal XAML snapshot to LockApp CoreWindow thread");
            return;
        }

        std::vector<std::wstring> batch;
        batch.reserve(request.elementRecords.size() + 2);

        std::wostringstream begin;
        begin.imbue(std::locale::classic());
        begin << std::setprecision(9);
        begin << L"{\"event\":\"snapshotBegin\",\"elapsedMs\":" << GetTickCount64()-m_startedTick << L",\"sequence\":"
              << request.sequence << L",\"generation\":" << request.generation
              << L",\"reason\":";
        AppendJsonString(begin, request.reason);
        begin << L",\"attempted\":" << request.attempted << L"}";
        batch.push_back(begin.str());

        for (auto& record : request.elementRecords) {
            batch.push_back(std::move(record));
        }

        std::wostringstream end;
        end.imbue(std::locale::classic());
        end << std::setprecision(9);
        end << L"{\"event\":\"snapshotEnd\",\"elapsedMs\":" << GetTickCount64()-m_startedTick << L",\"sequence\":"
            << request.sequence << L",\"generation\":" << request.generation
            << L",\"captured\":" << request.captured
            << L",\"failures\":" << request.failures
            << L",\"durationMs\":" << request.durationMs << L"}";
        batch.push_back(end.str());

        g_writer.WriteBatch(std::move(batch));
        if (!m_snapshotCount) StartLiveCapture(coreWindow);

        if (!m_snapshotCount) m_traceStartTick = GetTickCount64();
        ++m_snapshotCount;
        m_lastSnapshotTick = GetTickCount64();
        m_lastSnapshottedGeneration = generation;
        if (wcscmp(reason, L"verification") == 0) {
            m_verificationSnapshotTaken = true;
        }

        if (request.failures) Wh_Log(L"XAML snapshot %u completed with errors: %u/%u elements, %u failures, %llu ms (%s)",
               request.sequence, request.captured, request.attempted,
               request.failures,
               static_cast<unsigned long long>(request.durationMs), reason);
    }

    void CollectSnapshotOnUiThread(SnapshotRequest& request) {
        const ULONGLONG started = GetTickCount64();

        std::vector<InstanceHandle> handles;
        {
            std::lock_guard<std::mutex> lock(m_elementsMutex);
            handles.reserve(m_elements.size());
            for (auto const& entry : m_elements) {
                handles.push_back(entry.first);
            }
        }
        RefreshSnapshotRelations(handles);
        m_traceHandles.clear();
        request.attempted = static_cast<unsigned int>(handles.size());
        request.elementRecords.reserve(handles.size());

        for (InstanceHandle handle : handles) {
            try {
                auto obj = FromHandle(handle);
                auto runtimeTypeH = winrt::get_class_name(obj);
                std::wstring runtimeType(runtimeTypeH.data(), runtimeTypeH.size());
                std::wstring name;
                if (auto fe = obj.try_as<wux::FrameworkElement>()) {
                    name = fe.Name().c_str();
                }

                ElementMeta meta;
                bool stillPresent = false;
                {
                    std::lock_guard<std::mutex> lock(m_elementsMutex);
                    auto it = m_elements.find(handle);
                    if (it != m_elements.end()) {
                        if (!runtimeType.empty()) {
                            it->second.runtimeType = runtimeType;
                        }
                        if (!name.empty()) {
                            it->second.name = name;
                        }
                        meta = it->second;
                        stillPresent = true;
                    }
                }
                if (!stillPresent) {
                    continue;
                }

                std::wstring path = BuildPath(handle);
                std::wostringstream ss;
                ss.imbue(std::locale::classic());
                ss << std::setprecision(9);
                ss << L"{\"event\":\"snapshot\",\"sequence\":"
                   << request.sequence << L",\"handle\":"
                   << static_cast<unsigned long long>(handle)
                   << L",\"parent\":" << static_cast<unsigned long long>(meta.parent)
                   << L",\"childIndex\":" << meta.childIndex
                   << L",\"declaredType\":";
                AppendJsonString(ss, meta.declaredType);
                ss << L",\"runtimeType\":";
                AppendJsonString(ss, meta.runtimeType);
                ss << L",\"name\":";
                AppendJsonString(ss, meta.name);
                ss << L",\"path\":";
                AppendJsonString(ss, path);

                if (IsInteractionTarget(meta, path)) m_traceHandles.push_back(handle);
                AppendRectangle(ss, obj);
                try {
                    auto details = RuntimeDumpDetails(obj);
                    ss << L",\"effective\":" << details;
                } catch (...) {
                    ss << L",\"effective\":null,\"effectiveHresult\":" << static_cast<long>(winrt::to_hresult());
                }
                AppendProperties(ss, handle);
                AppendVisualStates(ss, obj);
                ss << L"}";

                request.elementRecords.push_back(ss.str());
                ++request.captured;
            } catch (...) {
                ++request.failures;
            }
        }

        request.durationMs = GetTickCount64() - started;
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation,
        VisualElement element,
        VisualMutationType mutationType) override try {
        if (mutationType == Add) {
            RecordAdd(relation, element);
            TrackLiveElement(element.Handle);
            MarkMutation();
        } else if (mutationType == Remove) {
            RecordRemove(element);
            MarkMutation();
        }
        return S_OK;
    } catch (...) {
        Wh_Log(L"OnVisualTreeChange error: %08X",
               static_cast<DWORD>(winrt::to_hresult()));
        // Returning an error can suppress later callbacks.
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle element,
        VisualElementState elementState,
        LPCWSTR context) noexcept override {
        try {
            MarkMutation();
            if (g_writer.HasOutput()) {
                std::wostringstream ss;
                ss.imbue(std::locale::classic());
                ss << std::setprecision(9);
                ss << L"{\"event\":\"elementState\",\"handle\":"
                   << static_cast<unsigned long long>(element)
                   << L",\"state\":" << static_cast<int>(elementState)
                   << L",\"context\":";
                AppendJsonString(ss, context ? context : L"");
                ss << L"}";
                g_writer.Write(ss.str());
            }
        } catch (...) {
        }
        return S_OK;
    }

    winrt::com_ptr<IXamlDiagnostics> m_xamlDiagnostics;
    winrt::com_ptr<IVisualTreeService3> m_visualTreeService;
    std::mutex m_elementsMutex;
    std::unordered_map<InstanceHandle, ElementMeta> m_elements;

    HANDLE m_stopEvent = nullptr;
    HANDLE m_schedulerThread = nullptr;
    ULONGLONG m_startedTick = 0;
    std::atomic<ULONGLONG> m_lastMutationTick{0};
    std::atomic<uint64_t> m_mutationGeneration{0};
    uint64_t m_lastSnapshottedGeneration = 0;
    ULONGLONG m_lastSnapshotTick = 0;
    unsigned int m_snapshotCount = 0;
    bool m_verificationSnapshotTaken = false;
};

winrt::com_ptr<VisualTreeWatcher> g_visualTreeWatcher;

// {C85D8CC7-5463-40E8-A432-F5916B6427E5}
static constexpr CLSID CLSID_WindhawkTAP = {
    0xc85d8cc7,
    0x5463,
    0x40e8,
    {0xa4, 0x32, 0xf5, 0x91, 0x6b, 0x64, 0x27, 0xe5}};

class WindhawkTAP
    : public winrt::implements<WindhawkTAP, IObjectWithSite,
                               winrt::non_agile> {
public:
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override try {
        if (g_visualTreeWatcher) {
            g_visualTreeWatcher->UnadviseVisualTreeChange();
            g_visualTreeWatcher = nullptr;
        }

        m_site.copy_from(site);
        if (m_site) {
            // InitializeXamlDiagnosticsEx loads our module once more.
            FreeLibrary(GetCurrentModuleHandle());
            g_visualTreeWatcher = winrt::make_self<VisualTreeWatcher>(m_site);
        }
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** site) noexcept override {
        return m_site.as(riid, site);
    }

private:
    winrt::com_ptr<IUnknown> m_site;
};

template <class T>
struct SimpleFactory
    : winrt::implements<SimpleFactory<T>, IClassFactory, winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid,
                                             void** object) override try {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        *object = nullptr;
        return winrt::make<T>().as(riid, object);
    } catch (...) {
        return winrt::to_hresult();
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override {
        return S_OK;
    }
};

using PFN_INITIALIZE_XAML_DIAGNOSTICS_EX = decltype(&InitializeXamlDiagnosticsEx);

HRESULT InjectWindhawkTAP() noexcept {
    HMODULE module = GetCurrentModuleHandle();
    if (!module) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t location[MAX_PATH];
    DWORD chars = GetModuleFileNameW(module, location, ARRAYSIZE(location));
    if (!chars || chars == ARRAYSIZE(location)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HMODULE xaml =
        LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!xaml) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    auto initializeXamlDiagnosticsEx =
        reinterpret_cast<PFN_INITIALIZE_XAML_DIAGNOSTICS_EX>(
            GetProcAddress(xaml, "InitializeXamlDiagnosticsEx"));
    if (!initializeXamlDiagnosticsEx) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (int i = 0; i < 10000; ++i) {
        wchar_t connectionName[256];
        swprintf_s(connectionName, L"VisualDiagConnection%d", i + 1);

        hr = initializeXamlDiagnosticsEx(connectionName, GetCurrentProcessId(),
                                         L"", location, CLSID_WindhawkTAP,
                                         nullptr);
        if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    }
    return hr;
}

void InitializeTap() {
    if (g_initialized.exchange(true)) {
        return;
    }

    HRESULT hr = InjectWindhawkTAP();
    if (FAILED(hr)) {
        g_initialized = false;
        Wh_Log(L"InjectWindhawkTAP failed: %08X", hr);
    }
}

void UninitializeTap() {
    if (g_visualTreeWatcher) {
        g_visualTreeWatcher->UnadviseVisualTreeChange();
        g_visualTreeWatcher = nullptr;
    }
    g_initialized = false;
}

using RunFromWindowThreadProc_t = void(WINAPI*)(PVOID);

bool RunFromWindowThread(HWND hWnd, RunFromWindowThreadProc_t proc,
                         PVOID param) {
    static const UINT message =
        RegisterWindowMessageW(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct CallParam {
        RunFromWindowThreadProc_t proc;
        PVOID param;
    } call{proc, param};

    DWORD threadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (!threadId) {
        return false;
    }
    if (threadId == GetCurrentThreadId()) {
        proc(param);
        return true;
    }

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (code == HC_ACTION) {
                auto cwp = reinterpret_cast<const CWPSTRUCT*>(lParam);
                if (cwp->message == message) {
                    auto call = reinterpret_cast<CallParam*>(cwp->lParam);
                    call->proc(call->param);
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        },
        nullptr, threadId);
    if (!hook) {
        return false;
    }

    SendMessageW(hWnd, message, 0, reinterpret_cast<LPARAM>(&call));
    UnhookWindowsHookEx(hook);
    return true;
}

void OnWindowCreated(HWND hWnd, LPCWSTR className) {
    if (!className ||
        (reinterpret_cast<ULONG_PTR>(className) & ~static_cast<ULONG_PTR>(0xFFFF)) == 0) {
        return;
    }

    if (_wcsicmp(className, L"Windows.UI.Core.CoreWindow") == 0) {

        InitializeTap();
    }
}

using CreateWindowInBand_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int,
                                           int, int, int, HWND, HMENU,
                                           HINSTANCE, PVOID, DWORD);
CreateWindowInBand_t CreateWindowInBand_Original = nullptr;

HWND WINAPI CreateWindowInBand_Hook(DWORD exStyle, LPCWSTR className,
                                    LPCWSTR windowName, DWORD style, int x,
                                    int y, int width, int height, HWND parent,
                                    HMENU menu, HINSTANCE instance, PVOID param,
                                    DWORD band) {
    HWND hWnd = CreateWindowInBand_Original(
        exStyle, className, windowName, style, x, y, width, height, parent, menu,
        instance, param, band);
    if (hWnd) {
        OnWindowCreated(hWnd, className);
    }
    return hWnd;
}

using CreateWindowInBandEx_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD,
                                             int, int, int, int, HWND, HMENU,
                                             HINSTANCE, PVOID, DWORD, DWORD);
CreateWindowInBandEx_t CreateWindowInBandEx_Original = nullptr;

HWND WINAPI CreateWindowInBandEx_Hook(DWORD exStyle, LPCWSTR className,
                                      LPCWSTR windowName, DWORD style, int x,
                                      int y, int width, int height, HWND parent,
                                      HMENU menu, HINSTANCE instance,
                                      PVOID param, DWORD band, DWORD typeFlags) {
    HWND hWnd = CreateWindowInBandEx_Original(
        exStyle, className, windowName, style, x, y, width, height, parent, menu,
        instance, param, band, typeFlags);
    if (hWnd) {
        OnWindowCreated(hWnd, className);
    }
    return hWnd;
}

std::vector<HWND> GetCoreWindows() {
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND hWnd, LPARAM param) -> BOOL {
            auto windows = reinterpret_cast<std::vector<HWND>*>(param);
            DWORD pid = 0;
            if (!GetWindowThreadProcessId(hWnd, &pid) ||
                pid != GetCurrentProcessId()) {
                return TRUE;
            }

            wchar_t className[64];
            if (GetClassNameW(hWnd, className, ARRAYSIZE(className)) &&
                _wcsicmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
                windows->push_back(hWnd);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

} // namespace

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdll-attribute-on-redeclaration"

__declspec(dllexport) _Use_decl_annotations_ STDAPI
DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* object) try {
    if (rclsid != CLSID_WindhawkTAP) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    *object = nullptr;
    return winrt::make<SimpleFactory<WindhawkTAP>>().as(riid, object);
} catch (...) {
    return winrt::to_hresult();
}

__declspec(dllexport) _Use_decl_annotations_ STDAPI DllCanUnloadNow(void) {
    return winrt::get_module_lock() ? S_FALSE : S_OK;
}

#pragma clang diagnostic pop

BOOL Wh_ModInit() {
    LoadSettings();


    HMODULE user32 =
        LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user32) {
        if (void* p = reinterpret_cast<void*>(
                GetProcAddress(user32, "CreateWindowInBand"))) {
            Wh_SetFunctionHook(p, reinterpret_cast<void*>(CreateWindowInBand_Hook),
                               reinterpret_cast<void**>(&CreateWindowInBand_Original));
        }
        if (void* p = reinterpret_cast<void*>(
                GetProcAddress(user32, "CreateWindowInBandEx"))) {
            Wh_SetFunctionHook(p,
                               reinterpret_cast<void*>(CreateWindowInBandEx_Hook),
                               reinterpret_cast<void**>(&CreateWindowInBandEx_Original));
        }
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    auto windows = GetCoreWindows();
    for (HWND hWnd : windows) {

        if (RunFromWindowThread(hWnd, [](PVOID) { InitializeTap(); }, nullptr)) {
            break;
        }
    }
}

void Wh_ModUninit() {

    UninitializeTap();
    g_writer.Close();
}

void Wh_ModSettingsChanged() {
    // Settings remain immutable while the UI and scheduler threads use them.
    // Reload the mod after changing settings to start a new consistent file.

}

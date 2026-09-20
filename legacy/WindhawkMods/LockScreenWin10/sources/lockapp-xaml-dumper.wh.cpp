// ==WindhawkMod==
// @id              lockapp-xaml-dumper
// @name            LockApp XAML Dumper
// @description     Dump LockApp.exe's Windows.UI.Xaml visual tree, properties, geometry and visual states to JSONL for offline comparison.
// @version         0.3.0
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
# LockApp XAML Dumper

Read-only diagnostic mod for LockApp.exe. It attaches to the Windows.UI.Xaml
Diagnostics visual-tree service and writes JSONL records for visual-tree
mutations plus delayed settled snapshots. Add/remove records are intentionally
lightweight so LockApp can construct its UI without the dumper querying hundreds
of properties synchronously. Settled snapshot records include:

- instance handle, parent handle and child index
- runtime class, declared class and x:Name
- a Windhawk-style element path
- current on-screen rectangle
- current style-relevant values from GetPropertyValuesChain by default
- visual-state groups and current states

The preferred output is a file in LockApp's actual ApplicationData LocalFolder.
The exact path is printed in the Windhawk log. To avoid blocking LockApp during
startup, Windhawk-log fallback is disabled by default. It can be enabled for
debugging; records then use small chunks to avoid Windhawk log truncation.

The first snapshot waits for LockApp to become quiet, with a maximum timeout. A
second verification snapshot is taken later even if no tree mutations occur, so
late data binding/layout changes (media, widgets, Spotlight, etc.) are captured.

This mod does not change XAML properties.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- includeProperties: true
  $name: Include properties
  $description: Include XAML Diagnostics properties for each added element.
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
- logFallback: false
  $name: Chunk records to Windhawk log if file output fails
  $description: Debug fallback only. Leave off for normal lock-screen captures because logging thousands of property characters can stall LockApp.
*/
// ==/WindhawkModSettings==

#include <xamlom.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
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
    bool logFallback = false;
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
    g_settings.logFallback = Wh_GetIntSetting(L"logFallback") != 0;
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

class DumpWriter {
public:
    DumpWriter() = default;
    ~DumpWriter() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(m_file);
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
    }

    void Initialize() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) {
            return;
        }
        m_initialized = true;

        std::wstring folder;

        // Don't construct %LOCALAPPDATA%\Packages\<family> manually. LockApp
        // can have an unusual environment/token while on the lock screen, which
        // caused ERROR_PATH_NOT_FOUND on real systems. Ask the package runtime
        // for its actual LocalFolder instead.
        try {
            auto localFolder =
                winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
            folder = localFolder.Path().c_str();
            Wh_Log(L"LockApp ApplicationData LocalFolder: %s", folder.c_str());
        } catch (...) {
            Wh_Log(L"ApplicationData::Current().LocalFolder() failed: %08X",
                   static_cast<DWORD>(winrt::to_hresult()));
        }

        // Legacy fallback only for diagnostics. Do not rely on it as the primary
        // path because this is exactly what failed with error 3 on build 26200.
        if (folder.empty()) {
            UINT32 familyNameLength = 0;
            LONG rc = GetCurrentPackageFamilyName(&familyNameLength, nullptr);
            if (rc == ERROR_INSUFFICIENT_BUFFER && familyNameLength > 1) {
                std::wstring familyName(familyNameLength, L'\0');
                rc = GetCurrentPackageFamilyName(&familyNameLength,
                                                 familyName.data());
                if (rc == ERROR_SUCCESS) {
                    familyName.resize(wcslen(familyName.c_str()));
                    wchar_t localAppData[32768];
                    DWORD chars = GetEnvironmentVariableW(
                        L"LOCALAPPDATA", localAppData, ARRAYSIZE(localAppData));
                    if (chars && chars < ARRAYSIZE(localAppData)) {
                        folder = localAppData;
                        folder += L"\\Packages\\";
                        folder += familyName;
                        folder += L"\\LocalState";
                    }
                }
            }
        }

        if (!folder.empty()) {
            std::wostringstream name;
            name << folder << L"\\LockApp-XamlDump-"
                 << MakeTimestampForFileName() << L"-pid" << GetCurrentProcessId()
                 << L".jsonl";
            m_path = name.str();

            m_file = CreateFileW(
                m_path.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }

        if (m_file == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            Wh_Log(L"LockApp XAML dump file could not be created (error %u).",
                   error);
            if (g_settings.logFallback) {
                Wh_Log(L"Windhawk log fallback is enabled (debug/slow mode).");
            } else {
                Wh_Log(L"Capture output is disabled to avoid stalling LockApp. "
                       L"Enable logFallback only for troubleshooting.");
            }
        } else {
            Wh_Log(L"LockApp XAML dump file: %s", m_path.c_str());
        }
    }

    const std::wstring& Path() const {
        return m_path;
    }

    bool HasOutput() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_file != INVALID_HANDLE_VALUE || g_settings.logFallback;
    }

    void Write(std::wstring_view json) {
        std::lock_guard<std::mutex> lock(m_mutex);
        WriteLocked(json);
    }

    void WriteBatch(const std::vector<std::wstring>& records) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto const& record : records) {
            WriteLocked(record);
        }
    }

private:
    void WriteLocked(std::wstring_view json) {
        ++m_sequence;

        bool wroteFile = false;
        if (m_file != INVALID_HANDLE_VALUE) {
            std::string utf8 = Utf8FromWide(json);
            utf8.push_back('\n');

            DWORD written = 0;
            wroteFile = WriteFile(m_file, utf8.data(),
                                  static_cast<DWORD>(utf8.size()), &written,
                                  nullptr) &&
                        written == utf8.size();
            if (!wroteFile) {
                Wh_Log(L"Dump file write failed at record %llu (error %u)",
                       static_cast<unsigned long long>(m_sequence),
                       GetLastError());
                CloseHandle(m_file);
                m_file = INVALID_HANDLE_VALUE;
            }
        }

        if (!wroteFile && g_settings.logFallback) {
            WriteChunkedToLog(m_sequence, json);
        }
    }

    static void WriteChunkedToLog(uint64_t sequence, std::wstring_view text) {
        // Keep individual log calls modest in size. The BEGIN/CHUNK/END markers
        // make it possible to reassemble a record after exporting the log.
        constexpr size_t kChunkChars = 650;
        size_t chunkCount = (text.size() + kChunkChars - 1) / kChunkChars;
        if (chunkCount == 0) {
            chunkCount = 1;
        }

        Wh_Log(L"[XAMLDUMP:%llu:BEGIN:%llu]",
               static_cast<unsigned long long>(sequence),
               static_cast<unsigned long long>(chunkCount));

        for (size_t i = 0; i < chunkCount; ++i) {
            size_t start = i * kChunkChars;
            size_t len = std::min(kChunkChars, text.size() - start);
            std::wstring chunk(text.substr(start, len));
            Wh_Log(L"[XAMLDUMP:%llu:CHUNK:%llu/%llu] %s",
                   static_cast<unsigned long long>(sequence),
                   static_cast<unsigned long long>(i + 1),
                   static_cast<unsigned long long>(chunkCount), chunk.c_str());
        }

        Wh_Log(L"[XAMLDUMP:%llu:END]",
               static_cast<unsigned long long>(sequence));
    }

    mutable std::mutex m_mutex;
    bool m_initialized = false;
    HANDLE m_file = INVALID_HANDLE_VALUE;
    std::wstring m_path;
    uint64_t m_sequence = 0;
};

DumpWriter g_writer;

template <typename GridLengthType>
std::wstring FormatGridLength(GridLengthType const& length) {
    std::wostringstream ss;
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
            ss << L" {Color=" << color << L"}";
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
        L"Content", L"HorizontalOffset", L"VerticalOffset"
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
    }

    void UnadviseVisualTreeChange() {
        // Stop future snapshot scheduling first. LockApp can suspend as soon as
        // it leaves the visible lock screen, so never rely on post-unlock work.
        StopSnapshotScheduler();

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
        ss << L"{\"event\":\"header\",\"schema\":3,\"process\":\"LockApp.exe\","
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
           << L"}}";
        g_writer.Write(ss.str());
    }

    std::wstring BuildPath(InstanceHandle handle) {
        std::vector<std::wstring> components;
        InstanceHandle current = handle;

        std::lock_guard<std::mutex> lock(m_elementsMutex);
        while (current) {
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

            auto p = uiElement.TransformToVisual(nullptr).TransformPoint({0, 0});
            double w = frameworkElement.ActualWidth();
            double h = frameworkElement.ActualHeight();
            ss << L"{\"x\":" << p.X << L",\"y\":" << p.Y
               << L",\"width\":" << w << L",\"height\":" << h << L"}";
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
            ss << L",\"source\":" << static_cast<int>(source)
               << L",\"local\":"
               << (source == BaseValueSourceLocal ? L"true" : L"false")
               << L",\"metadataBits\":"
               << static_cast<unsigned long>(p.MetadataBits) << L"}";
        }

        CoTaskMemFree(sources);
        CoTaskMemFree(values);
        ss << L"]";
    }

    void AppendVisualStates(std::wostringstream& ss,
                            wf::IInspectable const& obj) {
        ss << L",\"visualStateGroups\":[";
        if (!g_settings.includeVisualStates) {
            ss << L"]";
            return;
        }

        try {
            auto fe = obj.try_as<wux::FrameworkElement>();
            if (!fe) {
                ss << L"]";
                return;
            }

            auto groups = wux::VisualStateManager::GetVisualStateGroups(fe);
            bool firstGroup = true;
            for (auto const& group : groups) {
                if (!firstGroup) ss << L',';
                firstGroup = false;

                std::wstring groupName = group.Name().c_str();
                auto current = group.CurrentState();

                ss << L"{\"name\":";
                AppendJsonString(ss, groupName);
                ss << L",\"current\":";
                std::wstring currentName = current ? current.Name().c_str() : L"";
                AppendJsonString(ss, currentName);
                ss << L",\"states\":[";

                bool firstState = true;
                for (auto const& state : group.States()) {
                    if (!firstState) ss << L',';
                    firstState = false;
                    std::wstring stateName = state.Name().c_str();
                    AppendJsonString(ss, stateName);
                }
                ss << L"]}";
            }
        } catch (...) {
            // Visual states are diagnostic-only. Keep the main element record.
        }
        ss << L"]";
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
            ss << L"{\"event\":\"add\",\"handle\":"
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
            ss << L"{\"event\":\"remove\",\"handle\":"
               << static_cast<unsigned long long>(element.Handle) << L"}";
            g_writer.Write(ss.str());
        }

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

    void SnapshotSchedulerLoop() {
        while (WaitForSingleObject(m_stopEvent, 250) == WAIT_TIMEOUT) {
            if (!g_writer.HasOutput()) {
                continue;
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

            if (shouldSnapshot) {
                TakeScheduledSnapshot(generation, reason);
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
        begin << L"{\"event\":\"snapshotBegin\",\"sequence\":"
              << request.sequence << L",\"generation\":" << request.generation
              << L",\"reason\":";
        AppendJsonString(begin, request.reason);
        begin << L",\"attempted\":" << request.attempted << L"}";
        batch.push_back(begin.str());

        for (auto& record : request.elementRecords) {
            batch.push_back(std::move(record));
        }

        std::wostringstream end;
        end << L"{\"event\":\"snapshotEnd\",\"sequence\":"
            << request.sequence << L",\"generation\":" << request.generation
            << L",\"captured\":" << request.captured
            << L",\"failures\":" << request.failures
            << L",\"durationMs\":" << request.durationMs << L"}";
        batch.push_back(end.str());

        g_writer.WriteBatch(batch);

        ++m_snapshotCount;
        m_lastSnapshotTick = GetTickCount64();
        m_lastSnapshottedGeneration = generation;
        if (wcscmp(reason, L"verification") == 0) {
            m_verificationSnapshotTaken = true;
        }

        Wh_Log(L"XAML snapshot %u complete: %u/%u elements, %u failures, %llu ms (%s)",
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

                AppendRectangle(ss, obj);
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
        Wh_Log(L"LockApp CoreWindow created: %p", hWnd);
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
    Wh_Log(L"LockApp XAML Dumper initializing");

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
        Wh_Log(L"Existing LockApp CoreWindow: %p", hWnd);
        if (RunFromWindowThread(hWnd, [](PVOID) { InitializeTap(); }, nullptr)) {
            break;
        }
    }
}

void Wh_ModUninit() {
    Wh_Log(L"LockApp XAML Dumper uninitializing");
    UninitializeTap();
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    // Snapshot timing and filtering settings are read live, but restart/reload
    // the mod for a fresh, self-consistent capture file.
    Wh_Log(L"LockApp XAML Dumper settings changed; reload LockApp/mod for a fresh dump");
}

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

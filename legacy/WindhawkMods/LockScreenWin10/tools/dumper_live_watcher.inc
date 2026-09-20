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

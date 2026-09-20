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

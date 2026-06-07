#ifndef AIP_DEPENDENCIES_LOGGING_INC
#define AIP_DEPENDENCIES_LOGGING_INC

namespace aip
{
struct Utf8LoggerOptions
{
    bool enabled = true;
    bool fileEnabled = true;
    std::wstring filePath;
    size_t maxRecentLines = 250;
};

inline bool AppendUtf8LineToFile(const std::wstring& filePath, const std::wstring& line)
{
    if (filePath.empty())
    {
        return false;
    }

    EnsureDirectory(GetDirectoryName(filePath));
    HANDLE file = CreateFileW(
        filePath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    std::string utf8 = WideToUtf8(line + L"\r\n");
    const char* cursor = utf8.data();
    DWORD remaining = static_cast<DWORD>(utf8.size());
    bool ok = true;
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(file, cursor, remaining, &written, nullptr) || written == 0)
        {
            ok = false;
            break;
        }
        cursor += written;
        remaining -= written;
    }

    CloseHandle(file);
    return ok;
}

class Utf8Logger
{
public:
    void Configure(const Utf8LoggerOptions& options)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = options.enabled;
        fileEnabled_ = options.fileEnabled;
        filePath_ = options.filePath;
        maxRecentLines_ = options.maxRecentLines == 0 ? 1 : options.maxRecentLines;
        TrimRecentLocked();
    }

    void SetFilePath(const std::wstring& filePath)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        filePath_ = filePath;
    }

    std::wstring FilePath() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return filePath_;
    }

    void Write(const wchar_t* level, const std::wstring& message)
    {
        SYSTEMTIME st = {};
        GetLocalTime(&st);

        wchar_t prefix[96] = {};
        swprintf_s(prefix,
            L"%04u-%02u-%02u %02u:%02u:%02u [%s] ",
            st.wYear,
            st.wMonth,
            st.wDay,
            st.wHour,
            st.wMinute,
            st.wSecond,
            level != nullptr ? level : L"info");

        WriteLine(std::wstring(prefix) + message);
    }

    void WriteRawLine(const std::wstring& line)
    {
        WriteLine(line);
    }

    std::vector<std::wstring> RecentLines() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<std::wstring>(recent_.begin(), recent_.end());
    }

    void ClearRecent()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recent_.clear();
    }

private:
    void WriteLine(const std::wstring& line)
    {
        std::wstring filePath;
        bool shouldWriteFile = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!enabled_)
            {
                return;
            }

            recent_.push_back(line);
            TrimRecentLocked();
            shouldWriteFile = fileEnabled_ && !filePath_.empty();
            filePath = filePath_;
        }

        if (shouldWriteFile)
        {
            AppendUtf8LineToFile(filePath, line);
        }
    }

    void TrimRecentLocked()
    {
        while (recent_.size() > maxRecentLines_)
        {
            recent_.pop_front();
        }
    }

    mutable std::mutex mutex_;
    bool enabled_ = true;
    bool fileEnabled_ = true;
    size_t maxRecentLines_ = 250;
    std::wstring filePath_;
    std::deque<std::wstring> recent_;
};
}

#endif

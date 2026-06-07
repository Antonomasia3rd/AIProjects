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

        if (!shouldWriteFile)
        {
            return;
        }

        EnsureDirectory(GetDirectoryName(filePath));
        HANDLE file = CreateFileW(
            filePath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        std::string utf8 = WideToUtf8(line + L"\r\n");
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
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

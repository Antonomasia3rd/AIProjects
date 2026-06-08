#ifndef AIP_DEPENDENCIES_LOGGING_INC
#define AIP_DEPENDENCIES_LOGGING_INC

namespace aip
{
struct Utf8LoggerOptions
{
    bool enabled = true;
    bool fileEnabled = true;
    bool writeUtf8Bom = true;
    bool reportFileWriteFailuresToRecent = true;
    std::wstring filePath;
    size_t maxRecentLines = 250;
};

inline bool WriteAllBytes(HANDLE file, const void* data, DWORD size)
{
    const BYTE* cursor = static_cast<const BYTE*>(data);
    DWORD remaining = size;
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(file, cursor, remaining, &written, nullptr) || written == 0)
        {
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

class ExclusiveFileRangeLock
{
public:
    explicit ExclusiveFileRangeLock(HANDLE file, DWORD waitMs = INFINITE)
        : file_(file)
    {
        overlapped_ = {};
        if (file_ == INVALID_HANDLE_VALUE)
        {
            error_ = ERROR_INVALID_HANDLE;
            return;
        }

        if (waitMs == INFINITE)
        {
            if (LockFileEx(file_, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlapped_))
            {
                locked_ = true;
            }
            else
            {
                error_ = GetLastError();
            }
            return;
        }

        ULONGLONG start = GetTickCount64();
        for (;;)
        {
            if (LockFileEx(file_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &overlapped_))
            {
                locked_ = true;
                return;
            }

            DWORD err = GetLastError();
            if (err != ERROR_LOCK_VIOLATION)
            {
                error_ = err;
                return;
            }

            ULONGLONG elapsed = GetTickCount64() - start;
            if (waitMs == 0 || elapsed >= waitMs)
            {
                error_ = ERROR_LOCK_VIOLATION;
                return;
            }

            DWORD remaining = static_cast<DWORD>(waitMs - elapsed);
            Sleep(std::min<DWORD>(remaining, 25));
        }
    }

    ~ExclusiveFileRangeLock()
    {
        if (locked_)
        {
            UnlockFileEx(file_, 0, MAXDWORD, MAXDWORD, &overlapped_);
        }
    }

    ExclusiveFileRangeLock(const ExclusiveFileRangeLock&) = delete;
    ExclusiveFileRangeLock& operator=(const ExclusiveFileRangeLock&) = delete;

    bool IsLocked() const
    {
        return locked_;
    }

    DWORD Error() const
    {
        return error_ == ERROR_SUCCESS ? ERROR_LOCK_FAILED : error_;
    }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_ = {};
    bool locked_ = false;
    DWORD error_ = ERROR_SUCCESS;
};

inline bool AppendUtf8TextToFile(
    const std::wstring& filePath,
    const std::wstring& text,
    bool writeUtf8Bom = true,
    DWORD lockWaitMs = INFINITE)
{
    if (filePath.empty())
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    EnsureDirectory(GetDirectoryName(filePath));
    HANDLE file = CreateFileW(
        filePath.c_str(),
        FILE_APPEND_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool ok = true;
    DWORD error = ERROR_SUCCESS;
    {
        ExclusiveFileRangeLock lock(file, lockWaitMs);
        if (!lock.IsLocked())
        {
            ok = false;
            error = lock.Error();
        }
        else
        {
            LARGE_INTEGER size = {};
            if (!GetFileSizeEx(file, &size))
            {
                ok = false;
                error = GetLastError();
            }

            if (ok)
            {
                LARGE_INTEGER end = {};
                if (!SetFilePointerEx(file, end, nullptr, FILE_END))
                {
                    ok = false;
                    error = GetLastError();
                }
            }

            if (ok && writeUtf8Bom && size.QuadPart == 0)
            {
                static const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
                if (!WriteAllBytes(file, bom, static_cast<DWORD>(sizeof(bom))))
                {
                    ok = false;
                    error = GetLastError();
                }
            }

            if (ok)
            {
                std::string utf8 = WideToUtf8(text);
                if (!utf8.empty() && !WriteAllBytes(file, utf8.data(), static_cast<DWORD>(utf8.size())))
                {
                    ok = false;
                    error = GetLastError();
                }
            }
        }
    }

    CloseHandle(file);
    SetLastError(ok ? ERROR_SUCCESS : (error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error));
    return ok;
}

inline bool AppendUtf8LineToFile(const std::wstring& filePath, const std::wstring& line, bool writeUtf8Bom = true)
{
    if (filePath.empty())
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    EnsureDirectory(GetDirectoryName(filePath));
    HANDLE file = CreateFileW(
        filePath.c_str(),
        FILE_APPEND_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool ok = true;
    DWORD error = ERROR_SUCCESS;
    {
        ExclusiveFileRangeLock lock(file);
        if (!lock.IsLocked())
        {
            ok = false;
            error = lock.Error();
        }
        else
        {
            LARGE_INTEGER size = {};
            if (!GetFileSizeEx(file, &size))
            {
                ok = false;
                error = GetLastError();
            }
            else
            {
                LARGE_INTEGER end = {};
                if (!SetFilePointerEx(file, end, nullptr, FILE_END))
                {
                    ok = false;
                    error = GetLastError();
                }

                if (ok && writeUtf8Bom && size.QuadPart == 0)
                {
                    static const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
                    if (!WriteAllBytes(file, bom, static_cast<DWORD>(sizeof(bom))))
                    {
                        ok = false;
                        error = GetLastError();
                    }
                }

                if (ok)
                {
                    std::string utf8 = WideToUtf8(line + L"\r\n");
                    if (!utf8.empty() && !WriteAllBytes(file, utf8.data(), static_cast<DWORD>(utf8.size())))
                    {
                        ok = false;
                        error = GetLastError();
                    }
                }
            }
        }
    }

    CloseHandle(file);
    SetLastError(ok ? ERROR_SUCCESS : (error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error));
    return ok;
}

class Utf8Logger
{
public:
    void Configure(const Utf8LoggerOptions& options)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool targetChanged =
            filePath_ != options.filePath ||
            fileEnabled_ != options.fileEnabled ||
            writeUtf8Bom_ != options.writeUtf8Bom;
        enabled_ = options.enabled;
        fileEnabled_ = options.fileEnabled;
        writeUtf8Bom_ = options.writeUtf8Bom;
        reportFileWriteFailuresToRecent_ = options.reportFileWriteFailuresToRecent;
        filePath_ = options.filePath;
        maxRecentLines_ = options.maxRecentLines == 0 ? 1 : options.maxRecentLines;
        if (targetChanged)
        {
            ResetFileWriteFailureLocked();
        }
        TrimRecentLocked();
    }

    void SetFilePath(const std::wstring& filePath)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (filePath_ != filePath)
        {
            filePath_ = filePath;
            ResetFileWriteFailureLocked();
        }
    }

    std::wstring FilePath() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return filePath_;
    }

    bool LastFileWriteFailed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastFileWriteFailed_;
    }

    DWORD LastFileWriteError() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastFileWriteError_;
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
        ResetFileWriteFailureLocked();
    }

private:
    void ResetFileWriteFailureLocked()
    {
        failureReported_ = false;
        lastFileWriteFailed_ = false;
        lastFileWriteError_ = ERROR_SUCCESS;
    }

    void WriteLine(const std::wstring& line)
    {
        std::wstring filePath;
        bool shouldWriteFile = false;
        bool writeUtf8Bom = true;
        bool reportFailure = true;
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
            writeUtf8Bom = writeUtf8Bom_;
            reportFailure = reportFileWriteFailuresToRecent_;
        }

        if (!shouldWriteFile)
        {
            return;
        }

        if (AppendUtf8LineToFile(filePath, line, writeUtf8Bom))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastFileWriteFailed_ = false;
            lastFileWriteError_ = ERROR_SUCCESS;
            return;
        }

        DWORD error = GetLastError();
        std::lock_guard<std::mutex> lock(mutex_);
        lastFileWriteFailed_ = true;
        lastFileWriteError_ = error;
        if (reportFailure && !failureReported_)
        {
            std::wstring message = L"Log file write failed for " + filePath + L" (" + GetLastErrorText(error) + L")";
            recent_.push_back(message);
            TrimRecentLocked();
            failureReported_ = true;
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
    bool writeUtf8Bom_ = true;
    bool reportFileWriteFailuresToRecent_ = true;
    bool failureReported_ = false;
    bool lastFileWriteFailed_ = false;
    DWORD lastFileWriteError_ = ERROR_SUCCESS;
    size_t maxRecentLines_ = 250;
    std::wstring filePath_;
    std::deque<std::wstring> recent_;
};
}

#endif

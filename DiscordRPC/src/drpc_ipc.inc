class DiscordIpcClient
{
public:
    DiscordIpcClient(std::wstring clientId, bool verbose, IpcOptions options)
        : clientId_(std::move(clientId)), verbose_(verbose), options_(options)
    {
    }

    ~DiscordIpcClient()
    {
        Close();
    }

    bool IsConnected() const
    {
        return pipe_ != INVALID_HANDLE_VALUE;
    }

    void Connect()
    {
        Close();
        if (Trim(clientId_).empty())
        {
            throw std::runtime_error("Missing [general] client_id for Discord IPC.");
        }

        std::wstring lastError;
        for (int i = 0; i < 10; ++i)
        {
            std::wstring pipeName = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            if (!WaitNamedPipeW(pipeName.c_str(), static_cast<DWORD>(options_.connectTimeoutMs)))
            {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_SEM_TIMEOUT)
                {
                    lastError = GetLastErrorText(err);
                }
                continue;
            }

            HANDLE candidate = CreateFileW(
                pipeName.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                nullptr);

            if (candidate == INVALID_HANDLE_VALUE)
            {
                lastError = GetLastErrorText(GetLastError());
                continue;
            }

            pipe_ = candidate;
            pipeName_ = pipeName;
            try
            {
                std::string handshake = "{";
                bool first = true;
                JsonFieldNumber(handshake, first, "v", 1);
                JsonFieldString(handshake, first, "client_id", Trim(clientId_));
                handshake.push_back('}');
                SendFrame(0, handshake);

                std::string response = ReadFrame(options_.responseTimeoutMs);
                if (response.find("\"evt\":\"READY\"") == std::string::npos &&
                    response.find("\"cmd\":\"DISPATCH\"") == std::string::npos)
                {
                    throw std::runtime_error("Unexpected Discord IPC handshake response: " + WideToUtf8(SummarizeJson(response)));
                }

                LogInfo(L"Connected to Discord IPC pipe " + pipeName_ + L".");
                return;
            }
            catch (const std::exception& ex)
            {
                lastError = Utf8ToWide(ex.what());
                Close();
            }
        }

        std::wstring message = lastError.empty() ? L"no Discord IPC pipe found" : lastError;
        throw std::runtime_error(WideToUtf8(L"Discord IPC is unavailable. Make sure the Discord desktop app is running. Last error: " + message));
    }

    void SetActivity(const std::string& activityJson)
    {
        if (!IsConnected())
        {
            throw std::runtime_error("Discord IPC is not connected.");
        }

        std::string payload = BuildSetActivityPayload(activityJson);
        SendFrame(1, payload);
        std::string response = ReadFrame(options_.responseTimeoutMs);
        ThrowIfError(response);
        LogInfo(L"Presence updated via Discord IPC.");
    }

    void ClearActivity()
    {
        if (!IsConnected())
        {
            return;
        }

        try
        {
            SendFrame(1, BuildClearActivityPayload());
        }
        catch (const std::exception&)
        {
        }
    }

    void Close()
    {
        if (pipe_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        pipeName_.clear();
    }

private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::wstring pipeName_;
    std::wstring clientId_;
    bool verbose_ = false;
    IpcOptions options_;

    static void ThrowLastError(const char* action, DWORD error)
    {
        std::wstring message = Utf8ToWide(action) + L" failed: " + GetLastErrorText(error);
        throw std::runtime_error(WideToUtf8(message));
    }

    static DWORD WaitForOverlapped(HANDLE handle, OVERLAPPED& overlapped, DWORD timeoutMs, const char* action)
    {
        DWORD wait = WaitForSingleObject(overlapped.hEvent, timeoutMs);
        if (wait == WAIT_TIMEOUT)
        {
            CancelIoEx(handle, &overlapped);
            throw std::runtime_error(std::string(action) + " timed out.");
        }
        if (wait != WAIT_OBJECT_0)
        {
            ThrowLastError(action, GetLastError());
        }

        DWORD transferred = 0;
        if (!GetOverlappedResult(handle, &overlapped, &transferred, FALSE))
        {
            ThrowLastError(action, GetLastError());
        }
        return transferred;
    }

    void WriteExact(const void* data, DWORD size, DWORD timeoutMs)
    {
        const BYTE* bytes = static_cast<const BYTE*>(data);
        DWORD offset = 0;
        while (offset < size)
        {
            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (overlapped.hEvent == nullptr)
            {
                ThrowLastError("CreateEvent", GetLastError());
            }

            DWORD written = 0;
            BOOL ok = WriteFile(pipe_, bytes + offset, size - offset, &written, &overlapped);
            if (!ok)
            {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING)
                {
                    try
                    {
                        written = WaitForOverlapped(pipe_, overlapped, timeoutMs, "Discord IPC write");
                    }
                    catch (...)
                    {
                        CloseHandle(overlapped.hEvent);
                        throw;
                    }
                }
                else
                {
                    CloseHandle(overlapped.hEvent);
                    ThrowLastError("Discord IPC write", err);
                }
            }

            CloseHandle(overlapped.hEvent);
            if (written == 0)
            {
                throw std::runtime_error("Discord IPC write transferred zero bytes.");
            }
            offset += written;
        }
    }

    void ReadExact(void* data, DWORD size, DWORD timeoutMs)
    {
        BYTE* bytes = static_cast<BYTE*>(data);
        DWORD offset = 0;
        while (offset < size)
        {
            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (overlapped.hEvent == nullptr)
            {
                ThrowLastError("CreateEvent", GetLastError());
            }

            DWORD read = 0;
            BOOL ok = ReadFile(pipe_, bytes + offset, size - offset, &read, &overlapped);
            if (!ok)
            {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING)
                {
                    try
                    {
                        read = WaitForOverlapped(pipe_, overlapped, timeoutMs, "Discord IPC read");
                    }
                    catch (...)
                    {
                        CloseHandle(overlapped.hEvent);
                        throw;
                    }
                }
                else
                {
                    CloseHandle(overlapped.hEvent);
                    ThrowLastError("Discord IPC read", err);
                }
            }

            CloseHandle(overlapped.hEvent);
            if (read == 0)
            {
                throw std::runtime_error("Discord IPC pipe closed.");
            }
            offset += read;
        }
    }

    void SendRawFrame(uint32_t opcode, const std::string& payload)
    {
        if (!IsConnected())
        {
            throw std::runtime_error("Discord IPC pipe is not connected.");
        }

        uint32_t header[2] = { opcode, static_cast<uint32_t>(payload.size()) };
        WriteExact(header, sizeof(header), static_cast<DWORD>(options_.responseTimeoutMs));
        if (!payload.empty())
        {
            WriteExact(payload.data(), static_cast<DWORD>(payload.size()), static_cast<DWORD>(options_.responseTimeoutMs));
        }
    }

    void SendFrame(uint32_t opcode, const std::string& payload)
    {
        SendRawFrame(opcode, payload);
        if (verbose_ && opcode == 1)
        {
            LogDebug(L"IPC send: " + SummarizeRpcPayload(payload));
        }
    }

    std::string ReadFrame(int timeoutMs)
    {
        for (;;)
        {
            uint32_t header[2] = {};
            ReadExact(header, sizeof(header), static_cast<DWORD>(timeoutMs));
            uint32_t opcode = header[0];
            uint32_t length = header[1];
            if (length > 1024 * 1024)
            {
                throw std::runtime_error("Invalid Discord IPC frame length.");
            }

            std::string payload(length, '\0');
            if (length > 0)
            {
                ReadExact(payload.data(), length, static_cast<DWORD>(timeoutMs));
            }

            if (verbose_)
            {
                LogDebug(L"IPC recv: " + SummarizeRpcPayload(payload));
            }

            if (opcode == 3)
            {
                SendRawFrame(4, payload);
                continue;
            }
            if (opcode == 2)
            {
                throw std::runtime_error("Discord IPC closed: " + WideToUtf8(SummarizeJson(payload)));
            }
            if (opcode != 1)
            {
                throw std::runtime_error("Unexpected Discord IPC opcode.");
            }
            return payload;
        }
    }

    static std::wstring SummarizeRpcPayload(const std::string& json)
    {
        std::wstring cmd = ExtractJsonStringValue(json, "cmd");
        std::wstring evt = ExtractJsonStringValue(json, "evt");
        std::wstring nonce = ExtractJsonStringValue(json, "nonce");
        std::wstring result;
        if (!cmd.empty())
        {
            result += L"cmd=" + cmd + L" ";
        }
        if (!evt.empty())
        {
            result += L"evt=" + evt + L" ";
        }
        if (!nonce.empty())
        {
            result += L"nonce=" + nonce + L" ";
        }
        result += L"bytes=" + std::to_wstring(json.size());
        return Trim(result);
    }

    static std::wstring SummarizeJson(const std::string& json)
    {
        std::wstring message = ExtractJsonStringValue(json, "message");
        if (!message.empty())
        {
            return message;
        }
        return SummarizeRpcPayload(json);
    }

    static void ThrowIfError(const std::string& response)
    {
        if (response.find("\"evt\":\"ERROR\"") != std::string::npos ||
            (response.find("\"code\"") != std::string::npos &&
                response.find("\"message\"") != std::string::npos &&
                response.find("\"evt\"") != std::string::npos))
        {
            throw std::runtime_error("Discord IPC error: " + WideToUtf8(SummarizeJson(response)));
        }
    }
};

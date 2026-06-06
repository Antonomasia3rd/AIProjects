static bool JsonIntegerTokenRange(const std::string& json, size_t valueStart, size_t valueEnd, size_t& tokenStart, size_t& tokenEnd)
{
    tokenStart = valueStart;
    tokenEnd = valueEnd;
    while (tokenStart < tokenEnd && isspace(static_cast<unsigned char>(json[tokenStart])))
    {
        ++tokenStart;
    }
    while (tokenEnd > tokenStart && isspace(static_cast<unsigned char>(json[tokenEnd - 1])))
    {
        --tokenEnd;
    }
    if (tokenStart == tokenEnd)
    {
        return false;
    }

    size_t pos = tokenStart;
    if (json[pos] == '-')
    {
        ++pos;
        if (pos == tokenEnd)
        {
            return false;
        }
    }

    if (json[pos] == '0')
    {
        ++pos;
    }
    else if (json[pos] >= '1' && json[pos] <= '9')
    {
        do
        {
            ++pos;
        } while (pos < tokenEnd && isdigit(static_cast<unsigned char>(json[pos])));
    }
    else
    {
        return false;
    }

    return pos == tokenEnd;
}

static bool JsonTryParseIntegerToken(const std::string& json, size_t valueStart, size_t valueEnd, long long& value, size_t* tokenStartOut = nullptr, size_t* tokenEndOut = nullptr)
{
    size_t tokenStart = 0;
    size_t tokenEnd = 0;
    if (!JsonIntegerTokenRange(json, valueStart, valueEnd, tokenStart, tokenEnd))
    {
        return false;
    }

    try
    {
        size_t consumed = 0;
        long long parsed = std::stoll(json.substr(tokenStart, tokenEnd - tokenStart), &consumed, 10);
        if (consumed != tokenEnd - tokenStart)
        {
            return false;
        }
        value = parsed;
        if (tokenStartOut != nullptr)
        {
            *tokenStartOut = tokenStart;
        }
        if (tokenEndOut != nullptr)
        {
            *tokenEndOut = tokenEnd;
        }
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

static int JsonExtractInt(const std::string& json, const char* key, int fallback = -1)
{
    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!FindJsonFieldValue(json, key, keyPos, valueStart, valueEnd))
    {
        return fallback;
    }

    long long parsed = 0;
    if (!JsonTryParseIntegerToken(json, valueStart, valueEnd, parsed) || parsed < INT_MIN || parsed > INT_MAX)
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

static bool JsonExtractOptionalInt(const std::string& json, const char* key, int& value)
{
    int parsed = JsonExtractInt(json, key, INT_MIN);
    if (parsed == INT_MIN)
    {
        return false;
    }
    value = parsed;
    return true;
}

static void RemoveGatewayUnsupportedInstanceField(std::string& activity)
{
    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!FindJsonFieldValue(activity, "instance", keyPos, valueStart, valueEnd))
    {
        return;
    }

    size_t rawEnd = valueEnd;
    while (rawEnd > valueStart && isspace(static_cast<unsigned char>(activity[rawEnd - 1])))
    {
        --rawEnd;
    }
    if (activity.compare(valueStart, rawEnd - valueStart, "true") == 0)
    {
        RemoveJsonObjectMember(activity, keyPos, valueEnd);
    }
}

static void NormalizeGatewayTimestampUnits(std::string& activity)
{
    size_t keyPos = 0;
    size_t timestampsStart = 0;
    size_t timestampsEnd = 0;
    if (!FindJsonFieldValue(activity, "timestamps", keyPos, timestampsStart, timestampsEnd) ||
        timestampsStart >= activity.size() ||
        activity[timestampsStart] != '{')
    {
        return;
    }

    std::string timestamps = activity.substr(timestampsStart, timestampsEnd - timestampsStart);
    size_t startKeyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!FindJsonFieldValue(timestamps, "start", startKeyPos, valueStart, valueEnd))
    {
        return;
    }

    long long value = 0;
    size_t tokenStart = 0;
    size_t tokenEnd = 0;
    if (!JsonTryParseIntegerToken(timestamps, valueStart, valueEnd, value, &tokenStart, &tokenEnd) || value <= 0)
    {
        return;
    }

    if (value < 100000000000LL)
    {
        activity.replace(timestampsStart + tokenStart, tokenEnd - tokenStart, std::to_string(value * 1000LL));
    }
}

static bool ExtractJsonEscapedStringField(const std::string& objectJson, const char* key, std::string& value)
{
    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!FindJsonFieldValue(objectJson, key, keyPos, valueStart, valueEnd) ||
        valueStart >= objectJson.size() ||
        valueEnd <= valueStart + 1 ||
        objectJson[valueStart] != '"' ||
        objectJson[valueEnd - 1] != '"')
    {
        return false;
    }

    value = objectJson.substr(valueStart + 1, valueEnd - valueStart - 2);
    return true;
}

static void ConvertGatewayButtons(std::string& activity)
{
    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!FindJsonFieldValue(activity, "buttons", keyPos, valueStart, valueEnd) || valueStart >= activity.size() || activity[valueStart] != '[')
    {
        return;
    }

    std::string arrayText = activity.substr(valueStart, valueEnd - valueStart);
    std::vector<std::string> labels;
    std::vector<std::string> urls;
    size_t pos = 1;
    while (pos < arrayText.size() && labels.size() < 2)
    {
        while (pos < arrayText.size() && (isspace(static_cast<unsigned char>(arrayText[pos])) || arrayText[pos] == ','))
        {
            ++pos;
        }
        if (pos >= arrayText.size() || arrayText[pos] == ']')
        {
            break;
        }
        if (arrayText[pos] != '{')
        {
            ++pos;
            continue;
        }

        size_t objectEnd = JsonValueEnd(arrayText, pos);
        if (objectEnd == std::string::npos)
        {
            break;
        }

        std::string objectJson = arrayText.substr(pos, objectEnd - pos);
        std::string label;
        std::string url;
        if (ExtractJsonEscapedStringField(objectJson, "label", label) &&
            ExtractJsonEscapedStringField(objectJson, "url", url))
        {
            labels.push_back(label);
            urls.push_back(url);
        }
        pos = objectEnd;
    }

    if (labels.empty())
    {
        return;
    }

    auto buildStringArray = [](const std::vector<std::string>& values) {
        std::string json = "[";
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i != 0)
            {
                json.push_back(',');
            }
            json.push_back('"');
            json += values[i];
            json.push_back('"');
        }
        json.push_back(']');
        return json;
    };

    std::string labelArray = buildStringArray(labels);
    std::string urlArray = buildStringArray(urls);
    activity.replace(valueStart, valueEnd - valueStart, labelArray);
    activity.insert(valueStart + labelArray.size(), ",\"metadata\":{\"button_urls\":" + urlArray + "}");
}

struct WinHttpHandle
{
    HINTERNET handle = nullptr;

    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET value) : handle(value) {}
    ~WinHttpHandle()
    {
        Reset();
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET Get() const
    {
        return handle;
    }

    HINTERNET Release()
    {
        HINTERNET value = handle;
        handle = nullptr;
        return value;
    }

    void Reset(HINTERNET value = nullptr)
    {
        if (handle != nullptr)
        {
            WinHttpCloseHandle(handle);
        }
        handle = value;
    }
};

class DiscordGatewayClient
{
public:
    DiscordGatewayClient(
        std::wstring token,
        std::wstring clientId,
        bool verbose,
        GatewayOptions options,
        std::function<bool()> cancellationRequested)
        : token_(std::move(token)),
          clientId_(std::move(clientId)),
          verbose_(verbose),
          options_(std::move(options)),
          cancellationRequested_(std::move(cancellationRequested))
    {
    }

    ~DiscordGatewayClient()
    {
        Close();
    }

    bool IsConnected() const
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        return connected_.load() && websocket_ != nullptr;
    }

    void Connect(const std::string& initialActivityJson, const std::wstring& initialStatus)
    {
        Close();
        if (Trim(token_).empty())
        {
            throw std::runtime_error("Missing [general] token_protected, token, or token_env for Discord Gateway.");
        }
        if (Trim(clientId_).empty())
        {
            throw std::runtime_error("Missing [general] client_id for Discord Gateway.");
        }

        lastConnectError_.clear();
        heartbeatIntervalMs_ = 0;
        identified_ = false;
        heartbeatAcked_ = true;
        lastSequence_ = INT_MIN;

        session_ = WinHttpOpen(
            options_.browserUserAgent.c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (session_ == nullptr)
        {
            ThrowWinHttp("WinHttpOpen");
        }

        WinHttpSetTimeouts(
            session_,
            options_.connectTimeoutMs,
            options_.connectTimeoutMs,
            options_.sendTimeoutMs,
            options_.sendTimeoutMs);

        connect_ = WinHttpConnect(session_, L"gateway.discord.gg", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connect_ == nullptr)
        {
            ThrowWinHttp("WinHttpConnect");
        }

        request_ = WinHttpOpenRequest(
            connect_,
            L"GET",
            L"/?v=10&encoding=json",
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (request_ == nullptr)
        {
            ThrowWinHttp("WinHttpOpenRequest");
        }

        if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
        {
            ThrowWinHttp("WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET");
        }

        LogInfo(L"Connecting to Discord Gateway...");
        RunCancellableWinHttpRequest(
            request_,
            [this]() {
                if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                {
                    ThrowWinHttp("WinHttpSendRequest");
                }
                if (!WinHttpReceiveResponse(request_, nullptr))
                {
                    ThrowWinHttp("WinHttpReceiveResponse");
                }
            },
            [this]() { request_ = nullptr; },
            "Gateway connection cancelled.");

        websocket_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
        if (websocket_ == nullptr)
        {
            ThrowWinHttp("WinHttpWebSocketCompleteUpgrade");
        }
        DWORD closeTimeoutMs = static_cast<DWORD>(options_.closeTimeoutMs);
        if (!WinHttpSetOption(
                websocket_,
                WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT,
                &closeTimeoutMs,
                sizeof(closeTimeoutMs)))
        {
            DWORD error = GetLastError();
            WinHttpCloseHandle(websocket_);
            websocket_ = nullptr;
            SetLastError(error);
            ThrowWinHttp("WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT");
        }
        WinHttpCloseHandle(request_);
        request_ = nullptr;

        connected_ = true;
        reader_ = std::thread([this]() { ReadLoop(); });

        if (!WaitFor([this]() {
                return heartbeatIntervalMs_.load() > 0 || !connected_.load() || CancellationRequested();
            }, options_.helloTimeoutMs))
        {
            Close();
            throw std::runtime_error("Did not receive Gateway HELLO before timeout.");
        }
        if (CancellationRequested())
        {
            Close();
            throw std::runtime_error("Gateway connection cancelled.");
        }
        if (!connected_.load())
        {
            throw std::runtime_error("Gateway disconnected before HELLO.");
        }

        SendHeartbeat();
        heartbeat_ = std::thread([this]() { HeartbeatLoop(); });
        Identify(initialActivityJson, initialStatus);

        if (!WaitFor([this]() {
                return identified_.load() || !GetLastConnectError().empty() || !connected_.load() || CancellationRequested();
            }, options_.readyTimeoutMs))
        {
            Close();
            throw std::runtime_error("Did not receive Gateway READY before timeout.");
        }
        if (CancellationRequested())
        {
            Close();
            throw std::runtime_error("Gateway connection cancelled.");
        }

        std::wstring authError = GetLastConnectError();
        if (!authError.empty())
        {
            Close();
            throw std::runtime_error(WideToUtf8(L"Gateway authentication error: " + authError));
        }
        if (!identified_.load())
        {
            Close();
            throw std::runtime_error("Gateway disconnected before READY.");
        }

        LogInfo(L"Connected to Discord Gateway and identified.");
    }

    void SetPresence(const std::string& activityJson, const std::wstring& status)
    {
        if (!IsConnected())
        {
            throw std::runtime_error("Gateway is not connected.");
        }

        FetchAssetIds(clientId_);
        if (CancellationRequested())
        {
            throw std::runtime_error("Gateway operation cancelled.");
        }
        std::string data = BuildPresenceData(activityJson, status);
        if (verbose_)
        {
            LogDebug(L"Sending Gateway presence: " + SummarizeGatewayData(data));
        }
        SendPayload(3, data);
        LogInfo(L"Presence updated via Discord Gateway. Status=" + NormalizeStatus(status));
    }

    void ClearPresence(const std::wstring& status)
    {
        if (!IsConnected())
        {
            return;
        }
        try
        {
            std::string data = "{";
            bool first = true;
            std::wstring normalized = NormalizeStatus(status);
            if (normalized == L"idle")
            {
                JsonFieldNumber(data, first, "since", static_cast<long long>(std::time(nullptr)) * 1000LL);
            }
            else
            {
                JsonFieldRaw(data, first, "since", "null");
            }
            JsonFieldRaw(data, first, "activities", "[]");
            JsonFieldString(data, first, "status", normalized);
            JsonFieldBool(data, first, "afk", normalized == L"idle");
            data.push_back('}');
            SendPayload(3, data);
        }
        catch (const std::exception&)
        {
        }
    }

    void Close()
    {
        connected_ = false;
        identified_ = false;
        heartbeatIntervalMs_ = 0;
        heartbeatAcked_ = true;

        // Do not close the WebSocket handle before the worker threads exit.
        // The reader thread may be blocked inside WinHttpWebSocketReceive and
        // SendText may be using the same handle under sendMutex_. Close the
        // WebSocket protocol first to wake the reader, join the threads, then
        // release the handle lifetime.
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            if (websocket_ != nullptr)
            {
                WinHttpWebSocketClose(websocket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            }
        }

        if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id())
        {
            reader_.join();
        }
        if (heartbeat_.joinable() && heartbeat_.get_id() != std::this_thread::get_id())
        {
            heartbeat_.join();
        }

        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            if (websocket_ != nullptr)
            {
                WinHttpCloseHandle(websocket_);
                websocket_ = nullptr;
            }
        }

        if (request_ != nullptr)
        {
            WinHttpCloseHandle(request_);
            request_ = nullptr;
        }
        if (connect_ != nullptr)
        {
            WinHttpCloseHandle(connect_);
            connect_ = nullptr;
        }
        if (session_ != nullptr)
        {
            WinHttpCloseHandle(session_);
            session_ = nullptr;
        }
    }

private:
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;
    HINTERNET request_ = nullptr;
    HINTERNET websocket_ = nullptr;
    std::wstring token_;
    std::wstring clientId_;
    bool verbose_ = false;
    GatewayOptions options_;
    std::function<bool()> cancellationRequested_;
    mutable std::mutex sendMutex_;
    std::mutex errorMutex_;
    std::thread reader_;
    std::thread heartbeat_;
    std::atomic<bool> connected_{ false };
    std::atomic<bool> identified_{ false };
    std::atomic<bool> heartbeatAcked_{ true };
    std::atomic<int> heartbeatIntervalMs_{ 0 };
    std::atomic<int> lastSequence_{ INT_MIN };
    std::wstring lastConnectError_;
    std::map<std::wstring, std::wstring, std::less<>> assetIdCache_;
    std::wstring cachedAssetApplicationId_;

    template <typename Predicate>
    static bool WaitFor(Predicate predicate, int timeoutMs)
    {
        ULONGLONG start = GetTickCount64();
        while (!predicate())
        {
            if (GetTickCount64() - start >= static_cast<ULONGLONG>(timeoutMs))
            {
                return predicate();
            }
            Sleep(50);
        }
        return true;
    }

    bool CancellationRequested() const
    {
        return cancellationRequested_ && cancellationRequested_();
    }

    template <typename Operation, typename ClearOwner>
    void RunCancellableWinHttpRequest(
        HINTERNET request,
        Operation operation,
        ClearOwner clearOwner,
        const char* cancellationMessage)
    {
        HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (completed == nullptr)
        {
            throw std::runtime_error(WideToUtf8(L"CreateEvent failed: " + GetLastErrorText(GetLastError())));
        }

        std::atomic<bool> cancelled{ false };
        std::atomic<bool> requestClosed{ false };
        std::thread cancellationThread;
        try
        {
            cancellationThread = std::thread([&]() {
                while (WaitForSingleObject(completed, 50) == WAIT_TIMEOUT)
                {
                    if (!CancellationRequested())
                    {
                        continue;
                    }
                    cancelled = true;
                    if (!requestClosed.exchange(true))
                    {
                        WinHttpCloseHandle(request);
                    }
                    return;
                }
            });
        }
        catch (...)
        {
            CloseHandle(completed);
            throw;
        }

        std::exception_ptr failure;
        try
        {
            operation();
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        SetEvent(completed);
        cancellationThread.join();
        CloseHandle(completed);

        if (requestClosed.load())
        {
            clearOwner();
        }
        if (cancelled.load() || CancellationRequested())
        {
            if (!requestClosed.exchange(true))
            {
                WinHttpCloseHandle(request);
                clearOwner();
            }
            throw std::runtime_error(cancellationMessage);
        }
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }

    static void ThrowWinHttp(const char* action)
    {
        DWORD error = GetLastError();
        throw std::runtime_error(WideToUtf8(Utf8ToWide(action) + L" failed: " + GetLastErrorText(error)));
    }

    void SetLastConnectError(const std::wstring& value)
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastConnectError_ = value;
    }

    std::wstring GetLastConnectError()
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastConnectError_;
    }

    void Identify(const std::string& initialActivityJson, const std::wstring& initialStatus)
    {
        FetchAssetIds(clientId_);

        std::string properties = "{";
        bool propFirst = true;
        JsonFieldString(properties, propFirst, "os", L"Windows");
        JsonFieldString(properties, propFirst, "browser", L"Discord Client");
        JsonFieldString(properties, propFirst, "device", L"");
        JsonFieldString(properties, propFirst, "system_locale", options_.systemLocale);
        JsonFieldString(properties, propFirst, "browser_user_agent", options_.browserUserAgent);
        JsonFieldString(properties, propFirst, "browser_version", options_.browserVersion);
        JsonFieldString(properties, propFirst, "os_version", options_.osVersion);
        JsonFieldString(properties, propFirst, "release_channel", options_.releaseChannel);
        JsonFieldNumber(properties, propFirst, "client_build_number", options_.clientBuildNumber);
        JsonFieldRaw(properties, propFirst, "client_event_source", "null");
        properties.push_back('}');

        std::string clientState =
            "{\"guild_versions\":{},"
            "\"highest_last_message_id\":\"0\","
            "\"read_state_version\":0,"
            "\"user_guild_settings_version\":-1,"
            "\"user_settings_version\":-1,"
            "\"private_channels_version\":\"0\","
            "\"api_code_version\":0}";

        std::string data = "{";
        bool first = true;
        JsonFieldString(data, first, "token", token_);
        JsonFieldNumber(data, first, "capabilities", options_.capabilities);
        JsonFieldRaw(data, first, "properties", properties);
        JsonFieldBool(data, first, "compress", false);
        JsonFieldRaw(data, first, "client_state", clientState);
        JsonFieldRaw(data, first, "presence", BuildPresenceData(initialActivityJson, initialStatus));
        data.push_back('}');

        SendPayload(2, data);
        LogInfo(L"Gateway IDENTIFY sent.");
    }

    std::string BuildPresenceData(const std::string& activityJson, const std::wstring& status)
    {
        std::wstring normalized = NormalizeStatus(status);
        std::string gatewayActivity = BuildGatewayActivity(activityJson);

        std::string data = "{";
        bool first = true;
        if (normalized == L"idle")
        {
            JsonFieldNumber(data, first, "since", static_cast<long long>(std::time(nullptr)) * 1000LL);
        }
        else
        {
            JsonFieldRaw(data, first, "since", "null");
        }
        JsonFieldRaw(data, first, "activities", "[" + gatewayActivity + "]");
        JsonFieldString(data, first, "status", normalized);
        JsonFieldBool(data, first, "afk", normalized == L"idle");
        data.push_back('}');
        return data;
    }

    std::string BuildGatewayActivity(const std::string& ipcActivityJson)
    {
        std::string activity = ipcActivityJson;
        RemoveGatewayUnsupportedInstanceField(activity);
        NormalizeGatewayTimestampUnits(activity);
        ConvertGatewayButtons(activity);
        ResolveGatewayAssets(activity);

        size_t keyPos = 0;
        size_t valueStart = 0;
        size_t valueEnd = 0;
        if (!FindJsonFieldValue(activity, "application_id", keyPos, valueStart, valueEnd))
        {
            activity.insert(1, "\"application_id\":" + JsonEscape(clientId_) + ",");
        }
        return activity;
    }

    void ResolveGatewayAssets(std::string& activity)
    {
        size_t keyPos = 0;
        size_t valueStart = 0;
        size_t valueEnd = 0;
        if (!FindJsonFieldValue(activity, "assets", keyPos, valueStart, valueEnd) || valueStart >= activity.size() || activity[valueStart] != '{')
        {
            return;
        }

        std::string assets = activity.substr(valueStart, valueEnd - valueStart);
        ResolveAssetField(assets, "large_image");
        ResolveAssetField(assets, "small_image");
        activity.replace(valueStart, valueEnd - valueStart, assets);
    }

    void ResolveAssetField(std::string& assets, const char* key)
    {
        size_t keyPos = 0;
        size_t valueStart = 0;
        size_t valueEnd = 0;
        if (!FindJsonFieldValue(assets, key, keyPos, valueStart, valueEnd) || valueStart >= assets.size() || assets[valueStart] != '"')
        {
            return;
        }

        std::string raw = assets.substr(valueStart + 1, valueEnd - valueStart - 2);
        if (raw.find('\\') != std::string::npos ||
            StartsWithI(Utf8ToWide(raw), L"http://") ||
            StartsWithI(Utf8ToWide(raw), L"https://") ||
            StartsWithI(Utf8ToWide(raw), L"mp:"))
        {
            return;
        }

        auto found = assetIdCache_.find(Utf8ToWide(raw));
        if (found == assetIdCache_.end())
        {
            return;
        }

        std::string replacement = JsonEscape(found->second);
        assets.replace(valueStart, valueEnd - valueStart, replacement);
    }

    void SendPayload(int op, const std::string& dataJson)
    {
        std::string payload = "{";
        bool first = true;
        JsonFieldNumber(payload, first, "op", op);
        JsonFieldRaw(payload, first, "d", dataJson);
        payload.push_back('}');
        SendText(payload);

        if (verbose_)
        {
            LogDebug(L"Gateway send: op=" + std::to_wstring(op) + L" bytes=" + std::to_wstring(payload.size()));
        }
    }

    void SendText(const std::string& json)
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (!connected_.load() || websocket_ == nullptr)
        {
            throw std::runtime_error("Gateway WebSocket is not connected.");
        }

        DWORD result = WinHttpWebSocketSend(
            websocket_,
            WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            const_cast<char*>(json.data()),
            static_cast<DWORD>(json.size()));
        if (result != ERROR_SUCCESS)
        {
            throw std::runtime_error(WideToUtf8(L"Gateway send failed: " + GetLastErrorText(result)));
        }
    }

    void SendHeartbeat()
    {
        std::string payload = "{";
        bool first = true;
        JsonFieldNumber(payload, first, "op", 1);
        int seq = lastSequence_.load();
        if (seq == INT_MIN)
        {
            JsonFieldRaw(payload, first, "d", "null");
        }
        else
        {
            JsonFieldNumber(payload, first, "d", seq);
        }
        payload.push_back('}');
        SendText(payload);
        if (verbose_)
        {
            LogDebug(L"Gateway heartbeat sent.");
        }
    }

    void HeartbeatLoop()
    {
        while (connected_.load())
        {
            int interval = heartbeatIntervalMs_.load();
            int remaining = interval > 0 ? interval : 41250;
            while (remaining > 0 && connected_.load())
            {
                int slice = std::min(remaining, 250);
                Sleep(static_cast<DWORD>(slice));
                remaining -= slice;
            }
            if (!connected_.load())
            {
                break;
            }
            if (!heartbeatAcked_.load())
            {
                LogError(L"Gateway heartbeat ACK not received; reconnecting.");
                connected_ = false;
                break;
            }
            heartbeatAcked_ = false;
            try
            {
                SendHeartbeat();
            }
            catch (const std::exception& ex)
            {
                if (connected_.load())
                {
                    LogError(L"Gateway heartbeat failed: " + Utf8ToWide(ex.what()));
                }
                connected_ = false;
                break;
            }
        }
    }

    void ReadLoop()
    {
        try
        {
            std::vector<char> buffer(65536);
            while (connected_.load())
            {
                std::string message;
                for (;;)
                {
                    DWORD bytesRead = 0;
                    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
                    DWORD result = WinHttpWebSocketReceive(
                        websocket_,
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &bytesRead,
                        &type);
                    if (result != ERROR_SUCCESS)
                    {
                        if (connected_.load())
                        {
                            LogError(L"Gateway receive failed: " + GetLastErrorText(result));
                        }
                        connected_ = false;
                        return;
                    }

                    if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                    {
                        USHORT closeStatus = 0;
                        char reason[256] = {};
                        DWORD reasonLength = 0;
                        DWORD closeResult = WinHttpWebSocketQueryCloseStatus(
                            websocket_,
                            &closeStatus,
                            reason,
                            static_cast<DWORD>(sizeof(reason) - 1),
                            &reasonLength);
                        std::wstring closeText = L"Gateway websocket closed.";
                        if (closeResult == ERROR_SUCCESS)
                        {
                            reason[std::min<DWORD>(reasonLength, static_cast<DWORD>(sizeof(reason) - 1))] = '\0';
                            closeText += L" Status=" + std::to_wstring(closeStatus);
                            if (reason[0] != '\0')
                            {
                                closeText += L" Reason=" + Utf8ToWide(reason);
                            }
                            if (closeStatus == 4004)
                            {
                                SetLastConnectError(L"4004 Authentication failed. Check [general] token_protected, token, or token_env.");
                            }
                        }
                        LogError(closeText);
                        connected_ = false;
                        return;
                    }

                    if (bytesRead > 0)
                    {
                        message.append(buffer.data(), buffer.data() + bytesRead);
                    }

                    if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                        type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
                    {
                        break;
                    }
                }

                if (verbose_)
                {
                    LogDebug(L"Gateway recv: " + SummarizeGatewayPayload(message));
                }
                HandleMessage(message);
            }
        }
        catch (const std::exception& ex)
        {
            if (connected_.load())
            {
                LogError(L"Gateway read error: " + Utf8ToWide(ex.what()));
            }
        }

        connected_ = false;
        LogInfo(L"Gateway reader exited.");
    }

    void HandleMessage(const std::string& json)
    {
        int op = JsonExtractInt(json, "op");
        int seq = 0;
        if (JsonExtractOptionalInt(json, "s", seq))
        {
            lastSequence_ = seq;
        }

        if (op == 10)
        {
            int interval = JsonExtractInt(json, "heartbeat_interval");
            if (interval > 0)
            {
                heartbeatIntervalMs_ = interval;
                LogInfo(L"Gateway HELLO received. Heartbeat interval: " + std::to_wstring(interval) + L"ms.");
            }
            return;
        }

        if (op == 11)
        {
            heartbeatAcked_ = true;
            if (verbose_)
            {
                LogDebug(L"Gateway heartbeat ACK received.");
            }
            return;
        }

        if (op == 1)
        {
            try
            {
                SendHeartbeat();
            }
            catch (const std::exception&)
            {
            }
            return;
        }

        if (op == 9)
        {
            SetLastConnectError(L"Invalid Gateway session.");
            connected_ = false;
            return;
        }

        if (op == 0)
        {
            std::wstring eventName = ExtractJsonStringValue(json, "t");
            if (eventName == L"READY")
            {
                identified_ = true;
                LogInfo(L"Gateway READY received. Presence is now live.");
            }
            else if (eventName == L"RESUMED")
            {
                identified_ = true;
                LogInfo(L"Gateway RESUMED.");
            }
            else if (verbose_ && eventName == L"SESSIONS_REPLACE")
            {
                LogDebug(L"Gateway SESSIONS_REPLACE received.");
            }
            else if (verbose_ && !eventName.empty())
            {
                LogDebug(L"Gateway dispatch: " + eventName);
            }
        }
    }

    static std::wstring SummarizeGatewayPayload(const std::string& json)
    {
        int op = JsonExtractInt(json, "op");
        std::wstring eventName = ExtractJsonStringValue(json, "t");
        std::wstring summary = L"op=" + std::to_wstring(op);
        if (!eventName.empty())
        {
            summary += L" t=" + eventName;
        }
        summary += L" bytes=" + std::to_wstring(json.size());
        return summary;
    }

    static std::wstring SummarizeGatewayData(const std::string& json)
    {
        return L"bytes=" + std::to_wstring(json.size()) + L" status=" + ExtractJsonStringValue(json, "status");
    }

    void FetchAssetIds(const std::wstring& applicationId)
    {
        if (Trim(applicationId).empty() || cachedAssetApplicationId_ == applicationId)
        {
            return;
        }

        try
        {
            std::wstring path = L"/api/v10/oauth2/applications/" + applicationId + L"/assets";
            std::string response = HttpGetDiscord(path, options_.assetFetchUserAgent, options_.assetFetchTimeoutMs);
            assetIdCache_.clear();

            size_t search = 0;
            for (;;)
            {
                size_t objStart = FindJsonObjectStart(response, search);
                if (objStart == std::string::npos)
                {
                    break;
                }
                size_t objEnd = JsonValueEnd(response, objStart);
                if (objEnd == std::string::npos)
                {
                    break;
                }

                std::string entry = response.substr(objStart, objEnd - objStart);
                std::wstring assetId = ExtractJsonStringValue(entry, "id");
                std::wstring assetName = ExtractJsonStringValue(entry, "name");
                if (!assetId.empty() && !assetName.empty())
                {
                    assetIdCache_[assetName] = assetId;
                }
                search = objEnd;
            }

            cachedAssetApplicationId_ = applicationId;
            LogInfo(L"Gateway asset IDs cached: " + std::to_wstring(assetIdCache_.size()) + L" asset(s) for application " + applicationId + L".");
        }
        catch (const std::exception& ex)
        {
            if (CancellationRequested())
            {
                throw;
            }
            LogError(L"Could not fetch Gateway asset IDs for application " + applicationId + L": " + Utf8ToWide(ex.what()));
        }
    }

    std::string HttpGetDiscord(const std::wstring& path, const std::wstring& userAgent, int timeoutMs)
    {
        HINTERNET session = WinHttpOpen(
            userAgent.empty() ? L"DiscordRPC" : userAgent.c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (session == nullptr)
        {
            ThrowWinHttp("WinHttpOpen");
        }
        WinHttpHandle closeSession(session);

        WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
        HINTERNET connect = WinHttpConnect(session, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connect == nullptr)
        {
            ThrowWinHttp("WinHttpConnect");
        }
        WinHttpHandle closeConnect(connect);

        HINTERNET request = WinHttpOpenRequest(
            connect,
            L"GET",
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (request == nullptr)
        {
            ThrowWinHttp("WinHttpOpenRequest");
        }
        WinHttpHandle closeRequest(request);

        std::string response;
        RunCancellableWinHttpRequest(
            request,
            [&]() {
                if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                {
                    ThrowWinHttp("WinHttpSendRequest");
                }
                if (!WinHttpReceiveResponse(request, nullptr))
                {
                    ThrowWinHttp("WinHttpReceiveResponse");
                }

                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
                if (status < 200 || status >= 300)
                {
                    throw std::runtime_error("Discord asset API returned HTTP " + std::to_string(status) + ".");
                }

                for (;;)
                {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available))
                    {
                        ThrowWinHttp("WinHttpQueryDataAvailable");
                    }
                    if (available == 0)
                    {
                        break;
                    }

                    std::string chunk(available, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(request, chunk.data(), available, &read))
                    {
                        ThrowWinHttp("WinHttpReadData");
                    }
                    chunk.resize(read);
                    response += chunk;
                }
            },
            [&]() { closeRequest.Release(); },
            "Discord asset request cancelled.");
        return response;
    }
};

static std::wstring Trim(const std::wstring& value)
{
    return aip::Trim(value);
}

static std::wstring ToLower(std::wstring value)
{
    return aip::ToLower(std::move(value));
}

static bool EqualsI(const std::wstring& left, const wchar_t* right)
{
    return aip::EqualsI(left, right);
}

static bool StartsWithI(const std::wstring& value, const wchar_t* prefix)
{
    return aip::StartsWithI(value, prefix);
}

static std::wstring GetLastErrorText(DWORD error)
{
    return aip::GetLastErrorText(error);
}

static std::wstring GetFileBaseName(const std::wstring& path)
{
    return aip::GetFileBaseName(path);
}

static std::string WideToUtf8(const std::wstring& value)
{
    return aip::WideToUtf8(value);
}

static std::wstring Utf8ToWide(const std::string& value)
{
    return aip::Utf8ToWide(value);
}

static std::wstring IniReadRaw(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
#ifdef AIP_DISCORD_ENGINE_CONTEXT
    if (g_embeddedOptions && g_embeddedOptions->profileOverride)
    {
        const auto& values = g_embeddedOptions->profileOverride->values;
        const auto found = values.find({ToLower(section), ToLower(key)});
        return found == values.end() ? std::wstring(fallback) : found->second;
    }
#endif
    return aip::IniConfigStore(g_iniPath).ReadRaw(section, key, fallback);
}

static std::wstring IniReadS(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    return aip::DecodeBackslashEscapes(IniReadRaw(section, key, fallback));
}

static int IniReadI(const wchar_t* section, const wchar_t* key, int fallback)
{
    std::wstring raw = Trim(IniReadRaw(section, key, L""));
    if (raw.empty())
    {
        return fallback;
    }

    int parsed = 0;
    return aip::ParseIntValue(raw, parsed) ? parsed : fallback;
}

static bool IniReadB(const wchar_t* section, const wchar_t* key, bool fallback)
{
    std::wstring raw = ToLower(Trim(IniReadRaw(section, key, L"")));
    if (raw.empty())
    {
        return fallback;
    }
    bool parsed = false;
    return aip::ParseBoolValue(raw, parsed) ? parsed : fallback;
}

// Embedded diagnostics are reported through service snapshots. Legacy grammar
// and transport hooks remain inert; this library has no app logger or file sink.
static void LogInfo(const std::wstring&) {}
static void LogError(const std::wstring&) {}
static void LogDebug(const std::wstring&) {}
using aip::ExtractJsonStringValue;
using aip::TryExtractJsonStringValue;
using aip::FindJsonFieldValue;
using aip::DecodeJsonStringRange;
using aip::JsonEscape;
using aip::JsonFieldBool;
using aip::JsonFieldNumber;
using aip::JsonFieldRaw;
using aip::JsonFieldString;
using aip::JsonStringEnd;
using aip::JsonValueEnd;

static void RemoveJsonObjectMember(std::string& objectJson, size_t keyPos, size_t valueEnd)
{
    if (keyPos >= objectJson.size() || valueEnd > objectJson.size() || valueEnd < keyPos)
    {
        return;
    }

    size_t removeStart = keyPos;
    while (removeStart > 0 && isspace(static_cast<unsigned char>(objectJson[removeStart - 1])))
    {
        --removeStart;
    }

    size_t removeEnd = valueEnd;
    while (removeEnd < objectJson.size() && isspace(static_cast<unsigned char>(objectJson[removeEnd])))
    {
        ++removeEnd;
    }

    if (removeEnd < objectJson.size() && objectJson[removeEnd] == ',')
    {
        ++removeEnd;
        while (removeEnd < objectJson.size() && isspace(static_cast<unsigned char>(objectJson[removeEnd])))
        {
            ++removeEnd;
        }
    }
    else if (removeStart > 0 && objectJson[removeStart - 1] == ',')
    {
        --removeStart;
        while (removeStart > 0 && isspace(static_cast<unsigned char>(objectJson[removeStart - 1])))
        {
            --removeStart;
        }
    }

    objectJson.erase(removeStart, removeEnd - removeStart);
}

static std::wstring NormalizeTransportMode(const std::wstring& raw)
{
    std::wstring mode = ToLower(Trim(raw));
    if (mode == L"ipc" || mode == L"auto" || mode == L"gateway")
    {
        return mode;
    }
    return L"ipc";
}

static std::wstring NormalizeStatus(const std::wstring& raw)
{
    std::wstring status = ToLower(Trim(raw));
    if (status == L"online" || status == L"idle" || status == L"dnd" || status == L"invisible")
    {
        return status;
    }
    return L"online";
}

static std::wstring ResolveDiscordToken()
{
    std::wstring token, error;
    const bool resolved = aip::discord::ResolveConfiguredToken(
        IniReadS(L"general", L"token", L""), IniReadS(L"general", L"token_protected", L""),
        [](const std::wstring& value) { return aip::UnprotectSecretForCurrentUser(value, aip::DpapiLegacyEncoding::Utf8); }, token, error);
    if (!resolved) LogError(error);
    return token;
}

static int ClampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}

static IpcOptions LoadIpcOptions()
{
    IpcOptions options;
    options.connectTimeoutMs = ClampInt(IniReadI(L"ipc", L"connect_timeout_ms", 250), 50, 30000);
    options.responseTimeoutMs = ClampInt(IniReadI(L"ipc", L"response_timeout_ms", 5000), 250, 120000);
    return options;
}

static std::wstring NonEmptyConfig(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    std::wstring value = Trim(IniReadS(section, key, fallback));
    return value.empty() ? std::wstring(fallback) : value;
}

static GatewayOptions LoadGatewayOptions()
{
    GatewayOptions options;
    options.systemLocale = NonEmptyConfig(L"gateway", L"system_locale", L"en-US");
    options.browserUserAgent = NonEmptyConfig(
        L"gateway",
        L"browser_user_agent",
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9181 Chrome/128.0.6613.186 Electron/32.2.7 Safari/537.36");
    options.browserVersion = NonEmptyConfig(L"gateway", L"browser_version", L"32.2.7");
    options.osVersion = NonEmptyConfig(L"gateway", L"os_version", L"10.0.26100");
    options.releaseChannel = NonEmptyConfig(L"gateway", L"release_channel", L"stable");
    options.clientBuildNumber = ClampInt(IniReadI(L"gateway", L"client_build_number", 390000), 1, INT_MAX);
    options.capabilities = ClampInt(IniReadI(L"gateway", L"capabilities", 65), 0, INT_MAX);
    options.connectTimeoutMs = ClampInt(IniReadI(L"gateway", L"connect_timeout_ms", 10000), 1000, 120000);
    options.helloTimeoutMs = ClampInt(IniReadI(L"gateway", L"hello_timeout_ms", 10000), 1000, 120000);
    options.readyTimeoutMs = ClampInt(IniReadI(L"gateway", L"ready_timeout_ms", 30000), 1000, 180000);
    options.sendTimeoutMs = ClampInt(IniReadI(L"gateway", L"send_timeout_ms", 10000), 1000, 120000);
    options.closeTimeoutMs = ClampInt(IniReadI(L"gateway", L"close_timeout_ms", 2000), 0, 30000);
    options.assetFetchTimeoutMs = ClampInt(IniReadI(L"gateway", L"asset_fetch_timeout_ms", 10000), 1000, 120000);
    options.assetFetchUserAgent = NonEmptyConfig(L"gateway", L"asset_fetch_user_agent", L"DiscordBot (https://github.com, 1)");
    return options;
}

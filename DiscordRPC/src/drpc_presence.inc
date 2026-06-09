static ULONGLONG g_lastIdleTime = 0;
static ULONGLONG g_lastKernelTime = 0;
static ULONGLONG g_lastUserTime = 0;
static bool g_haveCpuSample = false;

static ULONGLONG FileTimeToUInt64(const FILETIME& ft)
{
    ULARGE_INTEGER value = {};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

static std::wstring FormatTwoDigits(WORD value)
{
    wchar_t buffer[8] = {};
    swprintf_s(buffer, L"%02u", value);
    return buffer;
}

static std::wstring FormatDouble(double value, int digits = 1)
{
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(digits) << value;
    std::wstring text = stream.str();
    if (digits > 0)
    {
        while (!text.empty() && text.back() == L'0')
        {
            text.pop_back();
        }
        if (!text.empty() && text.back() == L'.')
        {
            text.pop_back();
        }
    }
    return text;
}

static int GetInputIdleSeconds()
{
    LASTINPUTINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info))
    {
        return 0;
    }

    DWORD now = GetTickCount();
    return static_cast<int>((now - info.dwTime) / 1000);
}

static SystemStats GetSystemStats()
{
    SystemStats stats;

    FILETIME idle = {};
    FILETIME kernel = {};
    FILETIME user = {};
    if (GetSystemTimes(&idle, &kernel, &user))
    {
        ULONGLONG idleValue = FileTimeToUInt64(idle);
        ULONGLONG kernelValue = FileTimeToUInt64(kernel);
        ULONGLONG userValue = FileTimeToUInt64(user);
        if (g_haveCpuSample)
        {
            ULONGLONG idleDelta = idleValue - g_lastIdleTime;
            ULONGLONG kernelDelta = kernelValue - g_lastKernelTime;
            ULONGLONG userDelta = userValue - g_lastUserTime;
            ULONGLONG total = kernelDelta + userDelta;
            if (total > 0 && idleDelta <= total)
            {
                stats.cpuPercent = (static_cast<double>(total - idleDelta) * 100.0) / static_cast<double>(total);
            }
        }
        g_lastIdleTime = idleValue;
        g_lastKernelTime = kernelValue;
        g_lastUserTime = userValue;
        g_haveCpuSample = true;
    }

    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem))
    {
        constexpr double gb = 1024.0 * 1024.0 * 1024.0;
        stats.totalRamGb = static_cast<double>(mem.ullTotalPhys) / gb;
        stats.availRamGb = static_cast<double>(mem.ullAvailPhys) / gb;
        stats.usedRamGb = std::max(0.0, stats.totalRamGb - stats.availRamGb);
        stats.ramPercent = mem.dwMemoryLoad;
    }

    return stats;
}

static BatteryInfo GetBatteryInfo()
{
    BatteryInfo info;
    SYSTEM_POWER_STATUS status = {};
    if (!GetSystemPowerStatus(&status))
    {
        return info;
    }

    if (status.BatteryFlag == 128 || status.BatteryLifePercent == 255)
    {
        return info;
    }

    info.hasBattery = true;
    info.percent = status.BatteryLifePercent;
    info.plugged = status.ACLineStatus == 1;
    if (status.BatteryLifeTime != static_cast<DWORD>(-1))
    {
        int minutes = static_cast<int>(status.BatteryLifeTime / 60);
        info.remainingText = std::to_wstring(minutes / 60) + L"h " + std::to_wstring(minutes % 60) + L"m";
    }
    return info;
}

static std::wstring QueryProcessPath(DWORD pid)
{
    constexpr DWORD PROCESS_QUERY_LIMITED_INFORMATION_VALUE = 0x1000;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION_VALUE, FALSE, pid);
    if (process == nullptr)
    {
        return L"";
    }

    std::vector<wchar_t> buffer(4096);
    DWORD size = static_cast<DWORD>(buffer.size());
    std::wstring path;
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size))
    {
        path.assign(buffer.data(), size);
    }
    CloseHandle(process);
    return path;
}

static ForegroundWindowInfo GetForegroundWindowInfo(const std::wstring& fallback)
{
    ForegroundWindowInfo info;
    info.title = fallback;

    HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr)
    {
        return info;
    }

    wchar_t title[512] = {};
    if (GetWindowTextW(hwnd, title, ARRAYSIZE(title)) > 0)
    {
        info.title = title;
    }

    wchar_t className[256] = {};
    if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) > 0)
    {
        info.className = className;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    info.pid = pid;
    if (pid != 0)
    {
        info.exePath = QueryProcessPath(pid);
        info.exeName = GetFileBaseName(info.exePath);
    }

    if (Trim(info.title).empty())
    {
        info.title = fallback;
    }
    return info;
}

enum class CensorRuleKind
{
    FullReplace,
    WordReplace,
    PatternReplace
};

struct CompiledCensorRegexRule
{
    std::wstring patternText;
    std::wstring replacement;
    std::wregex pattern;
};

using ReplacementRule = std::pair<std::wstring, std::wstring>;

static std::mutex g_censorRulesMutex;
static std::wstring g_cachedFullReplaceRaw;
static std::shared_ptr<const std::vector<ReplacementRule>> g_cachedFullReplaceRules;
static std::wstring g_cachedWordReplaceRaw;
static std::shared_ptr<const std::vector<ReplacementRule>> g_cachedWordReplaceRules;
static std::wstring g_cachedPatternReplaceRaw;
static std::shared_ptr<const std::vector<CompiledCensorRegexRule>> g_cachedPatternReplaceRules;
static std::wstring g_cachedPatternRuntimeFailureRaw;
static std::vector<std::wstring> g_cachedPatternRuntimeFailurePatterns;
static std::wstring g_cachedRuleOrderRaw;
static std::shared_ptr<const std::vector<CensorRuleKind>> g_cachedRuleOrder;
static std::wstring g_cachedApplyPatternOnRawRaw;
static bool g_cachedApplyPatternOnRaw = false;

static std::vector<ReplacementRule> ParseReplacementRules(const std::wstring& raw)
{
    std::wstring normalized = raw;
    std::replace(normalized.begin(), normalized.end(), L'\r', L'\n');
    if (normalized.find(L'\n') == std::wstring::npos)
    {
        std::replace(normalized.begin(), normalized.end(), L';', L'\n');
    }

    std::vector<ReplacementRule> rules;
    std::wistringstream stream(normalized);
    std::wstring line;
    while (std::getline(stream, line))
    {
        line = Trim(line);
        if (line.empty())
        {
            continue;
        }

        size_t sep = line.find(L"=>");
        size_t sepLen = 2;
        if (sep == std::wstring::npos)
        {
            sep = line.find(L'=');
            sepLen = 1;
        }
        if (sep == std::wstring::npos)
        {
            continue;
        }

        std::wstring from = Trim(line.substr(0, sep));
        std::wstring to = Trim(line.substr(sep + sepLen));
        if (!from.empty())
        {
            rules.emplace_back(from, to);
        }
    }
    return rules;
}

static std::shared_ptr<const std::vector<ReplacementRule>> GetCachedFullCensorRules(const std::wstring& rawRules)
{
    std::lock_guard<std::mutex> lock(g_censorRulesMutex);
    if (rawRules != g_cachedFullReplaceRaw || !g_cachedFullReplaceRules)
    {
        g_cachedFullReplaceRaw = rawRules;
        g_cachedFullReplaceRules = std::make_shared<const std::vector<ReplacementRule>>(ParseReplacementRules(rawRules));
    }
    return g_cachedFullReplaceRules;
}

static std::shared_ptr<const std::vector<ReplacementRule>> GetCachedWordCensorRules(const std::wstring& rawRules)
{
    std::lock_guard<std::mutex> lock(g_censorRulesMutex);
    if (rawRules != g_cachedWordReplaceRaw || !g_cachedWordReplaceRules)
    {
        g_cachedWordReplaceRaw = rawRules;
        g_cachedWordReplaceRules = std::make_shared<const std::vector<ReplacementRule>>(ParseReplacementRules(rawRules));
    }
    return g_cachedWordReplaceRules;
}

static std::shared_ptr<const std::vector<CompiledCensorRegexRule>> GetCompiledPatternCensorRules(const std::wstring& rawRules)
{
    std::lock_guard<std::mutex> lock(g_censorRulesMutex);
    if (rawRules != g_cachedPatternReplaceRaw || !g_cachedPatternReplaceRules)
    {
        auto compiledRules = std::make_shared<std::vector<CompiledCensorRegexRule>>();
        for (const auto& rule : ParseReplacementRules(rawRules))
        {
            try
            {
                CompiledCensorRegexRule compiled;
                compiled.patternText = rule.first;
                compiled.replacement = rule.second;
                compiled.pattern = std::wregex(rule.first, std::regex_constants::icase);
                compiledRules->push_back(std::move(compiled));
            }
            catch (const std::exception&)
            {
                LogError(L"Invalid censor regex ignored: " + rule.first);
            }
        }
        g_cachedPatternReplaceRaw = rawRules;
        g_cachedPatternReplaceRules = compiledRules;
        g_cachedPatternRuntimeFailureRaw = rawRules;
        g_cachedPatternRuntimeFailurePatterns.clear();
    }
    return g_cachedPatternReplaceRules;
}

static void ReportPatternRuntimeFailureOnce(const std::wstring& rawRules, const std::wstring& patternText)
{
    bool shouldReport = false;
    {
        std::lock_guard<std::mutex> lock(g_censorRulesMutex);
        if (rawRules != g_cachedPatternRuntimeFailureRaw)
        {
            g_cachedPatternRuntimeFailureRaw = rawRules;
            g_cachedPatternRuntimeFailurePatterns.clear();
        }
        if (std::find(g_cachedPatternRuntimeFailurePatterns.begin(), g_cachedPatternRuntimeFailurePatterns.end(), patternText) == g_cachedPatternRuntimeFailurePatterns.end())
        {
            g_cachedPatternRuntimeFailurePatterns.push_back(patternText);
            shouldReport = true;
        }
    }

    if (shouldReport)
    {
        LogError(L"Censor regex failed during replacement: " + patternText);
    }
}

static std::wstring ReplaceAllI(std::wstring input, const std::wstring& from, const std::wstring& to)
{
    if (from.empty())
    {
        return input;
    }

    std::wstring lowerInput = ToLower(input);
    std::wstring lowerFrom = ToLower(from);
    size_t pos = 0;
    while ((pos = lowerInput.find(lowerFrom, pos)) != std::wstring::npos)
    {
        input.replace(pos, from.size(), to);
        lowerInput.replace(pos, from.size(), ToLower(to));
        pos += to.size();
    }
    return input;
}

static std::vector<CensorRuleKind> ParseCensorRuleOrder(const std::wstring& rawOrder, std::vector<std::wstring>* unknownTokens = nullptr)
{
    std::vector<CensorRuleKind> order;
    size_t pos = 0;
    while (pos <= rawOrder.size())
    {
        size_t end = rawOrder.find_first_of(L",;", pos);
        std::wstring token = ToLower(Trim(end == std::wstring::npos ? rawOrder.substr(pos) : rawOrder.substr(pos, end - pos)));
        if (token == L"full_replace")
        {
            order.push_back(CensorRuleKind::FullReplace);
        }
        else if (token == L"word_replace")
        {
            order.push_back(CensorRuleKind::WordReplace);
        }
        else if (token == L"pattern_replace")
        {
            order.push_back(CensorRuleKind::PatternReplace);
        }
        else if (!token.empty() && unknownTokens != nullptr)
        {
            unknownTokens->push_back(token);
        }

        if (end == std::wstring::npos)
        {
            break;
        }
        pos = end + 1;
    }

    if (order.empty())
    {
        order = { CensorRuleKind::FullReplace, CensorRuleKind::WordReplace, CensorRuleKind::PatternReplace };
    }
    return order;
}

static std::shared_ptr<const std::vector<CensorRuleKind>> GetCachedCensorRuleOrder(const std::wstring& rawOrder)
{
    std::lock_guard<std::mutex> lock(g_censorRulesMutex);
    if (rawOrder != g_cachedRuleOrderRaw || !g_cachedRuleOrder)
    {
        std::vector<std::wstring> unknownTokens;
        g_cachedRuleOrder = std::make_shared<const std::vector<CensorRuleKind>>(ParseCensorRuleOrder(rawOrder, &unknownTokens));
        g_cachedRuleOrderRaw = rawOrder;
        for (const auto& token : unknownTokens)
        {
            LogError(L"Unknown censor_map rule_order entry ignored: " + token);
        }
    }
    return g_cachedRuleOrder;
}

static bool GetCachedApplyPatternOnRaw(const std::wstring& rawValue)
{
    std::lock_guard<std::mutex> lock(g_censorRulesMutex);
    if (rawValue != g_cachedApplyPatternOnRawRaw)
    {
        bool parsed = false;
        g_cachedApplyPatternOnRaw = aip::ParseBoolValue(rawValue, parsed) ? parsed : false;
        g_cachedApplyPatternOnRawRaw = rawValue;
    }
    return g_cachedApplyPatternOnRaw;
}

static void ApplyFullCensorRules(std::wstring& title)
{
    auto rules = GetCachedFullCensorRules(IniReadS(L"censor_map", L"full_replace", L""));
    for (const auto& rule : *rules)
    {
        if (EqualsI(title, rule.first.c_str()))
        {
            title = rule.second;
        }
    }
}

static void ApplyWordCensorRules(std::wstring& title)
{
    auto rules = GetCachedWordCensorRules(IniReadS(L"censor_map", L"word_replace", L""));
    for (const auto& rule : *rules)
    {
        title = ReplaceAllI(title, rule.first, rule.second);
    }
}

static void ApplyPatternCensorRules(std::wstring& title, const std::wstring& rawTitle)
{
    bool applyPatternOnRaw = GetCachedApplyPatternOnRaw(IniReadRaw(L"censor_map", L"apply_pattern_on_raw", L"false"));
    std::wstring patternInput = applyPatternOnRaw ? rawTitle : title;
    std::wstring rawRules = IniReadS(L"censor_map", L"pattern_replace", L"");
    auto rules = GetCompiledPatternCensorRules(rawRules);
    for (const auto& rule : *rules)
    {
        try
        {
            patternInput = std::regex_replace(patternInput, rule.pattern, rule.replacement);
        }
        catch (const std::exception&)
        {
            ReportPatternRuntimeFailureOnce(rawRules, rule.patternText);
        }
    }
    title = patternInput;
}

static std::wstring ApplyCensorRules(const std::wstring& rawTitle)
{
    std::wstring title = rawTitle;
    auto order = GetCachedCensorRuleOrder(IniReadS(L"censor_map", L"rule_order", L"full_replace, word_replace, pattern_replace"));
    for (CensorRuleKind kind : *order)
    {
        switch (kind)
        {
        case CensorRuleKind::FullReplace:
            ApplyFullCensorRules(title);
            break;
        case CensorRuleKind::WordReplace:
            ApplyWordCensorRules(title);
            break;
        case CensorRuleKind::PatternReplace:
            ApplyPatternCensorRules(title, rawTitle);
            break;
        }
    }
    return title;
}

static std::wstring ApplyTemplate(const std::wstring& templateText, const TemplateContext& ctx)
{
    std::wstring out;
    out.reserve(templateText.size() + 32);
    for (size_t i = 0; i < templateText.size(); ++i)
    {
        wchar_t ch = templateText[i];
        if (ch == L'{' && i + 1 < templateText.size() && templateText[i + 1] == L'{')
        {
            out.push_back(L'{');
            ++i;
        }
        else if (ch == L'}' && i + 1 < templateText.size() && templateText[i + 1] == L'}')
        {
            out.push_back(L'}');
            ++i;
        }
        else if (ch == L'{')
        {
            size_t end = templateText.find(L'}', i + 1);
            if (end == std::wstring::npos)
            {
                out.push_back(ch);
            }
            else
            {
                std::wstring key = templateText.substr(i + 1, end - i - 1);
                auto found = ctx.tokens.find(key);
                if (found != ctx.tokens.end())
                {
                    out += found->second;
                }
                else
                {
                    out += templateText.substr(i, end - i + 1);
                }
                i = end;
            }
        }
        else
        {
            out.push_back(ch);
        }
    }
    return Trim(out);
}

static TemplateContext BuildTemplateContext()
{
    TemplateContext ctx;
    SYSTEMTIME now = {};
    GetLocalTime(&now);

    SystemStats stats = GetSystemStats();
    BatteryInfo battery = GetBatteryInfo();
    ctx.idleSeconds = GetInputIdleSeconds();

    std::wstring idleMessage = IniReadS(L"general", L"idle_message", L"Idle");
    ForegroundWindowInfo rawWindow = GetForegroundWindowInfo(idleMessage);
    rawWindow.title = ApplyCensorRules(rawWindow.title);

    if (!IniReadB(L"general", L"window_title_detection_enabled", true))
    {
        rawWindow.title = IniReadS(L"general", L"fallback_details", L"Foreground program detection is disabled.");
        rawWindow.className.clear();
        rawWindow.exeName.clear();
        rawWindow.exePath.clear();
        rawWindow.pid = 0;
    }

    ctx.tokens[L"time"] = FormatTwoDigits((now.wHour + 11) % 12 + 1) + L":" + FormatTwoDigits(now.wMinute) + L" " + (now.wHour >= 12 ? L"PM" : L"AM");
    ctx.tokens[L"time_24"] = FormatTwoDigits(now.wHour) + L":" + FormatTwoDigits(now.wMinute);
    ctx.tokens[L"time_h12"] = FormatTwoDigits((now.wHour + 11) % 12 + 1);
    ctx.tokens[L"time_h24"] = FormatTwoDigits(now.wHour);
    ctx.tokens[L"time_min"] = FormatTwoDigits(now.wMinute);
    ctx.tokens[L"time_sec"] = FormatTwoDigits(now.wSecond);
    ctx.tokens[L"time_ampm"] = now.wHour >= 12 ? L"PM" : L"AM";
    ctx.tokens[L"date"] = std::to_wstring(now.wYear) + L"-" + FormatTwoDigits(now.wMonth) + L"-" + FormatTwoDigits(now.wDay);
    ctx.tokens[L"date_day"] = FormatTwoDigits(now.wDay);
    ctx.tokens[L"date_mon"] = FormatTwoDigits(now.wMonth);
    ctx.tokens[L"date_yr"] = std::to_wstring(now.wYear);
    ctx.tokens[L"datetime"] = ctx.tokens[L"date"] + L" " + ctx.tokens[L"time"];

    ctx.tokens[L"cpu"] = FormatDouble(stats.cpuPercent) + L"%";
    ctx.tokens[L"ram_pct"] = FormatDouble(stats.ramPercent) + L"%";
    ctx.tokens[L"ram_total"] = FormatDouble(stats.totalRamGb) + L"GB";
    ctx.tokens[L"ram_used"] = FormatDouble(stats.usedRamGb) + L"GB";
    ctx.tokens[L"ram_avail"] = FormatDouble(stats.availRamGb) + L"GB";

    ULONGLONG uptimeSeconds = GetTickCount64() / 1000;
    ctx.tokens[L"uptime"] = std::to_wstring(uptimeSeconds / 3600) + L"h " + std::to_wstring((uptimeSeconds % 3600) / 60) + L"m";
    ctx.tokens[L"uptime_h"] = std::to_wstring(uptimeSeconds / 3600);
    ctx.tokens[L"uptime_m"] = std::to_wstring((uptimeSeconds % 3600) / 60);
    ctx.tokens[L"uptime_s"] = std::to_wstring(uptimeSeconds % 60);

    if (battery.hasBattery)
    {
        ctx.tokens[L"battery_pct"] = std::to_wstring(battery.percent) + L"%";
        ctx.tokens[L"battery_plug"] = battery.plugged ? L"AC" : L"BAT";
        ctx.tokens[L"battery_remaining"] = battery.remainingText;
    }
    else
    {
        ctx.tokens[L"battery_pct"] = L"";
        ctx.tokens[L"battery_plug"] = L"Desktop";
        ctx.tokens[L"battery_remaining"] = L"";
    }

    ctx.tokens[L"win_title"] = rawWindow.title;
    ctx.tokens[L"win_class"] = rawWindow.className;
    ctx.tokens[L"win_exe"] = rawWindow.exeName;
    ctx.tokens[L"win_exe_path"] = rawWindow.exePath;
    ctx.tokens[L"win_pid"] = rawWindow.pid == 0 ? L"" : std::to_wstring(rawWindow.pid);

    wchar_t computer[256] = {};
    DWORD computerLen = ARRAYSIZE(computer);
    if (GetComputerNameW(computer, &computerLen))
    {
        ctx.tokens[L"computer"] = computer;
    }

    wchar_t username[256] = {};
    DWORD usernameLen = ARRAYSIZE(username);
    if (GetUserNameW(username, &usernameLen))
    {
        ctx.tokens[L"username"] = username;
    }

    OSVERSIONINFOW os = {};
    os.dwOSVersionInfoSize = sizeof(os);
    bool haveOsVersion = false;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr)
    {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion != nullptr && rtlGetVersion(&os) == 0)
        {
            haveOsVersion = true;
        }
    }
#pragma warning(push)
#pragma warning(disable:4996)
    if (!haveOsVersion && GetVersionExW(&os))
#pragma warning(pop)
    {
        haveOsVersion = true;
    }
    if (haveOsVersion)
    {
        ctx.tokens[L"os"] = L"Windows " + std::to_wstring(os.dwMajorVersion) + L"." + std::to_wstring(os.dwMinorVersion) + L"." + std::to_wstring(os.dwBuildNumber);
    }

    ctx.tokens[L"screen_w"] = std::to_wstring(GetSystemMetrics(SM_CXSCREEN));
    ctx.tokens[L"screen_h"] = std::to_wstring(GetSystemMetrics(SM_CYSCREEN));

    ctx.tokens[L"idle_hh"] = std::to_wstring(ctx.idleSeconds / 3600);
    ctx.tokens[L"idle_mm"] = std::to_wstring((ctx.idleSeconds % 3600) / 60);
    ctx.tokens[L"idle_ss"] = std::to_wstring(ctx.idleSeconds % 60);
    ctx.tokens[L"idle_total_h"] = std::to_wstring(ctx.idleSeconds / 3600);
    ctx.tokens[L"idle_total_m"] = std::to_wstring(ctx.idleSeconds / 60);
    ctx.tokens[L"idle_total_s"] = std::to_wstring(ctx.idleSeconds);
    ctx.tokens[L"empty"] = L"";
    ctx.tokens[L"exe_path"] = g_exePath;

    return ctx;
}

static int NormalizeHour(int value)
{
    value %= 24;
    if (value < 0)
    {
        value += 24;
    }
    return value;
}

static bool HourInRange(int hour, int start, int end)
{
    start = NormalizeHour(start);
    end = NormalizeHour(end);
    if (start == end)
    {
        return true;
    }
    if (start < end)
    {
        return hour >= start && hour < end;
    }
    return hour >= start || hour < end;
}

static AssetInfo ChooseTimedAsset(const wchar_t* rangeSection, const wchar_t* assetSection, const TemplateContext& ctx)
{
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    int hour = now.wHour;
    std::wstring slot = L"default";

    if (HourInRange(hour, IniReadI(rangeSection, L"morning_start", 5), IniReadI(rangeSection, L"morning_end", 12)))
    {
        slot = L"morning";
    }
    else if (HourInRange(hour, IniReadI(rangeSection, L"afternoon_start", 12), IniReadI(rangeSection, L"afternoon_end", 16)))
    {
        slot = L"afternoon";
    }
    else if (HourInRange(hour, IniReadI(rangeSection, L"evening_start", 16), IniReadI(rangeSection, L"evening_end", 22)))
    {
        slot = L"evening";
    }
    else if (HourInRange(hour, IniReadI(rangeSection, L"night_start", 22), IniReadI(rangeSection, L"night_end", 5)))
    {
        slot = L"night";
    }

    AssetInfo asset;
    asset.image = ApplyTemplate(IniReadS(assetSection, (slot + L"_image").c_str(), L""), ctx);
    asset.text = ApplyTemplate(IniReadS(assetSection, (slot + L"_text").c_str(), IniReadS(assetSection, L"default_text", L"").c_str()), ctx);
    return asset;
}

static AssetInfo ChooseSmallAsset(const TemplateContext& ctx)
{
    int threshold = IniReadI(L"afk", L"idle_threshold", 60);
    bool lockScreen = EqualsI(ctx.tokens.at(L"win_title"), L"Windows Default Lock Screen");
    if (threshold > 0 && (ctx.idleSeconds >= threshold || lockScreen))
    {
        AssetInfo asset;
        std::wstring idleText;
        if (IniReadB(L"afk", L"show_idle_time", true))
        {
            idleText = ApplyTemplate(IniReadS(L"afk", L"idle_text", L"since {idle_hh}h {idle_mm}m {idle_ss}s"), ctx);
        }
        asset.image = ApplyTemplate(IniReadS(L"afk", L"afk_image", L""), ctx);
        asset.text = Trim(ApplyTemplate(IniReadS(L"afk", L"afk_message", L"AFK"), ctx) + L" " + idleText);
        return asset;
    }

    return ChooseTimedAsset(L"small_time_ranges", L"small_assets", ctx);
}

static std::wstring LimitDiscordText(const std::wstring& value, size_t maxLength)
{
    std::wstring trimmed = Trim(value);
    if (trimmed.size() <= maxLength)
    {
        return trimmed;
    }
    return trimmed.substr(0, maxLength);
}

static long long BootUnixTime()
{
    return static_cast<long long>(std::time(nullptr) - static_cast<time_t>(GetTickCount64() / 1000));
}

static PresencePayload BuildPresencePayload()
{
    PresencePayload payload;
    payload.context = BuildTemplateContext();
    const TemplateContext& ctx = payload.context;

    std::string activity = "{";
    bool first = true;

    std::wstring name = ApplyTemplate(IniReadS(L"general", L"activity_name", L"a game"), ctx);
    if (name.empty())
    {
        name = L"a game";
    }
    JsonFieldString(activity, first, "name", LimitDiscordText(name, 128));

    int activityType = IniReadI(L"general", L"activity_type", 0);
    if (activityType < 0 || activityType > 5 || activityType == 4)
    {
        activityType = 0;
    }
    JsonFieldNumber(activity, first, "type", activityType);

    if (IniReadB(L"layout", L"details_field", true))
    {
        std::wstring details = ApplyTemplate(IniReadS(L"general", L"details_template", L"{win_title}"), ctx);
        if (!details.empty())
        {
            JsonFieldString(activity, first, "details", LimitDiscordText(details, 128));
        }
    }

    if (IniReadB(L"layout", L"state_field", true))
    {
        std::wstring state = ApplyTemplate(IniReadS(L"general", L"state_template", L"CPU: {cpu}, RAM: {ram_used}/{ram_total} ({ram_pct})"), ctx);
        if (!state.empty())
        {
            JsonFieldString(activity, first, "state", LimitDiscordText(state, 128));
        }
    }

    JsonFieldRaw(activity, first, "timestamps", std::string("{\"start\":") + std::to_string(BootUnixTime()) + "}");

    AssetInfo largeAsset = ChooseTimedAsset(L"large_time_ranges", L"large_assets", ctx);
    AssetInfo smallAsset = ChooseSmallAsset(ctx);
    std::string assets = "{";
    bool assetsFirst = true;
    if (IniReadB(L"layout", L"show_large_image", false))
    {
        if (!Trim(largeAsset.image).empty())
        {
            JsonFieldString(assets, assetsFirst, "large_image", Trim(largeAsset.image));
        }
        if (!Trim(largeAsset.text).empty())
        {
            JsonFieldString(assets, assetsFirst, "large_text", LimitDiscordText(largeAsset.text, 128));
        }
    }
    if (IniReadB(L"layout", L"show_small_image", false))
    {
        if (!Trim(smallAsset.image).empty())
        {
            JsonFieldString(assets, assetsFirst, "small_image", Trim(smallAsset.image));
        }
        if (!Trim(smallAsset.text).empty())
        {
            JsonFieldString(assets, assetsFirst, "small_text", LimitDiscordText(smallAsset.text, 128));
        }
    }
    assets.push_back('}');
    if (!assetsFirst)
    {
        JsonFieldRaw(activity, first, "assets", assets);
    }

    if (IniReadB(L"layout", L"show_buttons", false))
    {
        std::string buttons = "[";
        bool buttonFirst = true;
        for (int i = 1; i <= 2; ++i)
        {
            std::wstring labelKey = L"button_" + std::to_wstring(i) + L"_label";
            std::wstring urlKey = L"button_" + std::to_wstring(i) + L"_url";
            std::wstring label = LimitDiscordText(ApplyTemplate(IniReadS(L"buttons", labelKey.c_str(), L""), ctx), 32);
            std::wstring url = Trim(IniReadS(L"buttons", urlKey.c_str(), L""));
            if (label.empty() || url.empty())
            {
                continue;
            }
            if (!buttonFirst)
            {
                buttons.push_back(',');
            }
            buttonFirst = false;
            std::string button = "{";
            bool buttonFieldFirst = true;
            JsonFieldString(button, buttonFieldFirst, "label", label);
            JsonFieldString(button, buttonFieldFirst, "url", url);
            button.push_back('}');
            buttons += button;
        }
        buttons.push_back(']');
        if (!buttonFirst)
        {
            JsonFieldRaw(activity, first, "buttons", buttons);
        }
    }

    JsonFieldBool(activity, first, "instance", true);
    activity.push_back('}');
    payload.activityJson = activity;
    return payload;
}

static std::string BuildSetActivityPayload(const std::string& activityJson)
{
    std::string args = "{";
    bool argsFirst = true;
    JsonFieldNumber(args, argsFirst, "pid", GetCurrentProcessId());
    JsonFieldRaw(args, argsFirst, "activity", activityJson);
    args.push_back('}');

    static std::atomic<unsigned long> nonceCounter(0);
    std::wstring nonce = std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++nonceCounter);

    std::string payload = "{";
    bool first = true;
    JsonFieldString(payload, first, "cmd", L"SET_ACTIVITY");
    JsonFieldRaw(payload, first, "args", args);
    JsonFieldString(payload, first, "nonce", nonce);
    payload.push_back('}');
    return payload;
}

static std::string BuildClearActivityPayload()
{
    std::string args = "{";
    bool argsFirst = true;
    JsonFieldNumber(args, argsFirst, "pid", GetCurrentProcessId());
    JsonFieldRaw(args, argsFirst, "activity", "null");
    args.push_back('}');

    std::string payload = "{";
    bool first = true;
    JsonFieldString(payload, first, "cmd", L"SET_ACTIVITY");
    JsonFieldRaw(payload, first, "args", args);
    JsonFieldString(payload, first, "nonce", std::to_wstring(GetTickCount64()));
    payload.push_back('}');
    return payload;
}

static bool IsSensitiveTokenKey(const std::wstring& key)
{
    return key == L"win_title" ||
        key == L"win_exe_path" ||
        key == L"win_pid" ||
        key == L"computer" ||
        key == L"username" ||
        key == L"exe_path";
}

static int RunDryRun(bool full)
{
    PresencePayload payload = BuildPresencePayload();
    PrintLine(GetUiString(L"dry_run_header", L"=== Discord activity JSON ==="));
    PrintLine(Utf8ToWide(payload.activityJson));
    PrintLine(L"");
    PrintLine(GetUiString(L"dry_run_tokens", L"=== Available template tokens ==="));
    if (!full && IniReadB(L"app", L"dry_run_redact_sensitive", true))
    {
        PrintLine(GetUiString(L"dry_run_redacted_note", L"Sensitive local tokens are redacted. Use --dry-run-full to show raw values."));
    }

    for (const auto& entry : payload.context.tokens)
    {
        std::wstring value = entry.second;
        if (!full && IniReadB(L"app", L"dry_run_redact_sensitive", true) && IsSensitiveTokenKey(entry.first) && !value.empty())
        {
            value = L"<redacted:" + entry.first + L">";
        }
        PrintLine(L"  {" + entry.first + L"} = " + value);
    }
    return 0;
}

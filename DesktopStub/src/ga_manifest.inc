std::wstring GetExeDir()
{
    size_t pos = g_exePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return g_exePath.substr(0, pos);
}

std::wstring MakeFileUri(const std::wstring& path)
{
    DWORD need = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!need) return L"";

    std::vector<wchar_t> full(need);
    DWORD n = GetFullPathNameW(path.c_str(), (DWORD)full.size(), full.data(), nullptr);
    if (!n || n >= full.size()) return L"";

    std::wstring p(full.data(), n);
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0)
        p = L"\\\\" + p.substr(8);
    else if (p.rfind(L"\\\\?\\", 0) == 0)
        p.erase(0, 4);

    size_t uriCapSize = std::max<size_t>(260, p.size() * 3 + 32);
    if (uriCapSize <= MAXDWORD)
    {
        std::vector<wchar_t> uri(uriCapSize);
        DWORD uriLen = static_cast<DWORD>(uri.size());
        if (SUCCEEDED(UrlCreateFromPathW(p.c_str(), uri.data(), &uriLen, 0)) && uri[0])
            return std::wstring(uri.data());
    }

    std::replace(p.begin(), p.end(), L'\\', L'/');

    std::wstring esc;
    esc.reserve(p.size() * 3);
    for (wchar_t ch : p)
    {
        switch (ch)
        {
        case L' ':  esc += L"%20"; break;
        case L'#':  esc += L"%23"; break;
        case L'%':  esc += L"%25"; break;
        case L'?':  esc += L"%3F"; break;
        case L'"':  esc += L"%22"; break;
        case L'<':  esc += L"%3C"; break;
        case L'>':  esc += L"%3E"; break;
        case L'|':  esc += L"%7C"; break;
        default:    esc.push_back(ch); break;
        }
    }

    if (esc.size() >= 2 && esc[1] == L':')
        esc.insert(esc.begin(), L'/');

    if (esc.rfind(L"//", 0) == 0)
        return L"file:" + esc;

    return L"file://" + esc;
}

static bool IsAsciiAlpha(wchar_t ch)
{
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

static bool IsAsciiDigit(wchar_t ch)
{
    return ch >= L'0' && ch <= L'9';
}

static bool IsAsciiAlnum(wchar_t ch)
{
    return IsAsciiAlpha(ch) || IsAsciiDigit(ch);
}

static std::wstring ExeFileName()
{
    std::wstring name = g_exePath;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name.erase(0, slash + 1);

    return name.empty() ? L"DesktopStub.exe" : name;
}

static std::wstring ExeBaseName()
{
    std::wstring name = ExeFileName();
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
        name.erase(dot);

    return name.empty() ? L"DesktopStub" : name;
}

static std::wstring PortableHostBaseName()
{
    std::wstring base = ExeBaseName();
    constexpr const wchar_t* appxStubSuffix = L"AppxStub";
    constexpr size_t appxStubSuffixLen = 8;
    if (base.size() > appxStubSuffixLen &&
        IEquals(base.substr(base.size() - appxStubSuffixLen), appxStubSuffix))
    {
        base.resize(base.size() - appxStubSuffixLen);
    }

    return base.empty() ? L"DesktopStub" : base;
}

static std::wstring DefaultLogPath()
{
    return GetExeDir() + L"\\" + ExeBaseName() + L".log";
}

static std::wstring EffectiveConfiguredLogPath()
{
    std::wstring configured = TrimCopy(IniReadS(L"Settings", L"LogPath", L""));
    return configured.empty() ? DefaultLogPath() : configured;
}

static std::wstring ManifestSetting(const wchar_t* key, const wchar_t* fallback, const wchar_t* legacySettingsKey = nullptr)
{
    // Manifest generation intentionally uses built-in defaults. The old [Manifest]
    // INI section and legacy [Settings] Manifest* keys are ignored so the INI
    // does not become a second manifest editor next to AppxManifest.xml itself.
    (void)key;
    (void)legacySettingsKey;
    return fallback ? fallback : L"";
}

static bool TryParseIntStrict(const std::wstring& value, int& parsed)
{
    std::wstring trimmed = TrimCopy(value);
    if (trimmed.empty())
        return false;

    errno = 0;
    wchar_t* end = nullptr;
    long result = wcstol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || errno == ERANGE || result < INT_MIN || result > INT_MAX)
        return false;

    while (end && *end && iswspace(*end) != 0)
        ++end;
    if (end && *end)
        return false;

    parsed = static_cast<int>(result);
    return true;
}

static bool IsManifestSimpleTokenChar(wchar_t ch)
{
    return IsAsciiAlnum(ch) || ch == L'_' || ch == L'-';
}

static bool IsManifestNonEmptyDottedToken(const std::wstring& value)
{
    std::wstring v = TrimCopy(value);
    if (v.empty() || v.front() == L'.' || v.back() == L'.')
        return false;

    bool previousDot = false;
    for (wchar_t ch : v)
    {
        if (ch == L'.')
        {
            if (previousDot)
                return false;
            previousDot = true;
            continue;
        }
        if (!IsManifestSimpleTokenChar(ch))
            return false;
        previousDot = false;
    }
    return true;
}

static bool IsManifestOptionalDottedToken(const std::wstring& value)
{
    std::wstring v = TrimCopy(value);
    return v.empty() || IsManifestNonEmptyDottedToken(v);
}

static bool IsManifestResourceLanguageValue(const std::wstring& value)
{
    std::wstring v = TrimCopy(value);
    if (v.empty() || v.front() == L'-' || v.back() == L'-')
        return false;

    bool previousDash = false;
    for (wchar_t ch : v)
    {
        if (ch == L'-')
        {
            if (previousDash)
                return false;
            previousDash = true;
            continue;
        }
        if (!IsAsciiAlnum(ch))
            return false;
        previousDash = false;
    }
    return true;
}

static bool IsManifestSafeRelativePath(const std::wstring& value)
{
    std::wstring v = TrimCopy(value);
    if (v.empty() || v.front() == L'\\' || v.front() == L'/')
        return false;
    if (!PathIsRelativeW(v.c_str()))
        return false;

    size_t segmentStart = 0;
    bool sawFileName = false;
    for (size_t i = 0; i <= v.size(); ++i)
    {
        wchar_t ch = (i < v.size()) ? v[i] : L'\\';
        if (i < v.size() && (ch < 32 || ch == L'<' || ch == L'>' || ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*' || ch == L':'))
            return false;

        if (ch != L'\\' && ch != L'/')
            continue;

        std::wstring segment = v.substr(segmentStart, i - segmentStart);
        if (segment.empty() || segment == L"." || segment == L"..")
            return false;
        sawFileName = true;
        segmentStart = i + 1;
    }

    return sawFileName;
}

static bool IsManifestExecutableValue(const std::wstring& value)
{
    return IsManifestSafeRelativePath(value);
}

static bool IsManifestRelativeAssetPath(const std::wstring& value)
{
    return IsManifestSafeRelativePath(value);
}

static bool IsManifestBackgroundColorValue(const std::wstring& value)
{
    std::wstring v = ToLowerCopy(TrimCopy(value));
    if (v == L"transparent")
        return true;
    if (v.size() != 7 && v.size() != 9)
        return false;
    if (v.front() != L'#')
        return false;
    for (size_t i = 1; i < v.size(); ++i)
    {
        if (HexDigitValue(v[i]) < 0)
            return false;
    }
    return true;
}

static bool IsManifestPublisherValue(const std::wstring& value)
{
    std::wstring v = TrimCopy(value);
    if (v.empty())
        return false;
    for (wchar_t ch : v)
    {
        if (ch < 32 || ch == L'<' || ch == L'>')
            return false;
    }
    return true;
}

static std::wstring ManifestSettingValidated(
    const wchar_t* key,
    const wchar_t* fallback,
    const wchar_t* legacySettingsKey,
    bool (*validator)(const std::wstring&))
{
    std::wstring value = ManifestSetting(key, fallback, legacySettingsKey);
    if (!validator || validator(value))
        return value;
    return fallback ? fallback : L"";
}

static std::wstring TrimNonAlnumEnds(std::wstring value)
{
    while (!value.empty() && !IsAsciiAlnum(value.front()))
        value.erase(value.begin());
    while (!value.empty() && !IsAsciiAlnum(value.back()))
        value.pop_back();
    return value;
}

static std::wstring SanitizeManifestIdentityName(const std::wstring& raw)
{
    std::wstring lower = ToLowerCopy(raw);
    std::wstring out;
    bool lastSeparator = false;
    for (wchar_t ch : lower)
    {
        if (IsAsciiAlnum(ch))
        {
            out.push_back(ch);
            lastSeparator = false;
        }
        else if (ch == L'.' || ch == L'-')
        {
            if (!out.empty() && !lastSeparator)
            {
                out.push_back(ch);
                lastSeparator = true;
            }
        }
        else if (!out.empty() && !lastSeparator)
        {
            out.push_back(L'.');
            lastSeparator = true;
        }
    }

    out = TrimNonAlnumEnds(out);
    if (out.empty())
        out = L"generateassets";

    if (out.find(L'.') == std::wstring::npos)
        out = L"dev.local." + out;

    if (out.size() > 50)
    {
        out.resize(50);
        out = TrimNonAlnumEnds(out);
    }

    return out.empty() ? L"dev.local.generateassets" : out;
}

static std::wstring SanitizeManifestToken(const std::wstring& raw, const wchar_t* fallback)
{
    std::wstring out;
    for (wchar_t ch : raw)
    {
        if (IsAsciiAlnum(ch) || ch == L'.' || ch == L'_' || ch == L'-')
            out.push_back(ch);
    }

    out = TrimNonAlnumEnds(out);
    if (out.empty())
        out = fallback ? fallback : L"App";
    if (!IsAsciiAlpha(out.front()))
        out = std::wstring(L"App") + out;
    return out;
}

static bool IsManifestVersion(const std::wstring& version)
{
    size_t pos = 0;
    int parts = 0;
    while (pos <= version.size())
    {
        size_t dot = version.find(L'.', pos);
        size_t end = dot == std::wstring::npos ? version.size() : dot;
        if (end == pos)
            return false;

        errno = 0;
        wchar_t* parseEnd = nullptr;
        std::wstring part = version.substr(pos, end - pos);
        unsigned long value = wcstoul(part.c_str(), &parseEnd, 10);
        if (parseEnd == part.c_str() || *parseEnd != L'\0' || errno == ERANGE || value > 65535)
            return false;

        ++parts;
        if (dot == std::wstring::npos)
            break;
        pos = dot + 1;
    }

    return parts == 4;
}

static std::wstring EffectiveManifestIdentityName()
{
    std::wstring configured = ManifestSetting(L"IdentityName", L"", L"ManifestIdentityName");
    if (configured.empty())
        configured = L"dev.local." + ExeBaseName();
    return SanitizeManifestIdentityName(configured);
}

static std::wstring EffectiveManifestPublisher()
{
    std::wstring publisher = ManifestSettingValidated(L"Publisher", L"CN=DesktopStub", L"ManifestPublisher", IsManifestPublisherValue);
    if (ToLowerCopy(publisher).rfind(L"cn=", 0) != 0)
        publisher = L"CN=" + publisher;
    return publisher;
}

static std::wstring EffectiveManifestPackageVersion()
{
    std::wstring version = ManifestSetting(L"PackageVersion", L"1.0.0.0", L"ManifestPackageVersion");
    return IsManifestVersion(version) ? version : L"1.0.0.0";
}

static std::wstring EffectiveManifestDisplayName()
{
    return ManifestSetting(L"DisplayName", L"Desktop", L"ManifestDisplayName");
}

static std::wstring EffectiveManifestPublisherDisplayName()
{
    return ManifestSetting(L"PublisherDisplayName", L"DesktopStub", L"ManifestPublisherDisplayName");
}

static std::wstring EffectiveManifestDescription()
{
    return ManifestSetting(L"Description", L"Desktop", L"ManifestDescription");
}

static std::wstring ManifestHostExecutableName()
{
    return PortableHostBaseName() + L".exe";
}

static std::wstring ManifestAppxActivationStubExecutableName()
{
    return PortableHostBaseName() + L"AppxStub.exe";
}

static std::wstring ManifestLiveTileBrokerExecutableName()
{
    return L"DesktopStubLiveTileBroker.exe";
}

static std::wstring ManifestLiveTileBrokerEntryPoint()
{
    return L"DesktopStub.LiveTileBroker.App";
}

static bool Win8LiveTileBrokerAppEnabled()
{
    // Experimental but now preferred for Win8/8.1 compatibility targets.
    // This uses a real CoreApplication-based packaged WinRT app as the tile
    // update broker instead of registering the main tray EXE as the RT app.
    return IniReadI(L"Settings", L"Win8LiveTileBrokerApp", 1) != 0;
}

static std::wstring ManifestLiveTileOopServerName()
{
    return L"DesktopStubLiveTileServer";
}

static std::wstring ManifestLiveTileOopRuntimeClassId()
{
    return L"DesktopStub.LiveTileUpdateServer";
}


static std::wstring ManifestLiveTileBackgroundTaskDllName()
{
    return L"DesktopStubLiveTileTask.dll";
}

static std::wstring ManifestLiveTileBackgroundTaskClassId()
{
    return L"DesktopStub.LiveTileBackgroundTask";
}

static std::wstring ManifestLiveTileBackgroundTaskName()
{
    return L"DesktopStubLiveTileUpdate";
}

static bool Win8LiveTileBackgroundTaskEnabled()
{
    // Experimental. This declares a WinRT background task and tries
    // ApplicationTrigger before falling back to the foreground package-identity
    // helper. In practice, ApplicationTrigger calls from the normal
    // DesktopStub.exe host fail because the host is deliberately unpackaged
    // (0x80073D57: no application identity). Keep this disabled by default;
    // set [Settings] Win8LiveTileBackgroundTask=1 only for targeted research.
    return IniReadI(L"Settings", L"Win8LiveTileBackgroundTask", 0) != 0;
}

static bool Win8LiveTileOopHelperEnabled()
{
    // Experimental. On Windows 10 with a Windows 8/8.1-style loose manifest,
    // RoActivateInstance currently returns REGDB_E_CLASSNOTREG for this
    // package-level out-of-process WinRT class. Keep it opt-in so the working
    // foreground package-identity helper stays fast and predictable.
    return IniReadI(L"Settings", L"Win8LiveTileOopHelper", 0) != 0;
}

static std::wstring EffectiveManifestExecutable()
{
    // Windows 8/8.1-style activation launches the manifest executable as an
    // immersive RT activation server and passes -ServerName:App.AppX... . If
    // the registered executable is the same resident tray EXE, Windows keeps an
    // empty immersive frame alive and can later report MoAppHang. The stabilized
    // compatibility path uses a small packaged CoreApplication broker for Live
    // Tile updates; the old copied activation stub remains available for tests.
    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    std::wstring fallback = ManifestHostExecutableName();
    if (target != ManifestCompatibilityTarget::Windows10)
    {
        fallback = Win8LiveTileBrokerAppEnabled()
            ? ManifestLiveTileBrokerExecutableName()
            : ManifestAppxActivationStubExecutableName();
    }
    return ManifestSettingValidated(L"Executable", fallback.c_str(), L"ManifestExecutable", IsManifestExecutableValue);
}

static std::wstring EffectiveManifestApplicationId()
{
    return SanitizeManifestToken(ManifestSetting(L"ApplicationId", L"App", L"ManifestApplicationId"), L"App");
}

static std::wstring EffectiveManifestMinVersion()
{
    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows81)
        return L"6.3.0";
    if (target == ManifestCompatibilityTarget::Windows8)
        return L"6.2.0";

    std::wstring version = ManifestSetting(L"MinVersion", L"10.0.15063.0", L"ManifestMinVersion");
    return IsManifestVersion(version) ? version : L"10.0.15063.0";
}

static std::wstring EffectiveManifestMaxVersionTested()
{
    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows81)
        return L"6.3.0";
    if (target == ManifestCompatibilityTarget::Windows8)
        return L"6.2.0";

    std::wstring version = ManifestSetting(L"MaxVersionTested", L"10.0.15063.0", L"ManifestMaxVersionTested");
    return IsManifestVersion(version) ? version : L"10.0.15063.0";
}

static std::wstring EffectiveManifestTargetDeviceFamilyName()
{
    return ManifestSettingValidated(L"TargetDeviceFamilyName", L"Windows.Desktop", L"ManifestTargetDeviceFamilyName", IsManifestNonEmptyDottedToken);
}

static std::wstring EffectiveManifestResourceLanguage()
{
    return ManifestSettingValidated(L"ResourceLanguage", L"en-us", L"ManifestResourceLanguage", IsManifestResourceLanguageValue);
}

static std::wstring EffectiveManifestPropertiesLogo()
{
    return ManifestSettingValidated(L"PropertiesLogo", L"Assets\\StoreLogo.png", L"ManifestPropertiesLogo", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestSquare150x150Logo()
{
    return ManifestSettingValidated(L"Square150x150Logo", L"Assets\\MediumTile.png", L"ManifestSquare150x150Logo", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestSquare44x44Logo()
{
    return ManifestSettingValidated(L"Square44x44Logo", L"Assets\\Square44x44Logo.png", L"ManifestSquare44x44Logo", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestSquare30x30Logo()
{
    return ManifestSettingValidated(L"Square30x30Logo", L"Assets\\Square30x30Logo.png", L"ManifestSquare30x30Logo", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestSquare71x71Logo()
{
    return ManifestSettingValidated(L"Square71x71Logo", L"Assets\\SmallTile.png", L"ManifestSquare71x71Logo", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestWide310x150Logo()
{
    return ManifestSettingValidated(L"Wide310x150Logo", L"Assets\\WideTile.png", L"ManifestWide310x150Logo", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestSquare310x310Logo()
{
    return ManifestSettingValidated(L"Square310x310Logo", L"Assets\\LargeTile.png", L"ManifestSquare310x310Logo", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestBackgroundColor()
{
    return ManifestSettingValidated(L"BackgroundColor", L"transparent", L"ManifestBackgroundColor", IsManifestBackgroundColorValue);
}

static std::wstring EffectiveManifestSplashScreenImage()
{
    return ManifestSettingValidated(L"SplashScreenImage", L"Assets\\SplashScreen.png", L"ManifestSplashScreenImage", IsManifestRelativeAssetPath);
}

static std::wstring EffectiveManifestEntryPoint()
{
    if (ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10)
    {
        if (Win8LiveTileBrokerAppEnabled())
            return ManifestLiveTileBrokerEntryPoint();
        std::wstring classBase = SanitizeManifestToken(ExeBaseName(), L"DesktopStub");
        return classBase + L".App";
    }

    return ManifestSettingValidated(L"EntryPoint", L"Windows.FullTrustApplication", L"ManifestEntryPoint", IsManifestNonEmptyDottedToken);
}

static std::wstring EffectiveManifestRestrictedCapability()
{
    if (ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10)
        return L"";

    std::wstring value = ManifestSetting(L"RestrictedCapability", L"runFullTrust", L"ManifestRestrictedCapability");
    std::wstring lowered = ToLowerCopy(TrimCopy(value));
    if (lowered == L"0" || lowered == L"false" || lowered == L"off" || lowered == L"none")
        return L"";
    return IsManifestOptionalDottedToken(value) ? value : L"runFullTrust";
}

static std::wstring XmlEscape(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

static std::wstring XmlUnescape(std::wstring value)
{
    struct Replacement { const wchar_t* from; const wchar_t* to; };
    static const Replacement replacements[] = {
        { L"&quot;", L"\"" },
        { L"&apos;", L"'" },
        { L"&lt;", L"<" },
        { L"&gt;", L">" },
        { L"&amp;", L"&" },
    };

    for (const auto& r : replacements)
    {
        size_t pos = 0;
        while ((pos = value.find(r.from, pos)) != std::wstring::npos)
        {
            value.replace(pos, wcslen(r.from), r.to);
            pos += wcslen(r.to);
        }
    }
    return value;
}

static bool IsXmlNameChar(wchar_t ch)
{
    return IsAsciiAlnum(ch) || ch == L'_' || ch == L'-' || ch == L'.' || ch == L':';
}

static std::wstring XmlLocalName(std::wstring name)
{
    size_t colon = name.find_last_of(L':');
    if (colon != std::wstring::npos)
        name.erase(0, colon + 1);
    return name;
}

static std::wstring XmlLocalNameFromTagStart(const wchar_t* tagStart)
{
    std::wstring s = TrimCopy(tagStart ? tagStart : L"");
    if (!s.empty() && s.front() == L'<')
        s.erase(s.begin());
    s = TrimCopy(s);

    size_t end = 0;
    while (end < s.size() && IsXmlNameChar(s[end]))
        ++end;
    return XmlLocalName(s.substr(0, end));
}

static bool FindXmlTagEnd(const std::wstring& xml, size_t tagStart, size_t& tagEnd)
{
    wchar_t quote = 0;
    for (size_t i = tagStart + 1; i < xml.size(); ++i)
    {
        wchar_t ch = xml[i];
        if (quote)
        {
            if (ch == quote)
                quote = 0;
            continue;
        }
        if (ch == L'\'' || ch == L'"')
        {
            quote = ch;
            continue;
        }
        if (ch == L'>')
        {
            tagEnd = i;
            return true;
        }
    }
    return false;
}

static bool FindXmlStartTagByLocalName(const std::wstring& xml, const std::wstring& localName, size_t from, size_t& tagStart, size_t& nameEnd, size_t& tagEnd)
{
    if (localName.empty())
        return false;

    size_t pos = from;
    while ((pos = xml.find(L'<', pos)) != std::wstring::npos)
    {
        size_t nameStart = pos + 1;
        if (nameStart >= xml.size())
            return false;
        if (xml[nameStart] == L'/' || xml[nameStart] == L'!' || xml[nameStart] == L'?')
        {
            ++pos;
            continue;
        }

        size_t end = nameStart;
        while (end < xml.size() && IsXmlNameChar(xml[end]))
            ++end;
        if (end == nameStart)
        {
            ++pos;
            continue;
        }

        std::wstring found = XmlLocalName(xml.substr(nameStart, end - nameStart));
        if (IEquals(found, localName) && FindXmlTagEnd(xml, pos, tagEnd))
        {
            tagStart = pos;
            nameEnd = end;
            return true;
        }
        ++pos;
    }
    return false;
}

static bool FindXmlEndTagByLocalName(const std::wstring& xml, const std::wstring& localName, size_t from, size_t& tagStart)
{
    size_t pos = from;
    while ((pos = xml.find(L"</", pos)) != std::wstring::npos)
    {
        size_t nameStart = pos + 2;
        size_t end = nameStart;
        while (end < xml.size() && IsXmlNameChar(xml[end]))
            ++end;
        if (end == nameStart)
        {
            pos += 2;
            continue;
        }

        std::wstring found = XmlLocalName(xml.substr(nameStart, end - nameStart));
        if (IEquals(found, localName))
        {
            tagStart = pos;
            return true;
        }
        pos += 2;
    }
    return false;
}

static bool ExtractXmlAttributeFromTag(const std::wstring& xml, const wchar_t* tagStartNeedle, const wchar_t* attributeName, std::wstring& out)
{
    size_t tag = 0;
    size_t nameEnd = 0;
    size_t tagEnd = 0;
    std::wstring tagLocalName = XmlLocalNameFromTagStart(tagStartNeedle);
    if (!FindXmlStartTagByLocalName(xml, tagLocalName, 0, tag, nameEnd, tagEnd))
        return false;

    std::wstring wantedAttribute = XmlLocalName(attributeName ? attributeName : L"");
    size_t pos = nameEnd;
    while (pos < tagEnd)
    {
        while (pos < tagEnd && iswspace(xml[pos]) != 0)
            ++pos;
        if (pos >= tagEnd || xml[pos] == L'/')
            break;

        size_t attrStart = pos;
        while (pos < tagEnd && IsXmlNameChar(xml[pos]))
            ++pos;
        if (pos == attrStart)
        {
            ++pos;
            continue;
        }

        std::wstring attrLocalName = XmlLocalName(xml.substr(attrStart, pos - attrStart));
        while (pos < tagEnd && iswspace(xml[pos]) != 0)
            ++pos;
        if (pos >= tagEnd || xml[pos] != L'=')
            continue;
        ++pos;
        while (pos < tagEnd && iswspace(xml[pos]) != 0)
            ++pos;
        if (pos >= tagEnd)
            break;

        wchar_t quote = xml[pos];
        size_t valueStart = pos;
        size_t valueEnd = pos;
        if (quote == L'\'' || quote == L'"')
        {
            valueStart = pos + 1;
            valueEnd = xml.find(quote, valueStart);
            if (valueEnd == std::wstring::npos || valueEnd > tagEnd)
                return false;
            pos = valueEnd + 1;
        }
        else
        {
            while (valueEnd < tagEnd && iswspace(xml[valueEnd]) == 0)
                ++valueEnd;
            pos = valueEnd;
        }

        if (IEquals(attrLocalName, wantedAttribute))
        {
            out = XmlUnescape(xml.substr(valueStart, valueEnd - valueStart));
            return true;
        }
    }
    return false;
}

static bool ExtractXmlElementText(const std::wstring& xml, const wchar_t* elementName, std::wstring& out)
{
    std::wstring localName = XmlLocalName(elementName ? elementName : L"");
    size_t tag = 0;
    size_t nameEnd = 0;
    size_t tagEnd = 0;
    if (!FindXmlStartTagByLocalName(xml, localName, 0, tag, nameEnd, tagEnd))
        return false;

    if (tagEnd > tag && xml[tagEnd - 1] == L'/')
        return false;

    size_t closeStart = 0;
    if (!FindXmlEndTagByLocalName(xml, localName, tagEnd + 1, closeStart))
        return false;

    out = XmlUnescape(xml.substr(tagEnd + 1, closeStart - tagEnd - 1));
    return true;
}

static bool ExtractXmlShowNameOnTiles(const std::wstring& xml, std::wstring& out)
{
    std::vector<std::wstring> tiles;
    size_t from = 0;
    size_t tag = 0;
    size_t nameEnd = 0;
    size_t tagEnd = 0;

    while (FindXmlStartTagByLocalName(xml, L"ShowOn", from, tag, nameEnd, tagEnd))
    {
        std::wstring tile;
        size_t pos = nameEnd;
        while (pos < tagEnd)
        {
            while (pos < tagEnd && iswspace(xml[pos]) != 0)
                ++pos;
            if (pos >= tagEnd || xml[pos] == L'/')
                break;

            size_t attrStart = pos;
            while (pos < tagEnd && IsXmlNameChar(xml[pos]))
                ++pos;
            if (pos == attrStart)
            {
                ++pos;
                continue;
            }

            std::wstring attrLocalName = XmlLocalName(xml.substr(attrStart, pos - attrStart));
            while (pos < tagEnd && iswspace(xml[pos]) != 0)
                ++pos;
            if (pos >= tagEnd || xml[pos] != L'=')
                continue;
            ++pos;
            while (pos < tagEnd && iswspace(xml[pos]) != 0)
                ++pos;
            if (pos >= tagEnd)
                break;

            wchar_t quote = xml[pos];
            size_t valueStart = pos;
            size_t valueEnd = pos;
            if (quote == L'\'' || quote == L'"')
            {
                valueStart = pos + 1;
                valueEnd = xml.find(quote, valueStart);
                if (valueEnd == std::wstring::npos || valueEnd > tagEnd)
                    break;
                pos = valueEnd + 1;
            }
            else
            {
                while (valueEnd < tagEnd && iswspace(xml[valueEnd]) == 0)
                    ++valueEnd;
                pos = valueEnd;
            }

            if (IEquals(attrLocalName, L"Tile"))
            {
                tile = XmlUnescape(xml.substr(valueStart, valueEnd - valueStart));
                break;
            }
        }

        if (!tile.empty())
            tiles.push_back(tile);
        from = tagEnd + 1;
    }

    if (tiles.empty())
        return false;

    out.clear();
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        if (i > 0)
            out += L",";
        out += tiles[i];
    }
    return true;
}

static std::vector<std::wstring> EffectiveManifestShowNameOnTiles()
{
    std::wstring configured = ManifestSetting(
        L"ShowNameOnTiles",
        L"square150x150Logo,wide310x150Logo,square310x310Logo",
        L"ManifestShowNameOnTiles");
    std::wstring lowered = ToLowerCopy(TrimCopy(configured));
    if (lowered == L"0" || lowered == L"false" || lowered == L"off" || lowered == L"none")
        return {};

    std::vector<std::wstring> tiles;
    size_t pos = 0;
    while (pos <= configured.size())
    {
        size_t comma = configured.find_first_of(L",;", pos);
        size_t end = comma == std::wstring::npos ? configured.size() : comma;
        std::wstring tile = TrimCopy(configured.substr(pos, end - pos));
        std::wstring loweredTile = ToLowerCopy(tile);
        if (loweredTile == L"square150x150logo")
            tiles.push_back(L"square150x150Logo");
        else if (loweredTile == L"wide310x150logo")
            tiles.push_back(L"wide310x150Logo");
        else if (loweredTile == L"square310x310logo")
            tiles.push_back(L"square310x310Logo");
        else if (!tile.empty())
        {
            // Ignore invalid ShowNameOnTiles entries instead of creating a
            // manifest that only fails later during AppX registration.
        }
        if (comma == std::wstring::npos)
            break;
        pos = comma + 1;
    }
    return tiles;
}

#pragma warning(push)
#pragma warning(disable: 4820) // Padding in this display-only struct is harmless.
struct ManifestDisplayInfo
{
    bool existing = false;
    std::wstring manifestTarget;
    std::wstring identityName;
    std::wstring publisher;
    std::wstring packageVersion;
    std::wstring displayName;
    std::wstring publisherDisplayName;
    std::wstring description;
    std::wstring executable;
    std::wstring applicationId;
    std::wstring targetDeviceFamilyName;
    std::wstring minVersion;
    std::wstring maxVersionTested;
    std::wstring resourceLanguage;
    std::wstring propertiesLogo;
    std::wstring square150x150Logo;
    std::wstring square44x44Logo;
    std::wstring square30x30Logo;
    std::wstring square71x71Logo;
    std::wstring wide310x150Logo;
    std::wstring square310x310Logo;
    std::wstring backgroundColor;
    std::wstring showNameOnTiles;
    std::wstring entryPoint;
    std::wstring restrictedCapability;
    std::wstring splashScreenImage;
};
#pragma warning(pop)

static ManifestDisplayInfo CurrentManifestDisplayInfo()
{
    ManifestDisplayInfo info;
    info.manifestTarget = ManifestCompatibilityTargetName(ConfiguredManifestCompatibilityTarget());
    info.identityName = EffectiveManifestIdentityName();
    info.publisher = EffectiveManifestPublisher();
    info.packageVersion = EffectiveManifestPackageVersion();
    info.displayName = EffectiveManifestDisplayName();
    info.publisherDisplayName = EffectiveManifestPublisherDisplayName();
    info.description = EffectiveManifestDescription();
    info.executable = EffectiveManifestExecutable();
    info.applicationId = EffectiveManifestApplicationId();
    info.targetDeviceFamilyName = EffectiveManifestTargetDeviceFamilyName();
    info.minVersion = EffectiveManifestMinVersion();
    info.maxVersionTested = EffectiveManifestMaxVersionTested();
    info.resourceLanguage = EffectiveManifestResourceLanguage();
    info.propertiesLogo = EffectiveManifestPropertiesLogo();
    info.square150x150Logo = EffectiveManifestSquare150x150Logo();
    info.square44x44Logo = EffectiveManifestSquare44x44Logo();
    info.square30x30Logo = EffectiveManifestSquare30x30Logo();
    info.square71x71Logo = EffectiveManifestSquare71x71Logo();
    info.wide310x150Logo = EffectiveManifestWide310x150Logo();
    info.square310x310Logo = EffectiveManifestSquare310x310Logo();
    info.backgroundColor = EffectiveManifestBackgroundColor();
    info.showNameOnTiles = ManifestSetting(L"ShowNameOnTiles", L"square150x150Logo,wide310x150Logo,square310x310Logo", L"ManifestShowNameOnTiles");
    info.entryPoint = EffectiveManifestEntryPoint();
    info.restrictedCapability = EffectiveManifestRestrictedCapability();
    info.splashScreenImage = EffectiveManifestSplashScreenImage();

    std::wstring manifestPath = GetExeDir() + L"\\AppxManifest.xml";
    DWORD attrs = GetFileAttributesW(manifestPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return info;

    std::wstring xml;
    if (!ReadTextFile(manifestPath, xml))
        return info;

    info.existing = true;
    if (xml.find(L"foundation/windows10") != std::wstring::npos || xml.find(L"<TargetDeviceFamily") != std::wstring::npos)
        info.manifestTarget = L"Windows 10";
    else
    {
        std::wstring osMin;
        if (ExtractXmlElementText(xml, L"OSMinVersion", osMin))
            info.manifestTarget = (osMin.rfind(L"6.2", 0) == 0) ? L"Windows 8" : L"Windows 8.1";
    }

    ExtractXmlAttributeFromTag(xml, L"<Identity", L"Name", info.identityName);
    ExtractXmlAttributeFromTag(xml, L"<Identity", L"Publisher", info.publisher);
    ExtractXmlAttributeFromTag(xml, L"<Identity", L"Version", info.packageVersion);
    ExtractXmlElementText(xml, L"DisplayName", info.displayName);
    ExtractXmlElementText(xml, L"PublisherDisplayName", info.publisherDisplayName);
    ExtractXmlAttributeFromTag(xml, L"<Application ", L"Executable", info.executable);
    ExtractXmlAttributeFromTag(xml, L"<Application ", L"Id", info.applicationId);
    ExtractXmlAttributeFromTag(xml, L"<Application ", L"EntryPoint", info.entryPoint);
    ExtractXmlAttributeFromTag(xml, L"<TargetDeviceFamily", L"Name", info.targetDeviceFamilyName);
    ExtractXmlAttributeFromTag(xml, L"<TargetDeviceFamily", L"MinVersion", info.minVersion);
    ExtractXmlAttributeFromTag(xml, L"<TargetDeviceFamily", L"MaxVersionTested", info.maxVersionTested);
    ExtractXmlElementText(xml, L"OSMinVersion", info.minVersion);
    ExtractXmlElementText(xml, L"OSMaxVersionTested", info.maxVersionTested);
    ExtractXmlAttributeFromTag(xml, L"<Resource", L"Language", info.resourceLanguage);
    ExtractXmlElementText(xml, L"Logo", info.propertiesLogo);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"Description", info.description);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"Square150x150Logo", info.square150x150Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"Square44x44Logo", info.square44x44Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"Square30x30Logo", info.square30x30Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"SmallLogo", info.square30x30Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"Logo", info.square150x150Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"BackgroundColor", info.backgroundColor);
    ExtractXmlAttributeFromTag(xml, L"<uap:DefaultTile", L"Square71x71Logo", info.square71x71Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:DefaultTile", L"Square70x70Logo", info.square71x71Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:DefaultTile", L"Wide310x150Logo", info.wide310x150Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:DefaultTile", L"WideLogo", info.wide310x150Logo);
    ExtractXmlAttributeFromTag(xml, L"<uap:DefaultTile", L"Square310x310Logo", info.square310x310Logo);
    ExtractXmlAttributeFromTag(xml, L"<rescap:Capability", L"Name", info.restrictedCapability);
    ExtractXmlAttributeFromTag(xml, L"<SplashScreen", L"Image", info.splashScreenImage);
    ExtractXmlShowNameOnTiles(xml, info.showNameOnTiles);
    return info;
}

static std::wstring BuildDefaultAppxManifest()
{
    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();

    std::wstring identity = XmlEscape(EffectiveManifestIdentityName());
    std::wstring publisher = XmlEscape(EffectiveManifestPublisher());
    std::wstring version = XmlEscape(EffectiveManifestPackageVersion());
    std::wstring displayName = XmlEscape(EffectiveManifestDisplayName());
    std::wstring publisherDisplayName = XmlEscape(EffectiveManifestPublisherDisplayName());
    std::wstring description = XmlEscape(EffectiveManifestDescription());
    std::wstring executable = XmlEscape(EffectiveManifestExecutable());
    std::wstring appId = XmlEscape(EffectiveManifestApplicationId());
    std::wstring targetDeviceFamilyName = XmlEscape(EffectiveManifestTargetDeviceFamilyName());
    std::wstring minVersion = XmlEscape(EffectiveManifestMinVersion());
    std::wstring maxVersionTested = XmlEscape(EffectiveManifestMaxVersionTested());
    std::wstring resourceLanguage = XmlEscape(EffectiveManifestResourceLanguage());
    std::wstring propertiesLogo = XmlEscape(EffectiveManifestPropertiesLogo());
    std::wstring square150x150Logo = XmlEscape(EffectiveManifestSquare150x150Logo());
    std::wstring square44x44Logo = XmlEscape(EffectiveManifestSquare44x44Logo());
    std::wstring square30x30Logo = XmlEscape(EffectiveManifestSquare30x30Logo());
    std::wstring square71x71Logo = XmlEscape(EffectiveManifestSquare71x71Logo());
    std::wstring wide310x150Logo = XmlEscape(EffectiveManifestWide310x150Logo());
    std::wstring square310x310Logo = XmlEscape(EffectiveManifestSquare310x310Logo());
    std::wstring backgroundColor = XmlEscape(EffectiveManifestBackgroundColor());
    std::wstring splashScreenImage = XmlEscape(EffectiveManifestSplashScreenImage());
    std::wstring entryPoint = XmlEscape(EffectiveManifestEntryPoint());
    std::wstring restrictedCapability = XmlEscape(EffectiveManifestRestrictedCapability());
    std::vector<std::wstring> showNameOnTiles = EffectiveManifestShowNameOnTiles();

    std::wstring manifest;
    manifest += L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";

    if (target == ManifestCompatibilityTarget::Windows10)
    {
        manifest += L"<Package\r\n";
        manifest += L"  xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"\r\n";
        manifest += L"  xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"\r\n";
        manifest += L"  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"\r\n";
        manifest += L"  IgnorableNamespaces=\"uap rescap\">\r\n\r\n";
        manifest += L"  <Identity\r\n";
        manifest += L"    Name=\"" + identity + L"\"\r\n";
        manifest += L"    Publisher=\"" + publisher + L"\"\r\n";
        manifest += L"    Version=\"" + version + L"\" />\r\n\r\n";
        manifest += L"  <Properties>\r\n";
        manifest += L"    <DisplayName>" + displayName + L"</DisplayName>\r\n";
        manifest += L"    <PublisherDisplayName>" + publisherDisplayName + L"</PublisherDisplayName>\r\n";
        manifest += L"    <Logo>" + propertiesLogo + L"</Logo>\r\n";
        manifest += L"  </Properties>\r\n\r\n";
        manifest += L"  <Dependencies>\r\n";
        manifest += L"    <TargetDeviceFamily Name=\"" + targetDeviceFamilyName + L"\"\r\n";
        manifest += L"                        MinVersion=\"" + minVersion + L"\"\r\n";
        manifest += L"                        MaxVersionTested=\"" + maxVersionTested + L"\" />\r\n";
        manifest += L"  </Dependencies>\r\n\r\n";
        manifest += L"  <Resources>\r\n";
        manifest += L"    <Resource Language=\"" + resourceLanguage + L"\" />\r\n";
        manifest += L"  </Resources>\r\n\r\n";
        manifest += L"  <Applications>\r\n";
        manifest += L"    <Application Id=\"" + appId + L"\"\r\n";
        manifest += L"                 Executable=\"" + executable + L"\"\r\n";
        manifest += L"                 EntryPoint=\"" + entryPoint + L"\">\r\n\r\n";
        manifest += L"      <uap:VisualElements\r\n";
        manifest += L"        DisplayName=\"" + displayName + L"\"\r\n";
        manifest += L"        Description=\"" + description + L"\"\r\n";
        manifest += L"        Square150x150Logo=\"" + square150x150Logo + L"\"\r\n";
        manifest += L"        Square44x44Logo=\"" + square44x44Logo + L"\"\r\n";
        manifest += L"        BackgroundColor=\"" + backgroundColor + L"\">\r\n\r\n";
        manifest += L"        <uap:DefaultTile\r\n";
        manifest += L"          Square71x71Logo=\"" + square71x71Logo + L"\"\r\n";
        manifest += L"          Wide310x150Logo=\"" + wide310x150Logo + L"\"\r\n";
        manifest += L"          Square310x310Logo=\"" + square310x310Logo + L"\">\r\n\r\n";
        if (!showNameOnTiles.empty())
        {
            manifest += L"          <uap:ShowNameOnTiles>\r\n";
            for (const std::wstring& tile : showNameOnTiles)
                manifest += L"            <uap:ShowOn Tile=\"" + XmlEscape(tile) + L"\" />\r\n";
            manifest += L"          </uap:ShowNameOnTiles>\r\n\r\n";
        }
        manifest += L"        </uap:DefaultTile>\r\n\r\n";
        manifest += L"      </uap:VisualElements>\r\n";
        manifest += L"    </Application>\r\n";
        manifest += L"  </Applications>\r\n\r\n";
        if (!restrictedCapability.empty())
        {
            manifest += L"  <Capabilities>\r\n";
            manifest += L"    <rescap:Capability Name=\"" + restrictedCapability + L"\" />\r\n";
            manifest += L"  </Capabilities>\r\n\r\n";
        }
        manifest += L"</Package>\r\n";
        return manifest;
    }

    if (target == ManifestCompatibilityTarget::Windows81)
    {
        manifest += L"<Package xmlns=\"http://schemas.microsoft.com/appx/2010/manifest\" xmlns:m2=\"http://schemas.microsoft.com/appx/2013/manifest\">\r\n\r\n";
        manifest += L"  <Identity Name=\"" + identity + L"\" Publisher=\"" + publisher + L"\" Version=\"" + version + L"\" ProcessorArchitecture=\"x64\" />\r\n\r\n";
        manifest += L"  <Properties>\r\n";
        manifest += L"    <DisplayName>" + displayName + L"</DisplayName>\r\n";
        manifest += L"    <PublisherDisplayName>" + publisherDisplayName + L"</PublisherDisplayName>\r\n";
        manifest += L"    <Logo>" + propertiesLogo + L"</Logo>\r\n";
        manifest += L"  </Properties>\r\n\r\n";
        manifest += L"  <Prerequisites>\r\n";
        manifest += L"    <OSMinVersion>" + minVersion + L"</OSMinVersion>\r\n";
        manifest += L"    <OSMaxVersionTested>" + maxVersionTested + L"</OSMaxVersionTested>\r\n";
        manifest += L"  </Prerequisites>\r\n\r\n";
        manifest += L"  <Resources>\r\n";
        manifest += L"    <Resource m2:Scale=\"100\" />\r\n";
        manifest += L"    <Resource Language=\"" + resourceLanguage + L"\" />\r\n";
        manifest += L"  </Resources>\r\n\r\n";
        manifest += L"  <Applications>\r\n";
        manifest += L"    <Application Id=\"" + appId + L"\" Executable=\"" + executable + L"\" EntryPoint=\"" + entryPoint + L"\">\r\n";
        manifest += L"      <m2:VisualElements DisplayName=\"" + displayName + L"\" Square150x150Logo=\"" + square150x150Logo + L"\" Square30x30Logo=\"" + square30x30Logo + L"\" Description=\"" + description + L"\" ForegroundText=\"light\" BackgroundColor=\"" + backgroundColor + L"\" ToastCapable=\"true\">\r\n";
        manifest += L"        <m2:DefaultTile Square70x70Logo=\"" + square71x71Logo + L"\" Wide310x150Logo=\"" + wide310x150Logo + L"\" Square310x310Logo=\"" + square310x310Logo + L"\" DefaultSize=\"square150x150Logo\">\r\n";
        if (!showNameOnTiles.empty())
        {
            manifest += L"          <m2:ShowNameOnTiles>\r\n";
            for (const std::wstring& tile : showNameOnTiles)
                manifest += L"            <m2:ShowOn Tile=\"" + XmlEscape(tile) + L"\" />\r\n";
            manifest += L"          </m2:ShowNameOnTiles>\r\n";
        }
        manifest += L"        </m2:DefaultTile>\r\n";
        manifest += L"        <m2:SplashScreen Image=\"" + splashScreenImage + L"\" />\r\n";
        manifest += L"        <m2:ApplicationView MinWidth=\"width320\" />\r\n";
        manifest += L"      </m2:VisualElements>\r\n";
        if (Win8LiveTileBackgroundTaskEnabled())
        {
            manifest += L"      <Extensions>\r\n";
            manifest += L"        <Extension Category=\"windows.backgroundTasks\" EntryPoint=\"" + ManifestLiveTileBackgroundTaskClassId() + L"\">\r\n";
            manifest += L"          <BackgroundTasks>\r\n";
            manifest += L"            <Task Type=\"systemEvent\" />\r\n";
            manifest += L"          </BackgroundTasks>\r\n";
            manifest += L"        </Extension>\r\n";
            manifest += L"      </Extensions>\r\n";
        }
        manifest += L"    </Application>\r\n";
        manifest += L"  </Applications>\r\n\r\n";
        if (Win8LiveTileBackgroundTaskEnabled() || Win8LiveTileOopHelperEnabled())
        {
            manifest += L"  <Extensions>\r\n";
            if (Win8LiveTileBackgroundTaskEnabled())
            {
                manifest += L"    <Extension Category=\"windows.activatableClass.inProcessServer\">\r\n";
                manifest += L"      <InProcessServer>\r\n";
                manifest += L"        <Path>" + ManifestLiveTileBackgroundTaskDllName() + L"</Path>\r\n";
                manifest += L"        <ActivatableClass ActivatableClassId=\"" + ManifestLiveTileBackgroundTaskClassId() + L"\" ThreadingModel=\"both\" />\r\n";
                manifest += L"      </InProcessServer>\r\n";
                manifest += L"    </Extension>\r\n";
            }
            if (Win8LiveTileOopHelperEnabled())
            {
                manifest += L"    <Extension Category=\"windows.activatableClass.outOfProcessServer\">\r\n";
                manifest += L"      <OutOfProcessServer ServerName=\"" + ManifestLiveTileOopServerName() + L"\">\r\n";
                manifest += L"        <Path>" + executable + L"</Path>\r\n";
                manifest += L"        <Instancing>singleInstance</Instancing>\r\n";
                manifest += L"        <ActivatableClass ActivatableClassId=\"" + ManifestLiveTileOopRuntimeClassId() + L"\" />\r\n";
                manifest += L"      </OutOfProcessServer>\r\n";
                manifest += L"    </Extension>\r\n";
            }
            manifest += L"  </Extensions>\r\n";
        }
        manifest += L"</Package>\r\n";
        return manifest;
    }

    manifest += L"<Package xmlns=\"http://schemas.microsoft.com/appx/2010/manifest\">\r\n\r\n";
    manifest += L"  <Identity Name=\"" + identity + L"\" Publisher=\"" + publisher + L"\" Version=\"" + version + L"\" ProcessorArchitecture=\"x64\" />\r\n\r\n";
    manifest += L"  <Properties>\r\n";
    manifest += L"    <DisplayName>" + displayName + L"</DisplayName>\r\n";
    manifest += L"    <PublisherDisplayName>" + publisherDisplayName + L"</PublisherDisplayName>\r\n";
    manifest += L"    <Logo>" + propertiesLogo + L"</Logo>\r\n";
    manifest += L"  </Properties>\r\n\r\n";
    manifest += L"  <Prerequisites>\r\n";
    manifest += L"    <OSMinVersion>" + minVersion + L"</OSMinVersion>\r\n";
    manifest += L"    <OSMaxVersionTested>" + maxVersionTested + L"</OSMaxVersionTested>\r\n";
    manifest += L"  </Prerequisites>\r\n\r\n";
    manifest += L"  <Resources>\r\n";
    manifest += L"    <Resource Language=\"" + resourceLanguage + L"\" />\r\n";
    manifest += L"  </Resources>\r\n\r\n";
    manifest += L"  <Applications>\r\n";
    manifest += L"    <Application Id=\"" + appId + L"\" Executable=\"" + executable + L"\" EntryPoint=\"" + entryPoint + L"\">\r\n";
    manifest += L"      <VisualElements DisplayName=\"" + displayName + L"\" Logo=\"" + square150x150Logo + L"\" SmallLogo=\"" + square30x30Logo + L"\" Description=\"" + description + L"\" ForegroundText=\"light\" BackgroundColor=\"" + backgroundColor + L"\" ToastCapable=\"true\">\r\n";
    manifest += L"        <DefaultTile WideLogo=\"" + wide310x150Logo + L"\" ShowName=\"allLogos\" />\r\n";
    manifest += L"        <SplashScreen Image=\"" + splashScreenImage + L"\" />\r\n";
    manifest += L"      </VisualElements>\r\n";
    if (Win8LiveTileBackgroundTaskEnabled())
    {
        manifest += L"      <Extensions>\r\n";
        manifest += L"        <Extension Category=\"windows.backgroundTasks\" EntryPoint=\"" + ManifestLiveTileBackgroundTaskClassId() + L"\">\r\n";
        manifest += L"          <BackgroundTasks>\r\n";
        manifest += L"            <Task Type=\"systemEvent\" />\r\n";
        manifest += L"          </BackgroundTasks>\r\n";
        manifest += L"        </Extension>\r\n";
        manifest += L"      </Extensions>\r\n";
    }
    manifest += L"    </Application>\r\n";
    manifest += L"  </Applications>\r\n\r\n";
    if (Win8LiveTileBackgroundTaskEnabled() || Win8LiveTileOopHelperEnabled())
    {
        manifest += L"  <Extensions>\r\n";
        if (Win8LiveTileBackgroundTaskEnabled())
        {
            manifest += L"    <Extension Category=\"windows.activatableClass.inProcessServer\">\r\n";
            manifest += L"      <InProcessServer>\r\n";
            manifest += L"        <Path>" + ManifestLiveTileBackgroundTaskDllName() + L"</Path>\r\n";
            manifest += L"        <ActivatableClass ActivatableClassId=\"" + ManifestLiveTileBackgroundTaskClassId() + L"\" ThreadingModel=\"both\" />\r\n";
            manifest += L"      </InProcessServer>\r\n";
            manifest += L"    </Extension>\r\n";
        }
        if (Win8LiveTileOopHelperEnabled())
        {
            manifest += L"    <Extension Category=\"windows.activatableClass.outOfProcessServer\">\r\n";
            manifest += L"      <OutOfProcessServer ServerName=\"" + ManifestLiveTileOopServerName() + L"\">\r\n";
            manifest += L"        <Path>" + executable + L"</Path>\r\n";
            manifest += L"        <Instancing>singleInstance</Instancing>\r\n";
            manifest += L"        <ActivatableClass ActivatableClassId=\"" + ManifestLiveTileOopRuntimeClassId() + L"\" />\r\n";
            manifest += L"      </OutOfProcessServer>\r\n";
            manifest += L"    </Extension>\r\n";
        }
        manifest += L"  </Extensions>\r\n";
    }
    manifest += L"</Package>\r\n";
    return manifest;
}

static bool EnsureAppxActivationStubExecutable(DWORD& error)
{
    error = ERROR_SUCCESS;

    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows10)
        return true;

    std::wstring executable = EffectiveManifestExecutable();
    if (IEquals(executable, ManifestHostExecutableName()))
        return true;

    std::wstring dir = GetExeDir();
    if (dir.empty() || g_exePath.empty())
    {
        error = ERROR_INVALID_PARAMETER;
        SetLastError(error);
        return false;
    }

    std::wstring targetPath = dir + L"\\" + executable;
    if (IEquals(targetPath, g_exePath))
        return true;

    if (IEquals(executable, ManifestLiveTileBrokerExecutableName()))
    {
        DWORD attrs = GetFileAttributesW(targetPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            return true;
        error = ERROR_FILE_NOT_FOUND;
        SetLastError(error);
        return false;
    }

    if (CopyFileW(g_exePath.c_str(), targetPath.c_str(), FALSE))
        return true;

    error = GetLastError();
    if (error == ERROR_SUCCESS)
        error = ERROR_WRITE_FAULT;
    return false;
}

static bool RegenerateAppxManifestFromConfig(std::wstring& manifestPath, DWORD& error)
{
    manifestPath = GetExeDir() + L"\\AppxManifest.xml";
    error = ERROR_SUCCESS;

    if (!EnsureAppxActivationStubExecutable(error))
        return false;

    if (WriteUtf8BomTextFile(manifestPath, BuildDefaultAppxManifest()))
        return true;

    error = GetLastError();
    if (error == ERROR_SUCCESS)
        error = ERROR_WRITE_FAULT;
    return false;
}

static bool RegenerateAppxManifestFromConfigAndLog()
{
    std::wstring manifestPath;
    DWORD manifestError = ERROR_SUCCESS;
    if (RegenerateAppxManifestFromConfig(manifestPath, manifestError))
    {
        Log(g_ui.appxManifestRegenerated.c_str(), manifestPath.c_str());
        return true;
    }

    Log(g_ui.appxManifestRegenerateFailed.c_str(), manifestPath.c_str());
    Log(g_ui.win32FailureFmt.c_str(), L"Regenerate AppxManifest.xml",
        manifestError, Win32ErrorString(manifestError).c_str());
    return false;
}

static bool EnsureAppxManifest(std::wstring& manifestPath, bool& created, DWORD& error)
{
    manifestPath = GetExeDir() + L"\\AppxManifest.xml";
    created = false;
    error = ERROR_SUCCESS;

    DWORD attrs = GetFileAttributesW(manifestPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            std::wstring xml;
            bool readExistingXml = ReadTextFile(manifestPath, xml);

            std::wstring existingExecutable;
            std::wstring expectedExecutable = EffectiveManifestExecutable();
            bool executableMismatch =
                readExistingXml &&
                ExtractXmlAttributeFromTag(xml, L"<Application ", L"Executable", existingExecutable) &&
                !IEquals(existingExecutable, expectedExecutable);

            std::wstring existingIdentityName;
            std::wstring expectedIdentityName = EffectiveManifestIdentityName();
            bool identityNameMismatch =
                readExistingXml &&
                ExtractXmlAttributeFromTag(xml, L"<Identity ", L"Name", existingIdentityName) &&
                !IEquals(existingIdentityName, expectedIdentityName);

            if (!EnsureAppxActivationStubExecutable(error))
                return false;

            if (executableMismatch || identityNameMismatch)
            {
                Log(L"[i] Existing AppxManifest.xml no longer matches the current EXE name or manifest defaults. Regenerating manifest.");
                if (WriteUtf8BomTextFile(manifestPath, BuildDefaultAppxManifest()))
                {
                    created = true;
                    return true;
                }

                error = GetLastError();
                if (error == ERROR_SUCCESS)
                    error = ERROR_WRITE_FAULT;
                return false;
            }

            Log(g_ui.appxManifestExisting.c_str(), manifestPath.c_str());
            return true;
        }
        error = ERROR_ALREADY_EXISTS;
        SetLastError(error);
        return false;
    }

    if (!EnsureAppxActivationStubExecutable(error))
        return false;

    if (WriteUtf8BomTextFile(manifestPath, BuildDefaultAppxManifest()))
    {
        created = true;
        return true;
    }

    error = GetLastError();
    if (error == ERROR_SUCCESS)
        error = ERROR_WRITE_FAULT;
    return false;
}


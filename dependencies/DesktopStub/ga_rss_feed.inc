// RSS/Atom feed content source for Live Tile updates.
//
// Ported from RssLiveTile (see legacy/RssLiveTile) now that a renamed,
// separately-configured copy of DesktopStub can register its own AppX
// package identity (ManifestDefaultIdentityForExecutable) and run
// standalone (ConfigureSingleInstanceIdentity), giving it its own Start
// tile the same way a dedicated RssLiveTile.exe used to. Reuses this
// repo's shared helpers (aip::XmlEscape, aip::QuoteCommandLineArg,
// aip::Trim, aip::DecodeCodePageText) instead of re-porting them a third
// time; only genuinely RSS-specific logic lives in this file.
//
// Everything in this file is inert unless ConfiguredContentSource() ==
// ContentSource::RssFeed, so it has no effect on the existing wallpaper
// path.

struct RssFeedItem
{
    std::wstring title;
    std::wstring summary;
    std::wstring link;
    std::wstring date;
};

struct RssFeedSnapshot
{
    bool ok = false;
    std::wstring sourceTitle;
    std::wstring status;
    std::vector<RssFeedItem> items;
};

struct RssFeedSettings
{
    std::wstring feedUrl;
    std::wstring userAgent = L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)";
    int updateIntervalSeconds = 900;
    int maxItems = 5;
    int httpTimeoutSeconds = 20;
    size_t maxFeedBytes = 2 * 1024 * 1024;
};

// ---------------------------------------------------------------------------
// Small text helpers
// ---------------------------------------------------------------------------

static std::wstring RssCollapseWhitespace(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    bool inWhitespace = false;
    for (wchar_t ch : value)
    {
        if (iswspace(ch))
        {
            if (!out.empty())
                inWhitespace = true;
            continue;
        }
        if (inWhitespace && !out.empty())
            out.push_back(L' ');
        out.push_back(ch);
        inWhitespace = false;
    }
    return aip::Trim(out);
}

static std::wstring RssStripHtmlTags(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    bool inTag = false;
    for (wchar_t ch : value)
    {
        if (ch == L'<') { inTag = true; out.push_back(L' '); continue; }
        if (ch == L'>') { inTag = false; out.push_back(L' '); continue; }
        if (!inTag)
            out.push_back(ch);
    }
    return RssCollapseWhitespace(out);
}

static std::wstring RssLimitText(std::wstring value, size_t maxChars)
{
    value = RssCollapseWhitespace(value);
    if (value.size() <= maxChars)
        return value;
    if (maxChars <= 3)
        return value.substr(0, maxChars);
    value.resize(maxChars - 3);
    while (!value.empty() && iswspace(value.back()))
        value.pop_back();
    value += L"...";
    return value;
}

static bool RssValidateFeedUrl(const std::wstring& value, std::wstring& error)
{
    std::wstring trimmed = aip::Trim(value);
    if (trimmed.empty())
    {
        error = L"FeedUrl cannot be empty.";
        return false;
    }

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(trimmed.c_str(), 0, 0, &components) ||
        (components.nScheme != INTERNET_SCHEME_HTTP &&
            components.nScheme != INTERNET_SCHEME_HTTPS) ||
        components.dwHostNameLength == 0)
    {
        error = L"FeedUrl must be a valid http or https URL.";
        return false;
    }
    return true;
}

static bool RssIsHttpUrl(const std::wstring& value)
{
    std::wstring error;
    return RssValidateFeedUrl(value, error);
}

static std::wstring RssLocalXmlName(const std::wstring& name)
{
    size_t colon = name.rfind(L':');
    std::wstring local = colon == std::wstring::npos ? name : name.substr(colon + 1);
    return aip::ToLower(local);
}

static bool RssNameMatches(const std::wstring& localName, std::initializer_list<const wchar_t*> names)
{
    for (const wchar_t* name : names)
    {
        if (localName == name)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Charset-aware byte decoding
// ---------------------------------------------------------------------------

static UINT RssCharsetCodePage(const std::wstring& charset)
{
    std::wstring c = aip::ToLower(aip::Trim(charset));
    if (c.empty()) return 0;
    if (c == L"utf-8" || c == L"utf8") return CP_UTF8;
    if (c == L"us-ascii" || c == L"ascii") return 20127;
    if (c == L"iso-8859-1" || c == L"latin1") return 28591;
    if (c == L"windows-1252" || c == L"cp1252") return 1252;
    return 0;
}

static std::wstring RssExtractCharset(const std::wstring& value)
{
    std::wstring lower = aip::ToLower(value);
    size_t pos = lower.find(L"charset=");
    if (pos == std::wstring::npos)
        return L"";
    pos += 8;
    size_t end = pos;
    while (end < value.size() && value[end] != L';' && !iswspace(value[end]))
        ++end;
    std::wstring charset = value.substr(pos, end - pos);
    if (charset.size() >= 2 && charset.front() == L'"' && charset.back() == L'"')
        charset = charset.substr(1, charset.size() - 2);
    return aip::Trim(charset);
}

static std::wstring RssXmlDeclaredEncoding(const std::vector<BYTE>& bytes)
{
    size_t scanLen = (std::min)(bytes.size(), static_cast<size_t>(256));
    std::string head(reinterpret_cast<const char*>(bytes.data()), scanLen);
    size_t declStart = head.find("<?xml");
    if (declStart == std::string::npos)
        return L"";
    size_t declEnd = head.find("?>", declStart);
    if (declEnd == std::string::npos)
        return L"";
    std::string decl = head.substr(declStart, declEnd - declStart);
    size_t encPos = decl.find("encoding");
    if (encPos == std::string::npos)
        return L"";
    size_t quoteStart = decl.find_first_of("\"'", encPos);
    if (quoteStart == std::string::npos)
        return L"";
    char quoteChar = decl[quoteStart];
    size_t quoteEnd = decl.find(quoteChar, quoteStart + 1);
    if (quoteEnd == std::string::npos)
        return L"";
    std::string enc = decl.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    return std::wstring(enc.begin(), enc.end());
}

static bool RssDecodeFeedBytes(
    const std::vector<BYTE>& bytes,
    const std::wstring& contentTypeHeader,
    std::wstring& text)
{
    text.clear();
    if (bytes.empty())
        return true;

    UINT codePage = RssCharsetCodePage(RssExtractCharset(contentTypeHeader));
    if (codePage == 0)
        codePage = RssCharsetCodePage(RssXmlDeclaredEncoding(bytes));

    if (codePage != 0 && aip::DecodeCodePageText(codePage, bytes.data(), bytes.size(), text, 0))
        return true;
    if (aip::DecodeCodePageText(CP_UTF8, bytes.data(), bytes.size(), text, 0))
        return true;
    return aip::DecodeCodePageText(1252, bytes.data(), bytes.size(), text, 0);
}

static RssFeedSettings ReadRssFeedSettings()
{
    RssFeedSettings settings;
    settings.feedUrl = aip::Trim(IniReadS(L"RssFeed", L"FeedUrl", L""));
    std::wstring ua = aip::Trim(IniReadS(L"RssFeed", L"UserAgent", L""));
    if (!ua.empty())
        settings.userAgent = ua;
    settings.updateIntervalSeconds = IniReadClampedI(L"RssFeed", L"UpdateIntervalSeconds", 900, 60, 86400);
    settings.maxItems = IniReadClampedI(L"RssFeed", L"MaxItems", 5, 1, 20);
    settings.httpTimeoutSeconds = IniReadClampedI(L"RssFeed", L"HttpTimeoutSeconds", 20, 5, 120);
    settings.maxFeedBytes = static_cast<size_t>(IniReadClampedI(L"RssFeed", L"MaxFeedBytes", 2 * 1024 * 1024, 64 * 1024, 16 * 1024 * 1024));
    return settings;
}

// ---------------------------------------------------------------------------
// WinHTTP fetch
// ---------------------------------------------------------------------------

static bool RssHttpGetText(
    const RssFeedSettings& settings,
    std::wstring& text,
    std::wstring& error)
{
    text.clear();
    error.clear();

    if (settings.feedUrl.empty())
    {
        error = L"FeedUrl is not configured.";
        return false;
    }
    if (!RssValidateFeedUrl(settings.feedUrl, error))
        return false;

    URL_COMPONENTSW components{};
    wchar_t hostBuf[256]{};
    wchar_t pathBuf[2048]{};
    components.dwStructSize = sizeof(components);
    components.lpszHostName = hostBuf;
    components.dwHostNameLength = ARRAYSIZE(hostBuf);
    components.lpszUrlPath = pathBuf;
    components.dwUrlPathLength = ARRAYSIZE(pathBuf);
    if (!WinHttpCrackUrl(settings.feedUrl.c_str(), 0, 0, &components))
    {
        error = L"WinHttpCrackUrl failed: " + aip::GetLastErrorText(GetLastError());
        return false;
    }
    bool isHttps = components.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring host(hostBuf);
    std::wstring path(pathBuf);
    if (path.empty())
        path = L"/";

    HINTERNET session = WinHttpOpen(
        settings.userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session)
    {
        error = L"WinHttpOpen failed: " + aip::GetLastErrorText(GetLastError());
        return false;
    }

    DWORD timeoutMs = static_cast<DWORD>(settings.httpTimeoutSeconds) * 1000;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    bool ok = false;
    std::wstring contentType;
    std::vector<BYTE> body;

    auto fail = [&](const std::wstring& message)
    {
        error = message;
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    };

    connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (!connection)
        return fail(L"WinHttpConnect failed: " + aip::GetLastErrorText(GetLastError()));

    request = WinHttpOpenRequest(
        connection,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        isHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!request)
        return fail(L"WinHttpOpenRequest failed: " + aip::GetLastErrorText(GetLastError()));

    static const wchar_t* acceptHeader = L"Accept: application/rss+xml, application/atom+xml, application/xml, text/xml, */*\r\n";
    WinHttpAddRequestHeaders(request, acceptHeader, static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return fail(L"WinHttpSendRequest failed: " + aip::GetLastErrorText(GetLastError()));
    if (!WinHttpReceiveResponse(request, nullptr))
        return fail(L"WinHttpReceiveResponse failed: " + aip::GetLastErrorText(GetLastError()));

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300)
        return fail(L"HTTP request failed with status " + std::to_wstring(statusCode) + L".");

    wchar_t contentTypeBuf[256]{};
    DWORD contentTypeSize = sizeof(contentTypeBuf);
    if (WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CONTENT_TYPE,
            WINHTTP_HEADER_NAME_BY_INDEX,
            contentTypeBuf,
            &contentTypeSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        contentType = contentTypeBuf;
    }

    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            return fail(L"WinHttpQueryDataAvailable failed: " + aip::GetLastErrorText(GetLastError()));
        if (available == 0)
            break;

        size_t remaining = settings.maxFeedBytes > body.size() ? settings.maxFeedBytes - body.size() : 0;
        if (remaining == 0)
            return fail(L"Feed response exceeded the configured MaxFeedBytes limit.");

        DWORD toRead = static_cast<DWORD>((std::min)(static_cast<size_t>(available), remaining));
        std::vector<BYTE> chunk(toRead);
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), toRead, &read))
            return fail(L"WinHttpReadData failed: " + aip::GetLastErrorText(GetLastError()));
        if (read == 0)
            break;
        body.insert(body.end(), chunk.begin(), chunk.begin() + read);
    }

    ok = RssDecodeFeedBytes(body, contentType, text);
    if (!ok)
        error = L"Feed response could not be decoded as text.";

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// ---------------------------------------------------------------------------
// WinRT XML DOM parsing
// ---------------------------------------------------------------------------

static std::wstring RssChildText(
    const winrt::Windows::Data::Xml::Dom::IXmlNode& node,
    std::initializer_list<const wchar_t*> names)
{
    auto children = node.ChildNodes();
    for (uint32_t i = 0; i < children.Length(); ++i)
    {
        auto child = children.Item(i);
        std::wstring local = RssLocalXmlName(child.NodeName().c_str());
        if (RssNameMatches(local, names))
            return RssCollapseWhitespace(child.InnerText().c_str());
    }
    return L"";
}

static std::wstring RssChildHtmlText(
    const winrt::Windows::Data::Xml::Dom::IXmlNode& node,
    std::initializer_list<const wchar_t*> names)
{
    return RssStripHtmlTags(RssChildText(node, names));
}

static std::wstring RssChildLink(const winrt::Windows::Data::Xml::Dom::IXmlNode& node)
{
    auto children = node.ChildNodes();
    std::wstring plainLink;
    for (uint32_t i = 0; i < children.Length(); ++i)
    {
        auto child = children.Item(i);
        std::wstring local = RssLocalXmlName(child.NodeName().c_str());
        if (local == L"link")
        {
            auto element = child.try_as<winrt::Windows::Data::Xml::Dom::IXmlElement>();
            if (element)
            {
                std::wstring href = element.GetAttribute(L"href").c_str();
                std::wstring rel = element.GetAttribute(L"rel").c_str();
                if (!href.empty() && (rel.empty() || rel == L"alternate"))
                    return aip::Trim(href);
            }
            std::wstring text = RssCollapseWhitespace(child.InnerText().c_str());
            if (!text.empty() && plainLink.empty())
                plainLink = text;
        }
    }
    return plainLink;
}

static std::vector<winrt::Windows::Data::Xml::Dom::IXmlNode> RssFindDescendantsByLocalName(
    const winrt::Windows::Data::Xml::Dom::IXmlNode& root,
    std::initializer_list<const wchar_t*> names,
    size_t maxResults)
{
    std::vector<winrt::Windows::Data::Xml::Dom::IXmlNode> matches;
    if (root == nullptr)
        return matches;

    std::vector<winrt::Windows::Data::Xml::Dom::IXmlNode> pending;
    pending.push_back(root);
    while (!pending.empty() && (maxResults == 0 || matches.size() < maxResults))
    {
        auto node = pending.front();
        pending.erase(pending.begin());

        auto children = node.ChildNodes();
        for (uint32_t i = 0; i < children.Length(); ++i)
        {
            auto child = children.Item(i);
            std::wstring local = RssLocalXmlName(child.NodeName().c_str());
            if (RssNameMatches(local, names))
            {
                matches.push_back(child);
                if (maxResults != 0 && matches.size() >= maxResults)
                    break;
            }
            else
            {
                pending.push_back(child);
            }
        }
    }
    return matches;
}

static RssFeedSnapshot RssParseFeedXml(const std::wstring& xml, const RssFeedSettings& settings)
{
    RssFeedSnapshot snapshot;

    try
    {
        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);
        using namespace winrt::Windows::Data::Xml::Dom;

        XmlDocument doc;
        XmlLoadSettings loadSettings;
        loadSettings.ProhibitDtd(true);
        loadSettings.ResolveExternals(false);
        loadSettings.ElementContentWhiteSpace(false);
        loadSettings.MaxElementDepth(64);
        doc.LoadXml(winrt::hstring(xml), loadSettings);

        auto root = doc.DocumentElement();
        auto channels = RssFindDescendantsByLocalName(root, { L"channel" }, 1);
        if (!channels.empty())
            snapshot.sourceTitle = RssChildText(channels.front(), { L"title" });
        if (snapshot.sourceTitle.empty() && root != nullptr)
            snapshot.sourceTitle = RssChildText(root, { L"title" });
        if (snapshot.sourceTitle.empty())
            snapshot.sourceTitle = L"RSS";

        auto items = RssFindDescendantsByLocalName(
            root,
            { L"item", L"entry" },
            static_cast<size_t>(settings.maxItems));
        for (const auto& node : items)
        {
            RssFeedItem item;
            item.title = RssLimitText(RssChildText(node, { L"title" }), 180);
            item.summary = RssLimitText(RssChildHtmlText(node, { L"description", L"summary", L"content" }), 240);
            item.date = RssLimitText(RssChildText(node, { L"pubdate", L"published", L"updated", L"date" }), 80);
            item.link = RssChildLink(node);

            if (item.title.empty())
                item.title = item.summary.empty() ? L"(untitled)" : RssLimitText(item.summary, 120);
            if (item.summary.empty())
                item.summary = item.date.empty() ? snapshot.sourceTitle : item.date;
            snapshot.items.push_back(std::move(item));
        }

        if (snapshot.items.empty())
        {
            snapshot.status = L"No RSS or Atom entries were found.";
            return snapshot;
        }

        snapshot.ok = true;
        snapshot.status = L"Loaded " + std::to_wstring(snapshot.items.size()) + L" item(s) from " + snapshot.sourceTitle + L".";
        return snapshot;
    }
    catch (const winrt::hresult_error& ex)
    {
        snapshot.status = FormatWide(L"Feed XML parse failed: 0x%08X %s", static_cast<unsigned int>(ex.code().value), ex.message().c_str());
        return snapshot;
    }
    catch (...)
    {
        snapshot.status = L"Feed XML parse failed with an unknown exception.";
        return snapshot;
    }
}

static RssFeedSnapshot ReadRssFeedSnapshot(const RssFeedSettings& settings)
{
    std::wstring xml;
    std::wstring error;
    if (!RssHttpGetText(settings, xml, error))
    {
        RssFeedSnapshot snapshot;
        snapshot.status = error;
        return snapshot;
    }
    return RssParseFeedXml(xml, settings);
}

// ---------------------------------------------------------------------------
// Tile XML generation (text-bound, unlike the wallpaper path's image-bound
// BuildLiveTileXml in ga_live_tile.inc -- these are independent generators
// feeding the same OS tile-update surface)
// ---------------------------------------------------------------------------

static std::wstring BuildRssLiveTileXml(const RssFeedSnapshot& snapshot, const RssFeedItem& item)
{
    std::wstring source = aip::XmlEscape(RssLimitText(snapshot.sourceTitle, 80));
    std::wstring title = aip::XmlEscape(RssLimitText(item.title, 160));
    std::wstring summary = aip::XmlEscape(RssLimitText(item.summary, 220));
    std::wstring date = aip::XmlEscape(RssLimitText(item.date, 80));
    std::wstring activationArguments;
    if (RssIsHttpUrl(item.link))
        activationArguments = aip::XmlEscape(L"--open-url " + aip::QuoteCommandLineArg(item.link));

    std::wstring xml;
    xml += L"<tile>\r\n";
    xml += L"  <visual branding=\"nameAndLogo\"";
    if (!activationArguments.empty())
        xml += L" arguments=\"" + activationArguments + L"\"";
    xml += L">\r\n";
    xml += L"    <binding template=\"TileMedium\">\r\n";
    xml += L"      <text hint-style=\"captionSubtle\">" + source + L"</text>\r\n";
    xml += L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + title + L"</text>\r\n";
    if (!date.empty())
        xml += L"      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + date + L"</text>\r\n";
    xml += L"    </binding>\r\n";
    xml += L"    <binding template=\"TileWide\">\r\n";
    xml += L"      <text hint-style=\"captionSubtle\">" + source + L"</text>\r\n";
    xml += L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>\r\n";
    xml += L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + summary + L"</text>\r\n";
    xml += L"    </binding>\r\n";
    xml += L"    <binding template=\"TileLarge\">\r\n";
    xml += L"      <text hint-style=\"captionSubtle\">" + source + L"</text>\r\n";
    xml += L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>\r\n";
    xml += L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + summary + L"</text>\r\n";
    if (!date.empty())
        xml += L"      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + date + L"</text>\r\n";
    xml += L"    </binding>\r\n";
    xml += L"  </visual>\r\n";
    xml += L"</tile>";
    return xml;
}

// ---------------------------------------------------------------------------
// Delivery: a queue-enabled sibling of ga_live_tile.inc's
// Appx_Update_LiveTileXmlBare. That function deliberately disables the
// notification queue (single wallpaper-derived tile, one binding set).
// RSS mode wants Windows to rotate through several items on its own timer,
// which needs the queue enabled and multiple notifications pushed -- a
// real behavioral difference, not just a content difference, so it gets
// its own bare primitive rather than overloading the existing one.
// ---------------------------------------------------------------------------

static bool Appx_Update_LiveTileQueueBare(const std::vector<std::wstring>& tileXmls, std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring packageFullName;
    if (!CurrentProcessPackageFullName(packageFullName))
    {
        failureMessage = FormatWide(L"Live Tile update requires package identity (error 0x%08lX). Launch normally so the app can register and relaunch the packaged entry.", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    if (tileXmls.empty())
    {
        failureMessage = L"No tile content to display.";
        return false;
    }

    try
    {
        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);
        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;

        std::vector<TileNotification> notifications;
        notifications.reserve(tileXmls.size());
        for (const std::wstring& tileXml : tileXmls)
        {
            if (aip::Trim(tileXml).empty())
                continue;
            XmlDocument doc;
            doc.LoadXml(winrt::hstring(tileXml));
            notifications.emplace_back(doc);
        }
        if (notifications.empty())
        {
            failureMessage = L"No tile content to display.";
            return false;
        }

        TileUpdater updater = TileUpdateManager::CreateTileUpdaterForApplication();
        updater.EnableNotificationQueue(true);
        updater.Clear();
        for (const TileNotification& notification : notifications)
            updater.Update(notification);
        return true;
    }
    catch (const winrt::hresult_error& e)
    {
        failureMessage = FormatWide(L"Live Tile update failed: 0x%08X", static_cast<unsigned int>(e.code().value));
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            failureMessage += L" - " + msg;
        return false;
    }
    catch (const std::exception& ex)
    {
        failureMessage = FormatWide(L"Live Tile update failed: %S", ex.what());
        return false;
    }
    catch (...)
    {
        failureMessage = L"Live Tile update failed with an unknown exception.";
        return false;
    }
}

static bool RssFeed_Update_LiveTile(const RssFeedSnapshot& snapshot, std::wstring& failureMessage)
{
    failureMessage.clear();
    if (!snapshot.ok || snapshot.items.empty())
    {
        failureMessage = snapshot.status.empty() ? L"No feed items are available." : snapshot.status;
        return false;
    }

    std::vector<std::wstring> tileXmls;
    tileXmls.reserve(snapshot.items.size());
    for (const RssFeedItem& item : snapshot.items)
        tileXmls.push_back(BuildRssLiveTileXml(snapshot, item));

    return Appx_Update_LiveTileQueueBare(tileXmls, failureMessage);
}

// Top-level entry point for RSS mode's tile refresh cycle. Deliberately
// simpler than wallpaper mode's Appx_Update_Or_Request_LiveTile: RSS mode
// targets Windows 10+ only (RssLiveTile itself never supported the
// Windows 8/8.1 legacy activation-stub template, so this isn't a
// regression), which means it never needs the packaged-activation-stub
// fallback that exists solely for that legacy case.
static bool RssFeed_Update_Or_Request_LiveTile(std::wstring& failureMessage)
{
    RssFeedSettings settings = ReadRssFeedSettings();
    if (settings.feedUrl.empty())
    {
        failureMessage = L"RssFeed.FeedUrl is not configured.";
        return false;
    }

    RssFeedSnapshot snapshot = ReadRssFeedSnapshot(settings);
    return RssFeed_Update_LiveTile(snapshot, failureMessage);
}

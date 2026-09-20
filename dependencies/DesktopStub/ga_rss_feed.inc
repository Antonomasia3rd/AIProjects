// RSS/Atom data provider. Composition, rendering, refresh scheduling, and delivery
// are owned by the content host; this module never publishes notifications.

#include "../http_fetch.inc"

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
    bool valid = true;
    std::wstring validationError;
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
    std::wstring feedUrl = IniReadS(L"RssFeed", L"FeedUrl", L"");
    std::wstring userAgent = IniReadS(
        L"RssFeed",
        L"UserAgent",
        L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)");
    std::wstring updateInterval = IniReadS(L"RssFeed", L"UpdateIntervalSeconds", L"900");
    std::wstring maxItems = IniReadS(L"RssFeed", L"MaxItems", L"5");
    std::wstring httpTimeout = IniReadS(L"RssFeed", L"HttpTimeoutSeconds", L"20");
    std::wstring maxFeedBytes = IniReadS(L"RssFeed", L"MaxFeedBytes", L"2097152");
    if (!ValidateDesktopStubContentProfileValues(
            L"RssFeed",
            feedUrl,
            userAgent,
            updateInterval,
            maxItems,
            httpTimeout,
            maxFeedBytes,
            settings.validationError))
    {
        settings.valid = false;
        return settings;
    }

    settings.feedUrl = aip::Trim(feedUrl);
    settings.userAgent = aip::Trim(userAgent);
    aip::ParseIntValue(updateInterval, settings.updateIntervalSeconds);
    aip::ParseIntValue(maxItems, settings.maxItems);
    aip::ParseIntValue(httpTimeout, settings.httpTimeoutSeconds);
    int parsedMaxFeedBytes = 2 * 1024 * 1024;
    aip::ParseIntValue(maxFeedBytes, parsedMaxFeedBytes);
    settings.maxFeedBytes = static_cast<size_t>(parsedMaxFeedBytes);
    return settings;
}

// ---------------------------------------------------------------------------
// WinHTTP fetch
// ---------------------------------------------------------------------------

static bool RssHttpGetText(
    const RssFeedSettings& settings,
    std::wstring& text,
    std::wstring& error,
    const std::function<bool()>& canceled = {})
{
    text.clear();
    error.clear();
    std::wstring normalizedUrl;
    if (!ValidateRssFeedUrlSetting(settings.feedUrl, false, normalizedUrl, error))
        return false;

    aip::HttpFetchOptions options;
    options.url = settings.feedUrl;
    options.userAgent = settings.userAgent;
    options.accept = L"application/rss+xml, application/atom+xml, application/xml, text/xml, */*";
    options.timeout = std::chrono::milliseconds(static_cast<long long>(settings.httpTimeoutSeconds) * 1000);
    options.maximumBytes = settings.maxFeedBytes;
    options.canceled = [canceled] { return !g_running.load() || (canceled && canceled()); };
    aip::HttpFetchResponse response;
    if (!aip::FetchHttpBytes(options, response, error))
        return false;
    if (!RssDecodeFeedBytes(response.body, response.contentType, text))
    {
        error = L"Feed response could not be decoded as text.";
        return false;
    }
    return true;
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

static RssFeedSnapshot ReadRssFeedSnapshot(const RssFeedSettings& settings, const std::function<bool()>& canceled = {})
{
    std::wstring xml;
    std::wstring error;
    if (!RssHttpGetText(settings, xml, error, canceled))
    {
        RssFeedSnapshot snapshot;
        snapshot.status = error;
        return snapshot;
    }
    return RssParseFeedXml(xml, settings);
}

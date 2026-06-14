#define wWinMain RssLiveTileApplicationMain
#include "..\RssLiveTile.cpp"
#undef wWinMain

#include <fstream>
#include <iostream>
#include <sstream>

static int g_failures = 0;

static void Check(bool condition, const char* name)
{
    if (condition)
    {
        std::cout << "ok - " << name << "\n";
        return;
    }
    std::cerr << "not ok - " << name << "\n";
    ++g_failures;
}

static std::string ReadSource(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int wmain()
{
    WinrtApartment testApartment;
    AppSettings settings;
    settings.maxItems = 5;
    std::cout << "Running RssLiveTile source checks...\n" << std::flush;

    const std::wstring rss =
        LR"(<?xml version="1.0" encoding="utf-8"?>
<rss:rss xmlns:rss="urn:test">
  <rss:channel>
    <rss:title>Test Feed</rss:title>
    <rss:item>
      <rss:title>First &amp; Best</rss:title>
      <rss:description><![CDATA[<p>Summary text</p>]]></rss:description>
      <rss:link>https://example.test/first?a=1&amp;b=2</rss:link>
      <rss:pubDate>2026-06-12</rss:pubDate>
    </rss:item>
  </rss:channel>
</rss:rss>)";

    std::cout << "stage - parse RSS\n" << std::flush;
    FeedSnapshot rssSnapshot = ParseFeedXml(rss, settings);
    Check(rssSnapshot.ok, "namespaced RSS parses");
    Check(rssSnapshot.sourceTitle == L"Test Feed", "RSS channel title is used");
    Check(rssSnapshot.items.size() == 1, "RSS item is discovered by local name");
    Check(
        !rssSnapshot.items.empty() &&
            rssSnapshot.items[0].title == L"First & Best" &&
            rssSnapshot.items[0].summary == L"Summary text",
        "RSS text and embedded HTML are normalized");

    const std::wstring atom =
        LR"(<?xml version="1.0" encoding="utf-8"?>
<atom:feed xmlns:atom="http://www.w3.org/2005/Atom">
  <atom:title>Atom Feed</atom:title>
  <atom:entry>
    <atom:title>Atom entry</atom:title>
    <atom:summary>Atom summary</atom:summary>
    <atom:link rel="self" href="https://example.test/feed.atom" />
    <atom:link rel="alternate" href="https://example.test/article" />
    <atom:updated>2026-06-12T00:00:00Z</atom:updated>
  </atom:entry>
</atom:feed>)";

    std::cout << "stage - parse Atom\n" << std::flush;
    FeedSnapshot atomSnapshot = ParseFeedXml(atom, settings);
    Check(atomSnapshot.ok, "namespaced Atom parses");
    Check(atomSnapshot.items.size() == 1, "Atom entry is discovered by local name");
    Check(
        !atomSnapshot.items.empty() &&
            atomSnapshot.items[0].link == L"https://example.test/article",
        "Atom alternate link is preferred over self link");

    std::cout << "stage - parse malformed XML\n" << std::flush;
    FeedSnapshot malformed = ParseFeedXml(L"<rss><channel><item>", settings);
    Check(!malformed.ok, "malformed XML is rejected");
    FeedSnapshot dtd = ParseFeedXml(
        L"<!DOCTYPE rss [<!ENTITY value \"unsafe\">]><rss><channel><item><title>&value;</title></item></channel></rss>",
        settings);
    Check(!dtd.ok, "feed DTD declarations are rejected");

    std::cout << "stage - build tile XML\n" << std::flush;
    std::wstring tileXml = BuildTileXml(rssSnapshot, rssSnapshot.items[0]);
    Check(
        tileXml.find(L"--open-url") != std::wstring::npos &&
            tileXml.find(L"https://example.test/first?a=1&amp;b=2") != std::wstring::npos,
        "tile XML carries escaped article activation arguments");

    FeedItem unsafeItem = rssSnapshot.items[0];
    unsafeItem.link = L"javascript:alert(1)";
    Check(
        BuildTileXml(rssSnapshot, unsafeItem).find(L"--open-url") == std::wstring::npos,
        "tile XML rejects non-HTTP activation links");
    std::wstring activationUrl;
    Check(
        TryParseTileActivationArguments(
            L"--open-url \"https://example.test/article?a=1&b=2\"",
            activationUrl) &&
            activationUrl == L"https://example.test/article?a=1&b=2",
        "chaseable tile arguments recover the article URL");
    Check(
        !TryParseTileActivationArguments(
            L"--open-url \"javascript:alert(1)\"",
            activationUrl),
        "chaseable tile arguments reject non-HTTP URLs");

    std::wstring error;
    Check(
        ValidateSetting(
            aip::IniSetting{ L"Settings", L"UpdateIntervalSeconds", L"15" },
            error),
        "minimum update interval is accepted");
    error.clear();
    Check(
        !ValidateSetting(
            aip::IniSetting{ L"Settings", L"UpdateIntervalSeconds", L"14" },
            error),
        "out-of-range update interval is rejected");
    error.clear();
    Check(
        !ValidateSetting(
            aip::IniSetting{ L"Manifest", L"Version", L"1.2.3" },
            error),
        "invalid manifest version is rejected");
    error.clear();
    Check(
        ValidateSetting(
            aip::IniSetting{ L"Settings", L"ShowMenuAsDropdown", L"0" },
            error) &&
            !ValidateSetting(
                aip::IniSetting{ L"Settings", L"ShowMenuAsDropdown", L"sideways" },
                error),
        "tray dropdown setting uses typed boolean validation");
    Check(
        std::wstring(WINDOW_CLASS_NAME) != L"" &&
            WM_RLT_CONTROL != 0 &&
            RLT_CONTROL_RELOAD != RLT_CONTROL_REFRESH,
        "resident control window identity is defined");
    AppOptions writeOptions;
    writeOptions.writes.push_back(
        aip::IniSetting{ L"Settings", L"UpdateIntervalSeconds", L"60" });
    Check(
        ExistingInstanceRequestForOptions(writeOptions) == RLT_CONTROL_RELOAD &&
            ExistingInstanceRequestForOptions(AppOptions{}) == RLT_CONTROL_REFRESH,
        "command-line settings reload an existing resident");
    AppOptions trayOptions;
    SetTrayOverride(trayOptions, false);
    SetTrayOverride(trayOptions, true);
    Check(
        trayOptions.forceTray && !trayOptions.forceNoTray,
        "tray command-line overrides use last-option-wins behavior");
    AppOptions multipleOptions;
    multipleOptions.allowMultiple = true;
    Check(
        BuildPackagedArguments(multipleOptions).find(L"--allow-multiple") != std::wstring::npos,
        "packaged bootstrap preserves multi-instance intent");
    RuntimeContext runtimeDefaults;
    Check(
        !runtimeDefaults.updatePending.load() &&
            !runtimeDefaults.forceUpdatePending.load(),
        "resident update queue starts empty");
    Check(
        runtimeDefaults.settings.showMenuAsDropdown,
        "tray dropdown layout defaults on");
    const std::string source = ReadSource("RssLiveTile.cpp");
    const size_t togglePosition = source.find(
        "AppendTrayMenuItem(menu, IDM_TOGGLE_MENU_DROPDOWN");
    const size_t layoutPosition = source.find(
        "TraySectionLayout sectionLayout(menu, showMenuAsDropdown)");
    const size_t feedPosition = source.find(
        "HMENU feedMenu = sectionLayout.Begin");
    Check(
        togglePosition != std::string::npos &&
            layoutPosition != std::string::npos &&
            feedPosition != std::string::npos &&
            togglePosition < layoutPosition &&
            layoutPosition < feedPosition,
        "tray dropdown toggle remains on the root before section layout");
    Check(
        POWERSHELL_OUTPUT_LIMIT_BYTES == 1024 * 1024,
        "PowerShell output capture is bounded");

    std::string encodedFeed =
        "<?xml version=\"1.0\" encoding=\"windows-1252\"?><rss><title>Caf";
    encodedFeed.push_back(static_cast<char>(0xE9));
    encodedFeed += "</title></rss>";
    std::vector<BYTE> encodedBytes(encodedFeed.begin(), encodedFeed.end());
    std::wstring decodedFeed;
    Check(
        DecodeFeedBytes(encodedBytes, L"application/rss+xml", decodedFeed) &&
            decodedFeed.find(L"Caf\u00e9") != std::wstring::npos,
        "XML-declared legacy feed encoding is decoded");
    std::wstring systemDirectory = aip::GetSystemDirectoryPath();
    Check(
        !systemDirectory.empty() &&
            DefaultPowerShellExe().rfind(systemDirectory, 0) == 0,
        "PowerShell discovery uses the growable shared system-directory helper");

    if (g_failures != 0)
    {
        std::cerr << "RssLiveTile source checks failed (" << g_failures << ").\n";
        return 1;
    }
    std::cout << "RssLiveTile source checks passed.\n";
    return 0;
}

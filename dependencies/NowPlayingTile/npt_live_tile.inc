static bool HasPackageIdentity()
{
    UINT32 length = 0;
    LONG rc = GetCurrentPackageFullName(&length, nullptr);
    return rc == ERROR_INSUFFICIENT_BUFFER;
}

static std::wstring BuildTextTileXml(const MediaSnapshot& snapshot)
{
    std::wstring title = XmlEscape(snapshot.title);
    std::wstring artist = XmlEscape(snapshot.artist);
    std::wstring source = XmlEscape(snapshot.source);
    std::wstring status = XmlEscape(snapshot.status);

    return
        L"<tile>"
        L"  <visual branding=\"nameAndLogo\">"
        L"    <binding template=\"TileMedium\">"
        L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + title + L"</text>"
        L"      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + L"</text>"
        L"      <text hint-style=\"captionSubtle\">" + status + L"</text>"
        L"    </binding>"
        L"    <binding template=\"TileWide\">"
        L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>"
        L"      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + artist + L"</text>"
        L"      <text hint-style=\"captionSubtle\">" + status + L" | " + source + L"</text>"
        L"    </binding>"
        L"    <binding template=\"TileLarge\">"
        L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>"
        L"      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + artist + L"</text>"
        L"      <text hint-style=\"captionSubtle\">" + status + L" | " + source + L"</text>"
        L"    </binding>"
        L"  </visual>"
        L"</tile>";
}

static std::wstring BuildArtworkTileXml(const MediaSnapshot& snapshot)
{
    std::wstring artwork = XmlEscape(snapshot.artworkUri);

    return
        L"<tile>"
        L"  <visual branding=\"nameAndLogo\">"
        L"    <binding template=\"TileMedium\">"
        L"      <image placement=\"background\" hint-crop=\"none\" hint-overlay=\"0\" src=\"" + artwork + L"\"/>"
        L"    </binding>"
        L"    <binding template=\"TileWide\">"
        L"      <image placement=\"background\" hint-crop=\"none\" hint-overlay=\"0\" src=\"" + artwork + L"\"/>"
        L"    </binding>"
        L"    <binding template=\"TileLarge\">"
        L"      <image placement=\"background\" hint-crop=\"none\" hint-overlay=\"0\" src=\"" + artwork + L"\"/>"
        L"    </binding>"
        L"  </visual>"
        L"</tile>";
}

static std::wstring BuildCombinedTileXml(const MediaSnapshot& snapshot)
{
    std::wstring title = XmlEscape(snapshot.title);
    std::wstring artist = XmlEscape(snapshot.artist);
    std::wstring source = XmlEscape(snapshot.source);
    std::wstring status = XmlEscape(snapshot.status);
    std::wstring artwork = XmlEscape(snapshot.artworkUri);

    return
        L"<tile>"
        L"  <visual branding=\"nameAndLogo\">"
        L"    <binding template=\"TileMedium\">"
        L"      <image placement=\"background\" hint-crop=\"none\" hint-overlay=\"45\" src=\"" + artwork + L"\"/>"
        L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + title + L"</text>"
        L"      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + artist + L"</text>"
        L"    </binding>"
        L"    <binding template=\"TileWide\">"
        L"      <image placement=\"background\" hint-crop=\"none\" hint-overlay=\"45\" src=\"" + artwork + L"\"/>"
        L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>"
        L"      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + artist + L"</text>"
        L"      <text hint-style=\"captionSubtle\">" + status + L" | " + source + L"</text>"
        L"    </binding>"
        L"    <binding template=\"TileLarge\">"
        L"      <image placement=\"background\" hint-crop=\"none\" hint-overlay=\"45\" src=\"" + artwork + L"\"/>"
        L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>"
        L"      <text hint-style=\"subtitle\" hint-wrap=\"true\">" + artist + L"</text>"
        L"      <text hint-style=\"captionSubtle\">" + status + L" | " + source + L"</text>"
        L"    </binding>"
        L"  </visual>"
        L"</tile>";
}

static std::vector<std::wstring> BuildTilePayloads(const MediaSnapshot& snapshot, TileLayout layout)
{
    std::vector<std::wstring> payloads;
    const bool hasArtwork = !snapshot.artworkUri.empty();
    switch (layout)
    {
    case TileLayout::Artwork:
        payloads.push_back(hasArtwork ? BuildArtworkTileXml(snapshot) : BuildTextTileXml(snapshot));
        break;
    case TileLayout::Combined:
        payloads.push_back(hasArtwork ? BuildCombinedTileXml(snapshot) : BuildTextTileXml(snapshot));
        break;
    case TileLayout::Cycle:
        payloads.push_back(BuildTextTileXml(snapshot));
        if (hasArtwork)
        {
            payloads.push_back(BuildArtworkTileXml(snapshot));
            payloads.push_back(BuildCombinedTileXml(snapshot));
        }
        break;
    case TileLayout::Text:
    default:
        payloads.push_back(BuildTextTileXml(snapshot));
        break;
    }
    return payloads;
}

static bool TryUpdateLiveTile(const MediaSnapshot& snapshot, const AppSettings& settings)
{
    try
    {
        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;

        std::vector<std::wstring> payloads = BuildTilePayloads(snapshot, settings.tileLayout);
        auto updater = TileUpdateManager::CreateTileUpdaterForApplication();
        updater.EnableNotificationQueue(payloads.size() > 1);
        updater.Clear();

        for (const auto& payload : payloads)
        {
            XmlDocument doc;
            doc.LoadXml(winrt::hstring(payload));
            TileNotification notification(doc);
            updater.Update(notification);
        }

        return true;
    }
    catch (const winrt::hresult_error& ex)
    {
        Log(L"Live Tile update failed: 0x%08X %s", static_cast<unsigned int>(ex.code()), ex.message().c_str());
        if (!HasPackageIdentity())
        {
            Log(L"This process has no package identity. Run NowPlayingTile.exe --register, then launch with NowPlayingTile.exe --launch-packaged or shell:AppsFolder.");
        }
        return false;
    }
    catch (...)
    {
        Log(L"Live Tile update failed with an unknown exception.");
        return false;
    }
}

static void MaybeUpdateLiveTile(RuntimeContext* ctx, const MediaSnapshot& snapshot)
{
    if (ctx == nullptr)
    {
        return;
    }

    std::wstring key = snapshot.source + L"\n" + snapshot.title + L"\n" + snapshot.artist + L"\n" + snapshot.status + L"\n" + snapshot.artworkHash + L"\n" + TileLayoutName(ctx->settings.tileLayout);
    auto now = steady_clock::now();
    bool stale = ctx->lastTileUpdate == steady_clock::time_point::min() || duration_cast<seconds>(now - ctx->lastTileUpdate).count() >= ctx->settings.tileRefreshSeconds;
    if (key == ctx->lastTileKey && !stale)
    {
        return;
    }

    if (TryUpdateLiveTile(snapshot, ctx->settings))
    {
        ctx->lastTileKey = key;
        ctx->lastTileUpdate = now;
    }
}

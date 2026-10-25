// -----------------------------------------------------------------------------
// Live Tile update
// -----------------------------------------------------------------------------
#include "../tile_notification_winrt.inc"
#include "../media_tile_layout.h"
#include "../prayer_tile_layout.h"
#include "../datetime_tile_layout.h"
#include "../game_tile_layout.h"
static bool CurrentProcessPackageFullName(std::wstring& packageFullName)
{
    packageFullName.clear();

    UINT32 length = 0;
    LONG rc = GetCurrentPackageFullName(&length, nullptr);
    if (rc == APPMODEL_ERROR_NO_PACKAGE)
        return false;
    if (rc != ERROR_INSUFFICIENT_BUFFER || length == 0)
    {
        SetLastError(static_cast<DWORD>(rc));
        return false;
    }

    std::vector<wchar_t> buffer(length);
    rc = GetCurrentPackageFullName(&length, buffer.data());
    if (rc != ERROR_SUCCESS)
    {
        SetLastError(static_cast<DWORD>(rc));
        return false;
    }

    packageFullName.assign(buffer.data());
    return !packageFullName.empty();
}

static bool IsUnreservedUriPathByte(unsigned char value)
{
    return (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') ||
        value == '-' ||
        value == '.' ||
        value == '_' ||
        value == '~' ||
        value == '/';
}

static std::wstring PercentEncodeUriPath(const std::wstring& path)
{
    if (path.empty())
        return L"";

    std::string bytes;
    if (!aip::TryWideToUtf8(path, bytes))
        return L"";

    static const wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(bytes.size());
    for (unsigned char value : bytes)
    {
        if (IsUnreservedUriPathByte(value))
        {
            out.push_back(static_cast<wchar_t>(value));
            continue;
        }

        out.push_back(L'%');
        out.push_back(hex[(value >> 4) & 0x0F]);
        out.push_back(hex[value & 0x0F]);
    }
    return out;
}

static std::wstring ManifestAssetUri(const std::wstring& manifestAssetPath)
{
    std::wstring path = TrimCopy(manifestAssetPath);
    std::replace(path.begin(), path.end(), L'\\', L'/');
    while (!path.empty() && path.front() == L'/')
        path.erase(path.begin());
    if (path.empty())
        return L"";
    return L"ms-appx:///" + PercentEncodeUriPath(path);
}

static std::wstring LiveTileAssetUriIfValid(
    const std::wstring& exeDir,
    const std::wstring& manifestAssetPath,
    int expectedWidth,
    int expectedHeight)
{
    if (!IsManifestRelativeAssetPath(manifestAssetPath))
        return L"";

    std::wstring fullPath = ResolveManifestAssetPath(exeDir, manifestAssetPath);
    if (!IsValidGeneratedPngWithDimensions(fullPath, expectedWidth, expectedHeight))
        return L"";

    return ManifestAssetUri(manifestAssetPath);
}

static bool LiveTileUsesAdaptiveXml()
{
    return ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10 &&
        ConfiguredLiveTileTemplateStyle() == LiveTileTemplateStyle::Adaptive;
}

static bool LiveTileUsesWindows81PresetXml()
{
    return ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10 &&
        ConfiguredLiveTileTemplateStyle() == LiveTileTemplateStyle::Windows81Preset;
}

static const wchar_t* LiveTileTemplateForTarget(const wchar_t* win10TemplateName)
{
    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows10)
        return win10TemplateName;

    if (IEquals(win10TemplateName ? win10TemplateName : L"", L"TileMedium"))
        return target == ManifestCompatibilityTarget::Windows81 ? L"TileSquare150x150Image" : L"TileSquareImage";
    if (IEquals(win10TemplateName ? win10TemplateName : L"", L"TileWide"))
        return target == ManifestCompatibilityTarget::Windows81 ? L"TileWide310x150Image" : L"TileWideImage";
    if (IEquals(win10TemplateName ? win10TemplateName : L"", L"TileLarge"))
        return target == ManifestCompatibilityTarget::Windows81 ? L"TileSquare310x310Image" : nullptr;

    return nullptr;
}

static void AppendLiveTileImageBinding(
    std::wstring& xml,
    const wchar_t* templateName,
    const std::wstring& imageUri,
    int overlay,
    const wchar_t* assetName)
{
    const std::wstring tileText = TileTextAppliesToTileName(assetName) ? TileTextXmlText() : L"";
    const std::wstring secondaryText = TileTextAppliesToTileName(assetName) ? TileTextXmlSecondaryText() : L"";
    const std::wstring badgeText = TileTextAppliesToTileName(assetName) ? TileTextXmlBadgeText() : L"";
    const bool omitBackgroundImage = LiveTileUsesAdaptiveXml() && ContentHasTransparentBackground() &&
        (!tileText.empty() || !secondaryText.empty() || !badgeText.empty());
    if (!templateName || !*templateName || (imageUri.empty() && !omitBackgroundImage))
        return;

    xml += L"    <binding template=\"";
    xml += templateName;
    if(LiveTileUsesAdaptiveXml()&&g_renderContent&&g_renderContent->textMode==aip::content::TextMode::Overlay&&
        (g_renderContent->clockSource||g_renderContent->calendarSource)){
        const bool isMediumSize=std::wstring(templateName)==L"TileMedium";
        const bool centered=isMediumSize&&aip::content::CalendarDateOnlyCentered(*g_renderContent,150,150);
        xml+=L"\" branding=\""+aip::content::DateTimeBindingBranding(*g_renderContent,centered,isMediumSize);
        const auto displayName=aip::content::DateTimeBindingDisplayName(*g_renderContent,isMediumSize?150:310,std::wstring(templateName)==L"TileLarge"?310:150);
        if(g_renderContent->clockSource||!displayName.empty())xml+=L"\" displayName=\""+aip::XmlEscape(displayName);
    }
    xml += L"\">\r\n";
    if (!omitBackgroundImage) {
        xml += L"      <image ";
        if (LiveTileUsesAdaptiveXml())
        {
            xml += L"src=\"";
            xml += XmlEscape(imageUri);
            xml += L"\" placement=\"background\" hint-overlay=\"";
            xml += IntToWString(overlay);
            xml += L"\"";
        }
        else
        {
            xml += L"id=\"1\" src=\"";
            xml += XmlEscape(imageUri);
            xml += L"\"";
        }
        xml += L"/>\r\n";
    }

    if (LiveTileUsesAdaptiveXml() && TileTextAppliesToTileName(assetName) &&
        (!tileText.empty() || !secondaryText.empty() || !badgeText.empty()))
    {
        if (!badgeText.empty() && (!tileText.empty() || !secondaryText.empty()))
        {
            xml += L"      <group>\r\n";
            xml += L"        <subgroup hint-weight=\"3\">\r\n";
            if (!tileText.empty())
            {
                xml += L"          <text hint-style=\"caption\">";
                xml += XmlEscape(tileText);
                xml += L"</text>\r\n";
            }
            if (!secondaryText.empty())
            {
                xml += L"          <text hint-style=\"captionSubtle\" hint-wrap=\"true\">";
                xml += XmlEscape(secondaryText);
                xml += L"</text>\r\n";
            }
            xml += L"        </subgroup>\r\n";
            xml += L"        <subgroup hint-weight=\"1\">\r\n";
            xml += L"          <text hint-style=\"title\" hint-align=\"right\">";
            xml += XmlEscape(badgeText);
            xml += L"</text>\r\n";
            xml += L"        </subgroup>\r\n";
            xml += L"      </group>\r\n";
        }
        else
        {
            if (!badgeText.empty())
            {
                xml += L"      <text hint-style=\"title\" hint-align=\"right\">";
                xml += XmlEscape(badgeText);
                xml += L"</text>\r\n";
            }
            if (!tileText.empty())
            {
                xml += L"      <text hint-style=\"caption\">";
                xml += XmlEscape(tileText);
                xml += L"</text>\r\n";
            }
            if (!secondaryText.empty())
            {
                xml += L"      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">";
                xml += XmlEscape(secondaryText);
                xml += L"</text>\r\n";
            }
        }
    }

    xml += L"    </binding>\r\n";
}

#include "ga_live_tile_templates.inc"

#include "../rss_tile_layout.h"
static std::wstring MediaAppIconUri(const std::wstring& directory, const aip::content::Snapshot& snapshot)
{
    if (snapshot.smtcAppIconPath.empty() || !snapshot.smtcAppIconLease) return {};
    // Store gives validated logos content-addressed filenames. Keep the
    // package asset immutable while an older notification can still use it.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto leaf = snapshot.smtcAppIconPath.substr(snapshot.smtcAppIconPath.find_last_of(L"\\/") + 1);
    HashWide(hash, leaf);
    const auto relative = L"Assets/MediaIndicators/Icon-" + Hex64(hash) + L".png";
    const auto path = ResolveManifestAssetPath(directory, relative);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return aip::tile_visual::PackageUri(relative);
    DWORD error = ERROR_SUCCESS;
    if (!EnsureParentDirectoryForFile(path, &error)) return {};
    ScopedGdiPlus gdi;
    if (!gdi.ok()) return {};
    Bitmap source(snapshot.smtcAppIconPath.c_str());
    if (source.GetLastStatus() != Ok || !SavePNG(&source, path.c_str())) return {};
    return aip::tile_visual::PackageUri(relative);
}
static std::wstring MediaTopicUri(const std::wstring& topic, const std::wstring& directory)
{
    aip::tile_visual::Assets icons; std::wstring error;
    return aip::tile_visual::EnsureTileTopicAssets(topic, directory, icons, error) ?
        aip::tile_visual::PackageUri(icons.smallTile) : std::wstring{};
}
static std::wstring NotesTileIconUri(const std::wstring& topic, const std::wstring& directory)
{
    if (!aip::notes::assets::Find(topic)) return {};
    const auto relative = L"Assets/NotesRows/v1/" + topic + L".png";
    const auto path = ResolveManifestAssetPath(directory, relative);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return aip::tile_visual::PackageUri(relative);
    DWORD error = ERROR_SUCCESS;
    ScopedGdiPlus gdi;
    if (!gdi.ok()) return {};
    auto bitmap = LoadNotesTileIcon(topic);
    if (!bitmap || !EnsureParentDirectoryForFile(path, &error) || !SavePNG(bitmap.get(), path.c_str())) return {};
    return aip::tile_visual::PackageUri(relative);
}
static std::wstring GameRowSpacingUri(const std::wstring& directory,int width,int spacing){
    if(spacing<=0||spacing>24||width<=20||width>1024)return {};
    // Adaptive text has no arbitrary pixel margin. An immutable transparent
    // inline image preserves the same row gap as the bitmap renderer.
    const auto relative=L"Assets/GameRows/v1/Gap-"+std::to_wstring(width-20)+L"x"+std::to_wstring(spacing)+L".png";
    const auto path=ResolveManifestAssetPath(directory,relative);if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)return aip::tile_visual::PackageUri(relative);
    ScopedGdiPlus gdi;DWORD error=ERROR_SUCCESS;if(!gdi.ok()||!EnsureParentDirectoryForFile(path,&error))return {};
    Bitmap bitmap(width-20,spacing,PixelFormat32bppARGB);Graphics graphics(&bitmap);graphics.Clear(Color(0,0,0,0));
    return bitmap.GetLastStatus()==Ok&&graphics.GetLastStatus()==Ok&&SavePNG(&bitmap,path.c_str())?aip::tile_visual::PackageUri(relative):std::wstring{};
}
static const wchar_t* ContentLiveTileBrandingXmlValue()
{
    if (!g_renderContent || (g_renderContent->secondaryTileId.empty() && g_renderContent->secondaryBranding.empty()))
        return LiveTileBrandingXmlValue();
    const auto value = ParseLiveTileBrandingValue(g_renderContent->secondaryBranding, LiveTileBranding::Name);
    switch (value) {
    case LiveTileBranding::None: return L"none";
    case LiveTileBranding::Logo: return L"logo";
    case LiveTileBranding::Name: return L"name";
    case LiveTileBranding::NameAndLogo: return L"nameAndLogo";
    default: return ConfiguredLiveTileTemplateStyle() == LiveTileTemplateStyle::Windows81Preset ? L"name" : L"nameAndLogo";
    }
}
static std::wstring BuildLiveTileXml(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets)
{
    (void)manifestInfo;

    std::wstring xml;
    bool hasUsableBinding = false;
    xml += L"<tile>\r\n";
    if (LiveTileUsesAdaptiveXml())
    {
        xml += L"  <visual branding=\"";
        xml += ContentLiveTileBrandingXmlValue();
        xml += L"\">\r\n";
    }
    else if (LiveTileUsesWindows81PresetXml())
    {
        xml += L"  <visual version=\"4\" branding=\"";
        xml += ContentLiveTileBrandingXmlValue();
        xml += L"\">\r\n";
    }
    else if (ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows81)
        xml += L"  <visual version=\"2\">\r\n";
    else
        xml += L"  <visual>\r\n";

    if (g_renderContent && !g_renderContent->activationArguments.empty() &&
        ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10)
    {
        const auto end = xml.find(L'>', xml.find(L"<visual"));
        if (end != std::wstring::npos)
            xml.insert(end, L" arguments=\"" + aip::XmlEscape(g_renderContent->activationArguments) + L"\"");
    }

    if(LiveTileUsesAdaptiveXml()&&g_renderContent&&g_renderContent->calendarSource&&
        g_renderContent->textMode!=aip::content::TextMode::Off&&
        aip::content::AppendDateTimeTileBinding(xml,L"TileSmall",71,*g_renderContent))hasUsableBinding=true;
    for (const auto& asset : liveTileAssets)
    {
        if(LiveTileUsesAdaptiveXml()&&g_renderContent&&g_renderContent->mailSource&&g_renderContent->textMode==aip::content::TextMode::Auto&&
            (std::wstring(asset.binding)==L"TileSmall"||(std::wstring(asset.binding)==L"TileLarge"&&!g_renderContent->mailIncludeLarge)))continue;
        if(LiveTileUsesAdaptiveXml()&&g_renderContent&&g_renderContent->calendarSource&&
            g_renderContent->calendarFlavor==aip::content::CalendarFlavor::Agenda&&std::wstring(asset.binding)!=L"TileWide")continue;
        std::wstring uri = LiveTileAssetUriIfValid(exeDir, asset.file, asset.w, asset.h);
        if (LiveTileUsesWindows81PresetXml())
        {
            if (AppendWindows81PresetLiveTileBinding(xml, asset.binding, asset.name, uri))
                hasUsableBinding = true;
            continue;
        }

        const wchar_t* templateName = LiveTileTemplateForTarget(asset.binding);
        if(LiveTileUsesAdaptiveXml()&&g_renderContent&&g_renderContent->mailSource&&g_renderContent->textMode==aip::content::TextMode::Auto){
            if(aip::content::AppendMailTileBinding(xml,templateName,asset.h,*g_renderContent,ContentHasTransparentBackground()?L"":uri))hasUsableBinding=true;
            continue;
        }
        if (LiveTileUsesAdaptiveXml() && g_renderContent && !g_renderContent->rssHeadlines.empty() &&
            g_renderContent->textMode == aip::content::TextMode::Auto && aip::content::RssUsesRichLayout(g_renderContent->presentation) &&
            aip::content::AppendRssRichTileBinding(xml, templateName, asset.h, g_renderContent->rssHeadlines,
                ContentHasTransparentBackground() ? L"" : uri, g_renderContent->presentation)) {
            hasUsableBinding = true; continue;
        }
        if (LiveTileUsesAdaptiveXml() && g_renderContent && (g_renderContent->clockSource || g_renderContent->calendarSource) &&
            g_renderContent->textMode == aip::content::TextMode::Auto &&
            aip::content::AppendDateTimeTileBinding(xml,templateName,asset.h,*g_renderContent,
                ContentHasTransparentBackground()?L"":uri)) { hasUsableBinding=true;continue; }
        if (LiveTileUsesAdaptiveXml() && g_renderContent && g_renderContent->prayerSource &&
            g_renderContent->textMode == aip::content::TextMode::Auto &&
            aip::content::AppendPrayerTileBinding(xml, templateName, asset.h, g_renderContent->text, g_renderContent->detailRows,
                ContentHasTransparentBackground() ? L"" : uri, g_renderContent->prayerClock, g_renderContent->presentation)) { hasUsableBinding = true; continue; }
        if (LiveTileUsesAdaptiveXml() && g_renderContent && g_renderContent->smtcSource &&
            g_renderContent->textMode == aip::content::TextMode::Auto) {
            std::wstring icon;
            if (g_renderContent->smtcIndicator == aip::content::MediaIndicatorMode::AppIcon) icon = MediaAppIconUri(exeDir, *g_renderContent);
            const bool appIcon = !icon.empty();
            if (icon.empty() && g_renderContent->smtcIndicator != aip::content::MediaIndicatorMode::None)
                icon = MediaTopicUri(aip::content::MediaStatusTopic(g_renderContent->smtcState), exeDir);
            std::vector<aip::content::MediaIndicatorImage> extras;
            for (const auto& topic : aip::content::MediaExtraTopics(*g_renderContent)) {
                const auto extra = MediaTopicUri(topic, exeDir);
                if (!extra.empty()) extras.push_back({extra, topic});
            }
            if (aip::content::AppendMediaTileBinding(xml, templateName, g_renderContent->text,
                    icon, ContentHasTransparentBackground() ? L"" : uri, extras,
                    appIcon ? g_renderContent->smtcAppName : aip::content::MediaStatusLabel(g_renderContent->smtcState))) {
                hasUsableBinding = true; continue;
            }
        }
        if (LiveTileUsesAdaptiveXml() && g_renderContent && !g_renderContent->detailRows.empty() &&
            g_renderContent->textMode == aip::content::TextMode::Auto) {
            std::vector<aip::content::MediaIndicatorImage> rowIcons;
            for (const auto& row : g_renderContent->detailRows) {
                auto icon = g_renderContent->gameSource&&g_renderContent->presentation.iconPlacement==L"None"?std::wstring{}:NotesTileIconUri(row.iconTopic, exeDir);
                if (!g_renderContent->gameSource&&icon.empty() && !row.iconTopic.empty()) icon = MediaTopicUri(g_renderContent->detailTopic, exeDir);
                rowIcons.push_back({icon, row.primary});
            }
            if(g_renderContent->gameSource&&aip::content::AppendGameTileBinding(xml,templateName,asset.w,asset.h,*g_renderContent,ContentHasTransparentBackground()?L"":uri,rowIcons,
                GameRowSpacingUri(exeDir,asset.w,g_renderContent->presentation.rowSpacing))){hasUsableBinding=true;continue;}
            if (aip::content::AppendDetailRowsBinding(xml, templateName, asset.h, g_renderContent->detailRows,
                ContentHasTransparentBackground() ? L"" : uri, L"", rowIcons)) {
                hasUsableBinding = true; continue;
            }
        }
        if (LiveTileUsesAdaptiveXml() && g_renderContent && g_renderContent->wrapSourceText &&
            g_renderContent->textMode == aip::content::TextMode::Auto) {
            if (aip::content::AppendWrappedSourceBinding(xml, templateName, g_renderContent->text,
                ContentHasTransparentBackground() ? L"" : uri)) { hasUsableBinding = true; continue; }
        }
        if (LiveTileUsesAdaptiveXml() && g_renderContent && !g_renderContent->rssHeadlines.empty() &&
            g_renderContent->textMode == aip::content::TextMode::Auto) {
            if (aip::content::AppendRssTileBinding(xml, templateName, asset.h, g_renderContent->rssHeadlines,
                ContentHasTransparentBackground() ? L"" : uri)) hasUsableBinding = true;
            continue;
        }
        const bool nativeTransparentText = LiveTileUsesAdaptiveXml() && ContentHasTransparentBackground() &&
            TileTextAppliesToTileName(asset.name) &&
            (!TileTextXmlText().empty() || !TileTextXmlSecondaryText().empty() || !TileTextXmlBadgeText().empty());
        if (!templateName || (uri.empty() && !nativeTransparentText))
            continue;

        AppendLiveTileImageBinding(xml, templateName, uri, 0, asset.name);
        hasUsableBinding = true;
    }

    if (!hasUsableBinding)
        return L"";

    xml += L"  </visual>\r\n";
    xml += L"</tile>";
    const bool richRssPresentation = g_renderContent && !g_renderContent->rssHeadlines.empty() &&
        g_renderContent->textMode == aip::content::TextMode::Auto && aip::content::RssUsesRichLayout(g_renderContent->presentation);
    if (LiveTileUsesAdaptiveXml() && g_renderContent && g_renderContent->presentation.Active() && !richRssPresentation && !g_renderContent->prayerSource && !g_renderContent->clockSource && !g_renderContent->calendarSource && !g_renderContent->mailSource && !g_renderContent->gameSource)
        xml = aip::content::ApplyTextPresentationXml(xml, g_renderContent->presentation);
    aip::tile_notification::Options options;
    if (g_renderContent) {
        options.queued = g_renderContent->nativeQueue;
        options.tag = g_renderContent->nativeQueueTag;
        options.reset = g_renderContent->nativeQueueReset;
        options.secondaryTileId = g_renderContent->secondaryTileId;
        if(g_renderContent->mailSource&&(!g_renderContent->nativeQueue||g_renderContent->nativeQueueReset))
            options.badgeCount=g_renderContent->mailBadgeEnabled?g_renderContent->mailUnreadCount:0;
    }
    std::wstring transport, error;
    if (!aip::tile_notification::Encode(xml, options, transport, error)) {
        Log(L"[!] Could not prepare the private tile notification payload: %ls", error.c_str());
        return {};
    }
    return transport;
}


static std::wstring PendingLiveTileXmlPath()
{
    return ProductRuntimeFilePath(L".livetile.pending.xml");
}

static std::wstring PendingLiveTileClearPath()
{
    return ProductRuntimeFilePath(L".livetile.clear");
}

static bool WritePendingLiveTileClearRequest()
{
    return WriteUtf8BomTextFile(PendingLiveTileClearPath(), L"clear");
}

static bool WritePendingLiveTileXml(const std::wstring& tileXml)
{
    aip::tile_notification::Payload payload;
    std::wstring error;
    if (!aip::tile_notification::Decode(tileXml, payload, error)) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    return WriteUtf8BomTextFile(PendingLiveTileXmlPath(), tileXml);
}

static bool Appx_Update_LiveTileXmlBare(const std::wstring& tileXml, std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring packageFullName;
    if (!CurrentProcessPackageFullName(packageFullName))
    {
        DWORD err = GetLastError();
        failureMessage = FormatWide(g_ui.liveTileUpdateRequiresIdentityDetails.c_str(), static_cast<unsigned long>(err));
        return false;
    }

    if (TrimCopy(tileXml).empty())
    {
        failureMessage = g_ui.liveTileXmlEmpty;
        return false;
    }

    try
    {
        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);

        std::wstring badgeError;
        const bool delivered=aip::tile_notification::DeliverWinrt(tileXml,failureMessage,&badgeError);
        if(!badgeError.empty())LogText(L"[!] The tile updated, but Windows could not update its unread badge.");
        return delivered;
    }
    catch (const winrt::hresult_error& e)
    {
        failureMessage = FormatWide(g_ui.liveTileUpdateFailedHresult.c_str(), Hex64(static_cast<uint32_t>(e.code().value)).c_str());
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            failureMessage += FormatWide(g_ui.liveTileErrorMessageSuffix.c_str(), msg.c_str());
        return false;
    }
    catch (const std::exception& ex)
    {
        failureMessage = FormatWide(g_ui.liveTileUpdateFailedException.c_str(), ExceptionTextToWide(ex).c_str());
        return false;
    }
    catch (...)
    {
        failureMessage = FormatWide(g_ui.liveTileUpdateFailedException.c_str(), UnknownExceptionText().c_str());
        return false;
    }
}

// Only explicit owners from this profile are candidates. Gallery history is
// retained when rows are removed/disabled, so include its still-pinned owners
// without resetting reservations, advancing cursors, or deleting image assets.
#include "../tile_lifecycle.h"
static bool TileInstancePrimaryClearsOnShutdown()
{
    if(!TileInstanceModeRequested())return g_clearLiveTileOnShutdown.load();
    aip::content::TileInstanceConfiguration instances;std::wstring error;
    if(!ReadCurrentTileInstances(instances,error))return false;
    return std::any_of(instances.tiles.begin(),instances.tiles.end(),[](const auto& tile){return tile.primary&&aip::content::TileInstanceClearsOnShutdown(tile);});
}
static bool AnyTileInstanceClearsOnShutdown()
{
    if(!TileInstanceModeRequested())return g_clearLiveTileOnShutdown.load();
    if(g_clearLiveTileOnShutdown.load())return true; // Gallery keeps its existing global exit policy.
    aip::content::TileInstanceConfiguration instances;std::wstring error;
    if(ReadCurrentTileInstances(instances,error)&&std::any_of(instances.tiles.begin(),instances.tiles.end(),[](const auto& tile){return aip::content::TileInstanceClearsOnShutdown(tile);}))return true;
    aip::gallery::Configuration gallery;
    return ReadGalleryConfiguration(gallery,error)&&!gallery.groupId.empty()&&std::any_of(gallery.tiles.begin(),gallery.tiles.end(),[](const auto& tile){return tile.delivery.Read(L"Settings",L"ClearLiveTileOnShutdown",L"1")==L"1";});
}
static std::set<std::wstring> OwnedSecondaryTileIdsForClear(bool shutdownPolicy=false)
{
    std::set<std::wstring> owned;
    aip::content::TileInstanceConfiguration instances; std::wstring instanceError;
    if (TileInstanceModeRequested()) {
        if (ReadCurrentTileInstances(instances, instanceError)) for (const auto& tile : instances.tiles)
            if (!tile.primary&&(!shutdownPolicy||aip::content::TileInstanceClearsOnShutdown(tile))) owned.insert(aip::content::TileInstanceNativeId(tile));
    } else for (const auto& tile : aip::content::SecondaryTiles) owned.insert(L"AipSource_" + std::wstring(tile.id));
    aip::gallery::Configuration gallery; std::wstring error;
    std::map<std::wstring,bool> galleryExit;
    if (ReadGalleryConfiguration(gallery, error)) for (const auto& tile : gallery.tiles) {
        const auto id = aip::gallery::NativeTileId(gallery.groupId, tile.id);
        const bool clear=tile.delivery.Read(L"Settings",L"ClearLiveTileOnShutdown",L"1")==L"1";
        if (!id.empty()){galleryExit[id]=clear;if(!shutdownPolicy||clear)owned.insert(id);}
    }
    std::wstring profile; { std::lock_guard<std::mutex> lock(g_pathMutex); profile = aip::MakeAbsolutePath(g_iniPath); }
    aip::gallery::Ledger ledger;
    if (!profile.empty() && ReadGalleryLedgerNative(aip::gallery::runtime::LedgerPath(profile), ledger, error) == aip::gallery::runtime::ReadResult::Ok &&
        aip::gallery::ValidateLedger(ledger, error)) for (const auto& tile : ledger.tiles) {
        const auto id = aip::gallery::NativeTileId(ledger.groupId, tile.first);
        const auto configured=galleryExit.find(id);const bool clear=configured==galleryExit.end()?g_clearLiveTileOnShutdown.load():configured->second;
        if (!id.empty()&&(!shutdownPolicy||clear)) owned.insert(id);
    }
    return owned;
}

static bool Appx_Clear_LiveTileBare(std::wstring& failureMessage,bool shutdownPolicy=false)
{
    failureMessage.clear();

    std::wstring packageFullName;
    if (!CurrentProcessPackageFullName(packageFullName))
    {
        DWORD err = GetLastError();
        failureMessage = FormatWide(g_ui.liveTileClearRequiresIdentityDetails.c_str(), static_cast<unsigned long>(err));
        return false;
    }

    try
    {
        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);

        using namespace winrt::Windows::UI::Notifications;

        const auto updates = aip::AcquireUncachedActivationFactory<TileUpdateManager,
            winrt::Windows::UI::Notifications::ITileUpdateManagerStatics>();
        const auto nativeTiles = aip::AcquireUncachedActivationFactory<winrt::Windows::UI::StartScreen::SecondaryTile,
            winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
        const auto owned = OwnedSecondaryTileIdsForClear(shutdownPolicy);
        if(!shutdownPolicy||TileInstancePrimaryClearsOnShutdown()){
            TileUpdater updater = updates.CreateTileUpdaterForApplication();updater.Clear();
        }
        // The cached pin inventory may be unavailable in a one-shot packaged
        // clear helper. Query existence at this explicit cleanup boundary.
        const auto failures = aip::content::ClearOwnedSecondaryTileNotifications(owned,
            [&](const std::wstring& id) { return nativeTiles.Exists(id); },
            [&](const std::wstring& id) { updates.CreateTileUpdaterForSecondaryTile(id).Clear(); });
        for (const auto& id : failures) Log(L"[!] Could not clear owned tile notification on shutdown: %ls", id.c_str());
        // Return the primary result, as before: startup must restore the primary
        // once it was cleared even if a secondary failed. Each secondary failure
        // remains explicit in the log; all other owned targets were attempted.
        return true;
    }
    catch (const winrt::hresult_error& e)
    {
        failureMessage = FormatWide(g_ui.liveTileClearFailedHresult.c_str(), Hex64(static_cast<uint32_t>(e.code().value)).c_str());
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            failureMessage += FormatWide(g_ui.liveTileErrorMessageSuffix.c_str(), msg.c_str());
        return false;
    }
    catch (const std::exception& ex)
    {
        failureMessage = FormatWide(g_ui.liveTileClearFailedException.c_str(), ExceptionTextToWide(ex).c_str());
        return false;
    }
    catch (...)
    {
        failureMessage = FormatWide(g_ui.liveTileClearFailedException.c_str(), UnknownExceptionText().c_str());
        return false;
    }
}

static bool Appx_Update_LiveTileFromPendingXmlForActivationStub(std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring tileXml;
    const auto pendingPath = PendingLiveTileXmlPath();
    const auto pendingAttributes = GetFileAttributesW(pendingPath.c_str());
    const auto pendingError = pendingAttributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    if (pendingAttributes != INVALID_FILE_ATTRIBUTES ||
        (pendingError != ERROR_FILE_NOT_FOUND && pendingError != ERROR_PATH_NOT_FOUND)) {
        std::vector<BYTE> pendingBytes;
        const auto maximumLegacyBytes = 2u *
            (aip::tile_notification::MaximumXmlUtf8Bytes + aip::tile_notification::MaximumHeaderCharacters) + 4u;
        if (!aip::ReadWholeFileBytes(pendingPath, pendingBytes, maximumLegacyBytes) ||
            !DecodeTextBytes(pendingBytes, tileXml) || TrimCopy(tileXml).empty()) {
            failureMessage = L"The pending tile notification payload could not be read or was empty.";
            return false;
        }
        return Appx_Update_LiveTileXmlBare(tileXml, failureMessage);
    }

    // Manual tile clicks can happen without a freshly queued XML request. In
    // that case, update from the latest stable live-tile asset names if present,
    // then exit. The tile click is intentionally not treated as an app launch in
    // Win8/8.1 compatibility mode.
    std::vector<LiveTileUpdateAsset> fallbackAssets;
    for (const auto& t : g_liveTileAssets)
        fallbackAssets.push_back({ t.name, t.file, t.binding, t.file, t.w, t.h });

    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    tileXml = BuildLiveTileXml(GetExeDir(), manifestInfo, fallbackAssets);
    if (tileXml.empty())
    {
    failureMessage = g_ui.liveTileNoPendingXmlOrAssets;
    return false;
}

    return Appx_Update_LiveTileXmlBare(tileXml, failureMessage);
}

// PowerShellSingleQuotedString now lives in the shared
// dependencies/powershell_runner.inc (aip::); see its one call site below.

static std::wstring LiveTileActivationTraceLogPath()
{
    return ProductRuntimeFilePath(L".appxactivation.log");
}

static std::wstring LocalAppDataPath()
{
    DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed > 0)
    {
        std::vector<wchar_t> buffer(needed);
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), needed);
        if (n > 0 && n < needed)
            return std::wstring(buffer.data(), n);
    }

    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)) && known)
    {
        std::wstring result = known;
        CoTaskMemFree(known);
        return result;
    }

    return L"";
}

static std::wstring PackageLocalStateDir(const std::wstring& packageFamilyName)
{
    std::wstring base = LocalAppDataPath();
    if (base.empty() || packageFamilyName.empty())
        return L"";
    return base + L"\\Packages\\" + packageFamilyName + L"\\LocalState";
}

static std::wstring PackageLocalStatePath(const std::wstring& packageFamilyName, const std::wstring& fileName)
{
    std::wstring dir = PackageLocalStateDir(packageFamilyName);
    if (dir.empty())
        return L"";
    return dir + L"\\" + fileName;
}

static std::wstring PackageProductRuntimeFilePath(const std::wstring& packageFamilyName, const wchar_t* suffix)
{
    return PackageLocalStatePath(packageFamilyName, ProductRuntimeFileName(suffix));
}

static std::wstring PackageLiveTileActivationTraceLogPath(const std::wstring& packageFamilyName)
{
    return PackageProductRuntimeFilePath(packageFamilyName, L".appxactivation.log");
}

static std::wstring PackagePendingLiveTileXmlPath(const std::wstring& packageFamilyName)
{
    return PackageProductRuntimeFilePath(packageFamilyName, L".livetile.pending.xml");
}

static std::wstring PackagePendingLiveTileClearPath(const std::wstring& packageFamilyName)
{
    return PackageProductRuntimeFilePath(packageFamilyName, L".livetile.clear");
}

static bool TryGetFileSize64(const std::wstring& path, unsigned long long& sizeOut)
{
    sizeOut = 0;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER li{};
    BOOL ok = GetFileSizeEx(h, &li);
    CloseHandle(h);
    if (!ok || li.QuadPart < 0)
        return false;

    sizeOut = static_cast<unsigned long long>(li.QuadPart);
    return true;
}

static bool ReadUtf8FileTailAsWide(const std::wstring& path, unsigned long long offset, std::wstring& textOut)
{
    textOut.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER seek{};
    seek.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h, seek, nullptr, FILE_BEGIN))
    {
        CloseHandle(h);
        return false;
    }

    std::string bytes;
    char buffer[4096];
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(h, buffer, sizeof(buffer), &read, nullptr))
        {
            CloseHandle(h);
            return false;
        }
        if (read == 0)
            break;
        bytes.append(buffer, buffer + read);
        if (bytes.size() > 1024 * 1024)
            bytes.erase(0, bytes.size() - 1024 * 1024);
    }

    CloseHandle(h);
    if (bytes.empty())
        return true;

    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (needed <= 0)
    {
        needed = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (needed <= 0)
            return false;
        textOut.resize(needed);
        int written = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), &textOut[0], needed);
        if (written != needed)
        {
            textOut.clear();
            return false;
        }
        return true;
    }

    textOut.resize(needed);
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), &textOut[0], needed);
    if (written != needed)
    {
        textOut.clear();
        return false;
    }
    return true;
}

struct LiveTileSuccessLogWatch
{
    std::wstring path;
    unsigned long long previousSize = 0;
};

static bool TryParseLiveTileSuccessPid(
    const std::wstring& tail,
    size_t successPosition,
    DWORD& pidOut)
{
    pidOut = 0;
    size_t lineStart = tail.rfind(L'\n', successPosition);
    lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
    size_t pidPosition = tail.find(L"pid=", lineStart);
    if (pidPosition == std::wstring::npos || pidPosition >= successPosition)
        return false;

    size_t digit = pidPosition + 4;
    unsigned long long value = 0;
    bool foundDigit = false;
    while (digit < tail.size() && tail[digit] >= L'0' && tail[digit] <= L'9')
    {
        foundDigit = true;
        value = value * 10 + static_cast<unsigned long long>(tail[digit] - L'0');
        if (value > MAXDWORD)
            return false;
        ++digit;
    }

    if (!foundDigit || value == 0)
        return false;
    pidOut = static_cast<DWORD>(value);
    return true;
}

static bool LiveTileSuccessLogContainsNewSuccess(
    const LiveTileSuccessLogWatch& watch,
    const wchar_t* requiredSuccessText,
    DWORD* successPid)
{
    if (successPid)
        *successPid = 0;
    if (watch.path.empty())
        return false;

    unsigned long long currentSize = 0;
    if (!TryGetFileSize64(watch.path, currentSize) || currentSize <= watch.previousSize)
        return false;

    std::wstring tail;
    if (!ReadUtf8FileTailAsWide(watch.path, watch.previousSize, tail))
        return false;

    size_t successPosition = std::wstring::npos;
    if (requiredSuccessText && *requiredSuccessText)
    {
        successPosition = tail.find(requiredSuccessText);
    }
    else
    {
        size_t updatePosition = tail.find(L"Live Tile update completed under package identity");
        size_t clearPosition = tail.find(L"Live Tile cleared under package identity");
        if (updatePosition == std::wstring::npos)
            successPosition = clearPosition;
        else if (clearPosition == std::wstring::npos)
            successPosition = updatePosition;
        else
            successPosition = (std::min)(updatePosition, clearPosition);
    }

    if (successPosition == std::wstring::npos)
        return false;

    if (successPid)
        TryParseLiveTileSuccessPid(tail, successPosition, *successPid);
    return true;
}

static bool WaitForPackagedLiveTileHelperSuccess(
    const std::vector<LiveTileSuccessLogWatch>& watches,
    DWORD timeoutMs,
    const wchar_t* requiredSuccessText,
    DWORD* successPid)
{
    if (successPid)
        *successPid = 0;
    const DWORD stepMs = 100;
    DWORD waited = 0;
    while (waited <= timeoutMs)
    {
        for (const auto& watch : watches)
        {
            DWORD reportedPid = 0;
            if (LiveTileSuccessLogContainsNewSuccess(watch, requiredSuccessText, &reportedPid))
            {
                if (successPid)
                    *successPid = reportedPid;
                return true;
            }
        }

        if (waited == timeoutMs)
            break;
        Sleep(stepMs);
        waited += stepMs;
        if (waited > timeoutMs)
            waited = timeoutMs;
    }
    return false;
}

static bool WaitForPackagedLiveTileHelperSuccess(const std::vector<LiveTileSuccessLogWatch>& watches, DWORD timeoutMs)
{
    // Update callers must not acknowledge a broker that only consumed an old
    // clear request. Clear callers pass their own required success text.
    return WaitForPackagedLiveTileHelperSuccess(watches, timeoutMs,
        L"Live Tile update completed under package identity", nullptr);
}

static bool WaitForPackagedLiveTileHelperSuccess(unsigned long long previousLogSize, DWORD timeoutMs)
{
    std::vector<LiveTileSuccessLogWatch> watches;
    watches.push_back({ LiveTileActivationTraceLogPath(), previousLogSize });
    return WaitForPackagedLiveTileHelperSuccess(watches, timeoutMs);
}

static bool WaitForLiveTileHelperProcessExit(
    DWORD pid,
    DWORD timeoutMs,
    DWORD& waitResult,
    DWORD& error)
{
    waitResult = WAIT_FAILED;
    error = ERROR_SUCCESS;
    if (pid == 0)
    {
        error = ERROR_INVALID_DATA;
        return false;
    }

    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process)
    {
        error = GetLastError();
        // The helper can exit between appending its success line and this
        // OpenProcess call. A nonexistent PID therefore proves the boundary
        // required before package re-registration.
        if (error == ERROR_INVALID_PARAMETER)
        {
            waitResult = WAIT_OBJECT_0;
            error = ERROR_SUCCESS;
            return true;
        }
        if (error == ERROR_SUCCESS)
            error = ERROR_OPEN_FAILED;
        return false;
    }

    waitResult = WaitForSingleObject(process, timeoutMs);
    if (waitResult == WAIT_FAILED)
    {
        error = GetLastError();
        if (error == ERROR_SUCCESS)
            error = ERROR_GEN_FAILURE;
    }
    else if (waitResult == WAIT_TIMEOUT)
    {
        error = ERROR_TIMEOUT;
    }
    CloseHandle(process);
    return waitResult == WAIT_OBJECT_0;
}

static bool TryReadRegisteredPackageViaPowerShell(
    const ManifestDisplayInfo& manifestInfo,
    std::wstring& packageFamilyName,
    std::wstring& packageFullName,
    std::wstring& failureMessage)
{
    packageFamilyName.clear();
    packageFullName.clear();
    failureMessage.clear();

    std::wstring identity = aip::PowerShellSingleQuotedString(manifestInfo.identityName);
    std::wstring command =
        L"$p = Get-AppxPackage -Name " + identity + L" | Select-Object -First 1; "
        L"if ($null -ne $p) { Write-Output ($p.PackageFamilyName + '|@|' + $p.PackageFullName) }";

    if (!PS_Run(command))
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        failureMessage = g_ui.powerShellPackageLookupFailed;
        if (!TrimCopy(g_psMsg).empty())
            failureMessage += FormatWide(g_ui.liveTilePowerShellFailureDetailsSuffix.c_str(), TrimCopy(g_psMsg).c_str());
        if (!TrimCopy(g_psOut).empty())
            failureMessage += FormatWide(g_ui.liveTilePowerShellOutputDetailsSuffix.c_str(), TrimCopy(g_psOut).c_str());
        return false;
    }

    std::wstring out;
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        out = TrimCopy(g_psOut);
    }

    // Use the last non-empty line because PowerShell output can contain progress
    // or CLIXML-normalized noise before the actual Write-Output line.
    std::wstring selectedLine;
    size_t pos = 0;
    while (pos <= out.size())
    {
        size_t next = out.find_first_of(L"\r\n", pos);
        std::wstring line = TrimCopy(out.substr(pos, (next == std::wstring::npos) ? std::wstring::npos : next - pos));
        if (!line.empty())
            selectedLine = line;
        if (next == std::wstring::npos)
            break;
        pos = next + 1;
        while (pos < out.size() && (out[pos] == L'\r' || out[pos] == L'\n'))
            ++pos;
    }

    size_t sep = selectedLine.find(L"|@|");
    if (sep == std::wstring::npos)
    {
        failureMessage = FormatWide(g_ui.powerShellPackageLookupNoPackage.c_str(), manifestInfo.identityName.c_str());
        if (!out.empty())
            failureMessage += FormatWide(g_ui.liveTilePowerShellOutputDetailsSuffix.c_str(), out.c_str());
        return false;
    }

    packageFamilyName = TrimCopy(selectedLine.substr(0, sep));
    packageFullName = TrimCopy(selectedLine.substr(sep + 3));
    if (packageFamilyName.empty() || packageFullName.empty())
    {
        failureMessage = FormatWide(g_ui.powerShellPackageLookupIncomplete.c_str(), selectedLine.c_str());
        return false;
    }

    Log(g_ui.powerShellPackageLookupFound.c_str(), packageFullName.c_str());
    return true;
}

static bool FindRegisteredPackageFamilyNameForManifest(
    const ManifestDisplayInfo& manifestInfo,
    std::wstring& packageFamilyName,
    std::wstring& packageFullName,
    std::wstring& failureMessage)
{
    packageFamilyName.clear();
    packageFullName.clear();
    failureMessage.clear();

    std::wstring apiFailure;
    try
    {
        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);

        using namespace winrt::Windows::Management::Deployment;

        PackageManager pm;

        auto acceptPackage = [&](auto const& package, const wchar_t* source) -> bool
        {
            auto id = package.Id();
            packageFamilyName = id.FamilyName().c_str();
            packageFullName = id.FullName().c_str();
            if (packageFamilyName.empty())
                return false;
            Log(g_ui.liveTilePackageLookupFound.c_str(), source, packageFullName.c_str());
            return true;
        };

        auto enumeratePackages = [&]() -> bool
        {
            auto packages = pm.FindPackagesForUser(winrt::hstring());
            for (auto const& package : packages)
            {
                auto id = package.Id();
                std::wstring name = id.Name().c_str();
                if (!IEquals(name, manifestInfo.identityName))
                    continue;
                return acceptPackage(package, L"Enumerated");
            }
            return false;
        };

        auto directLookup = [&]() -> bool
        {
            auto namedPackages = pm.FindPackagesForUser(winrt::hstring(), winrt::hstring(manifestInfo.identityName));
            for (auto const& package : namedPackages)
            {
                if (acceptPackage(package, L"Direct WinRT"))
                    return true;
            }
            return false;
        };

        // Windows 8/8.1-style loose registrations on Windows 10 have repeatedly
        // shown that PackageManager's direct name-filtered lookup can return empty
        // even when the package is installed and activatable. Prefer enumeration for
        // these compatibility targets; keep the direct lookup first for normal Win10
        // Desktop Bridge packages where it is fast and reliable.
        if (ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10)
        {
            if (enumeratePackages())
                return true;
            if (directLookup())
                return true;
        }
        else
        {
            if (directLookup())
                return true;
            if (enumeratePackages())
                return true;
        }

        apiFailure = FormatWide(g_ui.liveTileWinRtPackageLookupNoPackage.c_str(), manifestInfo.identityName.c_str());
    }
    catch (const winrt::hresult_error& e)
    {
        apiFailure = FormatWide(g_ui.liveTileWinRtPackageLookupFailedHresult.c_str(), Hex64(static_cast<uint32_t>(e.code().value)).c_str());
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            apiFailure += FormatWide(g_ui.liveTileErrorMessageSuffix.c_str(), msg.c_str());
    }
    catch (...)
    {
        apiFailure = FormatWide(g_ui.liveTileWinRtPackageLookupFailedException.c_str(), UnknownExceptionText().c_str());
    }

    LogText(apiFailure);

    std::wstring psFailure;
    if (TryReadRegisteredPackageViaPowerShell(manifestInfo, packageFamilyName, packageFullName, psFailure))
        return true;

    failureMessage = FormatWide(g_ui.liveTilePackageLookupFallbackFailure.c_str(), apiFailure.c_str(), psFailure.c_str());
    return false;
}


static bool EnsurePackagedLiveTileUpdateStubRegistered(
    const ManifestDisplayInfo& manifestInfo,
    std::wstring& packageFamilyName,
    std::wstring& packageFullName,
    std::wstring& failureMessage)
{
    failureMessage.clear();
    packageFamilyName.clear();
    packageFullName.clear();

    std::wstring lookupFailure;
    std::wstring existingPackageFamilyName;
    std::wstring existingPackageFullName;
    bool hadExistingPackage = FindRegisteredPackageFamilyNameForManifest(manifestInfo, existingPackageFamilyName, existingPackageFullName, lookupFailure);

    static bool s_refreshedWin8BrokerRegistrationThisProcess = false;
    bool refreshExistingPackage = hadExistingPackage &&
        ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10 &&
        Win8LiveTileBrokerAppEnabled() &&
        !s_refreshedWin8BrokerRegistrationThisProcess;

    if (hadExistingPackage && !refreshExistingPackage)
    {
        packageFamilyName = existingPackageFamilyName;
        packageFullName = existingPackageFullName;
        return true;
    }

    if (refreshExistingPackage)
    {
        s_refreshedWin8BrokerRegistrationThisProcess = true;
        Log(g_ui.liveTileExistingPackageRefresh.c_str(),
            existingPackageFullName.c_str());
    }
    else
    {
        Log(g_ui.liveTilePackageLookupNotFound.c_str(), lookupFailure.c_str());
    }

    std::wstring manifestPath;
    bool manifestCreated = false;
    DWORD manifestError = ERROR_SUCCESS;
    if (!EnsureAppxManifest(manifestPath, manifestCreated, manifestError))
    {
        failureMessage = FormatWide(g_ui.liveTilePrepareManifestForHelperFailed.c_str(),
            manifestPath.c_str(), static_cast<unsigned long>(manifestError));
        LogText(failureMessage);
        return false;
    }

    if (manifestCreated)
        Log(g_ui.appxManifestCreated.c_str(), manifestPath.c_str());
    else
        Log(g_ui.appxManifestExisting.c_str(), manifestPath.c_str());

    LogText((Win8LiveTileBrokerAppEnabled() && ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10) ?
        g_ui.liveTileRegisteringManifestBroker :
        g_ui.liveTileRegisteringManifestHelper);
    std::wstring registrationFailure;
    bool registrationOk = false;
    if (ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10)
    {
        registrationOk = RegisterAppxManifest(manifestPath, registrationFailure);
    }
    else
    {
        // In Win8/8.1 compatibility mode on Windows 10, PackageManager::RegisterPackageAsync
        // has repeatedly rejected this loose manifest as "not in the package root", while
        // Add-AppxPackage -Register succeeds. Avoid the noisy COM failure/fallback path here.
        auto setPowerShellFailureMessage = [&]()
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            registrationFailure = g_psMsg.empty() ? g_ui.powerShellRegistrationFailed : g_psMsg;
        };
        std::wstring ps = aip::BuildAppxRegistrationScript(
            {manifestPath, true},
            aip::PowerShellSingleQuotedString);
        registrationOk = PS_Run(ps);
        if (!registrationOk)
        {
            setPowerShellFailureMessage();

            // Development-mode loose packages sometimes reject same-version re-registration
            // with 0x80073CFB even with -ForceUpdateFromAnyVersion. When broker mode is
            // enabled, stale registration is dangerous because the manifest executable may
            // still point at the older helper. In that one case, remove the current-user
            // dev package and immediately register the loose package again.
            if (refreshExistingPackage && !existingPackageFullName.empty())
            {
                Log(g_ui.liveTileRegistrationBlockedRemoving.c_str(),
                    existingPackageFullName.c_str());
                std::wstring retryPs =
                    L"Remove-AppxPackage -Package " +
                    aip::PowerShellSingleQuotedString(existingPackageFullName) +
                    L" -ErrorAction SilentlyContinue; " +
                    aip::BuildAppxRegistrationScript(
                        {manifestPath, true},
                        aip::PowerShellSingleQuotedString);
                registrationOk = PS_Run(retryPs);
                if (!registrationOk)
                    setPowerShellFailureMessage();
            }
        }
    }

    if (!registrationOk)
    {
        failureMessage = FormatWide(g_ui.liveTileHelperRegistrationFailed.c_str(), registrationFailure.c_str());
        if (hadExistingPackage)
        {
            Log(g_ui.liveTileContinueExistingPackage.c_str(),
                failureMessage.c_str(), existingPackageFullName.c_str());
            packageFamilyName = existingPackageFamilyName;
            packageFullName = existingPackageFullName;
            return true;
        }
        LogText(failureMessage);
        return false;
    }

    LogText(g_ui.liveTileHelperRegistrationVerifying);
    lookupFailure.clear();
    if (FindRegisteredPackageFamilyNameForManifest(manifestInfo, packageFamilyName, packageFullName, lookupFailure))
    {
        Log(g_ui.liveTileHelperPackageReady.c_str(), packageFullName.c_str());
        return true;
    }

    failureMessage = FormatWide(g_ui.liveTileHelperPackageMissingAfterRegister.c_str(), lookupFailure.c_str());
    LogText(failureMessage);
    return false;
}

#ifndef AO_NOERRORUI
#define AO_NOERRORUI static_cast<ACTIVATEOPTIONS>(0x00000002)
#endif


static bool ForceRegisterWin8HelperManifestForBackgroundTask(
    const ManifestDisplayInfo& manifestInfo,
    std::wstring& packageFamilyName,
    std::wstring& packageFullName,
    std::wstring& failureMessage)
{
    failureMessage.clear();
    packageFamilyName.clear();
    packageFullName.clear();

    std::wstring manifestPath;
    bool manifestCreated = false;
    DWORD manifestError = ERROR_SUCCESS;
    if (!EnsureAppxManifest(manifestPath, manifestCreated, manifestError))
    {
        failureMessage = FormatWide(g_ui.liveTilePrepareManifestForBackgroundTaskFailed.c_str(),
            manifestPath.c_str(), static_cast<unsigned long>(manifestError));
        return false;
    }

    if (manifestCreated)
        Log(g_ui.appxManifestCreated.c_str(), manifestPath.c_str());
    else
        Log(g_ui.appxManifestExisting.c_str(), manifestPath.c_str());

    static bool refreshedThisProcess = false;
    if (!refreshedThisProcess)
    {
        LogText(g_ui.liveTileRegisteringBackgroundTaskManifest);
        std::wstring ps = aip::BuildAppxRegistrationScript(
            {manifestPath, true},
            aip::PowerShellSingleQuotedString);
        if (!PS_Run(ps))
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            failureMessage = g_ui.liveTileBackgroundTaskRegistrationFailed;
            if (!TrimCopy(g_psMsg).empty())
                failureMessage += FormatWide(g_ui.liveTilePowerShellFailureDetailsSuffix.c_str(), TrimCopy(g_psMsg).c_str());
            if (!TrimCopy(g_psOut).empty())
                failureMessage += FormatWide(g_ui.liveTilePowerShellOutputDetailsSuffix.c_str(), TrimCopy(g_psOut).c_str());
            return false;
        }
        refreshedThisProcess = true;
        LogText(g_ui.liveTileBackgroundTaskRegistrationVerifying);
    }

    std::wstring lookupFailure;
    if (FindRegisteredPackageFamilyNameForManifest(manifestInfo, packageFamilyName, packageFullName, lookupFailure))
        return true;

    failureMessage = FormatWide(g_ui.liveTileBackgroundTaskPackageMissingAfterRegister.c_str(), lookupFailure.c_str());
    return false;
}

static std::wstring BackgroundAccessStatusText(winrt::Windows::ApplicationModel::Background::BackgroundAccessStatus status)
{
    return L"value " + std::to_wstring(static_cast<int>(status));
}
static const wchar_t* ApplicationTriggerResultName(winrt::Windows::ApplicationModel::Background::ApplicationTriggerResult result)
{
    using winrt::Windows::ApplicationModel::Background::ApplicationTriggerResult;
    switch (result)
    {
    case ApplicationTriggerResult::Allowed: return L"Allowed";
    case ApplicationTriggerResult::CurrentlyRunning: return L"CurrentlyRunning";
    case ApplicationTriggerResult::DisabledByPolicy: return L"DisabledByPolicy";
    case ApplicationTriggerResult::UnknownError: return L"UnknownError";
    default: return L"Unknown";
    }
}

static bool TriggerPackagedLiveTileBackgroundTask(
    const ManifestDisplayInfo& manifestInfo,
    std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring packageFamilyName;
    std::wstring packageFullName;
    if (!ForceRegisterWin8HelperManifestForBackgroundTask(manifestInfo, packageFamilyName, packageFullName, failureMessage))
        return false;

    unsigned long long activationLogSizeBefore = 0;
    TryGetFileSize64(LiveTileActivationTraceLogPath(), activationLogSizeBefore);

    try
    {
        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);
        using namespace winrt::Windows::ApplicationModel::Background;

        const auto backgroundTaskTimeout = std::chrono::milliseconds(5000);
        BackgroundAccessStatus access = aip::AwaitWinrtOperation(
            BackgroundExecutionManager::RequestAccessAsync(),
            backgroundTaskTimeout,
            L"Background task access request timed out.");
        Log(g_ui.liveTileBackgroundAccessRequestReturned.c_str(),
            BackgroundAccessStatusText(access).c_str(), static_cast<int>(access));

        const std::wstring taskName = ManifestLiveTileBackgroundTaskName();
        const std::wstring entryPoint = ManifestLiveTileBackgroundTaskClassId();

        for (auto const& item : BackgroundTaskRegistration::AllTasks())
        {
            auto registration = item.Value();
            if (IEquals(std::wstring(registration.Name().c_str()), taskName))
            {
                Log(g_ui.liveTileBackgroundTaskRemovingPrevious.c_str(), taskName.c_str());
                registration.Unregister(true);
            }
        }

        ApplicationTrigger trigger;
        BackgroundTaskBuilder builder;
        builder.Name(winrt::hstring(taskName));
        builder.TaskEntryPoint(winrt::hstring(entryPoint));
        builder.SetTrigger(trigger);
        auto registration = builder.Register();
        Log(g_ui.liveTileBackgroundTaskRegistered.c_str(),
            std::wstring(registration.Name().c_str()).c_str(), entryPoint.c_str());

        ApplicationTriggerResult result = aip::AwaitWinrtOperation(
            trigger.RequestAsync(),
            backgroundTaskTimeout,
            L"Background task activation request timed out.");
        Log(g_ui.liveTileBackgroundTaskRequestReturned.c_str(),
            ApplicationTriggerResultName(result), static_cast<int>(result));

        if (result != ApplicationTriggerResult::Allowed && result != ApplicationTriggerResult::CurrentlyRunning)
        {
            failureMessage = FormatWide(g_ui.liveTileBackgroundTaskRequestFailed.c_str(),
                ApplicationTriggerResultName(result));
            return false;
        }

        if (WaitForPackagedLiveTileHelperSuccess(activationLogSizeBefore, 10000))
        {
            LogText(g_ui.liveTileBackgroundTaskReportedSuccess);
            return true;
        }

        failureMessage = FormatWide(g_ui.liveTileBackgroundTaskNoSuccess.c_str(), ApplicationTriggerResultName(result));
        return false;
    }
    catch (const winrt::hresult_error& e)
    {
        failureMessage = FormatWide(g_ui.liveTileBackgroundTaskFailedHresult.c_str(),
            Hex64(static_cast<uint32_t>(e.code().value)).c_str());
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            failureMessage += FormatWide(g_ui.liveTileErrorMessageSuffix.c_str(), msg.c_str());
        return false;
    }
    catch (const std::exception& ex)
    {
        failureMessage = FormatWide(g_ui.liveTileBackgroundTaskFailedException.c_str(), ExceptionTextToWide(ex).c_str());
        return false;
    }
    catch (...)
    {
        failureMessage = FormatWide(g_ui.liveTileBackgroundTaskFailedException.c_str(), UnknownExceptionText().c_str());
        return false;
    }
}

static bool ActivatePackagedLiveTileUpdateOopServer(
    const ManifestDisplayInfo& manifestInfo,
    std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring packageFamilyName;
    std::wstring packageFullName;
    if (!EnsurePackagedLiveTileUpdateStubRegistered(manifestInfo, packageFamilyName, packageFullName, failureMessage))
        return false;

    std::wstring classId = ManifestLiveTileOopRuntimeClassId();

    HRESULT hrInit = RoInitialize(RO_INIT_MULTITHREADED);
    bool shouldUninit = SUCCEEDED(hrInit);
    if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
    {
        failureMessage = FormatWide(g_ui.liveTileOopRoInitializeFailed.c_str(),
            Hex64(static_cast<uint32_t>(hrInit)).c_str());
        return false;
    }

    unsigned long long activationLogSizeBefore = 0;
    TryGetFileSize64(LiveTileActivationTraceLogPath(), activationLogSizeBefore);

    HSTRING className = nullptr;
    HRESULT hr = WindowsCreateString(classId.c_str(), static_cast<UINT32>(classId.size()), &className);
    if (SUCCEEDED(hr) && className)
    {
        ::IInspectable* instance = nullptr;
        hr = RoActivateInstance(className, &instance);
        if (instance)
            instance->Release();
        WindowsDeleteString(className);
    }
    else if (SUCCEEDED(hr))
    {
        hr = E_POINTER;
    }

    if (shouldUninit)
        RoUninitialize();

    if (FAILED(hr))
    {
        failureMessage = FormatWide(g_ui.liveTileOopActivateFailed.c_str(),
            classId.c_str(), Hex64(static_cast<uint32_t>(hr)).c_str());
        return false;
    }

    Log(g_ui.liveTileOopActivated.c_str(),
        classId.c_str(), packageFullName.c_str());

    if (WaitForPackagedLiveTileHelperSuccess(activationLogSizeBefore, 5000))
    {
        LogText(g_ui.liveTileOopReportedSuccess);
        return true;
    }

    failureMessage = FormatWide(g_ui.liveTileOopNoSuccess.c_str(), classId.c_str());
    return false;
}

static bool ActivateRegisteredPackageApplication(
    const ManifestDisplayInfo& manifestInfo,
    const wchar_t* arguments,
    std::wstring& failureMessage,
    DWORD* activatedPid = nullptr)
{
    failureMessage.clear();
    if (activatedPid)
        *activatedPid = 0;

    std::wstring packageFamilyName;
    std::wstring packageFullName;
    if (!EnsurePackagedLiveTileUpdateStubRegistered(manifestInfo, packageFamilyName, packageFullName, failureMessage))
        return false;

    std::wstring appUserModelId = packageFamilyName + L"!" + manifestInfo.applicationId;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninit = SUCCEEDED(hrInit);
    if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
    {
        failureMessage = FormatWide(g_ui.liveTileActivationCoInitializeFailed.c_str(),
            Hex64(static_cast<uint32_t>(hrInit)).c_str());
        return false;
    }

    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_ApplicationActivationManager,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&manager));
    if (FAILED(hr) || !manager)
    {
        if (shouldUninit)
            CoUninitialize();
        failureMessage = FormatWide(g_ui.liveTileActivationManagerCreateFailed.c_str(),
            Hex64(static_cast<uint32_t>(hr)).c_str());
        return false;
    }

    DWORD pid = 0;
    hr = manager->ActivateApplication(appUserModelId.c_str(), arguments ? arguments : L"", AO_NOERRORUI, &pid);
    manager->Release();
    if (shouldUninit)
        CoUninitialize();

    if (FAILED(hr))
    {
        failureMessage = FormatWide(g_ui.liveTileActivateApplicationFailed.c_str(),
            appUserModelId.c_str(), Hex64(static_cast<uint32_t>(hr)).c_str());
        return false;
    }

    Log(g_ui.liveTileRegisteredPackageActivated.c_str(),
        appUserModelId.c_str(), packageFullName.c_str(), static_cast<unsigned long>(pid));
    if (activatedPid)
        *activatedPid = pid;
    return true;
}

static bool ActivatePackagedLiveTileUpdateStub(
    const ManifestDisplayInfo& manifestInfo,
    std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring packageFamilyName;
    std::wstring packageFullName;
    if (!EnsurePackagedLiveTileUpdateStubRegistered(manifestInfo, packageFamilyName, packageFullName, failureMessage))
        return false;

    std::wstring appUserModelId = packageFamilyName + L"!" + manifestInfo.applicationId;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninit = SUCCEEDED(hrInit);
    if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
    {
        failureMessage = FormatWide(g_ui.liveTileActivationCoInitializeFailed.c_str(),
            Hex64(static_cast<uint32_t>(hrInit)).c_str());
        return false;
    }

    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_ApplicationActivationManager,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&manager));
    if (FAILED(hr) || !manager)
    {
        if (shouldUninit)
            CoUninitialize();
        failureMessage = FormatWide(g_ui.liveTileActivationManagerCreateFailed.c_str(),
            Hex64(static_cast<uint32_t>(hr)).c_str());
        return false;
    }

    std::vector<LiveTileSuccessLogWatch> successWatches;
    auto addSuccessWatch = [&](const std::wstring& path)
    {
        if (path.empty())
            return;
        unsigned long long size = 0;
        TryGetFileSize64(path, size);
        successWatches.push_back({ path, size });
    };

    addSuccessWatch(LiveTileActivationTraceLogPath());

    std::wstring packageLogPath = PackageLiveTileActivationTraceLogPath(packageFamilyName);
    if (!packageLogPath.empty())
        addSuccessWatch(packageLogPath);

    std::wstring packageLocalState = PackageLocalStateDir(packageFamilyName);
    std::wstring packagePendingXml = PackagePendingLiveTileXmlPath(packageFamilyName);
    if (!packageLocalState.empty() && !packagePendingXml.empty())
    {
        SHCreateDirectoryExW(nullptr, packageLocalState.c_str(), nullptr);

        std::wstring pendingXmlText;
        if (ReadTextFile(PendingLiveTileXmlPath(), pendingXmlText) && !pendingXmlText.empty())
        {
            if (WriteUtf8BomTextFile(packagePendingXml, pendingXmlText))
                Log(g_ui.liveTilePendingXmlMirrored.c_str(), packagePendingXml.c_str());
            else
                Log(g_ui.liveTilePendingXmlMirrorFailed.c_str(), packagePendingXml.c_str(), static_cast<unsigned long>(GetLastError()));
        }
    }

    DWORD pid = 0;
    hr = manager->ActivateApplication(appUserModelId.c_str(), L"tile-update", AO_NOERRORUI, &pid);
    manager->Release();
    if (shouldUninit)
        CoUninitialize();

    if (FAILED(hr))
    {
        // Win8/8.1 compatibility mode uses a tiny non-RT EXE as a package-identity
        // tile-update helper. Windows can still start that EXE and let it update
        // the tile, but ActivateApplication may report that the RT activation
        // contract did not complete (commonly 0x8027025B, "the app didn't start").
        // Treat the activation as successful if the helper appended a success
        // line to the activation trace after this request was made.
        if (WaitForPackagedLiveTileHelperSuccess(successWatches, 5000))
        {
            Log(g_ui.liveTileActivateReturnedSuccessWithHelperSuccess.c_str(),
                appUserModelId.c_str(), Hex64(static_cast<uint32_t>(hr)).c_str());
            return true;
        }

        failureMessage = FormatWide(g_ui.liveTileActivateApplicationFailed.c_str(),
            appUserModelId.c_str(), Hex64(static_cast<uint32_t>(hr)).c_str());
        return false;
    }

    Log((Win8LiveTileBrokerAppEnabled() && ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10) ?
        g_ui.liveTilePackagedBrokerActivated.c_str() :
        g_ui.liveTilePackagedHelperActivated.c_str(),
        appUserModelId.c_str(), packageFullName.c_str(), static_cast<unsigned long>(pid));

    if (WaitForPackagedLiveTileHelperSuccess(successWatches, 5000))
    {
        // Maintainer guardrail: Packaged WinRT Live Tile broker reported success.
        LogText((Win8LiveTileBrokerAppEnabled() && ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10) ?
            g_ui.liveTilePackagedBrokerReportedSuccess :
            g_ui.liveTilePackagedHelperReportedSuccess);
        return true;
    }

    failureMessage = FormatWide(g_ui.liveTilePackagedHelperNoSuccess.c_str(),
        appUserModelId.c_str(),
        packageFullName.c_str(),
        static_cast<unsigned long>(pid));
    return false;
}

static bool Appx_Request_LiveTileUpdateViaPackagedActivation(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets,
    std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring tileXml = BuildLiveTileXml(exeDir, manifestInfo, liveTileAssets);
    if (tileXml.empty())
    {
        failureMessage = g_ui.liveTileUpdateNoUsableAssets;
        LogText(failureMessage);
        return false;
    }

    if (!WritePendingLiveTileXml(tileXml))
    {
        DWORD err = GetLastError();
        if (err == ERROR_SUCCESS)
            err = ERROR_WRITE_FAULT;
        failureMessage = FormatWide(g_ui.liveTilePendingXmlWriteFailed.c_str(),
            PendingLiveTileXmlPath().c_str(), static_cast<unsigned long>(err));
        LogText(failureMessage);
        return false;
    }

    Log(g_ui.liveTilePendingXmlWritten.c_str(), PendingLiveTileXmlPath().c_str());

    if (ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10)
    {
        if (Win8LiveTileBrokerAppEnabled())
            LogText(g_ui.liveTileHelperModeBroker);
        else
            LogText(g_ui.liveTileHelperModeLegacy);
    }

    std::wstring backgroundFailure;
    if (Win8LiveTileBackgroundTaskConfigured())
    {
        std::wstring backgroundTaskDllPath;
        if (!Win8LiveTileBackgroundTaskDllPresent(&backgroundTaskDllPath))
        {
            backgroundFailure = FormatWide(g_ui.win8LiveTileBackgroundTaskMissingDll.c_str(), backgroundTaskDllPath.c_str());
            LogText(backgroundFailure);
        }
        else
        {
            std::wstring currentPackageFullName;
            if (!CurrentProcessPackageFullName(currentPackageFullName))
            {
                backgroundFailure = g_ui.liveTileBackgroundTaskSkippedUnpackaged;
                LogText(g_ui.liveTileBackgroundTaskSkippedUnpackaged);
            }
            else if (TriggerPackagedLiveTileBackgroundTask(manifestInfo, backgroundFailure))
            {
                return true;
            }

            if (!backgroundFailure.empty())
            {
                Log(g_ui.liveTileBackgroundTaskUnavailable.c_str(), backgroundFailure.c_str());
            }
        }
    }
    else
    {
        // Background task helper is intentionally quiet when disabled; the broker
        // helper mode line above describes the active default path.
    }

    std::wstring oopFailure;
    if (Win8LiveTileOopHelperEnabled())
    {
        if (ActivatePackagedLiveTileUpdateOopServer(manifestInfo, oopFailure))
            return true;

        Log(g_ui.liveTileOopActivationFailedFallback.c_str(),
            oopFailure.c_str());
    }
    else
    {
        // OOP server helper is intentionally quiet when disabled.
    }

    std::wstring foregroundFailure;
    if (ActivatePackagedLiveTileUpdateStub(manifestInfo, foregroundFailure))
        return true;

    failureMessage = FormatWide(g_ui.liveTileActivationFailureSummary.c_str(),
        backgroundFailure.c_str(), oopFailure.c_str(), foregroundFailure.c_str());
    return false;
}

static bool Appx_Update_LiveTile(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets,
    std::wstring& failureMessage)
{
    failureMessage.clear();

    std::wstring packageFullName;
    if (!CurrentProcessPackageFullName(packageFullName))
    {
        failureMessage = g_ui.liveTileUpdateRequiresIdentity;
        LogText(failureMessage);
        return false;
    }
    Log(g_ui.liveTilePackageIdentity.c_str(), packageFullName.c_str());

    std::wstring tileXml = BuildLiveTileXml(exeDir, manifestInfo, liveTileAssets);
    if (tileXml.empty())
    {
        failureMessage = g_ui.liveTileUpdateNoUsableAssets;
        LogText(failureMessage);
        return false;
    }

    try
    {
        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);

        if (!aip::tile_notification::DeliverWinrt(tileXml, failureMessage)) {
            LogText(failureMessage);
            return false;
        }

        LogText(g_ui.liveTileUpdateSuccess);
        return true;
    }
    catch (const winrt::hresult_error& e)
    {
        Log(g_ui.liveTileUpdateException.c_str(), static_cast<unsigned int>(e.code().value));
        if (!std::wstring(e.message().c_str()).empty())
            Log(g_ui.liveTileUpdateMessage.c_str(), e.message().c_str());
        failureMessage = g_ui.liveTileUpdateFailed;
        return false;
    }
    catch (const std::exception& ex)
    {
        std::wstring message = ExceptionTextToWide(ex);
        Log(g_ui.liveTileUpdateMessage.c_str(), message.c_str());
        failureMessage = g_ui.liveTileUpdateFailed;
        return false;
    }
    catch (...)
    {
        Log(g_ui.liveTileUpdateMessage.c_str(), UnknownExceptionText().c_str());
        failureMessage = g_ui.liveTileUpdateFailed;
        return false;
    }
}

static bool Appx_Clear_Or_Request_LiveTile(std::wstring& failureMessage,bool shutdownPolicy)
{
    failureMessage.clear();

    if (CurrentProcessHasPackageIdentity())
        return Appx_Clear_LiveTileBare(failureMessage,shutdownPolicy);

    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows10 ||
        target == ManifestCompatibilityTarget::Windows81 ||
        target == ManifestCompatibilityTarget::Windows8)
    {
        ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
        std::wstring packageFamilyName;
        std::wstring packageFullName;
        if (!EnsurePackagedLiveTileUpdateStubRegistered(manifestInfo, packageFamilyName, packageFullName, failureMessage))
            return false;

        std::vector<LiveTileSuccessLogWatch> successWatches;
        auto addSuccessWatch = [&](const std::wstring& path)
        {
            if (path.empty())
                return;
            unsigned long long size = 0;
            TryGetFileSize64(path, size);
            successWatches.push_back({ path, size });
        };
        addSuccessWatch(LiveTileActivationTraceLogPath());
        addSuccessWatch(PackageLiveTileActivationTraceLogPath(packageFamilyName));

        if (target == ManifestCompatibilityTarget::Windows81 || target == ManifestCompatibilityTarget::Windows8)
        {
            bool requestWritten = WritePendingLiveTileClearRequest();

            std::wstring packageLocalState = PackageLocalStateDir(packageFamilyName);
            std::wstring packageClear = PackagePendingLiveTileClearPath(packageFamilyName);
            bool packageRequestWritten = false;
            if (!packageLocalState.empty() && !packageClear.empty())
            {
                SHCreateDirectoryExW(nullptr, packageLocalState.c_str(), nullptr);
                packageRequestWritten = WriteUtf8BomTextFile(packageClear, L"clear");
            }

            if (!requestWritten && !packageRequestWritten)
            {
                DWORD err = GetLastError();
                if (err == ERROR_SUCCESS)
                    err = ERROR_WRITE_FAULT;
                failureMessage = FormatWide(
                    g_ui.liveTileClearRequestWriteFailed.c_str(),
                    PendingLiveTileClearPath().c_str(),
                    static_cast<unsigned long>(err));
                return false;
            }
        }

        DWORD activatedPid = 0;
        std::wstring activationFailure;
        bool activated = ActivateRegisteredPackageApplication(
            manifestInfo,
            L"tile-clear",
            activationFailure,
            &activatedPid);

        DWORD reportedPid = 0;
        bool helperReportedSuccess = WaitForPackagedLiveTileHelperSuccess(
            successWatches,
            5000,
            L"Live Tile cleared under package identity",
            &reportedPid);

        if (!helperReportedSuccess)
        {
            if (!activated)
            {
                failureMessage = activationFailure;
                return false;
            }

            std::wstring appUserModelId = packageFamilyName + L"!" + manifestInfo.applicationId;
            failureMessage = FormatWide(
                g_ui.liveTileClearHelperNoSuccess.c_str(),
                appUserModelId.c_str(),
                packageFullName.c_str(),
                static_cast<unsigned long>(activatedPid));
            return false;
        }

        std::wstring appUserModelId = packageFamilyName + L"!" + manifestInfo.applicationId;
        auto waitForHelperExit = [&](DWORD pid)
        {
            DWORD waitResult = WAIT_FAILED;
            DWORD waitError = ERROR_SUCCESS;
            if (WaitForLiveTileHelperProcessExit(pid, 5000, waitResult, waitError))
                return true;

            failureMessage = FormatWide(
                g_ui.liveTileClearHelperExitFailed.c_str(),
                appUserModelId.c_str(),
                packageFullName.c_str(),
                static_cast<unsigned long>(pid),
                static_cast<unsigned long>(waitResult),
                static_cast<unsigned long>(waitError));
            return false;
        };

        DWORD helperPid = reportedPid != 0 ? reportedPid : activatedPid;
        if (!waitForHelperExit(helperPid))
            return false;
        if (activatedPid != 0 && activatedPid != helperPid && !waitForHelperExit(activatedPid))
            return false;
        return true;
    }

    failureMessage = g_ui.liveTileUpdateRequiresIdentity;
    return false;
}

static bool Appx_Clear_Or_Request_LiveTile(std::wstring& failureMessage)
{
    return Appx_Clear_Or_Request_LiveTile(failureMessage,false);
}

static bool Appx_Update_Or_Request_LiveTile(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets,
    std::wstring& failureMessage)
{
    if (CurrentProcessHasPackageIdentity())
        return Appx_Update_LiveTile(exeDir, manifestInfo, liveTileAssets, failureMessage);

    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows81 || target == ManifestCompatibilityTarget::Windows8)
    {
        LogText(g_ui.liveTileRequestThroughActivationStub);
        return Appx_Request_LiveTileUpdateViaPackagedActivation(exeDir, manifestInfo, liveTileAssets, failureMessage);
    }

    return Appx_Update_LiveTile(exeDir, manifestInfo, liveTileAssets, failureMessage);
}


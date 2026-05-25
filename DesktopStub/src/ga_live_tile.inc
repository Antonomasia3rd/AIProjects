// -----------------------------------------------------------------------------
// Live Tile update
// -----------------------------------------------------------------------------
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

    int need = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return path;

    std::vector<char> bytes(static_cast<size_t>(need));
    if (WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), bytes.data(), need, nullptr, nullptr) <= 0)
        return path;

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

static std::wstring LiveTileAssetUriIfValid(const std::wstring& exeDir, const std::wstring& manifestAssetPath)
{
    if (!IsManifestRelativeAssetPath(manifestAssetPath))
        return L"";

    std::wstring fullPath = ResolveManifestAssetPath(exeDir, manifestAssetPath);
    if (!IsValidGeneratedPng(fullPath))
        return L"";

    return ManifestAssetUri(manifestAssetPath);
}

static void AppendLiveTileImageBinding(
    std::wstring& xml,
    const wchar_t* templateName,
    const std::wstring& imageUri,
    int overlay)
{
    if (!templateName || !*templateName || imageUri.empty())
        return;

    xml += L"    <binding template=\"";
    xml += templateName;
    xml += L"\">\r\n";
    xml += L"      <image src=\"";
    xml += XmlEscape(imageUri);
    xml += L"\" placement=\"background\" hint-overlay=\"";
    xml += IntToWString(overlay);
    xml += L"\"/>\r\n";
    xml += L"    </binding>\r\n";
}

static std::wstring BuildLiveTileXml(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets)
{
    (void)manifestInfo;

    std::wstring xml;
    bool hasUsableAsset = false;
    xml += L"<tile>\r\n";
    xml += L"  <visual branding=\"nameAndLogo\">\r\n";

    for (const auto& asset : liveTileAssets)
    {
        std::wstring uri = LiveTileAssetUriIfValid(exeDir, asset.file);
        if (!uri.empty())
            hasUsableAsset = true;
        AppendLiveTileImageBinding(xml, asset.binding, uri, 0);
    }

    if (!hasUsableAsset)
        return L"";

    xml += L"  </visual>\r\n";
    xml += L"</tile>";
    return xml;
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

        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;

        XmlDocument xml;
        xml.LoadXml(winrt::hstring(tileXml));

        TileNotification notification(xml);
        TileUpdater updater = TileUpdateManager::CreateTileUpdaterForApplication();
        updater.EnableNotificationQueue(false);
        updater.Update(notification);

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

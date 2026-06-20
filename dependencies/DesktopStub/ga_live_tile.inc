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
    if (!templateName || !*templateName || imageUri.empty())
        return;

    xml += L"    <binding template=\"";
    xml += templateName;
    xml += L"\">\r\n";
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

    std::wstring tileText = TileTextXmlText();
    std::wstring secondaryText = TileTextXmlSecondaryText();
    std::wstring badgeText = TileTextXmlBadgeText();
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
        xml += LiveTileBrandingXmlValue();
        xml += L"\">\r\n";
    }
    else if (LiveTileUsesWindows81PresetXml())
    {
        xml += L"  <visual version=\"4\" branding=\"";
        xml += LiveTileBrandingXmlValue();
        xml += L"\">\r\n";
    }
    else if (ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows81)
        xml += L"  <visual version=\"2\">\r\n";
    else
        xml += L"  <visual>\r\n";

    for (const auto& asset : liveTileAssets)
    {
        std::wstring uri = LiveTileAssetUriIfValid(exeDir, asset.file, asset.w, asset.h);
        if (LiveTileUsesWindows81PresetXml())
        {
            if (AppendWindows81PresetLiveTileBinding(xml, asset.binding, asset.name, uri))
                hasUsableBinding = true;
            continue;
        }

        const wchar_t* templateName = LiveTileTemplateForTarget(asset.binding);
        if (!templateName || uri.empty())
            continue;

        AppendLiveTileImageBinding(xml, templateName, uri, 0, asset.name);
        hasUsableBinding = true;
    }

    if (!hasUsableBinding)
        return L"";

    xml += L"  </visual>\r\n";
    xml += L"</tile>";
    return xml;
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
    if (tileXml.empty())
        return false;
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

        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;

        XmlDocument xml;
        xml.LoadXml(winrt::hstring(tileXml));

        TileNotification notification(xml);
        TileUpdater updater = TileUpdateManager::CreateTileUpdaterForApplication();
        updater.EnableNotificationQueue(false);
        updater.Update(notification);
        return true;
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

static bool Appx_Clear_LiveTileBare(std::wstring& failureMessage)
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

        TileUpdater updater = TileUpdateManager::CreateTileUpdaterForApplication();
        updater.Clear();
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
    if (ReadTextFile(PendingLiveTileXmlPath(), tileXml) && !TrimCopy(tileXml).empty())
        return Appx_Update_LiveTileXmlBare(tileXml, failureMessage);

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

static std::wstring PowerShellSingleQuotedString(const std::wstring& value)
{
    std::wstring quoted = L"'";
    for (wchar_t ch : value)
    {
        if (ch == L'\'')
            quoted += L"''";
        else
            quoted.push_back(ch);
    }
    quoted += L"'";
    return quoted;
}


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

static bool LiveTileSuccessLogContainsNewSuccess(const LiveTileSuccessLogWatch& watch)
{
    if (watch.path.empty())
        return false;

    unsigned long long currentSize = 0;
    if (!TryGetFileSize64(watch.path, currentSize) || currentSize <= watch.previousSize)
        return false;

    std::wstring tail;
    return ReadUtf8FileTailAsWide(watch.path, watch.previousSize, tail) &&
        (tail.find(L"Live Tile update completed under package identity") != std::wstring::npos ||
            tail.find(L"Live Tile cleared under package identity") != std::wstring::npos);
}

static bool WaitForPackagedLiveTileHelperSuccess(const std::vector<LiveTileSuccessLogWatch>& watches, DWORD timeoutMs)
{
    const DWORD stepMs = 100;
    DWORD waited = 0;
    while (waited <= timeoutMs)
    {
        for (const auto& watch : watches)
        {
            if (LiveTileSuccessLogContainsNewSuccess(watch))
                return true;
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

static bool WaitForPackagedLiveTileHelperSuccess(unsigned long long previousLogSize, DWORD timeoutMs)
{
    std::vector<LiveTileSuccessLogWatch> watches;
    watches.push_back({ LiveTileActivationTraceLogPath(), previousLogSize });
    return WaitForPackagedLiveTileHelperSuccess(watches, timeoutMs);
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

    std::wstring identity = PowerShellSingleQuotedString(manifestInfo.identityName);
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
        std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
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
                std::wstring retryPs = L"Remove-AppxPackage -Package \"" + existingPackageFullName +
                    L"\" -ErrorAction SilentlyContinue; Add-AppxPackage -Register \"" + manifestPath +
                    L"\" -ForceUpdateFromAnyVersion";
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
        std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
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

        BackgroundAccessStatus access = BackgroundExecutionManager::RequestAccessAsync().get();
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

        ApplicationTriggerResult result = trigger.RequestAsync().get();
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

static bool Appx_Clear_Or_Request_LiveTile(std::wstring& failureMessage)
{
    failureMessage.clear();

    if (CurrentProcessHasPackageIdentity())
        return Appx_Clear_LiveTileBare(failureMessage);

    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows81 || target == ManifestCompatibilityTarget::Windows8)
    {
        ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
        std::wstring packageFamilyName;
        std::wstring packageFullName;
        if (!EnsurePackagedLiveTileUpdateStubRegistered(manifestInfo, packageFamilyName, packageFullName, failureMessage))
            return false;

        WritePendingLiveTileClearRequest();

        std::wstring packageLocalState = PackageLocalStateDir(packageFamilyName);
        std::wstring packageClear = PackagePendingLiveTileClearPath(packageFamilyName);
        if (!packageLocalState.empty() && !packageClear.empty())
        {
            SHCreateDirectoryExW(nullptr, packageLocalState.c_str(), nullptr);
            WriteUtf8BomTextFile(packageClear, L"clear");
        }

        return ActivateRegisteredPackageApplication(manifestInfo, L"tile-clear", failureMessage);
    }

    failureMessage = g_ui.liveTileUpdateRequiresIdentity;
    return false;
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


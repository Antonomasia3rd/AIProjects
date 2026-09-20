// Use DesktopStub's shared runner: bounded draining, a fixed output cap, pinned
// PowerShell executable, and finite timeout/termination waits. Product-specific
// logging and return conventions remain in this small adapter.
using aip::PowerShellSingleQuotedString;

static bool RunPowerShellCommand(const std::wstring& command, std::wstring* output, DWORD* exitCode)
{
    aip::PowerShellRunOptions options;
    options.powerShellExe = aip::DefaultPowerShellExe();
    options.timeoutMs = POWERSHELL_COMMAND_TIMEOUT_MS;
    options.pollMs = POWERSHELL_POLL_MS;
    options.terminateWaitMs = POWERSHELL_TERMINATE_WAIT_MS;
    options.outputLimitBytes = POWERSHELL_OUTPUT_LIMIT_BYTES;
    const auto result = aip::RunPowerShellCaptured(L"$ErrorActionPreference = 'Stop'; " + command, options);
    DWORD code = result.exitCode;
    if (!result.ok && code == 0) code = result.waitError != ERROR_SUCCESS ? result.waitError : ERROR_GEN_FAILURE;
    if (exitCode) *exitCode = code;
    if (output) {
        *output = result.rawOutput;
        if (!result.started) *output += L"\r\n" + result.failedCall + L": " + aip::GetLastErrorText(result.failedCallError);
        if (result.outputLimitExceeded) *output += L"\r\nPowerShell output was truncated after " +
            std::to_wstring(POWERSHELL_OUTPUT_LIMIT_BYTES) + L" bytes; the output limit stopped the operation.";
        if (result.timedOut) *output += L"\r\nPowerShell command timed out after " +
            std::to_wstring(POWERSHELL_COMMAND_TIMEOUT_MS) + L" ms.";
        if ((result.timedOut || result.outputLimitExceeded || result.waitFailed) && !result.terminateConfirmed)
            *output += L"\r\nProcess termination was not confirmed. Package registration may still be in progress; check before retrying.";
    }
    if (!result.started) Log(L"%s failed: %s", result.failedCall.c_str(), aip::GetLastErrorText(result.failedCallError).c_str());
    if (result.waitFailed) Log(L"PowerShell wait failed: %s", aip::GetLastErrorText(result.waitError).c_str());
    return result.started;
}
static bool WriteUtf8File(const std::wstring& path, const std::wstring& text)
{
    if (!aip::WriteTextFileUtf8Bom(path, text))
    {
        Log(L"Could not write %s: %s", path.c_str(), GetLastErrorText(GetLastError()).c_str());
        return false;
    }
    return true;
}

static bool GetPngEncoderClsid(CLSID* clsid)
{
    UINT count = 0;
    UINT size = 0;
    GetImageEncodersSize(&count, &size);
    if (size == 0)
    {
        return false;
    }

    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, size, encoders) != Ok)
    {
        return false;
    }

    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0)
        {
            *clsid = encoders[i].Clsid;
            return true;
        }
    }

    return false;
}

static bool SaveLogoPng(const std::wstring& path, int width, int height)
{
    EnsureDirectory(GetDirectoryName(path));

    Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    SolidBrush background(Color(255, 27, 31, 36));
    graphics.FillRectangle(&background, 0, 0, width, height);

    int accentSize = std::max(4, std::min(width, height) / 6);
    SolidBrush accent(Color(255, 75, 161, 255));
    graphics.FillEllipse(&accent, width - accentSize * 2, accentSize, accentSize, accentSize);

    const bool wideLogo = width >= height * 2;
    const wchar_t* logoText = wideLogo ? L"Now Playing" : L"NP";
    REAL fontSize = static_cast<REAL>(wideLogo
        ? std::max(16, std::min(height - 20, width / 10))
        : std::max(14, std::min(width, height) / 2));
    FontFamily fontFamily(L"Segoe UI Semibold");
    Font font(&fontFamily, fontSize, FontStyleRegular, UnitPixel);
    SolidBrush text(Color(255, 245, 247, 250));
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    RectF textRect(0.0f, 0.0f, static_cast<REAL>(width), static_cast<REAL>(height));
    if (!wideLogo)
    {
        textRect = RectF(0.0f, static_cast<REAL>(height * 0.04f), static_cast<REAL>(width - accentSize), static_cast<REAL>(height * 0.92f));
    }
    graphics.DrawString(logoText, -1, &font, textRect, &format, &text);

    CLSID pngClsid = {};
    if (!GetPngEncoderClsid(&pngClsid))
    {
        Log(L"PNG encoder was not found.");
        return false;
    }

    std::wstring temporaryPath = BuildTemporarySiblingPath(path);
    DeleteFileW(temporaryPath.c_str());
    Status status = bitmap.Save(temporaryPath.c_str(), &pngClsid, nullptr);
    if (status != Ok)
    {
        DeleteFileW(temporaryPath.c_str());
        Log(L"Could not save %s as PNG. GDI+ status=%d", path.c_str(), static_cast<int>(status));
        return false;
    }
    if (!CommitTemporaryFile(temporaryPath, path))
    {
        Log(L"Could not replace %s after rendering: %s",
            path.c_str(),
            GetLastErrorText(GetLastError()).c_str());
        return false;
    }
    return true;
}

static std::wstring DefaultAppxManifestText()
{
    return
        L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        L"<Package\r\n"
        L"  xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"\r\n"
        L"  xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"\r\n"
        L"  xmlns:desktop=\"http://schemas.microsoft.com/appx/manifest/desktop/windows10\"\r\n"
        L"  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"\r\n"
        L"  IgnorableNamespaces=\"uap desktop rescap\">\r\n\r\n"
        L"  <Identity\r\n"
        L"    Name=\"NowPlayingTile.App\"\r\n"
        L"    Publisher=\"CN=NowPlayingTile\"\r\n"
        L"    Version=\"1.0.0.0\" />\r\n\r\n"
        L"  <Properties>\r\n"
        L"    <DisplayName>Now Playing Tile</DisplayName>\r\n"
        L"    <PublisherDisplayName>Now Playing Tile</PublisherDisplayName>\r\n"
        L"    <Logo>Assets\\StoreLogo.png</Logo>\r\n"
        L"  </Properties>\r\n\r\n"
        L"  <Dependencies>\r\n"
        L"    <TargetDeviceFamily\r\n"
        L"      Name=\"Windows.Desktop\"\r\n"
        L"      MinVersion=\"10.0.19041.0\"\r\n"
        L"      MaxVersionTested=\"10.0.22621.0\" />\r\n"
        L"  </Dependencies>\r\n\r\n"
        L"  <Applications>\r\n"
        L"    <Application\r\n"
        L"      Id=\"App\"\r\n"
        L"      Executable=\"NowPlayingTile.exe\"\r\n"
        L"      EntryPoint=\"Windows.FullTrustApplication\">\r\n"
        L"      <uap:VisualElements\r\n"
        L"        DisplayName=\"Now Playing Tile\"\r\n"
        L"        Description=\"Shows the current SMTC media session on a live tile.\"\r\n"
        L"        BackgroundColor=\"#1B1F24\"\r\n"
        L"        Square44x44Logo=\"Assets\\Square44x44Logo.png\"\r\n"
        L"        Square150x150Logo=\"Assets\\Square150x150Logo.png\"\r\n"
        L"        AppListEntry=\"default\">\r\n"
        L"        <uap:DefaultTile\r\n"
        L"          Wide310x150Logo=\"Assets\\Wide310x150Logo.png\"\r\n"
        L"          Square310x310Logo=\"Assets\\Square310x310Logo.png\"\r\n"
        L"          Square71x71Logo=\"Assets\\Square71x71Logo.png\"\r\n"
        L"          ShortName=\"Now Playing\" />\r\n"
        L"      </uap:VisualElements>\r\n"
        L"      <Extensions>\r\n"
        + aip::BuildDesktopStartupExtension(STARTUP_TASK_ID, L"NowPlayingTile.exe", APP_DISPLAY_NAME,
            [](const std::wstring& value) { return aip::XmlEscape(value); }) +
        L"      </Extensions>\r\n"
        L"    </Application>\r\n"
        L"  </Applications>\r\n\r\n"
        L"  <Capabilities>\r\n"
        L"    <rescap:Capability Name=\"runFullTrust\" />\r\n"
        L"  </Capabilities>\r\n"
        L"</Package>\r\n";
}

static bool ManifestDeclaresPackagedStartupTask(const std::wstring& path)
{
    std::vector<BYTE> bytes;
    std::wstring text;
    return aip::ReadWholeFileBytes(path, bytes) &&
        aip::DecodeTextBytes(bytes, text) &&
        text.find(L"Category=\"windows.startupTask\"") != std::wstring::npos &&
        text.find(L"TaskId=\"NowPlayingTileStartup\"") != std::wstring::npos;
}

static bool EnsurePackageFiles(bool forceRewrite)
{
    bool ok = true;
    std::wstring assetsDir = PathJoin(g_exeDir, L"Assets");
    EnsureDirectory(assetsDir);

    struct AssetSpec
    {
        const wchar_t* name;
        int width;
        int height;
    };

    const AssetSpec assets[] = {
        { L"StoreLogo.png", 50, 50 },
        { L"Square44x44Logo.png", 44, 44 },
        { L"Square71x71Logo.png", 71, 71 },
        { L"Square150x150Logo.png", 150, 150 },
        { L"Wide310x150Logo.png", 310, 150 },
        { L"Square310x310Logo.png", 310, 310 },
    };

    for (const auto& asset : assets)
    {
        std::wstring path = PathJoin(assetsDir, asset.name);
        if (forceRewrite || !FileExists(path))
        {
            ok = SaveLogoPng(path, asset.width, asset.height) && ok;
        }
    }

    std::wstring manifestPath = PathJoin(g_exeDir, L"AppxManifest.xml");
    if (forceRewrite ||
        !FileExists(manifestPath) ||
        !ManifestDeclaresPackagedStartupTask(manifestPath))
    {
        ok = WriteUtf8File(manifestPath, DefaultAppxManifestText()) && ok;
    }

    return ok;
}

static int ShowPowerShellResult(const wchar_t* title, bool started, DWORD exitCode, const std::wstring& output)
{
    std::wstring message;
    if (!started)
    {
        message = L"Could not start PowerShell. See NowPlayingTile.log for details.";
        MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
        return 1;
    }

    if (exitCode != 0)
    {
        message = L"Command failed with exit code " + std::to_wstring(exitCode) + L".\r\n\r\n" + output;
        MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
        return static_cast<int>(exitCode);
    }

    message = output.empty() ? L"Done." : output;
    MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONINFORMATION);
    return 0;
}

static int RegisterDevelopmentPackage(bool quiet)
{
    if (!EnsurePackageFiles(true))
    {
        Log(L"Could not generate AppxManifest.xml or tile assets.");
        if (!quiet)
        {
            MessageBoxW(nullptr, L"Could not generate AppxManifest.xml or tile assets. See NowPlayingTile.log for details.", APP_DISPLAY_NAME, MB_OK | MB_ICONERROR);
        }
        return 1;
    }

    std::wstring manifest = PathJoin(g_exeDir, L"AppxManifest.xml");
    std::wstring command =
        aip::BuildAppxRegistrationScript({manifest, true}, aip::PowerShellSingleQuotedString) +
        L"$pkg = Get-AppxPackage -Name " + aip::PowerShellSingleQuotedString(PACKAGE_NAME) + L"; "
        L"if ($null -eq $pkg) { throw 'Package registration completed, but Get-AppxPackage could not find NowPlayingTile.App.' }; "
        L"'Registered package: ' + $pkg.PackageFullName; "
        L"'Launch target: shell:AppsFolder\\' + $pkg.PackageFamilyName + '!App'";

    std::wstring output;
    DWORD exitCode = 1;
    bool started = RunPowerShellCommand(command, &output, &exitCode);
    Log(L"RegisterDevelopmentPackage exit=%lu output=%s", exitCode, output.c_str());
    if (quiet)
    {
        return started && exitCode == 0 ? 0 : static_cast<int>(exitCode == 0 ? 1 : exitCode);
    }
    return ShowPowerShellResult(L"NowPlayingTile registration", started, exitCode, output);
}

static int UnregisterDevelopmentPackage(bool quiet)
{
    std::wstring command =
        L"$pkg = Get-AppxPackage -Name " + aip::PowerShellSingleQuotedString(PACKAGE_NAME) + L"; "
        L"if ($null -eq $pkg) { 'NowPlayingTile.App is not registered.'; exit 0 }; "
        L"Remove-AppxPackage -Package $pkg.PackageFullName; "
        L"'Removed package: ' + $pkg.PackageFullName";

    std::wstring output;
    DWORD exitCode = 1;
    bool started = RunPowerShellCommand(command, &output, &exitCode);
    Log(L"UnregisterDevelopmentPackage exit=%lu output=%s", exitCode, output.c_str());
    if (quiet)
    {
        return started && exitCode == 0 ? 0 : static_cast<int>(exitCode == 0 ? 1 : exitCode);
    }
    return ShowPowerShellResult(L"NowPlayingTile unregistration", started, exitCode, output);
}

static std::wstring PackagedLaunchArguments(const AppOptions& options)
{
    std::wstring arguments;
    auto append = [&](const std::wstring& value)
    {
        if (!arguments.empty())
        {
            arguments.push_back(L' ');
        }
        arguments += value;
    };

    if (options.updateOnce)
    {
        append(L"--once");
    }
    if (options.showWidget)
    {
        append(L"--widget");
    }
    if (options.requestExit)
    {
        append(L"--exit");
    }
    if (options.allowMultiple)
    {
        append(L"--allow-multiple");
    }
    for (const auto& setting : options.settings)
    {
        append(L"--set");
        append(aip::QuoteCommandLineArg(
            setting.section + L"." + setting.key + L"=" + setting.value));
    }
    return arguments;
}

static int LaunchPackagedInstance(bool quiet, const std::wstring& arguments)
{
    std::wstring command =
        L"$pkg = Get-AppxPackage -Name " + aip::PowerShellSingleQuotedString(PACKAGE_NAME) + L"; "
        L"if ($null -eq $pkg) { throw 'NowPlayingTile.App is not registered. Run NowPlayingTile.exe --register first.' }; "
        L"$pkg.PackageFamilyName + '!App'";

    std::wstring output;
    DWORD exitCode = 1;
    bool started = RunPowerShellCommand(command, &output, &exitCode);
    std::wstring appUserModelId = Trim(output);
    if (!started || exitCode != 0 || appUserModelId.empty())
    {
        Log(L"Resolve packaged AUMID exit=%lu output=%s", exitCode, output.c_str());
        if (quiet)
        {
            return started && exitCode != 0 ? static_cast<int>(exitCode) : 1;
        }
        return ShowPowerShellResult(L"NowPlayingTile launch", started, exitCode == 0 ? 1 : exitCode, output);
    }

    HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool uninitializeApartment = SUCCEEDED(apartmentResult);
    if (FAILED(apartmentResult) && apartmentResult != RPC_E_CHANGED_MODE)
    {
        Log(L"CoInitializeEx(packaged activation) failed: 0x%08X", static_cast<unsigned int>(apartmentResult));
        return static_cast<int>(apartmentResult);
    }

    IApplicationActivationManager* activationManager = nullptr;
    HRESULT activationResult = CoCreateInstance(
        CLSID_ApplicationActivationManager,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&activationManager));
    DWORD processId = 0;
    if (SUCCEEDED(activationResult))
    {
        activationResult = activationManager->ActivateApplication(
            appUserModelId.c_str(),
            arguments.empty() ? nullptr : arguments.c_str(),
            AO_NONE,
            &processId);
        activationManager->Release();
    }

    if (uninitializeApartment)
    {
        CoUninitialize();
    }

    Log(L"LaunchPackagedInstance AUMID=%s arguments=%s result=0x%08X pid=%lu",
        appUserModelId.c_str(),
        arguments.c_str(),
        static_cast<unsigned int>(activationResult),
        processId);
    if (FAILED(activationResult))
    {
        if (!quiet)
        {
            wchar_t resultText[16] = {};
            swprintf_s(resultText, L"%08X", static_cast<unsigned int>(activationResult));
            std::wstring message =
                L"Packaged activation failed with HRESULT 0x" +
                std::wstring(resultText) +
                L". See NowPlayingTile.log for details.";
            MessageBoxW(nullptr, message.c_str(), L"NowPlayingTile launch", MB_OK | MB_ICONERROR);
        }
        return static_cast<int>(activationResult);
    }

    if (!quiet)
    {
        std::wstring message = L"Launched packaged instance";
        if (!arguments.empty())
        {
            message += L" with arguments: " + arguments;
        }
        MessageBoxW(nullptr, message.c_str(), L"NowPlayingTile launch", MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}

static bool AutoRegisterAndLaunchIfNeeded(const AppOptions& options, int* exitCode)
{
    if (exitCode != nullptr)
    {
        *exitCode = 0;
    }
    if (HasPackageIdentity())
    {
        return false;
    }

    bool startupExplicit = false;
    bool startupDesired = false;
    FindNowPlayingStartupPreference(
        options.settings,
        startupExplicit,
        startupDesired);
    (void)startupDesired;

    if (options.showHelp ||
        options.requestExit ||
        options.registerPackage ||
        options.unregisterPackage ||
        options.launchPackaged ||
        options.regenerateManifest ||
        (options.showWidget && !startupExplicit))
    {
        return false;
    }

    Log(L"Process has no package identity; registering loose package and relaunching through shell:AppsFolder.");

    int registerRc = RegisterDevelopmentPackage(true);
    if (registerRc != 0)
    {
        Log(L"Automatic package registration failed: %d", registerRc);
        MessageBoxW(nullptr,
            L"NowPlayingTile could not register its loose AppX package automatically.\r\n\r\n"
            L"The unpackaged bootstrap process will exit instead of trying Live Tile updates without package identity.\r\n"
            L"See NowPlayingTile.log for the AppX deployment error.",
            APP_DISPLAY_NAME,
            MB_OK | MB_ICONERROR);
        if (exitCode != nullptr)
        {
            *exitCode = registerRc;
        }
        return true;
    }

    int launchRc = LaunchPackagedInstance(true, PackagedLaunchArguments(options));
    if (launchRc != 0)
    {
        Log(L"Automatic packaged launch failed: %d", launchRc);
        MessageBoxW(nullptr,
            L"NowPlayingTile was registered, but Windows did not launch the packaged instance automatically.\r\n\r\n"
            L"Open it from Start / shell:AppsFolder, or run NowPlayingTile.exe --launch-packaged.\r\n"
            L"See NowPlayingTile.log for details.",
            APP_DISPLAY_NAME,
            MB_OK | MB_ICONERROR);
        if (exitCode != nullptr)
        {
            *exitCode = launchRc;
        }
        return true;
    }

    Log(L"Automatic registration and packaged relaunch completed; exiting unpackaged bootstrap process.");
    return true;
}

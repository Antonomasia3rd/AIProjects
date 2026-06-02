static std::wstring QuoteCommandLineArg(const std::wstring& value)
{
    std::wstring out = L"\"";
    unsigned int backslashes = 0;
    for (wchar_t ch : value)
    {
        if (ch == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (ch == L'\"')
        {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

static std::wstring QuotePowerShellLiteral(const std::wstring& value)
{
    std::wstring out = L"'";
    for (wchar_t ch : value)
    {
        if (ch == L'\'')
        {
            out += L"''";
        }
        else
        {
            out.push_back(ch);
        }
    }
    out.push_back(L'\'');
    return out;
}

static std::wstring Base64EncodeBytes(const BYTE* data, size_t length)
{
    static const wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3)
    {
        DWORD block = static_cast<DWORD>(data[i]) << 16;
        bool have2 = i + 1 < length;
        bool have3 = i + 2 < length;
        if (have2)
        {
            block |= static_cast<DWORD>(data[i + 1]) << 8;
        }
        if (have3)
        {
            block |= static_cast<DWORD>(data[i + 2]);
        }

        out.push_back(alphabet[(block >> 18) & 0x3F]);
        out.push_back(alphabet[(block >> 12) & 0x3F]);
        out.push_back(have2 ? alphabet[(block >> 6) & 0x3F] : L'=');
        out.push_back(have3 ? alphabet[block & 0x3F] : L'=');
    }

    return out;
}

static std::wstring PowerShellEncodedCommand(const std::wstring& command)
{
    return Base64EncodeBytes(reinterpret_cast<const BYTE*>(command.c_str()), command.size() * sizeof(wchar_t));
}

static std::wstring DefaultPowerShellPath()
{
    wchar_t systemDirectory[MAX_PATH] = {};
    UINT length = GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory));
    if (length > 0 && length < ARRAYSIZE(systemDirectory))
    {
        return std::wstring(systemDirectory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    }
    return L"powershell.exe";
}

static bool DecodeUtf8Bytes(const std::vector<BYTE>& bytes, std::wstring* text)
{
    if (text == nullptr)
    {
        return true;
    }
    text->clear();
    if (bytes.empty())
    {
        return true;
    }

    int wideLength = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLength <= 0)
    {
        wideLength = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), nullptr, 0);
        if (wideLength <= 0)
        {
            return false;
        }
        text->resize(static_cast<size_t>(wideLength));
        MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), &(*text)[0], wideLength);
        return true;
    }

    text->resize(static_cast<size_t>(wideLength));
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), &(*text)[0], wideLength);
    return true;
}

static bool RunPowerShellCommand(const std::wstring& command, std::wstring* output, DWORD* exitCode)
{
    if (exitCode != nullptr)
    {
        *exitCode = 1;
    }
    if (output != nullptr)
    {
        output->clear();
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    {
        Log(L"CreatePipe failed: %s", GetLastErrorText(GetLastError()).c_str());
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulIn == INVALID_HANDLE_VALUE)
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        Log(L"CreateFileW(NUL) failed: %s", GetLastErrorText(GetLastError()).c_str());
        return false;
    }

    std::wstring psCommand =
        L"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
        L"$OutputEncoding = [System.Text.Encoding]::UTF8; " +
        command;
    std::wstring cmdLine = QuoteCommandLineArg(DefaultPowerShellPath()) +
        L" -NoProfile -ExecutionPolicy Bypass -EncodedCommand " +
        PowerShellEncodedCommand(psCommand);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nulIn;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    CloseHandle(nulIn);

    if (!ok)
    {
        Log(L"CreateProcessW(powershell.exe) failed: %s", GetLastErrorText(GetLastError()).c_str());
        CloseHandle(readPipe);
        return false;
    }

    std::vector<BYTE> bytes;
    BYTE buffer[4096] = {};
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) && read != 0)
    {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != nullptr)
    {
        *exitCode = code;
    }
    DecodeUtf8Bytes(bytes, output);
    return true;
}

static bool WriteUtf8File(const std::wstring& path, const std::wstring& text)
{
    EnsureDirectory(GetDirectoryName(path));

    int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (byteCount <= 1)
    {
        return false;
    }

    std::vector<char> bytes(static_cast<size_t>(byteCount));
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, bytes.data(), byteCount, nullptr, nullptr);

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        Log(L"Could not write %s: %s", path.c_str(), GetLastErrorText(GetLastError()).c_str());
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(byteCount - 1), &written, nullptr);
    CloseHandle(file);
    return ok && written == static_cast<DWORD>(byteCount - 1);
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
    if (!FileExists(path))
    {
        EnsureDirectory(GetDirectoryName(path));
    }

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

    DeleteFileW(path.c_str());
    Status status = bitmap.Save(path.c_str(), &pngClsid, nullptr);
    if (status != Ok)
    {
        Log(L"Could not save %s as PNG. GDI+ status=%d", path.c_str(), static_cast<int>(status));
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
        L"  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"\r\n"
        L"  IgnorableNamespaces=\"uap rescap\">\r\n\r\n"
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
        L"    </Application>\r\n"
        L"  </Applications>\r\n\r\n"
        L"  <Capabilities>\r\n"
        L"    <rescap:Capability Name=\"runFullTrust\" />\r\n"
        L"  </Capabilities>\r\n"
        L"</Package>\r\n";
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
    if (forceRewrite || !FileExists(manifestPath))
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
        L"$manifest = " + QuotePowerShellLiteral(manifest) + L"; "
        L"Add-AppxPackage -Register $manifest -ForceUpdateFromAnyVersion; "
        L"$pkg = Get-AppxPackage -Name " + QuotePowerShellLiteral(PACKAGE_NAME) + L"; "
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
        L"$pkg = Get-AppxPackage -Name " + QuotePowerShellLiteral(PACKAGE_NAME) + L"; "
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

static int LaunchPackagedInstance(bool quiet)
{
    std::wstring command =
        L"$pkg = Get-AppxPackage -Name " + QuotePowerShellLiteral(PACKAGE_NAME) + L"; "
        L"if ($null -eq $pkg) { throw 'NowPlayingTile.App is not registered. Run NowPlayingTile.exe --register first.' }; "
        L"$target = 'shell:AppsFolder\\' + $pkg.PackageFamilyName + '!App'; "
        L"Start-Process $target; "
        L"'Launched: ' + $target";

    std::wstring output;
    DWORD exitCode = 1;
    bool started = RunPowerShellCommand(command, &output, &exitCode);
    Log(L"LaunchPackagedInstance exit=%lu output=%s", exitCode, output.c_str());
    if (quiet)
    {
        return started && exitCode == 0 ? 0 : static_cast<int>(exitCode == 0 ? 1 : exitCode);
    }
    return ShowPowerShellResult(L"NowPlayingTile launch", started, exitCode, output);
}

static bool AutoRegisterAndLaunchIfNeeded(const AppOptions& options)
{
    if (HasPackageIdentity())
    {
        return false;
    }

    if (options.showHelp ||
        options.requestExit ||
        options.registerPackage ||
        options.unregisterPackage ||
        options.launchPackaged ||
        options.regenerateManifest ||
        options.showWidget)
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
        return true;
    }

    int launchRc = LaunchPackagedInstance(true);
    if (launchRc != 0)
    {
        Log(L"Automatic packaged launch failed: %d", launchRc);
        MessageBoxW(nullptr,
            L"NowPlayingTile was registered, but Windows did not launch the packaged instance automatically.\r\n\r\n"
            L"Open it from Start / shell:AppsFolder, or run NowPlayingTile.exe --launch-packaged.\r\n"
            L"See NowPlayingTile.log for details.",
            APP_DISPLAY_NAME,
            MB_OK | MB_ICONERROR);
        return true;
    }

    Log(L"Automatic registration and packaged relaunch completed; exiting unpackaged bootstrap process.");
    return true;
}

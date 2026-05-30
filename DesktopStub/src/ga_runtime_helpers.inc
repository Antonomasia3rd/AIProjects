bool UsePowerShell()
{
    return IniReadI(L"Settings", L"UsePowerShell", 0) != 0;
}

static LiveTileUpdateMode ConfiguredLiveTileUpdateMode()
{
    LiveTileUpdateMode mode = static_cast<LiveTileUpdateMode>(g_liveTileUpdateMode.load());
    switch (mode)
    {
    case LiveTileUpdateMode::Registration:
    case LiveTileUpdateMode::LiveTile:
    case LiveTileUpdateMode::Auto:
        return mode;
    default:
        return LiveTileUpdateMode::Auto;
    }
}

static ManifestCompatibilityTarget ConfiguredManifestCompatibilityTarget()
{
    ManifestCompatibilityTarget target = static_cast<ManifestCompatibilityTarget>(g_manifestCompatibilityTarget.load());
    switch (target)
    {
    case ManifestCompatibilityTarget::Windows10:
    case ManifestCompatibilityTarget::Windows81:
    case ManifestCompatibilityTarget::Windows8:
        return target;
    default:
        return ManifestCompatibilityTarget::Windows10;
    }
}

static std::wstring ManifestCompatibilityTargetSummaryText()
{
    return ManifestCompatibilityTargetName(ConfiguredManifestCompatibilityTarget());
}

static bool CurrentProcessHasPackageIdentity()
{
    UINT32 length = 0;
    LONG rc = GetCurrentPackageFullName(&length, nullptr);
    return rc == ERROR_INSUFFICIENT_BUFFER && length > 0;
}

static LiveTileUpdateMode EffectiveLiveTileUpdateMode()
{
    LiveTileUpdateMode mode = ConfiguredLiveTileUpdateMode();
    if (mode != LiveTileUpdateMode::Auto)
        return mode;

    // Windows 8/8.1 manifest mode is meant for the Start Screen / Live Tile
    // compatibility path. The resident tray process is still unpackaged, but it
    // can ask the registered package activation stub to perform the actual
    // TileUpdateManager call under package identity. Treat Auto as LiveTile for
    // these manifest targets so wallpaper changes generate live-tile assets and
    // route the update through the stub.
    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (target == ManifestCompatibilityTarget::Windows81 ||
        target == ManifestCompatibilityTarget::Windows8)
        return LiveTileUpdateMode::LiveTile;

    return CurrentProcessHasPackageIdentity()
        ? LiveTileUpdateMode::LiveTile
        : LiveTileUpdateMode::Registration;
}

static bool UseExperimentalLiveTileUpdate()
{
    return EffectiveLiveTileUpdateMode() == LiveTileUpdateMode::LiveTile;
}

static bool LiveTileModeHasFixedRuntime(LiveTileUpdateMode mode, ManifestCompatibilityTarget target, bool& shouldRunPackaged)
{
    // Windows 10 Registration mode must run from the normal unpackaged desktop
    // process so it can re-register the loose AppX package. LiveTile mode must
    // run with package identity so TileUpdateManager has the required context.
    // Auto is intentionally excluded because its effective mode depends on the
    // current package identity and would otherwise cause pointless relaunches.
    if (target != ManifestCompatibilityTarget::Windows10)
        return false;

    if (mode == LiveTileUpdateMode::LiveTile)
    {
        shouldRunPackaged = true;
        return true;
    }

    if (mode == LiveTileUpdateMode::Registration)
    {
        shouldRunPackaged = false;
        return true;
    }

    return false;
}

static std::wstring LiveTileUpdateModeSummaryText()
{
    LiveTileUpdateMode configured = ConfiguredLiveTileUpdateMode();
    if (configured != LiveTileUpdateMode::Auto)
        return LiveTileUpdateModeName(configured);

    std::wstring text = LiveTileUpdateModeName(LiveTileUpdateMode::Auto);
    text += L" (";
    text += LiveTileUpdateModeName(EffectiveLiveTileUpdateMode());
    text += L")";
    return text;
}

static ErrorAction ParseErrorAction(const std::wstring& value)
{
    std::wstring v = ToLowerCopy(TrimCopy(value));
    if (v == L"ignore" || v == L"none" || v == L"do nothing" || v == L"do_nothing")
        return ErrorAction::Ignore;
    if (v == L"exit" || v == L"exit program" || v == L"exit_program")
        return ErrorAction::Exit;
    if (v == L"crash" || v == L"crash program" || v == L"crash_program" || v == L"failfast" || v == L"fail fast")
        return ErrorAction::Crash;
    return ErrorAction::Warn;
}

static const wchar_t* ErrorActionName(ErrorAction action)
{
    switch (action)
    {
    case ErrorAction::Ignore: return g_ui.singleInstanceActionIgnore.c_str();
    case ErrorAction::Exit: return g_ui.singleInstanceActionExit.c_str();
    case ErrorAction::Crash: return g_ui.singleInstanceActionCrash.c_str();
    case ErrorAction::Warn:
    default:
        return g_ui.singleInstanceActionWarn.c_str();
    }
}

static ErrorAction CurrentSingleInstanceFailureAction()
{
    return ParseErrorAction(IniReadS(L"Settings", L"SingleInstanceFailureAction", L"Warn"));
}

static constexpr int SUPPORTED_ASSET_SCALES[] = { 100, 125, 150, 200, 400 };

static int SnapToSupportedScale(int percent)
{
    for (int scale : SUPPORTED_ASSET_SCALES)
    {
        if (percent <= scale)
            return scale;
    }
    return 400;
}

static HMONITOR GetPrimaryMonitorHandle()
{
    struct MonitorSearch
    {
        HMONITOR primary = nullptr;
    } search;

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM param) noexcept -> BOOL
        {
            auto* search = reinterpret_cast<MonitorSearch*>(param);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(monitor, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY))
            {
                search->primary = monitor;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    if (search.primary)
        return search.primary;
    return MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
}

static bool GetPrimaryMonitorRect(RECT& rect)
{
    HMONITOR monitor = GetPrimaryMonitorHandle();
    if (!monitor)
        return false;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return false;

    rect = mi.rcMonitor;
    return true;
}

static int CurrentDpiPercent()
{
    UINT dpiX = 96;
    UINT dpiY = 96;

    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore)
    {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
#pragma warning(suppress: 4191)
        auto getDpiForMonitor = (GetDpiForMonitorFn)GetProcAddress(shcore, "GetDpiForMonitor");
        if (getDpiForMonitor)
        {
            HMONITOR monitor = GetPrimaryMonitorHandle();
            if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY)) && dpiX > 0)
            {
                FreeLibrary(shcore);
                return MulDiv((int)dpiX, 100, 96);
            }
        }
        FreeLibrary(shcore);
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using GetDpiForSystemFn = UINT(WINAPI*)();
#pragma warning(suppress: 4191)
        auto getDpiForSystem = (GetDpiForSystemFn)GetProcAddress(user32, "GetDpiForSystem");
        if (getDpiForSystem)
        {
            dpiX = getDpiForSystem();
            if (dpiX > 0)
                return MulDiv((int)dpiX, 100, 96);
        }
    }

    HDC dc = GetDC(nullptr);
    if (dc)
    {
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        if (dpi > 0)
            return MulDiv(dpi, 100, 96);
    }

    return 100;
}

static int CurrentAssetScale()
{
    return SnapToSupportedScale(CurrentDpiPercent());
}

static bool AddScale(std::vector<int>& scales, int scale)
{
    if (std::find(scales.begin(), scales.end(), scale) != scales.end())
        return false;
    scales.push_back(scale);
    return true;
}

static std::vector<int> GetConfiguredScales()
{
    std::vector<int> scales;
    if (g_generateScaleAuto)
    {
        // Auto mode: use only the detected DPI scale; manual flags are ignored
        AddScale(scales, CurrentAssetScale());
    }
    else
    {
        if (g_generateScale100) AddScale(scales, 100);
        if (g_generateScale125) AddScale(scales, 125);
        if (g_generateScale150) AddScale(scales, 150);
        if (g_generateScale200) AddScale(scales, 200);
        if (g_generateScale400) AddScale(scales, 400);
    }
    if (scales.empty()) AddScale(scales, 100);
    std::sort(scales.begin(), scales.end());
    return scales;
}

static int ScalePixels(int value, int scale)
{
    return MulDiv(value, scale, 100);
}

static std::wstring ScaleAssetPath(const std::wstring& path, int scale)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        dot = path.size();

    return path.substr(0, dot) + L".scale-" + IntToWString(scale) + path.substr(dot);
}

static bool DeleteFileIfExistsForDisabledAsset(const std::wstring& path)
{
    if (!PathFileExistsW(path.c_str()))
        return true;

    if (DeleteFileW(path.c_str()))
    {
        Log(g_ui.deletedDisabledAsset.c_str(), path.c_str());
        return true;
    }

    Log(g_ui.failedDeleteDisabledAsset.c_str(), path.c_str());
    LogWin32Failure(L"DeleteFileW");
    return false;
}

static bool DeleteDisabledAssetFiles(const std::wstring& basePath)
{
    bool ok = DeleteFileIfExistsForDisabledAsset(basePath);
    for (int scale : SUPPORTED_ASSET_SCALES)
        ok = DeleteFileIfExistsForDisabledAsset(ScaleAssetPath(basePath, scale)) && ok;
    return ok;
}

const wchar_t* FitModeToString(FitMode m)
{
    switch (m)
    {
    case FitMode::Fill:    return g_ui.fitFill.c_str();
    case FitMode::Fit:     return g_ui.fitFit.c_str();
    case FitMode::Stretch: return g_ui.fitStretch.c_str();
    case FitMode::Center:  return g_ui.fitCenter.c_str();
    case FitMode::Tile:    return g_ui.fitTile.c_str();
    case FitMode::Span:    return g_ui.fitSpan.c_str();
    default:               return L"?";
    }
}

static void AppendDialogTemplateString(WORD*& dst, const std::wstring& text)
{
    memcpy(dst, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    dst += text.size() + 1;
}

static DLGITEMTEMPLATE* AlignDialogItem(WORD*& p)
{
    DLGITEMTEMPLATE* item = (DLGITEMTEMPLATE*)(((DWORD_PTR)p + 3) & ~3);
    p = (WORD*)item;
    return item;
}

INT_PTR ShowRenameDialog(HWND parent, std::wstring& path)
{
    size_t stringBytes =
        (g_ui.renameDialogTitle.size() + g_ui.okText.size() + g_ui.cancelText.size() + 64) * sizeof(wchar_t);
    std::vector<BYTE> tmpl(4096 + stringBytes);
    DLGTEMPLATE* dlg = (DLGTEMPLATE*)tmpl.data();

    dlg->style = WS_POPUP | WS_BORDER | WS_SYSMENU | DS_MODALFRAME | WS_CAPTION | DS_SETFONT | DS_CENTER;
    dlg->cdit = 3;
    dlg->x = 10; dlg->y = 10; dlg->cx = 320; dlg->cy = 72;

    WORD* p = (WORD*)(dlg + 1);

    *p++ = 0; // no menu
    *p++ = 0; // default class

    AppendDialogTemplateString(p, g_ui.renameDialogTitle);
    *p++ = 9; // font size
    AppendDialogTemplateString(p, L"Segoe UI");
    // ---- Edit box ----
    DLGITEMTEMPLATE* item = AlignDialogItem(p);
    item->x = 6; item->y = 6; item->cx = 308; item->cy = 14;
    item->id = 1001;
    item->style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0081; // EDIT class
    *p++ = 0; // no text
    *p++ = 0; // no extra data

    // ---- OK button ----
    item = AlignDialogItem(p);
    item->x = 188; item->y = 32; item->cx = 58; item->cy = 16;
    item->id = IDOK;
    item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080; // BUTTON
    AppendDialogTemplateString(p, g_ui.okText);
    *p++ = 0;

    // ---- Cancel button ----
    item = AlignDialogItem(p);
    item->x = 256; item->y = 32; item->cx = 58; item->cy = 16;
    item->id = IDCANCEL;
    item->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    AppendDialogTemplateString(p, g_ui.cancelText);
    *p++ = 0;

    return DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        (DLGTEMPLATE*)tmpl.data(),
        parent,
        RenameDlgProc,
        (LPARAM)&path
    );
}


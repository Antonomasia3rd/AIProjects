// Inert host fixture helpers; include after Check/WriteContentFixture helpers.
static void TestContentPresetsAndImportConfirmation()
{
    const auto savedCommandLine = g_commandLine;
    std::wstring original;
    if (!ReadIniFileForMutation(original)) { Check(false, "preset test reads its private profile"); return; }
    const auto savedResponse = contentDialogResponse;
    const auto savedDialogs = contentDialogs;
    const auto savedDialogText = contentDialogText;
    const auto savedMode = g_liveTileUpdateMode.load();
    g_liveTileUpdateMode = static_cast<int>(LiveTileUpdateMode::Registration);
    const std::wstring base = L"; keep comment\r\n[Content]\r\nEnabled=1\r\nCount=2\r\nCycleSeconds=10\r\nRefreshSeconds=2\r\n"
        L"TextMode=Auto\r\n[Content.1]\r\nEnabled=1\r\nName=Personal wallpaper\r\nBackground=Wallpaper\r\nTextSources=CustomText\r\n"
        L"Text=Personal caption\r\nSecondaryText=Personal second\r\nBadgeText=7\r\n"
        L"[Content.2]\r\nEnabled=0\r\nName=Saved other\r\nBackground=None\r\nText=Keep me\r\n"
        L"[Content.8]\r\nEnabled=0\r\nName=Outside Count\r\nBackground=None\r\nText=Also keep me\r\n";
    Check(WriteContentFixture(base), "preset private profile has active and dormant saved contents");
    contentDialogResponse = IDNO;
    Check(!ApplyContentPreset(nullptr, aip::content::WallpaperAndSmtcCycle()), "declined preset makes no configuration change");
    std::wstring after;
    Check(ReadIniFileForMutation(after) && after == base, "declined preset preserves exact INI bytes");
    contentDialogResponse = IDYES;
    Check(ApplyContentPreset(nullptr, aip::content::WallpaperAndSmtcCycle()), "approved two-item preset commits one validated layout");
    Check(IniReadI(L"Content", L"Count", 0) == 10 && IniReadI(L"Content.1", L"Enabled", 1) == 0 &&
        IniReadS(L"Content.2", L"Text", L"") == L"Keep me" && IniReadS(L"Content.8", L"Text", L"") == L"Also keep me",
        "new preset appends after highest saved section and keeps every prior field");
    Check(IniReadS(L"Content.9", L"Text", L"") == L"Personal caption" && IniReadS(L"Content.9", L"SecondaryText", L"") == L"Personal second" &&
        IniReadS(L"Content.9", L"BadgeText", L"") == L"7" && IniReadS(L"Content.10", L"Background", L"") == L"Transparent" &&
        IniReadS(L"Content.10", L"TextSources", L"") == L"SMTC", "preset copies current user wallpaper text and adds transparent media content");
    Check(IniReadS(L"Content", L"CycleDriver", L"") == L"Auto", "new presets choose Windows rotation when available with the app timer as fallback");
    Check(ReadIniFileForMutation(after) && after.find(L"; keep comment") != std::wstring::npos, "preset retains unrelated INI comments");
    const auto approved = after;
    const auto dialogs = contentDialogs;
    Check(ApplyContentPreset(nullptr, aip::content::WallpaperAndSmtcCycle()) && ReadIniFileForMutation(after) && after == approved &&
        contentDialogs == dialogs, "reapplying an identical active preset does not append slots, write changes or ask again");
    Check(IniReadI(L"Content", L"CycleSeconds", 0) == 10 && IniReadI(L"Content", L"RefreshSeconds", 0) == 2,
        "preset retains current cycle/source intervals");
    for (UINT choice = 0; choice < _countof(aip::content::CycleDrivers); ++choice) {
        Check(DispatchContentEngineCommand(nullptr, ID_CONTENT_CYCLE_DRIVER + choice) &&
            IniReadS(L"Content", L"CycleDriver", L"") == aip::content::CycleDrivers[choice].value,
            "each tray cycle driver choice saves the shared canonical INI value");
    }
    HMENU timing = CreatePopupMenu();
    AppendContentEngineMenu(timing, ContentMenuPart::Timing);
    TrayMenuCommands commands;
    InspectTrayMenu(timing, commands);
    for (UINT choice = 0; choice < _countof(aip::content::CycleDrivers); ++choice)
        Check(commands.count(ID_CONTENT_CYCLE_DRIVER + choice) == 1, "timing menu exposes each cycle driver exactly once");
    Check(commands.count(ID_CONTENT_OPTIONS + 3) && commands[ID_CONTENT_OPTIONS + 3].path.find(L"DesktopStub cycle interval") != std::wstring::npos,
        "cycle interval label distinguishes the app timer from Windows-managed timing");
    DestroyMenu(timing);
    Check(WriteContentFixture(base), "replacement callback fixture restores the saved layout");
    contentDialogResponse = IDNO;
    const std::vector<aip::IniSetting> import = {{L"Content", L"Enabled", L"1"}, {L"Content", L"Count", L"1"},
        {L"Content.1", L"Name", L"Imported"}, {L"Content.1", L"Background", L"None"}};
    bool confirmed = false;
    Check(!SaveContentMenuSettings(nullptr, import, false, true, [&](const auto& before, const auto&, const auto&) {
        confirmed = before.find(L"[Content.8]") != std::wstring::npos;
        return false;
    }) && confirmed && ReadIniFileForMutation(after) && after == base,
        "a replacement preview sees unused sections and refusal preserves them exactly");
    Check(WriteContentFixture(base), "CLI preset fixture starts from the same sparse saved contents");
    g_commandLine = {}; g_commandLine.configureOnly = true; g_commandLine.presetName = L"WallpaperSMTC";
    g_commandLine.settings = {{L"Content", L"CycleDriver", L"DesktopStub"}, {L"Content.9", L"Text", L"Explicit caption"}};
    Check(RunRenderOnlyCommand() == 0 && IniReadS(L"Content.9", L"Text", L"") == L"Explicit caption" &&
        IniReadS(L"Content", L"CycleDriver", L"") == L"DesktopStub" && IniReadS(L"Content.8", L"Text", L"") == L"Also keep me",
        "configuration-only preset applies before explicit overrides and preserves sparse saved entries");
    std::wstring overriddenPreset;
    Check(ReadIniFileForMutation(overriddenPreset) && RunRenderOnlyCommand() == 0 &&
        ReadIniFileForMutation(after) && after == overriddenPreset,
        "repeating an unchanged preset with an explicit driver does not consume more saved slots");
    std::wstring error;
    g_commandLine = {}; g_commandLine.presetName = L"WallpaperSMTC";
    Check(WriteContentFixture(base) && PrepareCommandLinePresetPreview(error), "CLI preflight can preview a preset without writing it");
    auto concurrent = base;
    aip::WriteIniValueToText(concurrent, L"Content.1", L"Text", L"Fresh caption");
    aip::WriteIniValueToText(concurrent, L"Content.12", L"Name", L"New saved slot");
    aip::WriteIniValueToText(concurrent, L"Content.12", L"Enabled", L"0");
    Check(WriteContentFixture(concurrent) && MutateDesktopStubIniWithWait([&](std::wstring& text) {
        return ApplyCommandLinePresetToDocument(text, error) && ValidateCommandLinePresetDocument(text, error);
    }, CurrentIniWriteLockWaitMs()) && IniReadI(L"Content", L"Count", 0) == 14 &&
        IniReadS(L"Content.13", L"Text", L"") == L"Fresh caption" && IniReadS(L"Content.12", L"Name", L"") == L"New saved slot",
        "locked preset expansion uses fresh fields/slot allocation instead of a stale preflight plan");
    g_commandLine.nowPlayingImports.push_back(L"unused-profile.ini");
    Check(!ValidateCommandLinePresetCombination(error), "preset plus a replacement import is rejected with an explicit combination error");
    const auto originalIniPath = g_iniPath;
    const auto freshIniPath = ParentDirectoryOfPath(g_iniPath) + L"\\fresh-preset-" + std::to_wstring(GetTickCount64()) + L".ini";
    g_iniPath = freshIniPath;
    { std::lock_guard<std::mutex> lock(g_iniCacheMutex); g_iniCacheLoaded = false; g_iniCacheDoc.clear(); }
    g_commandLine = {}; g_commandLine.configureOnly = true; g_commandLine.presetName = L"Wallpaper";
    Check(GetFileAttributesW(freshIniPath.c_str()) == INVALID_FILE_ATTRIBUTES && !ValidateDesktopStubContentProfile(true,error) &&
        error.find(L"Add tile")!=std::wstring::npos && RunRenderOnlyCommand() == 2 &&
        GetFileAttributesW(freshIniPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "a legacy preset rejects a fresh independent factory before creating an INI");
    DeleteFileW(freshIniPath.c_str());
    g_iniPath = originalIniPath;
    g_commandLine = savedCommandLine;
    Check(WriteContentFixture(original), "preset fixtures restore their original private profile");
    contentDialogResponse = savedResponse; contentDialogs = savedDialogs; contentDialogText = savedDialogText;
    g_liveTileUpdateMode = savedMode;
}

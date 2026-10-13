// Included by the inert host fixture after Check, WriteContentFixture and
// InspectTrayMenu. No resident, notification or provider entry point runs.
static void TestDisplaySettingsParity()
{
    const auto savedCommandLine = g_commandLine;
    std::wstring original;
    if (!ReadIniFileForMutation(original)) { Check(false, "display parity reads its private fixture profile"); return; }
    g_commandLine = {};
    std::wstring error;
    for (const auto& setting : g_displaySettings) {
        auto fallback = std::wstring(setting.fallback);
        Check(NormalizeDesktopStubTypedSetting(setting.section, setting.key, fallback, error) && fallback == setting.fallback,
            "display schema defaults remain valid canonical settings");
        auto invalid = std::wstring(L"invalid-value");
        Check(!NormalizeDesktopStubTypedSetting(setting.section, setting.key, invalid, error) &&
            error.find(std::wstring(setting.section) + L"." + setting.key) != std::wstring::npos && invalid == L"invalid-value",
            "generic CLI rejects malformed display values without replacing them by a fallback");
        if (setting.type != DesktopStubDisplaySettingType::Boolean) continue;
        for (const auto& input : {std::pair<const wchar_t*, const wchar_t*>{L" true ", L"1"},
                {L"false", L"0"}, {L"YES", L"1"}, {L"off", L"0"}}) {
            auto value = std::wstring(input.first);
            Check(NormalizeDesktopStubTypedSetting(setting.section, setting.key, value, error) && value == input.second,
                "display booleans canonicalize the same accepted words as dedicated CLI flags");
        }
    }
    struct EnumAlias { const wchar_t* key; const wchar_t* value; const wchar_t* expected; };
    const EnumAlias aliases[] = {
        {L"ExperimentalLiveTileUpdate", L" true ", L"LiveTile"},
        {L"ExperimentalLiveTileUpdate", L"disabled", L"Registration"},
        {L"ExperimentalLiveTileUpdate", L"automatic", L"Auto"},
        {L"LiveTileTemplateStyle", L"win8.1", L"Windows81Preset"},
        {L"LiveTileTemplateStyle", L"default", L"Adaptive"},
        {L"LiveTileBranding", L"name+logo", L"NameAndLogo"},
        {L"LiveTileBranding", L"off", L"None"},
        {L"AppxManifestTarget", L"8.1", L"Windows81"},
        {L"AppxManifestTarget", L"80", L"Windows8"},
        {L"AppxManifestTarget", L"default", L"Windows10"},
    };
    for (const auto& alias : aliases) {
        auto value = std::wstring(alias.value);
        Check(NormalizeDesktopStubTypedSetting(L"Settings", alias.key, value, error) && value == alias.expected,
            "generic display enum overrides use the dedicated flag's accepted aliases");
    }
    for (const auto& setting : g_displaySettings) {
        auto invalidProfile = original;
        Check(aip::WriteIniValueToText(invalidProfile, setting.section, setting.key, L"invalid-value") && WriteContentFixture(invalidProfile),
            "private fixture can represent an invalid manual display edit");
        Check(!ValidateDesktopStubDisplayProfile(false, error), "direct INI rejects invalid display values instead of choosing another mode");
        g_commandLine.settings = {{setting.section, setting.key, setting.fallback}};
        Check(ValidateDesktopStubDisplayProfile(true, error), "an explicit valid CLI override can repair a malformed saved display setting");
        g_commandLine.settings.clear();
    }
    auto booleanProfile = original;
    Check(aip::WriteIniValueToText(booleanProfile, L"TileText", L"Enabled", L"true") &&
        aip::WriteIniValueToText(booleanProfile, L"TileText", L"ApplyToMediumTile", L"false") &&
        aip::WriteIniValueToText(booleanProfile, L"Assets", L"WideTile", L"yes") &&
        WriteContentFixture(booleanProfile) && ValidateDesktopStubDisplayProfile(false, error),
        "direct INI accepts boolean words without requiring numeric rewriting");
    Check(IniReadI(L"TileText", L"Enabled", 0) == 1 && IniReadI(L"TileText", L"ApplyToMediumTile", 1) == 0 &&
        IniReadI(L"Assets", L"WideTile", 0) == 1, "runtime display reads agree with the validated boolean words");
    g_commandLine.settings = {{L"TileText", L"Enabled", L"1"}, {L"TileText", L"SecondaryText", L"saved secondary"},
        {L"TileText", L"BadgeText", L"7"}};
    AddCommandLinePrimaryTileTextSetting(L"");
    const auto lookup = [](const wchar_t* key) {
        for (const auto& setting : g_commandLine.settings)
            if (IEquals(setting.section, L"TileText") && IEquals(setting.key, key)) return setting.value;
        return std::wstring(L"<missing>");
    };
    Check(lookup(L"Text").empty() && lookup(L"Enabled") == L"1" && lookup(L"SecondaryText") == L"saved secondary" && lookup(L"BadgeText") == L"7",
        "empty primary CLI text preserves enabled secondary and badge fields");
    const auto emptyPrimary = g_commandLine.settings;
    g_commandLine.settings.pop_back();
    AddCommandLineIniSetting(L"TileText", L"Text", L""); // --tile-text-clear.
    Check(g_commandLine.settings.size() == emptyPrimary.size() && lookup(L"Enabled") == L"1" && lookup(L"Text").empty(),
        "empty primary CLI value matches the explicit primary-clear action");
    AddCommandLineBoolSetting(L"TileText", L"Enabled", false);
    AddCommandLinePrimaryTileTextSetting(L"");
    Check(lookup(L"Enabled") == L"0", "empty primary text does not enable disabled default text");
    AddCommandLinePrimaryTileTextSetting(L"new title");
    Check(lookup(L"Enabled") == L"1" && lookup(L"Text") == L"new title", "nonempty primary text enables default text like the tray editor");
    g_commandLine.settings.clear();
    auto rejected = original;
    Check(aip::WriteIniValueToText(rejected, L"Settings", L"ExperimentalLiveTileUpdate", L"invalid-value") && WriteContentFixture(rejected),
        "configuration-only rejection fixture is initialized");
    g_commandLine.configureOnly = true;
    Check(RunRenderOnlyCommand() == 2, "configuration-only command rejects a malformed saved Live Tile mode before any write");
    std::wstring after;
    Check(ReadIniFileForMutation(after) && after == rejected, "rejected display configuration preserves exact existing INI bytes");
    g_commandLine = {};
    Check(WriteContentFixture(original), "display checks restore the original private profile");
    LoadUiStrings();
    for (bool grouped : {true, false}) {
        HMENU menu = CreatePopupMenu();
        PopulateDesktopStubTrayMenu(menu, grouped, false);
        TrayMenuCommands commands;
        InspectTrayMenu(menu, commands);
        for (UINT command : {ID_TILE_TEXT_APPLY_MEDIUM, ID_TILE_TEXT_APPLY_WIDE, ID_TILE_TEXT_APPLY_LARGE}) {
            const auto found = commands.find(command);
            Check(found != commands.end(), "both tray views keep each text-size command reachable exactly once");
            if (found != commands.end() && grouped) Check(found->second.path.find(L"Text on tile sizes") != std::wstring::npos &&
                found->second.path.find(g_ui.tileTextTitle) == std::wstring::npos,
                "composed/default text-size gates live in the shared display group");
        }
        DestroyMenu(menu);
    }
    g_commandLine = savedCommandLine;
}

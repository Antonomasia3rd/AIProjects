// Explicit configuration only. The provider remains idle until selected and a
// city/country are supplied; this UI never discovers or requests location.
static constexpr UINT ID_PRAYER_SETTINGS = 29500;
struct PrayerMethodChoice { int id; const wchar_t* label; };
// AlAdhan's public methods catalog: https://api.aladhan.com/v1/methods
static constexpr PrayerMethodChoice g_prayerMethods[] = {
    {0,L"Shia Ithna-Ashari / Leva Institute, Qum"}, {1,L"University of Islamic Sciences, Karachi"},
    {2,L"Islamic Society of North America (ISNA)"}, {3,L"Muslim World League"},
    {4,L"Umm Al-Qura University, Makkah"}, {5,L"Egyptian General Authority of Survey"},
    {7,L"University of Tehran, Institute of Geophysics"}, {8,L"Gulf Region"}, {9,L"Kuwait"}, {10,L"Qatar"},
    {11,L"Majlis Ugama Islam Singapura, Singapore"}, {12,L"Union Organization Islamic de France"},
    {13,L"Diyanet, Turkey (experimental)"}, {14,L"Spiritual Administration of Muslims of Russia"},
    {15,L"Moonsighting Committee Worldwide"}, {16,L"Dubai (experimental)"}, {17,L"JAKIM, Malaysia"},
    {18,L"Tunisia"}, {19,L"Algeria"}, {20,L"KEMENAG, Indonesia"}, {21,L"Morocco"},
    {22,L"Comunidade Islamica de Lisboa, Portugal"}, {23,L"Ministry of Awqaf, Jordan"}
};
static constexpr UINT PrayerMethodCommand(int method) { return ID_PRAYER_SETTINGS + (method == 20 ? 11u : 100u + static_cast<UINT>(method)); }
static constexpr const wchar_t* g_prayerAdjustmentKeys[] = {L"FajrAdjustmentMinutes", L"DhuhrAdjustmentMinutes", L"AsrAdjustmentMinutes", L"MaghribAdjustmentMinutes", L"IshaAdjustmentMinutes"};
static std::wstring PrayerUiString(const wchar_t* key, const wchar_t* fallback) { return IniReadS(L"Strings", key, fallback); }
static void AppendPrayerSettingsMenu(HMENU menu)
{
    const auto add = [&](UINT offset, const wchar_t* key, const wchar_t* fallback, const wchar_t* setting = nullptr) {
        auto text = PrayerUiString(key, fallback);
        if (setting) text += L": " + ContentMenuValue(IniReadS(L"PrayerTimes", setting, L""));
        aip::AppendTrayMenuItem(menu, ID_PRAYER_SETTINGS + offset, text, false, true);
    };
    add(0, L"PrayerLocation", L"Set city and country...");
    add(1, L"PrayerCity", L"City", L"City"); add(2, L"PrayerCountry", L"Country", L"Country");
    add(3, L"PrayerState", L"State / region (optional)", L"State"); add(4, L"PrayerTimeZone", L"Time zone override (blank uses schedule)", L"TimeZone");
    HMENU method = aip::BeginTrayNestedMenu(menu);
    const auto selected = IniReadS(L"PrayerTimes", L"Method", L"Auto");
    aip::AppendTrayMenuItem(method, ID_PRAYER_SETTINGS + 10, PrayerUiString(L"PrayerMethodAuto", L"Auto (regional calculation method)"), selected == L"Auto" || selected == L"-1", true);
    aip::AppendTrayMenuItem(method, ID_PRAYER_SETTINGS + 11, PrayerUiString(L"PrayerMethodKemenag", L"20 - KEMENAG (Indonesia)"), selected == L"20", true);
    std::wstring selectedName = PrayerUiString(L"PrayerMethodAuto", L"Auto (regional calculation method)");
    for (const auto& choice : g_prayerMethods) {
        const auto label = PrayerUiString((L"PrayerMethodName" + std::to_wstring(choice.id)).c_str(), choice.label);
        if (selected == std::to_wstring(choice.id)) selectedName = label;
        if (choice.id != 20) aip::AppendTrayMenuItem(method, PrayerMethodCommand(choice.id), label, selected == std::to_wstring(choice.id), true);
    }
    aip::EndTrayNestedMenu(menu, method, PrayerUiString(L"PrayerMethod", L"Calculation method") + L": " + selectedName);
    HMENU school = aip::BeginTrayNestedMenu(menu);
    const bool hanafi = IniReadS(L"PrayerTimes", L"School", L"0") == L"1";
    aip::AppendTrayMenuItem(school, ID_PRAYER_SETTINGS + 13, PrayerUiString(L"PrayerSchoolStandard", L"Standard (Shafi / Maliki / Hanbali)"), !hanafi, true);
    aip::AppendTrayMenuItem(school, ID_PRAYER_SETTINGS + 14, PrayerUiString(L"PrayerSchoolHanafi", L"Hanafi"), hanafi, true);
    aip::EndTrayNestedMenu(menu, school, PrayerUiString(L"PrayerSchool", L"Asr calculation"));
    HMENU latitude = aip::BeginTrayNestedMenu(menu);
    const auto latitudeValue = IniReadS(L"PrayerTimes", L"LatitudeAdjustmentMethod", L"3");
    const wchar_t* latitudeLabels[] = {L"Middle of the night", L"One seventh of the night", L"Angle based"};
    std::wstring latitudeName;
    for (UINT i = 0; i < 3; ++i) {
        const auto label = PrayerUiString((L"PrayerHighLatitudeName" + std::to_wstring(i + 1)).c_str(), latitudeLabels[i]);
        const bool active = latitudeValue == std::to_wstring(i + 1);
        aip::AppendTrayMenuItem(latitude, ID_PRAYER_SETTINGS + 130 + i, label, active, true);
        if (active) latitudeName = label;
    }
    aip::EndTrayNestedMenu(menu, latitude, PrayerUiString(L"PrayerHighLatitude", L"High-latitude adjustment") + L": " + latitudeName);
    HMENU adjustments = aip::BeginTrayNestedMenu(menu);
    const wchar_t* prayers[] = {L"Fajr", L"Dhuhr", L"Asr", L"Maghrib", L"Isha"};
    for (UINT i = 0; i < 5; ++i) aip::AppendTrayMenuItem(adjustments, ID_PRAYER_SETTINGS + 20 + i,
        PrayerUiString((L"PrayerLabel" + std::wstring(prayers[i])).c_str(), prayers[i]) + L": " + IniReadS(L"PrayerTimes", g_prayerAdjustmentKeys[i], L"0"), false, true);
    aip::EndTrayNestedMenu(menu, adjustments, PrayerUiString(L"PrayerAdjustments", L"Local minute adjustments"));
    bool sunrise = true, clock24 = false, timetable = true;
    aip::content::ParseBool(IniReadS(L"PrayerTimes", L"ShowSunrise", L"1"), sunrise);
    aip::content::ParseBool(IniReadS(L"PrayerTimes", L"TwentyFourHour", L"0"), clock24);
    aip::content::ParseBool(IniReadS(L"PrayerTimes", L"ShowTimetable", L"1"), timetable);
    aip::AppendTrayMenuItem(menu, ID_PRAYER_SETTINGS + 30, PrayerUiString(L"PrayerShowSunrise", L"Show Sunrise / Shuruq in header and timetable"), sunrise, true);
    aip::AppendTrayMenuItem(menu, ID_PRAYER_SETTINGS + 31, PrayerUiString(L"Prayer24Hour", L"Use 24-hour times"), clock24, true);
    aip::AppendTrayMenuItem(menu, ID_PRAYER_SETTINGS + 32, PrayerUiString(L"PrayerShowTimetable", L"Show prayer timetable on Large tiles"), timetable, true);
    bool nextSunrise=false;
    aip::content::ParseBool(IniReadS(L"PrayerTimes",L"NextIncludesSunrise",L"0"),nextSunrise);
    aip::AppendTrayMenuItem(menu,ID_PRAYER_SETTINGS+33,PrayerUiString(L"PrayerNextSunrise",L"Include Sunrise / Shuruq in next-event countdown"),nextSunrise,true);
    add(40, L"PrayerRefreshSeconds", L"Schedule refresh (seconds)", L"RefreshSeconds");
    add(41, L"PrayerTimeoutSeconds", L"HTTP timeout (seconds)", L"TimeoutSeconds");
    add(42, L"PrayerMaximumBytes", L"Response limit (bytes)", L"MaximumBytes");
    add(60, L"PrayerRefreshNow", L"Refresh prayer schedule now");
}
static bool DispatchPrayerSettingsCommand(HWND owner, UINT command)
{
    if (command < ID_PRAYER_SETTINGS || command > ID_PRAYER_SETTINGS + 132) return false;
    const auto offset = command - ID_PRAYER_SETTINGS;
    if (offset >= 100 && offset <= 123) {
        const auto method = static_cast<int>(offset - 100);
        if (method == 6) return false;
        SaveContentMenuSettings(owner, {{L"PrayerTimes", L"Method", std::to_wstring(method)}}); return true;
    }
    if (offset >= 130 && offset <= 132) {
        SaveContentMenuSettings(owner, {{L"PrayerTimes", L"LatitudeAdjustmentMethod", std::to_wstring(offset - 129)}}); return true;
    }
    if (offset > 60) return false;
    if (offset == 60) { RequestContentPrayerRefresh(); return true; }
    if (offset == 0) {
        auto city = IniReadS(L"PrayerTimes", L"City", L""), country = IniReadS(L"PrayerTimes", L"Country", L"");
        if (ShowTextInputDialog(owner, PrayerUiString(L"PrayerLocation", L"Set city and country..."),
                PrayerUiString(L"PrayerCityPrompt", L"Enter the city manually. Leave blank to keep prayer times unconfigured."), city, true) != IDOK) return true;
        if (ShowTextInputDialog(owner, PrayerUiString(L"PrayerCountry", L"Country"),
                PrayerUiString(L"PrayerCountryPrompt", L"Enter the country for this city. No location detection is used."), country, true) != IDOK) return true;
        SaveContentMenuSettings(owner, {{L"PrayerTimes", L"City", city}, {L"PrayerTimes", L"Country", country}}); return true;
    }
    if (offset == 10 || offset == 11 || offset == 13 || offset == 14) {
        SaveContentMenuSettings(owner, {{L"PrayerTimes", offset < 13 ? L"Method" : L"School", offset == 10 ? L"Auto" : offset == 11 ? L"20" : offset == 13 ? L"0" : L"1"}}); return true;
    }
    if (offset >= 30 && offset <= 33) {
        const auto* key = offset == 30 ? L"ShowSunrise" : offset == 31 ? L"TwentyFourHour" : offset == 32 ? L"ShowTimetable" : L"NextIncludesSunrise"; bool enabled = offset == 30 || offset == 32;
        aip::content::ParseBool(IniReadS(L"PrayerTimes", key, enabled?L"1":L"0"), enabled);
        SaveContentMenuSettings(owner, {{L"PrayerTimes", key, enabled ? L"0" : L"1"}}); return true;
    }
    const wchar_t* key = offset == 1 ? L"City" : offset == 2 ? L"Country" : offset == 3 ? L"State" : offset == 4 ? L"TimeZone" :
        offset == 12 ? L"Method" : offset == 15 ? L"LatitudeAdjustmentMethod" : offset == 40 ? L"RefreshSeconds" :
        offset == 41 ? L"TimeoutSeconds" : offset == 42 ? L"MaximumBytes" : offset >= 20 && offset < 25 ? g_prayerAdjustmentKeys[offset - 20] : nullptr;
    if (!key) return true;
    auto value = IniReadS(L"PrayerTimes", key, L"");
    if (ShowTextInputDialog(owner, PrayerUiString(L"PrayerSettingsTitle", L"Prayer times settings"),
        PrayerUiString(L"PrayerSettingPrompt", L"Edit PrayerTimes setting:") + L" " + key, value, true) == IDOK)
        SaveContentMenuSettings(owner, {{L"PrayerTimes", key, value}});
    return true;
}

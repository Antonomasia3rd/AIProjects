// Private configuration, generated pixels and injected HTTP/clock only.
static aip::prayer::Data PrayerHostFixtureData(const aip::prayer::Configuration& cfg, aip::prayer::UTC now)
{
    using namespace aip::prayer;
    Data data; data.configKey = ConfigurationKey(cfg); data.lastSuccessUtc = now; data.nextRefreshUtc = now + cfg.refreshSeconds;
    const auto date = DateAtUTC(now);
    for (int next = 0; next < 2; ++next) {
        Day day; day.date = AddDays(date, next); day.timeZone = L"Europe/London"; day.fetchedAtUtc = now;
        const int hours[] = {5, 12, 15, 18, 19};
        for (int i = 0; i < 5; ++i) day.prayers[static_cast<std::size_t>(i)] = {static_cast<PrayerId>(i), CivilDay(day.date) * 86400 + hours[i] * 3600, 0};
        day.hasSunrise = true; day.sunrise = {PrayerId::Sunrise, CivilDay(day.date) * 86400 + 6 * 3600, 0};
        data.schedule.days.push_back(day);
    }
    return data;
}
static std::string PrayerHostHttpBody(aip::prayer::Date date, bool iso)
{
    using namespace aip::prayer;
    const char* labels[] = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha", "Sunrise"}; const int hours[] = {5, 12, 15, 18, 19, 6};
    std::string body = "{\"code\":200,\"data\":{\"timings\":{";
    for (int i = 0; i < 6; ++i) {
        if (i) body += ',';
        const auto value = iso ? DateText(date) + L"T" + Two(hours[i]) + L":00:00+00:00" : Two(hours[i]) + L":00";
        body += "\"" + std::string(labels[i]) + "\":\"" + aip::WideToUtf8(value) + "\"";
    }
    return body + "},\"date\":{\"gregorian\":{\"date\":\"" + aip::WideToUtf8(Two(date.day) + L"-" + Two(date.month) + L"-" + std::to_wstring(date.year)) +
        "\"}},\"meta\":{\"timezone\":\"Europe/London\"}}}";
}
static int PrayerHostChooseMapping(HWND) { return 8; }
static void TestPrayerTimesHost(const std::wstring& bitmapPath)
{
    using namespace aip::content;
    std::wstring original, error; ReadIniFileForMutation(original);
    HMENU settingsMenu = CreatePopupMenu(); AppendPrayerSettingsMenu(settingsMenu);
    TrayMenuCommands settingsCommands; InspectTrayMenu(settingsMenu, settingsCommands); DestroyMenu(settingsMenu);
    bool namedMethods = settingsCommands.count(ID_PRAYER_SETTINGS + 10) != 0;
    for (const auto& method : g_prayerMethods) namedMethods = namedMethods && settingsCommands.count(PrayerMethodCommand(method.id)) != 0;
    Check(namedMethods && !settingsCommands.count(ID_PRAYER_SETTINGS + 12) && !settingsCommands.count(ID_PRAYER_SETTINGS + 106),
        "every supported calculation method has a named menu choice without requiring an ID prompt");
    Check(settingsCommands.at(ID_PRAYER_SETTINGS + 130).path.find(L"Middle of the night") != std::wstring::npos &&
        settingsCommands.at(ID_PRAYER_SETTINGS + 131).path.find(L"One seventh") != std::wstring::npos &&
        settingsCommands.at(ID_PRAYER_SETTINGS + 132).path.find(L"Angle based") != std::wstring::npos,
        "all three high-latitude rules are named choices");
    Check(DispatchPrayerSettingsCommand(nullptr, ID_PRAYER_SETTINGS + 103) && IniReadS(L"PrayerTimes", L"Method", L"") == L"3" &&
        DispatchPrayerSettingsCommand(nullptr, ID_PRAYER_SETTINGS + 131) && IniReadS(L"PrayerTimes", L"LatitudeAdjustmentMethod", L"") == L"2",
        "named method and latitude commands store their canonical values without opening numeric dialogs");
    const auto readText = [](const std::wstring& text, const wchar_t* section, const wchar_t* key) {
        const auto document = aip::ParseIniDocument(text); std::wstring value; aip::ReadIniValueFromDoc(document, section, key, value); return value;
    };
    Configuration cfg; SecondaryTileConfiguration tiles;
    const auto oldProfile = L"[Content]\r\nEnabled=0\r\nCount=7\r\n[Tiles]\r\nEnabled=1\r\n";
    Check(ReadSecondaryTileMenuText(oldProfile, cfg, tiles, error) && tiles.tiles.size() == 7 && !tiles.tiles.back().enabled && cfg.prayer.city.empty() && cfg.prayer.country.empty(),
        "old Count7 profiles remain valid and Prayer location/mapping remain unconfigured");
    Check(ContentSourceChoiceOffset(8) == 34 && ContentSourceChoiceForOffset(34)->id == SourceId::PrayerTimes &&
        !ContentSourceChoiceForOffset(32) && !ContentSourceChoiceForOffset(33), "Prayer source choice avoids both existing move commands");
    std::wstring occupied = oldProfile;
    aip::WriteIniValueToText(occupied, L"Content", L"Count", L"8");
    aip::WriteIniValueToText(occupied, L"Content.8", L"Name", L"User's eighth content");
    aip::WriteIniValueToText(occupied, L"Content.8", L"Background", L"Image");
    aip::WriteIniValueToText(occupied, L"Content.8", L"ImagePath", bitmapPath);
    aip::WriteIniValueToText(occupied, L"Content.8", L"TextSources", L"CustomText");
    Check(StageSecondaryTileDefaultLayout(occupied, false, error) && readText(occupied, L"Tile.prayer", L"Content") == L"9" &&
        readText(occupied, L"Content.8", L"Name") == L"User's eighth content" && readText(occupied, L"Content.8", L"TextSources") == L"CustomText",
        "explicit Prayer setup allocates an unused slot without overwriting or reusing occupied Content8");
    std::wstring full = L"[Content]\r\nEnabled=0\r\nCount=32\r\n";
    for (int i = 1; i <= 32; ++i) full += L"[Content." + std::to_wstring(i) + L"]\r\nName=Keep " + std::to_wstring(i) + L"\r\nBackground=None\r\nTextSources=CustomText\r\nText=preserve\r\n";
    const auto before = full;
    Check(!StageSecondaryTileDefaultLayout(full, false, error) && full == before && error == PrayerMappingRequired,
        "full layouts stage no partial edits before asking for an explicit existing mapping");
    const auto savedChoice = g_testPrayerMappingChoice; g_testPrayerMappingChoice = PrayerHostChooseMapping;
    Check(WriteContentFixture(full) && SaveSecondaryTileMenuSettings(nullptr, {}, true, false) &&
        IniReadS(L"Tile.prayer", L"Content", L"") == L"8" && IniReadS(L"Content.8", L"Name", L"") == L"Keep 8" &&
        IniReadS(L"Content.8", L"TextSources", L"") == L"CustomText,PrayerTimes", "full layouts offer a mapping choice and append Prayer without replacing existing text/background settings");
    g_testPrayerMappingChoice = savedChoice;

    using namespace aip::prayer;
    aip::prayer::Configuration prayer; prayer.city = L"Fixture City"; prayer.country = L"GB"; prayer.timeZone = L"Europe/London";
    std::atomic<UTC> now{CivilDay({2026, 10, 18}) * 86400 + 3600};
    std::atomic<int> http{0}, loads{0}, saves{0};
    ContentPrayerProvider provider;
    provider.testClock = [&] { return now.load(); };
    provider.testLoad = [&](const auto&, const auto&, auto&) { ++loads; return false; };
    provider.testSave = [&](const auto&, const auto&, const auto&) { ++saves; };
    provider.testFetch = [&](const auto& settings, UTC at, const auto& previous, const auto& canceled) {
        return aip::prayer::Refresh(settings, at, previous, canceled, [&](const aip::HttpFetchOptions& request, aip::HttpFetchResponse& response, std::wstring& issue) {
            ++http; const auto start = request.url.find(L"timingsByCity/"); Date date;
            if (start == std::wstring::npos || !ParseDate(request.url.substr(start + 14, 10), date, true)) { issue = L"Synthetic date missing"; return false; }
            const auto body = PrayerHostHttpBody(date, request.url.find(L"iso8601=true") != std::wstring::npos);
            response.statusCode = 200; response.body.assign(body.begin(), body.end()); return true;
        });
    };
    provider.Refresh({}, true);
    Check(http == 0 && loads == 0 && !provider.source.HasPendingWork(), "unset location starts no HTTP, cache read or worker request");
    provider.Refresh(prayer, true);
    auto view = provider.Read(prayer);
    Check(http == 4 && loads == 1 && saves == 1 && view.available && view.detailRows.size() == 5 && view.detailRows[1].value == L"12:00",
        "one selected source retrieves cached today/tomorrow response pairs and uses the city's schedule times");
    for (int tick = 0; tick < 10; ++tick) provider.Refresh(prayer, false);
    Check(http == 4 && loads == 1, "unchanged selected polls do not repeat network or persistent cache reads");
    now = CivilDay({2026, 10, 18}) * 86400 + 5 * 3600;
    view = provider.Read(prayer);
    Check(view.secondary.find(L"Fajr") != std::wstring::npos && view.secondary.find(L"0 min") != std::wstring::npos, "exact prayer time can show its zero-minute boundary");
    ++now;
    Check(provider.Read(prayer).secondary.find(L"Dhuhr") != std::wstring::npos && http == 4,
        "the view deadline advances to the following prayer on the next tick within the same minute without HTTP");
    auto adjusted = prayer; adjusted.adjustmentsMinutes[1] = 5; adjusted.twentyFourHour = false;
    const auto adjustedView = provider.Read(adjusted);
    Check(adjustedView.detailRows.size() == 5 && adjustedView.detailRows[1].value == L"12:05 PM" && http == 4, "local adjustment/display edits reformat cached data without new HTTP");
    provider.forceRequested = true; provider.Refresh(prayer, true);
    Check(http == 8 && loads == 1 && provider.Read(prayer).available, "explicit force refresh refetches unexpired daily schedules rather than only clearing the retry deadline");
    provider.Stop();

    ContentPrayerProvider canceling; std::atomic<bool> started{false}, canceledOld{false};
    canceling.testClock = [&] { return now.load(); };
    canceling.testFetch = [&](const auto& settings, UTC at, const auto&, const auto& canceled) {
        if (settings.city == L"Old location") {
            started = true; const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!canceled() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            canceledOld = canceled();
        }
        return PrayerHostFixtureData(settings, at);
    };
    auto old = prayer; old.city = L"Old location";
    canceling.Refresh(old, false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!started && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    canceling.Refresh(prayer, true);
    Check(started && canceledOld && canceling.Read(prayer).title.find(L"Fixture City") != std::wstring::npos,
        "location-key changes promptly cancel old pending work before the busy-source early return");
    canceling.Stop();

    Snapshot frame; frame.active = true; frame.prayerSource = true; frame.text = {L"Next: Dhuhr 12:00 - 20 min", L"Fixture City", L"Europe/London | Sunrise 06:00"};
    const auto fixtureSchedule = PrayerHostFixtureData(prayer, now);
    for (const auto& row : fixtureSchedule.schedule.days.front().prayers) frame.detailRows.push_back({PrayerName(row.id), TimeText(row.utcSeconds, row.offsetMinutes, true), L""});
    std::wstring medium, large;
    Check(AppendPrayerTileBinding(medium, L"TileMedium", 150, frame.text, frame.detailRows) && medium.find(L"Next: Dhuhr") != std::wstring::npos && medium.find(L"<group>") == std::wstring::npos,
        "Medium Prayer tile shows compact next prayer/time/countdown instead of clipping a timetable");
    Check(AppendPrayerTileBinding(large, L"TileLarge", 310, frame.text, frame.detailRows) && large.find(L"Isha") != std::wstring::npos && large.find(L"Sunrise 06:00") != std::wstring::npos,
        "Large Prayer tile retains all five obligatory prayers with sunrise in its separate header");
    const auto compactStatus = TileSnapshotVisibleText(frame, 150, TileStatusLabels{}), largeStatus = TileSnapshotVisibleText(frame, 310, TileStatusLabels{});
    Check(compactStatus.find(L"Isha") == std::wstring::npos && largeStatus.find(L"Isha 19:00") != std::wstring::npos,
        "Prayer tile status follows the same compact versus full timetable content");
    { ScopedGdiPlus gdi; Check(gdi.ok(), "Prayer renderer fixture starts only offscreen GDI+");
        if (gdi.ok()) { Gdiplus::Bitmap canvas(310, 310, PixelFormat32bppARGB); Gdiplus::Graphics graphics(&canvas);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0)); Check(RenderPrayerTileSimulation(graphics, frame, 310, 310, 1), "bitmap fallback renders the five-prayer layout offscreen"); } }
    Check(WriteContentFixture(original), "Prayer host fixture restores the prior private configuration");
}

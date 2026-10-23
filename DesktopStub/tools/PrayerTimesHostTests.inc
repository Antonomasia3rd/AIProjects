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
    Check(http == 4 && loads == 1 && saves == 1 && view.available && view.detailRows.size() == 6 && view.detailRows[2].value == L"12:00 PM" &&
        view.clock.time == L"5:00 AM" && !prayer.twentyFourHour,
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
    Check(adjustedView.detailRows.size() == 6 && adjustedView.detailRows[2].value == L"12:05 PM" && http == 4, "local adjustment/display edits reformat cached data without new HTTP");
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
    frame.prayerClock = {L"11:14 AM",L"Dhuhr",L"Surabaya",L"44",L"in 44 minutes",L"Sunrise 05:06"};
    std::wstring clockMedium,clockWide,clockLarge;
    Check(AppendPrayerTileBinding(clockMedium,L"TileMedium",150,frame.text,frame.detailRows,L"",frame.prayerClock) &&
        clockMedium.find(L"hint-style=\"title\"") != std::wstring::npos && clockMedium.find(L"in 44 minutes") != std::wstring::npos,
        "clock Medium layout gives the next prayer time a larger style and keeps a readable countdown sentence");
    Check(AppendPrayerTileBinding(clockWide,L"TileWide",150,frame.text,frame.detailRows,L"",frame.prayerClock) &&
        clockWide.find(L"headerNumeral") != std::wstring::npos && clockWide.find(L">minutes<") != std::wstring::npos &&
        clockWide.find(L"Isha") == std::wstring::npos,
        "clock Wide layout separates the top-right countdown from the time and location");
    Check(AppendPrayerTileBinding(clockLarge,L"TileLarge",310,frame.text,frame.detailRows,L"",frame.prayerClock) &&
        clockLarge.find(L"Isha") != std::wstring::npos && clockLarge.find(L"at Surabaya") != std::wstring::npos,
        "clock Large layout retains the complete prayer timetable under the prominent next-prayer header");
    const auto clockStatus=TileSnapshotVisibleText(frame,150,TileStatusLabels{});
    Check(clockStatus.find(L"11:14 AM\r\nDhuhr\r\nat Surabaya\r\nin 44 minutes\r\nSunrise 05:06") != std::wstring::npos &&
        clockStatus.find(L"Next: Dhuhr") == std::wstring::npos,
        "clock status uses the semantic clock fields with line breaks rather than the old summary sentence");
    const auto clockKey=ContentSnapshotKey(frame);frame.prayerClock.minutes=L"43";
    Check(ContentSnapshotKey(frame)!=clockKey,"a changed semantic countdown invalidates the snapshot cache even if summary text is unchanged");
    frame.prayerClock.minutes=L"44";frame.prayerClock.showTimetable=false;
    Check(TileSnapshotVisibleText(frame,310,TileStatusLabels{}).find(L"Isha")==std::wstring::npos && ContentSnapshotKey(frame)!=clockKey,
        "timetable visibility updates both tile status and the presentation cache key");
    frame.prayerClock.showTimetable=true;
    {
        const auto tagFor = [](const std::wstring& xml, const std::wstring& text) {
            const auto content = xml.find(L">" + text + L"</text>");
            if (content == std::wstring::npos) return std::wstring{};
            const auto start = xml.rfind(L"<text ", content);
            return start == std::wstring::npos ? std::wstring{} : xml.substr(start, content - start + 1);
        };
        const int oldTemplate = g_liveTileTemplateStyle.load(), oldTarget = g_manifestCompatibilityTarget.load();
        g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Adaptive);
        g_manifestCompatibilityTarget = static_cast<int>(ManifestCompatibilityTarget::Windows10);
        frame.backgroundId = BackgroundId::None;
        const wchar_t* bindings[] = {L"TileMedium", L"TileWide", L"TileLarge"};
        const int widths[] = {150,310,310}, heights[] = {150,150,310};
        ManifestDisplayInfo manifest;
        const auto native = [&](const Snapshot& snapshot, int size) {
            const std::vector<LiveTileUpdateAsset> assets{{L"LivePrayerFixture",L"unused.png",bindings[size],L"absent-prayer-fixture.png",widths[size],heights[size]}};
            ContentRenderScope scope(&snapshot);
            return BuildLiveTileXml(ParentDirectoryOfPath(bitmapPath),manifest,assets);
        };
        for(int size=0;size<3;++size) {
            const auto xml=native(frame,size);
            const auto time=tagFor(xml,L"11:14 AM"), prayerName=tagFor(xml,L"Dhuhr"), location=tagFor(xml,L"at Surabaya");
            Check(!xml.empty() && time.find(L"hint-style=\"title\"")!=std::wstring::npos && TextStylePixels(L"title")==24 &&
                ComputePrayerClockGeometry(widths[size],heights[size],frame.prayerClock,frame.detailRows).timePixels==24,
                "actual native notification keeps the default time at title24 on Medium Wide and Large");
            Check(prayerName.find(L"hint-style=\"captionSubtle\"")!=std::wstring::npos && location.find(L"hint-style=\"captionSubtle\"")!=std::wstring::npos,
                "default prayer name and location share the same subdued text style on every tile size");
        }
        Check(WriteContentFixture(L"[Tile.prayer]\r\nTextPreset=Custom\r\nTextStyle=title\r\nTextSecondaryStyle=subtitleSubtle\r\nTextBadgeStyle=header\r\nTextSecondaryMaxLines=3\r\nTextMaxLines=1\r\nTextWrap=1\r\n"),
            "custom Prayer presentation regression uses only a private INI document");
        Check(ReadTextPresentation(IniReadS,L"Tile.prayer",frame.presentation,error) && frame.presentation.Active(),
            "custom Prayer roles and wrapping are read through the actual INI configuration reader");
        for(int size=0;size<3;++size) {
            const auto xml=native(frame,size);
            const auto prayerName=tagFor(xml,L"Dhuhr"), location=tagFor(xml,L"at Surabaya");
            Check(prayerName.find(L"hint-style=\"subtitleSubtle\"")!=std::wstring::npos && location.find(L"hint-style=\"subtitleSubtle\"")!=std::wstring::npos &&
                prayerName.find(L"hint-maxLines=\"3\"")!=std::wstring::npos && location.find(L"hint-maxLines=\"3\"")!=std::wstring::npos &&
                prayerName.find(L"hint-wrap=\"true\"")!=std::wstring::npos && location.find(L"hint-wrap=\"true\"")!=std::wstring::npos,
                "actual native notification preserves identical custom Secondary styles and wrapping for prayer and city");
            const auto badge=tagFor(xml,size==0?L"in 44 minutes":L"44");
            Check(badge.find(L"hint-style=\"header\"")!=std::wstring::npos && tagFor(xml,L"11:14 AM").find(L"hint-style=\"title\"")!=std::wstring::npos,
                "outer generic text presentation cannot reassign the location to Badge or the badge to Time");
        }
        frame.presentation.wrap=false;
        const auto noWrap=native(frame,1);
        Check(tagFor(noWrap,L"Dhuhr").find(L"hint-wrap=\"false\"")!=std::wstring::npos && tagFor(noWrap,L"at Surabaya").find(L"hint-maxLines=\"1\"")!=std::wstring::npos,
            "turning off wrapping applies to both prayer name and location in the final notification");
        frame.presentation.wrap=true;
        g_liveTileTemplateStyle=oldTemplate;g_manifestCompatibilityTarget=oldTarget;
    }
    { ScopedGdiPlus gdi; Check(gdi.ok(), "Prayer renderer fixture starts only offscreen GDI+");
        if (gdi.ok()) {
            const auto customFrame=frame; frame.presentation={};
            Gdiplus::Bitmap canvas(310, 310, PixelFormat32bppARGB); Gdiplus::Graphics graphics(&canvas);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0)); Check(RenderPrayerTileSimulation(graphics, frame, 310, 310, 1), "bitmap fallback renders the clock and five-prayer layout offscreen");
            auto hasInk=[&](int left,int top,int right,int bottom) {
                for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){Gdiplus::Color pixel;
                    if(canvas.GetPixel(x,y,&pixel)==Gdiplus::Ok && pixel.GetA())return true;}
                return false;
            };
            Check(hasInk(10,10,220,120)&&hasInk(230,10,300,110)&&hasInk(10,130,300,290),
                "offscreen clock paints visible main text, a separate countdown column and lower prayer rows");
            Gdiplus::Bitmap compact(150,150,PixelFormat32bppARGB);Gdiplus::Graphics compactGraphics(&compact);
            compactGraphics.Clear(Gdiplus::Color(0,0,0,0));
            Check(RenderPrayerTileSimulation(compactGraphics,frame,150,150,1),"compact bitmap clock uses the single-column fallback without a device or live tile");
            Gdiplus::Bitmap prayerField(180,180,PixelFormat32bppARGB),cityField(180,180,PixelFormat32bppARGB),singleLine(180,180,PixelFormat32bppARGB);
            Gdiplus::Graphics prayerGraphics(&prayerField),cityGraphics(&cityField),singleGraphics(&singleLine);
            prayerGraphics.Clear(Gdiplus::Color(0,0,0,0));cityGraphics.Clear(Gdiplus::Color(0,0,0,0));singleGraphics.Clear(Gdiplus::Color(0,0,0,0));
            Check(ConfigureTileTextGraphics(prayerGraphics)&&ConfigureTileTextGraphics(cityGraphics)&&ConfigureTileTextGraphics(singleGraphics),
                "isolated Prayer fields use the same grayscale text rendering configuration as generated tile PNGs");
            Gdiplus::REAL prayerY=10,cityY=10,singleY=10;
            const auto longSecondary=L"Surabaya Indonesia Eastern District";
            auto nowrapFrame=customFrame;nowrapFrame.presentation.wrap=false;
            Check(DrawPrayerClockField(prayerGraphics,customFrame,longSecondary,L"caption",1,10,prayerY,100,170,1,1) &&
                DrawPrayerClockField(cityGraphics,customFrame,longSecondary,L"header",1,10,cityY,100,170,2,1) &&
                DrawPrayerClockField(singleGraphics,nowrapFrame,longSecondary,L"caption",1,10,singleY,100,170,2,1),
                "offscreen Prayer and city fields render the actual INI-selected custom Secondary role");
            prayerGraphics.Flush(Gdiplus::FlushIntentionSync);cityGraphics.Flush(Gdiplus::FlushIntentionSync);
            Gdiplus::Rect fieldBounds(0,0,180,180);Gdiplus::BitmapData prayerPixels{},cityPixels{};
            const bool prayerLocked=prayerField.LockBits(&fieldBounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&prayerPixels)==Gdiplus::Ok;
            const bool cityLocked=cityField.LockBits(&fieldBounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&cityPixels)==Gdiplus::Ok;
            bool samePixels=prayerLocked&&cityLocked,hasSecondaryInk=false;unsigned maxSecondaryAlpha=0;
            if(samePixels)for(int y=0;y<180;++y){
                const auto* first=reinterpret_cast<const std::uint32_t*>(static_cast<const BYTE*>(prayerPixels.Scan0)+static_cast<std::ptrdiff_t>(y)*prayerPixels.Stride);
                const auto* second=reinterpret_cast<const std::uint32_t*>(static_cast<const BYTE*>(cityPixels.Scan0)+static_cast<std::ptrdiff_t>(y)*cityPixels.Stride);
                for(int x=0;x<180;++x){const auto alpha=static_cast<unsigned>(first[x]>>24);
                    samePixels=samePixels&&first[x]==second[x];hasSecondaryInk=hasSecondaryInk||alpha!=0;maxSecondaryAlpha=(std::max)(maxSecondaryAlpha,alpha);
                }
            }
            if(prayerLocked)prayerField.UnlockBits(&prayerPixels);if(cityLocked)cityField.UnlockBits(&cityPixels);
            Check(samePixels&&hasSecondaryInk&&prayerY==cityY&&maxSecondaryAlpha<=153,
                "Prayer and location use identical subtitleSubtle font alpha and wrap bounds rather than mixing Secondary and Badge");
            Check(prayerY>singleY+10,"bitmap TextWrap from the actual INI reader permits multiple Secondary lines and disables them when requested");
        } }
    Check(WriteContentFixture(original), "Prayer host fixture restores the prior private configuration");
}

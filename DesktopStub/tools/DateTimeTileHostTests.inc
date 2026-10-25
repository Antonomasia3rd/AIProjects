// Actual typed model, source composition, XML dispatch, bitmap dispatch and
// status adapter. Calendar input is a private local fixture; Kuro is injected.
static bool DateTimeFixtureHasInk(Gdiplus::Bitmap& bitmap,int left,int top,int right,int bottom)
{
    for(int y=top;y<bottom;y+=2)for(int x=left;x<right;x+=2){Gdiplus::Color color;
        if(bitmap.GetPixel(x,y,&color)==Gdiplus::Ok&&color.GetA())return true;}
    return false;
}
static void TestDateTimeTileHost(const std::wstring& directory,const std::wstring& bitmapPath)
{
    using namespace aip::content;
    std::wstring original,error;Check(ReadIniFileForMutation(original),"date/time host fixture captures only its private INI");
    const auto oldRefresh=g_iniCacheRefreshMs.load();g_iniCacheRefreshMs=0;
    const auto oldNotifications=g_notificationsEnabled.load();g_notificationsEnabled=false;
    std::wstring value=L"24hour";
    Check(NormalizeDesktopStubTypedSetting(L"Tile.world",L"Clock.TimeFormat",value,error)&&value==L"24Hour",
        "typed CLI normalizes per-tile Clock time format");
    value=L"20";Check(NormalizeDesktopStubTypedSetting(L"Tile.prayer",L"Alerts.LeadMinutes",value,error)&&value==L"20",
        "typed CLI accepts independent prayer notification lead minutes");
    value=L"0";Check(!NormalizeDesktopStubTypedSetting(L"Tile.feed",L"Alerts.MinimumNewItems",value,error),
        "typed CLI rejects impossible RSS new-item threshold before save");
    value=L"hsr-123";Check(!NormalizeDesktopStubTypedSetting(L"Tile.wuwa",L"Kuro.PlayerId",value,error),
        "typed CLI rejects another game's nonnumeric identifier for WuWa");
    value=L"https://calendar.invalid/private.ics";Check(!NormalizeDesktopStubTypedSetting(L"Tile.calendar",L"Calendar.FilePath",value,error),
        "calendar configuration does not silently fetch a network URL");
    const auto profile=std::wstring(L"[Tiles]\r\nSchemaVersion=1\r\nInstances=utc,jakarta,agenda,wuwa,prayer,feed\r\n")+
        L"[Tile.utc]\r\nType=Clock\r\nName=UTC clock\r\nClock.TimeZone=UTC\r\nClock.TimeFormat=12Hour\r\nClock.Location=UTC fixture\r\n"+
        L"[Tile.jakarta]\r\nType=Clock\r\nName=World clock\r\nClock.TimeZone=SE Asia Standard Time\r\nClock.TimeFormat=24Hour\r\nClock.Location=Surabaya fixture\r\nClock.ShowDate=0\r\n"+
        L"[Tile.agenda]\r\nType=Calendar\r\nName=Calendar fixture\r\nCalendar.TimeZone=UTC\r\nCalendar.MaxEvents=3\r\nCalendar.TimeFormat=12Hour\r\n"+
        L"[Tile.wuwa]\r\nType=WuWa\r\nName=Rover notes\r\n"+
        L"[Tile.prayer]\r\nType=PrayerTimes\r\nName=Prayer fixture\r\nAlerts.Enabled=1\r\nAlerts.LeadMinutes=20\r\nAlerts.Events=Fajr,Shuruq,Dhuhr\r\n"+
        L"[Tile.feed]\r\nType=RSS\r\nName=Feed fixture\r\nAlerts.MinimumNewItems=3\r\nAlerts.CooldownSeconds=120\r\n"+
        L"[Kuro]\r\nGameToken=unused-invalid-global\r\n[Clock]\r\nTimeFormat=unused-invalid-global\r\n";
    Check(WriteContentFixture(profile),"create independent private Clock, Calendar, WuWa and alert settings");
    TileInstanceConfiguration cfg;Check(ReadCurrentTileInstances(cfg,error)&&cfg.tiles.size()==6,"instance reader accepts new source types while ignoring unused global sources");
    if(cfg.tiles.size()!=6){WriteContentFixture(original);g_iniCacheRefreshMs=oldRefresh;g_notificationsEnabled=oldNotifications;return;}
    Check(cfg.tiles[0].clock.timeFormat==L"12Hour"&&cfg.tiles[1].clock.timeFormat==L"24Hour"&&!cfg.tiles[1].clock.showDate&&
        cfg.tiles[0].clock.location!=cfg.tiles[1].clock.location,"world clocks retain independent time formats, labels and date visibility");
    Check(cfg.tiles[3].kuro.gameToken.empty()&&cfg.tiles[4].settings.at(L"alerts.events")==L"Fajr,Dhuhr,Sunrise"&&
        cfg.tiles[4].settings.at(L"alerts.leadminutes")==L"20"&&cfg.tiles[5].settings.at(L"alerts.minimumnewitems")==L"3",
        "typed instance snapshots capture canonical per-tile alerts without inheriting global Kuro credentials");
    const auto kuroDocument=aip::ParseIniDocument(L"[Tiles]\r\nSchemaVersion=1\r\nInstances=left,right\r\n"
        L"[Tile.left]\r\nType=WuWa\r\nKuro.GameToken=INERT-LEFT-TOKEN\r\nKuro.PlayerId=12345\r\nKuro.Region=Europe\r\nKuro.RefreshSeconds=300\r\n"
        L"[Tile.right]\r\nType=WuWa\r\nKuro.GameToken=INERT-RIGHT-TOKEN\r\nKuro.PlayerId=67890\r\nKuro.Region=Asia\r\nKuro.RefreshSeconds=600\r\n");
    TileInstanceConfiguration twoKuro;
    Check(ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring text;
        return aip::ReadIniValueFromDoc(kuroDocument,section,key,text)?text:std::wstring(fallback);},twoKuro,error)&&twoKuro.tiles.size()==2,
        "two WuWa tile accounts parse through the actual immutable instance model without a provider or credential access");
    if(twoKuro.tiles.size()==2)Check(twoKuro.tiles[0].kuro.gameToken==L"INERT-LEFT-TOKEN"&&twoKuro.tiles[1].kuro.gameToken==L"INERT-RIGHT-TOKEN"&&
        twoKuro.tiles[0].settings.at(L"kuro.playerid")==L"12345"&&twoKuro.tiles[1].settings.at(L"kuro.playerid")==L"67890"&&
        twoKuro.tiles[0].kuro.refreshSeconds==300&&twoKuro.tiles[1].kuro.refreshSeconds==600&&
        aip::kuro::ConfigurationKey(twoKuro.tiles[0].kuro)!=aip::kuro::ConfigurationKey(twoKuro.tiles[1].kuro),
        "WuWa credentials, role IDs, per-tile intervals and internal provider identities remain independent");
    const auto fixed=aip::datetime::Seconds({2026,10,23,3,14,59});
    ContentDateTimeProvider fixedProvider;
    const auto utcView=fixedProvider.Clock(cfg.tiles[0].clock,fixed,500),jakartaView=fixedProvider.Clock(cfg.tiles[1].clock,fixed,500);
    Check(utcView.ok&&utcView.time==L"3:14 AM"&&jakartaView.ok&&jakartaView.time==L"10:14"&&jakartaView.date.empty(),
        "real Windows time-zone provider composes independent UTC and Surabaya clocks at one fixed instant");
    Check(utcView.nextDelayMs==500&&jakartaView.nextDelayMs==500,"clock provider wakes at the next exact minute without one-second polling");
    auto unknown=cfg.tiles[0].clock;unknown.timeZone=L"Definitely not a Windows time zone";
    const auto invalid=fixedProvider.Clock(unknown,fixed);
    Check(!invalid.ok&&!invalid.status.empty(),"unknown clock time zone produces an actionable status instead of a guessed time");
    const auto calendarPath=directory+L"\\datetime-calendar.ics";
    Check(WriteUtf8BomTextFile(calendarPath,L"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:fixture\r\nDTSTART:20261023T033000Z\r\nDTEND:20261023T043000Z\r\nSUMMARY:Fixture meeting\r\nLOCATION:Private room\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"),
        "calendar uses an explicitly selected private local fixture");
    auto calendarConfig=cfg.tiles[2].calendar;calendarConfig.filePath=calendarPath;
    ContentDateTimeProvider calendarProvider;
    const auto privateCalendarReader=[&calendarPath](const std::wstring& path,std::wstring& text,std::wstring& issue){
        if(path!=calendarPath){issue=L"The fixture reads only its own private calendar.";return false;}
        return ReadContentCalendarFile(path,text,issue);
    };
    calendarProvider.testReader=privateCalendarReader;
    const auto agenda=calendarProvider.Calendar(calendarConfig,fixed,500,true,true);
    Check(agenda.ok&&agenda.events.size()==1&&agenda.events.front().title==L"Fixture meeting"&&agenda.events.front().location==L"Private room"&&
        agenda.events.front().when.find(L"3:30 AM")!=std::wstring::npos,"calendar provider reads selected .ics events using configured display zone and AM/PM");
    calendarConfig.showLocation=false;const auto noLocation=calendarProvider.Calendar(calendarConfig,fixed,500,false,true);
    Check(noLocation.ok&&noLocation.events.size()==1&&noLocation.events.front().location.empty(),"calendar location display is per tile and does not require re-importing events");
    SYSTEMTIME current{};GetSystemTime(&current);
    const auto currentUtc=aip::datetime::Seconds(DateTimeCivil(current));
    const auto stamp=[](const aip::datetime::Civil& civil){return std::to_wstring(civil.year)+aip::prayer::Two(civil.month)+aip::prayer::Two(civil.day)+
        L"T"+aip::prayer::Two(civil.hour)+aip::prayer::Two(civil.minute)+aip::prayer::Two(civil.second)+L"Z";};
    Check(WriteUtf8BomTextFile(calendarPath,L"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:upcoming\r\nDTSTART:"+
        stamp(aip::datetime::FromSeconds(currentUtc+3600))+L"\r\nDTEND:"+stamp(aip::datetime::FromSeconds(currentUtc+7200))+
        L"\r\nSUMMARY:Upcoming composed event\r\nLOCATION:Private composed room\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"),
        "prepare an upcoming event relative to the host fixture clock without waiting");
    cfg.tiles[2].calendar.filePath=calendarPath;
    TileInstanceProviderState clockState,worldState,calendarState,kuroState;
    calendarState.datetime=std::make_unique<ContentDateTimeProvider>();calendarState.datetime->testReader=privateCalendarReader;
    aip::content::SmtcMediaSnapshot media;
    auto clock=ComposeTileInstance(cfg.tiles[0],clockState,media,false),world=ComposeTileInstance(cfg.tiles[1],worldState,media,false);
    auto calendar=ComposeTileInstance(cfg.tiles[2],calendarState,media,true);
    Check(clock.clockSource&&world.clockSource&&calendar.calendarSource&&!clock.calendarSource&&clock.dateTime.location==L"UTC fixture"&&
        world.dateTime.location==L"Surabaya fixture"&&world.dateTime.date.empty()&&calendar.detailRows.size()==1&&calendar.detailRows.front().primary==L"Upcoming composed event"&&
        calendar.detailRows.front().secondary.find(L"Private composed room")!=std::wstring::npos,
        "ComposeTileInstance routes each real date/time provider and selected calendar file to semantic snapshot fields");
    Check(!clockState.kuro&&!clockState.rss&&!clockState.prayer&&!worldState.kuro&&!calendarState.kuro,
        "Clock and Calendar instances never construct unrelated account or network providers");
    const auto calendarAgain=ComposeTileInstance(cfg.tiles[2],calendarState,media,false);
    Check(calendar.dateTime.time.empty()&&calendarAgain.dateTime.time.empty()&&
        (calendar.dateTime.date!=calendarAgain.dateTime.date||ContentSnapshotKey(calendar)==ContentSnapshotKey(calendarAgain)),
        "calendar content identity excludes an invisible running clock and remains stable across unchanged composition");
    kuroState.kuro=std::make_unique<ContentKuroProvider>();unsigned transportCalls=0;
    kuroState.kuro->testTransport=[&](const auto&,auto&,auto&){++transportCalls;return false;};
    const auto unsetKuro=ComposeTileInstance(cfg.tiles[3],kuroState,media,true);
    Check(transportCalls==0&&unsetKuro.text.primary==L"Wuthering Waves setup"&&unsetKuro.text.secondary.find(L"Sign in to Kuro")!=std::wstring::npos,
        "unconfigured WuWa composition is inert, game-identified and independent from unrelated global credentials");
    auto configuredKuro=cfg.tiles[3];configuredKuro.kuro.gameToken=L"SYNTHETIC-INERT-TOKEN";configuredKuro.kuro.playerId=L"12345";configuredKuro.kuro.region=L"Europe";
    const auto details=L"{\"Base\":{\"Id\":12345,\"Name\":\"Rover\",\"Energy\":120,\"MaxEnergy\":240,\"StoreEnergy\":20,\"MaxStoreEnergy\":480,\"Liveness\":60,\"LivenessMaxCount\":100,\"LivenessUnlock\":true,\"WeeklyInstCount\":2}}";
    const auto roleBody=std::string("{\"code\":0,\"data\":{\"Europe\":")+aip::JsonEscape(details)+"}}";
    kuroState.kuro->testTransport=[&](const auto& options,auto& response,auto&){++transportCalls;
        const auto body=options.url.find(L"oauth/code")!=std::wstring::npos?std::string("{\"codes\":0,\"oauthCode\":\"INERT-CODE\"}"):roleBody;
        response.statusCode=200;response.body.assign(body.begin(),body.end());return true;};
    const auto kuro=ComposeTileInstance(configuredKuro,kuroState,media,true);
    Check(transportCalls==2&&kuro.text.primary==L"Waveplates 120/240"&&kuro.detailRows.size()==4&&kuro.detailRows[2].primary==L"Daily activity",
        "real WuWa adapter with two injected read-only responses reaches typed game detail rows through ComposeTileInstance");
    Check(kuro.text.primary.find(configuredKuro.kuro.gameToken)==std::wstring::npos&&kuro.text.secondary.find(configuredKuro.kuro.gameToken)==std::wstring::npos,
        "configured account token never becomes tile text or visible status");
    clock.dateTime={L"11:14 AM",L"Friday",L"23",L"October 23, 2026",L"Surabaya <fixture>"};clock.text={L"11:14 AM",L"Surabaya fixture",L"October 23, 2026"};
    calendar.dateTime={L"11:14 AM",L"Friday",L"23",L"October 23, 2026",L""};calendar.detailRows={
        {L"First meeting",L"11:30 AM",L""},{L"Second meeting",L"12:30 PM",L""},{L"Third meeting",L"1:30 PM",L""}};
    calendar.calendarEvents={{L"First meeting",L"11:30 AM - noon",L"11:30 AM",L"Today",L"",true,false},
        {L"Second meeting",L"12:30 PM - 1:00 PM",L"12:30 PM",L"Today",L"",true,false},
        {L"Third meeting",L"1:30 PM - 2:00 PM",L"1:30 PM",L"Today",L"",true,false}};
    const auto clockKey=ContentSnapshotKey(clock);clock.dateTime.time=L"11:15 AM";
    Check(ContentSnapshotKey(clock)!=clockKey,"semantic clock minute invalidates content cache even when fallback strings are unchanged");clock.dateTime.time=L"11:14 AM";
    const auto calendarKey=ContentSnapshotKey(calendar);calendar.dateTime.dayNumber=L"24";
    Check(ContentSnapshotKey(calendar)!=calendarKey,"semantic Calendar date stamp invalidates its content cache independently from fallback text");calendar.dateTime.dayNumber=L"23";
    const auto oldTemplate=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    const wchar_t* bindings[]={L"TileMedium",L"TileWide",L"TileLarge"};const wchar_t* assetNames[]={L"LiveMediumTile",L"LiveWideTile",L"LiveLargeTile"};
    const int widths[]={150,310,310},heights[]={150,150,310};
    ScopedGdiPlus gdi;Check(gdi.ok(),"date/time renderer uses only offscreen GDI+");
    const Snapshot* sourceFrames[]={&clock,&calendar,&kuro};
    for(int size=0;size<3;++size)for(const auto* snapshot:sourceFrames) {
        auto frame=*snapshot;frame.backgroundId=BackgroundId::Transparent;frame.policy.included=false;
        const std::vector<LiveTileUpdateAsset> assets{{assetNames[size],L"absent.png",bindings[size],L"absent-datetime-fixture.png",widths[size],heights[size]}};
        ContentRenderScope scope(&frame);ManifestDisplayInfo manifest;
        const auto xml=BuildLiveTileXml(ParentDirectoryOfPath(bitmapPath),manifest,assets);
        Check(!xml.empty(),"native publication builder accepts all three sizes for Clock Calendar and WuWa");
        if(frame.clockSource)Check(xml.find(L"11:14 AM")!=std::wstring::npos&&xml.find(L"Surabaya &lt;fixture&gt;")!=std::wstring::npos,
            "native clock shows semantic time and escaped city label");
        if(frame.calendarSource)Check(xml.find(L"First meeting")!=std::wstring::npos&&
            (xml.find(L"Third meeting")!=std::wstring::npos)==(size==2)&&
            (xml.find(L"headerNumeral")!=std::wstring::npos)==(size==1),"native Calendar uses its distinct per-size event form and keeps the numeral column only on Wide next-event");
        if(snapshot==&kuro)Check(frame.gameSource&&xml.find(L"<group><subgroup hint-textStacking=\"top\">")!=std::wstring::npos&&
            xml.find(L"hint-style=\"base\" hint-wrap=\"true\" hint-maxLines=\"2\" hint-align=\"left\">Waveplates</text>")!=std::wstring::npos&&
            xml.find(L"hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"2\" hint-align=\"left\">120/240")!=std::wstring::npos&&
            (xml.find(L"Daily activity")!=std::wstring::npos)==(size>0)&&xml.find(L"Wuthering Waves setup")==std::wstring::npos&&
            xml.find(L" alt=\"")==std::wstring::npos,
            "native WuWa uses wrapped own-game groups, size-bounded rows and a text fallback without fabricated row icons");
        if(snapshot==&kuro){auto illustrated=frame;illustrated.detailRows.front().iconTopic=L"notes-genshin-resin_full";
            ContentRenderScope illustratedScope(&illustrated);const auto illustratedXml=BuildLiveTileXml(ParentDirectoryOfPath(bitmapPath),manifest,assets);
            const auto rowText=illustratedXml.find(L">Waveplates</text>"),rowArt=illustratedXml.find(L" alt=\"Waveplates\"");
            const auto geometry=ComputeGameRowGeometry(widths[size],true,GameTextPresentation(illustrated.presentation));
            Check(rowText!=std::wstring::npos&&rowArt!=std::wstring::npos&&rowText<rowArt&&
                illustratedXml.find(L"<subgroup hint-weight=\""+std::to_wstring(geometry.iconWeight)+L"\" hint-textStacking=\"center\">")!=std::wstring::npos&&
                geometry.iconX>geometry.textX+geometry.textWidth,
                "native game publication keeps available bundled row art in its separately weighted right column");}
        const auto status=TileSnapshotVisibleText(frame,heights[size],TileStatusLabels{},widths[size]);
        Check(frame.clockSource?(status.find(L"11:14 AM\r\nFriday\r\nSurabaya <fixture>")!=std::wstring::npos&&status.find(L"October 23, 2026")==std::wstring::npos):
            frame.calendarSource?(status.find(L"First meeting")!=std::wstring::npos&&(status.find(L"Third meeting")!=std::wstring::npos)==(size==2)):
            status.find(L"Waveplates\r\n120/240")!=std::wstring::npos,"visible status mirrors semantic source content and current tile size");
        if(gdi.ok()) {
            Gdiplus::Bitmap canvas(widths[size],heights[size],PixelFormat32bppARGB);
            {Gdiplus::Graphics graphics(&canvas);graphics.Clear(Gdiplus::Color(0,0,0,0));}
            Check(ApplyTileTextOverlay(&canvas,assetNames[size],widths[size],heights[size])&&DateTimeFixtureHasInk(canvas,8,8,widths[size]-8,heights[size]-20),
                "actual bitmap overlay dispatcher renders visible Clock Calendar and WuWa contents on transparent pixels");
        }
    }
    g_liveTileTemplateStyle=oldTemplate;g_manifestCompatibilityTarget=oldTarget;
    clockState.Stop();worldState.Stop();calendarState.Stop();kuroState.Stop();
    calendarProvider.Stop();DeleteFileW(calendarPath.c_str());Check(WriteContentFixture(original),"date/time fixture restores its original private configuration");
    g_iniCacheRefreshMs=oldRefresh;g_notificationsEnabled=oldNotifications;
}

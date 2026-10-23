// Native XML contracts extracted from the installed Calendar and Clock modules.
// No installed application is executed and no personal calendar data is read.
static void TestDateTimeParityBaseHost()
{
    using namespace aip::content;
    Snapshot calendar;calendar.active=true;calendar.calendarSource=true;calendar.textMode=TextMode::Auto;
    calendar.dateTime={L"",L"Friday",L"23",L"October 23, 2026",L"",L"Fri"};calendar.text={L"Friday 23",L"",L""};
    std::wstring smallBinding,mediumBinding;
    Check(AppendDateTimeTileBinding(smallBinding,L"TileSmall",71,calendar)&&smallBinding.find(L"branding=\"none\"")!=std::wstring::npos&&
        smallBinding.find(L"hint-textStacking=\"center\"")!=std::wstring::npos&&smallBinding.find(L"<text hint-align=\"center\">Fri</text>")!=std::wstring::npos&&
        smallBinding.find(L"titleNumeral")!=std::wstring::npos,"Calendar Small carries centered abbreviated weekday and titleNumeral date without app-name branding");
    Check(AppendDateTimeTileBinding(mediumBinding,L"TileMedium",150,calendar)&&mediumBinding.find(L"branding=\"none\"")!=std::wstring::npos&&
        mediumBinding.find(L"headerNumeral")!=std::wstring::npos&&mediumBinding.find(L"hint-align=\"right\"")==std::wstring::npos,
        "Calendar date-only Medium centers the weekday and headerNumeral date instead of using the wide date column");
    Snapshot clock=calendar;clock.calendarSource=false;clock.clockSource=true;clock.dateTime.time=L"11:14 AM";clock.dateTime.location=L"Surabaya";
    for(const auto& item:std::vector<std::pair<const wchar_t*,const wchar_t*>>{{L"TileMedium",L"base"},{L"TileWide",L"title"},{L"TileLarge",L"subheader"}}){
        std::wstring xml;Check(AppendDateTimeTileBinding(xml,item.first,std::wstring(item.first)==L"TileLarge"?310:150,clock)&&
            xml.find(L"hint-style=\""+std::wstring(item.second)+L"\"")!=std::wstring::npos&&xml.find(L"displayName=\"Surabaya\"")!=std::wstring::npos&&
            xml.find(L">Surabaya</text>")==std::wstring::npos,"Clock XML uses the installed per-size time style and location branding without duplicate location text");
    }
    auto shortChanged=calendar;shortChanged.dateTime.shortWeekday=L"Fr";
    Check(ContentSnapshotKey(calendar)!=ContentSnapshotKey(shortChanged),"Calendar abbreviated weekday and new date/time renderer identity participate in cache invalidation");
    auto directional=clock;directional.dateTime.rightToLeft=true;std::wstring rtlXml,ltrXml;
    Check(AppendDateTimeTileBinding(rtlXml,L"TileMedium",150,directional)&&AppendDateTimeTileBinding(ltrXml,L"TileMedium",150,clock)&&
        rtlXml.find(L"\x200f" L"11:14 AM")!=std::wstring::npos&&ltrXml.find(L"\x200e" L"11:14 AM")!=std::wstring::npos&&
        ltrXml.find(L"hint-style=\"captionsubtle\">Friday</text>")!=std::wstring::npos,
        "native Clock emits its recovered UI-direction marker and subdued full weekday");
    auto mediumStatus=calendar;mediumStatus.dateTime.location=L"Hidden calendar label";mediumStatus.text.secondary=L"No events";
    const auto centeredStatus=TileSnapshotVisibleText(mediumStatus,150,TileStatusLabels{},150);
    Check(centeredStatus.find(L"Hidden calendar label")==std::wstring::npos&&centeredStatus.find(L"No events")==std::wstring::npos,
        "centered date-only Calendar status excludes invisible location and no-events fields");
    aip::tile_registration::Target staticClock;ApplyDateTimeRegistrationVisuals(clock,staticClock);
    Check(staticClock.displayName==L"Surabaya","Registration Clock preserves the configured city through Windows manifest branding");
    aip::tile_registration::Target staticCalendar;calendar.secondaryBranding=L"Auto";ApplyDateTimeRegistrationVisuals(calendar,staticCalendar);
    Check(staticCalendar.hideMediumName,"native-default Registration Calendar suppresses the date-only Medium name footer");
    calendar.secondaryBranding=L"Name";ApplyDateTimeRegistrationVisuals(calendar,staticCalendar);
    Check(!staticCalendar.hideMediumName,"explicit Registration Calendar branding remains configurable");calendar.secondaryBranding=L"Auto";
    ScopedGdiPlus gdi;Check(gdi.ok(),"date/time parity bitmap check initializes only offscreen GDI+");
    if(gdi.ok()){
        Gdiplus::Bitmap image(71,71,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(0,0,0,0));}
        const auto* previous=g_renderContent;g_renderContent=&calendar;
        Check(ApplyTileTextOverlay(&image,L"SmallTile",71,71)&&DateTimeFixtureHasInk(image,0,0,71,71),"Calendar Small bitmap overlay draws its date instead of silently retaining a static icon");
        g_renderContent=previous;
    }
}

static size_t DateTimeParityCount(const std::wstring& value,const std::wstring& needle)
{
    size_t result=0,position=0;
    while((position=value.find(needle,position))!=std::wstring::npos){++result;position+=needle.size();}
    return result;
}
static std::wstring DateTimeParityBinding(const std::wstring& xml,const wchar_t* name)
{
    const auto first=xml.find(L"<binding template=\""+std::wstring(name)+L"\"");
    if(first==std::wstring::npos)return {};
    const auto last=xml.find(L"</binding>",first);
    return last==std::wstring::npos?std::wstring():xml.substr(first,last+10-first);
}
struct DateTimeParityInk {
    int left=INT_MAX,top=INT_MAX,right=-1,bottom=-1;size_t pixels=0;
    bool Centered(int width,int height,int tolerance)const {
        return pixels>20&&left>0&&top>0&&right<width-1&&bottom<height-1&&
            std::abs(left+right-(width-1))<=2*tolerance&&std::abs(top+bottom-(height-1))<=2*tolerance;
    }
};
static DateTimeParityInk DateTimeParityInkBounds(Gdiplus::Bitmap& bitmap)
{
    DateTimeParityInk result;
    for(int y=0;y<static_cast<int>(bitmap.GetHeight());++y)for(int x=0;x<static_cast<int>(bitmap.GetWidth());++x){Gdiplus::Color pixel;
        if(bitmap.GetPixel(x,y,&pixel)!=Gdiplus::Ok||pixel.GetA()<32)continue;
        result.left=(std::min)(result.left,x);result.right=(std::max)(result.right,x);
        result.top=(std::min)(result.top,y);result.bottom=(std::max)(result.bottom,y);++result.pixels;
    }
    return result;
}
static void TestDateTimeParityHost()
{
    TestDateTimeParityBaseHost();
    using namespace aip::content;
    const auto oldTemplate=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);
    g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    Snapshot calendar;calendar.active=true;calendar.calendarSource=true;calendar.textMode=TextMode::Auto;
    calendar.backgroundId=BackgroundId::Transparent;calendar.secondaryBranding=L"Auto";
    calendar.dateTime={L"",L"Friday",L"23",L"October 23, 2026",L"",L"Fri"};calendar.text={L"Friday 23",L"",L""};
    auto clock=calendar;clock.calendarSource=false;clock.clockSource=true;clock.dateTime.time=L"11:14 AM";clock.dateTime.location=L"Surabaya";
    std::wstring noSmallClock;
    Check(!AppendDateTimeTileBinding(noSmallClock,L"TileSmall",71,clock)&&noSmallClock.empty(),
        "Clock has no invented Small semantic binding where the installed app uses an icon");
    for(const auto* branding:{L"None",L"Logo",L"Name",L"NameAndLogo"})for(const auto* source:{&calendar,&clock}){
        auto frame=*source;frame.secondaryBranding=branding;std::wstring xml;
        const auto expected=std::wstring(branding)==L"None"?L"none":std::wstring(branding)==L"Logo"?L"logo":std::wstring(branding)==L"Name"?L"name":L"nameAndLogo";
        Check(AppendDateTimeTileBinding(xml,L"TileMedium",150,frame)&&xml.find(L"branding=\""+std::wstring(expected)+L"\"")!=std::wstring::npos,
            "explicit Clock and Calendar branding takes priority over the native default");
    }
    const auto fixed=aip::datetime::Seconds({2026,10,23,3,14,59});const auto header=ContentDateTimeHeader(fixed,0,L"UTC",L"12Hour");
    SYSTEMTIME system=DateTimeSystemTime({2026,10,23,3,14,59});wchar_t longDay[128]{},shortDay[128]{};
    const bool localized=GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&system,L"dddd",longDay,128,nullptr)&&
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&system,L"ddd",shortDay,128,nullptr);
    Check(localized&&header.ok&&header.weekday==longDay&&header.shortWeekday==shortDay,
        "full and abbreviated Calendar weekdays follow the current Windows locale");
    auto localizedFrame=calendar;localizedFrame.dateTime.weekday=L"Jum'at & <hari>";localizedFrame.dateTime.shortWeekday=L"Jum";
    std::wstring localizedSmall,localizedMedium;
    Check(AppendDateTimeTileBinding(localizedSmall,L"TileSmall",71,localizedFrame)&&AppendDateTimeTileBinding(localizedMedium,L"TileMedium",150,localizedFrame)&&
        localizedSmall.find(L">Jum</text>")!=std::wstring::npos&&localizedMedium.find(L"Jum&apos;at &amp; &lt;hari&gt;")!=std::wstring::npos,
        "native Calendar selects the localized weekday form and escapes markup");
    for(const auto* source:{&calendar,&clock}){
        auto custom=*source;custom.presentation.preset=L"Custom";custom.presentation.primaryStyle=L"subtitle";
        custom.presentation.secondaryStyle=L"bodySubtle";custom.presentation.align=L"right";custom.presentation.stacking=L"bottom";
        custom.presentation.wrap=false;custom.presentation.primaryLines=3;custom.presentation.secondaryLines=2;std::wstring xml;
        Check(AppendDateTimeTileBinding(xml,L"TileMedium",150,custom)&&xml.find(L"hint-textStacking=\"bottom\"")!=std::wstring::npos&&
            xml.find(L"hint-style=\"subtitle\"")!=std::wstring::npos&&xml.find(L"hint-style=\"bodySubtle\"")!=std::wstring::npos&&
            xml.find(L"hint-wrap=\"false\" hint-maxLines=\"1\" hint-align=\"right\"")!=std::wstring::npos,
            "explicit date/time typography, alignment, stacking and no-wrap remain effective");
    }
    ScopedGdiPlus gdi;Check(gdi.ok(),"expanded date/time parity fixtures use offscreen GDI+ only");
    const auto directory=ParentDirectoryOfPath(g_iniPath)+L"\\datetime-parity";
    Check(EnsureDirectoryTreeExists(directory),"date/time parity owns a private output directory");
    const wchar_t* names[]={L"LiveMediumTile",L"LiveWideTile",L"LiveLargeTile"};const wchar_t* bindings[]={L"TileMedium",L"TileWide",L"TileLarge"};
    const wchar_t* files[]={L"datetime-0.png",L"datetime-1.png",L"datetime-2.png"};
    const int widths[]={150,310,310},heights[]={150,150,310};std::vector<LiveTileUpdateAsset> assets;
    if(gdi.ok())for(size_t index=0;index<3;++index){
        const auto file=L"datetime-"+std::to_wstring(index)+L".png";Gdiplus::Bitmap image(widths[index],heights[index],PixelFormat32bppARGB);
        {Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(0,0,0,0));}
        Check(SavePNG(&image,(directory+L"\\"+file).c_str()),"native date/time XML fixture supplies a real dimension-checked background");
        assets.push_back({names[index],files[index],bindings[index],file,widths[index],heights[index]});
    }
    ManifestDisplayInfo manifest;
    for(const auto* source:{&calendar,&clock})for(const auto mode:{TextMode::Auto,TextMode::Off,TextMode::Overlay}){
        auto frame=*source;frame.textMode=mode;ContentRenderScope scope(&frame);const auto xml=BuildLiveTileXml(directory,manifest,assets);
        const size_t expectedSmall=frame.calendarSource&&mode==TextMode::Auto?1:0;
        Check(!xml.empty()&&DateTimeParityCount(xml,L"template=\"TileSmall\"")==expectedSmall&&DateTimeParityCount(xml,L"<binding ")==3+expectedSmall,
            "publication adds one Calendar Auto Small binding to three M/W/L assets and respects Off/Overlay");
        if(mode==TextMode::Auto&&frame.calendarSource){const auto binding=DateTimeParityBinding(xml,L"TileSmall");
            Check(binding.find(L">Fri</text>")!=std::wstring::npos&&binding.find(L">23</text>")!=std::wstring::npos&&
                binding.find(L"<image")==std::wstring::npos&&GetFileAttributesW((directory+L"\\SmallTile.png").c_str())==INVALID_FILE_ATTRIBUTES,
                "native Calendar Small has date text without needing a Small PNG");}
        if(mode!=TextMode::Auto)Check(xml.find(L"<text")==std::wstring::npos,"Off and baked Overlay add no duplicate semantic date/time text");
    }
    if(gdi.ok())for(int scale:{1,2})for(int logicalSize:{71,150}){
        const bool isSmall=logicalSize==71;Gdiplus::Bitmap image(logicalSize*scale,logicalSize*scale,PixelFormat32bppARGB);
        {Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(0,0,0,0));}ContentRenderScope scope(&calendar);
        Check(ApplyTileTextOverlay(&image,isSmall?L"SmallTile":L"MediumTile",logicalSize,logicalSize),"Calendar bitmap dispatch renders both centered sizes at both DPI scales");
        const auto ink=DateTimeParityInkBounds(image);
        Check(ink.Centered(logicalSize*scale,logicalSize*scale,8*scale)&&
            DateTimeFixtureHasInk(image,0,ink.top,logicalSize*scale,(ink.top+ink.bottom)/2)&&
            DateTimeFixtureHasInk(image,0,(ink.top+ink.bottom)/2,logicalSize*scale,ink.bottom+1),
            "weekday and numeral ink are centered in both axes without clipping at Small/Medium or doubled DPI");
    }
    if(gdi.ok()){
        Gdiplus::Bitmap image(71,71,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(0,0,0,0));}
        auto off=calendar;off.textMode=TextMode::Off;ContentRenderScope scope(&off);
        Check(ApplyTileTextOverlay(&image,L"SmallTile",71,71)&&!DateTimeParityInkBounds(image).pixels,"TextMode Off leaves a transparent Small bitmap free of text");
    }
    // Generate must select the static Small asset before overlay is ever called.
    // Stage a private package and inject its deployment, never the native API.
    std::wstring original,error;Check(ReadIniFileForMutation(original),"static date fixture captures only its private profile");
    const auto oldBase=g_testTileRegistrationBase,oldUser=g_testTileRegistrationUserScope,oldLauncher=g_testTileRegistrationLauncherSource;
    const auto oldResolver=g_testTileRegistrationHostAumid;const auto oldDeploy=g_testTileRegistrationRegister;
    const auto oldOpen=g_testTileRegistrationDeploymentOpen;const auto oldAdmission=g_testGenerateAfterAdmission;
    const auto oldMode=g_liveTileUpdateMode.load(),oldRelaunch=g_relaunchAfterExit.load();
    const auto oldLogging=g_logging.load(),oldNotifications=g_notificationsEnabled.load();const auto oldLog=GetLogPathCopy();
    const auto oldStart=g_notifyOnStart.load(),oldSuccess=g_notifyOnSuccess.load(),oldFailure=g_notifyOnFailure.load(),oldBusy=g_notifyOnBusy.load();
    const auto oldDeployCount=registrationHostDeployments;const bool oldDeploySucceeds=registrationHostDeploySucceeds;const auto oldDeployPath=registrationHostDeploymentPath;
    g_testTileRegistrationBase=directory+L"\\registration";g_testTileRegistrationUserScope=L"3333333333333333";
    g_testTileRegistrationLauncherSource=directory+L"\\inert-launcher.exe";g_testTileRegistrationHostAumid=RegistrationHostAumid;
    g_testTileRegistrationRegister=RegistrationHostDeploy;g_testTileRegistrationDeploymentOpen=nullptr;g_testGenerateAfterAdmission=nullptr;registrationHostDeploySucceeds=true;
    const std::vector<BYTE> launcher{'M','Z','i','n','e','r','t'};const auto source=directory+L"\\transparent.png";
    if(gdi.ok()){Gdiplus::Bitmap image(1,1,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(0,0,0,0));}
        Check(SavePNG(&image,source.c_str()),"static Calendar creates only its private transparent source");}
    const auto profile=std::wstring(L"[Tiles]\r\nSchemaVersion=1\r\nInstances=parity-calendar\r\n[Tile.parity-calendar]\r\nType=Calendar\r\nName=Calendar parity\r\n")+
        L"Delivery.Mode=Registration\r\nDelivery.ManifestTarget=Windows10\r\nAssets.SmallTile=0\r\nAssets.GenerateIconWhenDisabled=0\r\n"+
        L"Assets.ScaleAuto=0\r\nAssets.Scale100=1\r\nAssets.Scale125=0\r\nAssets.Scale150=0\r\nAssets.Scale200=0\r\nAssets.Scale400=0\r\nCache.Enabled=0\r\n"+
        L"[Settings]\r\nLogging=0\r\nNotificationsEnabled=0\r\nNotifyOnStart=0\r\nNotifyOnSuccess=0\r\nNotifyOnFailure=0\r\nNotifyOnBusy=0\r\n";
    TileInstanceConfiguration configured;aip::tile_registration::Target target;
    const bool staged=WriteWholeFileBytes(g_testTileRegistrationLauncherSource,launcher)&&WriteContentFixture(profile)&&ReadCurrentTileInstances(configured,error)&&configured.tiles.size()==1&&
        BuildTileRegistrationTarget(L"parity-calendar",L"Calendar parity",{},target,error)&&PrepareTileRegistrationPackage(target,true,error);
    Check(staged,"Calendar static generation stages synthetic launcher bytes with an injected deployment callback");
    if(staged&&gdi.ok()){
        auto frame=calendar;const auto& tile=configured.tiles.front();frame.policy=tile.policy;frame.name=tile.name;frame.secondaryTileId=TileInstanceNativeId(tile);
        frame.registrationAppearance=tile.appearance;frame.backgroundPath=source;frame.secondaryFrameKey=ContentSnapshotKey(frame);const auto before=registrationHostDeployments;
        const bool generated=Generate(source.c_str(),L"inert Calendar Small parity",false,&frame);const auto output=target.assetsDirectory+L"\\SmallTile.png";std::vector<BYTE> first,second;
        Check(generated&&registrationHostDeployments==before+1&&IsValidGeneratedPngWithDimensions(output,71,71)&&aip::ReadWholeFileBytes(output,first),
            "actual Registration Generate publishes Calendar Small even when the generic Small asset switch is disabled");
        if(generated){Gdiplus::Bitmap image(output.c_str());const auto ink=DateTimeParityInkBounds(image);
            Check(ink.Centered(71,71,8)&&ink.pixels<71*71/2,"generated static Small is centered date ink over transparent pixels rather than a filled icon");}
        frame.dateTime.dayNumber=L"24";frame.text.primary=L"Friday 24";frame.secondaryFrameKey=ContentSnapshotKey(frame);
        Check(Generate(source.c_str(),L"inert Calendar next-day parity",false,&frame)&&aip::ReadWholeFileBytes(output,second)&&first!=second,
            "actual static Calendar day change regenerates Small instead of retaining yesterday's PNG");
    }
    Check(RemoveRegistrationFixtureTree(g_testTileRegistrationBase,g_testTileRegistrationBase),"static Calendar removes only its owned staging root");
    g_testTileRegistrationBase=oldBase;g_testTileRegistrationUserScope=oldUser;g_testTileRegistrationLauncherSource=oldLauncher;
    g_testTileRegistrationHostAumid=oldResolver;g_testTileRegistrationRegister=oldDeploy;g_testTileRegistrationDeploymentOpen=oldOpen;
    registrationHostDeployments=oldDeployCount;registrationHostDeploySucceeds=oldDeploySucceeds;registrationHostDeploymentPath=oldDeployPath;
    InvalidateTileRegistrationReadyCache();Check(WriteContentFixture(original),"date/time parity restores the exact private INI");LoadRuntimeSettingsFromIni(false,false);
    g_testGenerateAfterAdmission=oldAdmission;g_liveTileUpdateMode=oldMode;g_relaunchAfterExit=oldRelaunch;g_logging=oldLogging;g_notificationsEnabled=oldNotifications;
    g_notifyOnStart=oldStart;g_notifyOnSuccess=oldSuccess;g_notifyOnFailure=oldFailure;g_notifyOnBusy=oldBusy;SetLogPathCopy(oldLog);
    g_liveTileTemplateStyle=oldTemplate;g_manifestCompatibilityTarget=oldTarget;
    Check(RemoveRegistrationFixtureTree(directory,directory),"date/time parity removes only its exact private output directory");
}

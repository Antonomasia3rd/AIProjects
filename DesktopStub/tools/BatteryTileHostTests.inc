// Prepared views only. Hardware sampling and native notification delivery are
// deliberately absent; the actual host adapters below use private PNGs/XML.
#include "../../dependencies/battery_tile_layout.h"
static aip::content::BatteryTileText BatteryFixtureText()
{
    aip::content::BatteryTileText text;text.charge=L"80%";text.status=L"Plugged in, not charging";
    text.runtime=L"2 h 15 min remaining";text.wear=L"Wear level: 9.2%";
    text.metricLabel=L"capacity (Wh)";text.metricValue=L"30";text.details=L"Critical action: hibernate";return text;
}
static void TestBatteryTilePlanHost()
{
    using namespace aip::content;const auto text=BatteryFixtureText();
    const auto compactPlan=BuildBatteryTilePlan(text,71,71),medium=BuildBatteryTilePlan(text,150,150),
        wide=BuildBatteryTilePlan(text,310,150),large=BuildBatteryTilePlan(text,310,310);
    Check(compactPlan.smallTile&&compactPlan.stacking==L"center"&&compactPlan.footer==0&&compactPlan.groups.size()==1&&compactPlan.groups[0].columns.size()==1&&
        compactPlan.groups[0].columns[0].texts.size()==1&&compactPlan.groups[0].columns[0].texts[0].text==L"80%"&&compactPlan.groups[0].columns[0].texts[0].align==L"center",
        "Battery Small deliberately centers only the charge rather than shrinking every status field into an unreadable tile");
    Check(!medium.smallTile&&medium.footer>=26&&medium.groups.size()==1&&medium.groups[0].columns.size()==1&&
        medium.groups[0].columns[0].texts.size()==4&&medium.groups[0].columns[0].texts[1].text==text.status&&
        medium.groups[0].columns[0].texts[3].text==text.wear,
        "Battery Medium stacks power status runtime and wear with a clear shared-branding footer");
    Check(wide.groups.size()==1&&wide.groups[0].columns.size()==2&&wide.groups[0].columns[0].weight==70&&wide.groups[0].columns[1].weight==30&&
        wide.groups[0].columns[1].texts[0].text==text.metricLabel&&wide.groups[0].columns[1].texts[1].text==L"30"&&
        wide.groups[0].columns[1].texts[1].align==L"right"&&wide.groups[0].columns[1].texts[1].role==BatteryFieldRole::Badge,
        "Battery Wide reserves a separately weighted top-right Wh metric rather than merging it into the power status");
    Check(large.groups.size()==2&&large.groups[0].columns[0].texts[0].style==L"subheader"&&large.groups[1].columns.size()==1&&
        large.groups[1].columns[0].texts.size()==2&&large.groups[1].columns[0].texts[0].text==text.wear&&large.groups[1].columns[0].texts[1].text==text.details,
        "Battery Large keeps a larger charge and full-width wear and critical-action detail rows");
    std::wstring smallXml,mediumXml,wideXml,largeXml;
    Check(AppendBatteryTileBinding(smallXml,L"TileSmall",71,71,text)&&smallXml.find(L"branding=\"none\"")!=std::wstring::npos&&
        smallXml.find(L"hint-textStacking=\"center\"")!=std::wstring::npos&&smallXml.find(L">80%</text>")!=std::wstring::npos&&smallXml.find(text.status)==std::wstring::npos,
        "Battery Small adaptive binding agrees with its centered charge-only plan");
    Check(AppendBatteryTileBinding(mediumXml,L"TileMedium",150,150,text)&&mediumXml.find(text.status)!=std::wstring::npos&&
        mediumXml.find(text.runtime)!=std::wstring::npos&&mediumXml.find(text.wear)!=std::wstring::npos&&mediumXml.find(text.metricLabel)==std::wstring::npos,
        "Battery Medium XML keeps useful text while deliberately omitting the narrow numeral column");
    Check(AppendBatteryTileBinding(wideXml,L"TileWide",310,150,text)&&wideXml.find(L"<subgroup hint-weight=\"70\">")!=std::wstring::npos&&
        wideXml.find(L"<subgroup hint-weight=\"30\">")!=std::wstring::npos&&wideXml.find(text.status)<wideXml.find(text.metricLabel)&&
        wideXml.find(text.wear)!=std::wstring::npos&&wideXml.find(text.details)==std::wstring::npos,
        "Battery Wide adaptive XML keeps wear beside the separate status and metric columns without Large-only details");
    Check(AppendBatteryTileBinding(largeXml,L"TileLarge",310,310,text)&&largeXml.find(text.wear)!=std::wstring::npos&&largeXml.find(text.details)!=std::wstring::npos,
        "Battery Large adaptive XML includes the optional health and power-policy fields");
    auto unknown=text;unknown.metricValue.clear();const auto noMetric=BuildBatteryTilePlan(unknown,310,150);
    Check(noMetric.groups.size()==1&&noMetric.groups[0].columns.size()==1&&noMetric.groups[0].columns[0].weight==100,
        "unavailable battery capacity returns the whole row to status rather than drawing a guessed numeral");
    auto ratio=text;ratio.rightColumnPercent=0;const auto low=BuildBatteryTilePlan(ratio,310,150);ratio.rightColumnPercent=100;const auto high=BuildBatteryTilePlan(ratio,310,150);
    Check(low.groups[0].columns[1].weight==20&&high.groups[0].columns[1].weight==45,
        "battery column geometry defensively bounds even a directly constructed out-of-range prepared view");
    auto critical=text;critical.charge=L"25%";critical.metricLabel=L"until critical (%)";critical.metricValue=L"5";critical.criticalView=true;
    std::wstring criticalXml;AppendBatteryTileBinding(criticalXml,L"TileWide",310,150,critical);
    Check(criticalXml.find(L">25%</text>")!=std::wstring::npos&&criticalXml.find(L">5</text>")!=std::wstring::npos&&
        criticalXml.find(L"until critical (%)")!=std::wstring::npos&&criticalXml.find(L"capacity (Wh)")==std::wstring::npos&&
        criticalXml.find(text.runtime)<criticalXml.find(text.status),
        "critical presentation orders charge runtime and action while keeping the model's percentage-point delta separate");
    auto escaped=text;escaped.status=L"AC & <battery> \"state\" 'now'";std::wstring escapedXml;
    AppendBatteryTileBinding(escapedXml,L"TileWide",310,150,escaped,{},L"ms-appx:///battery & image.png");
    Check(escapedXml.find(L"AC &amp; &lt;battery&gt; &quot;state&quot; &apos;now&apos;")!=std::wstring::npos&&
        escapedXml.find(L"battery &amp; image.png")!=std::wstring::npos,
        "battery status and private background URIs are escaped independently of their source");
    TextPresentation custom;custom.preset=L"Custom";custom.primaryStyle=L"body";custom.secondaryStyle=L"subtitleSubtle";custom.badgeStyle=L"titleNumeral";
    custom.wrap=false;custom.align=L"center";custom.stacking=L"bottom";custom.gapLines=1;std::wstring styled;
    AppendBatteryTileBinding(styled,L"TileWide",310,150,text,custom);
    Check(styled.find(L"hint-textStacking=\"bottom\"")!=std::wstring::npos&&styled.find(L"hint-style=\"body\" hint-wrap=\"false\" hint-maxLines=\"1\" hint-align=\"center\">80%")!=std::wstring::npos&&
        styled.find(L"hint-style=\"subtitleSubtle\" hint-wrap=\"false\"")!=std::wstring::npos&&
        styled.find(L"hint-style=\"titleNumeral\" hint-wrap=\"false\" hint-maxLines=\"1\" hint-align=\"right\">30")!=std::wstring::npos,
        "battery typography independently customizes charge status and badge while preserving right metric alignment");
    Check(styled.find(L"hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"2\" hint-align=\"right\">capacity (Wh)")!=std::wstring::npos&&
        styled.find(L"hint-minLines=\"1\"")!=std::wstring::npos,
        "capacity units retain their caption size while custom role spacing remains effective");
    std::wstring untouched=L"sentinel";
    Check(!AppendBatteryTileBinding(untouched,L"TileSquare150x150Text04",150,150,text)&&untouched==L"sentinel"&&
        !AppendBatteryTileBinding(untouched,L"TileSmall",20,20,text)&&untouched==L"sentinel"&&
        !AppendBatteryTileBinding(untouched,L"TileWide",310,150,BatteryTileText{})&&untouched==L"sentinel",
        "legacy unsupported empty or invalid-size battery bindings decline atomically for the host's bitmap fallback");
}
#if defined(DESKTOPSTUB_INERT_HARDWARE)
static std::size_t BatteryFixtureCount(const std::wstring& text,const std::wstring& token)
{
    std::size_t count=0,at=0;while((at=text.find(token,at))!=std::wstring::npos){++count;at+=token.size();}return count;
}
static std::wstring BatteryFixtureBinding(const std::wstring& xml,const wchar_t* name)
{
    const auto first=xml.find(L"<binding template=\""+std::wstring(name)+L"\"");if(first==std::wstring::npos)return {};
    const auto end=xml.find(L"</binding>",first);return end==std::wstring::npos?std::wstring{}:xml.substr(first,end+10-first);
}
static bool BatteryFixtureRaster(const aip::content::Snapshot& frame,const wchar_t* name,int width,int height,int scale,
    std::vector<Gdiplus::ARGB>& pixels,std::wstring path={})
{
    Gdiplus::Bitmap image(width*scale,height*scale,PixelFormat32bppARGB);
    {Gdiplus::Graphics graphics(&image);if(graphics.Clear(Gdiplus::Color(0,0,0,0))!=Gdiplus::Ok)return false;}
    ContentRenderScope scope(&frame);if(!ApplyTileTextOverlay(&image,name,width,height))return false;
    pixels.clear();pixels.reserve(static_cast<std::size_t>(width*height*scale*scale));
    for(int y=0;y<height*scale;++y)for(int x=0;x<width*scale;++x){Gdiplus::Color color;
        if(image.GetPixel(x,y,&color)!=Gdiplus::Ok)return false;pixels.push_back(color.GetValue());}
    return path.empty()||SavePNG(&image,path.c_str());
}
static bool BatteryFixtureHasInk(const std::vector<Gdiplus::ARGB>& pixels,int stride,int x1,int y1,int x2,int y2)
{
    for(int y=y1;y<y2;++y)for(int x=x1;x<x2;++x)if((pixels[static_cast<std::size_t>(y)*stride+x]>>24)!=0)return true;return false;
}
static void TestBatteryTileNativeHost()
{
    using namespace aip::content;
    const auto oldStyle=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    Snapshot frame;frame.active=true;frame.batterySource=true;frame.batteryText=BatteryFixtureText();frame.text={frame.batteryText.charge,frame.batteryText.status,frame.batteryText.runtime};
    frame.artworkTopic=L"battery";frame.secondaryTileId=L"AipSource_battery-layout-fixture";frame.secondaryBranding=L"NameAndLogo";
    frame.backgroundId=BackgroundId::Transparent;frame.textMode=TextMode::Auto;frame.policy.included=true;frame.policy.owner=L"battery-layout-fixture";
    frame.policy.values[L"settings.experimentallivetileupdate"]=L"LiveTile";
    const auto directory=aip::GetDirectoryName(g_iniPath)+L"\\BatteryLayout";
    const bool created=CreateDirectoryW(directory.c_str(),nullptr)!=FALSE;Check(created,"Battery renderer fixture creates only a private output directory");
    const wchar_t* bindings[]={L"TileSmall",L"TileMedium",L"TileWide",L"TileLarge"};
    const wchar_t* names[]={L"SmallTile",L"LiveMediumTile",L"LiveWideTile",L"LiveLargeTile"};
    const wchar_t* files[]={L"battery-small.png",L"battery-medium.png",L"battery-wide.png",L"battery-large.png"};
    const int widths[]={71,150,310,310},heights[]={71,150,150,310};
    std::vector<LiveTileUpdateAsset> assets;
    for(int index=0;index<4;++index)assets.push_back({names[index],files[index],bindings[index],files[index],widths[index],heights[index]});
    ManifestDisplayInfo manifest;const auto native=[&](const Snapshot& value,const std::vector<LiveTileUpdateAsset>& selected){ContentRenderScope scope(&value);return BuildLiveTileXml(directory,manifest,selected);};
    const auto xml=native(frame,assets);
    Check(BatteryFixtureCount(xml,L"<binding ")==4&&BatteryFixtureCount(xml,L"template=\"TileSmall\"")==1,
        "real Battery publication emits exactly four size bindings even when a Small asset is supplied alongside its semantic Small binding");
    auto noSmallAssets=assets;noSmallAssets.erase(noSmallAssets.begin());
    Check(BatteryFixtureBinding(native(frame,noSmallAssets),L"TileSmall").find(L">80%</text>")!=std::wstring::npos,
        "real Battery publication supplies its charge-only Small binding without requiring a generated Small PNG");
    const auto wide=BatteryFixtureBinding(xml,L"TileWide"),large=BatteryFixtureBinding(xml,L"TileLarge");
    Check(wide.find(frame.batteryText.wear)!=std::wstring::npos&&wide.find(L"hint-weight=\"30\"")!=std::wstring::npos&&
        wide.find(L"capacity (Wh)")!=std::wstring::npos&&large.find(frame.batteryText.details)!=std::wstring::npos,
        "real native Battery adapter publishes prepared wear capacity and Large-only action details through the dedicated plan");
    for(const auto member:{&BatteryTileText::charge,&BatteryTileText::status,&BatteryTileText::runtime,&BatteryTileText::wear,
        &BatteryTileText::metricLabel,&BatteryTileText::metricValue,&BatteryTileText::details}){
        auto changed=frame;changed.batteryText.*member+=L" changed";Check(ContentSnapshotKey(changed)!=ContentSnapshotKey(frame),
            "each independently prepared Battery field invalidates its immutable content cache");}
    auto changed=frame;++changed.batteryText.rightColumnPercent;Check(ContentSnapshotKey(changed)!=ContentSnapshotKey(frame),"Battery column proportion participates in the content identity");
    changed=frame;changed.batteryText.criticalView=true;Check(ContentSnapshotKey(changed)!=ContentSnapshotKey(frame),"Battery critical field ordering participates in the content identity even when strings are identical");
    const auto smallStatus=TileSnapshotVisibleText(frame,71,TileStatusLabels{},71),wideStatus=TileSnapshotVisibleText(frame,150,TileStatusLabels{},310);
    Check(smallStatus.find(L"80%")!=std::wstring::npos&&smallStatus.find(frame.batteryText.status)==std::wstring::npos&&
        wideStatus.find(frame.batteryText.wear)!=std::wstring::npos&&wideStatus.find(frame.batteryText.metricLabel)!=std::wstring::npos,
        "Battery visible status describes the same size-specific fields as its actual adaptive bindings");
    ScopedGdiPlus gdi;Check(gdi.ok(),"Battery presentation acquires only an offscreen GDI+ scope");
    if(gdi.ok()){
        for(int index=0;index<4;++index)for(int scale:{1,2}){
            std::vector<Gdiplus::ARGB> nativePixels,staticPixels,offPixels;
            const auto output=created&&scale==1?directory+L"\\"+assets[index].file:std::wstring{};
            const bool rendered=BatteryFixtureRaster(frame,names[index],widths[index],heights[index],scale,nativePixels,output);
            Check(rendered&&BatteryFixtureHasInk(nativePixels,widths[index]*scale,0,0,widths[index]*scale,heights[index]*scale),
                "actual Battery bitmap dispatcher paints visible content for every tile size at normal and doubled DPI");
            auto registration=frame;registration.policy.values[L"settings.experimentallivetileupdate"]=L"Registration";
            const bool staticRendered=BatteryFixtureRaster(registration,names[index],widths[index],heights[index],scale,staticPixels);
            bool equalBody=rendered&&staticRendered;const auto bodyBottom=(heights[index]-(index?26:0))*scale;
            if(equalBody)for(int y=0;y<bodyBottom&&equalBody;++y)for(int x=0;x<widths[index]*scale;++x){const auto offset=static_cast<std::size_t>(y)*widths[index]*scale+x;
                if(nativePixels[offset]!=staticPixels[offset]){equalBody=false;break;}}
            Check(equalBody,"Registration Battery bitmap preserves the native plan's body while leaving its separate branding footer clear");
            auto off=frame;off.textMode=TextMode::Off;
            Check(BatteryFixtureRaster(off,names[index],widths[index],heights[index],scale,offPixels)&&
                !BatteryFixtureHasInk(offPixels,widths[index]*scale,0,0,widths[index]*scale,heights[index]*scale),
                "Battery TextMode Off suppresses semantic drawing at every tile size and DPI");
        }
        auto metric=frame;metric.batteryText.status.clear();metric.batteryText.runtime.clear();metric.batteryText.wear.clear();
        std::vector<Gdiplus::ARGB> withMetric,withoutMetric;
        const bool withOk=BatteryFixtureRaster(metric,L"LiveWideTile",310,150,1,withMetric);metric.batteryText.metricValue.clear();
        const bool withoutOk=BatteryFixtureRaster(metric,L"LiveWideTile",310,150,1,withoutMetric);
        Check(withOk&&withoutOk&&BatteryFixtureHasInk(withMetric,310,220,10,300,110)&&!BatteryFixtureHasInk(withoutMetric,310,220,10,300,110),
            "Battery raster really places capacity at the top-right and removes that column when the metric is unavailable");
        auto custom=frame;custom.presentation.preset=L"Custom";custom.presentation.primaryStyle=L"body";custom.presentation.badgeStyle=L"titleNumeral";
        const auto customized=native(custom,assets);
        Check(BatteryFixtureBinding(customized,L"TileWide").find(L"hint-style=\"body\" hint-wrap=\"true\" hint-maxLines=\"2\" hint-align=\"left\">80%")!=std::wstring::npos&&
            BatteryFixtureBinding(customized,L"TileWide").find(L"hint-style=\"titleNumeral\" hint-wrap=\"true\" hint-maxLines=\"2\" hint-align=\"right\">30")!=std::wstring::npos,
            "actual native Battery publication applies custom primary and badge roles once rather than a second generic XML rewrite");
        if(created){bool validImages=true;for(const auto& asset:assets)validImages=validImages&&IsValidGeneratedPngWithDimensions(directory+L"\\"+asset.file,asset.w,asset.h);
            Check(validImages,"legacy Battery fixture provides a separate valid rendered PNG at each declared asset path and size");
            auto hiddenWide=frame;hiddenWide.policy.values[L"tiletext.applytowidetile"]=L"0";
            const auto hiddenXml=native(hiddenWide,assets),hiddenBinding=BatteryFixtureBinding(hiddenXml,L"TileWide");
            Check(!hiddenBinding.empty()&&hiddenBinding.find(L"<image")!=std::wstring::npos&&hiddenBinding.find(L"<text")==std::wstring::npos&&
                hiddenBinding.find(L"80%")==std::wstring::npos&&BatteryFixtureBinding(hiddenXml,L"TileMedium").find(L">80%</text>")!=std::wstring::npos,
                "Battery native size visibility suppresses Wide semantic text without falling into generic text while Medium remains enabled");
            auto legacyFrame=frame;legacyFrame.policy.values[L"settings.livetiletemplatestyle"]=L"Windows81Preset";
            const auto legacy=native(legacyFrame,assets);
            Check(!legacy.empty()&&legacy.find(L"<image")!=std::wstring::npos&&legacy.find(L"<text")==std::wstring::npos&&
                legacy.find(L"battery-wide.png")!=std::wstring::npos,
                "legacy Battery publication delivers the complete rendered PNG rather than replacing its capacity column with generic preset text");
        }
    }
    if(created){for(const auto& asset:assets)DeleteFileW((directory+L"\\"+asset.file).c_str());Check(RemoveDirectoryW(directory.c_str())!=FALSE,
        "Battery renderer fixture removes only its private generated PNG directory");}
    g_liveTileTemplateStyle=oldStyle;g_manifestCompatibilityTarget=oldTarget;
}
static void TestBatteryTileMenuHost()
{
    using namespace aip::content;const auto oldCommands=g_tileInstanceMenuCommands;const auto oldNext=g_nextTileInstanceMenuCommand;
    std::wstring error;const auto document=aip::ParseIniDocument(L"[Tiles]\r\nInstances=power\r\n[Tile.power]\r\nType=Battery\r\nName=Power fixture\r\n");
    TileInstanceConfiguration config;const bool parsed=ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;
        return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);},config,error)&&config.tiles.size()==1;
    Check(parsed,"Battery menu fixture parses its explicit typed source without any sampling");
    if(parsed){ResetTileInstanceMenuCommands();HMENU menu=CreatePopupMenu();AppendTileInstanceDetails(menu,config.tiles.front());
        bool everyOption=true;for(const auto& field:aip::battery::Settings){bool found=false;for(const auto& item:g_tileInstanceMenuCommands)
                if(item.second.id==L"power"&&item.second.key==L"Battery."+std::wstring(field.key)){found=true;break;}
            if(!found)everyOption=false;}
        Check(everyOption,"Battery settings menu exposes every validated source option through the same commands captured by Properties");DestroyMenu(menu);
        ResetTileInstanceMenuCommands();auto desktop=config.tiles.front();desktop.id=L"desktop";desktop.type=TileInstanceType::Wallpaper;desktop.primary=true;
        menu=CreatePopupMenu();AppendTileInstanceDetails(menu,desktop);bool batteryOptions=false;
        for(const auto& item:g_tileInstanceMenuCommands)if(item.second.key.compare(0,8,L"Battery.")==0)batteryOptions=true;
        Check(!batteryOptions,"ordinary Desktop settings remain free of Battery options until a Battery tile is explicitly added");DestroyMenu(menu);}
    g_tileInstanceMenuCommands=oldCommands;g_nextTileInstanceMenuCommand=oldNext;
}
static void TestBatteryTileProviderHost()
{
    using namespace aip::battery;aip::battery::Configuration cfg;cfg.criticalVisibility=Visibility::Off;
    const bool oldRunning=g_running.exchange(true);ContentBatteryProvider provider;unsigned collects=0;bool criticalSample=false;
    provider.testCollect=[&](const native::CollectOptions& options){++collects;native::Collection result;
        Check(options.includeDevices&&options.timeoutMilliseconds==static_cast<unsigned>(cfg.collectionTimeoutMs)&&
            options.ioTimeoutMilliseconds==static_cast<unsigned>(cfg.deviceTimeoutMs),"Battery provider forwards the per-tile bounded device sampling options to its injected collector");
        result.raw.presence=Presence::Present;result.raw.powerSource=PowerSource::AC;result.raw.chargeState=ChargeState::NotCharging;
        result.raw.percent=80;result.raw.remainingSeconds=8100;Device device;device.id=L"inert-battery";
        if(criticalSample){result.raw.powerSource=PowerSource::DC;result.raw.chargeState=ChargeState::Discharging;result.raw.percent=25;result.raw.remainingSeconds=2340;
            result.raw.policy.dc.criticalPercent=20;result.raw.policy.dc.reservePercent=30;result.raw.policy.dc.action=CriticalAction::Hibernate;}
        device.designCapacityMWh=50000;device.fullCapacityMWh=45400;device.currentCapacityMWh=30000;result.raw.devices.push_back(std::move(device));return result;};
    provider.Refresh(cfg,true);bool completed=false;std::wstring diagnostic;const auto view=provider.Read(cfg,completed,diagnostic);
    Check(completed&&view.hasBattery&&view.charge==L"80%"&&view.metricValue==L"30"&&view.wear.find(L"9.2")!=std::wstring::npos&&collects==1,
        "real Battery provider formats injected capacity and wear into the requested normal view without hardware access");
    auto textOnly=cfg;textOnly.titleTemplate=L"Charge: %charge%";provider.Refresh(textOnly,true);const auto reformatted=provider.Read(textOnly,completed,diagnostic);
    Check(completed&&reformatted.charge==L"Charge: 80%"&&collects==1,"Battery text-only changes reformat a captured raw snapshot without another collector invocation");
    criticalSample=true;auto criticalCfg=cfg;criticalCfg.criticalVisibility=Visibility::Always;provider.Refresh(criticalCfg,true,true);
    const auto critical=provider.Read(criticalCfg,completed,diagnostic);
    Check(completed&&critical.criticalView&&critical.charge==L"25%"&&critical.runtime==L"39 min remaining"&&critical.metricValue==L"5"&&
        critical.status.find(L"Hibernate")!=std::wstring::npos&&collects==2,
        "real Battery provider keeps charge25 runtime39minutes and Windows-critical delta25minus20equal5 as distinct prepared fields without executing the action");
    provider.Cancel();const auto canceled=provider.Read(cfg,completed,diagnostic);
    Check(!completed&&!diagnostic.empty()&&!canceled.hasBattery,"Battery provider cancellation discards its prior source view before reuse");
    provider.Stop();g_running=oldRunning;
}
#endif

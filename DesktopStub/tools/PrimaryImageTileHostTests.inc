// Primary wallpaper image/target contracts. Private PNGs and injected updater
// only: no Windows tile, package, VM, wallpaper or user settings are changed.
struct PrimaryImageFixtureUpdater {
    unsigned* updates;bool* queued;unsigned* clears;
    void EnableNotificationQueue(bool value){*queued=value;}
    void Clear(){++*clears;}
    void Update(const std::wstring&){++*updates;}
};
static void TestPrimaryImageTileHost()
{
    using namespace aip::content;
    const auto oldTemplate=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);
    g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    const auto root=aip::GetDirectoryName(g_iniPath)+L"\\PrimaryImageContracts";
    const auto images=root+L"\\Images";
    const bool rootMade=CreateDirectoryW(root.c_str(),nullptr)!=FALSE;
    const bool imagesMade=rootMade&&CreateDirectoryW(images.c_str(),nullptr)!=FALSE;
    Check(imagesMade,"primary image contract owns a private local fixture directory");
    Snapshot primary;primary.active=true;primary.name=L"Desktop";primary.artworkTopic=L"desktop";
    primary.backgroundId=BackgroundId::Wallpaper;primary.textMode=TextMode::Off;primary.secondaryBranding=L"NameAndLogo";
    primary.policy.included=true;primary.policy.owner=L"desktop";
    primary.policy.values[L"settings.experimentallivetileupdate"]=L"LiveTile";
    primary.policy.values[L"settings.appxmanifesttarget"]=L"Windows10";
    primary.policy.values[L"settings.livetiletemplatestyle"]=L"Adaptive";
    const wchar_t* names[]={L"LiveMediumTile",L"LiveWideTile",L"LiveLargeTile"};
    const wchar_t* bindings[]={L"TileMedium",L"TileWide",L"TileLarge"};
    const wchar_t* files[]={L"Images\\wallpaper medium \x6d4b.png",L"Images\\wallpaper wide \x6d4b.png",L"Images\\wallpaper large \x6d4b.png"};
    const int widths[]={150,310,310},heights[]={150,150,310};
    std::vector<LiveTileUpdateAsset> assets;
    ScopedGdiPlus gdi;Check(gdi.ok(),"primary image contract uses only an offscreen image scope");
    bool written=imagesMade&&gdi.ok();
    if(written)for(int index=0;index<3;++index){
        Gdiplus::Bitmap image(widths[index],heights[index],PixelFormat32bppARGB);
        {Gdiplus::Graphics graphics(&image);written=graphics.Clear(Gdiplus::Color(255,31,95,173))==Gdiplus::Ok&&written;}
        written=SavePNG(&image,(root+L"\\"+files[index]).c_str())&&written;
        assets.push_back({names[index],files[index],bindings[index],files[index],widths[index],heights[index]});
    }
    Check(written,"primary image contract creates all three expected wallpaper image sizes");
    if(written){
        ManifestDisplayInfo manifest;
        const auto build=[&](const Snapshot& frame,const std::vector<LiveTileUpdateAsset>& selected){ContentRenderScope scope(&frame);return BuildLiveTileXml(root,manifest,selected);};
        const auto transport=build(primary,assets);
        aip::tile_notification::Payload payload;std::wstring error;
        Check(aip::tile_notification::Decode(transport,payload,error)&&payload.options.secondaryTileId.empty()&&!payload.options.queued&&!payload.options.reset,
            "a primary wallpaper update retains the primary application destination without a secondary ID or queue reset");
        Check(payload.xml.find(L"<text")==std::wstring::npos&&payload.xml.find(L"branding=\"nameAndLogo\"")!=std::wstring::npos,
            "image-only Desktop XML retains its configured branding without requiring text to admit the wallpaper");
        for(const auto* binding:bindings)Check(payload.xml.find(L"template=\""+std::wstring(binding)+L"\"")!=std::wstring::npos,
            "primary wallpaper XML includes Medium Wide and Large adaptive bindings supported by Windows 10");
        Check(payload.xml.find(L"placement=\"background\" hint-overlay=\"0\"")!=std::wstring::npos&&
            payload.xml.find(L"ms-appx:///Images/wallpaper%20wide%20%E6%B5%8B.png")!=std::wstring::npos,
            "primary image XML preserves a visible zero-overlay packaged wallpaper URI with Unicode and spaces encoded");
        auto secondary=primary;secondary.secondaryTileId=L"AipSource_image-contract";
        aip::tile_notification::Payload other;
        Check(aip::tile_notification::Decode(build(secondary,assets),other,error)&&other.xml==payload.xml&&other.options.secondaryTileId==secondary.secondaryTileId,
            "primary and secondary image faces use identical image XML and differ only in the intended delivery target");
        unsigned updates=0,clears=0;bool queued=true;std::wstring destination=L"not selected";
        Check(aip::tile_notification::Deliver(transport,[](const std::wstring& xml){return xml;},[](const std::wstring& xml){return xml;},
            [](std::wstring&,const std::wstring&){},[&](const std::wstring& target){destination=target;return PrimaryImageFixtureUpdater{&updates,&queued,&clears};},error)&&
            destination.empty()&&updates==1&&clears==0&&!queued,
            "the shared delivery adapter actually submits a primary wallpaper through its empty application destination");
        auto invalid=assets;invalid.front().file=L"Images\\missing.png";
        const std::vector<LiveTileUpdateAsset> missing{invalid.front()};
        Check(build(primary,missing).empty(),"missing primary image cannot be acknowledged as a usable wallpaper notification");
        auto wrong=assets.front();wrong.w=149;
        Check(build(primary,{wrong}).empty(),"wrong-dimension primary image cannot be silently admitted as a wallpaper binding");
        {ContentRenderScope scope(&primary);Check(!SecondaryLiveAssetsOnly(true),"primary wallpaper retains main static assets instead of the secondary-only image path");}
        {ContentRenderScope scope(&secondary);Check(SecondaryLiveAssetsOnly(true),"secondary wallpaper retains its isolated live-only asset optimization");}
    }
    if(imagesMade){for(const auto* file:files)DeleteFileW((root+L"\\"+file).c_str());Check(RemoveDirectoryW(images.c_str())!=FALSE,"primary image fixture removes only its private image folder");}
    if(rootMade)Check(RemoveDirectoryW(root.c_str())!=FALSE,"primary image fixture removes only its private root");
    g_liveTileTemplateStyle=oldTemplate;g_manifestCompatibilityTarget=oldTarget;
}

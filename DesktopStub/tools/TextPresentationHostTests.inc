static void TestTextPresentationHost()
{
    using namespace aip::content;
    std::wstring original,error;ReadIniFileForMutation(original);
    auto text=BuildInitialIniTemplate();aip::WriteIniValueToText(text,L"Content",L"Enabled",L"0");
    Check(WriteContentFixture(text),"presentation fixture owns only the private host profile");
    Configuration cfg;SecondaryTileConfiguration tiles;
    Check(ReadContentConfiguration(cfg,error,&tiles)&&!cfg.presentation.Active(),"new presentation defaults do not opt existing profiles into changed pixels");
    Check(DispatchTextPresentationCommand(nullptr,ID_TEXT_PRESENTATION+1)&&DispatchTextPresentationCommand(nullptr,ID_TEXT_PRESENTATION+16+8)&&
        ReadContentConfiguration(cfg,error,&tiles)&&cfg.presentation.Active()&&cfg.presentation.primaryStyle==L"title",
        "named style and preset tray actions use canonical shared configuration");
    Check(SaveContentMenuSettings(nullptr,{{L"Tile.prayer",L"TextStyle",L"body"}})&&ReadContentConfiguration(cfg,error,&tiles)&&
        tiles.tiles.back().presentation.primaryStyle==L"body"&&cfg.presentation.primaryStyle==L"title","source text style overrides inherit global settings without replacing them");
    const auto saved=cfg.presentation;
    Check(!SaveContentMenuSettings(nullptr,{{L"TileText",L"TextMaxLines",L"99"}})&&ReadContentConfiguration(cfg,error)&&
        TextPresentationKey(cfg.presentation)==TextPresentationKey(saved),"invalid line limit applies no partial presentation change");
    Snapshot frame;frame.active=true;frame.text={L"Visible title",L"Description\nsecond line",L"Paused"};frame.presentation=cfg.presentation;
    {ScopedGdiPlus gdi;Check(gdi.ok(),"presentation fixture uses only offscreen GDI+");if(gdi.ok()){
        Gdiplus::Bitmap canvas(150,150,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&canvas);graphics.Clear(Gdiplus::Color(0,0,0,0));
        ContentRenderScope scope(&frame);Gdiplus::Font font(L"Segoe UI",12,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::REAL y=10;
        Check(DrawTopSourceField(graphics,frame.text.primary,font,10,y,130,130,2,255,0)&&y>10,"opt-in named size renders within bounded bitmap fields");
    }}
    frame.presentation = {}; frame.rssHeadlines = {{L"First title",L"First summary",L""},{L"Second title",L"Second summary",L""},{L"Third title",L"Third summary",L""}};
    frame.text = frame.rssHeadlines.front();
    {ScopedGdiPlus gdi;Check(gdi.ok(),"readable RSS fixture uses offscreen rendering only");if(gdi.ok()){
        const int widths[] = {150,310,310}, heights[] = {150,150,310};
        const wchar_t* names[] = {L"MediumTile",L"WideTile",L"LargeTile"};
        for (int dpiScale : {1,2}) for(std::size_t index=0;index<3;++index){
            Gdiplus::Bitmap canvas(widths[index]*dpiScale,heights[index]*dpiScale,PixelFormat32bppARGB);
            {Gdiplus::Graphics graphics(&canvas);graphics.Clear(Gdiplus::Color(0,0,0,0));}
            ContentRenderScope scope(&frame);
            Check(ApplyTileTextOverlay(&canvas,names[index],widths[index],heights[index]),"Default RSS renders every supported bitmap size at 100 and 200 percent scaling");
            const auto rows = RssRichTileRows(widths[index],heights[index]); bool allRowsPainted = true;
            for(std::size_t row=0;row<rows;++row){
                const int top = static_cast<int>((10 + row * (heights[index]-30.0)/rows)*dpiScale);
                const int bottom = static_cast<int>((10 + (row+1) * (heights[index]-30.0)/rows)*dpiScale);
                bool opaqueTitle = false,subtleSummary = false;
                for(int y=top;y<bottom;++y)for(int x=10*dpiScale;x<(widths[index]-10)*dpiScale;++x){Gdiplus::Color pixel;
                    if(canvas.GetPixel(x,y,&pixel)==Gdiplus::Ok){opaqueTitle = opaqueTitle || pixel.GetA()>220;subtleSummary = subtleSummary || (pixel.GetA()>=145 && pixel.GetA()<=160);}}
                allRowsPainted = allRowsPainted && opaqueTitle && subtleSummary;
            }
            Check(allRowsPainted,"each visible RSS row paints a strong title and a subdued summary without leaving later rows blank");
        }
    }}
    frame.textMode = TextMode::Overlay;
    std::wstring rssBitmapKey;
    {ContentRenderScope scope(&frame);rssBitmapKey=BuildGenerationStateKey(L"");}
    frame.presentation.preset = L"RssHeadlines"; frame.presentation.primaryStyle = L"subtitle";
    {ContentRenderScope scope(&frame);
        Check(BuildGenerationStateKey(L"") != rssBitmapKey,"RSS bitmap cache changes when its selected readable presentation changes");
    }
    const auto priorMode=ConfiguredLiveTileUpdateMode(); const auto priorTemplate=ConfiguredLiveTileTemplateStyle();
    const auto priorTarget=ConfiguredManifestCompatibilityTarget();
    const auto priorMediumAsset=IniReadI(L"Assets",L"MediumTile",0);
    const auto priorCacheEnabled=GeneratedAssetCacheEnabled();
    const auto priorCacheEntries=GeneratedAssetCacheMaxEntries();
    const auto oldModeAtomic=g_liveTileUpdateMode.load(), oldTemplateAtomic=g_liveTileTemplateStyle.load(), oldTargetAtomic=g_manifestCompatibilityTarget.load();
    const bool oldScaleAuto=g_generateScaleAuto.load(), oldFallback=g_wallpaperDetectionFallbackOnInvalid.load();
    TileDeliveryPolicy local;local.included=true;local.owner=L"pure-read-fixture";
    local.values={{L"settings.experimentallivetileupdate",L"Registration"},{L"settings.livetiletemplatestyle",L"Windows81Preset"},
        {L"settings.appxmanifesttarget",L"Windows8"},{L"settings.generatescaleauto",L"0"},{L"settings.generatescale100",L"0"},
        {L"settings.generatescale125",L"1"},{L"settings.generatescale150",L"0"},{L"settings.generatescale200",L"1"},
        {L"settings.generatescale400",L"0"},{L"settings.tilewallpaperfit",L"Fit"},{L"settings.disablefitting",L"0"},
        {L"settings.wallpaperdetectionfallbackoninvalid",L"0"},{L"assets.mediumtile",L"false"},
        {L"settings.generatedassetcache",L"0"},{L"settings.generatedassetcachemaxentries",L"17"}};
    {TilePolicyReadScope read(local);
        Check(IniReadS(L"Assets",L"MediumTile",L"missing")==L"false" && IniReadI(L"Assets",L"MediumTile",1)==0 &&
            IniReadI(L"Settings",L"GeneratedAssetCacheMaxEntries",32)==17 &&
            IniReadClampedI(L"Settings",L"GeneratedAssetCacheMaxEntries",32,0,9)==9 &&
            !GeneratedAssetCacheEnabled() && GeneratedAssetCacheMaxEntries()==17,
            "string, typed boolean, integer, clamped integer and cache getters all use the captured tile policy before global INI values");
        Check(ConfiguredLiveTileUpdateMode()==LiveTileUpdateMode::Registration && ConfiguredLiveTileTemplateStyle()==LiveTileTemplateStyle::Windows81Preset &&
            ConfiguredManifestCompatibilityTarget()==ManifestCompatibilityTarget::Windows8 && GetConfiguredScales()==std::vector<int>({125,200}) &&
            GetWallpaperFit()==FitMode::Fit && !WallpaperDetectionFallbackEnabled(),
            "captured tile getters honor transport, template, target, manual DPI and fitting without registration or desktop queries");
        auto other=local;other.values[L"settings.experimentallivetileupdate"]=L"LiveTile";other.values[L"settings.tilewallpaperfit"]=L"Stretch";
        other.values[L"assets.mediumtile"]=L"true";other.values[L"settings.generatedassetcache"]=L"1";other.values[L"settings.generatedassetcachemaxentries"]=L"5";
        {TilePolicyReadScope nested(other);Check(ConfiguredLiveTileUpdateMode()==LiveTileUpdateMode::LiveTile && GetWallpaperFit()==FitMode::Stretch,
            "a nested tile policy cannot overwrite its caller's active delivery or image fitting");
            Check(IniReadI(L"Assets",L"MediumTile",0)==1 && GeneratedAssetCacheEnabled() && GeneratedAssetCacheMaxEntries()==5,
                "a nested tile independently overrides typed asset and cache values");}
        Check(ConfiguredLiveTileUpdateMode()==LiveTileUpdateMode::Registration && GetWallpaperFit()==FitMode::Fit &&
            IniReadI(L"Assets",L"MediumTile",1)==0 && !GeneratedAssetCacheEnabled() && GeneratedAssetCacheMaxEntries()==17,
            "nested getter scope restores the previous captured tile policy");
    }
    Check(ConfiguredLiveTileUpdateMode()==priorMode && ConfiguredLiveTileTemplateStyle()==priorTemplate && ConfiguredManifestCompatibilityTarget()==priorTarget &&
        g_liveTileUpdateMode.load()==oldModeAtomic && g_liveTileTemplateStyle.load()==oldTemplateAtomic && g_manifestCompatibilityTarget.load()==oldTargetAtomic &&
        g_generateScaleAuto.load()==oldScaleAuto && g_wallpaperDetectionFallbackOnInvalid.load()==oldFallback &&
        IniReadI(L"Assets",L"MediumTile",0)==priorMediumAsset && GeneratedAssetCacheEnabled()==priorCacheEnabled &&
        GeneratedAssetCacheMaxEntries()==priorCacheEntries,
        "per-tile reads leave legacy atomics unchanged and restore unscoped application behavior");
    Check(WriteContentFixture(original),"presentation fixture restores prior private configuration");
}

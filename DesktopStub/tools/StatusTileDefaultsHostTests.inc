// Actual XML and offscreen dispatch, with prepared strings only. No source
// provider, hardware collector, account or package operation is started.
static aip::content::Snapshot StatusDefaultsFrame(unsigned source){
    using namespace aip::content;Snapshot frame;frame.active=true;frame.textMode=TextMode::Auto;frame.backgroundId=BackgroundId::Transparent;
    frame.secondaryTileId=L"AipSource_status-default-fixture";frame.secondaryBranding=L"None";
    frame.policy.included=true;frame.policy.owner=L"status-default-fixture";frame.policy.values[L"settings.experimentallivetileupdate"]=L"LiveTile";
    frame.text={L"2:28 PM",L"",L""};
    if(source==0){frame.clockSource=true;frame.dateTime.time=frame.text.primary;}
    if(source==1){frame.prayerSource=true;frame.prayerClock.time=frame.text.primary;frame.prayerClock.prayer=L"Fajr";frame.prayerClock.showTimetable=false;}
    if(source==2){frame.batterySource=true;frame.batteryText.charge=frame.text.primary;}
    if(source==3){frame.smtcSource=true;frame.smtcIndicator=MediaIndicatorMode::None;}
    if(source==4)frame.wrapSourceText=true;
    return frame;
}
static void TestStatusTileDefaultsHost(){
    using namespace aip::content;
    const auto oldStyle=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    struct Restore{int style,target;~Restore(){g_liveTileTemplateStyle=style;g_manifestCompatibilityTarget=target;}} restore{oldStyle,oldTarget};
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    const wchar_t* names[]={L"LiveMediumTile",L"LiveWideTile",L"LiveLargeTile"};
    const wchar_t* bindings[]={L"TileMedium",L"TileWide",L"TileLarge"};
    const wchar_t* expected[]={L"base",L"title",L"subheader"};const int widths[]={150,310,310},heights[]={150,150,310};
    const wchar_t* sources[]={L"clock",L"prayer",L"battery",L"smtc",L"discord"};
    const auto directory=aip::MakeAbsolutePath(L"artifacts\\desktopstub-layout-20261028");
    Check(EnsureDirectoryTreeExists(directory),"standard status witness directory is local and contains no account state");
    ManifestDisplayInfo manifest;
    for(size_t size=0;size<3;++size){
        const std::vector<LiveTileUpdateAsset> assets={{names[size],L"status.png",bindings[size],L"status.png",widths[size],heights[size]}};
        for(unsigned source=0;source<5;++source){
            auto frame=StatusDefaultsFrame(source);std::wstring xml;{ContentRenderScope scope(&frame);xml=BuildLiveTileXml(directory,manifest,assets);}
            const auto binding=BatteryFixtureBinding(xml,bindings[size]);
            Check(binding.find(L"hint-style=\""+std::wstring(expected[size])+L"\"")!=std::wstring::npos,
                "real status publication uses native Clock's size-specific primary heading");
            const auto before=ContentSnapshotKey(frame);if(source)Check(before.find(L"StatusHeadingClock1")!=std::wstring::npos,
                "implicit changed layouts invalidate a pre-update status snapshot cache");
            frame.presentation.preset=L"Custom";frame.presentation.primaryStyle=L"subtitle";frame.presentation.wrap=false;frame.presentation.primaryLines=1;
            {ContentRenderScope scope(&frame);xml=BuildLiveTileXml(directory,manifest,assets);}
            Check(BatteryFixtureBinding(xml,bindings[size]).find(L"hint-style=\"subtitle\"")!=std::wstring::npos&&
                frame.presentation.primaryStyle==L"subtitle"&&ContentSnapshotKey(frame).find(L"StatusHeadingClock1")==std::wstring::npos,
                "real native path preserves an explicit custom primary style and its cache policy");
        }
    }
    ScopedGdiPlus gdi;Check(gdi.ok(),"standard status witnesses use only offscreen GDI+");
    if(gdi.ok())for(size_t size=0;size<3;++size)for(int scale:{1,2}){
        int clockWidth=0;
        for(unsigned source=0;source<5;++source){
            const auto frame=StatusDefaultsFrame(source);std::vector<Gdiplus::ARGB> pixels;
            const auto file=scale==1?directory+L"\\status-"+sources[source]+L"-"+std::to_wstring(size)+L".png":std::wstring{};
            const bool rendered=BatteryFixtureRaster(frame,names[size],widths[size],heights[size],scale,pixels,file);
            int left=INT_MAX,right=-1;
            if(rendered)for(int y=10*scale;y<(10+StatusTilePrimaryPixels(widths[size],heights[size])+3)*scale;++y)
                for(int x=0;x<widths[size]*scale;++x)if(pixels[static_cast<size_t>(y)*widths[size]*scale+x]>>24){left=(std::min)(left,x);right=(std::max)(right,x);}
            const auto inkWidth=right>=left?right-left+1:0;if(!source)clockWidth=inkWidth;
            Check(rendered&&inkWidth>0&&inkWidth==clockWidth,
                "actual status bitmap headings have the reference Clock ink width at 100 and 200 percent scale");
        }
    }
    // Long default content exercises all field allocations, instead of relying
    // on a short song title that cannot expose the old fixed-line overflow.
    std::wstring metrics;
    if(gdi.ok())for(size_t size=0;size<3;++size)for(bool extras:{false,true})for(int scale:{1,2}){
        auto frame=StatusDefaultsFrame(3);frame.text={L"A deliberately long playing title repeated to force the complete allowed header allocation with additional words",
            L"A deliberately long artist name repeated to exercise its bounded secondary allocation",
            L"Playing - an application with a long display name\nRepeat track\nShuffle on"};
        frame.smtcShowRepeatShuffle=extras;frame.smtcRepeat=MediaRepeatMode::Track;frame.smtcShuffle=MediaToggleState::On;
        frame.smtcState=MediaPlaybackState::Playing;frame.smtcIndicator=MediaIndicatorMode::Status;
        frame.secondaryBranding=L"NameAndLogo";frame.artworkTopic=L"smtc";frame.policy.values[L"settings.experimentallivetileupdate"]=L"Registration";
        const auto budget=StatusMediaTextBudget(widths[size],heights[size],extras);
        std::wstring xml;std::vector<MediaIndicatorImage> icons;if(extras)icons={{L"repeat.png",L"Repeat"},{L"shuffle.png",L"Shuffle"}};
        AppendMediaTileBinding(xml,bindings[size],frame.text,L"",{},icons);
        Check(xml.find(L"hint-maxLines=\""+std::to_wstring(budget.primary)+L"\">")!=std::wstring::npos&&
            xml.find(frame.text.primary)!=std::wstring::npos&&xml.find(frame.text.secondary)!=std::wstring::npos&&xml.find(frame.text.badge)!=std::wstring::npos,
            "native long-title layout retains full strings with explicit size-aware visible line budgets");
        Gdiplus::Bitmap image(widths[size]*scale,heights[size]*scale,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(0,0,0,0));ConfigureTileTextGraphics(graphics);
        Gdiplus::Font font(L"Segoe UI",static_cast<REAL>(12*scale),Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
        Gdiplus::Font primaryFont(size?L"Segoe UI Light":L"Segoe UI Semibold",static_cast<REAL>(StatusTilePrimaryPixels(widths[size],heights[size])*scale),Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
        const auto layout=ComputeMediaTileGeometry(widths[size],heights[size],true,extras?2:0);
        REAL y=static_cast<REAL>(layout.textY*scale);const REAL bottom=static_cast<REAL>((heights[size]-26)*scale);
        ContentRenderScope scope(&frame);const auto textWidth=static_cast<REAL>(layout.textWidth*scale),x=static_cast<REAL>(layout.textX*scale);
        const auto completeBudget=primaryFont.GetHeight(&graphics)*budget.primary+font.GetHeight(&graphics)*(budget.secondary+budget.badge)+6*scale;
        metrics+=L"size="+std::to_wstring(size)+L" scale="+std::to_wstring(scale)+L" metadata="+std::to_wstring(extras)+
            L" primaryLine="+std::to_wstring(primaryFont.GetHeight(&graphics))+L" captionLine="+std::to_wstring(font.GetHeight(&graphics))+
            L" completeBudget="+std::to_wstring(completeBudget)+L" body="+std::to_wstring(bottom-y)+L"\r\n";
        Check(SourceTextBottomInset(frame)==26&&completeBudget<=bottom-y,
            "actual font line heights plus both 3px gaps fit production NameAndLogo Registration body at both scales");
        Check((layout.iconY+layout.iconSide*(extras?3:1)+(extras?16:0))*scale<=bottom+.02f,
            "actual production indicator stack stays above the same name/logo footer at both scales");
        bool drawn=DrawTopSourceField(graphics,frame.text.primary,font,x,y,textWidth,bottom,budget.primary,255,static_cast<REAL>(3*scale),nullptr,false,StatusTilePrimaryStyle(widths[size],heights[size]));
        const auto primaryEnd=y;drawn=drawn&&DrawTopSourceField(graphics,frame.text.secondary,font,x,y,textWidth,bottom,budget.secondary,153,static_cast<REAL>(3*scale));
        const auto secondaryEnd=y;drawn=drawn&&DrawTopSourceField(graphics,frame.text.badge,font,x,y,textWidth,bottom,budget.badge,153,0);
        Check(drawn&&primaryEnd>10&&secondaryEnd>primaryEnd&&y>secondaryEnd&&y<=bottom+.02f,
            "default long titles and artists leave a visible final status field inside the shared branding-safe body");
        const auto suffix=std::to_wstring(size)+(extras?L"-extras-":L"-plain-")+std::to_wstring(scale*100)+L".png";
        const auto file=directory+L"\\status-production-long-"+suffix;
        std::vector<Gdiplus::ARGB> actual,without;Check(BatteryFixtureRaster(frame,names[size],widths[size],heights[size],scale,actual,file),
            "actual full Now Playing dispatcher exports long title/artist/status with production branding and indicators");
        auto noBadge=frame;noBadge.text.badge.clear();const bool withoutOk=BatteryFixtureRaster(noBadge,names[size],widths[size],heights[size],scale,without);
        const auto regionChanged=[&](const std::vector<Gdiplus::ARGB>& first,const std::vector<Gdiplus::ARGB>& second,int top,int end){
            if(first.size()!=second.size()||first.empty())return false;
            for(int row=(std::max)(0,top);row<(std::min)(static_cast<int>(bottom),end);++row)
                for(int column=static_cast<int>(x);column<static_cast<int>(x+textWidth);++column){const auto index=static_cast<size_t>(row)*widths[size]*scale+column;if(first[index]!=second[index])return true;}return false;};
        Check(withoutOk&&regionChanged(actual,without,static_cast<int>(secondaryEnd),static_cast<int>(bottom)),
            "the final long status role really paints ink above the footer instead of silently disappearing after prior fields");
        if(extras){
            auto marker=frame;marker.text.badge=L"Playing\nRepeat track\nShuffle on";
            auto withoutRepeat=marker;withoutRepeat.text.badge=L"Playing\n \nShuffle on";
            std::vector<Gdiplus::ARGB> withRepeat,blankRepeat;
            const bool markers=BatteryFixtureRaster(marker,names[size],widths[size],heights[size],scale,withRepeat,directory+L"\\status-production-repeat-"+suffix)&&
                BatteryFixtureRaster(withoutRepeat,names[size],widths[size],heights[size],scale,blankRepeat);
            Check(markers&&regionChanged(withRepeat,blankRepeat,static_cast<int>(secondaryEnd+font.GetHeight(&graphics)),static_cast<int>(secondaryEnd+2*font.GetHeight(&graphics))),
                "known Repeat metadata paints actual second-line ink under production branding at both scales");
            auto withoutShuffle=marker;withoutShuffle.text.badge=L"Playing\nRepeat track\n ";std::vector<Gdiplus::ARGB> blankShuffle;
            Check(markers&&BatteryFixtureRaster(withoutShuffle,names[size],widths[size],heights[size],scale,blankShuffle)&&
                regionChanged(withRepeat,blankShuffle,static_cast<int>(secondaryEnd+2*font.GetHeight(&graphics)),static_cast<int>(secondaryEnd+3*font.GetHeight(&graphics))),
                "known Shuffle metadata paints actual final-line ink above the production footer at both scales");
        }
    }
    if(gdi.ok())Check(WriteUtf8BomTextFile(directory+L"\\status-production-font-metrics.txt",metrics),"actual measured status budgets are retained with the local visual witnesses");
}

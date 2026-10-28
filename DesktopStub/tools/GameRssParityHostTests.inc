// All four actual typed game routes, shared native binding and bitmap layout.
// Accounts stay blank and transports are inert; sample resources are injected.
static void TestGameRssParityHost(){
    using namespace aip::content;std::wstring error;
    const auto document=aip::ParseIniDocument(L"[Tiles]\r\nInstances=genshin,hsr,zzz,wuwa\r\n[Tile.genshin]\r\nType=Genshin\r\n[Tile.hsr]\r\nType=HSR\r\n[Tile.zzz]\r\nType=ZZZ\r\n[Tile.wuwa]\r\nType=WuWa\r\n");
    TileInstanceConfiguration cfg;Check(ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;
        return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);},cfg,error)&&cfg.tiles.size()==4,"all four game types parse with independent default presentation and no account data");
    const auto notifications=g_notificationsEnabled.load();g_notificationsEnabled=false;
    ScopedGdiPlus gdi;Check(gdi.ok(),"game parity bitmap fixture uses offscreen GDI only");
    for(const auto& tile:cfg.tiles){TileInstanceProviderState provider;const auto routed=ComposeTileInstance(tile,provider,SmtcMediaSnapshot{},false,false);provider.Stop();
        Check(routed.gameSource&&!routed.prayerSource&&!routed.smtcSource,"Genshin HSR ZZZ and WuWa each route through the common game renderer");
        auto frame=routed;frame.detailRows={{L"First resource",L"120/240",L""},{L"Second resource",L"4/5",L""},{L"Third resource",L"Ready",L""},{L"Fourth resource",L"Hidden",L""}};
        if(tile.type!=TileInstanceType::Wuwa)frame.detailRows[0].iconTopic=aip::notes::ResourceRowIcon(static_cast<aip::notes::ResourceKind>(DescribeTileInstanceType(tile.type).notesResource),aip::notes::StateKind::Ok);
        for(const auto size:std::vector<std::pair<int,int>>{{150,150},{310,150},{310,310}}){
            const auto effective=GameTextPresentation(frame.presentation,size.first,size.second),rss=RssRichPresentation(TextPresentation{},size.first,size.second);
            Check(effective.primaryStyle==rss.primaryStyle&&effective.primaryLines==rss.primaryLines&&effective.secondaryLines==rss.secondaryLines,"every typed game shares RSS's effective per-size text budget");
            const auto limit=RssRichTileRows(size.first,size.second);std::wstring xml;
            const std::vector<MediaIndicatorImage> icons=tile.type==TileInstanceType::Wuwa?std::vector<MediaIndicatorImage>{}:std::vector<MediaIndicatorImage>{{L"ms-appx:///fixture-resource.png",L"Resource"}};
            Check(AppendGameTileBinding(xml,size.first==150?L"TileMedium":size.second==150?L"TileWide":L"TileLarge",size.first,size.second,frame,L"",icons)&&
                (xml.find(L"Second resource")!=std::wstring::npos)==(limit>=2)&&(xml.find(L"Third resource")!=std::wstring::npos)==(limit>=3)&&xml.find(L"Fourth resource")==std::wstring::npos,"each game's native XML uses RSS resource capacities at all sizes");
            Check(icons.empty()?xml.find(L"<image")==std::wstring::npos:xml.find(L"First resource")<xml.find(L"fixture-resource.png"),"available game artwork follows text; WuWa keeps full-width text without fabricated artwork");
            const auto visible=TileSnapshotVisibleText(frame,size.second,TileStatusLabels{},size.first);
            Check((visible.find(L"Second resource")!=std::wstring::npos)==(limit>=2)&&(visible.find(L"Third resource")!=std::wstring::npos)==(limit>=3)&&visible.find(L"Fourth resource")==std::wstring::npos,"contents adapter uses the same game resource capacity as native XML");
            if(gdi.ok()){Gdiplus::Bitmap bitmap(size.first,size.second,PixelFormat32bppARGB);bool rendered=false;
                {Gdiplus::Graphics graphics(&bitmap);graphics.Clear(Gdiplus::Color(0,0,0,0));rendered=RenderGameRowsSimulation(graphics,frame,size.first,size.second,1.0);}
                Check(rendered,"all four game default layouts render through the real bitmap path");
                bool ink=false;for(int y=8;y<100&&!ink;y+=2)for(int x=8;x<size.first-8;x+=2){Gdiplus::Color pixel;bitmap.GetPixel(x,y,&pixel);if(pixel.GetA()){ink=true;break;}}
                Check(ink,"game bitmap contains visible resource text");}
        }
        auto stale=frame;stale.detailRows.insert(stale.detailRows.begin(),{L"",L"Refreshed 5m ago\nRefresh delayed",L""});
        Check(GameVisibleRowCount(stale,150,150)==2&&TileSnapshotVisibleText(stale,150,TileStatusLabels{},150).find(L"First resource")!=std::wstring::npos,"Medium freshness metadata keeps one useful game resource visible");
    }
    {
        Snapshot fallback;fallback.active=true;fallback.gameSource=true;fallback.wrapSourceText=true;fallback.backgroundId=BackgroundId::Transparent;
        fallback.text={L"Game setup",L"Choose an account",L""};fallback.presentation.preset=L"Custom";
        fallback.presentation.primaryStyle=L"subtitle";fallback.presentation.secondaryStyle=L"captionSubtle";
        fallback.policy.included=true;fallback.policy.values[L"settings.livetiletemplatestyle"]=L"Adaptive";
        std::vector<LiveTileUpdateAsset> assets={{L"Medium",L"MediumTile.png",L"TileMedium",L"MediumTile.png",150,150}};
        ContentRenderScope scope(&fallback);const auto xml=BuildLiveTileXml(GetExeDir(),CurrentManifestDisplayInfo(),assets);
        Check(xml.find(L"hint-style=\"subtitle\"")!=std::wstring::npos&&xml.find(L"Game setup")!=std::wstring::npos,
            "game setup/error fallback preserves explicit Custom style in the actual native publication path");
    }
    g_notificationsEnabled=notifications;
}

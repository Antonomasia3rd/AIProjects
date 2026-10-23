// Fresh factory text and offscreen bitmaps only. No native tile, package,
// account, provider, Startup or current-user profile operation is requested.
static std::vector<BYTE> PrimaryDesktopBitmapPixels(Gdiplus::Bitmap& bitmap)
{
    Gdiplus::Rect bounds(0,0,static_cast<INT>(bitmap.GetWidth()),static_cast<INT>(bitmap.GetHeight()));
    Gdiplus::BitmapData data{};
    if(bitmap.LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok)return {};
    std::vector<BYTE> result(static_cast<std::size_t>(bounds.Width)*bounds.Height*4);
    for(int row=0;row<bounds.Height;++row)memcpy(result.data()+static_cast<std::size_t>(row)*bounds.Width*4,
        static_cast<BYTE*>(data.Scan0)+static_cast<std::ptrdiff_t>(row)*data.Stride,static_cast<std::size_t>(bounds.Width)*4);
    bitmap.UnlockBits(&data);return result;
}
static void TestPrimaryDesktopFactoryDefaults()
{
    using namespace aip::content;
    const auto document=aip::ParseIniDocument(BuildInitialIniTemplate());
    const auto read=[&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);};
    TileInstanceConfiguration factory;std::wstring error;
    Check(ReadTileInstanceConfiguration(read,factory,error)&&factory.configured&&factory.tiles.size()==1,
        "fresh installs contain exactly one independent Desktop tile instead of eight unconfigured sources");
    if(factory.tiles.size()!=1)return;
    const auto& desktop=factory.tiles.front();
    Check(desktop.id==L"desktop"&&desktop.primary&&desktop.type==TileInstanceType::Wallpaper&&desktop.name==L"Desktop"&&
        desktop.policy.Mode()==L"LiveTile"&&desktop.appearance.branding==L"NameAndLogo"&&desktop.configuration.entries.front().background==L"Wallpaper",
        "fresh Desktop owns the primary wallpaper identity with explicit Live Tile delivery and name-plus-logo branding");
    Check(read(L"Settings",L"RunAtStartup",L"1")==L"0"&&read(L"Settings",L"RunAtStartupPackaged",L"1")==L"0",
        "fresh defaults do not enable either sign-in mechanism implicitly");
    Check(std::none_of(std::begin(g_defaults),std::end(g_defaults),[](const auto& setting){return IEquals(setting.section,L"Tiles")||IEquals(setting.section,L"Tile.desktop");}),
        "normal missing-default upgrades cannot seed the fresh source collection into an existing profile");
    TileInstance gallery;gallery.type=TileInstanceType::Gallery;
    Check(TileInstanceArtworkTopic(desktop)==L"desktop"&&TileInstanceArtworkTopic(gallery)==L"gallery"&&
        aip::tile_visual::FindTopic(L"desktop")->glyph==aip::tile_visual::Glyph::Desktop&&aip::tile_visual::FindTopic(L"gallery")->glyph==aip::tile_visual::Glyph::Photo,
        "wallpaper and gallery retain distinct Desktop and Photo fallback identities");
    Snapshot frame;frame.active=true;frame.artworkTopic=TileInstanceArtworkTopic(desktop);frame.policy=desktop.policy;
    frame.registrationAppearance=desktop.appearance;frame.secondaryBranding=desktop.appearance.branding;
    Check(UsesOriginalPrimaryDesktopArtwork(frame),"only the primary Desktop selects its original embedded fallback");
    auto secondary=frame;secondary.secondaryTileId=L"AipSource_another-desktop";
    Check(!UsesOriginalPrimaryDesktopArtwork(secondary),"another wallpaper tile uses its topic artwork without borrowing the primary identity");
    ScopedGdiPlus session;Check(session.ok(),"Desktop fallback fixture opens an offscreen GDI+ session");if(!session.ok())return;
    std::unique_ptr<Gdiplus::Bitmap> embedded(LoadEmbeddedDesktopIcon());Check(embedded!=nullptr,"original embedded Desktop icon remains available");if(!embedded)return;
    for(const auto size:{std::make_pair(150,150),std::make_pair(310,150),std::make_pair(310,310)}){
        std::unique_ptr<Gdiplus::Bitmap> expected(RenderDesktopIconAsset(embedded.get(),size.first,size.second,size.first,size.second));
        ContentRenderScope scope(&frame);std::unique_ptr<Gdiplus::Bitmap> actual(RenderDisabledTileAsset(size.first,size.second,size.first,size.second));
        const auto bytes=actual?PrimaryDesktopBitmapPixels(*actual):std::vector<BYTE>{};
        Check(expected&&actual&&!bytes.empty()&&bytes==PrimaryDesktopBitmapPixels(*expected),
            "primary fallback pixels and historical icon geometry match the original Desktop renderer on every supported size");
    }
    frame.policy.values[L"settings.generatedesktopiconfordisabledentries"]=L"0";
    {ContentRenderScope scope(&frame);std::unique_ptr<Gdiplus::Bitmap> hidden(RenderDisabledTileAsset(150,150,150,150));const auto bytes=hidden?PrimaryDesktopBitmapPixels(*hidden):std::vector<BYTE>{};
        Check(!bytes.empty()&&std::all_of(bytes.begin(),bytes.end(),[](BYTE value){return value==0;}),
            "disabling the fallback icon still produces transparent pixels for the typed primary tile");}
}

// Two independent Now Playing instances use one injected current-media view.
// No native SMTC stream, playback action, account or pin UI is exercised.
static bool AlbumArtOtherSettingsKept(const std::wstring& before,const std::wstring& after)
{
    const auto first=aip::ParseIniDocument(before),second=aip::ParseIniDocument(after);
    const auto unchanged=[](const auto& source,const auto& destination){for(const auto& section:source)for(const auto& entry:section.entries){
        const auto key=aip::content::Lower(entry.key);
        if(aip::content::Lower(section.name)==L"tile.cover"&&(key==L"content.background"||key==L"textmode"||key==L"branding"))continue;
        std::wstring value;if(!aip::ReadIniValueFromDoc(destination,section.name.c_str(),entry.key.c_str(),value)||value!=entry.value)return false;}return true;};
    return unchanged(first,second)&&unchanged(second,first);
}
static UINT AlbumArtPresetCommand(const std::wstring& id,const wchar_t* value)
{
    for(const auto& command:g_tileInstanceMenuCommands)if(command.second.id==id&&command.second.verb==TileInstanceMenuVerb::NowPlayingLayout&&command.second.value==value)return command.first;
    return 0;
}
static bool AlbumArtCommandChecked(HMENU menu,UINT id)
{
    for(int row=0;row<GetMenuItemCount(menu);++row){MENUITEMINFOW item{};item.cbSize=sizeof(item);item.fMask=MIIM_ID|MIIM_STATE|MIIM_SUBMENU;
        if(!GetMenuItemInfoW(menu,row,TRUE,&item))continue;if(item.wID==id&&!item.hSubMenu)return (item.fState&MFS_CHECKED)!=0;
        if(item.hSubMenu&&AlbumArtCommandChecked(item.hSubMenu,id))return true;}return false;
}
static void TestAlbumArtTileHost()
{
    using namespace aip::content;
    std::wstring original,error;Check(ReadIniFileForMutation(original),"album-art fixture captures only its private profile");
    const auto oldRefresh=g_iniCacheRefreshMs.load();const auto oldNotifications=g_notificationsEnabled.load();
    const auto oldStyle=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    const auto oldCommands=g_tileInstanceMenuCommands;const auto oldNext=g_nextTileInstanceMenuCommand;
    const auto oldPins=CachedSecondaryTileIds();const bool pinsKnown=SecondaryTileInventoryReady();const auto oldGallery=galleryHostFrames;
    const auto oldReader=g_contentSmtc.testReader;const auto oldUser=g_testTileRegistrationUserScope;
    Snapshot oldPrimary;{std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);oldPrimary=g_contentSnapshot;}
    const auto oldFrames=CurrentSecondarySnapshots();
    auto oldProviders=std::move(g_tileInstanceProviders);g_tileInstanceProviders.clear();
    g_contentSmtc.Cancel();g_iniCacheRefreshMs=0;g_notificationsEnabled=false;g_testTileRegistrationUserScope=L"9090909090909090";galleryHostFrames.clear();
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    const auto restore=[&]{StopTileInstanceProviders();g_tileInstanceProviders=std::move(oldProviders);g_contentSmtc.Cancel();g_contentSmtc.testReader=oldReader;
        g_secondaryPinInventory.Reset();if(pinsKnown)g_secondaryPinInventory.Complete(true,oldPins,GetTickCount64());else for(const auto& id:oldPins)g_secondaryPinInventory.Mutation(id,true);
        galleryHostFrames=oldGallery;g_testTileRegistrationUserScope=oldUser;g_tileInstanceMenuCommands=oldCommands;g_nextTileInstanceMenuCommand=oldNext;
        PublishContentSnapshot(oldPrimary);PublishSecondarySnapshots(oldFrames);Check(WriteContentFixture(original),"album-art fixture restores its exact private profile");
        g_iniCacheRefreshMs=oldRefresh;g_notificationsEnabled=oldNotifications;g_liveTileTemplateStyle=oldStyle;g_manifestCompatibilityTarget=oldTarget;};
    const auto profile=std::wstring(L"[Tiles]\r\nInstances=info,cover\r\n")+
        L"[Tile.info]\r\nType=NowPlaying\r\nName=Information fixture\r\nContent.Background=Transparent\r\nTextMode=Auto\r\nBranding=NameAndLogo\r\nClickAction=pause\r\nRefreshSeconds=31\r\nDelivery.Mode=LiveTile\r\n"+
        L"[Tile.cover]\r\nType=NowPlaying\r\nName=Cover fixture\r\nContent.Background=Transparent\r\nTextMode=Auto\r\nBranding=NameAndLogo\r\nClickAction=next\r\nRefreshSeconds=17\r\n"+
        L"TextPreset=Custom\r\nTextStyle=title\r\nTextSecondaryStyle=bodySubtle\r\nTextWrap=0\r\nTextMaxLines=1\r\nDelivery.Mode=LiveTile\r\nDelivery.Template=Adaptive\r\nCache.Enabled=0\r\n"+
        L"[Settings]\r\nNotifySourceErrors=0\r\n";
    Check(WriteContentFixture(profile),"album-art setup uses two independent private Now Playing instances");
    TileInstanceConfiguration config;if(!ReadCurrentTileInstances(config,error)||config.tiles.size()!=2){Check(false,"album-art fixture reads two typed instances");restore();return;}
    ResetTileInstanceMenuCommands();HMENU menu=CreatePopupMenu();AppendTileInstanceDetails(menu,config.tiles[1]);
    const auto artCommand=AlbumArtPresetCommand(L"cover",L"AlbumArt"),initialInfo=AlbumArtPresetCommand(L"cover",L"Info");
    Check(artCommand&&initialInfo&&AlbumArtCommandChecked(menu,initialInfo)&&!AlbumArtCommandChecked(menu,artCommand),
        "Now Playing source menu exposes both presentation presets and derives Information from existing source text and branding settings");DestroyMenu(menu);
    std::wstring before,after;ReadIniFileForMutation(before);
    Check(artCommand&&DispatchTileInstanceCommand(nullptr,artCommand)&&ReadIniFileForMutation(after)&&ReadCurrentTileInstances(config,error)&&config.tiles.size()==2&&
        config.tiles[1].configuration.entries.front().background==L"SMTCArtwork"&&config.tiles[1].configuration.textMode==TextMode::Off&&config.tiles[1].appearance.branding==L"Name"&&
        AlbumArtOtherSettingsKept(before,after),
        "actual AlbumArt preset atomically changes its three presentation fields while preserving the other tile name click interval typography and delivery");
    if(config.tiles.size()!=2){restore();return;}
    ResetTileInstanceMenuCommands();menu=CreatePopupMenu();AppendTileInstanceDetails(menu,config.tiles[1]);
    const auto activeArt=AlbumArtPresetCommand(L"cover",L"AlbumArt"),infoCommand=AlbumArtPresetCommand(L"cover",L"Info");
    Check(activeArt&&AlbumArtCommandChecked(menu,activeArt)&&!AlbumArtCommandChecked(menu,infoCommand),
        "album-art preset state is derived after save without introducing another stored tile type or presentation mode");DestroyMenu(menu);
    const auto infoId=TileInstanceNativeId(config.tiles[0]),artId=TileInstanceNativeId(config.tiles[1]);
    g_secondaryPinInventory.Complete(true,{infoId,artId},GetTickCount64());
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfSize=58;file.bfOffBits=54;BITMAPINFOHEADER image{};
    image.biSize=sizeof(image);image.biWidth=1;image.biHeight=1;image.biPlanes=1;image.biBitCount=24;image.biSizeImage=4;
    std::vector<BYTE> bytes(58,64);memcpy(bytes.data(),&file,sizeof(file));memcpy(bytes.data()+14,&image,sizeof(image));
    unsigned reads=0;std::vector<bool> requestedArt;bool haveArt=true;
    g_contentSmtc.testReader=[&](auto,bool artwork,const auto&){++reads;requestedArt.push_back(artwork);SmtcSessionData data;
        data.hasSession=true;data.title=haveArt?L"Synthetic first song":L"Synthetic current song";data.artist=L"Synthetic artist";
        data.appId=L"Synthetic.Player";data.playbackState=MediaPlaybackState::Playing;data.status=L"Playing";
        if(artwork&&haveArt)data.artworkBytes=bytes;return data;};
    const auto wallpaperRequests=contentWallpaperBackgroundRequests;
    Check(TileInstanceEngineTick(true,false)&&reads==1&&requestedArt.size()==1&&requestedArt.front(),
        "one injected SMTC read serves Information and album-art tiles and still requests artwork when the art tile's text is Off");
    auto firstFrames=CurrentSecondarySnapshots();const auto find=[&](const auto& frames,const std::wstring& id)->Snapshot{
        const auto found=std::find_if(frames.begin(),frames.end(),[&](const auto& frame){return frame.secondaryTileId==id;});return found==frames.end()?Snapshot{}:*found;};
    auto infoFrame=find(firstFrames,infoId),artFrame=find(firstFrames,artId);firstFrames.clear();
    Check(infoFrame.active&&artFrame.active&&infoFrame.smtcSource&&artFrame.smtcSource&&infoFrame.textMode==TextMode::Auto&&artFrame.textMode==TextMode::Off&&
        infoFrame.text.primary==L"Synthetic first song"&&artFrame.backgroundLease&&!infoFrame.backgroundLease&&
        artFrame.backgroundId==BackgroundId::SmtcArtwork&&artFrame.secondaryBranding==L"Name"&&infoFrame.backgroundPath!=artFrame.backgroundPath,
        "two distinct publication destinations keep the current information frame separate from a leased pure-art frame");
    Check(infoFrame.activationArguments.find(L"--tile-setup info")!=std::wstring::npos&&artFrame.activationArguments.find(L"--tile-setup cover")!=std::wstring::npos&&
        config.tiles[0].clickAction==L"pause"&&config.tiles[1].clickAction==L"next"&&contentWallpaperBackgroundRequests==wallpaperRequests,
        "album-art composition preserves independent profile-routed click actions and never observes the desktop wallpaper");
    auto renamedTrack=artFrame;renamedTrack.text={L"Invisible changed title",L"Invisible changed artist",L"Paused"};renamedTrack.smtcState=MediaPlaybackState::Paused;
    Check(ContentSnapshotKey(renamedTrack)==ContentSnapshotKey(artFrame),"unchanged art-only imagery does not regenerate solely because invisible title artist or playback text changed");renamedTrack={};
    const auto directory=aip::GetDirectoryName(g_iniPath)+L"\\AlbumArtLayout";const bool created=CreateDirectoryW(directory.c_str(),nullptr)!=FALSE;
    Check(created,"album-art renderer owns only a private output directory");
    ScopedGdiPlus gdi;Check(gdi.ok(),"album-art checks acquire only an offscreen image scope");
    if(created&&gdi.ok()){
        Gdiplus::Bitmap canvas(150,150,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&canvas);graphics.Clear(Gdiplus::Color(255,40,80,120));}
        const auto path=directory+L"\\cover.png";bool pure=false;
        {ContentRenderScope scope(&artFrame);pure=ApplyTileTextOverlay(&canvas,L"LiveMediumTile",150,150);}
        for(int y=0;y<150&&pure;y+=3)for(int x=0;x<150;x+=3){Gdiplus::Color pixel;if(canvas.GetPixel(x,y,&pixel)!=Gdiplus::Ok||pixel.GetValue()!=Gdiplus::Color(255,40,80,120).GetValue()){pure=false;break;}}
        Check(pure&&SavePNG(&canvas,path.c_str()),"art-only TextMode Off preserves the complete image without title status glyph or baked corner overlays");
        ManifestDisplayInfo manifest;std::vector<LiveTileUpdateAsset> assets{{L"LiveMediumTile",L"cover.png",L"TileMedium",L"cover.png",150,150}};
        std::wstring xml;{ContentRenderScope scope(&artFrame);xml=BuildLiveTileXml(directory,manifest,assets);}
        Check(!xml.empty()&&xml.find(L"<image")!=std::wstring::npos&&xml.find(L"cover.png")!=std::wstring::npos&&
            xml.find(L"<text")==std::wstring::npos&&xml.find(L"branding=\"name\"")!=std::wstring::npos,
            "actual native album-art publication is an image binding with the requested shared name branding and no media-text binding");
        DeleteFileW(path.c_str());
    }
    if(created)Check(RemoveDirectoryW(directory.c_str())!=FALSE,"album-art checks remove only their private rendered-image directory");
    const auto oldArtworkPath=artFrame.backgroundPath;haveArt=false;
    Check(TileInstanceEngineTick(true,false)&&reads==2,"a second injected media result updates both independent tiles without opening another native media stream");
    const auto currentFrames=CurrentSecondarySnapshots();const auto currentInfo=find(currentFrames,infoId),currentArt=find(currentFrames,artId);
    Check(currentArt.active&&!currentArt.backgroundLease&&currentArt.backgroundPath!=oldArtworkPath&&IsUsableWallpaperPath(currentArt.backgroundPath)&&
        currentArt.textMode==TextMode::Off&&currentInfo.text.primary==L"Synthetic current song",
        "missing album art clears the previous cover and lease to a usable blank fallback while the information tile advances to the current song");
    Check(!oldArtworkPath.empty()&&GetFileAttributesW(oldArtworkPath.c_str())!=INVALID_FILE_ATTRIBUTES,
        "a frozen old album-art frame keeps its source image alive until its generation lease ends");
    artFrame={};Check(!oldArtworkPath.empty()&&GetFileAttributesW(oldArtworkPath.c_str())==INVALID_FILE_ATTRIBUTES,
        "releasing the final old album-art frame deletes its superseded cached cover");
    g_secondaryPinInventory.Complete(true,{infoId},GetTickCount64());
    Check(TileInstanceEngineTick(true,false)&&reads==3&&requestedArt.size()==3&&!requestedArt.back(),
        "unpinning only the art tile removes artwork demand while the remaining Information tile still refreshes its shared text source");
    Check(infoCommand&&DispatchTileInstanceCommand(nullptr,infoCommand)&&ReadIniFileForMutation(after)&&ReadCurrentTileInstances(config,error)&&config.tiles.size()==2&&
        config.tiles[1].configuration.entries.front().background==L"Transparent"&&config.tiles[1].configuration.textMode==TextMode::Auto&&
        config.tiles[1].appearance.branding==L"NameAndLogo"&&AlbumArtOtherSettingsKept(before,after),
        "Information preset reverses the three presentation fields without replacing the independent tile's other options");
    ResetTileInstanceMenuCommands();if(config.tiles.size()==2){menu=CreatePopupMenu();AppendTileInstanceDetails(menu,config.tiles[1]);
        const auto stale=AlbumArtPresetCommand(L"cover",L"AlbumArt");DestroyMenu(menu);
        std::wstring changedType;ReadIniFileForMutation(changedType);aip::WriteIniValueToText(changedType,L"Tile.cover",L"Type",L"Calendar");
        Check(WriteContentFixture(changedType),"stale album-art menu guard changes only its private fixture type");
        if(stale)DispatchTileInstanceCommand(nullptr,stale);std::wstring unchanged;ReadIniFileForMutation(unchanged);
        Check(stale&&unchanged==changedType,"a stale Now Playing preset cannot rewrite a tile that has since changed to another source type");}
    infoFrame={};restore();
}

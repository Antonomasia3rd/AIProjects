// Exact host configuration, hidden native dialogs and synthetic account I/O.
// No real credential, account, Gateway, message, package or notification is used.
static std::wstring discordFixtureConfirmationToken;
static void TestDiscordReaderHost(const std::wstring& bitmapPath){
    namespace d=aip::discord_tiles;namespace c=aip::content;
    std::wstring original,error;Check(ReadIniFileForMutation(original),"Discord fixture captures its private test INI");
    const auto savedDialog=contentDialogResponse;const auto oldRefresh=g_iniCacheRefreshMs.load();g_iniCacheRefreshMs=0;
    Check(WriteContentFixture(L"[Tiles]\r\nSchemaVersion=1\r\nInstances=desktop\r\n[Tile.desktop]\r\nType=Wallpaper\r\nPrimary=1\r\n"),"prepare primary removal guard fixture");
    std::wstring primaryBefore,primaryAfter;ReadIniFileForMutation(primaryBefore);
    Check(!SaveTileInstanceSettingsWithRemovalConfirmation(nullptr,{{L"Tiles",L"Instances",L""}},L"desktop",L"desktop",true)&&ReadIniFileForMutation(primaryAfter)&&primaryBefore==primaryAfter,"authoritative removal refuses the main tile even after stale confirmation");
    const auto profile=L"[Tiles]\r\nSchemaVersion=1\r\nInstances=discord\r\n[Tile.discord]\r\nType=Discord\r\nName=Discord test\r\nDiscordReader.View=Profile\r\nDiscordReader.Connection=UserTokenExperimental\r\nDiscordReader.UseGateway=0\r\n";
    Check(WriteContentFixture(profile),"create one independent reader fixture without legacy presence settings");
    c::TileInstance tile;Check(ReadDiscordReaderTile(L"discord",tile,error)&&!tile.discordReader.enabled&&!tile.discordReader.rememberCredential,"fresh typed reader is explicitly disabled with session-only credential default");
    auto setting=std::wstring(L"true");Check(NormalizeDesktopStubTypedSetting(L"Tile.discord",L"DiscordReader.RememberCredential",setting,error)&&setting==L"1","typed CLI uses the same remember-credential validation");
    setting=L"private-unprotected";Check(!NormalizeDesktopStubTypedSetting(L"Tile.discord",L"DiscordReader.CredentialProtected",setting,error)&&error.find(setting)==std::wstring::npos,"CLI rejects plaintext credential without echoing it");
    std::atomic<unsigned> probes{0},tokens{0};g_testDiscordProtect=[](const auto&){return std::wstring(L"dpapi:synthetic-envelope");};
    g_testDiscordCredentialDialog=[&](HWND,const auto&,auto& text,bool& remember){++tokens;text=L"inert-token-value";remember=false;return IDOK;};
    g_testDiscordIdentityProbe=[&](const auto& request){++probes;Check(request.enabled&&request.experimentalWarningReviewed&&request.profileKey==request.warningProfileKey,"identity probe has explicit warning and matching profile context");Check(request.profileOverride&&request.profilePath.empty(),"session-only identity probe receives protected in-memory profile");aip::discord::ReaderIdentityProbeResult result;result.ok=true;result.user.id=L"42";result.user.username=L"synthetic-user";result.user.displayName=L"Synthetic user";return result;};
    unsigned notices=0;g_testDiscordReaderNotice=[&](const auto&,bool){++notices;};
    Check(!ActivateDiscordReader(nullptr,L"discord",true)&&notices==1,"unconfigured compose uses the nonmodal notification path");
    g_notificationsEnabled=true;g_testNotificationConfirmationToast=[](const auto&,const auto&,const auto&,const auto& token){discordFixtureConfirmationToken=token;return true;};
    Check(ConfigureDiscordReader(nullptr,L"discord",true)&&probes==0&&tokens==0,"queued policy notification cannot request credentials before approval");
    QueueUserConfirmationAction(L"confirm="+discordFixtureConfirmationToken+L"&choice=no");DrainUserConfirmations();
    Check(probes==0&&tokens==0,"declining policy notification cannot authenticate");
    Check(ConfigureDiscordReader(nullptr,L"discord",true),"a new connection can queue a new policy review");
    QueueUserConfirmationAction(L"confirm="+discordFixtureConfirmationToken+L"&choice=yes");DrainUserConfirmations();
    Check(probes==1&&tokens==1&&ReadDiscordReaderTile(L"discord",tile,error)&&!tile.discordReader.enabled,"policy Yes only probes; verified-account notification must still be approved");
    Check(SaveConfirmedTileInstanceSettings(nullptr,{{L"Tile.discord",L"DiscordReader.ChannelId",L"501"}},L"discord",{}),"change account context while account notification is pending");
    QueueUserConfirmationAction(L"confirm="+discordFixtureConfirmationToken+L"&choice=yes");DrainUserConfirmations();
    Check(ReadDiscordReaderTile(L"discord",tile,error)&&tile.discordReader.accountId.empty()&&!DiscordReaderSessionProfile(tile),"late account Yes cannot save stale credentials or enable the changed tile");
    ClearUserConfirmations();g_testNotificationConfirmationToast=nullptr;g_notificationsEnabled=false;probes=0;tokens=0;
    Check(WriteContentFixture(profile),"restore initial reader after asynchronous confirmation tests");
    contentDialogResponse=IDNO;Check(!ConfigureDiscordReader(nullptr,L"discord",true)&&probes==0&&tokens==0,"declining the prominent personal-account warning cannot read or request any credential");
    contentDialogResponse=IDYES;Check(ConfigureDiscordReader(nullptr,L"discord",true)&&probes==1&&tokens==1,"explicit reviewed account setup accepts an injected session-only credential");
    Check(ReadDiscordReaderTile(L"discord",tile,error)&&tile.discordReader.enabled&&tile.discordReader.accountId==L"42"&&!tile.discordReader.rememberCredential,"verified identity and reader enablement are saved only after explicit account review");
    std::wstring after;Check(ReadIniFileForMutation(after)&&after.find(L"inert-token-value")==std::wstring::npos&&after.find(L"dpapi:synthetic-envelope")==std::wstring::npos,"session-only setup writes neither plaintext nor protected credential to disk");
    auto protectedProfile=DiscordReaderInlineProfile(tile,error);Check(protectedProfile&&DiscordReaderPersonalAcknowledged(tile),"saved session-only configuration resolves the exact reviewed account and credential revision");
    const auto reviewedKey=DiscordReaderRequestKey(tile);auto changed=tile;changed.discordReader.accountId=L"43";
    Check(!DiscordReaderSessionProfile(changed)&&!DiscordReaderPersonalAcknowledged(changed)&&DiscordReaderRequestKey(changed)!=reviewedKey,"another account cannot reuse session credentials, warning acknowledgement or reviewed-send context");
    Check(DiscordReaderSessionProfile(tile)!=nullptr,"checking a different candidate cannot delete the original active session credential");
    changed=tile;changed.discordReader.channelId=L"500";Check(DiscordReaderRequestKey(changed)!=reviewedKey,"a destination change creates a different exact reviewed-send context");
    ContentDiscordAccountProvider provider;std::atomic<unsigned> requests{0},posts{0};
    provider.configureTestOptions=[&](auto& options){options.useGateway=false;options.credentialDecoder=[](const auto&){return std::wstring(L"synthetic-only");};options.http=[&](const auto& request,auto& response,std::wstring&){++requests;response.statusCode=200;if(request.method!=L"GET"){++posts;response.body=R"({"id":"900","channel_id":"500","author":{"id":"42"},"content":"e"})";}
        else if(request.url.find(L"/channels/")!=std::wstring::npos)response.body=R"([{"id":"800","channel_id":"500","author":{"id":"43","username":"friend"},"content":"e","timestamp":"2026-10-28T12:00:00Z"}])";
        else response.body=R"({"id":"42","username":"synthetic-user","global_name":"Synthetic user","avatar":null})";return true;};};
    provider.Refresh(tile,true);const auto snapshot=provider.Read();
    Check(provider.started&&snapshot.data.hasSuccess&&snapshot.data.user.id==L"42"&&requests>0&&posts==0,"actual host provider reads its verified account through an injected transport without background send or read acknowledgement");
    Check(DiscordReaderForTile(tile)==provider.reader,"compose lookup is bound to the actual active reader's exact current account/configuration");
    c::Snapshot frame;frame.name=L"Discord test";c::ProjectDiscordAccount(frame,snapshot.data,tile.discordReader,ContentDiscordAccountProvider::Now());
    frame.textMode=c::TextMode::Auto;frame.discordAvatarPath=bitmapPath;frame.discordAvatarLease=std::make_shared<int>(1);
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR gdip=0;const bool started=Gdiplus::GdiplusStartup(&gdip,&startup,nullptr)==Gdiplus::Ok;Check(started,"Discord native renderer fixture starts GDI+ only");
    if(started){for(const auto& size:std::vector<std::pair<int,int>>{{150,150},{310,150},{310,310}}){Gdiplus::Bitmap image(size.first,size.second,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(0,0,0,0));Check(RenderDiscordAccountSimulation(graphics,frame,size.first,size.second,1.0),"actual GDI renderer draws prepared profile and right avatar without source I/O");}
        auto custom=frame;custom.active=true;custom.secondaryBranding=L"None";custom.presentation.preset=L"Custom";
        custom.presentation.primaryStyle=custom.presentation.secondaryStyle=custom.presentation.badgeStyle=L"caption";
        custom.presentation.primaryLines=custom.presentation.secondaryLines=custom.presentation.badgeLines=1;
        custom.presentation.gapLines=0;custom.presentation.stacking=L"top";custom.discordAccountRows={{L"Reader resource row A",L"",L""},{L"Reader resource row B",L"",L""}};
        {ContentRenderScope scope(&custom);Gdiplus::Bitmap actual(150,150,PixelFormat32bppARGB),expected(150,150,PixelFormat32bppARGB);
            {Gdiplus::Graphics graphics(&actual);graphics.Clear(Gdiplus::Color(0,0,0,0));}
            bool expectedDrawn=false;{Gdiplus::Graphics graphics(&expected);graphics.Clear(Gdiplus::Color(0,0,0,0));expectedDrawn=ConfigureTileTextGraphics(graphics)&&RenderDiscordAccountSimulation(graphics,custom,150,150,1.0);}
            Check(expectedDrawn&&ApplyTileTextOverlay(&actual,L"MediumTile",150,150),"Custom Discord profile uses actual production text-overlay entry point");
            bool equal=true,rowInk=false;int mismatchX=-1,mismatchY=-1;Gdiplus::ARGB actualMismatch=0,expectedMismatch=0;for(int y=0;y<150;++y)for(int x=0;x<150;++x){Gdiplus::Color a,b;
                if(actual.GetPixel(x,y,&a)!=Gdiplus::Ok||expected.GetPixel(x,y,&b)!=Gdiplus::Ok||a.GetValue()!=b.GetValue()){if(equal){mismatchX=x;mismatchY=y;actualMismatch=a.GetValue();expectedMismatch=b.GetValue();}equal=false;}
                if(y>=70&&y<110&&x>=10&&x<130&&a.GetA())rowInk=true;}
            Gdiplus::Color avatarPixel;const auto geometry=c::ComputeDiscordAccountGeometry(150,150,true,custom.discordAvatarScalePercent);
            const bool avatarInk=actual.GetPixel(static_cast<int>(geometry.avatarX+geometry.avatarSide/2),static_cast<int>(10+geometry.avatarSide/2),&avatarPixel)==Gdiplus::Ok&&avatarPixel.GetA()>200&&avatarPixel.GetR()==64&&avatarPixel.GetG()==64&&avatarPixel.GetB()==64;
            if(!equal||!rowInk||!avatarInk)std::cerr<<"Custom Discord witness: equal="<<equal<<" rows="<<rowInk<<" avatar="<<avatarInk<<" avatarARGB="<<avatarPixel.GetValue()<<" firstMismatch="<<mismatchX<<','<<mismatchY<<" actualARGB="<<actualMismatch<<" expectedARGB="<<expectedMismatch<<'\n';
            Check(equal,"Custom profile production overlay exactly matches dedicated-renderer pixels");
            Check(rowInk,"Custom profile retains its resource-row ink below the header and avatar");
            Check(avatarInk,"Custom profile retains the right avatar's synthetic gray pixel");
        }
        Gdiplus::GdiplusShutdown(gdip);}
    Check(posts==0,"drawing the prepared reader snapshot never emits a message or read acknowledgement");
    const auto section=c::TileInstanceSection(tile.id);
    Check(SaveConfirmedTileInstanceSettings(nullptr,{{section,L"DiscordReader.View",L"Channel"},{section,L"DiscordReader.ChannelId",L"500"},{section,L"DiscordReader.AllowReviewedSend",L"1"}},tile.id,{}),"explicitly opt the isolated fixture into reviewed channel sending");
    Check(ReadDiscordReaderTile(tile.id,tile,error),"read current channel send fixture");provider.Refresh(tile,true);
    g_testDiscordComposeDialog=[](HWND,const auto&,const auto&,auto&){return IDCANCEL;};
    Check(!ActivateDiscordReader(nullptr,tile.id,true)&&posts==0,"canceling the actual host compose action cannot send");
    g_testDiscordComposeDialog=[](HWND,const auto&,const auto&,auto& message){message=L"e";return IDOK;};contentDialogResponse=IDNO;
    Check(!ActivateDiscordReader(nullptr,tile.id,true)&&posts==0,"canceling the actual host message review cannot send");
    contentDialogResponse=IDYES;g_testDiscordComposeDialog=[&](HWND,const auto&,const auto&,auto& message){message=L"e";SaveConfirmedTileInstanceSettings(nullptr,{{section,L"DiscordReader.ChannelId",L"501"}},tile.id,{});return IDOK;};
    Check(!ActivateDiscordReader(nullptr,tile.id,true)&&posts==0,"changing the configured destination while composing invalidates the reviewed send before HTTP");
    Check(SaveConfirmedTileInstanceSettings(nullptr,{{section,L"DiscordReader.ChannelId",L"500"}},tile.id,{}),"restore the isolated intended send destination");
    g_testDiscordComposeDialog=[](HWND,const auto&,const auto&,auto& message){message=L"e";return IDOK;};
    Check(ActivateDiscordReader(nullptr,tile.id,true)&&posts==1,"only an explicit compose and send review reaches the injected transport once");
    provider.Stop();Check(!DiscordReaderForTile(tile)&&!DiscordReaderSessionProfile(tile)&&!DiscordReaderPersonalAcknowledged(tile),"stopping the reader clears active registration and its session-only protected credential");
    // Native dialog geometry is measured with actual controls at equivalent font
    // scales; this does not claim a real multi-monitor DPI transition test.
    for(int scale:{100,150,200}){std::wstring token;bool remember=false;const auto prompt=std::wstring(L"Personal account token (private; do not share it)\nUnchecked: use only until this tile/profile stops; no credential is written to the INI.");const auto bytes=DiscordReaderCredentialTemplate(prompt,scale);DiscordReaderCredentialDialogState state{{&token,false,true,false},&remember};
        HWND dialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(bytes.data()),nullptr,DiscordReaderCredentialDialogProc,reinterpret_cast<LPARAM>(&state));Check(dialog!=nullptr,"native credential dialog can be created hidden at each font/DPI-equivalent scale");
        if(dialog){RECT promptRect{},edit{},checkbox{},client{};GetWindowRect(GetDlgItem(dialog,TEXT_INPUT_PROMPT_ID),&promptRect);GetWindowRect(GetDlgItem(dialog,TEXT_INPUT_EDIT_ID),&edit);GetWindowRect(GetDlgItem(dialog,DISCORD_READER_REMEMBER_ID),&checkbox);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&promptRect),2);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&edit),2);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&checkbox),2);GetClientRect(dialog,&client);
            Check(promptRect.bottom<=edit.top&&edit.bottom<checkbox.top&&checkbox.bottom<=client.bottom,"measured long prompt, secret edit and Remember checkbox do not overlap or clip");
            Check(SendDlgItemMessageW(dialog,TEXT_INPUT_EDIT_ID,EM_GETPASSWORDCHAR,0,0)==L'\x25cf'&&IsDlgButtonChecked(dialog,DISCORD_READER_REMEMBER_ID)==BST_UNCHECKED,"actual native credential control masks input and defaults Remember off");DestroyWindow(dialog);}
    }
    g_testDiscordIdentityProbe={};g_testDiscordProtect={};g_testDiscordCredentialDialog={};g_testDiscordComposeDialog={};g_testDiscordReaderNotice={};contentDialogResponse=savedDialog;StopTileInstanceProviders();
    Check(WriteContentFixture(original),"Discord fixture restores its private INI");g_iniCacheRefreshMs=oldRefresh;
}

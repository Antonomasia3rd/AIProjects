// Explicit account setup and reviewed sends. No render/refresh path calls these
// actions. The source/credential context is captured and revalidated on save.
#ifdef DESKTOPSTUB_INERT_HARDWARE
static std::function<aip::discord::ReaderIdentityProbeResult(const aip::discord::ReaderIdentityProbeOptions&)> g_testDiscordIdentityProbe;
static std::function<std::wstring(const std::wstring&)> g_testDiscordProtect;
static std::function<INT_PTR(HWND,const std::wstring&,std::wstring&,bool&)> g_testDiscordCredentialDialog;
static std::function<INT_PTR(HWND,const std::wstring&,const std::wstring&,std::wstring&)> g_testDiscordComposeDialog;
#endif
static constexpr int DISCORD_READER_REMEMBER_ID=1079;
struct DiscordReaderCredentialDialogState {TextInputDialogState input;bool* remember=nullptr;};
static std::vector<BYTE> DiscordReaderCredentialTemplate(const std::wstring& prompt,int scalePercent=100){
    std::vector<BYTE> bytes(4096+(prompt.size()+g_ui.okText.size()+g_ui.cancelText.size()+256)*sizeof(wchar_t));auto* dialog=reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    dialog->style=WS_POPUP|WS_SYSMENU|DS_MODALFRAME|WS_CAPTION|DS_SETFONT;dialog->cdit=5;dialog->cx=290;dialog->cy=102;
    auto* p=reinterpret_cast<WORD*>(dialog+1);*p++=0;*p++=0;AppendDialogTemplateString(p,L"Discord credential");*p++=static_cast<WORD>((std::clamp)(MulDiv(8,scalePercent,100),8,32));AppendDialogTemplateString(p,L"MS Shell Dlg");
    const auto add=[&](short x,short y,short width,short height,WORD id,DWORD style,WORD kind,const std::wstring& text){auto* item=AlignDialogItem(p);item->x=x;item->y=y;item->cx=width;item->cy=height;item->id=id;item->style=WS_CHILD|WS_VISIBLE|style;p=reinterpret_cast<WORD*>(item+1);*p++=0xffff;*p++=kind;AppendDialogTemplateString(p,text);*p++=0;};
    add(7,58,276,14,TEXT_INPUT_EDIT_ID,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,0x81,L"");
    add(7,79,276,14,DISCORD_READER_REMEMBER_ID,WS_TABSTOP|BS_AUTOCHECKBOX,0x80,L"Remember token protected in this profile");
    add(233,6,50,14,IDOK,WS_TABSTOP|BS_DEFPUSHBUTTON,0x80,g_ui.okText);add(233,24,50,14,IDCANCEL,WS_TABSTOP|BS_PUSHBUTTON,0x80,g_ui.cancelText);
    add(7,6,214,48,TEXT_INPUT_PROMPT_ID,WS_GROUP|SS_LEFT|SS_NOPREFIX,0x82,prompt);return bytes;
}
static void LayoutDiscordReaderCredentialDialog(HWND window){
    RECT before{},after{},check{};const auto edit=GetDlgItem(window,TEXT_INPUT_EDIT_ID),checkbox=GetDlgItem(window,DISCORD_READER_REMEMBER_ID);
    GetWindowRect(edit,&before);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&before),2);GetWindowRect(checkbox,&check);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&check),2);
    LayoutTextInputDialog(window);GetWindowRect(edit,&after);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&after),2);
    // The shared prompt measurement moves the edit; this dialog also owns a
    // checkbox below it, so move both by exactly the same measured amount.
    MoveWindow(checkbox,check.left,check.top+(after.top-before.top),check.right-check.left,check.bottom-check.top,TRUE);
}
static INT_PTR CALLBACK DiscordReaderCredentialDialogProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) noexcept{
    try{auto* state=reinterpret_cast<DiscordReaderCredentialDialogState*>(GetWindowLongPtrW(window,DWLP_USER));
        if(message==WM_INITDIALOG){state=reinterpret_cast<DiscordReaderCredentialDialogState*>(lParam);if(!state||!state->remember||!state->input.value)return FALSE;SetWindowLongPtrW(window,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
            SetDlgItemTextW(window,TEXT_INPUT_EDIT_ID,state->input.value->c_str());CheckDlgButton(window,DISCORD_READER_REMEMBER_ID,*state->remember?BST_CHECKED:BST_UNCHECKED);
            SendDlgItemMessageW(window,TEXT_INPUT_EDIT_ID,EM_SETPASSWORDCHAR,L'\x25cf',0);LayoutDiscordReaderCredentialDialog(window);if(state->input.interactive)SetFocus(GetDlgItem(window,TEXT_INPUT_EDIT_ID));return FALSE;}
        if(!state)return FALSE;if(message==WM_COMMAND){if(LOWORD(wParam)==IDOK){if(CommitTextInputDialog(window,state->input)){*state->remember=IsDlgButtonChecked(window,DISCORD_READER_REMEMBER_ID)==BST_CHECKED;EndDialog(window,IDOK);}return TRUE;}if(LOWORD(wParam)==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}}
        if(message==WM_CLOSE){EndDialog(window,IDCANCEL);return TRUE;}if(message==WM_NCDESTROY){SetDlgItemTextW(window,TEXT_INPUT_EDIT_ID,L"");SetWindowLongPtrW(window,DWLP_USER,0);}return FALSE;
    }catch(...){EndDialog(window,IDCANCEL);return TRUE;}
}
static INT_PTR ShowDiscordReaderCredentialDialog(HWND owner,const std::wstring& prompt,std::wstring& token,bool& remember){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testDiscordCredentialDialog)return g_testDiscordCredentialDialog(owner,prompt,token,remember);return IDCANCEL;
#else
    const auto bytes=DiscordReaderCredentialTemplate(prompt+L"\nUnchecked: use only until this tile/profile stops; no credential is written to the INI.");DiscordReaderCredentialDialogState state{{&token,false,true,true},&remember};
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(bytes.data()),owner,DiscordReaderCredentialDialogProc,reinterpret_cast<LPARAM>(&state));
#endif
}
static bool ReadDiscordReaderTile(const std::wstring& id,aip::content::TileInstance& output,std::wstring& error){
    aip::content::TileInstanceConfiguration cfg;if(!ReadCurrentTileInstances(cfg,error))return false;
    const auto item=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& value){return value.id==id&&value.type==aip::content::TileInstanceType::Discord;});
    if(item==cfg.tiles.end()){error=L"This Discord tile is no longer configured.";return false;}output=*item;return true;
}
static bool SaveDiscordReaderChanges(HWND owner,const aip::content::TileInstance& before,const std::vector<aip::IniSetting>& changes){
    const auto expected=DiscordReaderRequestKey(before),profile=DiscordReaderProfileKey(before.id);
    return SaveConfirmedTileInstanceSettings(owner,changes,before.id,[expected,profile,id=before.id](const std::wstring& text,std::wstring& error){
        if(DiscordReaderProfileKey(id)!=profile){error=L"The active profile changed. Reopen this tile's account settings.";return false;}
        const auto document=aip::ParseIniDocument(text);aip::content::TileInstanceConfiguration current;
        if(!aip::content::ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);},current,error))return false;
        const auto selected=std::find_if(current.tiles.begin(),current.tiles.end(),[&](const auto& tile){return tile.id==id;});
        if(selected==current.tiles.end()||selected->type!=aip::content::TileInstanceType::Discord||DiscordReaderRequestKey(*selected)!=expected){error=L"This tile's account, channel or credential changed while the dialog was open. Review its current settings again.";return false;}return true;
    });
}
static void DiscordReaderNotice(HWND owner,const std::wstring& text,bool failed=true){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,text.c_str(),L"Discord account reader",MB_OK|(failed?MB_ICONWARNING:MB_ICONINFORMATION));}
static aip::discord::ReaderIdentityProbeResult RunDiscordReaderIdentityProbe(aip::discord::ReaderIdentityProbeOptions options){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testDiscordIdentityProbe)return g_testDiscordIdentityProbe(options);
    aip::discord::ReaderIdentityProbeResult result;result.status=L"Discord account lookup is inert in validation.";return result;
#else
    std::atomic<bool> done{false},canceled{false};aip::discord::ReaderIdentityProbeResult result;
    options.canceled=[&]{return canceled.load()||g_shutdownState.IsRequested();};
    std::thread worker([&]{try{result=aip::discord::ProbeReaderIdentity(options);}catch(...){result.status=L"The Discord account lookup could not complete.";}done=true;});
    bool quit=false;while(!done){MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){quit=true;canceled=true;continue;}TranslateMessage(&message);DispatchMessageW(&message);}if(g_shutdownState.IsRequested())canceled=true;}
    worker.join();if(quit)PostQuitMessage(0);return result;
#endif
}
static bool ConfigureDiscordReader(HWND owner,const std::wstring& id,bool replaceCredential){
    using namespace aip::discord_tiles;aip::content::TileInstance tile;std::wstring error;
    if(!ReadDiscordReaderTile(id,tile,error)){DiscordReaderNotice(owner,error);return false;}
    const bool presence=tile.discordReader.view==L"Presence";auto connection=presence?L"UserTokenExperimental":tile.discordReader.connection;
    if(connection!=L"UserTokenExperimental"&&connection!=L"OAuth"&&connection!=L"Bot"){DiscordReaderNotice(owner,L"Choose OAuth, Bot or the explicitly experimental personal-token reader connection first. Rich presence IPC itself requires no account-reader login.");return false;}
    if(presence&&(replaceCredential||DiscordReaderProfilePath(tile).empty())){DiscordReaderNotice(owner,L"Rich presence keeps its existing account/profile configuration. Choose or create an advanced Discord profile, then use Identify and review to acknowledge its experimental Gateway path. Reader credentials are separate from the presence writer.");return false;}
    const bool personal=connection==L"UserTokenExperimental";
    if(personal){const auto warning=std::wstring(ExperimentalWarning)+L"\n\nProfile: "+DiscordReaderProfileKey(id)+L"\nTile: "+tile.name+
        L"\n\nThis token can expose private account data and act as this account when you explicitly send. DesktopStub cannot promise that using an alt or a modified Discord client avoids enforcement. No message or read acknowledgement is sent by account lookup.\n\nReview this experimental account connection?";
        if(DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,warning.c_str(),L"Experimental Discord personal account",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return false;}
    auto prospective=tile;prospective.discordReader.connection=connection;std::vector<aip::IniSetting> changes;
    const auto section=aip::content::TileInstanceSection(id);
    std::wstring enteredRevision;std::shared_ptr<const aip::discord::Profile> enteredProfile;
    if(replaceCredential){std::wstring token;bool remember=prospective.discordReader.rememberCredential;const auto label=personal?L"Personal account token (private; do not share it)":connection==L"Bot"?L"Bot token (private)":L"OAuth access token (private)";
        if(ShowDiscordReaderCredentialDialog(owner,label,token,remember)!=IDOK)return false;
        struct Wipe{std::wstring& value;~Wipe(){if(!value.empty())SecureZeroMemory(&value[0],value.size()*sizeof(wchar_t));}}wipe{token};
        if(token.empty()||!SafeSingleLine(token,8192)){DiscordReaderNotice(owner,L"Enter a nonempty single-line credential; no value was saved.");return false;}
        std::wstring protectedValue;
        try{
#ifdef DESKTOPSTUB_INERT_HARDWARE
            if(g_testDiscordProtect)protectedValue=g_testDiscordProtect(token);
#else
            protectedValue=aip::ProtectSecretForCurrentUser(token);
#endif
        }catch(...){}
        if(protectedValue.empty()){DiscordReaderNotice(owner,L"The credential could not be protected for this Windows user. Nothing was saved.");return false;}
        const auto revision=NewGalleryIdentity();if(revision.empty())return false;
        prospective.discordReader.rememberCredential=remember;enteredRevision=revision;
        prospective.settings[L"discordreader.credentialprotected"]=protectedValue;prospective.settings[L"discordreader.credentialconnection"]=connection;prospective.settings[L"discordreader.credentialrevision"]=revision;
        changes={{section,L"DiscordReader.RememberCredential",remember?L"1":L"0"},{section,L"DiscordReader.CredentialProtected",remember?protectedValue:L""},{section,L"DiscordReader.CredentialConnection",remember?std::wstring(connection):L""},{section,L"DiscordReader.CredentialRevision",remember?revision:L""}};
        enteredProfile=DiscordReaderInlineProfile(prospective,error,true);
    }
    aip::discord::ReaderIdentityProbeOptions probe;probe.enabled=true;probe.connection=connection;probe.profileKey=DiscordReaderProfileKey(id);probe.warningProfileKey=probe.profileKey;probe.experimentalWarningReviewed=personal;
    probe.operationTimeoutMs=prospective.discordReader.operationTimeoutMs;probe.maximumBytes=static_cast<std::size_t>(prospective.discordReader.maximumBytes);
    if(presence)probe.profilePath=DiscordReaderProfilePath(tile);else{probe.profileOverride=enteredProfile?enteredProfile:DiscordReaderInlineProfile(prospective,error);probe.profilePath=probe.profileOverride?L"":DiscordReaderProfilePath(prospective);}
    if(!error.empty()||(!probe.profileOverride&&probe.profilePath.empty())){DiscordReaderNotice(owner,error.empty()?L"Set a protected credential or choose an explicit advanced Discord profile first.":error);return false;}
    const auto identity=RunDiscordReaderIdentityProbe(probe);if(g_shutdownState.IsRequested())return false;
    if(!identity.ok||!Snowflake(identity.user.id)){DiscordReaderNotice(owner,identity.status.empty()?L"The account could not be identified; nothing was saved.":identity.status);return false;}
    const auto name=PreferredUserName(identity.user.displayName,identity.user.username,L"Discord account");
    const auto prompt=L"Account: "+DisplayText(name,128)+L"\nUsername: @"+DisplayText(identity.user.username.value_or(L""),128)+L"\nAccount ID: "+identity.user.id+
        L"\nConnection: "+std::wstring(connection)+L"\nTile: "+tile.name+L"\n\nEnable this reviewed account connection? Sending remains separately controlled and never occurs during reading, rendering or reconnect.";
    if(DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,prompt.c_str(),L"Review Discord account",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return false;
    prospective.discordReader.accountId=identity.user.id;
    changes.push_back({section,L"DiscordReader.AccountId",identity.user.id});changes.push_back({section,L"DiscordReader.Connection",connection});changes.push_back({section,L"DiscordReader.Enabled",presence?L"0":L"1"});
    changes.push_back({section,L"DiscordReader.ExperimentalAcknowledged",personal?L"1":L"0"});changes.push_back({section,L"DiscordReader.AcknowledgedProfileKey",personal?probe.profileKey:L""});
    changes.push_back({section,L"DiscordReader.AcknowledgedAccountId",personal?identity.user.id:L""});changes.push_back({section,L"DiscordReader.AcknowledgedCredentialRevision",personal?(enteredRevision.empty()?DiscordReaderCredentialRevision(prospective):enteredRevision):L""});
    if(!SaveDiscordReaderChanges(owner,tile,changes))return false;
    if(enteredProfile){std::lock_guard<std::mutex> lock(g_discordReaderCredentialMutex);const auto key=DiscordReaderProfileKey(id);if(prospective.discordReader.rememberCredential)g_discordReaderCredentials.erase(key);else g_discordReaderCredentials[key]={identity.user.id,connection,enteredRevision,enteredProfile};}
    if(personal){std::lock_guard<std::mutex> lock(g_discordReaderWarningMutex);if(g_discordReaderWarningSessions.size()>1024)g_discordReaderWarningSessions.clear();g_discordReaderWarningSessions.insert(DiscordReaderWarningKey(prospective));}
    RefreshTileInstanceNow(id);return true;
}
static bool ActivateDiscordReader(HWND owner,const std::wstring& id,bool forceCompose){
    using namespace aip::discord_tiles;aip::content::TileInstance tile;std::wstring error;
    if(!ReadDiscordReaderTile(id,tile,error)){DiscordReaderNotice(owner,error);return false;}
    const auto& c=tile.discordReader;
    if(!forceCompose&&(c.clickAction==L"OpenChannel"||c.clickAction==L"OpenProfile")){
        const auto url=c.clickAction==L"OpenChannel"&&Snowflake(c.channelId)?L"https://discord.com/channels/"+(c.guildId.empty()?std::wstring(L"@me"):c.guildId)+L"/"+c.channelId:std::wstring(L"https://discord.com/channels/@me");
        return reinterpret_cast<INT_PTR>(ShellExecuteW(owner,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
    }
    const auto reader=DiscordReaderForTile(tile);if(!reader){DiscordReaderNotice(owner,L"Connect this exact account and channel before composing. No message was sent.");return false;}
    const auto snapshot=reader->Read().data;const auto key=DiscordReaderRequestKey(tile);const auto nonce=NewGalleryIdentity().substr(0,25);static std::atomic<std::uint64_t> activations{0};ComposeIntent intent;
    if(!forceCompose&&c.clickAction==L"SendConfigured"){
        if(!BeginConfiguredSend(snapshot,c,nonce,++activations,intent,error)){DiscordReaderNotice(owner,error);return false;}
    }else{
        if(!BeginCompose(snapshot,c,c.configuredText,nonce,++activations,intent,error)){DiscordReaderNotice(owner,error);return false;}
        auto message=intent.Text();const auto sender=snapshot.user.username.value_or(c.accountId);
        const auto prompt=L"Sender: @"+sender+L"\nChannel ID: "+c.channelId+L"\nEnter a message. You will review it before sending.";
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(!g_testDiscordComposeDialog||g_testDiscordComposeDialog(owner,L"Compose Discord message",prompt,message)!=IDOK)return false;
#else
        if(ShowTextInputDialog(owner,L"Compose Discord message",prompt,message,true)!=IDOK)return false;
#endif
        if(!SetComposeText(intent,message,error)||!ReviewCompose(intent,snapshot,c,error)){DiscordReaderNotice(owner,error);return false;}
        const auto review=L"Send as @"+sender+L"\nTo channel "+c.channelId+L"\n\n"+message+L"\n\nSend this message now?";
        if(DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,review.c_str(),L"Review Discord message",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return false;
    }
    aip::content::TileInstance current;if(!ReadDiscordReaderTile(id,current,error)||DiscordReaderRequestKey(current)!=key||DiscordReaderForTile(current)!=reader){DiscordReaderNotice(owner,L"The tile, account, channel or credential changed. No message was sent.");return false;}
    aip::discord::ReaderSendResult sent;std::atomic<bool> done{false};std::thread worker([&]{try{sent=reader->SendReviewed(intent);}catch(...){sent.status=L"The send could not be confirmed. Do not retry blindly.";}done=true;});
    bool quit=false;while(!done){MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){quit=true;continue;}TranslateMessage(&message);DispatchMessageW(&message);}}
    worker.join();if(quit)PostQuitMessage(0);DiscordReaderNotice(owner,sent.status.empty()?(sent.state==aip::discord::ReaderSendState::Sent?L"Message sent.":L"The message could not be confirmed. Do not retry blindly."):sent.status,sent.state!=aip::discord::ReaderSendState::Sent);return sent.state==aip::discord::ReaderSendState::Sent;
}

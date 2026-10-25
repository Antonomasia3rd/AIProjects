// Actual asynchronous Mail adapter and publication with synthetic credentials,
// HTTP and immutable content only. No browser, mailbox or native badge API.
static void GmailHostReply(aip::HttpFetchResponse& response,const std::string& text,int status=200)
{
    response.statusCode=status;response.body.assign(text.begin(),text.end());
}
static aip::gmail::Hooks GmailHostHooks(const aip::gmail::Configuration& configuration,std::atomic<unsigned>& requests,std::atomic<bool>& fail)
{
    aip::gmail::Hooks hooks;hooks.inert=true;const auto account=aip::gmail::AccountConfiguration(configuration);
    hooks.unprotect=[account](const std::wstring&,std::wstring& value){value=aip::google_calendar::detail::Envelope(account)+L"SYNTHETIC-REFRESH";return true;};
    hooks.http=[account,&requests,&fail](const aip::HttpFetchOptions& request,aip::HttpFetchResponse& response,std::wstring&){
        ++requests;if(request.url.find(L"oauth2.googleapis.com/token")!=std::wstring::npos){
            const auto body=std::string("{\"access_token\":\"SYNTHETIC-ACCESS\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":")+aip::JsonEscape(aip::google_calendar::OAuthScopes(account.oauthPurpose))+"}";
            GmailHostReply(response,body);return true;}
        if(fail.load()){GmailHostReply(response,"{}",503);return true;}
        if(request.url.find(L"/profile?")!=std::wstring::npos){GmailHostReply(response,R"({"emailAddress":"fixture@example.invalid"})");return true;}
        if(request.url.find(L"/labels/INBOX?")!=std::wstring::npos){GmailHostReply(response,R"({"id":"INBOX","messagesUnread":7,"threadsUnread":4})");return true;}
        if(request.url.find(L"/messages?")!=std::wstring::npos){GmailHostReply(response,R"({"messages":[{"id":"fixture1"}]})");return true;}
        GmailHostReply(response,R"({"id":"fixture1","internalDate":"1793000000000","payload":{"headers":[{"name":"From","value":"Fixture Person <fixture@example.invalid>"},{"name":"Subject","value":"Synthetic Mail subject"}]}})");return true;
    };return hooks;
}
static void TestGmailHost()
{
    const auto oldRunning=g_running.load();g_running=true;
    ContentGmailProvider provider;aip::gmail::Configuration cfg;std::atomic<unsigned> requests{0};std::atomic<bool> fail{false};
    provider.testHooks=GmailHostHooks(cfg,requests,fail);provider.Refresh(cfg,true,true);auto view=provider.Read(cfg);
    Check(!view.configured&&!view.ok&&!view.pending&&requests==0,"unconfigured actual Mail adapter does not start HTTP or expose a stale mailbox");
    cfg.account.clientId=L"123-fixture.apps.googleusercontent.com";cfg.account.ownerKey=L"private-mail-host/one";cfg.account.refreshTokenProtected=L"dpapi:v1:utf8:fixture";
    provider.testHooks=GmailHostHooks(cfg,requests,fail);provider.Refresh(cfg,true,true);bool completed=false;view=provider.Read(cfg,&completed);
    Check(completed&&view.configured&&view.ok&&!view.stale&&view.messages.size()==1&&view.unreadMessages==7&&view.unreadThreads==4,
        "actual asynchronous Mail source returns independent exact unread counters and parsed message rows");
    Check(view.mailboxAddress==L"fixture@example.invalid"&&view.messages.front().sender==L"Fixture Person"&&view.messages.front().subject==L"Synthetic Mail subject"&&view.messages.front().snippet.empty(),
        "native Mail view keeps sender subject preview and private account activation metadata separate");
    const auto fetched=requests.load();provider.Refresh(cfg,true,false);view=provider.Read(cfg);Check(view.ok&&requests==fetched,"unchanged Mail reads reuse the configured source deadline without more HTTP");
    fail=true;provider.Refresh(cfg,true,true);view=provider.Read(cfg,&completed);
    Check(completed&&view.ok&&view.stale&&!view.status.empty()&&view.messages.size()==1,"transient Mail refresh failure retains the last good mailbox view with a diagnostic");
    auto changed=cfg;changed.account.ownerKey=L"private-mail-host/another";provider.testHooks=GmailHostHooks(changed,requests,fail);provider.Refresh(changed,true,true);view=provider.Read(changed);
    Check(!view.ok&&!view.stale&&view.messages.empty()&&view.mailboxAddress.empty(),"another account/profile revision never inherits the former Mail rows or address");
    fail=false;provider.Refresh(changed,true,true);view=provider.Read(changed);Check(view.ok&&!view.stale,"a later successful Mail refresh clears the transient failure");
    provider.Stop();view=provider.Read(changed);Check(!view.ok&&!view.stale&&view.messages.empty(),"Stop releases cached Mail content and cannot revive it through Read");
    const auto oldUser=g_testTileRegistrationUserScope;g_testTileRegistrationUserScope=L"7777777777777777";
    aip::content::TileInstance left,right;left.type=right.type=aip::content::TileInstanceType::Mail;left.id=L"mail-left";right.id=L"mail-right";
    const auto boundLeft=GmailConfigurationForTile(left),boundRight=GmailConfigurationForTile(right);
    Check(!boundLeft.account.ownerKey.empty()&&boundLeft.account.ownerKey!=boundRight.account.ownerKey&&boundLeft.account.oauthPurpose==aip::google_calendar::OAuthPurpose::GmailMetadata,
        "actual host account helper binds each Mail tile to a distinct Gmail purpose/profile/tile owner");g_testTileRegistrationUserScope=oldUser;
    const auto oldTemplate=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    aip::content::Snapshot frame;frame.active=true;frame.mailSource=true;frame.secondaryBranding=L"Auto";frame.backgroundId=aip::content::BackgroundId::Transparent;
    frame.mailRows={{L"Fixture sender",L"Synthetic subject",L""},{L"Second fixture",L"Second subject",L""}};frame.mailUnreadCount=7;frame.mailboxAddress=L"fixture@example.invalid";
    const std::vector<LiveTileUpdateAsset> assets{{L"LiveMediumTile",L"absent.png",L"TileMedium",L"absent.png",150,150},
        {L"LiveWideTile",L"absent.png",L"TileWide",L"absent.png",310,150},{L"LiveLargeTile",L"absent.png",L"TileLarge",L"absent.png",310,310}};
    std::wstring xml;{ContentRenderScope scope(&frame);ManifestDisplayInfo manifest;xml=BuildLiveTileXml(ParentDirectoryOfPath(g_iniPath),manifest,assets);}
    Check(!xml.empty()&&xml.find(L"Synthetic subject")!=std::wstring::npos&&xml.find(L"fixture@example.invalid")==std::wstring::npos&&xml.find(L"template=\"TileSmall\"")==std::wstring::npos,
        "actual Mail XML publication contains prepared text without private account addresses or invented Small text");
    frame.mailIncludeLarge=false;{ContentRenderScope scope(&frame);ManifestDisplayInfo manifest;xml=BuildLiveTileXml(ParentDirectoryOfPath(g_iniPath),manifest,assets);}
    Check(xml.find(L"template=\"TileLarge\"")==std::wstring::npos,"intermediate actual Mail queue publication omits Large instead of a generic fallback binding");
    frame.secondaryTileId=L"AipInstance_mailfixture";frame.mailIncludeLarge=true;frame.policy.owner=L"mailfixture";
    const auto prepared=BuildMailTileFrames(frame,true);
    Check(prepared.size()==2&&prepared[0].nativeQueueReset&&!prepared[1].nativeQueueReset&&
        prepared[0].nativeQueueTag==L"mail1"&&prepared[1].nativeQueueTag==L"mail2"&&
        !prepared[0].mailIncludeLarge&&prepared[1].mailIncludeLarge,
        "actual Mail queue rotates each message and publishes its aggregate Large once with a single reset");
    const auto previous=CurrentSecondarySnapshots();PublishSecondarySnapshots(prepared);
    Check(MailQueueFrameCurrent(prepared[0])&&MailQueueFrameCurrent(prepared[1])&&SecondaryDeliveryKey(prepared[0])!=SecondaryDeliveryKey(prepared[1]),
        "Mail queue frames retain separate target/tag delivery and are valid only in the current publication");
    frame.mailRows.clear();frame.mailUnreadCount=0;frame.textMode=aip::content::TextMode::Off;
    const auto empty=BuildMailTileFrames(frame,true);PublishSecondarySnapshots(empty);
    Check(empty.size()==1&&!empty[0].nativeQueue&&empty[0].nativeQueueReset&&!MailQueueFrameCurrent(prepared[0]),
        "empty or disconnected Mail publication clears its former queue and rejects stale message work");
    {ContentRenderScope scope(&empty[0]);ManifestDisplayInfo manifest;xml=BuildLiveTileXml(ParentDirectoryOfPath(g_iniPath),manifest,assets);}
    // Missing fixture images may leave no static binding. The publication reset
    // contract above is still validated without touching a native notification.
    auto cache=prepared[0];const auto cachedKey=ContentSnapshotKey(cache);cache.mailRows.front().secondary=L"Changed subject";
    Check(cachedKey!=ContentSnapshotKey(cache),"Mail content cache invalidates when the visible subject changes");
    PublishSecondarySnapshots(previous);
    g_liveTileTemplateStyle=oldTemplate;g_manifestCompatibilityTarget=oldTarget;g_running=oldRunning;
}

// Account reading never starts the presence writer. Credentials stay in an
// explicit protected profile/inline value; avatar requests carry no auth header.
#include "../DiscordRPC/account_reader.h"
#include "../discord_reader_tile_layout.h"
static std::wstring DiscordReaderProfileKey(const std::wstring& id){std::wstring path;if(!aip::named_objects::NormalizeFileIdentity(g_iniPath,path))return {};return path+L"#"+id;}
static std::wstring DiscordReaderProfilePath(const aip::content::TileInstance& tile){auto path=TrimCopy(tile.configuration.discord.profilePath);if(!path.empty()&&PathIsRelativeW(path.c_str()))path=ParentDirectoryOfPath(g_iniPath)+L"\\"+path;return path;}
static std::wstring DiscordReaderExtra(const aip::content::TileInstance& tile,const wchar_t* key){return aip::content::TileInstanceRead(tile,IniReadS,L"DiscordReader",key,L"");}
// Session credentials are protected before reaching this map and never written
// to settings. A different account, tile profile or connection cannot reuse one.
struct DiscordReaderSessionCredential {std::wstring accountId,connection,revision;std::shared_ptr<const aip::discord::Profile> profile;};
static std::mutex g_discordReaderCredentialMutex;
static std::map<std::wstring,DiscordReaderSessionCredential> g_discordReaderCredentials;
static void ForgetDiscordReaderSessionCredential(const std::wstring& profileKey){std::lock_guard<std::mutex> lock(g_discordReaderCredentialMutex);g_discordReaderCredentials.erase(profileKey);}
static std::shared_ptr<const aip::discord::Profile> DiscordReaderSessionProfile(const aip::content::TileInstance& tile,std::wstring* revision=nullptr){
    std::lock_guard<std::mutex> lock(g_discordReaderCredentialMutex);const auto key=DiscordReaderProfileKey(tile.id);const auto found=g_discordReaderCredentials.find(key);
    if(found==g_discordReaderCredentials.end())return {};
    if(tile.discordReader.rememberCredential||found->second.accountId!=tile.discordReader.accountId||found->second.connection!=tile.discordReader.connection)return {};
    if(revision)*revision=found->second.revision;return found->second.profile;
}
static std::wstring DiscordReaderCredentialRevision(const aip::content::TileInstance& tile){
    if(tile.discordReader.view==L"Presence"){const auto path=DiscordReaderProfilePath(tile);return path.empty()?L"":FileFingerprint(path);}
    std::wstring sessionRevision;if(DiscordReaderSessionProfile(tile,&sessionRevision))return sessionRevision;
    if(tile.discordReader.rememberCredential&&!DiscordReaderExtra(tile,L"CredentialProtected").empty())return DiscordReaderExtra(tile,L"CredentialRevision");
    const auto path=DiscordReaderProfilePath(tile);return path.empty()?L"":FileFingerprint(path);
}
static std::mutex g_discordReaderWarningMutex;
static std::set<std::wstring> g_discordReaderWarningSessions;
static std::wstring DiscordReaderWarningKey(const aip::content::TileInstance& tile){return DiscordReaderProfileKey(tile.id)+L":"+tile.discordReader.accountId+L":"+DiscordReaderCredentialRevision(tile);}
static std::wstring DiscordReaderRequestKey(const aip::content::TileInstance& tile){
    // This context guards a reviewed send as well as refresh reuse. Compare the
    // exact canonical values rather than a lossy presentation/cache fingerprint.
    std::wstring key;const auto append=[&](const std::wstring& value){key+=std::to_wstring(value.size())+L":"+value;};
    for(const auto& value:aip::discord_tiles::ConfigurationValues(tile.discordReader)){append(value.first);append(value.second);}
    append(DiscordReaderProfileKey(tile.id));append(DiscordReaderCredentialRevision(tile));append(DiscordReaderProfilePath(tile));return key;
}
struct DiscordReaderRegistration {std::weak_ptr<aip::discord::AccountReader> reader;std::wstring key;};
static std::mutex g_discordReaderRegistryMutex;
static std::map<std::wstring,DiscordReaderRegistration> g_discordReaderRegistry;
static std::atomic<std::uint64_t> g_discordReaderGenerations{0};
static std::shared_ptr<aip::discord::AccountReader> DiscordReaderForTile(const aip::content::TileInstance& tile){std::lock_guard<std::mutex> lock(g_discordReaderRegistryMutex);const auto found=g_discordReaderRegistry.find(DiscordReaderProfileKey(tile.id));return found!=g_discordReaderRegistry.end()&&found->second.key==DiscordReaderRequestKey(tile)?found->second.reader.lock():std::shared_ptr<aip::discord::AccountReader>{};}
static std::shared_ptr<const aip::discord::Profile> DiscordReaderInlineProfile(const aip::content::TileInstance& tile,std::wstring& error,bool pendingCredential=false){
    if(!pendingCredential&&!tile.discordReader.rememberCredential)return DiscordReaderSessionProfile(tile);
    const auto value=DiscordReaderExtra(tile,L"CredentialProtected");if(value.empty())return {};
    if(DiscordReaderExtra(tile,L"CredentialConnection")!=tile.discordReader.connection){error=L"The saved credential belongs to a different Discord connection type. Replace it in this tile's settings.";return {};}
    auto profile=std::make_shared<aip::discord::Profile>();const bool personal=tile.discordReader.connection==L"UserTokenExperimental";
    profile->values[{personal?L"general":L"reader",personal?L"token_protected":tile.discordReader.connection==L"Bot"?L"bot_token_protected":L"oauth_token_protected"}]=value;return profile;
}
static bool DiscordReaderPersonalAcknowledged(const aip::content::TileInstance& tile){
    const auto& c=tile.discordReader;
    const bool saved=c.experimentalAcknowledged&&c.acknowledgedProfileKey==DiscordReaderProfileKey(tile.id)&&c.acknowledgedAccountId==c.accountId&&
        !DiscordReaderCredentialRevision(tile).empty()&&DiscordReaderExtra(tile,L"AcknowledgedCredentialRevision")==DiscordReaderCredentialRevision(tile);
    if(!saved)return false;if(!c.warnEveryConnection)return true;
    std::lock_guard<std::mutex> lock(g_discordReaderWarningMutex);return g_discordReaderWarningSessions.count(DiscordReaderWarningKey(tile))!=0;
}
static bool DiscordReaderWarningAcknowledged(const aip::content::TileInstance& tile){return tile.discordReader.connection!=L"UserTokenExperimental"||DiscordReaderPersonalAcknowledged(tile);}
static std::wstring DiscordReaderTime(const aip::discord_tiles::Timestamp& value,const std::wstring& format){
    const auto ticks=(static_cast<std::uint64_t>(value.seconds+11644473600LL))*10000000ULL;FILETIME ft{static_cast<DWORD>(ticks),static_cast<DWORD>(ticks>>32)};SYSTEMTIME utc{},local{};
    if(!FileTimeToSystemTime(&ft,&utc))return {};if(format==L"ISO")return FormatWide(L"%04u-%02u-%02u %02u:%02u UTC",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute);
    if(!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local))return {};wchar_t date[128]{},time[128]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&local,nullptr,date,static_cast<int>(std::size(date)),nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,0,&local,format==L"24"?L"HH:mm":format==L"12"?L"h:mm tt":nullptr,time,static_cast<int>(std::size(time)));
    return std::wstring(date)+L" "+time;
}
struct ContentDiscordAccountProvider {
    std::shared_ptr<aip::discord::AccountReader> reader;
    std::wstring key,error,avatarKey,warningKey,registryKey;std::uint64_t generation=0,revision=0;bool started=false,stopping=false,warnEach=false;
    aip::content::AsyncSource<std::shared_ptr<const aip::content::SmtcArtworkFile>> avatar;
    std::shared_ptr<aip::content::SmtcArtworkCache> artwork;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    std::function<void(aip::discord::ReaderOptions&)> configureTestOptions;
    std::function<bool(const aip::HttpFetchOptions&,aip::HttpFetchResponse&,std::wstring&)> avatarTransport;
#endif
    static std::uint64_t Now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(steady_clock::now().time_since_epoch()).count());}
    void Cancel(){if(!registryKey.empty()){std::lock_guard<std::mutex> lock(g_discordReaderRegistryMutex);const auto current=g_discordReaderRegistry.find(registryKey);if(current!=g_discordReaderRegistry.end()&&current->second.reader.lock()==reader)g_discordReaderRegistry.erase(current);}if(started&&warnEach&&!warningKey.empty()){std::lock_guard<std::mutex> lock(g_discordReaderWarningMutex);g_discordReaderWarningSessions.erase(warningKey);}key.clear();avatarKey.clear();avatar.CancelPending();artwork.reset();if(reader)stopping=!reader->Stop(0);if(!stopping)started=false;}
    void Stop(){Cancel();ForgetDiscordReaderSessionCredential(registryKey);if(reader&&reader->Stop(5000)){reader.reset();stopping=false;}started=false;}
    void Refresh(const aip::content::TileInstance& tile,bool synchronous){
        using namespace aip::discord_tiles;const auto& c=tile.discordReader;
        {std::lock_guard<std::mutex> lock(g_discordReaderCredentialMutex);const auto found=g_discordReaderCredentials.find(DiscordReaderProfileKey(tile.id));if(found!=g_discordReaderCredentials.end()&&(c.rememberCredential||found->second.accountId!=c.accountId||found->second.connection!=c.connection))g_discordReaderCredentials.erase(found);}
        if(stopping){stopping=reader&&!reader->Stop(0);if(stopping)return;started=false;}
        const auto reject=[&](const wchar_t* text){Cancel();error=text;};
        if(c.view==L"Presence"){reject(L"");return;}
        if(!c.enabled){reject(L"The account reader is disabled. Identify and connect it explicitly in Source settings.");return;}
        if(c.connection!=L"UserTokenExperimental"&&c.connection!=L"Bot"&&c.connection!=L"OAuth"){reject(L"This reader connection is not implemented in this build. Choose a supported reader connection or keep the Presence view.");return;}
        if(c.accountId.empty()){reject(L"Identify and review this account in Source settings before connecting.");return;}
        if(c.view==L"Channel"&&c.channelId.empty()){reject(L"Choose the exact channel ID in Source settings.");return;}
        if(!DiscordReaderWarningAcknowledged(tile)){reject(L"The experimental personal-account warning needs acknowledgement for this account and credential revision. Open Source settings.");return;}
        std::wstring issue;auto profile=DiscordReaderInlineProfile(tile,issue);if(!issue.empty()){reject(issue.c_str());return;}
        const auto path=DiscordReaderProfilePath(tile);if(!profile&&path.empty()){reject(L"Set a protected reader credential or choose an explicit advanced Discord profile in Source settings.");return;}
        const auto requested=DiscordReaderRequestKey(tile);
        if(key!=requested){
            avatar.CancelPending();avatarKey.clear();artwork.reset();aip::discord::ReaderOptions options;options.enabled=true;
            generation=++g_discordReaderGenerations;revision=++g_discordReaderGenerations;
            options.binding={DiscordReaderProfileKey(tile.id),c.accountId,c.guildId,c.channelId,generation,revision};options.configuration=c;
            options.profilePath=path;options.profileOverride=profile;options.refreshMs=c.refreshMs;options.operationTimeoutMs=c.operationTimeoutMs;
            options.reconnectMinimumMs=c.reconnectMinimumMs;options.reconnectMaximumMs=c.reconnectMaximumMs;options.maximumBytes=static_cast<std::size_t>(c.maximumBytes);
            options.useGateway=c.useGateway;options.allowReviewedSend=c.allowReviewedSend;
            if(c.connection==L"UserTokenExperimental")options.acknowledgement=Acknowledgement{options.binding.profileKey,c.accountId,c.connection,generation,1};
#ifdef DESKTOPSTUB_INERT_HARDWARE
            if(!configureTestOptions){reject(L"Discord account operations are inert in validation.");return;}configureTestOptions(options);
#endif
            if(!reader)reader=std::make_shared<aip::discord::AccountReader>();warnEach=c.warnEveryConnection;warningKey=DiscordReaderWarningKey(tile);
            const bool ok=started?reader->Reload(options,error):reader->Start(options,error);if(!ok){Cancel();return;}key=requested;started=true;error.clear();
            registryKey=DiscordReaderProfileKey(tile.id);{std::lock_guard<std::mutex> lock(g_discordReaderRegistryMutex);g_discordReaderRegistry[registryKey]={reader,key};}
        }
        if(synchronous&&reader&&started){const auto until=steady_clock::now()+milliseconds(c.operationTimeoutMs+1000);while(g_running&&steady_clock::now()<until){const auto value=reader->Read();if(value.data.hasSuccess||value.data.failure!=Failure::None||!value.running)break;std::this_thread::sleep_for(milliseconds(5));}}
        RefreshAvatar(tile,synchronous);
    }
    aip::discord::ReaderSnapshot Read()const{return reader&&started?reader->Read():aip::discord::ReaderSnapshot{};}
    void RefreshAvatar(const aip::content::TileInstance& tile,bool synchronous){
        const auto value=Read();const auto url=tile.discordReader.view==L"Profile"&&tile.discordReader.avatarPlacement==L"Right"?aip::discord_tiles::AvatarUrl(value.data.user):L"";
        if(url.empty()){avatar.CancelPending();avatarKey.clear();artwork.reset();return;}
        if(avatarKey!=url){avatar.CancelPending();avatarKey=url;artwork=std::make_shared<aip::content::SmtcArtworkCache>(L"DesktopStub Discord avatar",DiscordReaderProfileKey(tile.id)+L":"+value.data.binding.accountId);}
        auto cache=artwork;const auto timeout=tile.discordReader.operationTimeoutMs;const auto maximum=(std::min)(static_cast<std::size_t>(tile.discordReader.maximumBytes),static_cast<std::size_t>(512u*1024u));
        std::function<bool(const aip::HttpFetchOptions&,aip::HttpFetchResponse&,std::wstring&)> http;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        http=avatarTransport;if(!http)return;
#else
        http=aip::FetchHttpBytes;
#endif
        avatar.Request(url,std::chrono::hours(24),[url,http,cache,timeout,maximum](const auto& canceled){aip::HttpFetchOptions request;request.url=url;request.accept=L"image/png";request.maximumBytes=maximum;request.timeout=milliseconds(timeout);request.followRedirects=false;request.canceled=canceled;
            aip::HttpFetchResponse response;std::wstring issue;if(!http(request,response,issue)||response.statusCode!=200||canceled())return std::shared_ptr<const aip::content::SmtcArtworkFile>{};
            return cache->Store(response.body,issue,steady_clock::now()+milliseconds(timeout),canceled);
        },WakePollThread);
        if(synchronous)avatar.Wait(url,milliseconds(timeout));
    }
    std::shared_ptr<const aip::content::SmtcArtworkFile> Avatar(){std::shared_ptr<const aip::content::SmtcArtworkFile> image;std::string issue;if(!avatarKey.empty())avatar.Read(avatarKey,image,issue);return image;}
};

#pragma once
#include "account_reader_protocol.inc"
#include <condition_variable>
namespace aip { namespace discord { namespace account_detail {
using Clock=std::chrono::steady_clock;
inline discord_tiles::Milliseconds Now(){return static_cast<discord_tiles::Milliseconds>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
inline void Wipe(std::wstring& value){if(!value.empty())SecureZeroMemory(&value[0],value.size()*sizeof(wchar_t));value.clear();}
inline void Wipe(std::string& value){if(!value.empty())SecureZeroMemory(&value[0],value.size());value.clear();}
inline bool Canceled(const Cancel& cancel){return cancel&&cancel();}
inline bool Connection(const std::wstring& value){return value==L"UserTokenExperimental"||value==L"Bot"||value==L"OAuth";}
inline bool ProfileCopy(const std::wstring& path,const std::shared_ptr<const Profile>& supplied,std::shared_ptr<const Profile>& result,std::wstring& error){
    auto profile=std::make_shared<Profile>();if(supplied){for(const auto& field:supplied->values)profile->values[{ToLower(field.first.first),ToLower(field.first.second)}]=field.second;}
    else{if(path.empty()||PathIsRelativeW(path.c_str())){error=L"Select an existing absolute Discord credential profile.";return false;}std::vector<aip::IniSectionData> document;
        if(!aip::LoadIniDocument(path,document)){error=L"The Discord credential profile could not be read.";return false;}
        for(const auto& section:document)for(const auto& field:section.entries)profile->values[{ToLower(section.name),ToLower(field.key)}]=field.value;}
    result=std::move(profile);return true;
}
inline std::wstring Value(const Profile& profile,const wchar_t* section,const wchar_t* key){const auto found=profile.values.find({section,key});return found==profile.values.end()?L"":found->second;}
inline bool Token(const Profile& profile,const std::wstring& connection,const CredentialDecoder& decoder,std::wstring& token){
    const auto decode=decoder?decoder:CredentialDecoder([](const std::wstring& protectedValue){return aip::UnprotectSecretForCurrentUser(protectedValue,aip::DpapiLegacyEncoding::Utf8);});
    std::wstring error;bool resolved=false;
    if(connection==L"UserTokenExperimental")resolved=ResolveConfiguredToken(Value(profile,L"general",L"token"),Value(profile,L"general",L"token_protected"),decode,token,error);
    else resolved=ResolveConfiguredToken(L"",Value(profile,L"reader",connection==L"Bot"?L"bot_token_protected":L"oauth_token_protected"),decode,token,error);
    if(!resolved||token.size()<8||token.size()>8192||std::any_of(token.begin(),token.end(),[](wchar_t c){return c<=0x20||c>0x7e;})){Wipe(token);return false;}return true;
}
inline bool NativeHttp(const ReaderHttpRequest& request,ReaderHttpResponse& response,std::wstring& error){
    aip::HttpFetchOptions options;options.url=request.url;options.method=request.method;options.body=request.body;options.headers=request.headers;
    options.userAgent=L"DesktopStub Discord reader";options.accept=L"application/json";options.followRedirects=false;options.timeout=std::chrono::milliseconds(request.timeoutMs);options.maximumBytes=request.maximumBytes;options.canceled=request.canceled;
    aip::HttpFetchResponse received;std::wstring privateError;bool ok=false;try{ok=aip::FetchHttpBytes(options,received,privateError);}catch(...){ok=false;}
    for(auto& field:options.headers)Wipe(field.second);Wipe(options.body);Wipe(privateError);response.statusCode=received.statusCode;response.body.assign(received.body.begin(),received.body.end());
    if(!received.body.empty())SecureZeroMemory(received.body.data(),received.body.size());error=ok?L"":L"Discord could not be reached.";return ok;
}
struct Reply {bool ok=false;unsigned code=0,retryAfter=0;discord_tiles::Failure failure=discord_tiles::Failure::None;std::string body;};
inline unsigned RetryAfter(const std::string& json){reader_detail::Object object;if(!reader_detail::ObjectValue(json,object))return 1000;const auto* raw=reader_detail::Raw(object,"retry_after");if(!raw||raw->empty()||raw->size()>16)return 1000;
    const auto limit=static_cast<std::uint64_t>((std::numeric_limits<unsigned>::max)());
    std::uint64_t whole=0,fraction=0;std::size_t p=0;while(p<raw->size()&&(*raw)[p]>='0'&&(*raw)[p]<='9'){whole=whole*10+static_cast<unsigned>((*raw)[p++]-'0');if(whole>limit/1000)return static_cast<unsigned>(limit);}if(!p)return 1000;
    unsigned digits=0;bool remainder=false;if(p<raw->size()&&(*raw)[p]=='.'){++p;while(p<raw->size()&&(*raw)[p]>='0'&&(*raw)[p]<='9'){if(digits<3)fraction=fraction*10+static_cast<unsigned>((*raw)[p]-'0');else remainder|=(*raw)[p]!='0';++digits;++p;}if(!digits)return 1000;}
    if(p!=raw->size())return 1000;while(digits<3){fraction*=10;++digits;}const auto ms=whole*1000+fraction+(remainder?1:0);return static_cast<unsigned>((std::max)(1ULL,(std::min)(limit,ms)));
}
inline Reply Request(const ReaderHttpTransport& transport,const std::wstring& token,const std::wstring& connection,const std::wstring& path,
    int timeout,std::size_t maximum,const Cancel& cancel,const std::string& body={}){
    Reply result;if(Canceled(cancel)){result.failure=discord_tiles::Failure::Transient;return result;}
    ReaderHttpRequest request;request.url=L"https://discord.com/api/v10"+path;request.timeoutMs=timeout;request.maximumBytes=maximum;request.canceled=cancel;
    request.headers.push_back({L"Authorization",connection==L"Bot"?L"Bot "+token:connection==L"OAuth"?L"Bearer "+token:token});
    if(!body.empty()){request.method=L"POST";request.body=body;request.headers.push_back({L"Content-Type",L"application/json"});}
    ReaderHttpResponse response;std::wstring privateError;bool ok=false;try{ok=(transport?transport:ReaderHttpTransport(NativeHttp))(request,response,privateError);}catch(...){ok=false;}
    for(auto& field:request.headers)Wipe(field.second);Wipe(request.body);
    Wipe(privateError);
    if(!ok||Canceled(cancel)){Wipe(response.body);result.failure=discord_tiles::Failure::Transient;return result;}
    result.code=response.statusCode;if(response.body.size()>maximum){Wipe(response.body);result.failure=discord_tiles::Failure::InvalidData;return result;}
    if(result.code==429){result.retryAfter=RetryAfter(response.body);result.failure=discord_tiles::Failure::Transient;Wipe(response.body);return result;}
    if(result.code<200||result.code>=300){result.failure=result.code==401?discord_tiles::Failure::Authentication:(result.code==403||result.code==404)?discord_tiles::Failure::Permission:discord_tiles::Failure::Transient;Wipe(response.body);return result;}
    result.ok=true;result.body=std::move(response.body);return result;
}
inline std::wstring FailureText(discord_tiles::Failure failure){using F=discord_tiles::Failure;return failure==F::Authentication?L"Discord rejected this credential. Review the selected account before reconnecting.":
    failure==F::Permission?L"Discord denied access to the selected channel or capability.":failure==F::InvalidData?L"Discord returned data that could not be safely matched to this reader.":L"Discord refresh is delayed. Useful previous data is retained according to this tile's policy.";}
inline bool ValidLimits(int timeout,std::size_t bytes){return timeout>=100&&timeout<=30000&&bytes>=1024&&bytes<=1048576;}
inline discord_tiles::Capabilities BasicCapabilities(const ReaderOptions& options){discord_tiles::Capabilities caps;
    caps.backend=options.configuration.connection==L"Bot"?discord_tiles::Backend::Bot:options.configuration.connection==L"OAuth"?discord_tiles::Backend::OAuth:discord_tiles::Backend::UserTokenExperimental;
    caps.identity=true;caps.channelMessages=!options.binding.channelId.empty()&&options.configuration.connection!=L"OAuth";caps.messageContent=caps.channelMessages;
    caps.presence=options.configuration.connection==L"UserTokenExperimental"&&options.useGateway;caps.platformPresence=caps.presence;
    caps.canSend=caps.channelMessages&&options.allowReviewedSend;caps.enforceNonce=caps.canSend;return discord_tiles::HonestCapabilities(caps);
}
inline bool Prepare(const ReaderOptions& source,ReaderOptions& result,std::wstring& error){
    if(!source.enabled){error=L"Discord account reading is disabled.";return false;}
    if(!Connection(source.configuration.connection)){error=L"This Discord reader connection is not implemented. Keep IPC for presence or select a supported reader connection.";return false;}
    discord_tiles::Reducer candidate;if(!candidate.Reset(source.binding,source.configuration,error))return false;
    if(source.configuration.view==L"Channel"&&source.binding.channelId.empty()){error=L"Select the channel for this Discord reader.";return false;}
    if(!discord_tiles::ExperimentalAcknowledged(source.configuration,source.binding,source.acknowledgement?&*source.acknowledgement:nullptr)){error=discord_tiles::ExperimentalWarning;return false;}
    if(!ValidLimits(source.operationTimeoutMs,source.maximumBytes)||source.refreshMs<1000||source.refreshMs>86400000||source.reconnectMinimumMs<1000||source.reconnectMaximumMs<source.reconnectMinimumMs||source.reconnectMaximumMs>3600000){error=L"Discord reader timing or size limits are invalid.";return false;}
    result=source;
    // Configuration is the validated CLI/INI/UI source of truth. A caller may
    // further disable Gateway/sending, but cannot override a disabled setting.
    result.refreshMs=source.configuration.refreshMs;result.operationTimeoutMs=source.configuration.operationTimeoutMs;
    result.reconnectMinimumMs=source.configuration.reconnectMinimumMs;result.reconnectMaximumMs=source.configuration.reconnectMaximumMs;
    result.maximumBytes=static_cast<std::size_t>(source.configuration.maximumBytes);result.useGateway=source.useGateway&&source.configuration.useGateway;
    result.allowReviewedSend=source.allowReviewedSend&&source.configuration.allowReviewedSend;
    return ProfileCopy(source.profilePath,source.profileOverride,result.profileOverride,error);
}
class NativeGateway final:public ReaderGateway {
    std::unique_ptr<DiscordGatewayClient> client_;std::wstring token_,clientId_;GatewayOptions options_;int timeoutMs_;CancellationRelay deadline_;
public:
    NativeGateway(std::wstring token,std::wstring clientId,GatewayOptions options,int timeoutMs):token_(std::move(token)),clientId_(std::move(clientId)),options_(std::move(options)),timeoutMs_(timeoutMs){
        options_.connectTimeoutMs=(std::min)(options_.connectTimeoutMs,timeoutMs_);options_.sendTimeoutMs=(std::min)(options_.sendTimeoutMs,timeoutMs_);options_.closeTimeoutMs=(std::min)(options_.closeTimeoutMs,timeoutMs_);}
    ~NativeGateway()override{Close();Wipe(token_);}
    void Connect(const ReaderDispatch& dispatch,const Cancel& cancel)override{
        const auto until=Clock::now()+std::chrono::milliseconds(timeoutMs_);auto operation=deadline_.Begin([until]{return Clock::now()>=until;});
        if(!client_){const auto deadline=deadline_.Callback();client_.reset(new DiscordGatewayClient(token_,clientId_,false,options_,[cancel,deadline]{return Canceled(cancel)||Canceled(deadline);},true,dispatch));}
        client_->Connect("{}",L"online");
    }
    bool IsConnected()const override{return client_&&client_->IsConnected();}
    discord_tiles::Failure LastFailure()const override{return client_&&client_->AuthenticationFailed()?discord_tiles::Failure::Authentication:discord_tiles::Failure::Disconnected;}
    void Close()override{if(client_)client_->Close();}
};
} // account_detail
ReaderIdentityProbeResult ProbeReaderIdentity(const ReaderIdentityProbeOptions& options){
    using namespace account_detail;ReaderIdentityProbeResult result;
    if(!options.enabled||!Connection(options.connection)||options.profileKey.empty()||!discord_tiles::SafeSingleLine(options.profileKey,2048)||!ValidLimits(options.operationTimeoutMs,options.maximumBytes)){
        result.failure=discord_tiles::Failure::InvalidData;result.status=L"The explicit Discord identity probe is not configured.";return result;}
    if(options.connection==L"UserTokenExperimental"&&(!options.experimentalWarningReviewed||options.warningProfileKey!=options.profileKey)){result.status=discord_tiles::ExperimentalWarning;result.failure=discord_tiles::Failure::Permission;return result;}
    std::shared_ptr<const Profile> profile;if(!ProfileCopy(options.profilePath,options.profileOverride,profile,result.status)){result.failure=discord_tiles::Failure::InvalidData;return result;}
    std::wstring token;if(!Token(*profile,options.connection,options.credentialDecoder,token)){result.failure=discord_tiles::Failure::Authentication;result.status=L"The selected Discord credential could not be decoded.";return result;}
    const auto deadline=Clock::now()+std::chrono::milliseconds(options.operationTimeoutMs);const auto cancel=[&]{return Canceled(options.canceled)||Clock::now()>=deadline;};
    auto reply=Request(options.http,token,options.connection,L"/users/@me",options.operationTimeoutMs,options.maximumBytes,cancel);Wipe(token);
    result.retryAfterMs=reply.retryAfter;if(!reply.ok){result.failure=reply.failure;result.status=FailureText(reply.failure);return result;}
    result.ok=DecodeReaderIdentity(reply.body,result.user);Wipe(reply.body);result.failure=result.ok?discord_tiles::Failure::None:discord_tiles::Failure::InvalidData;
    result.status=result.ok?L"Discord account identity verified. Review this account before enabling its reader.":FailureText(result.failure);return result;
}
struct AccountReader::Impl {
    struct State {
        mutable std::mutex mutex;std::mutex network;std::condition_variable changed;ReaderOptions options;discord_tiles::Reducer reducer;discord_tiles::SendLedger sends;
        ReaderDecodeState decoder;std::uint64_t receipt=0,revision=0,optionsGeneration=1;std::atomic<bool> stop{false};bool done=false,running=true,connected=false,terminal=false;std::wstring status;account_detail::Clock::time_point requestsAfter{};
    };
    mutable std::mutex lifecycle,pointer;std::shared_ptr<State> state;std::thread worker;
    static void Failure(const std::shared_ptr<State>& state,discord_tiles::Failure failure,std::uint64_t generation=0){std::lock_guard<std::mutex> lock(state->mutex);if((generation&&generation!=state->optionsGeneration)||state->stop)return;discord_tiles::Event event;
        event.binding=state->options.binding;event.sequence=++state->receipt;event.observedAt=account_detail::Now();event.kind=discord_tiles::EventKind::Failure;event.failure=failure;state->reducer.Apply(event);
        state->status=account_detail::FailureText(failure);state->connected=false;state->terminal=failure==discord_tiles::Failure::Authentication||failure==discord_tiles::Failure::Permission;++state->revision;}
    static void Dispatch(const std::shared_ptr<State>& state,std::uint64_t generation,const std::string& json){std::lock_guard<std::mutex> lock(state->mutex);
        if(state->stop||generation!=state->optionsGeneration)return;std::vector<discord_tiles::Event> events;std::wstring error;auto sequence=state->receipt;auto decoder=state->decoder;
        if(!DecodeReaderDispatch(json,state->options.binding,state->options.configuration,state->reducer.Read().capabilities,decoder,sequence,account_detail::Now(),events,error)){
            discord_tiles::Event failure;failure.binding=state->options.binding;failure.sequence=++state->receipt;failure.kind=discord_tiles::EventKind::Failure;failure.failure=discord_tiles::Failure::InvalidData;state->reducer.Apply(failure);state->status=L"A Discord event could not be safely matched to this reader.";++state->revision;return;}
        state->receipt=sequence;state->decoder=decoder;for(const auto& event:events)state->reducer.Apply(event);++state->revision;
    }
    static void Run(std::shared_ptr<State> state)noexcept{
        std::unique_ptr<ReaderGateway> gateway;ReaderOptions options;std::wstring token;std::uint64_t generation=0;unsigned failures=0;bool initial=true;auto next=account_detail::Clock::now();
        try{for(;;){
            const bool disconnected=gateway&&!gateway->IsConnected();
            const auto disconnectFailure=disconnected?gateway->LastFailure():discord_tiles::Failure::None;
            {std::unique_lock<std::mutex> lock(state->mutex);if(state->stop)break;
                if(generation!=state->optionsGeneration){lock.unlock();if(gateway)gateway->Close();gateway.reset();account_detail::Wipe(token);lock.lock();options=state->options;generation=state->optionsGeneration;
                    state->reducer.Reset(options.binding,options.configuration,state->status);state->decoder={};state->receipt=0;state->terminal=false;state->connected=false;initial=true;failures=0;next=account_detail::Clock::now();
                    discord_tiles::Event cap;cap.kind=discord_tiles::EventKind::Capabilities;cap.binding=options.binding;cap.sequence=++state->receipt;cap.capabilities=account_detail::BasicCapabilities(options);state->reducer.Apply(cap);++state->revision;
                }
                if(disconnected&&state->connected){discord_tiles::Event gap;gap.kind=disconnectFailure==discord_tiles::Failure::Authentication?discord_tiles::EventKind::Failure:discord_tiles::EventKind::Gap;
                    gap.failure=disconnectFailure;gap.binding=options.binding;gap.sequence=++state->receipt;state->reducer.Apply(gap);state->connected=false;next=account_detail::Clock::now();
                    if(disconnectFailure==discord_tiles::Failure::Authentication){state->terminal=true;state->status=account_detail::FailureText(disconnectFailure);}++state->revision;}
                if(state->terminal){lock.unlock();if(gateway)gateway->Close();gateway.reset();account_detail::Wipe(token);lock.lock();
                    state->changed.wait_for(lock,std::chrono::milliseconds(200),[&]{return state->stop||state->optionsGeneration!=generation;});continue;}
                if(account_detail::Clock::now()<(std::max)(next,state->requestsAfter)){state->changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return state->stop||state->optionsGeneration!=generation;});continue;}
            }
            if(token.empty()&&!account_detail::Token(*options.profileOverride,options.configuration.connection,options.credentialDecoder,token)){Failure(state,discord_tiles::Failure::Authentication,generation);continue;}
            const auto cancel=[state,generation]{if(state->stop)return true;std::lock_guard<std::mutex> lock(state->mutex);return generation!=state->optionsGeneration;};
            const auto deadline=account_detail::Clock::now()+std::chrono::milliseconds(options.operationTimeoutMs);const auto operationCancel=[&]{return cancel()||account_detail::Clock::now()>=deadline;};
            bool ok=true;unsigned retry=0;
            {std::lock_guard<std::mutex> network(state->network);
                auto identity=account_detail::Request(options.http,token,options.configuration.connection,L"/users/@me",options.operationTimeoutMs,options.maximumBytes,operationCancel);discord_tiles::UserPatch user;
                if(!identity.ok||!DecodeReaderIdentity(identity.body,user)||user.id!=options.binding.accountId){const auto failure=identity.ok?discord_tiles::Failure::Authentication:identity.failure;retry=identity.retryAfter;account_detail::Wipe(identity.body);Failure(state,failure,generation);ok=false;}
                else{account_detail::Wipe(identity.body);std::lock_guard<std::mutex> lock(state->mutex);if(generation==state->optionsGeneration&&!state->stop){discord_tiles::Event e;e.kind=discord_tiles::EventKind::Identity;e.binding=options.binding;e.sequence=++state->receipt;e.user=user;state->reducer.Apply(e);}}
                if(ok&&!options.binding.channelId.empty()&&options.configuration.connection!=L"OAuth"){
                    std::vector<discord_tiles::Message> previousMessages;std::uint64_t historyBefore=0;
                    {std::lock_guard<std::mutex> lock(state->mutex);if(generation!=state->optionsGeneration||state->stop)continue;historyBefore=state->receipt;previousMessages=state->reducer.Read().messages;}
                    auto history=account_detail::Request(options.http,token,options.configuration.connection,L"/channels/"+options.binding.channelId+L"/messages?limit="+std::to_wstring(options.configuration.messageLimit),options.operationTimeoutMs,options.maximumBytes,operationCancel);
                    std::lock_guard<std::mutex> lock(state->mutex);std::vector<discord_tiles::Event> events;auto sequence=state->receipt;std::wstring error;
                    if(generation!=state->optionsGeneration||state->stop){account_detail::Wipe(history.body);continue;}
                    if(!history.ok||!DecodeReaderHistory(history.body,options.binding,options.configuration,initial?discord_tiles::Origin::InitialHistory:discord_tiles::Origin::RecoveryHistory,sequence,account_detail::Now(),events,error)){
                        retry=history.retryAfter;discord_tiles::Event e;e.binding=options.binding;e.sequence=++state->receipt;e.kind=discord_tiles::EventKind::Failure;e.failure=history.ok?discord_tiles::Failure::InvalidData:history.failure;state->reducer.Apply(e);state->status=account_detail::FailureText(e.failure);state->terminal=e.failure==discord_tiles::Failure::Authentication||e.failure==discord_tiles::Failure::Permission;ok=false;
                    }else{
                        // A REST page can prove a previously cached message was
                        // deleted only inside its covered range. Do not use an
                        // older page to remove anything if live events arrived
                        // while that page was in flight.
                        if(!initial&&state->receipt==historyBefore){std::set<std::wstring> returned;std::uint64_t oldest=(std::numeric_limits<std::uint64_t>::max)();
                            const bool complete=!events.empty()&&events.back().kind==discord_tiles::EventKind::SyncComplete&&events.back().complete;
                            for(const auto& e:events)if(e.kind==discord_tiles::EventKind::MessageCreate){returned.insert(e.message.id);std::uint64_t id=0;discord_tiles::Snowflake(e.message.id,id);oldest=(std::min)(oldest,id);}
                            for(const auto& message:previousMessages){std::uint64_t id=0;discord_tiles::Snowflake(message.id,id);if(!returned.count(message.id)&&(complete||(!returned.empty()&&id>=oldest))){discord_tiles::Event removed;
                                removed.binding=options.binding;removed.sequence=++sequence;removed.observedAt=account_detail::Now();removed.kind=discord_tiles::EventKind::MessageDelete;
                                removed.origin=discord_tiles::Origin::RecoveryHistory;removed.messageId=message.id;removed.message.channelId=options.binding.channelId;events.push_back(std::move(removed));}}}
                        state->receipt=sequence;for(const auto& e:events)state->reducer.Apply(e);
                    }account_detail::Wipe(history.body);
                }else if(ok){std::lock_guard<std::mutex> lock(state->mutex);if(generation==state->optionsGeneration&&!state->stop){discord_tiles::Event e;e.binding=options.binding;e.sequence=++state->receipt;e.observedAt=account_detail::Now();e.kind=discord_tiles::EventKind::SyncComplete;state->reducer.Apply(e);}}
            }
            if(cancel())continue;
            if(ok&&options.configuration.connection==L"UserTokenExperimental"&&options.useGateway){
                if(!gateway){
                    if(options.gatewayFactory)gateway=options.gatewayFactory();
                    else{Options environment;environment.profileOverride=options.profileOverride;
                        struct Scope{const Options* saved;explicit Scope(const Options* value):saved(g_embeddedOptions){g_embeddedOptions=value;}~Scope(){g_embeddedOptions=saved;}} scoped(&environment);
                        const auto config=LoadGatewayOptions();const auto clientId=account_detail::Value(*options.profileOverride,L"general",L"client_id");
                        gateway.reset(new account_detail::NativeGateway(token,clientId,config,options.operationTimeoutMs));}
                }
                try{if(!gateway)throw std::runtime_error("No Gateway reader");if(!gateway->IsConnected()){gateway->Connect([state,generation](const std::string& event){Dispatch(state,generation,event);},cancel);}if(!gateway->IsConnected()){Failure(state,gateway->LastFailure(),generation);ok=false;}}
                catch(...){Failure(state,gateway?gateway->LastFailure():discord_tiles::Failure::Disconnected,generation);ok=false;}
            }
            {std::lock_guard<std::mutex> lock(state->mutex);if(generation!=state->optionsGeneration||state->stop)continue;state->connected=ok;
                if(ok){state->status=L"Discord reader refreshed; background reading never acknowledges or sends messages.";initial=false;failures=0;next=account_detail::Clock::now()+std::chrono::milliseconds(options.refreshMs);}
                else{failures=(std::min)(failures+1,20u);const auto shifted=static_cast<std::uint64_t>(options.reconnectMinimumMs)<<(std::min)(failures-1,16u);const auto delay=(std::max)(static_cast<std::uint64_t>(retry),(std::min)(shifted,static_cast<std::uint64_t>(options.reconnectMaximumMs)));next=account_detail::Clock::now()+std::chrono::milliseconds(delay);if(retry)state->requestsAfter=(std::max)(state->requestsAfter,account_detail::Clock::now()+std::chrono::milliseconds(retry));}
                ++state->revision;
            }
        }}catch(...){Failure(state,discord_tiles::Failure::Transient,generation);}
        if(gateway)gateway->Close();account_detail::Wipe(token);{std::lock_guard<std::mutex> lock(state->mutex);state->running=false;state->connected=false;state->done=true;++state->revision;}state->changed.notify_all();
    }
};
AccountReader::AccountReader():impl_(new Impl()){}
AccountReader::~AccountReader(){if(!Stop(5000)){std::lock_guard<std::mutex> lock(impl_->lifecycle);if(impl_->worker.joinable())impl_->worker.detach();}}
bool AccountReader::Start(const ReaderOptions& options,std::wstring& error){ReaderOptions prepared;if(!account_detail::Prepare(options,prepared,error))return false;std::lock_guard<std::mutex> lock(impl_->lifecycle);
    if(impl_->worker.joinable()){error=L"The Discord reader is already running; reload its explicit options.";return false;}auto state=std::make_shared<Impl::State>();state->options=std::move(prepared);
    if(!state->reducer.Reset(state->options.binding,state->options.configuration,error))return false;{std::lock_guard<std::mutex> pointer(impl_->pointer);impl_->state=state;}
    try{impl_->worker=std::thread(&Impl::Run,state);}catch(...){std::lock_guard<std::mutex> pointer(impl_->pointer);impl_->state.reset();error=L"The Discord reader worker could not start.";return false;}error.clear();return true;
}
bool AccountReader::Reload(const ReaderOptions& options,std::wstring& error){ReaderOptions prepared;if(!account_detail::Prepare(options,prepared,error))return false;std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);std::shared_ptr<Impl::State> state;
    {std::lock_guard<std::mutex> pointer(impl_->pointer);state=impl_->state;}if(!state||state->stop){error=L"The Discord reader is not running.";return false;}
    {std::lock_guard<std::mutex> lock(state->mutex);if(state->done||!state->running){error=L"The Discord reader stopped; start a new reader after Stop completes.";return false;}
        const bool otherAccount=state->options.binding.profileKey!=prepared.binding.profileKey||state->options.binding.accountId!=prepared.binding.accountId||state->options.configuration.connection!=prepared.configuration.connection;
        state->options=std::move(prepared);++state->optionsGeneration;state->connected=false;if(otherAccount)state->requestsAfter={};
        state->reducer.Reset(state->options.binding,state->options.configuration,error);state->decoder={};state->receipt=0;state->sends={};++state->revision;}state->changed.notify_all();error.clear();return true;
}
bool AccountReader::Stop(unsigned timeoutMs){std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);if(!impl_->worker.joinable())return true;std::shared_ptr<Impl::State> state;
    {std::lock_guard<std::mutex> pointer(impl_->pointer);state=impl_->state;}state->stop=true;state->changed.notify_all();std::unique_lock<std::mutex> lock(state->mutex);
    if(!state->done&&!state->changed.wait_for(lock,std::chrono::milliseconds((std::min)(timeoutMs,30000u)),[&]{return state->done;}))return false;lock.unlock();impl_->worker.join();return true;
}
ReaderSnapshot AccountReader::Read()const{std::shared_ptr<Impl::State> state;{std::lock_guard<std::mutex> pointer(impl_->pointer);state=impl_->state;}if(!state)return {};std::lock_guard<std::mutex> lock(state->mutex);
    ReaderSnapshot snapshot;snapshot.running=state->running;snapshot.connected=state->connected;snapshot.revision=state->revision;snapshot.data=state->reducer.Read();snapshot.status=state->status;return snapshot;
}
ReaderSendResult AccountReader::SendReviewed(const discord_tiles::ComposeIntent& intent){ReaderSendResult result;std::shared_ptr<Impl::State> state;{std::lock_guard<std::mutex> pointer(impl_->pointer);state=impl_->state;}
    if(!state){result.status=L"Start the selected Discord account reader before sending.";return result;}std::lock_guard<std::mutex> network(state->network);ReaderOptions options;std::uint64_t generation=0;discord_tiles::SendRequest send;
    {std::lock_guard<std::mutex> lock(state->mutex);options=state->options;generation=state->optionsGeneration;if(state->stop||!state->connected||!discord_tiles::AuthorizeSend(intent,state->reducer.Read(),options.configuration,state->sends,send,result.status,options.acknowledgement?&*options.acknowledgement:nullptr))return result;
        const auto now=account_detail::Clock::now();if(now<state->requestsAfter){result.state=ReaderSendState::RateLimited;result.retryAfterMs=static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(state->requestsAfter-now).count()+1);result.status=L"Discord's retry interval has not elapsed; no request was sent.";state->sends.Complete(send.binding,send.activation,discord_tiles::SendOutcome::Rejected);return result;}}
    std::wstring token;if(!account_detail::Token(*options.profileOverride,options.configuration.connection,options.credentialDecoder,token)){result.status=L"The selected Discord credential could not be decoded.";std::lock_guard<std::mutex> lock(state->mutex);state->sends.Complete(send.binding,send.activation,discord_tiles::SendOutcome::Rejected);return result;}
    const auto deadline=account_detail::Clock::now()+std::chrono::milliseconds(options.operationTimeoutMs);const auto cancel=[state,generation,deadline]{if(state->stop||account_detail::Clock::now()>=deadline)return true;std::lock_guard<std::mutex> lock(state->mutex);return generation!=state->optionsGeneration;};
    std::string mentions="{\"parse\":[";bool first=true;for(const auto& pair:std::vector<std::pair<bool,const wchar_t*>>{{send.allowUserMentions,L"users"},{send.allowRoleMentions,L"roles"},{send.allowEveryoneMentions,L"everyone"}})if(pair.first){if(!first)mentions+=',';first=false;mentions+=aip::JsonEscape(pair.second);}mentions+="]}";
    std::string body="{\"content\":"+aip::JsonEscape(send.text)+",\"nonce\":"+aip::JsonEscape(send.nonce)+",\"allowed_mentions\":"+mentions;
    if(send.enforceNonce)body+=",\"enforce_nonce\":true";body+='}';
    auto reply=account_detail::Request(options.http,token,options.configuration.connection,L"/channels/"+send.binding.channelId+L"/messages",options.operationTimeoutMs,options.maximumBytes,cancel,body);account_detail::Wipe(token);account_detail::Wipe(body);
    auto outcome=discord_tiles::SendOutcome::Unknown;
    if(reply.ok){reader_detail::Object message,author;std::wstring id,channel,authorId;
        if(reader_detail::ObjectValue(reply.body,message)&&reader_detail::Identifier(message,"id",id)&&reader_detail::Identifier(message,"channel_id",channel)&&channel==send.binding.channelId&&
            reader_detail::Raw(message,"author")&&reader_detail::ObjectValue(*reader_detail::Raw(message,"author"),author)&&reader_detail::Identifier(author,"id",authorId)&&authorId==send.binding.accountId){
            result.state=ReaderSendState::Sent;result.messageId=id;result.status=L"Discord confirmed the selected sender and channel for this message.";outcome=discord_tiles::SendOutcome::Sent;
        }else{result.state=ReaderSendState::Unknown;result.status=L"Discord returned an ambiguous send result. Check the channel; no automatic retry will occur.";}}
    else if(reply.code==429){result.state=ReaderSendState::RateLimited;result.retryAfterMs=reply.retryAfter;result.status=L"Discord rate-limited this send. Review a new activation after the retry interval.";outcome=discord_tiles::SendOutcome::Rejected;}
    else if(reply.code>=400&&reply.code<500){result.state=ReaderSendState::Rejected;result.status=account_detail::FailureText(reply.failure);outcome=discord_tiles::SendOutcome::Rejected;}
    else{result.state=ReaderSendState::Unknown;result.status=L"Message delivery is unknown. Check the channel before composing another message; this activation will not retry.";}
    account_detail::Wipe(reply.body);{std::lock_guard<std::mutex> lock(state->mutex);state->sends.Complete(send.binding,send.activation,outcome);if(result.retryAfterMs)state->requestsAfter=(std::max)(state->requestsAfter,account_detail::Clock::now()+std::chrono::milliseconds(result.retryAfterMs));++state->revision;}return result;
}
} }

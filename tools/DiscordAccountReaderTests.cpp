#include "../dependencies/DiscordRPC/account_reader.h"
#include "../dependencies/DiscordRPC/gateway_identify.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace {
using namespace aip::discord;namespace model=aip::discord_tiles;
std::atomic<unsigned> checks{0};
void Check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
const std::wstring self=L"123456789012345678",other=L"223456789012345678",channel=L"323456789012345678",guild=L"423456789012345678";
const std::string token="SYNTHETIC-ACCOUNT-READER-TOKEN";
std::string User(const std::wstring& id=self){std::string ascii;for(auto c:id)ascii+=static_cast<char>(c);return "{\"id\":\""+ascii+"\",\"username\":\"fixture-user\",\"global_name\":\"Fixture name\",\"avatar\":\"0123456789abcdef0123456789abcdef\",\"email\":\"NEVER-EXPORTED@example.invalid\"}";}
std::string Message(const char* id="623456789012345678",const char* content="seed"){return std::string("{\"id\":\"")+id+"\",\"channel_id\":\"323456789012345678\",\"author\":"+User(other)+",\"content\":\""+content+"\",\"timestamp\":\"2026-10-28T10:11:12.123456789+00:00\",\"mentions\":[{\"id\":\"123456789012345678\"}],\"mention_everyone\":false}";}
std::string Envelope(const char* event,unsigned sequence,const std::string& data){return std::string("{\"op\":0,\"t\":\"")+event+"\",\"s\":"+std::to_string(sequence)+",\"d\":"+data+"}";}
model::Binding Binding(){return {L"PROFILE-FIXTURE",self,guild,channel,1,1};}
model::Configuration Configuration(){model::Configuration c;c.view=L"Channel";c.connection=L"UserTokenExperimental";c.accountId=self;c.channelId=channel;c.guildId=guild;
    c.experimentalAcknowledged=true;c.acknowledgedProfileKey=L"PROFILE-FIXTURE";c.acknowledgedAccountId=self;c.allowReviewedSend=true;return c;}
std::shared_ptr<const Profile> ProfileFixture(){auto p=std::make_shared<Profile>();p->values[{L"general",L"token_protected"}]=L"SYNTHETIC-PROTECTED";return p;}
ReaderOptions Options(){ReaderOptions options;options.enabled=true;options.binding=Binding();options.configuration=Configuration();options.profileOverride=ProfileFixture();
    options.credentialDecoder=[](const std::wstring&){return std::wstring(token.begin(),token.end());};options.refreshMs=30000;options.operationTimeoutMs=500;options.allowReviewedSend=true;return options;}
std::string Ready(){return "{\"user\":"+User()+",\"session_id\":\"fixture-session\",\"relationships\":[{\"id\":\"223456789012345678\",\"type\":1,\"user\":"+User(other)+"}],\"merged_presences\":{\"friends\":[{\"user_id\":\"223456789012345678\",\"status\":\"online\",\"client_status\":{\"mobile\":\"online\"}}]},\"read_state\":{\"entries\":[{\"id\":\"323456789012345678\",\"last_message_id\":\"623456789012345678\",\"mention_count\":2}]}}";}
struct Traffic {std::mutex mutex;ReaderDispatch observer;std::atomic<unsigned> requests{0},posts{0},connected{0},closed{0};std::atomic<bool> socket{false};
    std::atomic<int> response{0};std::atomic<bool> block{false},blockHistory{false},historyWaiting{false};std::atomic<model::Failure> gatewayFailure{model::Failure::Disconnected};std::vector<std::wstring> paths;std::string posted;};
class FakeGateway final:public ReaderGateway {std::shared_ptr<Traffic> traffic_;
public:explicit FakeGateway(std::shared_ptr<Traffic> traffic):traffic_(std::move(traffic)){}
    void Connect(const ReaderDispatch& observer,const Cancel& cancel)override{if(cancel())throw std::runtime_error("Canceled");{std::lock_guard<std::mutex> lock(traffic_->mutex);traffic_->observer=observer;}traffic_->socket=true;++traffic_->connected;observer(Envelope("READY",1,Ready()));}
    bool IsConnected()const override{return traffic_->socket;}
    model::Failure LastFailure()const override{return traffic_->gatewayFailure;}
    void Close()override{traffic_->socket=false;++traffic_->closed;}
};
ReaderHttpTransport Http(std::shared_ptr<Traffic> traffic){return [traffic](const ReaderHttpRequest& request,ReaderHttpResponse& response,std::wstring& error){
    ++traffic->requests;Check(request.url.find(L"https://discord.com/api/v10/")==0,"API request stays on the fixed Discord HTTPS origin");
    Check(request.headers.size()>=1&&request.headers[0].first==L"Authorization"&&request.headers[0].second==std::wstring(token.begin(),token.end()),"personal credential is attached only to the API request");
    {std::lock_guard<std::mutex> lock(traffic->mutex);traffic->paths.push_back(request.url);}
    while(traffic->block&&!request.canceled())std::this_thread::sleep_for(std::chrono::milliseconds(1));if(request.canceled())return false;
    if(request.method==L"POST"){++traffic->posts;{std::lock_guard<std::mutex> lock(traffic->mutex);traffic->posted=request.body;}
        if(traffic->response==1){response.statusCode=429;response.body="{\"retry_after\":0.125,\"global\":false}";return true;}
        if(traffic->response==2){error=L"PRIVATE-SYNTHETIC-ACCOUNT-READER-TOKEN";return false;}
        response.statusCode=200;response.body="{\"id\":\"723456789012345678\",\"channel_id\":\"323456789012345678\",\"author\":"+User()+"}";return true;
    }
    Check(request.method==L"GET"&&request.body.empty(),"reader refresh never posts or sends a read acknowledgement");response.statusCode=200;
    if(request.url.find(L"/channels/")!=std::wstring::npos&&traffic->blockHistory){traffic->historyWaiting=true;while(traffic->blockHistory&&!request.canceled())std::this_thread::sleep_for(std::chrono::milliseconds(1));if(request.canceled())return false;}
    response.body=request.url.find(L"/users/@me")!=std::wstring::npos?User():traffic->response==3?"[]":"["+Message()+"]";return true;
};}
template<class Predicate>ReaderSnapshot Wait(AccountReader& reader,Predicate done){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{const auto snapshot=reader.Read();if(done(snapshot))return snapshot;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);throw std::runtime_error("Reader fixture exceeded three-second wait.");}
void Decoder(){
    const auto readerIdentify=BuildGatewayIdentifyBody(L"SYNTHETIC","{\"os\":\"fixture\"}","{}",65,"{\"activities\":[{}]}",true);
    Check(readerIdentify.find("\"presence\"")==std::string::npos&&readerIdentify.find("activities")==std::string::npos,"actual Gateway reader IDENTIFY serializer cannot include initial presence or activities");
    const auto presenceIdentify=BuildGatewayIdentifyBody(L"SYNTHETIC","{\"os\":\"fixture\"}","{}",65,"{\"activities\":[{}]}",false);
    Check(presenceIdentify.find("\"presence\":{\"activities\":[{}]}")!=std::string::npos,"legacy presence mode retains its explicit initial presence payload");
    auto cfg=Configuration();auto binding=Binding();model::Capabilities caps;caps.backend=model::Backend::UserTokenExperimental;caps.identity=caps.channelMessages=caps.messageContent=caps.presence=caps.platformPresence=true;
    std::uint64_t sequence=0;ReaderDecodeState state;std::vector<model::Event> events;std::wstring error;model::Reducer reducer;Check(reducer.Reset(binding,cfg,error),"decoder model binding validates");
    Check(DecodeReaderDispatch(Envelope("READY",1,Ready()),binding,cfg,caps,state,sequence,1000,events,error),"READY projects bounded identity, relationships, visible platforms and selected read state");
    for(const auto& event:events)Check(reducer.Apply(event)!=model::ApplyResult::Invalid,"projected READY event matches the account-bound reducer");
    auto snapshot=reducer.Read();Check(snapshot.user.username==L"fixture-user"&&snapshot.friends.size()==1&&snapshot.friends.front().presence.platforms->mobile==model::Status::Online,"profile projection retains friend identity and visible mobile presence");
    Check(snapshot.personalMentions&&snapshot.personalMentions->value==2&&!snapshot.personalUnread,"supplied personal mention count remains distinct from missing unread count");
    Check(DecodeReaderHistory("["+Message()+"]",binding,cfg,model::Origin::InitialHistory,sequence,1100,events,error),"bounded initial history decodes");for(const auto& event:events)reducer.Apply(event);
    Check(reducer.Read().localNewMessages&&reducer.Read().localNewMessages->value==0,"historical messages establish a baseline without becoming new unread messages");
    Check(DecodeReaderDispatch(Envelope("MESSAGE_CREATE",2,Message("623456789012345679","new")),binding,cfg,caps,state,sequence,1200,events,error),"new message decodes with exact large snowflakes");for(const auto& event:events)reducer.Apply(event);
    snapshot=reducer.Read();Check(snapshot.messages.front().id==L"623456789012345679"&&snapshot.localNewMessages->value==1,"live message arrives once after the initial baseline");
    const auto timestamp=snapshot.messages.front().sentAt;Check(timestamp&&timestamp->nanoseconds==123456789,"RFC3339 fractional timestamps retain nanosecond precision");
    const auto before=sequence;Check(DecodeReaderDispatch(Envelope("MESSAGE_CREATE",2,Message()),binding,cfg,caps,state,sequence,1250,events,error)&&events.empty()&&sequence==before,"replayed wire events do not increment receipt state");
    Check(DecodeReaderDispatch(Envelope("MESSAGE_UPDATE",3,"{\"id\":\"623456789012345679\",\"channel_id\":\"323456789012345678\",\"content\":\"edited\",\"edited_timestamp\":\"2026-10-28T10:12:00Z\"}"),binding,cfg,caps,state,sequence,1300,events,error),"partial message update decodes without requiring omitted author fields");for(const auto& event:events)reducer.Apply(event);
    Check(reducer.Read().messages.front().content==L"edited"&&reducer.Read().messages.front().authorId==other,"partial edits keep existing author data");
    Check(DecodeReaderDispatch(Envelope("SESSIONS_REPLACE",4,"[]"),binding,cfg,caps,state,sequence,1350,events,error)&&events.empty(),"unknown array dispatches are ignored rather than treated as security-session inventory");
    Check(DecodeReaderDispatch(Envelope("MESSAGE_DELETE",6,"{\"id\":\"623456789012345679\",\"channel_id\":\"323456789012345678\"}"),binding,cfg,caps,state,sequence,1400,events,error),"delete after a wire gap decodes");for(const auto& event:events)reducer.Apply(event);
    Check(reducer.Read().messages.size()==1&&!reducer.Read().localNewMessages->exact,"gap/deletion state does not claim a complete personal inbox");
    Check(!DecodeReaderDispatch("{\"op\":0,\"op\":0,\"s\":7,\"t\":\"READY\",\"d\":"+Ready()+"}",binding,cfg,caps,state,sequence,1500,events,error),"ambiguous duplicate envelope keys are rejected");
    const auto retained=state.wireSequence;Check(!DecodeReaderDispatch(Envelope("READY",1,"{\"user\":"+User(other)+"}"),binding,cfg,caps,state,sequence,1600,events,error)&&state.wireSequence==retained,"wrong-account READY cannot reset the current wire context");
    Check(!DecodeReaderHistory("["+Message()+",]",binding,cfg,model::Origin::InitialHistory,sequence,0,events,error),"malformed history arrays are rejected before mutation");
    model::UserPatch identity;Check(DecodeReaderIdentity(User(),identity)&&identity.username==L"fixture-user"&&!identity.avatarHash->empty(),"REST identity projects only the bounded public profile fields");
    Check(!DecodeReaderIdentity("{\"id\":9007199254740993,\"username\":\"x\"}",identity),"numeric IDs are not converted through floating point");
    Check(DecodeReaderDispatch(Envelope("READ_STATE_UPDATE",7,"{\"id\":\"323456789012345678\",\"last_message_id\":null,\"mention_count\":0}"),binding,cfg,caps,state,sequence,1700,events,error)&&events.size()==1&&!events.front().readState.lastReadMessageId,
        "a null read cursor remains unknown instead of rejecting otherwise valid personal read state");
}
void Probes(){
    ReaderIdentityProbeOptions options;options.profileKey=L"PROFILE-FIXTURE";options.warningProfileKey=options.profileKey;options.connection=L"UserTokenExperimental";options.profileOverride=ProfileFixture();
    std::atomic<unsigned> decodes{0},requests{0};options.credentialDecoder=[&](const std::wstring&){++decodes;return std::wstring(token.begin(),token.end());};
    options.http=[&](const auto& request,auto& response,auto&){++requests;Check(request.method==L"GET"&&request.url==L"https://discord.com/api/v10/users/@me"&&request.body.empty(),"identity probe has one fixed read-only endpoint");response.statusCode=200;response.body=User();return true;};
    Check(!ProbeReaderIdentity(options).ok&&decodes==0&&requests==0,"disabled probe never resolves credentials or uses a transport");options.enabled=true;
    Check(!ProbeReaderIdentity(options).ok&&decodes==0&&requests==0,"personal identity probe requires the profile-scoped warning before credential resolution");
    options.experimentalWarningReviewed=true;options.warningProfileKey=L"DIFFERENT";Check(!ProbeReaderIdentity(options).ok&&decodes==0,"another profile's warning does not authorize a probe");options.warningProfileKey=options.profileKey;
    auto result=ProbeReaderIdentity(options);Check(result.ok&&result.user.id==self&&requests==1,"explicit acknowledged identity probe verifies the account before a full reader binding");
    options.http=[](const auto&,auto&,std::wstring& error){error=L"PRIVATE-SYNTHETIC-ACCOUNT-READER-TOKEN";return false;};result=ProbeReaderIdentity(options);
    Check(!result.ok&&result.status.find(L"SYNTHETIC")==std::wstring::npos,"transport exception/error text cannot disclose credentials through probe status");
    options.credentialDecoder=[](const auto&)->std::wstring{throw std::runtime_error("PRIVATE");};const auto before=requests.load();result=ProbeReaderIdentity(options);Check(!result.ok&&requests==before,"protected-value decode failure never falls back to another credential");
    options.credentialDecoder=[](const auto&){return std::wstring(token.begin(),token.end());};
    options.http=[](const auto&,auto& response,auto&){response.statusCode=429;response.body="{\"retry_after\":3600.5}";return true;};
    result=ProbeReaderIdentity(options);Check(!result.ok&&result.retryAfterMs==3600500,"server retry intervals are not shortened to the configured reconnect maximum");
    options.http=[](const auto&,auto& response,auto&){response.statusCode=429;response.body="{\"retry_after\":0.0001}";return true;};
    Check(ProbeReaderIdentity(options).retryAfterMs==1,"fractional millisecond retry intervals round up instead of retrying early");
    options.connection=L"Bot";options.http=[&](const auto&,auto&,auto&){++requests;return false;};
    Check(!ProbeReaderIdentity(options).ok&&requests==before,"bot identity never reuses a configured personal-account credential");
    auto purposeProfile=std::make_shared<Profile>();purposeProfile->values[{L"reader",L"bot_token_protected"}]=L"SYNTHETIC-BOT";purposeProfile->values[{L"reader",L"oauth_token_protected"}]=L"SYNTHETIC-OAUTH";
    options.profileOverride=purposeProfile;options.http=[&](const auto& request,auto& response,auto&){++requests;const auto expected=options.connection==L"Bot"?L"Bot ":L"Bearer ";
        Check(request.headers[0].second==std::wstring(expected)+std::wstring(token.begin(),token.end()),"credential purpose determines the explicit API authorization scheme");response.statusCode=200;response.body=User();return true;};
    Check(ProbeReaderIdentity(options).ok,"a separately protected bot credential can perform the explicit identity probe");options.connection=L"OAuth";
    Check(ProbeReaderIdentity(options).ok,"a separately protected OAuth credential can perform the explicit identity probe");
}
void LifecycleAndSend(){
    AccountReader reader;std::wstring error;auto options=Options();auto traffic=std::make_shared<Traffic>();options.http=Http(traffic);options.gatewayFactory=[traffic]{return std::unique_ptr<ReaderGateway>(new FakeGateway(traffic));};
    auto disabled=options;disabled.enabled=false;Check(!reader.Start(disabled,error)&&traffic->requests==0,"disabled reader cannot open any transport");
    auto unacknowledged=options;unacknowledged.configuration.experimentalAcknowledged=false;Check(!reader.Start(unacknowledged,error)&&traffic->requests==0,"reader start fails before credentials when the account-bound warning is missing");
    Check(reader.Start(options,error),"explicit synthetic reader starts");auto snapshot=Wait(reader,[](const auto& view){return view.connected&&!view.data.friends.empty();});
    Check(snapshot.data.user.id==self&&snapshot.data.messages.size()==1&&snapshot.data.friends.front().user.id==other,"reader composes account/channel and Gateway projections");
    Check(traffic->posts==0&&traffic->connected==1,"background bootstrap opens a reader without sending a message");
    ReaderDispatch first;{std::lock_guard<std::mutex> lock(traffic->mutex);first=traffic->observer;}
    first(Envelope("MESSAGE_CREATE",2,Message("623456789012345679","new")));snapshot=reader.Read();Check(snapshot.data.localNewMessages->value==1,"live dispatch updates the account-bound message model");
    model::ComposeIntent intent;Check(model::BeginCompose(snapshot.data,options.configuration,L"e",L"fixture-1",1,intent,error),"compose freezes sender/channel/configuration and text");
    Check(reader.SendReviewed(intent).state==ReaderSendState::Rejected&&traffic->posts==0,"an unreviewed compose intent cannot send");
    Check(model::ReviewCompose(intent,snapshot.data,options.configuration,error),"the user-review stage validates the current context");
    auto sent=reader.SendReviewed(intent);Check(sent.state==ReaderSendState::Sent&&sent.messageId==L"723456789012345678"&&traffic->posts==1,"explicit reviewed send confirms the selected sender and channel");
    {std::lock_guard<std::mutex> lock(traffic->mutex);Check(traffic->posted.find("\"allowed_mentions\":{\"parse\":[]}")!=std::string::npos&&traffic->posted.find("fixture-1")!=std::string::npos,"default send suppresses mass mentions and includes the frozen nonce");}
    Check(reader.SendReviewed(intent).state==ReaderSendState::Rejected&&traffic->posts==1,"duplicate activation never repeats a POST");
    traffic->response=1;snapshot=reader.Read();model::ComposeIntent limited;Check(model::BeginCompose(snapshot.data,options.configuration,L"e",L"fixture-2",2,limited,error)&&model::ReviewCompose(limited,snapshot.data,options.configuration,error),"second reviewed activation is independent");
    sent=reader.SendReviewed(limited);Check(sent.state==ReaderSendState::RateLimited&&sent.retryAfterMs==125&&traffic->posts==2,"429 fractional retry-after is preserved without an automatic resend");
    Check(reader.SendReviewed(limited).state==ReaderSendState::Rejected&&traffic->posts==2,"rate-limited activation remains consumed");
    model::ComposeIntent early;Check(model::BeginCompose(snapshot.data,options.configuration,L"e",L"fixture-early",3,early,error)&&model::ReviewCompose(early,snapshot.data,options.configuration,error),"new activation can be reviewed during server backoff");
    Check(reader.SendReviewed(early).state==ReaderSendState::RateLimited&&traffic->posts==2,"server retry interval prevents even another reviewed activation from issuing a POST");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    traffic->response=2;snapshot=reader.Read();model::ComposeIntent ambiguous;Check(model::BeginCompose(snapshot.data,options.configuration,L"e",L"fixture-4",4,ambiguous,error)&&model::ReviewCompose(ambiguous,snapshot.data,options.configuration,error),"next activation reviews after server backoff");
    sent=reader.SendReviewed(ambiguous);Check(sent.state==ReaderSendState::Unknown&&sent.status.find(L"SYNTHETIC")==std::wstring::npos,"unknown POST delivery is sanitized and never silently retried");
    Check(reader.SendReviewed(ambiguous).state==ReaderSendState::Rejected&&traffic->posts==3,"ambiguous activation cannot be replayed");
    auto changed=options;changed.binding.configRevision=2;changed.binding.connectionGeneration=2;Check(reader.Reload(changed,error),"explicit configuration generation reloads");
    first(Envelope("MESSAGE_CREATE",9,Message("823456789012345678","OLD-GENERATION")));
    auto immediate=reader.Read();Check(immediate.data.binding.configRevision==2&&immediate.data.messages.empty(),"reload immediately clears old data and rejects late previous-generation events");
    snapshot=Wait(reader,[](const auto& view){return view.connected&&view.data.binding.configRevision==2;});
    Check(snapshot.data.messages.size()==1&&snapshot.data.messages.front().content==L"seed","new binding contains only its own fresh bootstrap");
    Check(reader.Stop(2000)&&!reader.Read().running,"reader stops within its deadline without clearing presence or sending");
}
void TerminalAndCancel(){
    auto options=Options();options.useGateway=false;std::atomic<unsigned> requests{0};options.http=[&](const auto&,auto& response,auto&){++requests;response.statusCode=200;response.body=User(other);return true;};
    AccountReader reader;std::wstring error;Check(reader.Start(options,error),"wrong-account fixture starts with explicit warning");const auto failed=Wait(reader,[](const auto& view){return view.data.failure==model::Failure::Authentication;});
    Check(failed.data.user.id.empty()&&requests==1,"a credential for another account cannot leak that profile or open a Gateway");std::this_thread::sleep_for(std::chrono::milliseconds(60));Check(requests==1,"authentication rejection remains terminal until explicit reload");Check(reader.Stop(2000),"terminal reader stops");
    options=Options();options.useGateway=false;options.http=[](const auto& request,auto&,auto&){while(!request.canceled())std::this_thread::sleep_for(std::chrono::milliseconds(1));return false;};
    Check(reader.Start(options,error),"cancellable synthetic HTTP starts");Wait(reader,[](const auto& view){return view.running;});Check(reader.Stop(2000),"stop cancels an in-flight reader request rather than waiting for another refresh");
    options=Options();auto traffic=std::make_shared<Traffic>();options.http=Http(traffic);options.gatewayFactory=[traffic]{return std::unique_ptr<ReaderGateway>(new FakeGateway(traffic));};
    Check(reader.Start(options,error),"asynchronous authentication failure fixture starts");Wait(reader,[](const auto& view){return view.connected;});const auto before=traffic->requests.load();
    traffic->gatewayFailure=model::Failure::Authentication;traffic->socket=false;Wait(reader,[](const auto& view){return view.data.failure==model::Failure::Authentication;});
    Check(traffic->requests==before&&traffic->connected==1,"a late Gateway authentication close becomes terminal before another REST request or reconnect");
    Check(reader.Stop(2000),"authentication-closed reader stops");
    options=Options();options.useGateway=false;options.configuration.refreshMs=1000;traffic=std::make_shared<Traffic>();options.http=Http(traffic);
    Check(reader.Start(options,error),"REST-only deletion reconciliation fixture starts");Wait(reader,[](const auto& view){return view.connected&&view.data.messages.size()==1;});traffic->response=3;
    Wait(reader,[](const auto& view){return view.connected&&view.data.messages.empty();});
    Check(traffic->connected==0&&traffic->posts==0,"a complete REST refresh removes deleted cached messages without Gateway, sends or read acknowledgements");Check(reader.Stop(2000),"REST-only reconciliation reader stops");
    options=Options();options.configuration.refreshMs=1000;traffic=std::make_shared<Traffic>();options.http=Http(traffic);options.gatewayFactory=[traffic]{return std::unique_ptr<ReaderGateway>(new FakeGateway(traffic));};
    Check(reader.Start(options,error),"concurrent page/live-event fixture starts");const auto baseline=Wait(reader,[](const auto& view){return view.connected;});traffic->response=3;traffic->blockHistory=true;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!traffic->historyWaiting&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(traffic->historyWaiting,"recovery history request reached its controlled in-flight interval");ReaderDispatch dispatch;{std::lock_guard<std::mutex> lock(traffic->mutex);dispatch=traffic->observer;}
    dispatch(Envelope("MESSAGE_CREATE",2,Message("623456789012345679","concurrent")));traffic->blockHistory=false;
    const auto recovered=Wait(reader,[&](const auto& view){return view.data.lastSuccess>baseline.data.lastSuccess;});
    Check(recovered.data.messages.size()==2,"a stale REST page cannot delete cached messages when a live event arrived during the request");Check(reader.Stop(2000),"concurrent recovery reader stops");
}
void LegacyGatewayGate(){
    aip::discord::Options options;options.sendEnabled=true;options.clearOnStop=false;options.refreshMs=1000;options.operationTimeoutMs=200;
    auto profile=std::make_shared<Profile>();profile->values[{L"general",L"client_id"}]=L"123456789012345678";
    profile->values[{L"general",L"transport_mode"}]=L"gateway";profile->values[{L"general",L"token_protected"}]=L"NOT-A-PROTECTED-CREDENTIAL";
    profile->values[{L"general",L"activity_name"}]=L"Fixture presence";profile->values[{L"general",L"details_template"}]=L"Fixture details";profile->values[{L"general",L"state_template"}]=L"Fixture state";
    options.profileOverride=profile;options.contextOverride=std::make_shared<Context>();Service service;std::wstring error;
    Check(service.Start(options,error),"legacy embedded presence service starts with injected profile/context only");
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);Snapshot snapshot;
    do{snapshot=service.Read();if(snapshot.phase==Phase::Error)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);
    Check(snapshot.phase==Phase::Error&&snapshot.error.find(L"experimental personal-account warning")!=std::wstring::npos&&snapshot.error.find(L"NOT-A-PROTECTED")==std::wstring::npos,
        "actual embedded Gateway path rejects missing warning before credential resolution or network construction");
    Check(service.Stop(2000),"warning-blocked legacy service stops without a transport clear");
}
}
int main(){try{Decoder();Probes();LifecycleAndSend();TerminalAndCancel();LegacyGatewayGate();std::cout<<checks<<" Discord account reader checks passed; synthetic HTTP/Gateway only; no account or message used.\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<" after "<<checks<<" checks\n";return 1;}}

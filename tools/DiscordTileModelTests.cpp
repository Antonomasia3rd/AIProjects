#include "../dependencies/discord_tile_model.h"
#include <iostream>

namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
using namespace aip::discord_tiles;
Binding Context(){Binding b;b.profileKey=L"owned-profile";b.accountId=L"9007199254740993";b.guildId=L"400";b.channelId=L"500";b.connectionGeneration=1;b.configRevision=1;return b;}
Configuration Config(){Configuration c;c.view=L"Channel";c.connection=L"UserTokenExperimental";c.accountId=Context().accountId;c.guildId=L"400";c.channelId=L"500";return c;}
struct Fixture {
    Reducer reducer;Binding binding=Context();Configuration cfg=Config();std::uint64_t seq=0;
    Fixture(){std::wstring error;Check(reducer.Reset(binding,cfg,error),"fixture binds one profile/account/channel without I/O");Capabilities caps;caps.backend=Backend::UserTokenExperimental;caps.identity=caps.channelMessages=caps.messageContent=caps.personalReadState=caps.personalMentions=caps.friends=caps.presence=caps.platformPresence=caps.canSend=caps.enforceNonce=true;auto e=EventFor(EventKind::Capabilities);e.capabilities=caps;Check(reducer.Apply(e)==ApplyResult::Applied,"explicit capabilities are admitted for matching connection kind");}
    Event EventFor(EventKind kind){Event e;e.binding=binding;e.sequence=++seq;e.observedAt=seq*1000;e.kind=kind;return e;}
    void Message(const std::wstring& id,Origin origin=Origin::Live,const std::wstring& body=L"e",std::optional<std::wstring> author=L"42"){
        auto e=EventFor(EventKind::MessageCreate);e.origin=origin;e.message.id=id;e.message.channelId=binding.channelId;e.message.authorId=author;e.message.authorName=L"Author";e.message.content=body;e.message.mentions.direct=false;
        Check(reducer.Apply(e)==ApplyResult::Applied,"selected channel message is reduced without acknowledging it");
    }
    void Sync(const std::wstring& baseline=L"0"){auto e=EventFor(EventKind::SyncComplete);e.baselineWatermark=baseline;Check(reducer.Apply(e)==ApplyResult::Applied,"successful bounded synchronization establishes source freshness");}
    void Delete(const std::wstring& id){auto e=EventFor(EventKind::MessageDelete);e.messageId=id;e.message.channelId=binding.channelId;Check(reducer.Apply(e)==ApplyResult::Applied,"selected deletion enters a bounded tombstone");}
};
void ConfigTests(){
    std::wstring error;Configuration c;std::map<std::wstring,std::wstring> overrides;
    const auto read=[&](const wchar_t* key,const wchar_t* fallback){const auto found=overrides.find(key);return found==overrides.end()?std::wstring(fallback):found->second;};
    Check(ReadConfiguration(read,c,error)&&ValidConfiguration(c)&&c.view==L"Presence"&&c.connection==L"IPC"&&c.clickAction==L"Compose"&&!c.directSendEnabled,"defaults preserve IPC presence and reviewed compose instead of automatic send");
    for(const auto& f:Fields){auto value=std::wstring(f.fallback);Check(NormalizeSetting(f.key,value,error),"every exported default is valid on all settings surfaces");}
    overrides={{L"View",L"profile"},{L"Connection",L"socialsdk"},{L"FriendsFilter",L"active"},{L"FriendsLimit",L"8"},{L"ConfiguredText",L"  e\nnext  "}};
    Check(ReadConfiguration(read,c,error)&&c.view==L"Profile"&&c.connection==L"SocialSDK"&&c.friendsFilter==L"Active"&&c.friendsLimit==8&&c.configuredText==L"  e\nnext  ","choices canonicalize while intentional literal message whitespace remains intact");
    for(const auto& pair:std::vector<std::pair<std::wstring,std::wstring>>{{L"AccountId",L"18446744073709551616"},{L"ChannelId",L"01"},{L"GuildId",L"0"},{L"View",L"Everything"},{L"Connection",L"unknown"},{L"FriendsLimit",L"101"},{L"MessageLimit",L"0"},{L"PreviewCharacters",L"2001"},{L"SendHistoryLimit",L"129"},{L"WarnEveryConnection",L"maybe"},{L"ConfiguredText",std::wstring(1,L'\0')}}){auto value=pair.second;Check(!NormalizeSetting(pair.first,value,error)&&!error.empty(),"invalid IDs, controls, choices and resource limits fail before mutation");}
    const auto before=c;overrides[L"MessageLimit"]=L"-1";Check(!ReadConfiguration(read,c,error)&&c.connection==before.connection&&c.configuredText==before.configuredText,"invalid configuration cannot partially replace a usable snapshot");
    auto value=L"x"+std::wstring(32,L'x');Check(!NormalizeSetting(L"secret-input-key",value,error)&&error.find(L"secret-input-key")==std::wstring::npos,"diagnostics never echo unknown credential-like input keys or values");
    value=L"channel";Check(NormalizeSetting(L"Discord.View",value,error)&&value==L"Channel","qualified Discord prefix uses the same schema");
    value=L"profile";Check(NormalizeSetting(L"DiscordReader.View",value,error)&&value==L"Profile","host DiscordReader prefix avoids collision with legacy presence settings");
    const auto exported=ConfigurationValues(Configuration{});Check(exported.size()==std::size(Fields),"every declared option has a canonical serialization value");
    for(const auto& field:Fields){std::wstring alternative;
        if(field.type==FieldType::Boolean)alternative=std::wstring(field.fallback)==L"1"?L"0":L"1";
        else if(field.type==FieldType::Integer){const std::wstring key=field.key;alternative=key==L"ReconnectMinimumMs"?L"2000":std::to_wstring(field.maximum);}
        else if(field.type==FieldType::Choice){const std::wstring choices=field.choices;const auto split=choices.find(L'|');const auto end=choices.find(L'|',split+1);alternative=choices.substr(split+1,end==std::wstring::npos?end:end-split-1);}
        else if(field.type==FieldType::Snowflake)alternative=L"123";else alternative=L"private configured value";
        std::map<std::wstring,std::wstring> one{{field.key,alternative}};Configuration changed;
        const auto oneReader=[&](const wchar_t* key,const wchar_t* fallback){const auto f=one.find(key);return f==one.end()?std::wstring(fallback):f->second;};
        const auto parsed=ReadConfiguration(oneReader,changed,error);Check(parsed&&SettingValue(changed,field.key)==alternative,"each individual advanced/display field reaches its canonical typed member");
        Check(parsed&&ConfigurationKey(changed)!=ConfigurationKey(Configuration{}),"each individual option invalidates the opaque full configuration fingerprint");
    }
    auto privateCfg=Configuration{};privateCfg.configuredText=L"SENTINEL_PRIVATE_MESSAGE";Check(ConfigurationKey(privateCfg).find(L"SENTINEL")==std::wstring::npos,"cache fingerprints never contain private configured message text");
    overrides={{L"ReconnectMinimumMs",L"60000"},{L"ReconnectMaximumMs",L"1000"}};Check(!ReadConfiguration(read,c,error)&&error.find(L"ReconnectMaximumMs")!=std::wstring::npos,"cross-field reconnect limits are validated atomically");
    std::uint64_t number=0;Check(Snowflake(L"9007199254740993",number)&&number==9007199254740993ULL,"snowflakes retain bits beyond JavaScript double precision");
    Check(Snowflake(L"18446744073709551615",number)&&number==(std::numeric_limits<std::uint64_t>::max)(),"maximum uint64 snowflake parses without overflow");
    Check(!Snowflake(L"-1",number)&&!Snowflake(L"1.0",number)&&!Snowflake(L"1e3",number)&&!Snowflake(L"0",number)&&Snowflake(L"0",number,true),"identifiers are exact decimal IDs while zero is only an explicit cursor");
    Timestamp t;Check(ParseTimestamp(L"2026-10-28T16:12:00.123456789+07:00",t)&&t.nanoseconds==123456789,"ISO timestamps preserve all nine fractional digits without floating point");
    Timestamp utc;Check(ParseTimestamp(L"2026-10-28T09:12:00.123456789Z",utc)&&utc==t,"offset timestamp normalizes to the same exact UTC instant");
    Check(!ParseTimestamp(L"2025-02-29T00:00:00Z",t)&&!ParseTimestamp(L"2024-01-01T00:00:60Z",t)&&!ParseTimestamp(L"2024-01-01T00:00:00",t)&&!ParseTimestamp(L"2024-01-01T00:00:00.1234567890Z",t),"invalid civil dates, leap seconds, missing offsets and excess precision are rejected");
    Check(ParseTimestamp(L"1970-01-01T00:00:00Z",t)&&t.seconds==0&&SnowflakeTimestamp(L"1")->seconds==1420070400,"epoch conversion and snowflake timestamp use integer arithmetic");
    const auto sanitized=DisplayText(L"abc\u202edef\nnext\tword",100,false);Check(sanitized==L"abcdef next word","visible labels remove bidi/control spoofing and flatten line breaks");
    const std::wstring emoji=L"\U0001F600";Check(DisplayText(emoji,1).empty()&&DisplayText(emoji,2)==emoji,"UTF-16 limits never leave half an emoji on either wchar platform");
    Check(!SafeDraftText(std::wstring(1,static_cast<wchar_t>(0xd800)),100)&&!SafeDraftText(L"e\u202e",100)&&SafeDraftText(L"e\nnext",100),"outbound literal drafts reject malformed Unicode and spoofing controls while retaining line breaks");
}
void MessageTests(){
    Fixture f;f.Message(L"100",Origin::InitialHistory);f.Message(L"80",Origin::InitialHistory);
    f.Message(L"101");f.Sync(L"100");auto s=f.reducer.Read();
    Check(s.messages.front().id==L"101"&&s.localNewMessages&&s.localNewMessages->value==1&&s.localNewMessages->exact,"paged initial history does not inflate local unread; a concurrent new event above the frozen watermark does");
    Check(s.hasSuccess&&s.connectionReady&&AgeSeconds(s,s.lastSuccess+300000)==300,"freshness uses supplied success completion time only");
    auto duplicate=f.EventFor(EventKind::MessageCreate);duplicate.sequence=1;duplicate.message.id=L"999";duplicate.message.channelId=f.binding.channelId;
    Check(f.reducer.Apply(duplicate)==ApplyResult::Ignored&&f.reducer.Read().messages.front().id==L"101","older or duplicate event sequences cannot roll back state or fabricate latest messages");
    f.Message(L"101");Check(f.reducer.Read().localNewMessages->value==1,"replayed message IDs cannot count twice even with a newer receipt sequence");
    auto edit=f.EventFor(EventKind::MessageUpdate);edit.message.id=L"101";edit.message.channelId=f.binding.channelId;edit.message.content=L"edited e";edit.message.editedAt=Timestamp{1800000000,999};edit.message.mentions.direct=true;
    Check(f.reducer.Apply(edit)==ApplyResult::Applied&&*f.reducer.Read().messages.front().content==L"edited e"&&f.reducer.Read().localMentions->value==1,"edits update content and known mention predicates without increasing message count");
    auto partial=f.EventFor(EventKind::MessageUpdate);partial.message.id=L"101";partial.message.channelId=f.binding.channelId;partial.message.authorName=L"New display name";
    Check(f.reducer.Apply(partial)==ApplyResult::Applied&&*f.reducer.Read().messages.front().content==L"edited e","missing partial content never erases known edited text");
    auto old=f.EventFor(EventKind::MessageCreate);old.origin=Origin::RecoveryHistory;old.message.id=L"101";old.message.channelId=f.binding.channelId;old.message.content=L"stale REST body";old.message.editedAt=Timestamp{1700000000,0};
    Check(f.reducer.Apply(old)==ApplyResult::Applied&&*f.reducer.Read().messages.front().content==L"edited e","a late REST page cannot overwrite a more recent server edit");
    f.Delete(L"101");f.Message(L"101",Origin::InitialHistory,L"resurrection");Check(f.reducer.Read().messages.front().id==L"100"&&f.reducer.Read().localNewMessages->value==0,"deleted ID cannot reappear from stale history or count again");
    f.Message(L"102",Origin::Live,L"own",f.binding.accountId);Check(f.reducer.Read().localNewMessages->value==0,"own messages are excluded from the local new-message counter by default");
    f.Message(L"103",Origin::Live,L"unknown author",{});Check(!f.reducer.Read().localNewMessages->exact,"unknown sender data cannot be reported as an exact personal unread count");
    auto author=f.EventFor(EventKind::MessageUpdate);author.message.id=L"103";author.message.channelId=f.binding.channelId;author.message.authorId=L"42";
    Check(f.reducer.Apply(author)==ApplyResult::Applied&&f.reducer.Read().localNewMessages->value==1,"a later author patch admits a previously observed message without counting it twice");
    auto foreign=f.EventFor(EventKind::MessageCreate);foreign.message.id=L"104";foreign.message.channelId=L"other";
    Check(f.reducer.Apply(foreign)==ApplyResult::Ignored&&f.reducer.Read().messages.front().id==L"103","channel-scoped reducers ignore another channel");
    auto read=f.EventFor(EventKind::ReadState);read.readState.channelId=f.binding.channelId;read.readState.lastReadMessageId=L"100";read.readState.unreadCount=12;read.readState.mentionCount=4;
    Check(f.reducer.Apply(read)==ApplyResult::Applied&&f.reducer.Read().personalUnread->value==12&&f.reducer.Read().personalMentions->value==4&&f.reducer.Read().localNewMessages->value==1,"authoritative channel counts stay distinct from the local baseline counter");
    read=f.EventFor(EventKind::ReadState);read.readState.channelId=f.binding.channelId;read.readState.lastReadMessageId=L"103";
    Check(f.reducer.Apply(read)==ApplyResult::Applied&&!f.reducer.Read().personalUnread&&!f.reducer.Read().personalMentions,"a changed read cursor invalidates omitted dependent counts rather than keeping stale totals");
    auto failure=f.EventFor(EventKind::Failure);failure.failure=Failure::Transient;f.reducer.Apply(failure);s=f.reducer.Read();
    Check(s.stale&&!s.connectionReady&&s.messages.front().id==L"103"&&s.hasSuccess,"transient failure retains useful last-good data without authorizing sends");
    f.Sync();Check(!f.reducer.Read().stale&&f.reducer.Read().connectionReady,"successful recovery clears failure/stale state");
    auto gap=f.EventFor(EventKind::Gap);f.reducer.Apply(gap);f.Message(L"105",Origin::RecoveryHistory);f.Sync();
    Check(!f.reducer.Read().localNewMessages->exact&&f.reducer.Read().localNewMessages->value==2,"reconnect/history gaps produce honest inexact locally observed counts");
    Check(f.reducer.ResetLocalBaseline(f.binding,L"105")&&f.reducer.Read().localNewMessages->value==0&&f.reducer.Read().localNewMessages->exact,"explicit local reset changes only the local cursor, not a remote read acknowledgement");
    auto previous=f.binding;auto next=f.binding;++next.connectionGeneration;
    Check(f.reducer.Reconnect(next)&&f.reducer.Read().messages.front().id==L"105"&&!f.reducer.Read().connectionReady,"same-account reconnect keeps data but requires a new ready observation");
    auto late=f.EventFor(EventKind::Identity);late.binding=previous;late.user.id=previous.accountId;late.user.username=L"stale user";
    Check(f.reducer.Apply(late)==ApplyResult::Ignored,"a prior connection generation cannot update the new reader");
    auto cfg=f.cfg;cfg.accountId=L"43";next.accountId=L"43";std::wstring error;
    Check(f.reducer.Reset(next,cfg,error)&&f.reducer.Read().messages.empty()&&!f.reducer.Read().hasSuccess,"switching accounts clears every message, cursor and freshness value");
}
void BoundsAndPrivacyTests(){
    Fixture f;auto cfg=f.cfg;cfg.messageLimit=2;cfg.eventHistoryLimit=16;cfg.showMessageContent=false;std::wstring error;
    Check(f.reducer.Reset(f.binding,cfg,error),"bounded private reader reset accepts validated limits");auto caps=f.EventFor(EventKind::Capabilities);caps.capabilities.backend=Backend::UserTokenExperimental;caps.capabilities.channelMessages=caps.capabilities.messageContent=true;f.reducer.Apply(caps);f.Sync();
    for(int i=1;i<=30;++i)f.Message(std::to_wstring(i),Origin::Live,L"private body");auto s=f.reducer.Read();
    Check(s.messages.size()==2&&s.messages.front().id==L"30"&&!s.messages.front().content&&s.localNewMessages->value<=16&&!s.localNewMessages->exact,"message caches/counter history are bounded and privacy mode does not retain body text");
    f.Message(L"1",Origin::InitialHistory,L"old");Check(f.reducer.Read().messages.back().id==L"29","evicted old history cannot churn the latest cache");
    for(int i=31;i<60;++i)f.Delete(std::to_wstring(i));f.Message(L"31",Origin::RecoveryHistory,L"deleted");Check(f.reducer.Read().messages.front().id==L"30","bounded tombstone eviction keeps older deleted IDs behind the safe replay floor");
    Fixture bot;auto botCfg=bot.cfg;botCfg.connection=L"Bot";Check(bot.reducer.Reset(bot.binding,botCfg,error),"bot reader has its own declared connection type");
    auto dishonest=bot.EventFor(EventKind::Capabilities);dishonest.capabilities.backend=Backend::Bot;dishonest.capabilities.channelMessages=dishonest.capabilities.personalReadState=dishonest.capabilities.personalMentions=dishonest.capabilities.friends=true;bot.reducer.Apply(dishonest);
    Check(!bot.reducer.Read().capabilities.personalReadState&&!bot.reducer.Read().capabilities.personalMentions&&!bot.reducer.Read().capabilities.friends,"bot guild observations cannot be relabeled personal unread or friends data");
    auto mismatch=bot.EventFor(EventKind::Capabilities);mismatch.capabilities.backend=Backend::UserTokenExperimental;mismatch.capabilities.canSend=true;
    Check(bot.reducer.Apply(mismatch)==ApplyResult::Invalid&&!bot.reducer.Read().capabilities.canSend,"mismatched backend capabilities cannot bypass the personal-account warning");
    Fixture loss;loss.Message(L"100",Origin::InitialHistory);loss.Sync(L"100");auto reduced=loss.EventFor(EventKind::Capabilities);reduced.capabilities=loss.reducer.Read().capabilities;reduced.capabilities.messageContent=false;loss.reducer.Apply(reduced);
    Check(!loss.reducer.Read().messages.front().content,"content entitlement loss clears retained private bodies immediately");
    reduced=loss.EventFor(EventKind::Capabilities);reduced.capabilities=loss.reducer.Read().capabilities;reduced.capabilities.channelMessages=false;loss.reducer.Apply(reduced);
    Check(loss.reducer.Read().messages.empty()&&!loss.reducer.Read().localNewMessages,"channel capability loss clears message/counter state");
    auto invalid=loss.cfg;invalid.maximumMessageCharacters=-1;Check(!loss.reducer.Reset(loss.binding,invalid,error),"direct struct construction cannot bypass cache/text-limit validation");
    Fixture discard;auto discardCfg=discard.cfg;discardCfg.retainLastGood=false;discard.reducer.Reset(discard.binding,discardCfg,error);
    auto discardCaps=discard.EventFor(EventKind::Capabilities);discardCaps.capabilities.backend=Backend::UserTokenExperimental;discardCaps.capabilities.channelMessages=true;discard.reducer.Apply(discardCaps);discard.Message(L"200",Origin::InitialHistory);discard.Sync(L"200");
    auto reconnected=discard.binding;++reconnected.connectionGeneration;Check(discard.reducer.Reconnect(reconnected)&&discard.reducer.Read().messages.empty(),"explicitly disabled cache retention applies to reconnects as well as failures");
    Fixture mentions;mentions.Sync();auto unknown=mentions.EventFor(EventKind::MessageCreate);unknown.message.id=L"1";unknown.message.channelId=mentions.binding.channelId;unknown.message.authorId=L"42";mentions.reducer.Apply(unknown);
    Check(mentions.reducer.Read().localNewMessages->exact&&mentions.reducer.Read().localMentions->value==0&&!mentions.reducer.Read().localMentions->exact,"unknown mention fields remain an inexact zero rather than an authoritative no-mentions claim");
}
void ProfileTests(){
    Fixture f;auto identity=f.EventFor(EventKind::Identity);identity.user.id=f.binding.accountId;identity.user.displayName=L"Display";identity.user.username=L"account";identity.user.avatarHash=std::wstring(32,L'a');f.reducer.Apply(identity);
    auto patch=f.EventFor(EventKind::Identity);patch.user.id=f.binding.accountId;patch.user.username=L"updated";f.reducer.Apply(patch);auto s=f.reducer.Read();
    Check(*s.user.displayName==L"Display"&&*s.user.username==L"updated"&&AvatarUrl(s.user,99).find(L".png?size=128")!=std::wstring::npos,"profile patches preserve omitted fields and avatar URLs use only controlled CDN identity/hash");
    patch=f.EventFor(EventKind::Identity);patch.user.id=f.binding.accountId;patch.user.avatarHash=L"https://attacker.invalid/token";f.reducer.Apply(patch);
    Check(AvatarUrl(f.reducer.Read().user).find(L"attacker")==std::wstring::npos,"arbitrary avatar URLs and authorization-bearing query strings are never retained");
    auto presence=f.EventFor(EventKind::Presence);presence.presence.userId=f.binding.accountId;presence.presence.status=Status::Online;presence.presence.platforms=Platforms{Status::Online,{},Status::Idle};presence.presence.platformSnapshot=true;f.reducer.Apply(presence);
    Check(VisiblePlatformCount(f.reducer.Read().presence)==2u,"only reported visible platform activity is counted, not security sessions/devices");
    presence=f.EventFor(EventKind::Presence);presence.presence.userId=f.binding.accountId;presence.presence.platforms=Platforms{{},Status::DoNotDisturb,{}};f.reducer.Apply(presence);
    Check(VisiblePlatformCount(f.reducer.Read().presence)==3u,"partial platform updates do not erase omitted known platforms");
    presence=f.EventFor(EventKind::Presence);presence.presence.userId=f.binding.accountId;presence.presence.status=Status::Offline;f.reducer.Apply(presence);
    Check(VisiblePlatformCount(f.reducer.Read().presence)==0u,"offline public presence clears stale visible platform activity without claiming logged-out devices");
    presence=f.EventFor(EventKind::Presence);presence.presence.userId=f.binding.accountId;presence.presence.status=Status::Invisible;f.reducer.Apply(presence);
    Check(f.reducer.Read().presence.status==Status::Invisible&&!VisibleOnline(f.reducer.Read().presence.status),"explicitly reported own invisible status remains distinct without inferring friends' hidden state");
    auto begin=f.EventFor(EventKind::FriendsBegin);f.reducer.Apply(begin);
    for(int i=1;i<=4;++i){auto relation=f.EventFor(EventKind::Relationship);relation.relationship=i==4?Relationship::Blocked:Relationship::Friend;relation.user.id=std::to_wstring(i);relation.user.username=L"Friend "+std::to_wstring(i);f.reducer.Apply(relation);
        auto p=f.EventFor(EventKind::Presence);p.presence.userId=std::to_wstring(i);p.presence.status=i==3?Status::Offline:Status::Online;f.reducer.Apply(p);}
    auto complete=f.EventFor(EventKind::FriendsComplete);f.reducer.Apply(complete);s=f.reducer.Read();
    Check(s.friends.size()==2&&s.onlineFriends&&s.onlineFriends->value==2&&s.onlineFriends->exact,"only actual friends enter the online list; blocked/offline users are excluded");
    f.reducer.Apply(f.EventFor(EventKind::FriendsBegin));auto relation=f.EventFor(EventKind::Relationship);relation.relationship=Relationship::Friend;relation.user.id=L"9";relation.user.username=L"New friend";f.reducer.Apply(relation);
    Check(f.reducer.Read().friends.size()==2,"a partial friends refresh preserves the useful previous snapshot until completion");
    complete=f.EventFor(EventKind::FriendsComplete);f.reducer.Apply(complete);Check(f.reducer.Read().friends.empty()&&f.reducer.Read().onlineFriends->value==0&&!f.reducer.Read().onlineFriends->exact,"a complete relationship replacement prunes former friends without treating an unknown presence as an exact zero");
    auto cfg=f.cfg;cfg.friendsCacheLimit=1;std::wstring error;f.reducer.Reset(f.binding,cfg,error);auto caps=f.EventFor(EventKind::Capabilities);caps.capabilities.backend=Backend::UserTokenExperimental;caps.capabilities.friends=true;f.reducer.Apply(caps);f.reducer.Apply(f.EventFor(EventKind::FriendsBegin));
    for(int i=10;i<13;++i){auto r=f.EventFor(EventKind::Relationship);r.relationship=Relationship::Friend;r.user.id=std::to_wstring(i);f.reducer.Apply(r);}complete=f.EventFor(EventKind::FriendsComplete);f.reducer.Apply(complete);
    Check(!f.reducer.Read().friendsComplete&&f.reducer.Read().onlineFriends&&!f.reducer.Read().onlineFriends->exact,"truncated relationship snapshots cannot claim a complete friends count");
}
void SendTests(){
    Fixture f;f.Sync();auto s=f.reducer.Read();Configuration cfg=f.cfg;cfg.allowReviewedSend=true;std::wstring error;ComposeIntent intent;SendLedger ledger;SendRequest request;
    Check(BeginCompose(s,cfg,L"e",L"nonce_1",1,intent,error)&&!intent.Reviewed(),"opening a composer only creates an unreviewed local intent");
    Check(!AuthorizeSend(intent,s,cfg,ledger,request,error),"draft creation never authorizes an outbound message");
    Check(ReviewCompose(intent,s,cfg,error)&&!AuthorizeSend(intent,s,cfg,ledger,request,error),"reviewed personal-token sending still requires an account/profile-bound warning acknowledgement");
    cfg.experimentalAcknowledged=true;cfg.acknowledgedProfileKey=s.binding.profileKey;cfg.acknowledgedAccountId=s.binding.accountId;
    auto sendOff=cfg;sendOff.allowReviewedSend=false;Check(!AuthorizeSend(intent,s,sendOff,ledger,request,error),"reader sending remains separately opt-in despite available account capabilities");
    Check(AuthorizeSend(intent,s,cfg,ledger,request,error)&&request.text==L"e"&&request.enforceNonce&&!request.allowEveryoneMentions&&!request.allowRoleMentions&&!request.allowUserMentions,"authorized intent returns only an explicit request with default mention suppression and no credentials");
    Check(!AuthorizeSend(intent,s,cfg,ledger,request,error)&&ledger.Complete(s.binding,1,SendOutcome::Unknown)&&!AuthorizeSend(intent,s,cfg,ledger,request,error),"duplicate activation and ambiguous completion cannot authorize an automatic retry");
    Check(SetComposeText(intent,L"different",error)&&!intent.Reviewed()&&!AuthorizeSend(intent,s,cfg,ledger,request,error),"editing a reviewed draft returns it to the review stage");
    ComposeIntent next;Check(BeginCompose(s,cfg,L"@everyone literal",L"nonce_2",2,next,error)&&ReviewCompose(next,s,cfg,error),"message text is literal data and is not expanded as a command/template");
    auto changed=s;++changed.binding.configRevision;Check(!AuthorizeSend(next,changed,cfg,ledger,request,error),"configuration changes while composing invalidate the frozen destination");
    changed=s;changed.binding.accountId=L"43";Check(!AuthorizeSend(next,changed,cfg,ledger,request,error),"account changes cannot send a reviewed draft under the new identity");
    auto wrongCfg=cfg;wrongCfg.accountId=L"43";Check(!AuthorizeSend(next,s,wrongCfg,ledger,request,error),"an independently changed configured account is rejected even before host revision refresh");
    changed=s;changed.connectionReady=false;Check(!AuthorizeSend(next,changed,cfg,ledger,request,error),"stale/disconnected snapshots cannot send using old capabilities");
    cfg.warnEveryConnection=true;Check(!AuthorizeSend(next,s,cfg,ledger,request,error),"a configured repeat warning requires this exact connection acknowledgement");
    Acknowledgement ack{s.binding.profileKey,s.binding.accountId,cfg.connection,s.binding.connectionGeneration,1};
    Check(AuthorizeSend(next,s,cfg,ledger,request,error,&ack),"an explicit session acknowledgement applies only to its profile/account/backend/generation");
    auto another=ack;another.accountId=L"43";Check(!ExperimentalAcknowledged(cfg,s.binding,&another),"another account's acknowledgement never transfers");
    cfg.warnEveryConnection=false;cfg.clickAction=L"SendConfigured";cfg.configuredText=L"e";ComposeIntent configured;
    Check(!BeginConfiguredSend(s,cfg,L"nonce_3",3,configured,error),"configured direct sends remain separately disabled by default");
    cfg.directSendEnabled=true;Check(BeginConfiguredSend(s,cfg,L"nonce_3",3,configured,error)&&configured.Configured(),"explicit direct-send configuration freezes its literal message and destination");
    cfg.configuredText=L"changed";Check(!AuthorizeSend(configured,s,cfg,ledger,request,error),"a changed configured template cannot reuse an older authorized intent");
    const auto emoji=std::wstring(L"\U0001F600");Check(!SendTextValid(emoji,1)&&SendTextValid(emoji,2)&&!SendTextValid(L" \r\n\t",2000),"sender limits count UTF-16 units and reject empty/control-only messages");
    SendLedger bounded;for(std::uint64_t i=1;i<=20;++i)Check(bounded.Reserve(s.binding,i,L"n"+std::to_wstring(i),2),"new explicit activations can reserve distinct request identities");
    Check(bounded.Retained()==2&&!bounded.Reserve(s.binding,1,L"old-nonce",2),"bounded ledger history retains a high-water mark that still rejects evicted activation replays");
}
}
int main(){ConfigTests();MessageTests();BoundsAndPrivacyTests();ProfileTests();SendTests();std::cout<<checks<<" Discord tile model checks; "<<failures<<" failures; no account/network/message/read-ack operations\n";return failures?1:0;}

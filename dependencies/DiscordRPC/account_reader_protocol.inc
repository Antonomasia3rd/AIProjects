#pragma once
// Bounded projections of supplied JSON only. No I/O or raw payload logging.
#include "account_reader.h"
#include "../content_sources/notes.inc"
namespace aip { namespace discord { namespace reader_detail {
using Object=std::map<std::string,std::string>;
inline bool ValidJson(const std::string& json){if(json.size()>1048576)return false;std::wstring decoded;if(!aip::TryUtf8ToWide(json,decoded))return false;
    std::size_t p=0;if(!aip::notes::detail::ValidateValue(json,p,0))return false;aip::notes::detail::SkipWhitespace(json,p);return p==json.size();}
inline bool ObjectValue(const std::string& json,Object& out){
    out.clear();if(!ValidJson(json))return false;
    std::size_t p=0;aip::notes::detail::SkipWhitespace(json,p);if(p>=json.size()||json[p++]!='{')return false;
    for(;;){aip::notes::detail::SkipWhitespace(json,p);if(p>=json.size())return false;if(json[p]=='}')return true;
        const auto end=aip::JsonStringEnd(json,p);std::string key;if(end==std::string::npos||!aip::DecodeJsonStringUtf8Range(json,p,end,key))return false;
        p=end;aip::notes::detail::SkipWhitespace(json,p);if(p>=json.size()||json[p++]!=':')return false;aip::notes::detail::SkipWhitespace(json,p);
        const auto valueEnd=aip::JsonValueEnd(json,p);if(valueEnd==std::string::npos||out.size()>=256||!out.emplace(key,json.substr(p,valueEnd-p)).second)return false;
        p=valueEnd;aip::notes::detail::SkipWhitespace(json,p);if(p<json.size()&&json[p]==',')++p;
    }
}
inline bool ArrayValue(const std::string& json,std::vector<std::string>& out,std::size_t maximum,bool& complete){
    out.clear();complete=true;if(!ValidJson(json))return false;
    std::size_t p=0;aip::notes::detail::SkipWhitespace(json,p);if(p>=json.size()||json[p++]!='[')return false;
    for(;;){aip::notes::detail::SkipWhitespace(json,p);if(p>=json.size())return false;if(json[p]==']')return true;
        const auto end=aip::JsonValueEnd(json,p);if(end==std::string::npos)return false;
        if(out.size()<maximum)out.push_back(json.substr(p,end-p));else complete=false;
        p=end;aip::notes::detail::SkipWhitespace(json,p);if(p<json.size()&&json[p]==',')++p;
    }
}
inline const std::string* Raw(const Object& object,const char* key){const auto f=object.find(key);return f==object.end()?nullptr:&f->second;}
inline bool Wide(const std::string& raw,std::wstring& value,std::size_t maximum=8192){std::string decoded;return aip::DecodeJsonStringUtf8Range(raw,0,raw.size(),decoded)&&aip::TryUtf8ToWide(decoded,value)&&value.size()<=maximum&&value.find(L'\0')==std::wstring::npos;}
inline bool String(const Object& object,const char* key,std::wstring& value,std::size_t maximum=8192){const auto* raw=Raw(object,key);return raw&&Wide(*raw,value,maximum);}
inline bool UInt(const std::string& text,std::uint64_t& value){if(text.empty()||text.size()>20||(text.size()>1&&text.front()=='0'))return false;std::uint64_t n=0;
    for(const auto c:text){if(c<'0'||c>'9'||n>((std::numeric_limits<std::uint64_t>::max)()-static_cast<unsigned>(c-'0'))/10)return false;n=n*10+static_cast<unsigned>(c-'0');}value=n;return true;}
inline bool UInt(const Object& object,const char* key,std::uint64_t& value){const auto* raw=Raw(object,key);return raw&&UInt(*raw,value);}
inline bool Boolean(const Object& object,const char* key,bool& value){const auto* raw=Raw(object,key);if(!raw||(*raw!="true"&&*raw!="false"))return false;value=*raw=="true";return true;}
inline bool Identifier(const Object& object,const char* key,std::wstring& value,bool zero=false){std::uint64_t ignored=0;return String(object,key,value,20)&&discord_tiles::Snowflake(value,ignored,zero);}
inline bool User(const Object& o,discord_tiles::UserPatch& user){user={};if(!Identifier(o,"id",user.id))return false;
    for(const auto* key:{"global_name","username","avatar"})if(const auto* raw=Raw(o,key)){std::wstring text;if(*raw!="null"&&!Wide(*raw,text,256))return false;
        if(std::string(key)=="global_name")user.displayName=text;else if(std::string(key)=="username")user.username=text;
        else{if(!discord_tiles::ValidAvatarHash(text))return false;user.avatarHash=text;}}
    return true;
}
inline bool Timestamp(const std::wstring& text,discord_tiles::Timestamp& output){
    // Discord timestamps are RFC3339. Preserve fractional precision; IDs are
    // independently decoded without double conversion.
    if(text.size()<20||text.size()>40)return false;
    const auto number=[&](std::size_t p,std::size_t n,int& value){if(p+n>text.size())return false;value=0;for(std::size_t i=p;i<p+n;++i){if(text[i]<L'0'||text[i]>L'9')return false;value=value*10+text[i]-L'0';}return true;};
    int year=0,month=0,day=0,hour=0,minute=0,second=0;if(!number(0,4,year)||!number(5,2,month)||!number(8,2,day)||!number(11,2,hour)||!number(14,2,minute)||!number(17,2,second)||
        text[4]!=L'-'||text[7]!=L'-'||text[10]!=L'T'||text[13]!=L':'||text[16]!=L':'||year<1||month<1||month>12||hour>23||minute>59||second>59)return false;
    const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};const bool leap=year%4==0&&(year%100!=0||year%400==0);if(day<1||day>lengths[month-1]+(month==2&&leap?1:0))return false;
    std::size_t p=19;std::uint32_t nanos=0;unsigned digits=0;if(p<text.size()&&text[p]==L'.'){++p;while(p<text.size()&&text[p]>=L'0'&&text[p]<=L'9'){if(++digits>9)return false;nanos=nanos*10+text[p++]-L'0';}if(!digits)return false;for(;digits<9;++digits)nanos*=10;}
    int offset=0;if(p<text.size()&&text[p]==L'Z'){++p;}else{if(p>=text.size()||(text[p]!=L'+'&&text[p]!=L'-'))return false;const int sign=text[p++]==L'+'?1:-1;int h=0,m=0;
        if(!number(p,2,h)||p+5!=text.size()||text[p+2]!=L':'||!number(p+3,2,m)||h>23||m>59)return false;offset=sign*(h*3600+m*60);p+=5;}if(p!=text.size())return false;
    int y=year;y-=month<=2;const int era=(y>=0?y:y-399)/400;const unsigned yo=static_cast<unsigned>(y-era*400);const unsigned mp=static_cast<unsigned>(month+(month>2?-3:9));
    const unsigned doy=(153*mp+2)/5+static_cast<unsigned>(day)-1;const auto days=static_cast<std::int64_t>(era)*146097+static_cast<std::int64_t>(yo*365+yo/4-yo/100+doy)-719468;
    output={days*86400+hour*3600+minute*60+second-offset,nanos};return discord_tiles::ValidTimestamp(output);
}
inline bool Message(const Object& o,const discord_tiles::Binding& binding,discord_tiles::MessagePatch& message){
    message={};if(!Identifier(o,"id",message.id)||!Identifier(o,"channel_id",message.channelId))return false;
    if(const auto* raw=Raw(o,"author")){Object author;discord_tiles::UserPatch user;if(!ObjectValue(*raw,author)||!User(author,user))return false;message.authorId=user.id;message.authorName=user.displayName&&!user.displayName->empty()?user.displayName:user.username;}
    if(const auto* raw=Raw(o,"content")){std::wstring text;if(!Wide(*raw,text,65536))return false;message.content=std::move(text);}
    for(const auto* key:{"timestamp","edited_timestamp"})if(const auto* raw=Raw(o,key)){if(*raw=="null")continue;std::wstring text;discord_tiles::Timestamp value;if(!Wide(*raw,text,40)||!Timestamp(text,value))return false;
        if(std::string(key)=="timestamp")message.sentAt=value;else message.editedAt=value;}
    if(const auto* raw=Raw(o,"mentions")){std::vector<std::string> values;bool complete=false;if(!ArrayValue(*raw,values,1000,complete)||!complete)return false;bool direct=false;
        for(const auto& item:values){Object person;std::wstring id;if(!ObjectValue(item,person)||!Identifier(person,"id",id))return false;direct|=id==binding.accountId;}message.mentions.direct=direct;}
    bool everyone=false;if(Boolean(o,"mention_everyone",everyone))message.mentions.everyone=everyone;
    // mention_roles alone does not establish that the current user has a role.
    return true;
}
inline std::optional<discord_tiles::Status> Status(const std::wstring& value){using S=discord_tiles::Status;if(value==L"online")return S::Online;if(value==L"idle")return S::Idle;if(value==L"dnd")return S::DoNotDisturb;if(value==L"offline")return S::Offline;if(value==L"invisible")return S::Invisible;return {};}
inline bool Presence(const Object& o,discord_tiles::PresencePatch& presence){presence={};Object user;if(const auto* raw=Raw(o,"user")){if(!ObjectValue(*raw,user)||!Identifier(user,"id",presence.userId))return false;}
    else if(!Identifier(o,"user_id",presence.userId))return false;
    if(const auto* raw=Raw(o,"status")){std::wstring status;if(!Wide(*raw,status,32))return false;presence.status=Status(status);}
    if(const auto* raw=Raw(o,"client_status")){Object platforms;if(!ObjectValue(*raw,platforms))return false;discord_tiles::Platforms result;
        for(const auto* key:{"desktop","mobile","web"})if(const auto* field=Raw(platforms,key)){std::wstring status;if(!Wide(*field,status,32))return false;const auto value=Status(status);
            if(std::string(key)=="desktop")result.desktop=value;else if(std::string(key)=="mobile")result.mobile=value;else result.web=value;}
        presence.platforms=result;presence.platformSnapshot=true;}
    return true;
}
inline bool ReadState(const Object& o,discord_tiles::ReadState& state){state={};if(!Identifier(o,"channel_id",state.channelId)&&!Identifier(o,"id",state.channelId))return false;
    if(const auto* raw=Raw(o,"last_message_id")){if(*raw!="null"){std::wstring cursor;if(!Identifier(o,"last_message_id",cursor,true))return false;state.lastReadMessageId=cursor;}}
    std::uint64_t number=0;if(Raw(o,"mention_count")){if(!UInt(o,"mention_count",number))return false;state.mentionCount=number;}
    if(Raw(o,"unread_count")){if(!UInt(o,"unread_count",number))return false;state.unreadCount=number;}return true;
}
inline discord_tiles::Relationship Relationship(std::uint64_t type){using R=discord_tiles::Relationship;return type==1?R::Friend:type==2?R::Blocked:type==3?R::PendingIncoming:type==4?R::PendingOutgoing:R::Other;}
struct Emitter {
    const discord_tiles::Binding& binding;std::uint64_t sequence;discord_tiles::Milliseconds now;std::vector<discord_tiles::Event> events;
    bool Add(discord_tiles::Event e){if(events.size()>=8192||sequence==(std::numeric_limits<std::uint64_t>::max)())return false;e.binding=binding;e.sequence=++sequence;e.observedAt=now;events.push_back(std::move(e));return true;}
    bool Simple(discord_tiles::EventKind kind){discord_tiles::Event e;e.kind=kind;return Add(std::move(e));}
};
} // reader_detail
bool DecodeReaderIdentity(const std::string& json,discord_tiles::UserPatch& user){reader_detail::Object object;return reader_detail::ObjectValue(json,object)&&reader_detail::User(object,user);}
bool DecodeReaderHistory(const std::string& json,const discord_tiles::Binding& binding,const discord_tiles::Configuration& cfg,
    discord_tiles::Origin origin,std::uint64_t& sequence,discord_tiles::Milliseconds now,std::vector<discord_tiles::Event>& events,std::wstring& error){
    using namespace reader_detail;events.clear();std::vector<std::string> rows;bool complete=false;if(!ArrayValue(json,rows,static_cast<std::size_t>(cfg.messageLimit),complete)){error=L"Discord channel history was not a bounded valid array.";return false;}
    Emitter output{binding,sequence,now,{}};std::wstring watermark=L"0";std::uint64_t maximum=0;
    for(const auto& row:rows){Object object;discord_tiles::Event event;event.kind=discord_tiles::EventKind::MessageCreate;event.origin=origin;
        if(!ObjectValue(row,object)||!Message(object,binding,event.message)||event.message.channelId!=binding.channelId){error=L"Discord channel history did not match the selected channel.";return false;}
        std::uint64_t id=0;discord_tiles::Snowflake(event.message.id,id);if(id>maximum){maximum=id;watermark=event.message.id;}if(!output.Add(std::move(event)))return false;}
    discord_tiles::Event done;done.kind=discord_tiles::EventKind::SyncComplete;done.baselineWatermark=watermark;done.complete=complete&&rows.size()<static_cast<std::size_t>(cfg.messageLimit);
    if(!output.Add(std::move(done)))return false;sequence=output.sequence;events=std::move(output.events);error.clear();return true;
}
bool DecodeReaderDispatch(const std::string& json,const discord_tiles::Binding& binding,const discord_tiles::Configuration& cfg,
    const discord_tiles::Capabilities& verified,ReaderDecodeState& decoder,std::uint64_t& sequence,discord_tiles::Milliseconds now,
    std::vector<discord_tiles::Event>& events,std::wstring& error){
    using namespace reader_detail;using K=discord_tiles::EventKind;events.clear();Object envelope;std::uint64_t opcode=0;
    const auto fail=[&]{error=L"Discord event payload was invalid or exceeded the reader limits.";return false;};
    if(!ObjectValue(json,envelope)||!UInt(envelope,"op",opcode))return fail();Emitter output{binding,sequence,now,{}};
    if(opcode==7||opcode==9){if(!output.Simple(K::Gap))return fail();if(opcode==9)decoder.wireSequence.reset();sequence=output.sequence;events=std::move(output.events);error.clear();return true;}
    if(opcode!=0){error.clear();return true;}
    std::wstring name;if(!String(envelope,"t",name,128))return fail();std::uint64_t wire=0;
    if(!UInt(envelope,"s",wire))return fail();const auto previous=name==L"READY"?std::optional<std::uint64_t>{}:decoder.wireSequence;
    if(previous&&wire<=*previous){error.clear();return true;}
    if(previous&&wire>*previous+1&&!output.Simple(K::Gap))return fail();
    const std::set<std::wstring> relevant={L"READY",L"RESUMED",L"USER_UPDATE",L"CURRENT_USER_UPDATE",L"MESSAGE_CREATE",L"MESSAGE_UPDATE",L"MESSAGE_DELETE",L"MESSAGE_DELETE_BULK",L"MESSAGE_ACK",L"READ_STATE_UPDATE",L"PRESENCE_UPDATE",L"RELATIONSHIP_ADD",L"RELATIONSHIP_UPDATE",L"RELATIONSHIP_REMOVE"};
    if(!relevant.count(name)){decoder.wireSequence=wire;sequence=output.sequence;events=std::move(output.events);error.clear();return true;}
    Object data;const auto* raw=Raw(envelope,"d");if(!raw||!ObjectValue(*raw,data))return fail();
    if(name==L"READY"){
        Object user;discord_tiles::UserPatch identity;const auto* current=Raw(data,"user");if(!current||!ObjectValue(*current,user)||!User(user,identity)||identity.id!=binding.accountId){error=L"Discord returned a different or invalid account for this reader binding.";return false;}
        auto caps=discord_tiles::HonestCapabilities(verified);caps.identity=true;
        if(caps.backend==discord_tiles::Backend::UserTokenExperimental){const auto* relations=Raw(data,"relationships");const auto* reads=Raw(data,"read_state");caps.friends=relations&&*relations!="null";caps.personalReadState=reads&&*reads!="null";caps.personalMentions=caps.personalReadState;}
        discord_tiles::Event capability;capability.kind=K::Capabilities;capability.capabilities=caps;if(!output.Add(std::move(capability)))return fail();
        discord_tiles::Event self;self.kind=K::Identity;self.user=identity;if(!output.Add(std::move(self)))return fail();
        std::map<std::wstring,discord_tiles::UserPatch> people;people.emplace(identity.id,identity);
        if(const auto* users=Raw(data,"users")){std::vector<std::string> rows;bool complete=false;if(!ArrayValue(*users,rows,10000,complete))return fail();
            for(const auto& row:rows){Object person;discord_tiles::UserPatch u;if(!ObjectValue(row,person)||!User(person,u))return fail();people.emplace(u.id,std::move(u));}}
        if(caps.friends){std::vector<std::string> rows;bool complete=false;if(!ArrayValue(*Raw(data,"relationships"),rows,static_cast<std::size_t>((std::min)(cfg.friendsCacheLimit,2000)),complete)||!output.Simple(K::FriendsBegin))return fail();
            for(const auto& row:rows){Object relation;discord_tiles::Event e;e.kind=K::Relationship;std::uint64_t type=0;if(!ObjectValue(row,relation)||!UInt(relation,"type",type))return fail();
                if(const auto* u=Raw(relation,"user")){Object object;if(!ObjectValue(*u,object)||!User(object,e.user))return fail();}
                else{std::wstring id;if(!Identifier(relation,"user_id",id)&&!Identifier(relation,"id",id))return fail();const auto found=people.find(id);if(found!=people.end())e.user=found->second;else e.user.id=id;}
                e.relationship=Relationship(type);if(!output.Add(std::move(e)))return fail();}
            discord_tiles::Event done;done.kind=K::FriendsComplete;done.complete=complete;if(!output.Add(std::move(done)))return fail();}
        const auto presenceRows=[&](const std::string& array){std::vector<std::string> rows;bool complete=false;if(!ArrayValue(array,rows,2000,complete))return false;for(const auto& row:rows){Object object;discord_tiles::Event e;e.kind=K::Presence;if(!ObjectValue(row,object)||!Presence(object,e.presence)||!output.Add(std::move(e)))return false;}return true;};
        if(const auto* presences=Raw(data,"presences")){if(!presenceRows(*presences))return fail();}
        if(const auto* merged=Raw(data,"merged_presences")){Object object;if(!ObjectValue(*merged,object))return fail();if(const auto* friends=Raw(object,"friends"))if(!presenceRows(*friends))return fail();}
        if(caps.personalReadState){const auto* reads=Raw(data,"read_state");Object object;std::vector<std::string> rows;bool complete=false;
            if(!reads->empty()&&reads->front()=='['){if(!ArrayValue(*reads,rows,10000,complete))return fail();}
            else if(!ObjectValue(*reads,object)||!Raw(object,"entries")||!ArrayValue(*Raw(object,"entries"),rows,10000,complete))return fail();
            for(const auto& row:rows){Object read;discord_tiles::Event e;e.kind=K::ReadState;if(!ObjectValue(row,read)||!ReadState(read,e.readState))return fail();if(e.readState.channelId==binding.channelId&&!output.Add(std::move(e)))return fail();}}
    }else if(name==L"USER_UPDATE"||name==L"CURRENT_USER_UPDATE"){discord_tiles::Event e;e.kind=K::Identity;if(!User(data,e.user)||!output.Add(std::move(e)))return fail();}
    else if(name==L"MESSAGE_CREATE"||name==L"MESSAGE_UPDATE"){discord_tiles::Event e;e.kind=name==L"MESSAGE_CREATE"?K::MessageCreate:K::MessageUpdate;if(!Message(data,binding,e.message))return fail();if(e.message.channelId==binding.channelId&&!output.Add(std::move(e)))return fail();}
    else if(name==L"MESSAGE_DELETE"){discord_tiles::Event e;e.kind=K::MessageDelete;if(!Identifier(data,"id",e.messageId)||!Identifier(data,"channel_id",e.message.channelId))return fail();if(e.message.channelId==binding.channelId&&!output.Add(std::move(e)))return fail();}
    else if(name==L"MESSAGE_DELETE_BULK"){std::wstring channel;if(!Identifier(data,"channel_id",channel)||!Raw(data,"ids"))return fail();std::vector<std::string> rows;bool complete=false;if(!ArrayValue(*Raw(data,"ids"),rows,1000,complete)||!complete)return fail();if(channel==binding.channelId)for(const auto& row:rows){discord_tiles::Event e;e.kind=K::MessageDelete;e.message.channelId=channel;if(!Wide(row,e.messageId,20)||!discord_tiles::Snowflake(e.messageId)||!output.Add(std::move(e)))return fail();}}
    else if(name==L"MESSAGE_ACK"||name==L"READ_STATE_UPDATE"){discord_tiles::Event e;e.kind=K::ReadState;Object read=data;if(name==L"MESSAGE_ACK"){if(const auto* id=Raw(read,"message_id"))read["last_message_id"]=*id;}if(!ReadState(read,e.readState))return fail();if(e.readState.channelId==binding.channelId&&!output.Add(std::move(e)))return fail();}
    else if(name==L"PRESENCE_UPDATE"){discord_tiles::Event e;e.kind=K::Presence;if(!Presence(data,e.presence)||!output.Add(std::move(e)))return fail();}
    else if(name==L"RELATIONSHIP_ADD"||name==L"RELATIONSHIP_UPDATE"||name==L"RELATIONSHIP_REMOVE"){discord_tiles::Event e;e.kind=K::Relationship;std::uint64_t type=0;if(name!=L"RELATIONSHIP_REMOVE"&&!UInt(data,"type",type))return fail();
        if(const auto* rawUser=Raw(data,"user")){Object object;if(!ObjectValue(*rawUser,object)||!User(object,e.user))return fail();}else if(!Identifier(data,"id",e.user.id)&&!Identifier(data,"user_id",e.user.id))return fail();e.relationship=name==L"RELATIONSHIP_REMOVE"?discord_tiles::Relationship::None:Relationship(type);if(!output.Add(std::move(e)))return fail();}
    decoder.wireSequence=wire;sequence=output.sequence;events=std::move(output.events);error.clear();return true;
}
} }

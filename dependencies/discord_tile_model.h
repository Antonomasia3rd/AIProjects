#pragma once
// Portable projection of explicitly authorized Discord data. No credentials,
// sockets, filesystem, read acknowledgements, presence writes or message sends.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aip { namespace discord_tiles {
using Milliseconds = std::uint64_t; // supplied monotonic observation clock
inline std::wstring Lower(std::wstring value) { for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value; }
inline std::wstring Trim(const std::wstring& value) { const auto first=value.find_first_not_of(L" \r\n\t");return first==std::wstring::npos?L"":value.substr(first,value.find_last_not_of(L" \r\n\t")-first+1); }
inline bool Snowflake(const std::wstring& value,std::uint64_t& number,bool allowZero=false) {
    if(value.empty()||value.size()>20||(value.size()>1&&value.front()==L'0'))return false;
    std::uint64_t parsed=0;for(auto c:value){if(c<L'0'||c>L'9'||parsed>((std::numeric_limits<std::uint64_t>::max)()-static_cast<unsigned>(c-L'0'))/10)return false;parsed=parsed*10+static_cast<unsigned>(c-L'0');}
    if(!allowZero&&!parsed)return false;number=parsed;return true;
}
inline bool Snowflake(const std::wstring& value) { std::uint64_t ignored=0;return Snowflake(value,ignored); }
inline bool SafeSingleLine(const std::wstring& value,std::size_t maximum) { return value.size()<=maximum&&std::none_of(value.begin(),value.end(),[](wchar_t c){return c<0x20||c==0x7f;}); }
inline bool BidiControl(std::uint32_t c) { return (c>=0x202a&&c<=0x202e)||(c>=0x2066&&c<=0x2069)||c==0x200e||c==0x200f||c==0x061c; }
// Bound by UTF-16 code units on every compiler. An emoji is never split into
// a dangling surrogate. Control/bidi text cannot spoof native status labels.
inline std::wstring DisplayText(const std::wstring& value,std::size_t maximum,bool multiline=false) {
    std::wstring out;std::size_t units=0;
    for(std::size_t i=0;i<value.size();++i){std::uint32_t c=static_cast<std::uint32_t>(value[i]);std::wstring part;
        if(c>=0xd800&&c<=0xdbff){if(i+1>=value.size())continue;const auto low=static_cast<std::uint32_t>(value[i+1]);if(low<0xdc00||low>0xdfff)continue;part+=value[i];part+=value[++i];if(units+2>maximum)break;out+=part;units+=2;continue;}
        if(c>=0xdc00&&c<=0xdfff)continue;if(c>0x10ffff||BidiControl(c)||c==0x7f)continue;
        if(c==L'\r'){if(i+1<value.size()&&value[i+1]==L'\n')++i;c=L'\n';}
        if(c==0x2028||c==0x2029)c=L'\n';if(c==L'\n'&&!multiline)c=L' ';if(c==L'\t')c=L' ';
        if(c<0x20&&c!=L'\n')continue;const std::size_t width=c>0xffff?2u:1u;if(units+width>maximum)break;
        out+=static_cast<wchar_t>(c);units+=width;
    }return out;
}
inline bool Utf16Length(const std::wstring& value,std::size_t& count) {
    std::size_t units=0;for(std::size_t i=0;i<value.size();++i){const auto c=static_cast<std::uint32_t>(value[i]);
        if(c>0x10ffff)return false;if(c>=0xd800&&c<=0xdbff){if(i+1>=value.size())return false;const auto low=static_cast<std::uint32_t>(value[++i]);if(low<0xdc00||low>0xdfff)return false;units+=2;}
        else if(c>=0xdc00&&c<=0xdfff)return false;else units+=c>0xffff?2u:1u;
    }count=units;return true;
}
inline bool SafeDraftText(const std::wstring& value,std::size_t maximum){std::size_t length=0;if(!Utf16Length(value,length)||length>maximum)return false;for(auto c:value){const auto n=static_cast<std::uint32_t>(c);if((n<0x20&&c!=L'\r'&&c!=L'\n'&&c!=L'\t')||n==0x7f||BidiControl(n))return false;}return true;}
struct Timestamp { std::int64_t seconds=0;std::uint32_t nanoseconds=0; };
inline bool operator==(const Timestamp& a,const Timestamp& b){return a.seconds==b.seconds&&a.nanoseconds==b.nanoseconds;}
inline bool operator<(const Timestamp& a,const Timestamp& b){return a.seconds<b.seconds||(a.seconds==b.seconds&&a.nanoseconds<b.nanoseconds);}
inline bool ValidTimestamp(const Timestamp& t){return t.seconds>=-11644473600LL&&t.seconds<=253402300799LL&&t.nanoseconds<1000000000;}
inline bool ParseTimestamp(const std::wstring& text,Timestamp& output){
    if(text.size()<20||text.size()>35)return false;const auto digits=[&](std::size_t p,std::size_t count,int& out){out=0;if(p+count>text.size())return false;for(std::size_t i=0;i<count;++i){const auto c=text[p+i];if(c<L'0'||c>L'9')return false;out=out*10+static_cast<int>(c-L'0');}return true;};
    int year=0,month=0,day=0,hour=0,minute=0,second=0;
    if(!digits(0,4,year)||text[4]!=L'-'||!digits(5,2,month)||text[7]!=L'-'||!digits(8,2,day)||(text[10]!=L'T'&&text[10]!=L't')||!digits(11,2,hour)||text[13]!=L':'||!digits(14,2,minute)||text[16]!=L':'||!digits(17,2,second))return false;
    const bool leap=year%4==0&&(year%100!=0||year%400==0);const int days[]={31,28+(leap?1:0),31,30,31,30,31,31,30,31,30,31};
    if(year<1601||year>9999||month<1||month>12||day<1||day>days[month-1]||hour>23||minute>59||second>59)return false;
    std::size_t p=19;std::uint32_t fraction=0;unsigned places=0;if(p<text.size()&&text[p]==L'.'){++p;while(p<text.size()&&text[p]>=L'0'&&text[p]<=L'9'){if(++places>9)return false;fraction=fraction*10+static_cast<unsigned>(text[p++]-L'0');}if(!places)return false;while(places++<9)fraction*=10;}
    int offset=0;if(p<text.size()&&(text[p]==L'Z'||text[p]==L'z')){++p;}
    else {if(p+6!=text.size()||(text[p]!=L'+'&&text[p]!=L'-')||text[p+3]!=L':')return false;const bool negative=text[p]==L'-';int h=0,m=0;if(!digits(p+1,2,h)||!digits(p+4,2,m)||h>23||m>59)return false;offset=(h*60+m)*60*(negative?-1:1);p+=6;}
    if(p!=text.size())return false;int y=year-(month<=2);const int era=y/400;const unsigned yo=static_cast<unsigned>(y-era*400);const unsigned adjusted=static_cast<unsigned>(month+(month>2?-3:9));
    const auto civil=static_cast<std::int64_t>(era)*146097+yo*365+yo/4-yo/100+(153*adjusted+2)/5+static_cast<unsigned>(day)-1-719468;
    Timestamp value{civil*86400+hour*3600+minute*60+second-offset,fraction};if(!ValidTimestamp(value))return false;output=value;return true;
}
inline std::optional<Timestamp> SnowflakeTimestamp(const std::wstring& id){std::uint64_t n=0;if(!Snowflake(id,n))return {};const auto ms=(n>>22)+1420070400000ULL;return Timestamp{static_cast<std::int64_t>(ms/1000),static_cast<std::uint32_t>(ms%1000)*1000000};}

enum class FieldType { Choice,Boolean,Integer,Snowflake,Text,Message };
struct Field {const wchar_t* key;const wchar_t* fallback;FieldType type;int minimum=0,maximum=0;const wchar_t* choices=L"";};
inline constexpr Field Fields[]={
    {L"View",L"Presence",FieldType::Choice,0,0,L"Presence|Channel|Profile"},
    {L"Connection",L"IPC",FieldType::Choice,0,0,L"IPC|RPC|OAuth|SocialSDK|Bot|UserTokenExperimental"},
    {L"AccountId",L"",FieldType::Snowflake},{L"GuildId",L"",FieldType::Snowflake},{L"ChannelId",L"",FieldType::Snowflake},
    {L"ShowMessageAuthor",L"1",FieldType::Boolean},{L"ShowMessageTime",L"1",FieldType::Boolean},{L"TimeFormat",L"System",FieldType::Choice,0,0,L"System|12|24|ISO"},
    {L"ShowUsername",L"1",FieldType::Boolean},{L"ShowPresence",L"1",FieldType::Boolean},{L"AvatarScalePercent",L"100",FieldType::Integer,25,200},
    {L"ShowMessageContent",L"1",FieldType::Boolean},{L"PreviewCharacters",L"160",FieldType::Integer,1,2000},
    {L"MessageLimit",L"25",FieldType::Integer,1,100},{L"MaximumMessageCharacters",L"2000",FieldType::Integer,1,4000},
    {L"FriendsLimit",L"5",FieldType::Integer,0,100},{L"FriendsCacheLimit",L"1000",FieldType::Integer,1,10000},
    {L"FriendsFilter",L"Online",FieldType::Choice,0,0,L"Online|Active|All"},
    {L"AvatarPlacement",L"Right",FieldType::Choice,0,0,L"Right|None"},
    {L"ShowMentionCount",L"1",FieldType::Boolean},{L"ShowPlatforms",L"1",FieldType::Boolean},
    {L"ClickAction",L"Compose",FieldType::Choice,0,0,L"Compose|OpenChannel|OpenProfile|SendConfigured"},
    {L"ConfiguredText",L"",FieldType::Message,0,4000},{L"DirectSendEnabled",L"0",FieldType::Boolean},
    {L"CountOwnMessages",L"0",FieldType::Boolean},{L"IncludeRoleMentions",L"0",FieldType::Boolean},{L"IncludeEveryoneMentions",L"0",FieldType::Boolean},
    {L"RetainLastGood",L"1",FieldType::Boolean},{L"ShowLastSuccessAge",L"1",FieldType::Boolean},
    {L"EventHistoryLimit",L"256",FieldType::Integer,16,4096},{L"SendHistoryLimit",L"32",FieldType::Integer,1,128},
    {L"SendCharacterLimit",L"2000",FieldType::Integer,1,4000},
    {L"SendAllowUserMentions",L"0",FieldType::Boolean},{L"SendAllowRoleMentions",L"0",FieldType::Boolean},{L"SendAllowEveryoneMentions",L"0",FieldType::Boolean},
    {L"ExperimentalAcknowledged",L"0",FieldType::Boolean},{L"AcknowledgedProfileKey",L"",FieldType::Text,0,2048},
    {L"AcknowledgedAccountId",L"",FieldType::Snowflake},{L"WarnEveryConnection",L"0",FieldType::Boolean},
    {L"RefreshMs",L"30000",FieldType::Integer,1000,86400000},{L"OperationTimeoutMs",L"5000",FieldType::Integer,100,30000},
    {L"ReconnectMinimumMs",L"1000",FieldType::Integer,1000,3600000},{L"ReconnectMaximumMs",L"60000",FieldType::Integer,1000,3600000},
    {L"MaximumBytes",L"262144",FieldType::Integer,1024,1048576},{L"UseGateway",L"1",FieldType::Boolean},{L"AllowReviewedSend",L"0",FieldType::Boolean}
};
inline bool BoundedInteger(const std::wstring& value,int low,int high,int& output){if(value.empty()||value.size()>9)return false;int n=0;for(auto c:value){if(c<L'0'||c>L'9'||n>high/10||(n==high/10&&c-L'0'>high%10))return false;n=n*10+static_cast<int>(c-L'0');}if(n<low)return false;output=n;return true;}
inline bool NormalizeSetting(std::wstring key,std::wstring& value,std::wstring& error){
    key=Lower(key);if(key.compare(0,14,L"discordreader.")==0)key.erase(0,14);else if(key.compare(0,8,L"discord.")==0)key.erase(0,8);const Field* field=nullptr;
    for(const auto& known:Fields)if(Lower(known.key)==key){field=&known;break;}
    const auto fail=[&]{error=L"DiscordReader."+std::wstring(field?field->key:L"setting")+L": unsupported value or setting.";return false;};if(!field)return fail();auto normalized=Trim(value);
    if(field->type==FieldType::Message){normalized=value;if(!SafeDraftText(normalized,static_cast<std::size_t>(field->maximum)))return fail();}
    else if(field->type==FieldType::Boolean){const auto low=Lower(normalized);if(low==L"1"||low==L"true"||low==L"yes"||low==L"on")normalized=L"1";else if(low==L"0"||low==L"false"||low==L"no"||low==L"off")normalized=L"0";else return fail();}
    else if(field->type==FieldType::Integer){int n=0;if(!BoundedInteger(normalized,field->minimum,field->maximum,n))return fail();normalized=std::to_wstring(n);}
    else if(field->type==FieldType::Snowflake){if(!normalized.empty()&&!Snowflake(normalized))return fail();}
    else if(field->type==FieldType::Text){if(!SafeSingleLine(normalized,static_cast<std::size_t>(field->maximum)))return fail();}
    else{const std::wstring choices=field->choices;std::size_t start=0;bool found=false;for(;;){const auto end=choices.find(L'|',start);const auto choice=choices.substr(start,end==std::wstring::npos?end:end-start);if(Lower(choice)==Lower(normalized)){normalized=choice;found=true;break;}if(end==std::wstring::npos)break;start=end+1;}if(!found)return fail();}
    value=std::move(normalized);error.clear();return true;
}
struct Configuration {
    std::wstring view=L"Presence",connection=L"IPC",accountId,guildId,channelId,friendsFilter=L"Online",avatarPlacement=L"Right",clickAction=L"Compose";
    bool showMessageContent=true,showMentionCount=true,showPlatforms=true,countOwnMessages=false,includeRoleMentions=false,includeEveryoneMentions=false,retainLastGood=true,showLastSuccessAge=true;
    int previewCharacters=160,messageLimit=25,maximumMessageCharacters=2000,friendsLimit=5,friendsCacheLimit=1000,eventHistoryLimit=256,sendHistoryLimit=32,sendCharacterLimit=2000;
    bool sendAllowUserMentions=false,sendAllowRoleMentions=false,sendAllowEveryoneMentions=false,experimentalAcknowledged=false,warnEveryConnection=false;
    std::wstring acknowledgedProfileKey,acknowledgedAccountId;
    std::wstring configuredText;bool directSendEnabled=false;
    int refreshMs=30000,operationTimeoutMs=5000,reconnectMinimumMs=1000,reconnectMaximumMs=60000,maximumBytes=262144;
    bool useGateway=true,allowReviewedSend=false;
    bool showMessageAuthor=true,showMessageTime=true,showUsername=true,showPresence=true;
    std::wstring timeFormat=L"System";int avatarScalePercent=100;
};
template<class Reader> bool ReadConfiguration(Reader read,Configuration& output,std::wstring& error){Configuration c;
    for(const auto& field:Fields){auto value=read(field.key,field.fallback);if(!NormalizeSetting(field.key,value,error))return false;const std::wstring k=field.key;
        if(k==L"View")c.view=value;else if(k==L"Connection")c.connection=value;else if(k==L"AccountId")c.accountId=value;else if(k==L"GuildId")c.guildId=value;else if(k==L"ChannelId")c.channelId=value;
        else if(k==L"FriendsFilter")c.friendsFilter=value;else if(k==L"AvatarPlacement")c.avatarPlacement=value;else if(k==L"ClickAction")c.clickAction=value;
        else if(k==L"AcknowledgedProfileKey")c.acknowledgedProfileKey=value;else if(k==L"AcknowledgedAccountId")c.acknowledgedAccountId=value;
        else if(k==L"ConfiguredText")c.configuredText=value;
        else if(k==L"TimeFormat")c.timeFormat=value;
        else if(field.type==FieldType::Integer){int n=0;BoundedInteger(value,field.minimum,field.maximum,n);if(k==L"PreviewCharacters")c.previewCharacters=n;else if(k==L"MessageLimit")c.messageLimit=n;else if(k==L"MaximumMessageCharacters")c.maximumMessageCharacters=n;else if(k==L"FriendsLimit")c.friendsLimit=n;else if(k==L"FriendsCacheLimit")c.friendsCacheLimit=n;else if(k==L"EventHistoryLimit")c.eventHistoryLimit=n;else if(k==L"SendHistoryLimit")c.sendHistoryLimit=n;else if(k==L"RefreshMs")c.refreshMs=n;else if(k==L"OperationTimeoutMs")c.operationTimeoutMs=n;else if(k==L"ReconnectMinimumMs")c.reconnectMinimumMs=n;else if(k==L"ReconnectMaximumMs")c.reconnectMaximumMs=n;else if(k==L"MaximumBytes")c.maximumBytes=n;else if(k==L"AvatarScalePercent")c.avatarScalePercent=n;else c.sendCharacterLimit=n;}
        else {const bool b=value==L"1";if(k==L"ShowMessageContent")c.showMessageContent=b;else if(k==L"ShowMentionCount")c.showMentionCount=b;else if(k==L"ShowPlatforms")c.showPlatforms=b;else if(k==L"CountOwnMessages")c.countOwnMessages=b;else if(k==L"IncludeRoleMentions")c.includeRoleMentions=b;else if(k==L"IncludeEveryoneMentions")c.includeEveryoneMentions=b;else if(k==L"RetainLastGood")c.retainLastGood=b;else if(k==L"ShowLastSuccessAge")c.showLastSuccessAge=b;else if(k==L"SendAllowUserMentions")c.sendAllowUserMentions=b;else if(k==L"SendAllowRoleMentions")c.sendAllowRoleMentions=b;else if(k==L"SendAllowEveryoneMentions")c.sendAllowEveryoneMentions=b;else if(k==L"ExperimentalAcknowledged")c.experimentalAcknowledged=b;else if(k==L"DirectSendEnabled")c.directSendEnabled=b;else if(k==L"UseGateway")c.useGateway=b;else if(k==L"AllowReviewedSend")c.allowReviewedSend=b;else if(k==L"ShowMessageAuthor")c.showMessageAuthor=b;else if(k==L"ShowMessageTime")c.showMessageTime=b;else if(k==L"ShowUsername")c.showUsername=b;else if(k==L"ShowPresence")c.showPresence=b;else c.warnEveryConnection=b;}
    }if(c.reconnectMaximumMs<c.reconnectMinimumMs){error=L"DiscordReader.ReconnectMaximumMs must be at least ReconnectMinimumMs.";return false;}output=std::move(c);error.clear();return true;
}
// For shared CLI/INI/GUI adapters. Values may contain the private configured
// message; do not log this map. ConfigurationKey exposes only a cache fingerprint.
inline std::map<std::wstring,std::wstring> ConfigurationValues(const Configuration& c){
    const auto b=[](bool v){return v?std::wstring(L"1"):std::wstring(L"0");};
    return {{L"View",c.view},{L"Connection",c.connection},{L"AccountId",c.accountId},{L"GuildId",c.guildId},{L"ChannelId",c.channelId},
        {L"ShowMessageContent",b(c.showMessageContent)},{L"ShowMessageAuthor",b(c.showMessageAuthor)},{L"ShowMessageTime",b(c.showMessageTime)},{L"TimeFormat",c.timeFormat},
        {L"ShowUsername",b(c.showUsername)},{L"ShowPresence",b(c.showPresence)},{L"AvatarScalePercent",std::to_wstring(c.avatarScalePercent)},
        {L"PreviewCharacters",std::to_wstring(c.previewCharacters)},{L"MessageLimit",std::to_wstring(c.messageLimit)},
        {L"MaximumMessageCharacters",std::to_wstring(c.maximumMessageCharacters)},{L"FriendsLimit",std::to_wstring(c.friendsLimit)},{L"FriendsCacheLimit",std::to_wstring(c.friendsCacheLimit)},
        {L"FriendsFilter",c.friendsFilter},{L"AvatarPlacement",c.avatarPlacement},{L"ShowMentionCount",b(c.showMentionCount)},{L"ShowPlatforms",b(c.showPlatforms)},
        {L"ClickAction",c.clickAction},{L"ConfiguredText",c.configuredText},{L"DirectSendEnabled",b(c.directSendEnabled)},{L"CountOwnMessages",b(c.countOwnMessages)},
        {L"IncludeRoleMentions",b(c.includeRoleMentions)},{L"IncludeEveryoneMentions",b(c.includeEveryoneMentions)},{L"RetainLastGood",b(c.retainLastGood)},
        {L"ShowLastSuccessAge",b(c.showLastSuccessAge)},{L"EventHistoryLimit",std::to_wstring(c.eventHistoryLimit)},{L"SendHistoryLimit",std::to_wstring(c.sendHistoryLimit)},
        {L"SendCharacterLimit",std::to_wstring(c.sendCharacterLimit)},{L"SendAllowUserMentions",b(c.sendAllowUserMentions)},{L"SendAllowRoleMentions",b(c.sendAllowRoleMentions)},
        {L"SendAllowEveryoneMentions",b(c.sendAllowEveryoneMentions)},{L"ExperimentalAcknowledged",b(c.experimentalAcknowledged)},{L"AcknowledgedProfileKey",c.acknowledgedProfileKey},
        {L"AcknowledgedAccountId",c.acknowledgedAccountId},{L"WarnEveryConnection",b(c.warnEveryConnection)},{L"RefreshMs",std::to_wstring(c.refreshMs)},
        {L"OperationTimeoutMs",std::to_wstring(c.operationTimeoutMs)},{L"ReconnectMinimumMs",std::to_wstring(c.reconnectMinimumMs)},{L"ReconnectMaximumMs",std::to_wstring(c.reconnectMaximumMs)},
        {L"MaximumBytes",std::to_wstring(c.maximumBytes)},{L"UseGateway",b(c.useGateway)},{L"AllowReviewedSend",b(c.allowReviewedSend)}};
}
inline std::optional<std::wstring> SettingValue(const Configuration& c,std::wstring key){key=Lower(key);if(key.compare(0,14,L"discordreader.")==0)key.erase(0,14);else if(key.compare(0,8,L"discord.")==0)key.erase(0,8);for(const auto& item:ConfigurationValues(c))if(Lower(item.first)==key)return item.second;return {};}
inline std::wstring ConfigurationKey(const Configuration& c){
    std::uint64_t hash=14695981039346656037ULL;const auto byte=[&](unsigned n){hash^=static_cast<std::uint8_t>(n);hash*=1099511628211ULL;};
    const auto unit=[&](unsigned n){byte(n&255);byte((n>>8)&255);};
    const auto text=[&](const std::wstring& value){std::size_t count=0;Utf16Length(value,count);for(unsigned shift=0;shift<64;shift+=8)byte(static_cast<unsigned>((static_cast<std::uint64_t>(count)>>shift)&255));
        for(auto wc:value){const auto n=static_cast<std::uint32_t>(wc);if(n>0xffff&&n<=0x10ffff){const auto v=n-0x10000;unit(0xd800+(v>>10));unit(0xdc00+(v&1023));}else unit(n);}};
    for(const auto& item:ConfigurationValues(c)){text(item.first);text(item.second);}static constexpr wchar_t hex[]=L"0123456789ABCDEF";std::wstring result=L"DiscordReader1:";
    for(int shift=60;shift>=0;shift-=4)result+=hex[(hash>>shift)&15];return result;
}
struct Binding {
    std::wstring profileKey,accountId,guildId,channelId;
    std::uint64_t connectionGeneration=0,configRevision=0;
};
inline bool operator==(const Binding& a,const Binding& b){return a.profileKey==b.profileKey&&a.accountId==b.accountId&&a.guildId==b.guildId&&a.channelId==b.channelId&&a.connectionGeneration==b.connectionGeneration&&a.configRevision==b.configRevision;}
inline bool ValidBinding(const Binding& b){return !b.profileKey.empty()&&SafeSingleLine(b.profileKey,2048)&&Snowflake(b.accountId)&&(!b.guildId.empty()?Snowflake(b.guildId):true)&&(!b.channelId.empty()?Snowflake(b.channelId):true)&&b.connectionGeneration&&b.configRevision;}
inline bool BindingMatchesConfiguration(const Binding& b,const Configuration& c){return ValidBinding(b)&&(c.accountId.empty()||c.accountId==b.accountId)&&(c.guildId.empty()||c.guildId==b.guildId)&&(c.channelId.empty()||c.channelId==b.channelId);}
struct Acknowledgement {std::wstring profileKey,accountId,connection;std::uint64_t connectionGeneration=0;unsigned policyVersion=1;};
inline bool ExperimentalAcknowledged(const Configuration& c,const Binding& b,const Acknowledgement* session=nullptr){
    if(c.connection!=L"UserTokenExperimental")return true;
    if(!ValidBinding(b))return false;
    if(session&&session->policyVersion==1&&session->profileKey==b.profileKey&&session->accountId==b.accountId&&session->connection==c.connection&&session->connectionGeneration==b.connectionGeneration)return true;
    return !c.warnEveryConnection&&c.experimentalAcknowledged&&c.acknowledgedProfileKey==b.profileKey&&c.acknowledgedAccountId==b.accountId;
}
inline constexpr const wchar_t* ExperimentalWarning=L"Discord prohibits automation of normal user accounts outside its approved OAuth/bot interfaces and may terminate the account. This personal-token connection is experimental. Connect only after reviewing the account and access involved.";

enum class Backend { IPC,RPC,OAuth,SocialSDK,Bot,UserTokenExperimental };
inline std::optional<Backend> BackendFor(const std::wstring& value){if(value==L"IPC")return Backend::IPC;if(value==L"RPC")return Backend::RPC;if(value==L"OAuth")return Backend::OAuth;if(value==L"SocialSDK")return Backend::SocialSDK;if(value==L"Bot")return Backend::Bot;if(value==L"UserTokenExperimental")return Backend::UserTokenExperimental;return {};}
inline bool ValidConfiguration(const Configuration& c){return BackendFor(c.connection).has_value()&&(c.view==L"Presence"||c.view==L"Channel"||c.view==L"Profile")&&
    (c.accountId.empty()||Snowflake(c.accountId))&&(c.guildId.empty()||Snowflake(c.guildId))&&(c.channelId.empty()||Snowflake(c.channelId))&&
    c.previewCharacters>=1&&c.previewCharacters<=2000&&c.messageLimit>=1&&c.messageLimit<=100&&c.maximumMessageCharacters>=1&&c.maximumMessageCharacters<=4000&&
    c.friendsLimit>=0&&c.friendsLimit<=100&&c.friendsCacheLimit>=1&&c.friendsCacheLimit<=10000&&c.eventHistoryLimit>=16&&c.eventHistoryLimit<=4096&&
    c.sendHistoryLimit>=1&&c.sendHistoryLimit<=128&&c.sendCharacterLimit>=1&&c.sendCharacterLimit<=4000&&
    c.refreshMs>=1000&&c.refreshMs<=86400000&&c.operationTimeoutMs>=100&&c.operationTimeoutMs<=30000&&
    c.reconnectMinimumMs>=1000&&c.reconnectMinimumMs<=3600000&&c.reconnectMaximumMs>=c.reconnectMinimumMs&&c.reconnectMaximumMs<=3600000&&c.maximumBytes>=1024&&c.maximumBytes<=1048576&&
    c.avatarScalePercent>=25&&c.avatarScalePercent<=200&&(c.timeFormat==L"System"||c.timeFormat==L"12"||c.timeFormat==L"24"||c.timeFormat==L"ISO")&&
    (c.friendsFilter==L"Online"||c.friendsFilter==L"Active"||c.friendsFilter==L"All")&&(c.avatarPlacement==L"Right"||c.avatarPlacement==L"None")&&
    (c.clickAction==L"Compose"||c.clickAction==L"OpenChannel"||c.clickAction==L"OpenProfile"||c.clickAction==L"SendConfigured")&&SafeSingleLine(c.acknowledgedProfileKey,2048)&&
    (c.acknowledgedAccountId.empty()||Snowflake(c.acknowledgedAccountId))&&SafeDraftText(c.configuredText,4000);}
enum class Status { Unknown,Online,Idle,DoNotDisturb,Offline,Invisible };
inline bool ValidStatus(Status s){return s==Status::Unknown||s==Status::Online||s==Status::Idle||s==Status::DoNotDisturb||s==Status::Offline||s==Status::Invisible;}
inline bool VisibleOnline(Status s){return s==Status::Online||s==Status::Idle||s==Status::DoNotDisturb;}
struct Platforms {std::optional<Status> desktop,mobile,web;};
struct Presence {Status status=Status::Unknown;std::optional<Platforms> platforms;};
struct PresencePatch {std::wstring userId;std::optional<Status> status;std::optional<Platforms> platforms;bool platformSnapshot=false;};
struct UserPatch {std::wstring id;std::optional<std::wstring> displayName,username,avatarHash;};
struct User {std::wstring id;std::optional<std::wstring> displayName,username,avatarHash;};
inline bool ValidAvatarHash(const std::wstring& hash){if(hash.empty())return true;const auto start=hash.compare(0,2,L"a_")==0?2u:0u;return hash.size()==start+32&&std::all_of(hash.begin()+start,hash.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');});}
inline std::wstring AvatarUrl(const User& user,unsigned side=128){if(!Snowflake(user.id)||!user.avatarHash||user.avatarHash->empty()||!ValidAvatarHash(*user.avatarHash))return L"";side=(std::max)(16u,(std::min)(1024u,side));unsigned power=16;while(power<side)power*=2;return L"https://cdn.discordapp.com/avatars/"+user.id+L"/"+*user.avatarHash+L".png?size="+std::to_wstring(power);}
inline void MergeUser(User& user,const UserPatch& patch){user.id=patch.id;if(patch.displayName)user.displayName=DisplayText(*patch.displayName,128);if(patch.username)user.username=DisplayText(*patch.username,128);if(patch.avatarHash&&ValidAvatarHash(*patch.avatarHash))user.avatarHash=Lower(*patch.avatarHash);}
struct Capabilities {
    Backend backend=Backend::IPC;
    bool identity=false,channelMessages=false,messageContent=false,personalReadState=false,personalMentions=false,friends=false,presence=false,platformPresence=false,canSend=false,enforceNonce=false;
    unsigned maximumSendCharacters=2000;
};
inline Capabilities HonestCapabilities(Capabilities c){if(c.backend==Backend::Bot||c.backend==Backend::IPC){c.personalReadState=false;c.personalMentions=false;c.friends=false;}c.maximumSendCharacters=(std::max)(1u,(std::min)(4000u,c.maximumSendCharacters));if(!c.presence)c.platformPresence=false;return c;}
struct Mentions {std::optional<bool> direct,role,everyone;};
struct MessagePatch {
    std::wstring id,channelId;
    std::optional<std::wstring> authorId,authorName,content;
    std::optional<Timestamp> sentAt,editedAt;
    Mentions mentions;
};
struct Message {std::wstring id,channelId;std::optional<std::wstring> authorId,authorName,content;std::optional<Timestamp> sentAt,editedAt;Mentions mentions;};
enum class Relationship {None,Friend,Blocked,PendingIncoming,PendingOutgoing,Other};
struct Friend {User user;Presence presence;};
enum class Failure {None,Transient,Authentication,Permission,Disconnected,InvalidData};
struct Count {std::uint64_t value=0;bool exact=false;};
struct ReadState {std::wstring channelId;std::optional<std::wstring> lastReadMessageId;std::optional<std::uint64_t> unreadCount,mentionCount;};
struct Snapshot {
    Binding binding;Capabilities capabilities;User user;Presence presence;
    std::vector<Message> messages;std::vector<Friend> friends;
    std::optional<Count> personalUnread,personalMentions,localNewMessages,localMentions,onlineFriends;
    std::optional<std::wstring> lastReadMessageId;
    bool baselineEstablished=false,historyComplete=false,friendsComplete=false,hasSuccess=false,stale=false,connectionReady=false;
    Milliseconds lastSuccess=0;Failure failure=Failure::None;std::uint64_t lastSequence=0;
};
enum class EventKind {Capabilities,Identity,MessageCreate,MessageUpdate,MessageDelete,ReadState,Presence,Relationship,FriendsBegin,FriendsComplete,SyncComplete,Gap,Failure};
enum class Origin {Live,InitialHistory,RecoveryHistory};
struct Event {
    Binding binding;std::uint64_t sequence=0;Milliseconds observedAt=0;EventKind kind=EventKind::SyncComplete;Origin origin=Origin::Live;
    Capabilities capabilities;UserPatch user;MessagePatch message;std::wstring messageId;ReadState readState;PresencePatch presence;
    Relationship relationship=Relationship::None;std::optional<std::wstring> baselineWatermark;bool complete=true;Failure failure=Failure::None;
};
enum class ApplyResult {Applied,Ignored,Invalid};
class Reducer {
    Configuration cfg_;Snapshot state_;
    std::map<std::uint64_t,Message> messages_;
    std::map<std::uint64_t,std::optional<bool>> counted_; // unknown mention data is not an exact zero
    std::set<std::uint64_t> liveBeforeBaseline_,liveObserved_,tombstones_;
    std::deque<std::uint64_t> tombstoneOrder_;
    std::map<std::uint64_t,Friend> friends_;
    std::map<std::uint64_t,Friend> friendsStaging_;
    std::uint64_t historicalHighWater_=0,baseline_=0,evictionFloor_=0;
    bool localExact_=true,friendsBatch_=false,friendsTruncated_=false;
    std::optional<bool> Mentioned(const Message& m)const{if(m.mentions.direct.value_or(false)||(cfg_.includeRoleMentions&&m.mentions.role.value_or(false))||(cfg_.includeEveryoneMentions&&m.mentions.everyone.value_or(false)))return true;
        if(!m.mentions.direct||(cfg_.includeRoleMentions&&!m.mentions.role)||(cfg_.includeEveryoneMentions&&!m.mentions.everyone))return {};return false;}
    void CountMessage(std::uint64_t id,const Message& m){
        if(!state_.baselineEstablished||id<=baseline_||tombstones_.count(id))return;
        if(!m.authorId){localExact_=false;return;}if(!cfg_.countOwnMessages&&*m.authorId==state_.binding.accountId){counted_.erase(id);return;}
        counted_[id]=Mentioned(m);while(counted_.size()>static_cast<std::size_t>(cfg_.eventHistoryLimit)){counted_.erase(counted_.begin());localExact_=false;}
    }
    void ClearContent(){for(auto& entry:messages_)entry.second.content.reset();}
    void ClearChannel(){messages_.clear();counted_.clear();liveBeforeBaseline_.clear();liveObserved_.clear();tombstones_.clear();tombstoneOrder_.clear();historicalHighWater_=baseline_=evictionFloor_=0;localExact_=true;state_.baselineEstablished=state_.historyComplete=false;state_.personalUnread.reset();state_.personalMentions.reset();state_.lastReadMessageId.reset();}
    void ClearData(){ClearChannel();friends_.clear();friendsStaging_.clear();friendsBatch_=friendsTruncated_=false;
        state_.user={};state_.presence={};state_.personalUnread.reset();state_.personalMentions.reset();state_.lastReadMessageId.reset();state_.baselineEstablished=state_.historyComplete=state_.friendsComplete=false;}
    void ApplyPresence(Presence& p,const PresencePatch& patch){if(patch.status)p.status=*patch.status;if(state_.capabilities.platformPresence){
        if(patch.status&&(*patch.status==Status::Offline||(*patch.status==Status::Invisible&&!patch.platforms)))p.platforms=Platforms{};
        else if(patch.platforms){if(patch.platformSnapshot)p.platforms=patch.platforms;else if(patch.platforms->desktop||patch.platforms->mobile||patch.platforms->web){
            if(!p.platforms)p.platforms=Platforms{};if(patch.platforms->desktop)p.platforms->desktop=patch.platforms->desktop;if(patch.platforms->mobile)p.platforms->mobile=patch.platforms->mobile;if(patch.platforms->web)p.platforms->web=patch.platforms->web;}}}}
public:
    bool Reset(const Binding& binding,const Configuration& cfg,std::wstring& error){
        if(!BindingMatchesConfiguration(binding,cfg)) {error=L"Discord snapshot binding does not match the selected profile/account/channel.";return false;}
        if(!ValidConfiguration(cfg)){error=L"Discord configuration is outside the validated schema.";return false;}
        cfg_=cfg;state_={};state_.binding=binding;state_.capabilities.backend=*BackendFor(cfg.connection);ClearData();error.clear();return true;
    }
    void Clear(){cfg_={};state_={};ClearData();}
    bool Reconnect(const Binding& binding){auto expected=state_.binding;expected.connectionGeneration=binding.connectionGeneration;
        if(!ValidBinding(binding)||!(expected==binding)||binding.connectionGeneration<=state_.binding.connectionGeneration)return false;
        state_.binding=binding;state_.lastSequence=0;state_.stale=state_.hasSuccess;state_.historyComplete=state_.friendsComplete=state_.connectionReady=false;friendsStaging_.clear();friendsBatch_=false;
        state_.personalUnread.reset();state_.personalMentions.reset();if(!cfg_.retainLastGood)ClearData();localExact_=false;return true;
    }
    bool ResetLocalBaseline(const Binding& binding,const std::wstring& cursor){std::uint64_t n=0;if(!(binding==state_.binding)||binding.channelId.empty()||!state_.capabilities.channelMessages||!Snowflake(cursor,n,true))return false;baseline_=n;counted_.clear();liveBeforeBaseline_.clear();liveObserved_.clear();localExact_=true;state_.baselineEstablished=true;return true;}
    ApplyResult Apply(const Event& e){
        if(!ValidBinding(state_.binding)||!(e.binding==state_.binding)||!e.sequence||e.sequence<=state_.lastSequence)return ApplyResult::Ignored;
        const auto finish=[&]{state_.lastSequence=e.sequence;return ApplyResult::Applied;};
        switch(e.kind){
        case EventKind::Capabilities:{if(e.capabilities.backend!=*BackendFor(cfg_.connection))return ApplyResult::Invalid;state_.capabilities=HonestCapabilities(e.capabilities);
            if(!state_.capabilities.messageContent)ClearContent();if(!state_.capabilities.channelMessages)ClearChannel();
            if(!state_.capabilities.identity)state_.user={};if(!state_.capabilities.personalReadState){state_.personalUnread.reset();state_.lastReadMessageId.reset();}
            if(!state_.capabilities.personalMentions)state_.personalMentions.reset();if(!state_.capabilities.friends){friends_.clear();friendsStaging_.clear();friendsBatch_=false;state_.friendsComplete=false;}
            if(!state_.capabilities.presence)state_.presence={};else if(!state_.capabilities.platformPresence){state_.presence.platforms.reset();for(auto& f:friends_)f.second.presence.platforms.reset();}
            return finish();}
        case EventKind::Identity:{std::uint64_t id=0;if(!Snowflake(e.user.id,id))return ApplyResult::Invalid;
            if(e.user.id==state_.binding.accountId){if(!state_.capabilities.identity)return ApplyResult::Ignored;MergeUser(state_.user,e.user);}
            else {auto found=friends_.find(id);auto staged=friendsStaging_.find(id);if(found==friends_.end()&&staged==friendsStaging_.end())return ApplyResult::Ignored;if(found!=friends_.end())MergeUser(found->second.user,e.user);if(staged!=friendsStaging_.end())MergeUser(staged->second.user,e.user);}return finish();}
        case EventKind::MessageCreate:case EventKind::MessageUpdate:{
            if(!state_.capabilities.channelMessages||e.message.channelId!=state_.binding.channelId||state_.binding.channelId.empty())return ApplyResult::Ignored;
            std::uint64_t id=0;if(!Snowflake(e.message.id,id)||(e.message.authorId&&!Snowflake(*e.message.authorId))||(e.message.sentAt&&!ValidTimestamp(*e.message.sentAt))||(e.message.editedAt&&!ValidTimestamp(*e.message.editedAt)))return ApplyResult::Invalid;
            if(tombstones_.count(id)||id<=evictionFloor_){if(e.origin==Origin::Live&&state_.baselineEstablished&&id>baseline_)localExact_=false;return finish();}
            auto found=messages_.find(id);const bool existed=found!=messages_.end();if(!existed){Message m;m.id=e.message.id;m.channelId=e.message.channelId;m.sentAt=SnowflakeTimestamp(e.message.id);found=messages_.emplace(id,std::move(m)).first;}
            auto& m=found->second;const bool older=m.editedAt&&(!e.message.editedAt||*e.message.editedAt<*m.editedAt);
            const bool overwrite=e.origin==Origin::Live?!older:(e.message.editedAt&&(!m.editedAt||*m.editedAt<*e.message.editedAt));
            if(e.message.authorId&&(!m.authorId||overwrite))m.authorId=e.message.authorId;if(e.message.authorName&&(!m.authorName||overwrite))m.authorName=DisplayText(*e.message.authorName,128);
            if(e.message.sentAt)m.sentAt=e.message.sentAt;if(e.message.editedAt&&(!m.editedAt||*m.editedAt<*e.message.editedAt))m.editedAt=e.message.editedAt;
            if(cfg_.showMessageContent&&state_.capabilities.messageContent&&e.message.content&&(!m.content||overwrite))m.content=DisplayText(*e.message.content,static_cast<std::size_t>(cfg_.maximumMessageCharacters),true);
            const auto mergeBool=[&](std::optional<bool>& target,const std::optional<bool>& supplied){if(supplied&&(!target||overwrite))target=supplied;};
            mergeBool(m.mentions.direct,e.message.mentions.direct);mergeBool(m.mentions.role,e.message.mentions.role);mergeBool(m.mentions.everyone,e.message.mentions.everyone);
            if(e.origin==Origin::InitialHistory)historicalHighWater_=(std::max)(historicalHighWater_,id);
            else if(e.kind==EventKind::MessageCreate||e.origin==Origin::RecoveryHistory){if(e.origin==Origin::RecoveryHistory)localExact_=false;liveObserved_.insert(id);while(liveObserved_.size()>static_cast<std::size_t>(cfg_.eventHistoryLimit)){liveObserved_.erase(liveObserved_.begin());localExact_=false;}if(!state_.baselineEstablished){liveBeforeBaseline_.insert(id);while(liveBeforeBaseline_.size()>static_cast<std::size_t>(cfg_.eventHistoryLimit)){liveBeforeBaseline_.erase(liveBeforeBaseline_.begin());localExact_=false;}}else CountMessage(id,m);}
            else if(liveObserved_.count(id))CountMessage(id,m);
            while(messages_.size()>static_cast<std::size_t>(cfg_.messageLimit)){evictionFloor_=(std::max)(evictionFloor_,messages_.begin()->first);messages_.erase(messages_.begin());}
            return finish();}
        case EventKind::MessageDelete:{if(!state_.capabilities.channelMessages||e.message.channelId!=state_.binding.channelId)return ApplyResult::Ignored;std::uint64_t id=0;if(!Snowflake(e.messageId,id))return ApplyResult::Invalid;
            messages_.erase(id);counted_.erase(id);liveBeforeBaseline_.erase(id);liveObserved_.erase(id);if(tombstones_.insert(id).second)tombstoneOrder_.push_back(id);
            while(tombstoneOrder_.size()>static_cast<std::size_t>(cfg_.eventHistoryLimit)){evictionFloor_=(std::max)(evictionFloor_,tombstoneOrder_.front());tombstones_.erase(tombstoneOrder_.front());tombstoneOrder_.pop_front();}return finish();}
        case EventKind::ReadState:{if(state_.binding.channelId.empty()||e.readState.channelId!=state_.binding.channelId||!state_.capabilities.personalReadState)return ApplyResult::Ignored;
            if(e.readState.lastReadMessageId){std::uint64_t cursor=0;if(!Snowflake(*e.readState.lastReadMessageId,cursor,true))return ApplyResult::Invalid;
                if(state_.lastReadMessageId!=e.readState.lastReadMessageId){state_.personalUnread.reset();state_.personalMentions.reset();}state_.lastReadMessageId=e.readState.lastReadMessageId;}
            if(e.readState.unreadCount)state_.personalUnread=Count{*e.readState.unreadCount,true};if(e.readState.mentionCount&&state_.capabilities.personalMentions)state_.personalMentions=Count{*e.readState.mentionCount,true};return finish();}
        case EventKind::Presence:{if(!state_.capabilities.presence||!Snowflake(e.presence.userId))return ApplyResult::Ignored;
            if((e.presence.status&&!ValidStatus(*e.presence.status))||(e.presence.platforms&&
                ((e.presence.platforms->desktop&&!ValidStatus(*e.presence.platforms->desktop))||(e.presence.platforms->mobile&&!ValidStatus(*e.presence.platforms->mobile))||(e.presence.platforms->web&&!ValidStatus(*e.presence.platforms->web)))))return ApplyResult::Invalid;
            if(e.presence.userId==state_.binding.accountId)ApplyPresence(state_.presence,e.presence);else{std::uint64_t id=0;Snowflake(e.presence.userId,id);auto f=friends_.find(id),staged=friendsStaging_.find(id);if(f==friends_.end()&&staged==friendsStaging_.end())return ApplyResult::Ignored;if(f!=friends_.end())ApplyPresence(f->second.presence,e.presence);if(staged!=friendsStaging_.end())ApplyPresence(staged->second.presence,e.presence);}return finish();}
        case EventKind::Relationship:{if(!state_.capabilities.friends)return ApplyResult::Ignored;std::uint64_t id=0;if(!Snowflake(e.user.id,id)||e.user.id==state_.binding.accountId)return ApplyResult::Invalid;
            if(e.relationship!=Relationship::Friend){friends_.erase(id);friendsStaging_.erase(id);return finish();}
            auto& target=friendsBatch_?friendsStaging_:friends_;auto f=target.find(id);if(f==target.end()){if(target.size()>=static_cast<std::size_t>(cfg_.friendsCacheLimit)){friendsTruncated_=true;state_.friendsComplete=false;return finish();}Friend entry;auto old=friends_.find(id);if(old!=friends_.end())entry=old->second;f=target.emplace(id,std::move(entry)).first;}MergeUser(f->second.user,e.user);return finish();}
        case EventKind::FriendsBegin:if(!state_.capabilities.friends)return ApplyResult::Ignored;friendsStaging_.clear();friendsBatch_=true;friendsTruncated_=false;state_.friendsComplete=false;return finish();
        case EventKind::FriendsComplete:if(!state_.capabilities.friends||!friendsBatch_)return ApplyResult::Ignored;
            if(e.complete)friends_=std::move(friendsStaging_);else for(const auto& f:friendsStaging_){auto old=friends_.find(f.first);if(old!=friends_.end())old->second=f.second;else if(friends_.size()<static_cast<std::size_t>(cfg_.friendsCacheLimit))friends_.insert(f);}
            state_.friendsComplete=e.complete&&!friendsTruncated_;friendsBatch_=false;friendsStaging_.clear();return finish();
        case EventKind::SyncComplete:{if(e.baselineWatermark){std::uint64_t value=0;if(!Snowflake(*e.baselineWatermark,value,true))return ApplyResult::Invalid;if(!state_.baselineEstablished)baseline_=value;}
            else if(!state_.baselineEstablished)baseline_=historicalHighWater_;
            if(!state_.baselineEstablished&&state_.capabilities.channelMessages&&!state_.binding.channelId.empty()){state_.baselineEstablished=true;for(const auto id:liveBeforeBaseline_){const auto m=messages_.find(id);if(m!=messages_.end())CountMessage(id,m->second);else localExact_=false;}liveBeforeBaseline_.clear();}
            state_.historyComplete=e.complete;state_.hasSuccess=true;state_.lastSuccess=e.observedAt;state_.stale=false;state_.connectionReady=true;state_.failure=Failure::None;return finish();}
        case EventKind::Gap:state_.stale=state_.hasSuccess;state_.connectionReady=false;state_.historyComplete=state_.friendsComplete=false;state_.personalUnread.reset();state_.personalMentions.reset();localExact_=false;return finish();
        case EventKind::Failure:state_.failure=e.failure==Failure::None?Failure::InvalidData:e.failure;state_.stale=state_.hasSuccess;state_.connectionReady=false;if(!cfg_.retainLastGood)ClearData();return finish();
        }return ApplyResult::Invalid;
    }
    Snapshot Read()const{Snapshot out=state_;out.messages.clear();for(auto m=messages_.rbegin();m!=messages_.rend();++m)out.messages.push_back(m->second);
        if(out.baselineEstablished){std::uint64_t mentions=0;bool known=true;for(const auto& entry:counted_){if(!entry.second)known=false;else if(*entry.second)++mentions;}out.localNewMessages=Count{static_cast<std::uint64_t>(counted_.size()),localExact_};if(cfg_.showMentionCount)out.localMentions=Count{mentions,localExact_&&known};}
        if(!cfg_.showMentionCount)out.personalMentions.reset();if(!cfg_.showPlatforms)out.presence.platforms.reset();
        std::vector<Friend> visible;std::uint64_t online=0;bool knownPresences=true;for(const auto& entry:friends_){const auto& f=entry.second;if(f.presence.status==Status::Unknown)knownPresences=false;if(VisibleOnline(f.presence.status))++online;
            if(cfg_.friendsFilter==L"All"||(cfg_.friendsFilter==L"Active"?f.presence.status==Status::Online:VisibleOnline(f.presence.status))){auto copy=f;if(!cfg_.showPlatforms)copy.presence.platforms.reset();visible.push_back(std::move(copy));}}
        std::sort(visible.begin(),visible.end(),[](const Friend& a,const Friend& b){const auto rank=[](Status s){return s==Status::Online?0:s==Status::Idle?1:s==Status::DoNotDisturb?2:3;};const auto x=rank(a.presence.status),y=rank(b.presence.status);return x!=y?x<y:a.user.id<b.user.id;});
        if(visible.size()>static_cast<std::size_t>((std::max)(0,cfg_.friendsLimit)))visible.resize(static_cast<std::size_t>((std::max)(0,cfg_.friendsLimit)));out.friends=std::move(visible);
        if(out.capabilities.friends)out.onlineFriends=Count{online,out.friendsComplete&&knownPresences};return out;
    }
};
inline std::optional<std::uint64_t> AgeSeconds(const Snapshot& snapshot,Milliseconds now){if(!snapshot.hasSuccess)return {};return now>=snapshot.lastSuccess?(now-snapshot.lastSuccess)/1000:0;}
inline std::optional<unsigned> VisiblePlatformCount(const Presence& p){if(!p.platforms)return {};unsigned n=0;for(const auto& s:{p.platforms->desktop,p.platforms->mobile,p.platforms->web})if(s&&VisibleOnline(*s))++n;return n;}
inline std::wstring MessagePreview(const Message& m,const Configuration& c){return c.showMessageContent&&m.content?DisplayText(*m.content,static_cast<std::size_t>(c.previewCharacters),true):L"";}

inline bool ValidNonce(const std::wstring& value){return !value.empty()&&value.size()<=25&&std::all_of(value.begin(),value.end(),[](wchar_t c){return (c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'_';});}
inline bool SendTextValid(const std::wstring& text,unsigned maximum){if(!SafeDraftText(text,maximum))return false;return std::any_of(text.begin(),text.end(),[](wchar_t c){return !std::iswspace(c);});}
class ComposeIntent {
    Binding binding_;std::wstring text_,nonce_;std::uint64_t activation_=0;bool reviewed_=false,configured_=false;
    friend bool BeginCompose(const Snapshot&,const Configuration&,const std::wstring&,const std::wstring&,std::uint64_t,ComposeIntent&,std::wstring&);
    friend bool SetComposeText(ComposeIntent&,const std::wstring&,std::wstring&);
    friend bool ReviewCompose(ComposeIntent&,const Snapshot&,const Configuration&,std::wstring&);
    friend bool BeginConfiguredSend(const Snapshot&,const Configuration&,const std::wstring&,std::uint64_t,ComposeIntent&,std::wstring&);
public:
    const Binding& Context()const{return binding_;}const std::wstring& Text()const{return text_;}const std::wstring& Nonce()const{return nonce_;}std::uint64_t Activation()const{return activation_;}bool Reviewed()const{return reviewed_;}bool Configured()const{return configured_;}
};
inline bool BeginCompose(const Snapshot& snapshot,const Configuration& cfg,const std::wstring& initial,const std::wstring& nonce,std::uint64_t activation,ComposeIntent& out,std::wstring& error){
    if(!BindingMatchesConfiguration(snapshot.binding,cfg)||snapshot.binding.channelId.empty()||snapshot.binding.channelId!=cfg.channelId||!ValidNonce(nonce)||!activation||!SafeDraftText(initial,4000)){error=L"The compose target, draft or activation is invalid.";return false;}
    ComposeIntent draft;draft.binding_=snapshot.binding;draft.text_=initial;draft.nonce_=nonce;draft.activation_=activation;out=std::move(draft);error.clear();return true;
}
inline bool SetComposeText(ComposeIntent& draft,const std::wstring& text,std::wstring& error){if(!SafeDraftText(text,4000)){error=L"The draft contains invalid Unicode/control text or exceeds the supported size.";return false;}draft.text_=text;draft.reviewed_=draft.configured_=false;error.clear();return true;}
inline bool ReviewCompose(ComposeIntent& draft,const Snapshot& snapshot,const Configuration& cfg,std::wstring& error){
    const auto maximum=(std::min)(static_cast<unsigned>(cfg.sendCharacterLimit),snapshot.capabilities.maximumSendCharacters);
    if(!BindingMatchesConfiguration(snapshot.binding,cfg)||!(draft.binding_==snapshot.binding)||draft.binding_.channelId!=cfg.channelId||!SendTextValid(draft.text_,maximum)){error=L"The account/channel changed or the message is empty, invalid or too long.";return false;}draft.reviewed_=true;error.clear();return true;
}
inline bool BeginConfiguredSend(const Snapshot& snapshot,const Configuration& cfg,const std::wstring& nonce,std::uint64_t activation,ComposeIntent& out,std::wstring& error){
    if(cfg.clickAction!=L"SendConfigured"||!cfg.directSendEnabled){error=L"Direct configured sending is disabled; open the compose box instead.";return false;}
    ComposeIntent draft;if(!BeginCompose(snapshot,cfg,cfg.configuredText,nonce,activation,draft,error)||!ReviewCompose(draft,snapshot,cfg,error))return false;draft.configured_=true;out=std::move(draft);return true;
}
enum class SendOutcome {Pending,Sent,Rejected,Unknown};
struct SendRecord {std::uint64_t activation=0;std::wstring nonce;SendOutcome outcome=SendOutcome::Pending;};
class SendLedger {
    Binding binding_;std::uint64_t highest_=0;std::deque<SendRecord> records_;
public:
    bool Reserve(const Binding& binding,std::uint64_t activation,const std::wstring& nonce,std::size_t limit){if(!ValidBinding(binding)||!activation||!ValidNonce(nonce)||limit<1||limit>128)return false;
        if(!(binding_==binding)){binding_=binding;highest_=0;records_.clear();}if(activation<=highest_||std::any_of(records_.begin(),records_.end(),[&](const SendRecord& r){return r.nonce==nonce;}))return false;
        highest_=activation;records_.push_back({activation,nonce,SendOutcome::Pending});while(records_.size()>limit)records_.pop_front();return true;}
    bool Complete(const Binding& binding,std::uint64_t activation,SendOutcome outcome){if(!(binding_==binding)||outcome==SendOutcome::Pending)return false;for(auto& r:records_)if(r.activation==activation&&r.outcome==SendOutcome::Pending){r.outcome=outcome;return true;}return false;}
    std::size_t Retained()const{return records_.size();}
};
struct SendRequest {Binding binding;std::wstring text,nonce;std::uint64_t activation=0;bool enforceNonce=false,allowUserMentions=false,allowRoleMentions=false,allowEveryoneMentions=false;};
inline bool AuthorizeSend(const ComposeIntent& intent,const Snapshot& snapshot,const Configuration& cfg,SendLedger& ledger,SendRequest& output,std::wstring& error,const Acknowledgement* acknowledgement=nullptr){
    const auto fail=[&](const wchar_t* reason){error=reason;return false;};
    if(!BindingMatchesConfiguration(snapshot.binding,cfg)||!(intent.Context()==snapshot.binding)||snapshot.binding.channelId!=cfg.channelId)return fail(L"The selected account, channel or configuration changed. Compose a new message.");
    if(!intent.Reviewed())return fail(L"Review the message before sending.");
    if(intent.Configured()&&(cfg.clickAction!=L"SendConfigured"||!cfg.directSendEnabled||cfg.configuredText!=intent.Text()))return fail(L"The configured send action changed; review a new message.");
    if(!snapshot.connectionReady||snapshot.stale)return fail(L"The connection is not currently ready. Refresh before sending.");
    if(!ValidConfiguration(cfg)||snapshot.capabilities.backend!=*BackendFor(cfg.connection))return fail(L"The connection kind does not match the selected configuration.");
    if(!snapshot.capabilities.canSend)return fail(L"This connection has no message-send capability.");
    if(!cfg.allowReviewedSend)return fail(L"Message sending is disabled for this reader.");
    if(!ExperimentalAcknowledged(cfg,snapshot.binding,acknowledgement))return fail(L"Review the experimental personal-account warning for this account before connecting or sending.");
    const auto maximum=(std::min)(static_cast<unsigned>(cfg.sendCharacterLimit),snapshot.capabilities.maximumSendCharacters);
    if(!SendTextValid(intent.Text(),maximum))return fail(L"The message is empty, invalid or too long for this connection.");
    if(!ledger.Reserve(snapshot.binding,intent.Activation(),intent.Nonce(),static_cast<std::size_t>(cfg.sendHistoryLimit)))return fail(L"This activation or message nonce was already used; no automatic retry was authorized.");
    SendRequest request;request.binding=snapshot.binding;request.text=intent.Text();request.nonce=intent.Nonce();request.activation=intent.Activation();request.enforceNonce=snapshot.capabilities.enforceNonce;
    request.allowUserMentions=cfg.sendAllowUserMentions;request.allowRoleMentions=cfg.sendAllowRoleMentions;request.allowEveryoneMentions=cfg.sendAllowEveryoneMentions;output=std::move(request);error.clear();return true;
}
} }

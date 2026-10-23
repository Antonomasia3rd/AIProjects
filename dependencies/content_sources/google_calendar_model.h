#pragma once
// Google Calendar configuration and wire-independent values. No network,
// browser, account discovery or operating-system state is accessed here.
#include "clock_calendar_model.h"
#include <functional>

namespace aip { namespace google_calendar {
struct Configuration {
    std::wstring clientId,clientSecret,refreshTokenProtected,calendarId=L"primary";
    // Host-supplied stable profile identity and tile ID, never a display name.
    // Included inside the DPAPI envelope to reject cross-profile reuse.
    std::wstring ownerKey;
    int refreshSeconds=300,timeoutSeconds=15,maximumBytes=2097152;
};
struct Credentials {std::wstring refreshTokenProtected;};
struct CalendarChoice {std::wstring id,name,timeZone;bool primary=false;};
struct Event {
    std::wstring id,summary,location,calendarTimeZone;
    aip::datetime::UTC start=0,end=0;
    bool allDay=false;
    aip::datetime::Civil startDate,endDate; // Calendar-local dates; end exclusive.
};
using Cancel=std::function<bool()>;
inline constexpr aip::datetime::Setting Settings[]={
    {L"ClientId",L""},{L"ClientSecret",L""},{L"RefreshTokenProtected",L""},{L"CalendarId",L"primary"},
    {L"RefreshSeconds",L"300"},{L"TimeoutSeconds",L"15"},{L"MaximumBytes",L"2097152"}};
inline bool NormalizeSetting(std::wstring& key,std::wstring& value,std::wstring& error) {
    const auto lower=aip::datetime::Lower(key);const aip::datetime::Setting* known=nullptr;
    for(const auto& setting:Settings)if(lower==aip::datetime::Lower(setting.key))known=&setting;
    if(!known){error=L"Unknown Google Calendar setting.";return false;}key=known->key;value=aip::datetime::Trim(value);
    const auto bounded=[&](std::size_t limit){return value.size()<=limit&&std::none_of(value.begin(),value.end(),[](wchar_t c){return c<0x20||c==0x7f;});};
    if(!bounded(lower==L"refreshtokenprotected"?131072:2048)){error=L"Google Calendar setting exceeds its bounded single-line format.";return false;}
    if(lower==L"clientid"&&!value.empty()){
        const std::wstring suffix=L".apps.googleusercontent.com";
        if(value.size()<=suffix.size()||value.compare(value.size()-suffix.size(),suffix.size(),suffix)!=0||
            std::any_of(value.begin(),value.end(),[](wchar_t c){return !((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'.');})){
            error=L"Enter the OAuth client ID for your Google Cloud Desktop app.";return false;
        }
    }
    if(lower==L"refreshtokenprotected"&&!value.empty()&&value.compare(0,14,L"dpapi:v1:utf8:")!=0){
        error=L"Google refresh tokens must be saved through Connect as user-protected credentials; plaintext tokens are not accepted.";return false;
    }
    if(lower==L"calendarid"&&value.empty())value=L"primary";
    if(lower==L"refreshseconds"||lower==L"timeoutseconds"||lower==L"maximumbytes"){
        int number=0;const auto minimum=lower==L"maximumbytes"?1024:lower==L"refreshseconds"?30:1;
        const auto maximum=lower==L"maximumbytes"?4194304:lower==L"refreshseconds"?86400:120;
        if(!aip::datetime::Number(value,number)||number<minimum||number>maximum){error=L"Google Calendar refresh, timeout or byte limit is outside its supported range.";return false;}
        value=std::to_wstring(number);
    }
    error.clear();return true;
}
template<class Reader> bool ReadConfiguration(Reader get,Configuration& output,std::wstring& error) {
    Configuration c;
    for(const auto& field:Settings){std::wstring key=field.key,value=get(field.key,field.fallback);if(!NormalizeSetting(key,value,error))return false;
        if(key==L"ClientId")c.clientId=value;else if(key==L"ClientSecret")c.clientSecret=value;
        else if(key==L"CalendarId")c.calendarId=value;else if(key==L"RefreshTokenProtected")c.refreshTokenProtected=value;
        else {int number=0;aip::datetime::Number(value,number);if(key==L"RefreshSeconds")c.refreshSeconds=number;else if(key==L"TimeoutSeconds")c.timeoutSeconds=number;else c.maximumBytes=number;}}
    output=std::move(c);return true;
}
inline bool NormalizeGoogleCalendarSetting(std::wstring& key,std::wstring& value,std::wstring& error){return NormalizeSetting(key,value,error);}
inline std::wstring ConfigurationKey(const Configuration& c) {
    // Private revision key only. Never write to diagnostics or notifications.
    return c.ownerKey+L"\n"+c.clientId+L"\n"+c.clientSecret+L"\n"+c.refreshTokenProtected+L"\n"+c.calendarId+L"\n"+
        std::to_wstring(c.refreshSeconds)+L":"+std::to_wstring(c.timeoutSeconds)+L":"+std::to_wstring(c.maximumBytes);
}
inline bool ParseDate(const std::wstring& text,aip::datetime::Civil& out) {
    if(text.size()!=10||text[4]!=L'-'||text[7]!=L'-')return false;
    aip::datetime::Civil date;const bool ok=aip::datetime::Number(text.substr(0,4),date.year)&&aip::datetime::Number(text.substr(5,2),date.month)&&
        aip::datetime::Number(text.substr(8,2),date.day)&&aip::datetime::Valid(date);if(ok)out=date;return ok;
}
inline bool ParseRfc3339(const std::wstring& text,aip::datetime::UTC& result) {
    using namespace aip::datetime;
    if(text.size()<20||text[10]!=L'T'||text[13]!=L':'||text[16]!=L':')return false;
    Civil c;if(!ParseDate(text.substr(0,10),c)||!Number(text.substr(11,2),c.hour)||!Number(text.substr(14,2),c.minute)||
        !Number(text.substr(17,2),c.second)||!Valid(c))return false;
    std::size_t pos=19;if(pos<text.size()&&text[pos]==L'.'){const auto begin=++pos;while(pos<text.size()&&text[pos]>=L'0'&&text[pos]<=L'9')++pos;if(pos==begin)return false;}
    int bias=0;
    if(pos+1==text.size()&&text[pos]==L'Z'){}
    else {int h=0,m=0;if(pos+6!=text.size()||(text[pos]!=L'+'&&text[pos]!=L'-')||text[pos+3]!=L':'||
        !Number(text.substr(pos+1,2),h)||!Number(text.substr(pos+4,2),m)||h>23||m>59)return false;
        bias=(h*60+m)*(text[pos]==L'+'?1:-1);}
    result=Seconds(c)-static_cast<UTC>(bias)*60;return true;
}
inline std::wstring Rfc3339(aip::datetime::UTC seconds) {
    const auto c=aip::datetime::FromSeconds(seconds);const auto two=[](int n){return (n<10?L"0":L"")+std::to_wstring(n);};
    return std::to_wstring(c.year)+L"-"+two(c.month)+L"-"+two(c.day)+L"T"+two(c.hour)+L":"+two(c.minute)+L":"+two(c.second)+L"Z";
}
template<class Resolver> bool ToOccurrence(const Event& event,Resolver resolve,aip::datetime::Occurrence& result) {
    auto start=event.start,end=event.end;
    if(event.allDay&&(!resolve(event.startDate,event.calendarTimeZone,start)||!resolve(event.endDate,event.calendarTimeZone,end)))return false;
    if(end<start)return false;result={start,end,event.allDay,event.summary,event.location,event.id};return true;
}
} }

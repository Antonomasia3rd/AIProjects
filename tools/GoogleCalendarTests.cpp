#include "../dependencies/content_sources/google_calendar.inc"
#include <iostream>
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
using namespace aip::google_calendar;
Configuration Config(){Configuration c;c.clientId=L"123-fixture.apps.googleusercontent.com";c.ownerKey=L"fixture-profile/calendar1";c.refreshTokenProtected=L"dpapi:v1:utf8:fixture";return c;}
void Reply(aip::HttpFetchResponse& response,const std::string& text){response.statusCode=200;response.body.assign(text.begin(),text.end());}
const std::string tokens=R"({"access_token":"ACCESS_ONLY_FIXTURE","refresh_token":"REFRESH_ONLY_FIXTURE","token_type":"Bearer","expires_in":3600})";
const std::string calendars=R"({"items":[{"id":"fixture@example.test","summary":"Test calendar","timeZone":"Asia/Jakarta","primary":true}]})";
Hooks BaseHooks(){Hooks hooks;hooks.inert=true;
    hooks.unprotect=[](const std::wstring&,std::wstring& value){value=L"DesktopStub.GoogleCalendar.v1\nfixture-profile/calendar1\n123-fixture.apps.googleusercontent.com\nREFRESH_ONLY_FIXTURE";return true;};
    return hooks;}
void ConfigurationTests(){
    std::wstring error;for(const auto& setting:Settings){auto key=std::wstring(setting.key),value=std::wstring(setting.fallback);Check(NormalizeSetting(key,value,error),"Google setting factory value validates");}
    for(const auto* value:{L"token-plaintext",L"http://other.example/",L"dpapi:old"}){std::wstring key=L"RefreshTokenProtected",text=value;Check(!NormalizeSetting(key,text,error),"plaintext/foreign/legacy serialized refresh tokens are not accepted");}
    auto cfg=Config();Check(RuntimeConfigValidate(cfg,error,true),"bounded owner-bound Google configuration validates");cfg.ownerKey.clear();Check(!RuntimeConfigValidate(cfg,error),"missing account owner is rejected");
    cfg=Config();Check(ImportDesktopClientJson(R"({"installed":{"client_id":"123-import.apps.googleusercontent.com","client_secret":"DESKTOP_PUBLIC_SECRET","token_uri":"https://attacker.example/token"}})",cfg,error)&&
        cfg.clientId==L"123-import.apps.googleusercontent.com"&&cfg.refreshTokenProtected.empty(),"Desktop OAuth JSON imports only client credentials and clears stale grants");
    for(const auto* json:{R"({"web":{"client_id":"123-fixture.apps.googleusercontent.com"}})",R"({"type":"service_account","private_key":"PRIVATE"})",
        R"({"installed":{"client_id":"123-fixture.apps.googleusercontent.com","client_id":"456-fixture.apps.googleusercontent.com"}})"})
        Check(!ImportDesktopClientJson(json,cfg,error),"web/service/duplicate desktop credentials reject without credential discovery");
    aip::datetime::UTC utc=0;Check(ParseRfc3339(L"2026-10-24T08:00:00+07:00",utc)&&Rfc3339(utc)==L"2026-10-24T01:00:00Z","offset event time normalizes to UTC");
    Check(ParseRfc3339(L"2026-10-24T01:00:00.123Z",utc),"fractional seconds accepted without changing minute semantics");
    for(const auto* text:{L"2026-02-30T01:00:00Z",L"2026-10-24T01:00:00",L"2026-10-24T01:00:00+24:00",L"2026-10-24T01:00:00.Z"})Check(!ParseRfc3339(text,utc),"invalid/ambiguous event dates reject");
}
void CallbackTests(){
    std::wstring code;bool denied=false;
    Check(detail::Callback("/oauth2callback?code=A%2BB&state=SECURE_STATE","/oauth2callback",L"SECURE_STATE",code,denied)&&code==L"A+B"&&!denied,"exact callback path and state accept encoded auth code");
    for(const auto* target:{"/other?code=A&state=SECURE_STATE","/oauth2callback?code=A&state=wrong","/oauth2callback?code=A&state=SECURE_STATE&state=SECURE_STATE",
        "/oauth2callback?code=%0d%0a&state=SECURE_STATE","/oauth2callback?code=%XY&state=SECURE_STATE","/oauth2callback?code=A&error=access_denied&state=SECURE_STATE"})
        Check(!detail::Callback(target,"/oauth2callback",L"SECURE_STATE",code,denied),"foreign/duplicate/malformed callback rejects");
    Check(detail::Callback("/oauth2callback?error=access_denied&state=SECURE_STATE","/oauth2callback",L"SECURE_STATE",code,denied)&&denied&&code.empty(),"consent refusal never approves account connection");
    const std::string validRequest="GET /oauth2callback?code=A&state=SECURE_STATE HTTP/1.1\r\nhost: 127.0.0.1:4242\r\nConnection: close\r\n\r\n";
    Check(detail::HttpCallback(validRequest,"127.0.0.1:4242","/oauth2callback",L"SECURE_STATE",code,denied),"callback HTTP headers are matched case-insensitively");
    Check(!detail::HttpCallback(validRequest,"127.0.0.1:9999","/oauth2callback",L"SECURE_STATE",code,denied)&&
        !detail::HttpCallback("GET /oauth2callback?code=A&state=SECURE_STATE HTTP/1.1\r\nHost: 127.0.0.1:4242\r\nHost: attacker\r\n\r\n","127.0.0.1:4242","/oauth2callback",L"SECURE_STATE",code,denied),
        "foreign-port and duplicate Host headers cannot approve callback");
    std::wstring verifier,challenge,state;Check(detail::NewPkce(verifier,challenge,state)&&verifier.size()==43&&challenge.size()==43&&state.size()==43&&verifier!=state,"OS cryptographic PKCE has independent 256-bit verifier/state");
    const auto url=detail::AuthorizationUrl(Config(),L"http://127.0.0.1:4242/oauth2callback",challenge,state);
    Check(url.find(L"https://accounts.google.com/o/oauth2/v2/auth?")==0&&url.find(L"code_challenge_method=S256")!=std::wstring::npos&&
        url.find(L"calendar.events.readonly")!=std::wstring::npos&&url.find(L"calendar.calendarlist.readonly")!=std::wstring::npos&&url.find(L"prompt=consent")!=std::wstring::npos,
        "browser authorization requests only read-only Calendar scopes with PKCE and offline consent");
}
void ConnectTests(){
    auto cfg=Config();cfg.refreshTokenProtected.clear();auto hooks=BaseHooks();int httpCalls=0,authorizeCalls=0,protectCalls=0;
    hooks.authorize=[&](const Configuration&,const std::wstring& verifier,const std::wstring& challenge,const std::wstring& state,std::wstring& redirect,std::wstring& code,std::wstring&,const Cancel& cancel){
        ++authorizeCalls;Check(!cancel()&&verifier.size()==43&&challenge.size()==43&&state.size()==43,"injected authorization receives bounded cryptographic flow");redirect=L"http://127.0.0.1:4242/oauth2callback";code=L"AUTH+CODE";return true;};
    hooks.http=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){++httpCalls;
        Check(options.url==L"https://oauth2.googleapis.com/token"&&options.method==L"POST"&&!options.followRedirects&&options.body.find("code=AUTH%2BCODE")!=std::string::npos&&
            options.body.find("code_verifier=")!=std::string::npos,"code exchange sends secrets in POST body to fixed Google token endpoint without redirects");Reply(response,tokens);return true;};
    hooks.protect=[&](const std::wstring& raw,std::wstring& protectedValue){++protectCalls;Check(raw.find(L"fixture-profile/calendar1")!=std::wstring::npos&&raw.find(L"REFRESH_ONLY_FIXTURE")!=std::wstring::npos,"protected refresh envelope binds profile/tile and OAuth client");protectedValue=L"dpapi:v1:utf8:NEW_FIXTURE";return true;};
    Credentials credentials;std::wstring error;Session session;
    Check(Connect(cfg,credentials,error,{},hooks,&session)&&credentials.refreshTokenProtected==L"dpapi:v1:utf8:NEW_FIXTURE"&&httpCalls==1&&authorizeCalls==1&&protectCalls==1,
        "explicit Connect returns only protected refresh credentials and caches access token in its instance session");
    cfg.refreshTokenProtected=credentials.refreshTokenProtected;detail::Sensitive access;
    Check(detail::Access(cfg,hooks,{},access,error,&session)&&access.value==L"ACCESS_ONLY_FIXTURE"&&httpCalls==1,"new connection reuses unexpired instance access token without refresh HTTP");
    Check(!Connect(cfg,credentials,error,[]{return true;},hooks)&&credentials.refreshTokenProtected.empty()&&authorizeCalls==1,"canceled Connect cannot start browser exchange or return grant");
    auto inert=BaseHooks();Check(!Connect(cfg,credentials,error,{},inert),"inert validation cannot create loopback listener or open a browser");
}
void FetchTests(){
    auto cfg=Config();auto hooks=BaseHooks();Session session;int refreshes=0,requests=0;
    hooks.http=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){++requests;
        Check(!options.followRedirects&&options.url.find(L"REFRESH_ONLY_FIXTURE")==std::wstring::npos&&options.timeout.count()<=15000&&options.maximumBytes==2097152,"bounded requests never put refresh credentials in URLs or follow redirects");
        if(options.url==L"https://oauth2.googleapis.com/token"){++refreshes;Reply(response,tokens);return true;}
        Check(options.method==L"GET"&&options.headers.size()==1&&options.headers[0].second==L"Bearer ACCESS_ONLY_FIXTURE","Calendar API uses read-only GET with access credential only in Authorization header");
        if(options.url.find(L"calendarList")!=std::wstring::npos){Reply(response,calendars);return true;}
        Check(options.url.find(L"singleEvents=true")!=std::wstring::npos&&options.url.find(L"timeMin=")!=std::wstring::npos&&options.url.find(L"timeMax=")!=std::wstring::npos,"event query asks Google to expand recurrence inside bounded time window");
        Reply(response,R"({"timeZone":"UTC","items":[{"id":"timed-1","summary":"Test event","start":{"dateTime":"2026-10-24T08:00:00+07:00"},"end":{"dateTime":"2026-10-24T09:00:00+07:00"}},{"id":"all-day","summary":"All day","start":{"date":"2026-10-24"},"end":{"date":"2026-10-25"}},{"id":"deleted","status":"cancelled"}]})");return true;};
    const auto start=aip::datetime::Seconds({2026,10,24});std::vector<Event> events;std::wstring error;
    Check(FetchEvents(cfg,start,start+7*86400,events,error,{},hooks,&session)&&events.size()==2&&events[0].id==L"timed-1"&&events[0].start==start+3600&&
        events[1].allDay&&events[1].calendarTimeZone==L"Asia/Jakarta","expanded events preserve stable IDs, timed offsets, canceled exceptions and original all-day calendar timezone");
    Check(refreshes==1&&requests==3,"event fetch and all-day timezone lookup reuse one memory-only access token");
    std::vector<CalendarChoice> picker;Check(ListCalendars(cfg,picker,error,{},hooks,&session)&&picker.size()==1&&picker[0].primary&&refreshes==1,"calendar picker returns named calendar choices without extra refresh");
    aip::datetime::Occurrence occurrence;bool zoneSeen=false;
    Check(ToOccurrence(events[1],[&](const aip::datetime::Civil& civil,const std::wstring& zone,aip::datetime::UTC& result){zoneSeen=zone==L"Asia/Jakarta";result=aip::datetime::Seconds(civil)-7*3600;return true;},occurrence)&&zoneSeen&&occurrence.end-occurrence.start==86400&&occurrence.allDay,
        "all-day conversion uses original local calendar dates with exclusive end");
    cfg.ownerKey=L"different-profile";const auto previous=requests;Check(!FetchEvents(cfg,start,start+86400,events,error,{},hooks,&session)&&requests==previous,"cross-profile credential envelope fails before any network request despite session reuse");
    cfg=Config();Check(!FetchEvents(cfg,start,start+33*86400,events,error,{},hooks)&&requests==previous,"overlarge event windows reject before transport");
    hooks.http=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){if(options.url.find(L"/token")!=std::wstring::npos)Reply(response,tokens);else Reply(response,R"({"items":[],"nextPageToken":"same"})");return true;};
    Check(!ListCalendars(cfg,picker,error,{},hooks)&&error.find(L"repeated")!=std::wstring::npos,"repeated pagination is rejected rather than looping");
    session.Clear();Check(session.accessToken.empty()&&session.expires==0,"disconnect clears cached bearer material");
}
void TimeZoneTests(){std::wstring windows;Check(ResolveIanaWindowsTimeZone(L"Asia/Jakarta",windows)&&windows==L"SE Asia Standard Time","OS ICU maps Indonesian calendar zone without small hardcoded alias list");
    Check(ResolveIanaWindowsTimeZone(L"Europe/Helsinki",windows)&&!windows.empty(),"OS ICU resolves another IANA timezone outside the previous aliases");
    Check(!ResolveIanaWindowsTimeZone(L"Unrecognized/NoSuchZone",windows)&&windows.empty(),"unrecognized IANA zone never guesses local timezone");}
}
int main(){ConfigurationTests();CallbackTests();ConnectTests();FetchTests();TimeZoneTests();std::cout<<"Google Calendar: "<<checks<<" checks, "<<failures<<" failures; injected HTTP/browser/credentials only\n";return failures?1:0;}

#include "../dependencies/content_sources/gmail.inc"
#include "../dependencies/tile_instances.h"
#include "../dependencies/mail_tile_layout.h"
#include "../dependencies/tile_notification_payload.h"
#include <iostream>
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
using namespace aip::gmail;
Configuration Config(){Configuration cfg;cfg.account.clientId=L"123-fixture.apps.googleusercontent.com";cfg.account.ownerKey=L"Gmail/profile/tile";cfg.account.refreshTokenProtected=L"dpapi:v1:utf8:fixture";return cfg;}
void Reply(aip::HttpFetchResponse& response,const std::string& value,int status=200){response.statusCode=status;response.body.assign(value.begin(),value.end());}
struct Transport {
    Configuration cfg=Config();unsigned calls=0,tokenCalls=0;bool calendarEnvelope=false,badScopes=false;std::wstring failAt;
    int failureStatus=503;std::string label=R"({"id":"INBOX","messagesUnread":2,"threadsUnread":1})";
    std::string list=R"({"messages":[{"id":"m2"},{"id":"m1"}]})";
    std::map<std::wstring,std::string> messages{
        {L"m2",R"({"id":"m2","internalDate":"1793000000000","snippet":"Hi &amp; welcome &lt;safe&gt;","payload":{"headers":[{"name":"From","value":"=?UTF-8?B?SsO8cmdlbg==?= <fixture@example.invalid>"},{"name":"Subject","value":"=?UTF-8?Q?Hello_=E4=B8=96=E7=95=8C?="}]}})"},
        {L"m1",R"({"id":"m1","internalDate":"1792000000000","snippet":"OLDER PREVIEW","payload":{"headers":[{"name":"From","value":"\"Older Person\" <older@example.invalid>"},{"name":"Subject","value":"Older subject"}]}})"}};
    Hooks Make(){Hooks hooks;hooks.inert=true;
        hooks.unprotect=[this](const std::wstring&,std::wstring& plain){auto account=AccountConfiguration(cfg);if(calendarEnvelope)account.oauthPurpose=aip::google_calendar::OAuthPurpose::Calendar;
            plain=aip::google_calendar::detail::Envelope(account)+L"REFRESH_FIXTURE_ONLY";return true;};
        hooks.http=[this](const aip::HttpFetchOptions& request,aip::HttpFetchResponse& response,std::wstring&){++calls;
            Check(!request.followRedirects&&request.maximumBytes<=static_cast<std::size_t>(cfg.account.maximumBytes),"all Gmail/OAuth requests retain redirect and byte bounds");
            Check(request.url.find(L"REFRESH_FIXTURE_ONLY")==std::wstring::npos&&request.url.find(L"ACCESS_FIXTURE_ONLY")==std::wstring::npos,"credentials never appear in request URLs");
            if(request.url==L"https://oauth2.googleapis.com/token"){
                ++tokenCalls;Check(request.method==L"POST"&&request.body.find("grant_type=refresh_token")!=std::string::npos,"Gmail uses the shared explicit refresh-token POST flow");
                const auto scope=badScopes?L"https://www.googleapis.com/auth/calendar.events.readonly":aip::google_calendar::OAuthScopes(AccountConfiguration(cfg).oauthPurpose);
                Reply(response,std::string("{\"access_token\":\"ACCESS_FIXTURE_ONLY\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":")+aip::JsonEscape(scope)+"}");return true;
            }
            const std::wstring fixedBase=L"https://gmail.googleapis.com/gmail/v1/users/me/";
            Check(request.url.compare(0,fixedBase.size(),fixedBase)==0&&request.method==L"GET"&&request.body.empty(),"inbox adapter uses only fixed read-only Gmail endpoints");
            Check(std::find(request.headers.begin(),request.headers.end(),std::make_pair(std::wstring(L"Authorization"),std::wstring(L"Bearer ACCESS_FIXTURE_ONLY")))!=request.headers.end(),"Gmail bearer stays in the Authorization header");
            if(!failAt.empty()&&request.url.find(failAt)!=std::wstring::npos){Reply(response,R"({"error":{"message":"PRIVATE REMOTE ERROR NEVER DISPLAY"}})",failureStatus);return true;}
            if(request.url.find(L"/profile?")!=std::wstring::npos){Reply(response,R"({"emailAddress":"fixture+mail@example.invalid"})");return true;}
            if(request.url.find(L"/labels/INBOX?")!=std::wstring::npos){Reply(response,label);return true;}
            if(request.url.find(L"/messages?")!=std::wstring::npos){
                Check(request.url.find(L"labelIds=INBOX")!=std::wstring::npos&&request.url.find(L"q=")==std::wstring::npos,"Gmail metadata filtering uses labelIds instead of unsupported q searches");
                Check((request.url.find(L"labelIds=UNREAD")!=std::wstring::npos)==cfg.unreadOnly,"unread-only filtering belongs to this Mail tile");Reply(response,list);return true;}
            const bool snippet=cfg.preview==L"Snippet";
            Check((request.url.find(L"format=full")!=std::wstring::npos)==snippet&&(request.url.find(L"format=metadata")!=std::wstring::npos)!=snippet,
                "only explicitly selected Snippet mode requests the read-only full format");
            Check(request.url.find(L"body")==std::wstring::npos&&request.url.find(L"parts")==std::wstring::npos&&request.url.find(L"attachments")==std::wstring::npos,
                "partial response fields exclude bodies, MIME parts and attachments");
            for(const auto& entry:messages)if(request.url.find(L"/messages/"+entry.first+L"?")!=std::wstring::npos){Reply(response,entry.second);return true;}
            Reply(response,"{}",404);return true;
        };return hooks;
    }
};
void Model(){
    Configuration cfg;std::wstring error;Check(ReadConfiguration([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},cfg,error)&&cfg.preview==L"Headers"&&cfg.unreadOnly&&cfg.badgeEnabled&&cfg.maxMessages==3&&cfg.account.refreshSeconds==120,"Mail defaults request headers only with an unread badge and bounded independent polling cadence");
    auto disabled=cfg;disabled.badgeEnabled=false;Check(ConfigurationKey(disabled)!=ConfigurationKey(cfg),"badge enablement participates in the per-tile immutable configuration");
    for(const auto& field:Settings){auto value=std::wstring(field.fallback);Check(NormalizeSetting(field.key,value,error),"Mail factory field validates");}
    for(const auto& bad:std::vector<std::pair<std::wstring,std::wstring>>{{L"Preview",L"Raw"},{L"UnreadOnly",L"maybe"},{L"MaxMessages",L"0"},{L"MaxMessages",L"6"},
        {L"RefreshTokenProtected",L"PLAIN-REFRESH"},{L"ClientId",L"other-client"},{L"PrimaryField",L"Html"},{L"SecondaryField",L"RawBody"},{L"BadgeEnabled",L"sometimes"}}){auto value=bad.second;Check(!NormalizeSetting(bad.first,value,error),"unsafe/unsupported Mail input fails before use");}
    cfg=Config();Check(AccountConfiguration(cfg).oauthPurpose==aip::google_calendar::OAuthPurpose::GmailMetadata,"headers default derives the distinct Gmail metadata purpose");
    const auto key=ConfigurationKey(cfg);cfg.preview=L"Snippet";Check(AccountConfiguration(cfg).oauthPurpose==aip::google_calendar::OAuthPurpose::GmailReadOnly&&key!=ConfigurationKey(cfg),"Snippet changes both the permission purpose and immutable source identity");
    Message message{L"id",L"Sender",L"Subject",L"Sensitive preview"};cfg.preview=L"Headers";
    Check(DisplayMessage(message,cfg).second==L"Subject","headers-only display cannot reveal even an accidentally populated snippet field");
    cfg.preview=L"Snippet";Check(DisplayMessage(message,cfg).second==L"Subject\nSensitive preview","permitted snippet is a separate deliberate display field");
    Check(InboxUrl(L"fixture+mail@example.invalid")==L"https://mail.google.com/mail/?authuser=fixture%2Bmail%40example.invalid#inbox"&&InboxUrl(L"bad\naddress")==L"https://mail.google.com/","mailbox activation uses a fixed Gmail URL and encoded account metadata");
}
void Fetch(){
    std::wstring error;Snapshot result;Transport t;Session session;auto hooks=t.Make();
    Check(FetchInbox(t.cfg,result,error,{},hooks,&session)&&result.ok&&result.unreadMessages==2&&result.unreadThreads==1&&result.messages.size()==2,
        "real Gmail parser combines exact Inbox unread counts and bounded message headers");
    Check(result.mailboxAddress==L"fixture+mail@example.invalid"&&result.messages[0].sender==L"Jürgen"&&result.messages[0].subject==L"Hello 世界"&&result.messages[0].snippet.empty(),
        "account identity and MIME sender/subject decode without enabling message preview");
    Check(result.messages[1].sender==L"Older Person"&&result.messages[0].receivedUtc>result.messages[1].receivedUtc,"collected message rows preserve friendly senders and sort by returned internal dates");
    const auto tokenCalls=t.tokenCalls;Check(FetchInbox(t.cfg,result,error,{},hooks,&session)&&t.tokenCalls==tokenCalls,"one tile reuses its bounded memory-only access token between refreshes");
    t.cfg.preview=L"Snippet";hooks=t.Make();Check(FetchInbox(t.cfg,result,error,{},hooks,&session)&&result.messages[0].snippet==L"Hi & welcome <safe>","explicit read-only Snippet mode decodes plain preview entities without HTML execution");
    t.cfg.unreadOnly=false;Check(FetchInbox(t.cfg,result,error,{},hooks),"per-tile all-Inbox selection removes only the UNREAD label restriction");
    t=Transport();hooks=t.Make();t.list=R"({})";t.label=R"({"id":"INBOX"})";
    Check(FetchInbox(t.cfg,result,error,{},hooks)&&result.messages.empty()&&result.unreadMessages==0,"empty Inbox and protobuf-omitted zero counts are ordinary successful data");
    t=Transport();hooks=t.Make();t.cfg.account.maximumBytes=1024;Check(FetchInbox(t.cfg,result,error,{},hooks),"minimum aggregate budget works for a genuinely small bounded response set");
    t=Transport();hooks=t.Make();t.calendarEnvelope=true;
    Check(!FetchInbox(t.cfg,result,error,{},hooks)&&t.calls==0&&!result.ok,"Calendar-protected credentials cannot make any Gmail or token request");
    t=Transport();hooks=t.Make();t.badScopes=true;Check(!FetchInbox(t.cfg,result,error,{},hooks)&&t.calls==1,"token granting only Calendar scope cannot fetch Gmail");
    t=Transport();hooks=t.Make();Check(!FetchInbox(t.cfg,result,error,[]{return true;},hooks)&&t.calls==0,"cancellation before fetch performs no account request");
    t=Transport();hooks=t.Make();t.cfg.account.refreshTokenProtected.clear();Check(!FetchInbox(t.cfg,result,error,{},hooks)&&t.calls==0,"unconfigured Mail is inert without a saved Gmail grant");
    for(const auto status:{301,401,403,429,503}){t=Transport();hooks=t.Make();t.failAt=L"/messages/m1?";t.failureStatus=status;
        Check(!FetchInbox(t.cfg,result,error,{},hooks)&&result.messages.empty()&&!result.ok&&error.find(L"PRIVATE REMOTE")==std::wstring::npos&&error.find(L"ACCESS_FIXTURE")==std::wstring::npos,
            "redirect/auth/rate/server failures reject partial results and redact remote contents and credentials");}
    t=Transport();hooks=t.Make();t.list=R"({"messages":[{"id":"m2"},{"id":"m2"}]})";Check(!FetchInbox(t.cfg,result,error,{},hooks),"duplicate message IDs reject the incomplete snapshot");
    t=Transport();hooks=t.Make();t.list=R"({"messages":[{"id":"../other"}]})";Check(!FetchInbox(t.cfg,result,error,{},hooks),"opaque message path IDs cannot redirect the fixed API route");
    t=Transport();hooks=t.Make();t.label=R"({"id":"INBOX","messagesUnread":-1})";Check(!FetchInbox(t.cfg,result,error,{},hooks),"negative unread counts are rejected");
    t=Transport();hooks=t.Make();t.messages[L"m1"]=R"({"id":"wrong","payload":{"headers":[]}})";Check(!FetchInbox(t.cfg,result,error,{},hooks),"response message identity must match the requested item");
    t=Transport();hooks=t.Make();t.messages[L"m1"]=std::string(3000,'x');t.cfg.account.maximumBytes=1024;Check(!FetchInbox(t.cfg,result,error,{},hooks),"aggregate byte budget bounds malformed/oversized message data");
    t=Transport();hooks=t.Make();ULONGLONG clock=0;hooks.monotonicNow=[&]{const auto value=clock;clock+=50000;return value;};
    Check(!FetchInbox(t.cfg,result,error,{},hooks)&&t.calls==0,"one shared source deadline expires before a later-stage request without sleeping");
}
void MailLayout(){
    using aip::content::TextMode;aip::content::Snapshot frame;frame.mailSource=true;frame.active=true;frame.secondaryBranding=L"Auto";
    frame.mailRows={{L"Sender & <one>",L"Subject one\nPreview one",L""},{L"Sender two",L"Subject two",L""},{L"Sender three",L"Subject three",L""}};
    const auto medium=aip::content::BuildMailTilePlan(frame,150,150),wide=aip::content::BuildMailTilePlan(frame,310,150),large=aip::content::BuildMailTilePlan(frame,310,310);
    Check(medium.size()==1&&medium[0].primary.lines==2&&medium[0].secondary.lines==2&&wide[0].primary.lines==1&&wide[0].secondary.lines==3,
        "recovered Mail Medium/Wide fields retain their distinct wrapping limits");
    Check(large.size()==3&&large[0].primary.style==L"caption"&&!large[0].primary.wrap&&large[0].secondary.lines==2,"Large uses grouped regular caption pairs rather than Calendar body headings");
    std::wstring xml;Check(aip::content::AppendMailTileBinding(xml,L"TileWide",150,frame)&&xml.find(L"Sender &amp; &lt;one&gt;")!=std::wstring::npos&&xml.find(L"hint-maxLines=\"3\"")!=std::wstring::npos&&xml.find(L"displayName=")==std::wstring::npos&&xml.find(L"branding=")==std::wstring::npos,
        "default Mail XML escapes fields and adds no invented date/count/footer override");
    std::wstring smallXml;Check(!aip::content::AppendMailTileBinding(smallXml,L"TileSmall",71,frame)&&smallXml.empty(),"Mail Small keeps its static image without an invented live text binding");
    frame.mailMessageIndex=1;xml.clear();Check(aip::content::AppendMailTileBinding(xml,L"TileMedium",150,frame)&&xml.find(L"Sender two")!=std::wstring::npos&&xml.find(L"Sender &amp;")==std::wstring::npos,"per-message native document selects exactly its prepared row");
    frame.mailIncludeLarge=false;xml.clear();Check(!aip::content::AppendMailTileBinding(xml,L"TileLarge",310,frame),"intermediate queue documents omit the aggregate Large binding");
    frame.mailIncludeLarge=true;frame.secondaryBranding=L"None";frame.presentation.preset=L"Custom";frame.presentation.primaryStyle=L"subtitle";frame.presentation.secondaryStyle=L"bodySubtle";
    xml.clear();Check(aip::content::AppendMailTileBinding(xml,L"TileMedium",150,frame)&&xml.find(L"branding=\"none\"")!=std::wstring::npos&&xml.find(L"hint-style=\"subtitle\"")!=std::wstring::npos&&xml.find(L"hint-style=\"bodySubtle\"")!=std::wstring::npos,"explicit Mail branding and typography remain configurable");
    frame.textMode=TextMode::Overlay;xml.clear();Check(!aip::content::AppendMailTileBinding(xml,L"TileMedium",150,frame)&&!aip::content::BuildMailTilePlan(frame,150,150).empty(),"Overlay retains the portable bitmap plan without duplicate XML text");
    frame.textMode=TextMode::Off;Check(aip::content::BuildMailTilePlan(frame,150,150).empty(),"Off hides Mail text in both semantic and bitmap paths");
    Configuration configuration=Config();configuration.preview=L"Snippet";Message oversized;
    oversized.sender=std::wstring(2000,L'語');oversized.subject=std::wstring(2000,L'界');oversized.snippet=std::wstring(8000,L'文');
    const auto projection=DisplayMessage(oversized,configuration);
    Check(projection.first.size()<=256&&projection.second.size()<=1025&&oversized.sender.size()==2000&&oversized.snippet.size()==8000,
        "wire projection clips presentation only and preserves raw provider fields");
    frame.textMode=TextMode::Auto;frame.presentation={};frame.mailRows.assign(5,{projection.first,projection.second,L""});xml.clear();std::wstring encoded,error;
    Check(aip::content::AppendMailTileBinding(xml,L"TileLarge",310,frame)&&aip::tile_notification::Encode(L"<tile><visual>"+xml+L"</visual></tile>",{},encoded,error),
        "five long multibyte Mail rows fit the actual64KiB UTF-8 tile transport limit");
    const auto boundary=ClipPresentation(std::wstring(254,L'x')+L"\xd83d\xde80"+std::wstring(8,L'y'),256);
    Check(aip::tile_notification::ValidXmlText(L"<tile>"+boundary+L"</tile>",error),"presentation clipping never cuts a UTF-16 surrogate pair");
}
void Instances(){
    const std::map<std::wstring,std::wstring> values{{L"Tiles.SchemaVersion",L"1"},{L"Tiles.Instances",L"left,right"},{L"Tile.left.Type",L"Mail"},
        {L"Tile.left.Mail.Preview",L"Snippet"},{L"Tile.left.Mail.MaxMessages",L"5"},{L"Tile.right.Type",L"Mail"},{L"Mail.ClientId",L"DO-NOT-INHERIT"}};
    aip::content::TileInstanceConfiguration cfg;std::wstring error;
    Check(aip::content::ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){const auto value=values.find(std::wstring(section)+L"."+key);return value==values.end()?std::wstring(fallback):value->second;},cfg,error)&&cfg.tiles.size()==2,"Mail type participates in the real independent tile model");
    if(cfg.tiles.size()==2)Check(cfg.tiles[0].mail.preview==L"Snippet"&&cfg.tiles[0].mail.maxMessages==5&&cfg.tiles[1].mail.preview==L"Headers"&&cfg.tiles[1].mail.account.clientId.empty()&&cfg.tiles[0].clickAction==L"source","Mail source preferences and credentials do not bleed across tiles or global sections");
}
}
int main(){Model();Fetch();MailLayout();Instances();std::cout<<checks<<" Gmail/Mail checks, "<<failures<<" failures; HTTP/credential operations injected, no account/browser calls\n";return failures?1:0;}

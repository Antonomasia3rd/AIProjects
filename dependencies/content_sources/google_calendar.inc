#ifndef AIP_GOOGLE_CALENDAR_INC
#define AIP_GOOGLE_CALENDAR_INC
// Desktop OAuth PKCE and read-only Google Calendar adapter. The host runs these
// bounded, cancelable functions on its existing async source worker. They never
// select accounts, start browser sign-in or persist credentials implicitly.
#include "google_calendar_model.h"
#include "notes.inc"
#include "../dpapi.inc"
#include <winsock.h>
#include <objbase.h>
#pragma comment(lib,"ws2_32.lib")

namespace aip { namespace google_calendar {
using HttpTransport=std::function<bool(const aip::HttpFetchOptions&,aip::HttpFetchResponse&,std::wstring&)>;
using SecretTransform=std::function<bool(const std::wstring&,std::wstring&)>;
struct Hooks {
    HttpTransport http;
    std::function<bool(const std::wstring&)> browser;
    // Inert fixture substitution for the entire loopback/browser stage.
    std::function<bool(const Configuration&,const std::wstring&,const std::wstring&,const std::wstring&,
        std::wstring&,std::wstring&,std::wstring&,const Cancel&)> authorize;
    SecretTransform protect,unprotect;
    bool inert=false;
    std::function<ULONGLONG()> monotonicNow; // Inert deadline fixtures only.
};
class Session {
public:
    // Per-instance, memory-only access token. No static/global account cache.
    std::mutex mutex;
    std::wstring key,accessToken;
    ULONGLONG expires=0;
    void Clear(){std::lock_guard<std::mutex> lock(mutex);if(!accessToken.empty())SecureZeroMemory(&accessToken[0],accessToken.size()*sizeof(wchar_t));accessToken.clear();key.clear();expires=0;}
    ~Session(){Clear();}
    Session()=default;Session(const Session&)=delete;Session& operator=(const Session&)=delete;
};
namespace detail {
inline bool Canceled(const Cancel& cancel){return cancel&&cancel();}
inline Cancel Deadline(const Cancel& cancel,unsigned milliseconds=45000,const std::function<ULONGLONG()>& clock={}) {
    const auto deadline=(clock?clock():GetTickCount64())+milliseconds;return [cancel,deadline,clock]{return Canceled(cancel)||(clock?clock():GetTickCount64())>=deadline;};
}
inline bool SafeToken(const std::wstring& token){return !token.empty()&&token.size()<=16384&&
    std::none_of(token.begin(),token.end(),[](wchar_t c){return c<=0x20||c==0x7f;});}
inline bool Configured(const Configuration& c,std::wstring& error,bool token=false) {
    if(c.oauthPurpose!=OAuthPurpose::Calendar&&c.oauthPurpose!=OAuthPurpose::GmailMetadata&&c.oauthPurpose!=OAuthPurpose::GmailReadOnly){error=L"Unsupported Google authorization purpose.";return false;}
    for(const auto& pair:std::vector<std::pair<std::wstring,std::wstring>>{{L"ClientId",c.clientId},{L"ClientSecret",c.clientSecret},
        {L"RefreshTokenProtected",c.refreshTokenProtected},{L"CalendarId",c.calendarId}}){auto key=pair.first,value=pair.second;if(!NormalizeSetting(key,value,error))return false;}
    if(c.clientId.empty()){error=L"Set the OAuth client ID from your Google Cloud Desktop app before connecting.";return false;}
    if(c.ownerKey.empty()||c.ownerKey.size()>4096||c.ownerKey.find_first_of(std::wstring(L"\r\n\0",3))!=std::wstring::npos){error=L"Google Calendar account has no valid tile/profile owner.";return false;}
    if(c.refreshSeconds<30||c.refreshSeconds>86400||c.timeoutSeconds<1||c.timeoutSeconds>120||c.maximumBytes<1024||c.maximumBytes>4194304){error=L"Google Calendar request bounds are invalid.";return false;}
    if(token&&c.refreshTokenProtected.empty()){error=L"Connect this Calendar tile to Google first.";return false;}return true;
}
inline void Wipe(std::wstring& value){if(!value.empty())SecureZeroMemory(&value[0],value.size()*sizeof(wchar_t));value.clear();}
inline void Wipe(std::string& value){if(!value.empty())SecureZeroMemory(&value[0],value.size());value.clear();}
struct Sensitive {std::wstring value;~Sensitive(){Wipe(value);}};
inline std::string Encode(const std::wstring& text) {
    const auto bytes=aip::WideToUtf8(text);static const char hex[]="0123456789ABCDEF";std::string result;
    for(const unsigned char c:bytes)if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')result+=static_cast<char>(c);
        else {result+='%';result+=hex[c>>4];result+=hex[c&15];}return result;
}
inline std::wstring Query(const std::wstring& value){const auto encoded=Encode(value);return {encoded.begin(),encoded.end()};}
inline bool Decode(const std::string& text,std::wstring& result) {
    const auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};std::string bytes;
    for(std::size_t p=0;p<text.size();++p){if(text[p]=='+')bytes+=' ';else if(text[p]!='%')bytes+=text[p];else {
        if(p+2>=text.size())return false;const auto a=hex(text[++p]),b=hex(text[++p]);if(a<0||b<0)return false;bytes+=static_cast<char>((a<<4)|b);}}
    return aip::TryUtf8ToWide(bytes,result)&&result.find_first_of(std::wstring(L"\r\n\0",3))==std::wstring::npos;
}
inline std::wstring Base64Url(const BYTE* data,std::size_t size) {
    constexpr wchar_t alphabet[]=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";std::wstring result;unsigned bits=0;int available=0;
    for(std::size_t p=0;p<size;++p){bits=(bits<<8)|data[p];available+=8;while(available>=6){available-=6;result+=alphabet[(bits>>available)&63];}}
    if(available)result+=alphabet[(bits<<(6-available))&63];return result;
}
inline bool NewPkce(std::wstring& verifier,std::wstring& challenge,std::wstring& state) {
    HCRYPTPROV provider=0;if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return false;
    BYTE secret[32]{},nonce[32]{},digest[32]{};HCRYPTHASH hash=0;DWORD size=sizeof(digest);
    bool ok=CryptGenRandom(provider,sizeof(secret),secret)&&CryptGenRandom(provider,sizeof(nonce),nonce);
    if(ok){verifier=Base64Url(secret,sizeof(secret));state=Base64Url(nonce,sizeof(nonce));const auto raw=aip::WideToUtf8(verifier);
        ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,reinterpret_cast<const BYTE*>(raw.data()),static_cast<DWORD>(raw.size()),0)&&CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)&&size==32;}
    if(ok)challenge=Base64Url(digest,sizeof(digest));if(hash)CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    SecureZeroMemory(secret,sizeof(secret));SecureZeroMemory(nonce,sizeof(nonce));SecureZeroMemory(digest,sizeof(digest));return ok;
}
inline std::wstring AuthorizationUrl(const Configuration& c,const std::wstring& redirect,const std::wstring& challenge,const std::wstring& state) {
    return L"https://accounts.google.com/o/oauth2/v2/auth?response_type=code&client_id="+Query(c.clientId)+L"&redirect_uri="+Query(redirect)+
        L"&scope="+Query(OAuthScopes(c.oauthPurpose))+
        L"&code_challenge_method=S256&code_challenge="+Query(challenge)+L"&state="+Query(state)+L"&access_type=offline&prompt=consent";
}
// Parse only our exact callback path, rejecting duplicate query keys and any
// credential in headers/body. The caller separately checks local peer and Host.
inline bool Callback(const std::string& target,const std::string& path,const std::wstring& state,std::wstring& code,bool& denied) {
    code.clear();denied=false;const auto q=target.find('?');if(q==std::string::npos||target.substr(0,q)!=path||target.size()>8192)return false;
    std::map<std::wstring,std::wstring> fields;std::size_t pos=q+1;
    while(pos<target.size()){const auto end=target.find('&',pos),eq=target.find('=',pos);if(eq==std::string::npos||(end!=std::string::npos&&eq>end))return false;
        std::wstring key,value;if(!Decode(target.substr(pos,eq-pos),key)||!Decode(target.substr(eq+1,end==std::string::npos?end:end-eq-1),value)||
            !fields.emplace(key,value).second)return false;if(end==std::string::npos)break;pos=end+1;}
    const auto actual=fields.find(L"state");if(actual==fields.end()||actual->second!=state)return false;
    const auto error=fields.find(L"error"),auth=fields.find(L"code");if(error!=fields.end()){denied=true;return auth==fields.end();}
    if(auth==fields.end()||auth->second.empty()||auth->second.size()>4096)return false;code=auth->second;return true;
}
inline bool HttpCallback(const std::string& request,const std::string& host,const std::string& path,const std::wstring& state,std::wstring& code,bool& denied) {
    const auto lineEnd=request.find("\r\n"),space=request.find(' ',4),headerEnd=request.find("\r\n\r\n");
    if(request.size()>8192||headerEnd==std::string::npos||lineEnd==std::string::npos||request.compare(0,4,"GET ")!=0||
        space==std::string::npos||space>=lineEnd||request.substr(space+1,lineEnd-space-1)!="HTTP/1.1")return false;
    bool foundHost=false;unsigned count=0;std::size_t pos=lineEnd+2;
    while(pos<headerEnd){const auto end=request.find("\r\n",pos),colon=request.find(':',pos);
        if(++count>64||end==std::string::npos||colon==std::string::npos||colon>=end)return false;
        auto name=request.substr(pos,colon-pos);for(auto& c:name)if(c>='A'&&c<='Z')c+=static_cast<char>('a'-'A');
        if(name=="host"){
            if(foundHost)return false;foundHost=true;auto value=request.substr(colon+1,end-colon-1);
            const auto first=value.find_first_not_of(" \t"),last=value.find_last_not_of(" \t");if(first==std::string::npos||value.substr(first,last-first+1)!=host)return false;
        }
        pos=end+2;
    }
    return foundHost&&Callback(request.substr(4,space-4),path,state,code,denied);
}
struct Socket {SOCKET value=INVALID_SOCKET;~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}};
struct Winsock {bool active=false;~Winsock(){if(active)WSACleanup();}};
inline void Reply(SOCKET socket,bool success) {
    const std::string body=success?"<!doctype html><meta charset=utf-8><title>DesktopStub</title>You can close this tab and return to DesktopStub.":
        "<!doctype html><meta charset=utf-8><title>DesktopStub</title>This sign-in response was not accepted. Return to DesktopStub.";
    const auto text="HTTP/1.1 "+std::string(success?"200 OK":"400 Bad Request")+"\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    send(socket,text.data(),static_cast<int>(text.size()),0);
}
inline bool NativeAuthorize(const Configuration& c,const std::wstring&,const std::wstring& challenge,const std::wstring& state,
    std::wstring& redirect,std::wstring& code,std::wstring& error,const Cancel& cancel,const Hooks& hooks) {
    if(hooks.inert){error=L"Google sign-in is disabled in inert validation.";return false;}
    WSADATA data{};Winsock winsock;winsock.active=WSAStartup(MAKEWORD(2,2),&data)==0;
    if(!winsock.active){error=L"Could not start the local Google sign-in listener.";return false;}
    Socket listener;listener.value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener.value==INVALID_SOCKET){error=L"Could not create the local Google sign-in listener.";return false;}
    const BOOL exclusive=TRUE;constexpr int exclusiveAddressUse=static_cast<int>(~SO_REUSEADDR);
    if(setsockopt(listener.value,SOL_SOCKET,exclusiveAddressUse,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==SOCKET_ERROR){error=L"Could not reserve the local Google sign-in listener.";return false;}
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
    int length=sizeof(address);u_long nonblocking=1;
    if(bind(listener.value,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener.value,4)==SOCKET_ERROR||
        getsockname(listener.value,reinterpret_cast<sockaddr*>(&address),&length)==SOCKET_ERROR||ioctlsocket(listener.value,FIONBIO,&nonblocking)==SOCKET_ERROR){error=L"Could not bind Google sign-in to the local loopback interface.";return false;}
    const auto host="127.0.0.1:"+std::to_string(ntohs(address.sin_port));const std::string path="/oauth2callback";
    redirect=L"http://"+std::wstring(host.begin(),host.end())+std::wstring(path.begin(),path.end());
    if(Canceled(cancel)){error=L"Google sign-in canceled.";return false;}
    const auto url=AuthorizationUrl(c,redirect,challenge,state);
    const bool opened=hooks.browser?hooks.browser(url):reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
    if(!opened){error=L"Could not open Google sign-in in your default browser.";return false;}
    const auto deadline=GetTickCount64()+180000;unsigned attempts=0;
    while(!Canceled(cancel)&&GetTickCount64()<deadline&&attempts<64){
        fd_set read{};FD_ZERO(&read);FD_SET(listener.value,&read);timeval delay{0,100000};const auto ready=select(0,&read,nullptr,nullptr,&delay);if(ready==SOCKET_ERROR)break;if(!ready)continue;
        sockaddr_in peer{};int peerLength=sizeof(peer);Socket client;client.value=accept(listener.value,reinterpret_cast<sockaddr*>(&peer),&peerLength);
        if(client.value==INVALID_SOCKET)continue;++attempts;if(peer.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)){Reply(client.value,false);continue;}
        ioctlsocket(client.value,FIONBIO,&nonblocking);std::string request;const auto readDeadline=(std::min)(deadline,GetTickCount64()+3000);
        while(!Canceled(cancel)&&GetTickCount64()<readDeadline&&request.size()<8192&&request.find("\r\n\r\n")==std::string::npos){
            fd_set incoming{};FD_ZERO(&incoming);FD_SET(client.value,&incoming);timeval tick{0,100000};if(select(0,&incoming,nullptr,nullptr,&tick)<=0)continue;
            char bytes[1024]{};const auto got=recv(client.value,bytes,static_cast<int>((std::min)(sizeof(bytes),8192-request.size())),0);if(got<=0)break;request.append(bytes,got);
        }
        bool denied=false;const bool valid=HttpCallback(request,host,path,state,code,denied);
        Reply(client.value,valid);Wipe(request);
        if(valid){if(denied){error=L"Google sign-in was declined. No account was connected.";return false;}return !Canceled(cancel);}
    }
    error=Canceled(cancel)?L"Google sign-in canceled.":L"Google sign-in timed out. Choose Connect to try again.";return false;
}
using Object=std::map<std::string,std::string>;
inline bool ReadObject(const std::string& json,Object& output) {
    output.clear();if(!aip::notes::detail::ValidDocument(json))return false;std::size_t pos=0;aip::notes::detail::SkipWhitespace(json,pos);++pos;
    for(;;){aip::notes::detail::SkipWhitespace(json,pos);if(pos>=json.size())return false;if(json[pos]=='}')return true;
        const auto end=aip::JsonStringEnd(json,pos);std::string key;if(end==std::string::npos||!aip::DecodeJsonStringUtf8Range(json,pos,end,key))return false;
        pos=end;aip::notes::detail::SkipWhitespace(json,pos);if(pos>=json.size()||json[pos++]!=':')return false;aip::notes::detail::SkipWhitespace(json,pos);
        const auto valueEnd=aip::JsonValueEnd(json,pos);if(valueEnd==std::string::npos||!output.emplace(key,json.substr(pos,valueEnd-pos)).second)return false;
        pos=valueEnd;aip::notes::detail::SkipWhitespace(json,pos);if(pos<json.size()&&json[pos]==',')++pos;
    }
}
inline bool String(const Object& object,const char* key,std::wstring& value,std::size_t limit=4096,bool optional=false) {
    const auto found=object.find(key);if(found==object.end()){value.clear();return optional;}std::string bytes;
    return aip::DecodeJsonStringUtf8Range(found->second,0,found->second.size(),bytes)&&aip::TryUtf8ToWide(bytes,value)&&value.size()<=limit&&
        value.find(L'\0')==std::wstring::npos;
}
inline bool Array(const std::string& json,std::vector<std::string>& output,std::size_t limit) {
    output.clear();if(json.empty()||json.front()!='['||json.back()!=']')return false;std::size_t p=1;
    for(;;){aip::notes::detail::SkipWhitespace(json,p);if(p>=json.size())return false;if(json[p]==']')return true;
        const auto end=aip::JsonValueEnd(json,p);if(end==std::string::npos||output.size()>=limit)return false;output.push_back(json.substr(p,end-p));
        p=end;aip::notes::detail::SkipWhitespace(json,p);if(p<json.size()&&json[p]==',')++p;
    }
}
inline bool Request(const Configuration& c,const Hooks& hooks,const Cancel& cancel,const std::wstring& url,const std::wstring& bearer,
    std::string& json,std::wstring& error,const std::string& body={},bool post=false) {
    if(Canceled(cancel)){error=L"Google Calendar request canceled.";return false;}if(hooks.inert&&!hooks.http){error=L"Google network access is disabled in inert validation.";return false;}
    aip::HttpFetchOptions request;request.url=url;request.userAgent=L"DesktopStub Calendar";request.accept=L"application/json";request.followRedirects=false;
    request.timeout=std::chrono::seconds(c.timeoutSeconds);request.maximumBytes=static_cast<std::size_t>(c.maximumBytes);request.canceled=cancel;
    if(!bearer.empty())request.headers.push_back({L"Authorization",L"Bearer "+bearer});
    if(post){request.method=L"POST";request.body=body;request.headers.push_back({L"Content-Type",L"application/x-www-form-urlencoded"});}
    aip::HttpFetchResponse response;std::wstring privateError;const auto ok=hooks.http?hooks.http(request,response,privateError):aip::FetchHttpBytes(request,response,privateError);
    Wipe(request.body);for(auto& header:request.headers)if(header.first==L"Authorization")Wipe(header.second);
    if(!ok||Canceled(cancel)){error=Canceled(cancel)?L"Google Calendar request canceled.":L"Google Calendar could not be reached. Check connectivity and retry.";return false;}
    if(response.body.size()>static_cast<std::size_t>(c.maximumBytes)){error=L"Google Calendar response exceeded the configured size limit.";return false;}
    if(response.statusCode<200||response.statusCode>=300){error=response.statusCode==401?L"Google authorization expired. Connect this Calendar tile again.":
        response.statusCode==403?L"Google denied Calendar access. Check the API, consent scopes and calendar permissions.":
        response.statusCode==429?L"Google Calendar rate limit reached. DesktopStub will retry at the configured refresh interval.":L"Google Calendar request failed. Reconnect if the account grant is no longer valid.";return false;}
    json.assign(response.body.begin(),response.body.end());if(!response.body.empty())SecureZeroMemory(response.body.data(),response.body.size());return true;
}
inline std::wstring Envelope(const Configuration& c){
    const auto prefix=c.oauthPurpose==OAuthPurpose::Calendar?L"DesktopStub.GoogleCalendar.v1\n":
        c.oauthPurpose==OAuthPurpose::GmailMetadata?L"DesktopStub.Gmail.Metadata.v1\n":L"DesktopStub.Gmail.ReadOnly.v1\n";
    return prefix+c.ownerKey+L"\n"+c.clientId+L"\n";
}
inline bool Protect(const Configuration& c,const std::wstring& token,const Hooks& hooks,std::wstring& protectedValue,std::wstring& error) {
    if(!SafeToken(token)){error=L"Google returned an invalid refresh credential.";return false;}
    Sensitive envelope;envelope.value=Envelope(c)+token;
    try {if(hooks.protect){if(hooks.protect(envelope.value,protectedValue))return true;}
        else if(!hooks.inert){protectedValue=aip::ProtectSecretForCurrentUser(envelope.value);return !protectedValue.empty();}}
    catch(...){}error=L"Google refresh credential could not be protected for this Windows user.";return false;
}
inline bool Unprotect(const Configuration& c,const Hooks& hooks,Sensitive& token,std::wstring& error) {
    Sensitive envelope;bool ok=false;
    try {if(hooks.unprotect)ok=hooks.unprotect(c.refreshTokenProtected,envelope.value);
        else if(!hooks.inert){envelope.value=aip::UnprotectSecretForCurrentUser(c.refreshTokenProtected,aip::DpapiLegacyEncoding::Utf8);ok=true;}}
    catch(...){}const auto prefix=Envelope(c);
    if(!ok||envelope.value.compare(0,prefix.size(),prefix)!=0||envelope.value.size()<=prefix.size()||envelope.value.size()>prefix.size()+16384){error=L"Connect this Calendar tile again; its saved Google credential does not belong to this tile, profile, client or Windows user.";return false;}
    token.value=envelope.value.substr(prefix.size());if(!SafeToken(token.value)){error=L"Saved Google authorization is invalid. Connect again.";return false;}return true;
}
inline bool Token(const Configuration& c,const Hooks& hooks,const Cancel& cancel,const std::string& form,Sensitive& access,Sensitive* refresh,std::wstring& error,int* expires=nullptr) {
    std::string body=form+"&client_id="+Encode(c.clientId);if(!c.clientSecret.empty())body+="&client_secret="+Encode(c.clientSecret);std::string json;
    const auto ok=Request(c,hooks,cancel,L"https://oauth2.googleapis.com/token",{},json,error,body,true);Wipe(body);if(!ok)return false;
    Object object;const auto valid=ReadObject(json,object)&&String(object,"access_token",access.value,16384)&&SafeToken(access.value);Wipe(json);
    std::wstring type;if(!valid||!String(object,"token_type",type,64)||aip::datetime::Lower(type)!=L"bearer"){error=L"Google returned an invalid access credential.";return false;}
    if(refresh&&(!String(object,"refresh_token",refresh->value,16384,true)||(!refresh->value.empty()&&!SafeToken(refresh->value)))){error=L"Google returned an invalid refresh credential.";return false;}
    std::wstring scopes;if(!String(object,"scope",scopes,8192,true)){error=L"Google returned an invalid permission list.";return false;}
    if(c.oauthPurpose!=OAuthPurpose::Calendar){
        if(refresh&&scopes.empty()){error=L"Google did not confirm the requested Gmail read permission.";return false;}
        if(!scopes.empty()){
            const auto grants=L" "+scopes+L" ";
            const bool readOnly=grants.find(L" https://www.googleapis.com/auth/gmail.readonly ")!=std::wstring::npos||grants.find(L" https://mail.google.com/ ")!=std::wstring::npos;
            const bool metadata=grants.find(L" https://www.googleapis.com/auth/gmail.metadata ")!=std::wstring::npos;
            if(!readOnly&&!(c.oauthPurpose==OAuthPurpose::GmailMetadata&&metadata)){error=L"Google did not grant this Mail tile's requested read permission. Connect the Mail tile again.";return false;}
        }
    }else if(!scopes.empty()){
        const auto grants=L" "+scopes+L" ";const bool broad=grants.find(L" https://www.googleapis.com/auth/calendar.readonly ")!=std::wstring::npos||
            grants.find(L" https://www.googleapis.com/auth/calendar ")!=std::wstring::npos;
        if(!broad&&(grants.find(L" https://www.googleapis.com/auth/calendar.events.readonly ")==std::wstring::npos||
            grants.find(L" https://www.googleapis.com/auth/calendar.calendarlist.readonly ")==std::wstring::npos)){
            error=L"Google Calendar needs both requested read-only permissions. Connect again and approve events and the calendar list.";return false;}
    }
    if(expires){*expires=300;const auto duration=object.find("expires_in");if(duration!=object.end()&&
        (!aip::notes::detail::ParseAsciiInt(duration->second,*expires)||*expires<1||*expires>86400)){error=L"Google returned an invalid access-token lifetime.";return false;}}
    for(auto& field:object)Wipe(field.second);return true;
}
inline bool Access(const Configuration& c,const Hooks& hooks,const Cancel& cancel,Sensitive& access,std::wstring& error,Session* session=nullptr) {
    if(!Configured(c,error,true)||Canceled(cancel))return false;
    const auto key=ConfigurationKey(c);
    if(session){std::lock_guard<std::mutex> lock(session->mutex);if(session->key==key&&session->expires>GetTickCount64()&&!session->accessToken.empty()){
        access.value=session->accessToken;return true;}}
    Sensitive refresh;if(!Unprotect(c,hooks,refresh,error))return false;int expires=0;
    auto body="grant_type=refresh_token&refresh_token="+Encode(refresh.value);const bool ok=Token(c,hooks,cancel,body,access,nullptr,error,&expires);Wipe(body);
    if(ok&&session&&!Canceled(cancel)){std::lock_guard<std::mutex> lock(session->mutex);Wipe(session->accessToken);session->key=key;session->accessToken=access.value;
        session->expires=GetTickCount64()+static_cast<ULONGLONG>((std::max)(0,expires-30))*1000;}
    return ok&&!Canceled(cancel);
}
inline bool Items(const std::string& json,Object& page,std::vector<std::string>& items,std::wstring& next,std::size_t limit) {
    if(!ReadObject(json,page)||!String(page,"nextPageToken",next,8192,true))return false;
    const auto found=page.find("items");return found==page.end()?(items.clear(),true):Array(found->second,items,limit);
}
} // detail

inline bool RuntimeConfigValidate(const Configuration& c,std::wstring& error,bool requireToken=false){return detail::Configured(c,error,requireToken);}
// Import the user's downloaded Google Cloud OAuth credentials file. Ignore its
// endpoint URLs entirely: this adapter sends credentials only to fixed Google
// hosts, and never accepts web clients or service-account private keys.
inline bool ImportDesktopClientJson(const std::string& json,Configuration& output,std::wstring& error) {
    if(json.size()>131072){error=L"Google OAuth credentials JSON must be at most 128 KiB.";return false;}
    detail::Object document,installed;
    if(!detail::ReadObject(json,document)||document.count("web")||document.count("private_key")||document.count("type")||
        document.count("installed")!=1||!detail::ReadObject(document.at("installed"),installed)){
        error=L"Choose credentials downloaded for a Google Cloud OAuth Desktop app, not a web app or service account.";return false;}
    Configuration result=output;
    if(!detail::String(installed,"client_id",result.clientId,2048)||!detail::String(installed,"client_secret",result.clientSecret,2048,true)){
        error=L"Google Desktop OAuth credentials are incomplete.";return false;}
    auto key=std::wstring(L"ClientId");if(result.clientId.empty()||!NormalizeSetting(key,result.clientId,error))return false;
    key=L"ClientSecret";if(!NormalizeSetting(key,result.clientSecret,error))return false;
    result.refreshTokenProtected.clear();output=std::move(result);error.clear();return true;
}
// ICU's documented ucal_getWindowsTimeZoneID maps Google's IANA identifiers to
// Windows time-zone IDs. Load only the OS System32 library, on demand. Unknown
// or unmappable IDs fail explicitly, never falling back to the local zone.
inline bool ResolveIanaWindowsTimeZone(const std::wstring& iana,std::wstring& windows) {
    windows.clear();if(iana.empty()||iana.size()>128||iana.find(L'\0')!=std::wstring::npos)return false;
    static std::mutex mutex;static std::map<std::wstring,std::wstring> cache;
    {std::lock_guard<std::mutex> lock(mutex);const auto found=cache.find(iana);if(found!=cache.end()){windows=found->second;return !windows.empty();}}
    HMODULE module=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);bool legacy=false;
    if(!module){module=LoadLibraryExW(L"icuin.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);legacy=module!=nullptr;}
    HRESULT apartment=E_FAIL;if(legacy)apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    using Convert=std::int32_t(__cdecl*)(const wchar_t*,std::int32_t,wchar_t*,std::int32_t,std::int32_t*);
    const auto convert=module?reinterpret_cast<Convert>(GetProcAddress(module,"ucal_getWindowsTimeZoneID")):nullptr;
    wchar_t output[256]{};std::int32_t status=0;
    const auto length=convert&&(!legacy||SUCCEEDED(apartment)||apartment==RPC_E_CHANGED_MODE)?convert(iana.c_str(),static_cast<std::int32_t>(iana.size()),output,static_cast<std::int32_t>(std::size(output)),&status):0;
    if(length>0&&length<static_cast<std::int32_t>(std::size(output))&&status<=0)windows.assign(output,static_cast<std::size_t>(length));
    if(legacy&&SUCCEEDED(apartment))CoUninitialize();if(module)FreeLibrary(module);
    {std::lock_guard<std::mutex> lock(mutex);if(cache.size()>=256)cache.erase(cache.begin());cache[iana]=windows;}
    return !windows.empty();
}
inline bool Connect(const Configuration& c,Credentials& credentials,std::wstring& error,const Cancel& cancel={},const Hooks& hooks={},Session* session=nullptr) {
    credentials={};if(!detail::Configured(c,error)||detail::Canceled(cancel))return false;
    const auto bounded=detail::Deadline(cancel,210000);
    detail::Sensitive verifier,code,access,refresh;std::wstring challenge,state,redirect;
    if(!detail::NewPkce(verifier.value,challenge,state)){error=L"Could not create secure Google sign-in proof.";return false;}
    const bool authorized=hooks.authorize?hooks.authorize(c,verifier.value,challenge,state,redirect,code.value,error,bounded):
        detail::NativeAuthorize(c,verifier.value,challenge,state,redirect,code.value,error,bounded,hooks);
    if(!authorized||detail::Canceled(bounded))return false;
    if(redirect.compare(0,17,L"http://127.0.0.1:")!=0||redirect.size()>256||code.value.empty()||code.value.size()>4096){error=L"Google sign-in returned an invalid local callback.";return false;}
    auto body="grant_type=authorization_code&code="+detail::Encode(code.value)+"&code_verifier="+detail::Encode(verifier.value)+"&redirect_uri="+detail::Encode(redirect);
    int expires=0;const auto exchanged=detail::Token(c,hooks,bounded,body,access,&refresh,error,&expires);detail::Wipe(body);if(!exchanged)return false;
    if(refresh.value.empty()){error=L"Google did not provide offline Calendar access. Reconnect and approve the requested read-only access.";return false;}
    if(detail::Canceled(bounded)||!detail::Protect(c,refresh.value,hooks,credentials.refreshTokenProtected,error))return false;
    if(session){auto configured=c;configured.refreshTokenProtected=credentials.refreshTokenProtected;std::lock_guard<std::mutex> lock(session->mutex);
        detail::Wipe(session->accessToken);session->key=ConfigurationKey(configured);session->accessToken=access.value;session->expires=GetTickCount64()+static_cast<ULONGLONG>((std::max)(0,expires-30))*1000;}
    return true;
}
namespace detail {
inline bool ListReadableCalendars(const Configuration& c,const std::wstring& access,std::vector<CalendarChoice>& output,std::wstring& error,
    const Cancel& bounded,const Hooks& hooks,std::size_t maximum) {
    output.clear();std::vector<CalendarChoice> gathered;std::wstring next;std::set<std::wstring> pages,ids;
    for(unsigned pageIndex=0;pageIndex<8;++pageIndex){std::string json;auto url=L"https://www.googleapis.com/calendar/v3/users/me/calendarList?maxResults=100&minAccessRole=reader&showDeleted=false";
        std::wstring request=url;if(!next.empty())request+=L"&pageToken="+detail::Query(next);
        if(c.allCalendars)request+=L"&showHidden=true";
        if(!detail::Request(c,hooks,bounded,request,access,json,error))return false;detail::Object page;std::vector<std::string> items;
        if(!detail::Items(json,page,items,next,100)){error=L"Google returned an invalid calendar list.";return false;}
        for(const auto& item:items){detail::Object row;CalendarChoice calendar;
            if(!detail::ReadObject(item,row)||!detail::String(row,"id",calendar.id,2048)||calendar.id.empty()||
                !detail::String(row,"summary",calendar.name,512,true)||!detail::String(row,"timeZone",calendar.timeZone,128,true)){error=L"Google returned an invalid calendar entry.";return false;}
            const auto primary=row.find("primary");calendar.primary=primary!=row.end()&&primary->second=="true";
            if(calendar.name.empty())calendar.name=L"Calendar";
            if(ids.insert(calendar.id).second){if(gathered.size()>=maximum){error=maximum<=100?
                L"All-calendars mode supports at most 100 readable calendars. Choose one calendar for this tile instead.":
                L"Google calendar list exceeds the bounded 800-calendar picker. Choose a calendar ID explicitly.";return false;}
                gathered.push_back(std::move(calendar));}
        }
        if(next.empty()){if(Canceled(bounded)){error=L"Google Calendar request canceled or timed out.";return false;}output=std::move(gathered);return true;}
        if(!pages.insert(next).second){error=L"Google returned a repeated calendar page.";return false;}
    }
    error=L"Google calendar list exceeds the bounded 800-calendar picker. Choose a calendar ID explicitly.";return false;
}
inline bool FetchCalendar(const Configuration& c,const std::wstring& calendarId,const std::wstring& timeZone,const std::wstring& access,
    aip::datetime::UTC minimum,aip::datetime::UTC maximum,std::vector<Event>& gathered,std::size_t& scanned,std::wstring& error,const Cancel& bounded,const Hooks& hooks) {
    std::wstring next;std::set<std::wstring> pages,ids;
    for(unsigned pageIndex=0;pageIndex<8;++pageIndex){
        auto url=L"https://www.googleapis.com/calendar/v3/calendars/"+detail::Query(calendarId)+
            L"/events?singleEvents=true&orderBy=startTime&showDeleted=false&maxResults=250&timeZone=UTC&timeMin="+detail::Query(Rfc3339(minimum))+L"&timeMax="+detail::Query(Rfc3339(maximum));
        if(!next.empty())url+=L"&pageToken="+detail::Query(next);std::string json;if(!detail::Request(c,hooks,bounded,url,access,json,error))return false;
        detail::Object page;std::vector<std::string> items;if(!detail::Items(json,page,items,next,250)){error=L"Google returned an invalid event page.";return false;}
        for(const auto& item:items){detail::Object row,start,end;Event event;std::wstring status;
            if(++scanned>4000){error=L"Google Calendar window exceeds the bounded 4000-row scan. Reduce Days ahead or select one calendar.";return false;}
            if(!detail::ReadObject(item,row)||!detail::String(row,"status",status,32,true)){error=L"Google returned an invalid event.";return false;}
            if(status==L"cancelled")continue;
            const auto startField=row.find("start"),endField=row.find("end");
            if(!detail::String(row,"id",event.id,1024)||event.id.empty()||!detail::String(row,"summary",event.summary,2048,true)||
                !detail::String(row,"location",event.location,2048,true)||startField==row.end()||endField==row.end()||
                !detail::ReadObject(startField->second,start)||!detail::ReadObject(endField->second,end)){error=L"Google returned an incomplete event.";return false;}
            if(event.summary.empty())event.summary=L"Untitled event";std::wstring first,last;
            event.allDay=start.count("date")!=0;
            if(event.allDay){
                if(!detail::String(start,"date",first,10)||!detail::String(end,"date",last,10)||!ParseDate(first,event.startDate)||!ParseDate(last,event.endDate)||
                    aip::datetime::Days(event.endDate)<=aip::datetime::Days(event.startDate)||start.count("dateTime")||end.count("dateTime")){
                    error=L"Google returned invalid all-day event dates.";return false;}
                // UTC response metadata does not change all-day wall dates.
                event.calendarTimeZone=timeZone;
            }else if(!detail::String(start,"dateTime",first,128)||!detail::String(end,"dateTime",last,128)||
                !ParseRfc3339(first,event.start)||!ParseRfc3339(last,event.end)||event.end<event.start){error=L"Google returned invalid event timestamps.";return false;}
            event.calendarId=calendarId;
            if(ids.insert(event.id).second){
                if(gathered.size()>=2000){error=L"Google Calendar window exceeds the bounded 2000-event limit. Reduce Days ahead or select one calendar.";return false;}
                if(c.allCalendars)event.id=std::to_wstring(calendarId.size())+L":"+calendarId+L":"+event.id;
                gathered.push_back(std::move(event));
            }
        }
        if(next.empty())return !detail::Canceled(bounded);
        if(!pages.insert(next).second){error=L"Google returned a repeated event page.";return false;}
    }
    error=L"Google Calendar event window exceeds eight pages. Reduce Days ahead or select one calendar.";return false;
}
inline bool AllDayUtc(const aip::datetime::Civil& date,const std::wstring& iana,std::map<std::wstring,DYNAMIC_TIME_ZONE_INFORMATION>& zones,
    aip::datetime::UTC& utc) {
    if(!aip::datetime::Valid(date)||iana.empty())return false;auto zone=zones.find(iana);
    if(zone==zones.end()){
        std::wstring windows;if(!ResolveIanaWindowsTimeZone(iana,windows))return false;DYNAMIC_TIME_ZONE_INFORMATION found{};bool matched=false;
        for(DWORD index=0;;++index){const auto status=EnumDynamicTimeZoneInformation(index,&found);if(status==ERROR_NO_MORE_ITEMS)break;
            if(status!=ERROR_SUCCESS)return false;if(windows==found.TimeZoneKeyName){matched=true;break;}}
        if(!matched)return false;zone=zones.emplace(iana,found).first;
    }
    TIME_ZONE_INFORMATION year{};if(!GetTimeZoneInformationForYear(static_cast<USHORT>(date.year),&zone->second,&year))return false;
    std::set<aip::datetime::UTC> candidates;
    for(const auto bias:{year.StandardBias,year.DaylightBias}){
        if(zone->second.DynamicDaylightTimeDisabled&&bias!=year.StandardBias)continue;
        const auto candidate=aip::datetime::Seconds(date)+static_cast<aip::datetime::UTC>(year.Bias+bias)*60;
        const auto civil=aip::datetime::FromSeconds(candidate);SYSTEMTIME source{},local{};source.wYear=static_cast<WORD>(civil.year);
        source.wMonth=static_cast<WORD>(civil.month);source.wDay=static_cast<WORD>(civil.day);source.wHour=static_cast<WORD>(civil.hour);
        source.wMinute=static_cast<WORD>(civil.minute);source.wSecond=static_cast<WORD>(civil.second);
        if(SystemTimeToTzSpecificLocalTimeEx(&zone->second,&source,&local)&&local.wYear==date.year&&local.wMonth==date.month&&local.wDay==date.day&&
            local.wHour==date.hour&&local.wMinute==date.minute&&local.wSecond==date.second)candidates.insert(candidate);
    }
    if(candidates.size()!=1)return false;utc=*candidates.begin();return true;
}
} // detail
inline bool ListCalendars(const Configuration& c,std::vector<CalendarChoice>& output,std::wstring& error,const Cancel& cancel={},const Hooks& hooks={},Session* session=nullptr) {
    output.clear();const auto bounded=detail::Deadline(cancel,45000,hooks.inert?hooks.monotonicNow:std::function<ULONGLONG()>{});
    detail::Sensitive access;if(!detail::Access(c,hooks,bounded,access,error,session))return false;
    if(detail::ListReadableCalendars(c,access.value,output,error,bounded,hooks,800))return true;if(session)session->Clear();return false;
}
inline bool FetchEvents(const Configuration& c,aip::datetime::UTC minimum,aip::datetime::UTC maximum,std::vector<Event>& output,
    std::wstring& error,const Cancel& cancel={},const Hooks& hooks={},Session* session=nullptr) {
    output.clear();if(minimum<aip::datetime::Seconds({1601,1,1})||maximum>aip::datetime::Seconds({9999,12,31,23,59,59})||
        maximum<=minimum||maximum-minimum>32LL*86400){error=L"Google Calendar query must cover a supported positive window of at most 32 days.";return false;}
    const auto bounded=detail::Deadline(cancel,45000,hooks.inert?hooks.monotonicNow:std::function<ULONGLONG()>{});
    detail::Sensitive access;if(!detail::Access(c,hooks,bounded,access,error,session))return false;
    std::vector<CalendarChoice> calendars;std::vector<Event> gathered;std::size_t scanned=0;
    const auto failed=[&]{if(session)session->Clear();if(error.empty())error=L"Google Calendar request canceled or timed out.";return false;};
    if(c.allCalendars){
        if(!detail::ListReadableCalendars(c,access.value,calendars,error,bounded,hooks,100))return failed();
        for(const auto& calendar:calendars){if(detail::Canceled(bounded)||!detail::FetchCalendar(c,calendar.id,calendar.timeZone,access.value,minimum,maximum,
            gathered,scanned,error,bounded,hooks))return failed();}
        std::map<std::wstring,DYNAMIC_TIME_ZONE_INFORMATION> zones;
        for(auto& event:gathered){if(detail::Canceled(bounded))return failed();if(event.allDay){
            if(!detail::AllDayUtc(event.startDate,event.calendarTimeZone,zones,event.start)||!detail::AllDayUtc(event.endDate,event.calendarTimeZone,zones,event.end)){
                error=L"An all-day Google Calendar event has an unavailable or ambiguous original time zone.";return failed();}
            event.allDayUtcResolved=true;}}
        std::stable_sort(gathered.begin(),gathered.end(),[](const Event& a,const Event& b){return a.start<b.start;});
    }else{
        const auto calendarId=c.calendarId.empty()?L"primary":c.calendarId;
        if(!detail::FetchCalendar(c,calendarId,L"",access.value,minimum,maximum,gathered,scanned,error,bounded,hooks))return failed();
        if(std::any_of(gathered.begin(),gathered.end(),[](const Event& e){return e.allDay;})){
            if(!detail::ListReadableCalendars(c,access.value,calendars,error,bounded,hooks,800))return failed();
            std::wstring originalZone;for(const auto& calendar:calendars)if(calendar.id==calendarId||(calendarId==L"primary"&&calendar.primary)){originalZone=calendar.timeZone;break;}
            if(originalZone.empty()){error=L"The selected Google calendar has no available original timezone for all-day events.";return failed();}
            for(auto& event:gathered)if(event.allDay)event.calendarTimeZone=originalZone;
        }
    }
    if(detail::Canceled(bounded))return failed();output=std::move(gathered);return true;
}
// Disconnect in the UI should always remove its local protected token even if
// this best-effort server revocation fails. Google revocation affects this
// Cloud project's other OAuth grants too; the host explains that action.
inline bool Revoke(const Configuration& c,std::wstring& error,const Cancel& cancel={},const Hooks& hooks={}) {
    if(!detail::Configured(c,error,true))return false;detail::Sensitive refresh;if(!detail::Unprotect(c,hooks,refresh,error))return false;
    auto body="token="+detail::Encode(refresh.value);std::string json;const auto ok=detail::Request(c,hooks,cancel,L"https://oauth2.googleapis.com/revoke",{},json,error,body,true);detail::Wipe(body);return ok;
}
} }
#endif

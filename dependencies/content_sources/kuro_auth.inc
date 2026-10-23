#pragma once
// Independent implementation of the explicit overseas game SDK password/code
// exchange verified against Wuthery/kuro.py 4be620b2... (2026-10-24).
#include "kuro_auth_model.h"
#include "kuro_notes.inc"
namespace aip { namespace kuro { namespace auth {
namespace detail {
using Fields=std::map<std::wstring,std::wstring>;
struct PrivateFields:Fields {using Fields::operator=;~PrivateFields(){for(auto& field:*this)Wipe(field.second);}};
struct PrivateBytes {std::string value;~PrivateBytes(){Wipe(value);}};
inline std::string PasswordEncoding(const std::wstring& password) {
    PrivateBytes bytes;bytes.value=aip::WideToUtf8(password);
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;encoded.reserve((bytes.value.size()+2)/3*4);
    for(size_t index=0;index<bytes.value.size();index+=3){
        const unsigned a=static_cast<unsigned char>(bytes.value[index]);
        const unsigned b=index+1<bytes.value.size()?static_cast<unsigned char>(bytes.value[index+1]):0;
        const unsigned c=index+2<bytes.value.size()?static_cast<unsigned char>(bytes.value[index+2]):0;
        encoded+=alphabet[a>>2];encoded+=alphabet[((a&3)<<4)|(b>>4)];
        encoded+=index+1<bytes.value.size()?alphabet[((b&15)<<2)|(c>>6)]:'=';
        encoded+=index+2<bytes.value.size()?alphabet[c&63]:'=';
    }
    for(size_t start:{size_t(0),size_t(1)})for(size_t index=start;index<encoded.size();index+=4){
        if(index+2<encoded.size())std::swap(encoded[index],encoded[index+2]);
        if(index+6>=encoded.size())break;
    }
    return encoded;
}
inline std::wstring Signature(const Fields& fields) {
    PrivateBytes source;
    for(const auto& field:fields)if(field.first!=L"sign"&&field.first!=L"market")
        source.value+=aip::WideToUtf8(field.first)+"="+aip::WideToUtf8(field.second)+"&";
    source.value+="32gh5r0p35ullmxrzzwk40ly";
    auto digest=aip::notes::detail::Md5Hex(source.value);if(digest.size()!=32)return {};
    std::swap(digest[1],digest[13]);std::swap(digest[5],digest[17]);std::swap(digest[7],digest[23]);
    return aip::Utf8ToWide(digest);
}
inline std::string Form(const Fields& fields) {
    std::string result;
    for(const auto& field:fields){if(!result.empty())result+='&';result+=aip::kuro::detail::FormValue(field.first)+"="+aip::kuro::detail::FormValue(field.second);}
    return result;
}
inline bool Send(const wchar_t* route,const Fields& fields,const HttpTransport& transport,const Cancel& canceled,
    std::chrono::steady_clock::time_point deadline,aip::kuro::detail::Members& response,std::wstring& message) {
    if((canceled&&canceled())||std::chrono::steady_clock::now()>=deadline){message=L"Sign-in was canceled or timed out.";return false;}
    aip::HttpFetchOptions options;options.url=route;options.method=L"POST";options.followRedirects=false;options.userAgent=L"DesktopStub";
    options.accept=L"application/json";options.headers={{L"Content-Type",L"application/x-www-form-urlencoded"}};
    options.body=Form(fields);options.maximumBytes=1048576;options.canceled=canceled;
    options.timeout=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
    aip::HttpFetchResponse result;std::wstring hidden;bool sent=false;
    try{sent=options.timeout.count()>0&&transport&&transport(options,result,hidden);}catch(...){}
    Wipe(options.body);Wipe(hidden);
    if(!sent||result.statusCode<200||result.statusCode>=300){
        message=result.statusCode==429?L"Kuro temporarily limited sign-in attempts. Wait before trying again.":
            L"The Kuro sign-in service could not be reached or rejected the request. Check the connection and retry.";
        if(!result.body.empty())SecureZeroMemory(result.body.data(),result.body.size());return false;
    }
    if((canceled&&canceled())||std::chrono::steady_clock::now()>=deadline){
        if(!result.body.empty())SecureZeroMemory(result.body.data(),result.body.size());message=L"Sign-in was canceled or timed out.";return false;}
    PrivateBytes document;document.value.assign(result.body.begin(),result.body.end());
    if(!result.body.empty())SecureZeroMemory(result.body.data(),result.body.size());
    if(document.value.size()>1048576||!aip::kuro::detail::Object(document.value,response)){
        message=L"The Kuro sign-in service returned an invalid response.";return false;}
    return true;
}
struct PrivateMembers:aip::kuro::detail::Members {
    void Clear(){for(auto& field:*this)Wipe(field.second);clear();}
    ~PrivateMembers(){Clear();}
};
}
inline bool ParseCaptchaMessage(const std::wstring& json,const std::wstring& expectedNonce,CaptchaProof& proof) {
    proof.Clear();if(json.size()>16384||expectedNonce.size()!=32)return false;
    aip::kuro::detail::Members values;if(!aip::kuro::detail::Object(aip::WideToUtf8(json),values)||values.size()!=6)return false;
    std::wstring nonce,captcha;
    if(!aip::kuro::detail::Wide(values["nonce"],nonce)||nonce!=expectedNonce||
        !aip::kuro::detail::Wide(values["captcha_id"],captcha)||captcha!=GameCaptchaId)return false;
    const bool valid=aip::kuro::detail::Wide(values["lot_number"],proof.lotNumber)&&aip::kuro::detail::Wide(values["pass_token"],proof.passToken)&&
        aip::kuro::detail::Wide(values["gen_time"],proof.genTime)&&aip::kuro::detail::Wide(values["captcha_output"],proof.captchaOutput)&&
        proof.Available()&&SafeText(proof.lotNumber,256)&&SafeText(proof.passToken,4096)&&SafeText(proof.genTime,64)&&SafeText(proof.captchaOutput,8192);
    if(!valid)proof.Clear();return valid;
}
inline std::wstring CaptchaHtml(const std::wstring& nonce) {
    if(nonce.size()!=32||!std::all_of(nonce.begin(),nonce.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))return {};
    return L"<!doctype html><html><head><meta charset='utf-8'><meta name='referrer' content='no-referrer'></head>"
        L"<body><p>Complete Kuro's verification below. Your password is never sent to this page.</p>"
        L"<button id='retry' disabled>Show verification</button><p id='status'>Loading verification...</p>"
        L"<script src='https://static.geetest.com/v4/gt4.js'></script><script>"
        L"if(typeof initGeetest4!=='function'){document.getElementById('status').textContent='Verification could not load. Close this window and retry sign-in.';}"
        L"else initGeetest4({captchaId:'1f4565ff7acc97b1a2fc97b921743aa4',product:'bind',language:'en'},function(c){"
        L"c.onReady(function(){document.getElementById('retry').disabled=false;document.getElementById('retry').onclick=function(){c.showCaptcha();};"
        L"document.getElementById('status').textContent='Complete the verification to continue sign-in.';c.showCaptcha();});"
        L"c.onSuccess(function(){var v=c.getValidate();if(!v)return;v.nonce='"+nonce+L"';window.chrome.webview.postMessage(JSON.stringify(v));});"
        L"c.onError(function(){document.getElementById('status').textContent='Verification failed to load. Close and retry sign-in.';});});</script></body></html>";
}
inline Result Login(const Credentials& credentials,const CaptchaProof& proof={},const HttpTransport& transport=aip::FetchHttpBytes,const Cancel& canceled={}) {
    Result result;
    if(!ValidCredentials(credentials)){result.message=L"Enter the Kuro email and password used for your overseas Wuthering Waves account.";return result;}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    detail::PrivateFields login;
    login={{L"__e__",L"1"},{L"email",credentials.email},{L"client_id",L"7rxmydkibzzsf12om5asjnoo"},{L"deviceNum",credentials.deviceId},
        {L"platform",L"PC"},{L"productId",L"A1730"},{L"productKey",L"5c063821193f41e09f1c4fdd7567dda3"},{L"projectId",L"G153"},
        {L"redirect_uri",L"1"},{L"response_type",L"code"},{L"sdkVersion",L"2.6.0h"},{L"channelId",L"240"}};
    detail::PrivateBytes encoded;encoded.value=detail::PasswordEncoding(credentials.password);login[L"password"]=aip::Utf8ToWide(encoded.value);
    login[L"sign"]=detail::Signature(login);if(login[L"sign"].empty()){result.message=L"Windows could not prepare the Kuro sign-in request.";return result;}
    // Upstream signs the login fields before appending the human verification.
    if(proof.Available()){
        login[L"geetestCaptchaOutput"]=proof.captchaOutput;login[L"geetestGenTime"]=proof.genTime;
        login[L"geetestLotNumber"]=proof.lotNumber;login[L"geetestPassToken"]=proof.passToken;
    }
    detail::PrivateMembers response;int status=-1;
    if(!detail::Send(L"https://sdkapi.kurogame-service.com/sdkcom/v2/login/emailPwd.lg",login,transport,canceled,deadline,response,result.message))return result;
    if(!aip::kuro::detail::ApiStatus(response,"codes",status)){result.message=L"The Kuro sign-in response has no valid status.";return result;}
    if(status==41000){result.status=Status::CaptchaRequired;result.message=L"Complete Kuro's human verification to continue signing in.";return result;}
    if(status!=0){result.message=L"Kuro did not accept this sign-in. Check the linked Kuro email/password or account verification, then retry.";return result;}
    std::wstring code;if(!aip::kuro::detail::Wide(response["code"],code)||code.empty()||!SafeText(code,16384)){
        result.message=L"Kuro accepted sign-in without returning the required authorization code.";return result;}
    detail::PrivateFields exchange;
    exchange={{L"client_id",L"7rxmydkibzzsf12om5asjnoo"},{L"deviceNum",credentials.deviceId},{L"client_secret",L"32gh5r0p35ullmxrzzwk40ly"},
        {L"code",code},{L"productId",L"A1725"},{L"projectId",L"G153"},{L"redirect_uri",L"1"},{L"grant_type",L"authorization_code"}};
    Wipe(code);exchange[L"sign"]=detail::Signature(exchange);response.Clear();
    if(exchange[L"sign"].empty()){result.message=L"Windows could not prepare the Kuro authorization exchange.";return result;}
    if(!detail::Send(L"https://sdkapi.kurogame-service.com/sdkcom/v2/auth/getToken.lg",exchange,transport,canceled,deadline,response,result.message))return result;
    if(!aip::kuro::detail::ApiStatus(response,"codes",status)||status!=0||!aip::kuro::detail::Wide(response["access_token"],result.token)||
        result.token.empty()||!SafeText(result.token,16384)){
        Wipe(result.token);result.message=L"Kuro could not exchange sign-in for a game access token. Retry sign-in.";return result;}
    if(response.count("expires_in"))aip::kuro::detail::Number(response,"expires_in",result.expiresIn,0);
    result.status=Status::Authenticated;result.message=L"Signed in. Finding linked Wuthering Waves roles...";return result;
}
}}}

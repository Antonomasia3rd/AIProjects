#include "../dependencies/content_sources/kuro_auth.inc"
#include "../dependencies/content_sources/kuro_signin_layout.h"
#include <iostream>
namespace {
int checks=0,failures=0;
void Check(bool condition,const char* message){++checks;if(!condition){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
using namespace aip::kuro;using namespace aip::kuro::auth;
void Reply(aip::HttpFetchResponse& response,const std::string& json,int status=200){response.statusCode=status;response.body.assign(json.begin(),json.end());}
void Input(Credentials& input){input.email=L"fixture@example.invalid";input.password=L"password";input.deviceId=L"01234567-89AB-4DEF-8123-456789ABCDEF";}
const std::wstring nonce=L"0123456789abcdef0123456789abcdef";
const std::wstring proofJson=L"{\"nonce\":\""+nonce+L"\",\"captcha_id\":\"1f4565ff7acc97b1a2fc97b921743aa4\",\"lot_number\":\"LOT\",\"pass_token\":\"PASS\",\"gen_time\":\"123\",\"captcha_output\":\"OUTPUT\"}";
void ModelTests(){
    Credentials input;Check(!ValidCredentials(input),"empty credentials fail without transport");Input(input);Check(ValidCredentials(input),"bounded manual credentials accepted");
    input.email=L"missing-at";Check(!ValidCredentials(input),"obviously malformed email rejected");input.email=L"fixture@example.invalid";
    input.password=L"line\nbreak";Check(!ValidCredentials(input),"control characters rejected in manual password");input.password=L"password";
    Check(aip::kuro::auth::detail::PasswordEncoding(input.password)=="FzcGdvc3Q=cm","password encoding matches independently computed Base64/shuffle reference");
    aip::kuro::auth::detail::Fields fields{{L"z",L"2"},{L"a",L"1"},{L"sign",L"old"},{L"market",L"ignored"}};
    Check(aip::kuro::auth::detail::Signature(fields)==L"66b28962e4313fad92096bac1de9f182","signature sorts fields and excludes sign/market as upstream");
    std::wstring secret=L"temporary password";Wipe(secret);Check(secret.empty(),"explicit secret buffer clear empties the temporary input");
    CaptchaProof proof;Check(ParseCaptchaMessage(proofJson,nonce,proof)&&proof.Available()&&proof.lotNumber==L"LOT","strict nonce-bound human CAPTCHA proof parses");
    Check(!ParseCaptchaMessage(proofJson,L"ffffffffffffffffffffffffffffffff",proof)&&!proof.Available(),"another sign-in nonce cannot submit CAPTCHA proof");
    for(const auto& invalid:std::vector<std::wstring>{L"[]",proofJson+L"x",L"{\"nonce\":\"x\"}",std::wstring(16385,L'x')})
        Check(!ParseCaptchaMessage(invalid,nonce,proof)&&!proof.Available(),"malformed/oversized CAPTCHA messages fail closed");
    auto duplicate=proofJson;duplicate.insert(1,L"\"nonce\":\"x\",");Check(!ParseCaptchaMessage(duplicate,nonce,proof),"duplicate CAPTCHA message keys rejected");
    auto other=proofJson;other.replace(other.find(GameCaptchaId),32,L"ec4aa4174277d822d73f2442a165a2cd");Check(!ParseCaptchaMessage(other,nonce,proof),"KuroBBS CAPTCHA cannot stand in for game CAPTCHA");
    Check(CaptchaHtml(L"unsafe'nonce").empty(),"CAPTCHA page rejects untrusted script nonce syntax");
    const auto page=CaptchaHtml(nonce);
    Check(page.find(L"https://static.geetest.com/v4/gt4.js")!=std::wstring::npos&&page.find(GameCaptchaId)!=std::wstring::npos&&
        page.find(L"window.chrome.webview.postMessage")!=std::wstring::npos,"human verification page uses the verified public script/id and isolated message callback");
    input.password=L"UNIQUE-PRIVATE-PASSWORD-NEVER-IN-BROWSER";
    Check(page.find(input.email)==std::wstring::npos&&page.find(input.password)==std::wstring::npos&&page.find(L"access_token")==std::wstring::npos,
        "verification document never embeds account email, password or game token");
    CaptchaStage stage=CaptchaStage::Error;
    Check(ParseCaptchaStageMessage(L"{\"nonce\":\""+nonce+L"\",\"stage\":\"ready\"}",nonce,stage)&&stage==CaptchaStage::Ready,
        "challenge readiness requires the current native session nonce");
    Check(ParseCaptchaStageMessage(L"{\"nonce\":\""+nonce+L"\",\"stage\":\"error\"}",nonce,stage)&&stage==CaptchaStage::Error,
        "challenge script errors return through the same nonce-bound channel");
    for(const auto& invalid:std::vector<std::wstring>{L"{\"stage\":\"ready\"}",L"{\"nonce\":\"wrong\",\"stage\":\"ready\"}",
        L"{\"nonce\":\""+nonce+L"\",\"stage\":\"ready\",\"extra\":true}",L"{\"nonce\":\""+nonce+L"\",\"stage\":\"loaded\"}",proofJson})
        Check(!ParseCaptchaStageMessage(invalid,nonce,stage),"foreign, extra, unknown and proof-only messages cannot masquerade as challenge readiness");
    Check(page.find(L"onReady(function()")!=std::wstring::npos&&page.find(L"window.kuroNotice('ready')")!=std::wstring::npos&&
        page.find(L"onerror='window.kuroFailure()'")!=std::wstring::npos&&page.find(L"c.onError(function(){window.kuroFailure();})")!=std::wstring::npos,
        "actual challenge HTML reports GeeTest readiness and script/service failures rather than relying on blank document navigation");
}
void FlowTests(){
    Credentials input;Input(input);int calls=0;std::string firstBody;
    const HttpTransport transport=[&](const auto& options,auto& response,auto&){
        ++calls;Check(options.method==L"POST"&&!options.followRedirects&&options.url.find(L'?')==std::wstring::npos,"all sign-in steps use fixed HTTPS POST with no redirects or query credentials");
        Check(options.timeout.count()>0&&options.timeout<=std::chrono::seconds(30)&&options.maximumBytes==1048576,"each sign-in step has a bounded shared timeout and response limit");
        Check(options.body.find("password=password")==std::string::npos&&options.url.find(input.email)==std::wstring::npos,"wire password encoding is not a raw-password URL or form");
        if(calls==1){firstBody=options.body;
            Check(options.url==L"https://sdkapi.kurogame-service.com/sdkcom/v2/login/emailPwd.lg"&&options.body.find("email=fixture%40example.invalid")!=std::string::npos&&
                options.body.find("password=FzcGdvc3Q%3Dcm")!=std::string::npos&&options.body.find("productId=A1730")!=std::string::npos&&options.body.find("sign=")!=std::string::npos,
                "password login carries the verified SDK fields and encoded password");
            Reply(response,R"({"codes":0,"code":"PRIVATE-AUTH-CODE"})");
        }else{
            Check(options.url==L"https://sdkapi.kurogame-service.com/sdkcom/v2/auth/getToken.lg"&&options.body.find("grant_type=authorization_code")!=std::string::npos&&
                options.body.find("code=PRIVATE-AUTH-CODE")!=std::string::npos&&options.body.find("productId=A1725")!=std::string::npos&&options.body.find("password=")==std::string::npos,
                "authorization exchange uses the verified second request without forwarding the password");
            Reply(response,R"({"codes":0,"access_token":"PRIVATE-GAME-TOKEN","expires_in":3600})");
        }return true;
    };
    auto result=Login(input,{},transport);Check(calls==2&&result.status==Status::Authenticated&&result.token==L"PRIVATE-GAME-TOKEN"&&result.expiresIn==3600,"explicit manual sign-in returns game token only after successful code exchange");
    Check(result.message.find(result.token)==std::wstring::npos,"visible sign-in status never includes returned token");
    calls=0;result=Login(input,{},[&](const auto&,auto& response,auto&){++calls;Reply(response,R"({"codes":41000,"msg":"password PRIVATE-GAME-TOKEN"})");return true;});
    Check(calls==1&&result.status==Status::CaptchaRequired&&result.token.empty()&&result.message.find(L"PRIVATE")==std::wstring::npos,"human verification request is distinct from credential failure and echoes no server secrets");
    CaptchaProof proof;ParseCaptchaMessage(proofJson,nonce,proof);calls=0;
    result=Login(input,proof,[&](const auto& options,auto& response,auto&){++calls;
        Check(options.body.find("geetestCaptchaOutput=OUTPUT")!=std::string::npos&&options.body.find("geetestPassToken=PASS")!=std::string::npos,
            "completed human challenge passes the four verified geetest fields to login");
        auto oldSign=firstBody.substr(firstBody.find("sign=")+5);auto newSign=options.body.substr(options.body.find("sign=")+5);
        Check(oldSign==newSign,"login signature is computed before geetest fields as required upstream");Reply(response,R"({"codes":-1})");return true;});
    Check(calls==1&&result.status==Status::Error&&result.token.empty(),"rejected verified sign-in never exchanges or stores a token");
    calls=0;result=Login(input,{},[&](const auto&,auto&,auto&){++calls;return false;},[]{return true;});Check(calls==0&&result.status==Status::Error,"pre-canceled sign-in executes no HTTP request");
    bool canceled=false;calls=0;result=Login(input,{},[&](const auto&,auto& response,auto&){++calls;canceled=true;Reply(response,R"({"codes":0,"code":"CODE"})");return true;},[&]{return canceled;});
    Check(calls==1&&result.status==Status::Error&&result.token.empty(),"cancellation after response prevents authorization exchange");
    for(int status:{302,401,429,500}){calls=0;result=Login(input,{},[&](const auto&,auto& response,auto& hidden){++calls;hidden=L"password email PRIVATE-TOKEN";Reply(response,"PRIVATE-TOKEN",status);return true;});
        Check(calls==1&&result.status==Status::Error&&result.token.empty()&&result.message.find(L"PRIVATE")==std::wstring::npos,"HTTP failures/redirects remain redacted and never advance the login flow");}
    for(const auto& body:std::vector<std::string>{"[]",R"({"codes":0})",R"({"codes":0,"code":"CODE","code":"OTHER"})",R"({"codes":-1,"msg":"fixture@example.invalid password"})"}){
        calls=0;result=Login(input,{},[&](const auto&,auto& response,auto&){++calls;Reply(response,body);return true;});
        Check(calls==1&&result.status==Status::Error&&result.token.empty()&&result.message.find(input.email)==std::wstring::npos,"invalid login envelopes stop safely without leaking echoed credentials");}
    calls=0;result=Login(input,{},[&](const auto&,auto& response,auto&){Reply(response,++calls==1?R"({"codes":0,"code":"CODE"})":R"({"codes":0,"access_token":"bad\nvalue"})");return true;});
    Check(calls==2&&result.status==Status::Error&&result.token.empty(),"unsafe returned token is never accepted");
    Credentials empty;calls=0;result=Login(empty,{},[&](const auto&,auto&,auto&){++calls;return false;});Check(calls==0&&result.status==Status::Error,"empty manual inputs cannot launch network activity");
}
void RoleRetryTests(){
    Result signedIn;signedIn.status=Status::Authenticated;signedIn.token=L"PRIVATE-REUSABLE-GAME-TOKEN";
    std::vector<Role> roles;std::wstring error;int calls=0;
    const auto unavailable=[&](const aip::HttpFetchOptions& options,auto& response,auto&){++calls;
        Check(options.url.find(L"emailPwd.lg")==std::wstring::npos&&options.url.find(L"getToken.lg")==std::wstring::npos,
            "character retry performs no password login or authorization-code exchange");
        Reply(response,calls==1?R"({"codes":0,"oauthCode":"CODE"})":R"({"code":1005,"message":"PRIVATE-REUSABLE-GAME-TOKEN"})");return true;};
    Check(!DiscoverSignedInRoles(signedIn,L"FIXTURE-DEVICE",roles,error,unavailable)&&calls==5&&roles.empty()&&
        signedIn.status==Status::Authenticated&&signedIn.token==L"PRIVATE-REUSABLE-GAME-TOKEN"&&error.find(L"1005")!=std::wstring::npos&&
        error.find(signedIn.token)==std::wstring::npos,"retryable role lookup failure preserves successful temporary sign-in and redacts remote messages");
    calls=0;
    Check(DiscoverSignedInRoles(signedIn,L"FIXTURE-DEVICE",roles,error,[&](const auto& options,auto& response,auto&){++calls;
        Check(options.url.find(L"emailPwd.lg")==std::wstring::npos,"successful character retry still does not need the password");
        Reply(response,calls==1?R"({"codes":0,"oauthCode":"NEW-CODE"})":R"({"code":0,"data":{"Asia":"{\"roleId\":12345,\"roleName\":\"Rover\",\"level\":60}"}})");return true;})&&
        calls==2&&roles.size()==1&&roles.front().region==L"Asia"&&signedIn.token==L"PRIVATE-REUSABLE-GAME-TOKEN",
        "a new lookup can discover the character using the same explicit temporary token");
    Result absent;calls=0;Check(!DiscoverSignedInRoles(absent,L"FIXTURE",roles,error,[&](const auto&,auto&,auto&){++calls;return false;})&&calls==0,
        "character retry cannot start without an authenticated temporary session");
}
void LayoutTests(){
    using namespace aip::kuro::signin;
    Check(CompactHeight==300&&ExpandedHeight>CompactHeight,"sign-in opens compact and expands only for a visible challenge");
    for(int width:{624,744,1000})for(bool challenge:{false,true}){
        const int height=challenge?640:260;const auto layout=Compute(width,height,challenge);
        for(const auto& box:{layout.heading,layout.emailLabel,layout.email,layout.passwordLabel,layout.password,layout.login,layout.retry,layout.save,layout.close,layout.status})
            Check(box.x>=0&&box.y>=0&&box.width>0&&box.height>0&&box.Right()<=width&&box.Bottom()<=height,
                "resizable sign-in controls remain within the supported client bounds");
        Check(layout.login.Right()<layout.retry.x&&layout.retry.Right()<layout.save.x&&layout.save.Right()<layout.close.x,
            "sign-in/retry/save/close controls do not overlap at the minimum supported width");
        Check(challenge?(layout.browser.y==BrowserTop&&layout.browser.height>0&&layout.browser.Bottom()<=height):
            (layout.browser.width==0&&layout.browser.height==0),"the compact layout reserves no empty browser area");
    }
}
}
int main(){ModelTests();FlowTests();RoleRetryTests();LayoutTests();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}

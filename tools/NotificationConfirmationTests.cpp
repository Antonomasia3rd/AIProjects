#include "../dependencies/notification_confirmation.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
using namespace aip::notifications;
namespace{
int checks=0;
void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
std::wstring Token(unsigned value){std::wostringstream text;text<<std::hex<<std::setw(32)<<std::setfill(L'0')<<value;return text.str();}
void RegistryTests(){
    ConfirmationRegistry state;const auto first=Token(1),second=Token(2);
    Check(state.Add(L"delivery:rss",first,L"user/profile-a",1000),"add valid session-owned confirmation");
    Check(!state.Resolve(first,ConfirmationChoice::Yes,L"user/profile-b",1001)&&state.Entries().size()==1,"another profile cannot approve or consume request");
    Check(!state.Resolve(first,static_cast<ConfirmationChoice>(99),L"user/profile-a",1001),"unknown decision cannot consume confirmation");
    const auto yes=state.Resolve(first,ConfirmationChoice::Yes,L"user/profile-a",1002);
    Check(yes&&yes->approved&&yes->key==L"delivery:rss"&&yes->token==first&&state.Entries().empty(),"Yes consumes and identifies exact operation once");
    Check(!state.Resolve(first,ConfirmationChoice::Yes,L"user/profile-a",1003)&&!state.Add(L"delivery:rss",first,L"user/profile-a",1003),"replay and accidental recently consumed nonce reuse rejected");
    Check(state.Add(L"delivery:rss",second,L"user/profile-a",1004),"fresh nonce may replace completed operation");
    const auto no=state.Resolve(second,ConfirmationChoice::No,L"user/profile-a",1005);
    Check(no&&!no->approved&&state.Entries().empty()&&!state.Resolve(second,ConfirmationChoice::Yes,L"user/profile-a",1006),"No consumes request without authorizing action");
    Check(state.Add(L"same",Token(3),L"profile",2000,100)&&state.Add(L"same",Token(4),L"profile",2001,100)&&state.Entries().size()==1,"same key/profile replaces stale question with fresh nonce");
    Check(!state.Resolve(Token(3),ConfirmationChoice::Yes,L"profile",2002)&&!state.Add(L"same",Token(3),L"profile",2002),"superseded request cannot be approved or reused");
    Check(state.Add(L"same",Token(5),L"other-profile",2002,100)&&state.Entries().size()==2,"same operation key remains separate across profiles");
    Check(!state.Add(L"other-key",Token(4),L"profile",2003),"one nonce cannot identify two operations");
    state.Expire(2101);Check(state.Entries().size()==1,"deadline is exclusive and expiry removes old request");
    Check(!state.Resolve(Token(4),ConfirmationChoice::Yes,L"profile",2101),"expired Yes does nothing");
    Check(state.CancelKey(L"same",L"other-profile")&&state.Entries().empty(),"explicit cancellation removes matching owner without approval");
    Check(state.Add(L"clock",Token(6),L"profile",3000)&&!state.Resolve(Token(6),ConfirmationChoice::Yes,L"profile",2999)&&state.Entries().empty(),"monotonic clock rollback fails closed");
    Check(!state.Add(L"",Token(7),L"profile",4000)&&!state.Add(L"key",Token(7),L"",4000)&&!state.Add(L"key",L"not-random-token",L"profile",4000),"empty owner/key and malformed token rejected");
    Check(!state.Add(L"key",Token(7),L"profile",4000,0)&&!state.Add(L"key",Token(7),L"profile",4000,86400001)&&
        !state.Add(L"key",Token(7),L"profile",(std::numeric_limits<std::uint64_t>::max)()-5,6),"zero, unbounded and overflowing expirations rejected");
    for(unsigned i=0;i<32;++i)Check(state.Add(L"operation"+std::to_wstring(i),Token(100+i),L"profile",5000),"bounded capacity accepts distinct operations");
    Check(state.Entries().size()==32&&!state.Add(L"overflow",Token(999),L"profile",5001),"capacity refuses new request rather than silently approving/dropping older work");
    Check(state.Add(L"operation0",Token(1000),L"profile",5002)&&state.Entries().size()==32,"capacity allows a fresh replacement of same operation");
    Check(state.CancelToken(Token(1000))&&!state.CancelToken(Token(1000)),"token cancellation is one-shot");
    Check(state.Add(state.Entries().front().key,Token(1001),state.Entries().front().profile,5003)&&state.Entries().size()==31,
        "replacement accepts key/profile references from Entries without invalidating its own input");
    state.Clear();Check(state.Entries().empty(),"session end drops all approval capability");
    for(unsigned i=0;i<250;++i){const auto token=Token(2000+i);Check(state.Add(L"repeat",token,L"profile",10000+i)&&state.Resolve(token,ConfirmationChoice::Yes,L"profile",10000+i)->approved,"bounded repeated lifecycle does not accumulate active requests");}
    Check(state.Entries().empty(),"long lifecycle retains no pending requests");
}
void ArgumentTests(){
    const auto token=Token(42);ConfirmationAction action;action.token=L"sentinel";
    Check(ParseConfirmationAction(L"confirm="+token+L"&choice=yes",action)&&action.token==token&&action.choice==ConfirmationChoice::Yes,"parse exact Yes callback arguments");
    Check(ParseConfirmationAction(L"confirm="+token+L"&choice=no",action)&&action.choice==ConfirmationChoice::No,"parse exact No callback arguments");
    for(const auto& arguments:std::vector<std::wstring>{L"",L"review",L"yes",L"confirm="+token,L"confirm="+token+L"&choice=open",L"confirm="+token+L"&choice=yes&extra=1",
        L"confirm="+token+L"&choice=yes ",L"choice=yes&confirm="+token,L"confirm="+std::wstring(32,L'A')+L"&choice=yes",L"confirm="+std::wstring(10000,L'a')+L"&choice=yes"}){
        action.token=L"sentinel";Check(!ParseConfirmationAction(arguments,action)&&action.token==L"sentinel","body/malformed/uppercase/extra/oversized callback cannot resolve approval");
    }
}
void XmlTests(){
    const auto token=Token(42);const auto xml=XMLConfirmationXml(L"Title <&>",L"Keep old pin?",token,L"Yes \"now\"",L"No & stay");
    Check(xml.find(L"Title &lt;&amp;&gt;")!=std::wstring::npos&&xml.find(L"Yes &quot;now&quot;")!=std::wstring::npos&&xml.find(L"No &amp; stay")!=std::wstring::npos,"text and action labels are XML escaped");
    Check(xml.find(L"confirm="+token+L"&amp;choice=yes")!=std::wstring::npos&&xml.find(L"confirm="+token+L"&amp;choice=no")!=std::wstring::npos&&xml.find(L"activationType=\"foreground\"")!=std::wstring::npos,"only explicit inline buttons carry decisions");
    Check(xml.find(L"launch=\"review\"")!=std::wstring::npos&&xml.find(L"protocol")==std::wstring::npos&&xml.find(L"background")==std::wstring::npos,"body click is harmless and payload invents no protocol/background registration");
    Check(XMLConfirmationXml(L"t",L"m",L"invalid").empty(),"malformed token cannot produce a toast");
    std::wstring unicode(127,L'a');unicode+=static_cast<wchar_t>(0xd83d);unicode+=static_cast<wchar_t>(0xde00);
    Check(ConfirmationXmlText(unicode,128)==std::wstring(127,L'a'),"truncation never cuts a UTF-16 surrogate pair");
    Check(ConfirmationXmlText(std::wstring{L'a',wchar_t(0),wchar_t(1),wchar_t(0xd800),L'b',wchar_t(0xdc00),wchar_t(0xffff)},128)==L"ab","invalid controls, noncharacters and lone surrogates cannot invalidate XML");
    Check(XMLConfirmationXml(L"t",L"m",token,L"",L"").find(L"content=\"Yes\"")!=std::wstring::npos,"empty localized foreground labels retain readable safe defaults");
}
}
int main(){try{RegistryTests();ArgumentTests();XmlTests();std::cout<<checks<<" notification confirmation checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"Failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}

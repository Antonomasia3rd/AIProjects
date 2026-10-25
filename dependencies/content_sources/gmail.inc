#pragma once
// Bounded read-only Gmail inbox data. Uses purpose-bound shared Google OAuth,
// but never Calendar endpoints/grants or message bodies/attachments/remote art.
#include "gmail_model.h"
#include "google_calendar.inc"
namespace aip { namespace gmail {
using Hooks=aip::google_calendar::Hooks;using Session=aip::google_calendar::Session;using Cancel=aip::google_calendar::Cancel;
namespace detail {
namespace google=aip::google_calendar::detail;
inline std::wstring MailError(std::wstring error){std::size_t at=0;while((at=error.find(L"Calendar",at))!=std::wstring::npos){error.replace(at,8,L"Mail");at+=4;}return error;}
inline bool Id(const std::wstring& id){return !id.empty()&&id.size()<=256&&std::all_of(id.begin(),id.end(),[](wchar_t c){return(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'_';});}
inline std::wstring Clean(std::wstring text,std::size_t maximum=2048){
    for(auto& c:text)if(c<0x20||c==0x7f)c=L' ';return aip::datetime::Trim(text.substr(0,maximum));
}
inline std::wstring DecodeWord(const std::wstring& word){
    if(word.size()>8192||word.compare(0,2,L"=?")!=0)return word;
    const auto charsetEnd=word.find(L'?',2);if(charsetEnd==std::wstring::npos||charsetEnd+3>=word.size()||word[charsetEnd+2]!=L'?'||word.substr(word.size()-2)!=L"?=")return word;
    const auto charset=aip::datetime::Lower(word.substr(2,charsetEnd-2));const auto encoding=word[charsetEnd+1];
    const auto encoded=word.substr(charsetEnd+3,word.size()-charsetEnd-5);std::string bytes;
    if(encoding==L'B'||encoding==L'b'){
        DWORD size=0;if(!CryptStringToBinaryW(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr)||size>8192)return word;
        std::vector<BYTE> value(size);if(!CryptStringToBinaryW(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,value.data(),&size,nullptr,nullptr))return word;
        bytes.assign(value.begin(),value.begin()+size);
    }else if(encoding==L'Q'||encoding==L'q'){
        const auto hex=[](wchar_t c){return c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:-1;};
        for(std::size_t i=0;i<encoded.size();++i){auto c=encoded[i];if(c==L'_')bytes+=' ';else if(c==L'='){
            if(i+2>=encoded.size())return word;const auto a=hex(encoded[++i]),b=hex(encoded[++i]);if(a<0||b<0)return word;bytes+=static_cast<char>((a<<4)|b);
        }else if(c>0x7f)return word;else bytes+=static_cast<char>(c);}
    }else return word;
    std::wstring decoded;
    if(charset==L"utf-8"||charset==L"utf8"){if(!aip::TryUtf8ToWide(bytes,decoded))return word;}
    else if(charset==L"us-ascii"||charset==L"ascii"||charset==L"iso-8859-1"||charset==L"windows-1252"){
        const auto page=charset==L"windows-1252"?1252u:charset==L"iso-8859-1"?28591u:20127u;
        const auto length=MultiByteToWideChar(page,0,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);if(length<=0)return word;
        decoded.resize(length);if(!MultiByteToWideChar(page,0,bytes.data(),static_cast<int>(bytes.size()),&decoded[0],length))return word;
    }else return word;return Clean(std::move(decoded));
}
inline std::wstring Header(std::wstring text){
    std::wstring result;std::size_t at=0;
    while(at<text.size()&&result.size()<2048){const auto begin=text.find(L"=?",at);
        if(begin==std::wstring::npos){result+=text.substr(at,2048-result.size());break;}result+=text.substr(at,begin-at);
        const auto end=text.find(L"?=",begin+2);if(end==std::wstring::npos){result+=text.substr(begin,2048-result.size());break;}
        result+=DecodeWord(text.substr(begin,end+2-begin));at=end+2;
        auto next=at;while(next<text.size()&&(text[next]==L' '||text[next]==L'\t'||text[next]==L'\r'||text[next]==L'\n'))++next;
        if(text.compare(next,2,L"=?")==0)at=next;
    }
    return Clean(std::move(result));
}
inline std::wstring Sender(std::wstring text){
    text=Header(std::move(text));const auto address=text.find(L'<');
    if(address!=std::wstring::npos&&address>0){auto name=aip::datetime::Trim(text.substr(0,address));
        if(name.size()>1&&name.front()==L'\"'&&name.back()==L'\"')name=name.substr(1,name.size()-2);if(!name.empty())return name;}
    return text;
}
inline std::wstring Snippet(std::wstring text){
    for(const auto& item:std::vector<std::pair<const wchar_t*,const wchar_t*>>{{L"&quot;",L"\""},{L"&#39;",L"'"},{L"&apos;",L"'"},{L"&nbsp;",L" "},{L"&lt;",L"<"},{L"&gt;",L">"},{L"&amp;",L"&"}}){
        std::size_t at=0;const auto length=wcslen(item.first);while((at=text.find(item.first,at))!=std::wstring::npos){text.replace(at,length,item.second);at+=wcslen(item.second);}}
    return Clean(std::move(text),4096);
}
inline bool Count(const google::Object& object,const char* key,int& value){const auto field=object.find(key);value=0;return field==object.end()||(aip::notes::detail::ParseAsciiInt(field->second,value)&&value>=0);}
inline bool MessageRow(const std::string& json,const std::wstring& expected,const Configuration& cfg,Message& output){
    google::Object row,payload;Message message;std::wstring date;
    if(!google::ReadObject(json,row)||!google::String(row,"id",message.id,256)||message.id!=expected||
        !google::String(row,"internalDate",date,32,true))return false;
    if(!date.empty()){
        std::uint64_t number=0;for(const auto c:date){if(c<L'0'||c>L'9'||number>25340230079999ULL)return false;number=number*10+static_cast<unsigned>(c-L'0');}
        if(number>253402300799999ULL)return false;message.receivedUtc=static_cast<aip::datetime::UTC>(number/1000);
    }
    const auto body=row.find("payload");if(body!=row.end()){
        if(!google::ReadObject(body->second,payload))return false;const auto headers=payload.find("headers");
        if(headers!=payload.end()){
            std::vector<std::string> values;if(!google::Array(headers->second,values,100))return false;bool sender=false,subject=false;
            for(const auto& raw:values){google::Object header;std::wstring name,value;
                if(!google::ReadObject(raw,header)||!google::String(header,"name",name,128)||!google::String(header,"value",value,8192))return false;
                name=aip::datetime::Lower(name);
                if(name==L"from"){if(sender)return false;sender=true;message.sender=Sender(std::move(value));}
                else if(name==L"subject"){if(subject)return false;subject=true;message.subject=Header(std::move(value));}
            }
        }
    }
    if(cfg.preview==L"Snippet"){
        std::wstring snippet;if(!google::String(row,"snippet",snippet,8192,true))return false;message.snippet=Snippet(std::move(snippet));
    }
    output=std::move(message);return true;
}
}
inline bool FetchInbox(const Configuration& configuration,Snapshot& output,std::wstring& error,const Cancel& cancel={},const Hooks& hooks={},Session* session=nullptr){
    output={};error.clear();Configuration cfg=configuration;cfg.account=AccountConfiguration(cfg);
    const auto fail=[&]{if(session)session->Clear();if(error.empty())error=L"Gmail refresh was canceled or timed out.";error=detail::MailError(error);output={};output.error=error;return false;};
    if(cfg.maxMessages<1||cfg.maxMessages>5||(cfg.preview!=L"Headers"&&cfg.preview!=L"Snippet")){error=L"Mail preview settings are invalid.";return fail();}
    const auto bounded=aip::google_calendar::detail::Deadline(cancel,45000,hooks.inert?hooks.monotonicNow:std::function<ULONGLONG()>{});
    aip::google_calendar::detail::Sensitive access;
    if(!aip::google_calendar::detail::Access(cfg.account,hooks,bounded,access,error,session))return fail();
    std::size_t remaining=static_cast<std::size_t>(cfg.account.maximumBytes);
    const auto request=[&](const std::wstring& url,std::string& json){
        if(!remaining){error=L"Gmail inbox data exceeded this tile's aggregate response limit.";return false;}
        auto options=cfg.account;options.maximumBytes=static_cast<int>(remaining);
        if(!aip::google_calendar::detail::Request(options,hooks,bounded,url,access.value,json,error))return false;
        if(json.size()>remaining){error=L"Gmail inbox data exceeded the aggregate response limit.";return false;}remaining-=json.size();return true;
    };
    std::string json;detail::google::Object profile;
    if(!request(L"https://gmail.googleapis.com/gmail/v1/users/me/profile?fields=emailAddress",json)||
        !detail::google::ReadObject(json,profile)||!detail::google::String(profile,"emailAddress",output.mailboxAddress,512)||
        output.mailboxAddress.find(L'@')==std::wstring::npos||output.mailboxAddress.find_first_of(L" \t\r\n<>")!=std::wstring::npos){
        if(error.empty())error=L"Google returned an invalid Gmail account identity.";return fail();}
    detail::google::Object label;std::wstring id;
    if(!request(L"https://gmail.googleapis.com/gmail/v1/users/me/labels/INBOX?fields=id,messagesUnread,threadsUnread",json)||
        !detail::google::ReadObject(json,label)||!detail::google::String(label,"id",id,64)||id!=L"INBOX"||
        !detail::Count(label,"messagesUnread",output.unreadMessages)||!detail::Count(label,"threadsUnread",output.unreadThreads)){
        if(error.empty())error=L"Google returned an invalid Gmail unread count.";return fail();}
    auto list=L"https://gmail.googleapis.com/gmail/v1/users/me/messages?labelIds=INBOX&maxResults="+std::to_wstring(cfg.maxMessages)+L"&fields=messages(id),nextPageToken";
    if(cfg.unreadOnly)list+=L"&labelIds=UNREAD";
    detail::google::Object page;if(!request(list,json)||!detail::google::ReadObject(json,page)){if(error.empty())error=L"Google returned an invalid Gmail inbox list.";return fail();}
    std::vector<std::string> ids;const auto messages=page.find("messages");
    if(messages!=page.end()&&!detail::google::Array(messages->second,ids,static_cast<std::size_t>(cfg.maxMessages))){error=L"Gmail inbox list exceeded the requested message limit.";return fail();}
    std::set<std::wstring> unique;
    for(const auto& raw:ids){detail::google::Object item;std::wstring messageId;
        if(!detail::google::ReadObject(raw,item)||!detail::google::String(item,"id",messageId,256)||!detail::Id(messageId)||!unique.insert(messageId).second){error=L"Google returned an invalid Gmail message identity.";return fail();}
        const auto fields=cfg.preview==L"Snippet"?L"id,internalDate,snippet,payload(headers)":L"id,internalDate,payload(headers)";
        auto url=L"https://gmail.googleapis.com/gmail/v1/users/me/messages/"+messageId+L"?format="+(cfg.preview==L"Snippet"?L"full":L"metadata")+
            L"&fields="+aip::google_calendar::detail::Query(fields);
        if(cfg.preview==L"Headers")url+=L"&metadataHeaders=From&metadataHeaders=Subject";
        Message message;if(!request(url,json)||!detail::MessageRow(json,messageId,cfg,message)){if(error.empty())error=L"Google returned invalid Gmail message metadata.";return fail();}
        output.messages.push_back(std::move(message));
    }
    if(bounded())return fail();std::stable_sort(output.messages.begin(),output.messages.end(),[](const Message& a,const Message& b){return a.receivedUtc>b.receivedUtc;});
    output.ok=true;return true;
}
inline bool FetchMailboxAddress(const Configuration& cfg,std::wstring& address,std::wstring& error,const Cancel& cancel={},const Hooks& hooks={},Session* session=nullptr){
    address.clear();const auto account=AccountConfiguration(cfg);const auto bounded=aip::google_calendar::detail::Deadline(cancel,45000,hooks.inert?hooks.monotonicNow:std::function<ULONGLONG()>{});
    aip::google_calendar::detail::Sensitive access;std::string json;detail::google::Object profile;
    if(!aip::google_calendar::detail::Access(account,hooks,bounded,access,error,session)||
        !aip::google_calendar::detail::Request(account,hooks,bounded,L"https://gmail.googleapis.com/gmail/v1/users/me/profile?fields=emailAddress",access.value,json,error)||
        !detail::google::ReadObject(json,profile)||!detail::google::String(profile,"emailAddress",address,512)||address.find(L'@')==std::wstring::npos||
        address.find_first_of(L" \t\r\n<>")!=std::wstring::npos||bounded()){
        address.clear();if(session)session->Clear();error=error.empty()?L"Gmail account identity could not be read.":detail::MailError(error);return false;}
    return true;
}
inline std::wstring InboxUrl(const std::wstring& address){
    return address.empty()||address.size()>512||address.find(L'@')==std::wstring::npos||address.find_first_of(L" \t\r\n<>")!=std::wstring::npos?
        L"https://mail.google.com/":L"https://mail.google.com/mail/?authuser="+aip::google_calendar::detail::Query(address)+L"#inbox";
}
} }

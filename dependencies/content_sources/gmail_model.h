#pragma once
// Per-tile Gmail read configuration. No account or network is accessed here.
#include "google_calendar_model.h"
namespace aip { namespace gmail {
struct Configuration {
    aip::google_calendar::Configuration account;
    std::wstring preview=L"Headers";
    std::wstring primaryField=L"Sender",secondaryField=L"SubjectAndSnippet";
    bool unreadOnly=true,badgeEnabled=true;int maxMessages=3;
    Configuration(){account.refreshSeconds=120;}
};
struct Message {std::wstring id,sender,subject,snippet;aip::datetime::UTC receivedUtc=0;};
struct Snapshot {bool ok=false;int unreadMessages=0,unreadThreads=0;std::vector<Message> messages;std::wstring error,mailboxAddress;};
inline constexpr aip::datetime::Setting Settings[]={
    {L"ClientId",L""},{L"ClientSecret",L""},{L"RefreshTokenProtected",L""},{L"Preview",L"Headers"},
    {L"UnreadOnly",L"1"},{L"MaxMessages",L"3"},{L"RefreshSeconds",L"120"},{L"TimeoutSeconds",L"15"},{L"MaximumBytes",L"2097152"}
    ,{L"PrimaryField",L"Sender"},{L"SecondaryField",L"SubjectAndSnippet"},{L"BadgeEnabled",L"1"}
};
inline bool NormalizeSetting(std::wstring key,std::wstring& value,std::wstring& error){
    const auto lower=aip::datetime::Lower(key);const aip::datetime::Setting* known=nullptr;
    for(const auto& field:Settings)if(lower==aip::datetime::Lower(field.key))known=&field;
    if(!known){error=L"Unknown Mail setting.";return false;}key=known->key;
    if(lower==L"preview"){
        const auto choice=aip::datetime::Lower(aip::datetime::Trim(value));
        if(choice==L"headers")value=L"Headers";else if(choice==L"snippet")value=L"Snippet";
        else{error=L"Mail preview must be Headers or Snippet.";return false;}
    }else if(lower==L"primaryfield"||lower==L"secondaryfield"){
        const auto choice=aip::datetime::Lower(aip::datetime::Trim(value));
        if(lower==L"primaryfield"&&choice==L"sender")value=L"Sender";
        else if(choice==L"subject")value=L"Subject";
        else if(lower==L"secondaryfield"&&choice==L"subjectandsnippet")value=L"SubjectAndSnippet";
        else if(lower==L"secondaryfield"&&choice==L"snippet")value=L"Snippet";
        else{error=L"Choose Sender/Subject for primary Mail text, or Subject/SubjectAndSnippet/Snippet for secondary text.";return false;}
    }else if(lower==L"unreadonly"||lower==L"badgeenabled"){
        const auto choice=aip::datetime::Lower(aip::datetime::Trim(value));
        if(choice==L"1"||choice==L"true"||choice==L"yes"||choice==L"on")value=L"1";
        else if(choice==L"0"||choice==L"false"||choice==L"no"||choice==L"off")value=L"0";
        else{error=L"Mail."+key+L" must be a boolean.";return false;}
    }else if(lower==L"maxmessages"){
        int count=0;if(!aip::datetime::Number(aip::datetime::Trim(value),count)||count<1||count>5){error=L"Mail keeps between 1 and 5 message previews per tile.";return false;}
        value=std::to_wstring(count);
    }else if(!aip::google_calendar::NormalizeSetting(key,value,error))return false;
    error.clear();return true;
}
inline aip::google_calendar::Configuration AccountConfiguration(const Configuration& cfg){
    auto account=cfg.account;account.calendarId=L"primary";account.allCalendars=false;
    account.oauthPurpose=cfg.preview==L"Snippet"?aip::google_calendar::OAuthPurpose::GmailReadOnly:aip::google_calendar::OAuthPurpose::GmailMetadata;
    return account;
}
template<class Reader> bool ReadConfiguration(Reader read,Configuration& output,std::wstring& error){
    Configuration cfg;
    for(const auto& field:Settings){std::wstring value=read(field.key,field.fallback);if(!NormalizeSetting(field.key,value,error))return false;
        const auto key=std::wstring(field.key);
        if(key==L"ClientId")cfg.account.clientId=value;else if(key==L"ClientSecret")cfg.account.clientSecret=value;
        else if(key==L"RefreshTokenProtected")cfg.account.refreshTokenProtected=value;else if(key==L"Preview")cfg.preview=value;
        else if(key==L"PrimaryField")cfg.primaryField=value;else if(key==L"SecondaryField")cfg.secondaryField=value;
        else if(key==L"UnreadOnly")cfg.unreadOnly=value==L"1";
        else if(key==L"BadgeEnabled")cfg.badgeEnabled=value==L"1";
        else{int number=0;aip::datetime::Number(value,number);if(key==L"MaxMessages")cfg.maxMessages=number;
            else if(key==L"RefreshSeconds")cfg.account.refreshSeconds=number;else if(key==L"TimeoutSeconds")cfg.account.timeoutSeconds=number;else cfg.account.maximumBytes=number;}}
    cfg.account=AccountConfiguration(cfg);output=std::move(cfg);return true;
}
inline std::wstring ConfigurationKey(const Configuration& cfg){return aip::google_calendar::ConfigurationKey(AccountConfiguration(cfg))+L"|"+cfg.preview+L"|"+std::to_wstring(cfg.unreadOnly)+L"|"+std::to_wstring(cfg.maxMessages)+L"|"+cfg.primaryField+L"|"+cfg.secondaryField+L"|"+std::to_wstring(cfg.badgeEnabled);}
inline std::wstring ClipPresentation(const std::wstring& text,std::size_t maximum){
    if(text.size()<=maximum)return text;if(!maximum)return {};
    auto clipped=text.substr(0,maximum-1);
    if(!clipped.empty()&&clipped.back()>=0xd800&&clipped.back()<=0xdbff)clipped.pop_back();
    return clipped+L"\u2026";
}
inline std::pair<std::wstring,std::wstring> DisplayMessage(const Message& message,const Configuration& cfg,
    const std::wstring& unknownSender=L"Unknown sender",const std::wstring& noSubject=L"(No subject)"){
    const auto sender=ClipPresentation(message.sender.empty()?unknownSender:message.sender,256),subject=ClipPresentation(message.subject.empty()?noSubject:message.subject,512);
    const auto snippet=cfg.preview==L"Snippet"?ClipPresentation(message.snippet,512):std::wstring();
    auto secondary=cfg.secondaryField==L"Snippet"?snippet:subject;
    if(cfg.secondaryField==L"SubjectAndSnippet"&&!snippet.empty())secondary+=L"\n"+snippet;
    return {cfg.primaryField==L"Subject"?subject:sender,std::move(secondary)};
}
} }

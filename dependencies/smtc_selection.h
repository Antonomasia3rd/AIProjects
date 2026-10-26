#pragma once
#include <string>
#include <vector>
#include <cwctype>
namespace aip { namespace content {
struct SmtcSelection {std::wstring mode=L"Current",appId;int instance=0;};
inline bool NormalizeSmtcSelectionSetting(const std::wstring& key,std::wstring& value,std::wstring& error){
    const auto first=value.find_first_not_of(L" \t\r\n");value=first==std::wstring::npos?L"":value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);
    if(key==L"sessioninstance"){
        if(value.empty()||value.size()>2){error=L"SMTC.SessionInstance must be 0 through 64.";return false;}
        int number=0;for(const auto c:value){if(c<L'0'||c>L'9'){error=L"SMTC.SessionInstance must be 0 through 64.";return false;}number=number*10+c-L'0';}
        if(number>64){error=L"SMTC.SessionInstance must be 0 through 64.";return false;}value=std::to_wstring(number);
    }else if(key==L"sessionmode"){
        auto mode=value;for(auto& c:mode)c=static_cast<wchar_t>(std::towlower(c));
        if(mode==L"current")value=L"Current";else if(mode==L"application")value=L"Application";
        else{error=L"SMTC.SessionMode must be Current or Application.";return false;}
    }else{
        if(value.size()>512){error=L"SMTC.SessionAppId is limited to 512 characters.";return false;}
        for(const auto c:value)if(c<0x20||c==0x7f){error=L"SMTC.SessionAppId must be a single application identifier.";return false;}
    }
    error.clear();return true;
}
inline std::wstring SmtcSelectionKey(const SmtcSelection& value){return value.mode==L"Current"?L"Current":L"Application:"+std::to_wstring(value.appId.size())+L":"+value.appId+L":"+std::to_wstring(value.instance);}
inline std::size_t SelectSmtcApplicationIndex(const SmtcSelection& selection,const std::vector<std::wstring>& ids,std::size_t current){
    const auto absent=ids.size();if(selection.mode==L"Current")return current<ids.size()?current:absent;
    if(selection.appId.empty())return absent;
    if(!selection.instance&&current<ids.size()&&ids[current]==selection.appId)return current;
    int count=0;for(std::size_t i=0;i<ids.size();++i)if(ids[i]==selection.appId&&++count>=(selection.instance?selection.instance:1))return i;
    return absent;
}
} }

#pragma once
// Pure, session-bound confirmation policy. Tokens come from the host's random
// generator; no filesystem, OS API, notification, callback or mutation runs here.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace aip { namespace notifications {
enum class ConfirmationChoice { Yes, No };
struct ConfirmationAction { std::wstring token; ConfirmationChoice choice=ConfirmationChoice::No; };
struct PendingConfirmation {
    std::wstring key,token,profile;
    std::uint64_t created=0,deadline=0;
};
struct ConfirmationResolution { std::wstring key,token;bool approved=false; };
inline bool ValidConfirmationToken(const std::wstring& token){
    return token.size()==32&&std::all_of(token.begin(),token.end(),[](wchar_t ch){return(ch>=L'0'&&ch<=L'9')||(ch>=L'a'&&ch<=L'f');});
}
inline bool ConfirmationIdentityText(const std::wstring& value,std::size_t maximum){
    return !value.empty()&&value.size()<=maximum&&std::none_of(value.begin(),value.end(),[](wchar_t ch){return ch<0x20||ch==0x7f;});
}
inline bool ParseConfirmationAction(const std::wstring& arguments,ConfirmationAction& output){
    constexpr std::size_t prefix=8,tokenSize=32;
    if(arguments.size()!=prefix+tokenSize+11&&arguments.size()!=prefix+tokenSize+10)return false;
    if(arguments.compare(0,prefix,L"confirm=")!=0)return false;
    ConfirmationAction parsed;parsed.token=arguments.substr(prefix,tokenSize);
    if(!ValidConfirmationToken(parsed.token))return false;
    const auto suffix=arguments.substr(prefix+tokenSize);
    if(suffix==L"&choice=yes")parsed.choice=ConfirmationChoice::Yes;
    else if(suffix==L"&choice=no")parsed.choice=ConfirmationChoice::No;
    else return false;
    output=std::move(parsed);return true;
}
class ConfirmationRegistry {
    std::vector<PendingConfirmation> pending_;
    std::vector<std::wstring> retired_;
    void Retire(const std::wstring& token){
        if(std::find(retired_.begin(),retired_.end(),token)!=retired_.end())return;
        retired_.push_back(token);if(retired_.size()>128)retired_.erase(retired_.begin());
    }
public:
    static constexpr std::size_t Capacity=32;
    static constexpr std::uint64_t DefaultTtlMs=300000;
    const std::vector<PendingConfirmation>& Entries()const{return pending_;}
    void Expire(std::uint64_t now){
        for(auto it=pending_.begin();it!=pending_.end();){
            if(now<it->created||now>=it->deadline){Retire(it->token);it=pending_.erase(it);}else ++it;
        }
    }
    bool Add(const std::wstring& key,const std::wstring& token,const std::wstring& profile,std::uint64_t now,std::uint64_t ttlMs=DefaultTtlMs){
        if(!ConfirmationIdentityText(key,256)||!ConfirmationIdentityText(profile,32768)||!ValidConfirmationToken(token)||!ttlMs||
            ttlMs>86400000||ttlMs>(std::numeric_limits<std::uint64_t>::max)()-now)return false;
        // Entries() is intentionally readable by hosts. Copy before Expire or
        // replacement so an input referencing an existing entry cannot dangle.
        const PendingConfirmation proposed{key,token,profile,now,now+ttlMs};
        Expire(now);
        if(std::find(retired_.begin(),retired_.end(),proposed.token)!=retired_.end()||
            std::any_of(pending_.begin(),pending_.end(),[&](const auto& item){return item.token==proposed.token;}))return false;
        const auto existing=std::find_if(pending_.begin(),pending_.end(),[&](const auto& item){return item.key==proposed.key&&item.profile==proposed.profile;});
        if(existing==pending_.end()&&pending_.size()>=Capacity)return false;
        if(existing!=pending_.end()){Retire(existing->token);pending_.erase(existing);}
        pending_.push_back(proposed);return true;
    }
    std::optional<ConfirmationResolution> Resolve(const std::wstring& token,ConfirmationChoice choice,const std::wstring& profile,std::uint64_t now){
        if(!ValidConfirmationToken(token)||(choice!=ConfirmationChoice::Yes&&choice!=ConfirmationChoice::No))return std::nullopt;
        Expire(now);
        const auto found=std::find_if(pending_.begin(),pending_.end(),[&](const auto& item){return item.token==token&&item.profile==profile;});
        if(found==pending_.end())return std::nullopt;
        ConfirmationResolution resolution{found->key,found->token,choice==ConfirmationChoice::Yes};
        Retire(found->token);pending_.erase(found);return resolution;
    }
    bool CancelToken(const std::wstring& token){
        const auto found=std::find_if(pending_.begin(),pending_.end(),[&](const auto& item){return item.token==token;});
        if(found==pending_.end())return false;Retire(found->token);pending_.erase(found);return true;
    }
    bool CancelKey(const std::wstring& key,const std::wstring& profile){
        const auto found=std::find_if(pending_.begin(),pending_.end(),[&](const auto& item){return item.key==key&&item.profile==profile;});
        return found!=pending_.end()&&CancelToken(found->token);
    }
    void Clear(){for(const auto& item:pending_)Retire(item.token);pending_.clear();}
};
inline std::wstring ConfirmationXmlText(const std::wstring& text,std::size_t maximum){
    std::wstring clean;
    for(std::size_t i=0;i<text.size();++i){
        const auto ch=static_cast<std::uint32_t>(text[i]);
        if(ch>=0xd800&&ch<=0xdbff){
            if(i+1>=text.size())continue;const auto low=static_cast<std::uint32_t>(text[i+1]);
            if(low<0xdc00||low>0xdfff)continue;
            if(clean.size()+2>maximum)break;clean+=text[i];clean+=text[++i];continue;
        }
        if((ch>=0xdc00&&ch<=0xdfff)||ch==0xfffe||ch==0xffff||ch>0x10ffff||(ch<0x20&&ch!=9&&ch!=10&&ch!=13))continue;
        if(clean.size()>=maximum)break;clean+=text[i];
    }
    std::wstring escaped;
    for(const auto ch:clean){
        if(ch==L'&')escaped+=L"&amp;";else if(ch==L'<')escaped+=L"&lt;";else if(ch==L'>')escaped+=L"&gt;";
        else if(ch==L'\"')escaped+=L"&quot;";else if(ch==L'\'')escaped+=L"&apos;";else escaped+=ch;
    }
    return escaped;
}
inline std::wstring XMLConfirmationXml(const std::wstring& title,const std::wstring& message,const std::wstring& token,
    const std::wstring& yesLabel=L"Yes",const std::wstring& noLabel=L"No"){
    if(!ValidConfirmationToken(token))return {};
    const auto yes=ConfirmationXmlText(yesLabel,64),no=ConfirmationXmlText(noLabel,64);
    return L"<toast launch=\"review\"><visual><binding template=\"ToastGeneric\"><text>"+ConfirmationXmlText(title,128)+
        L"</text><text>"+ConfirmationXmlText(message,1024)+L"</text></binding></visual><actions>"
        L"<action content=\""+(yes.empty()?L"Yes":yes)+L"\" activationType=\"foreground\" arguments=\"confirm="+token+L"&amp;choice=yes\"/>"
        L"<action content=\""+(no.empty()?L"No":no)+L"\" activationType=\"foreground\" arguments=\"confirm="+token+L"&amp;choice=no\"/>"
        L"</actions><audio silent=\"true\"/></toast>";
}
} }

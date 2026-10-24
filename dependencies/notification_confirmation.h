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
struct ConfirmationLaunch {std::wstring action,profile;bool review=false;};
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
// Foreground toast actions may launch another EXE. Reserve their arguments
// before positional wallpaper parsing; stale actions must never become paths.
inline bool ParseConfirmationLaunch(const std::vector<std::wstring>& args,ConfirmationLaunch& output,bool& recognized){
    const bool legacyReview=(args.size()==2&&args[1]==L"review")||(args.size()==3&&args[1]==L"-ToastActivated"&&args[2]==L"review");
    recognized=legacyReview;for(std::size_t i=1;i<args.size();++i){if(args[i]==L"--ini"&&i+1<args.size()){++i;continue;}
        if(args[i].compare(0,8,L"confirm=")==0||args[i]==L"--notification-review")recognized=true;}
    if(!recognized)return false;ConfirmationLaunch parsed;bool profileSeen=false,actionSeen=false;
    for(std::size_t i=1;i<args.size();++i){const auto& arg=args[i];
        if(arg==L"--ini"){
            if(profileSeen||i+1>=args.size()||!ConfirmationIdentityText(args[i+1],32760))return false;
            profileSeen=true;parsed.profile=args[++i];
        }else if(arg.compare(0,8,L"confirm=")==0||arg==L"--notification-review"||(legacyReview&&arg==L"review")){
            if(actionSeen)return false;actionSeen=true;parsed.action=arg;parsed.review=arg==L"review"||arg==L"--notification-review";
            ConfirmationAction action;if(!parsed.review&&!ParseConfirmationAction(arg,action))return false;
        }else if(arg!=L"-ToastActivated"&&arg!=L"--launched-from-appx-stub")return false;
    }
    if(!actionSeen)return false;output=std::move(parsed);return true;
}
inline std::wstring ConfirmationProfilePrefix(const std::wstring& profile){
    if(profile.empty())return {};if(!ConfirmationIdentityText(profile,32760))return {};
    std::wstring quoted=L"--ini \"";std::size_t slashes=0;
    for(const auto ch:profile){if(ch==L'\\'){++slashes;continue;}
        quoted.append(ch==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;quoted+=ch;
    }
    quoted.append(slashes*2,L'\\');return quoted+L"\" ";
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
    const std::wstring& yesLabel=L"Yes",const std::wstring& noLabel=L"No",const std::wstring& profile={}){
    if(!ValidConfirmationToken(token))return {};
    const auto prefix=ConfirmationProfilePrefix(profile);if(!profile.empty()&&prefix.empty())return {};
    const auto yes=ConfirmationXmlText(yesLabel,64),no=ConfirmationXmlText(noLabel,64);
    const auto argument=[&](const std::wstring& value){return ConfirmationXmlText(prefix+value,33000);};
    return L"<toast launch=\""+argument(L"--notification-review")+L"\"><visual><binding template=\"ToastGeneric\"><text>"+ConfirmationXmlText(title,128)+
        L"</text><text>"+ConfirmationXmlText(message,1024)+L"</text></binding></visual><actions>"
        L"<action content=\""+(yes.empty()?L"Yes":yes)+L"\" activationType=\"foreground\" arguments=\""+argument(L"confirm="+token+L"&choice=yes")+L"\"/>"
        L"<action content=\""+(no.empty()?L"No":no)+L"\" activationType=\"foreground\" arguments=\""+argument(L"confirm="+token+L"&choice=no")+L"\"/>"
        L"</actions><audio silent=\"true\"/></toast>";
}
} }

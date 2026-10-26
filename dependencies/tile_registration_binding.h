#pragma once
// Public, bounded activation metadata for an owned independent tile package.
// No account data, command evaluation, filesystem or operating-system access.
#include "tile_registration.h"
#include <vector>

namespace aip { namespace tile_registration {
inline constexpr const wchar_t* ActivationBindingFileName=L"registration.activation";
inline constexpr std::size_t MaximumActivationCharacters=65536;
inline constexpr std::size_t MaximumActivationBytes=262144;
struct ActivationBinding {
    std::wstring userScope,profileScope,tileScope,identityName,publisher;
    std::wstring profile,hostPath,tileId,hostProduct,hostAumid;
};
inline bool LocalAbsolutePath(const std::wstring& path) {
    return path.size()>3&&((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&path[1]==L':'&&
        (path[2]==L'\\'||path[2]==L'/')&&Text(path,32767)&&path.find(L'\"')==std::wstring::npos;
}
inline bool BindingTileId(const std::wstring& id) {
    return !id.empty()&&id.size()<=54&&std::all_of(id.begin(),id.end(),[](wchar_t c){
        return (c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'_';
    });
}
inline bool ValidActivationBinding(const ActivationBinding& value) {
    return ScopeKey(value.userScope)&&ScopeKey(value.profileScope)&&ScopeKey(value.tileScope)&&
        value.identityName==IdentityName(value.userScope,value.profileScope,value.tileScope)&&Text(value.publisher,8192)&&
        LocalAbsolutePath(value.profile)&&LocalAbsolutePath(value.hostPath)&&BindingTileId(value.tileId)&&
        Text(value.hostProduct,128)&&(value.hostAumid.empty()||Aumid(value.hostAumid));
}
inline ActivationBinding ActivationBindingForTarget(const Target& target) {
    return {target.userScope,target.profileScope,target.tileScope,target.identityName,target.publisher,
        target.profile,target.hostPath,target.tileId,target.hostProduct,target.hostAumid};
}
inline bool WriteActivationBinding(const ActivationBinding& value,std::wstring& output,std::wstring& error) {
    if(!ValidActivationBinding(value)){error=L"The Registration tile activation binding is invalid.";return false;}
    auto candidate=std::wstring(L"AIP_TILE_ACTIVATION_V1\r\n");
    for(const auto* field:{&value.userScope,&value.profileScope,&value.tileScope,&value.identityName,&value.publisher,
        &value.profile,&value.hostPath,&value.tileId,&value.hostProduct,&value.hostAumid})candidate+=*field+L"\r\n";
    if(candidate.size()>MaximumActivationCharacters){error=L"The Registration tile activation binding exceeds its limit.";return false;}
    output=std::move(candidate);error.clear();return true;
}
inline bool ReadActivationBinding(const std::wstring& text,ActivationBinding& output,std::wstring& error) {
    const auto fail=[&]{error=L"The Registration tile activation binding is malformed or incomplete.";return false;};
    if(text.empty()||text.size()>MaximumActivationCharacters||text.find(L'\0')!=std::wstring::npos)return fail();
    std::vector<std::wstring> lines;std::size_t start=0;
    while(start<text.size()){
        const auto end=text.find(L'\n',start);if(end==std::wstring::npos)return fail();
        auto line=text.substr(start,end-start);if(!line.empty()&&line.back()==L'\r')line.pop_back();
        if(line.find(L'\r')!=std::wstring::npos||lines.size()>=11)return fail();
        lines.push_back(std::move(line));start=end+1;
    }
    if(lines.size()!=11||lines[0]!=L"AIP_TILE_ACTIVATION_V1")return fail();
    ActivationBinding candidate{lines[1],lines[2],lines[3],lines[4],lines[5],lines[6],lines[7],lines[8],lines[9],lines[10]};
    if(!ValidActivationBinding(candidate))return fail();
    output=std::move(candidate);error.clear();return true;
}
inline std::wstring ActivationOwnerText(const ActivationBinding& binding,const std::wstring& normalizedProfile) {
    return L"AIP_TILE_REGISTRATION_V1\r\n"+binding.userScope+L"\r\n"+binding.profileScope+L"\r\n"+
        binding.tileScope+L"\r\n"+normalizedProfile+L"\r\n"+binding.tileId+L"\r\n";
}
} }

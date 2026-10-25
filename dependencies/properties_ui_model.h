#pragma once
// Portable Properties preferences and interaction/navigation state.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <set>
#include <string>
#include <vector>
namespace aip { namespace properties {
struct Field {const wchar_t* key;const wchar_t* fallback;};
inline constexpr Field Fields[]={{L"HighlightMode",L"Accent"},{L"HighlightColor",L"#0078D4"},{L"CaptionMode",L"Properties"}};
inline std::wstring Lower(std::wstring text){for(auto& c:text)c=static_cast<wchar_t>(std::towlower(c));return text;}
inline bool NormalizeSetting(const std::wstring& key,std::wstring& value,std::wstring& error){
    const auto k=Lower(key);const auto start=value.find_first_not_of(L" \r\n\t"),last=value.find_last_not_of(L" \r\n\t");
    value=start==std::wstring::npos?L"":value.substr(start,last-start+1);const auto v=Lower(value);
    if(k==L"highlightmode"){if(v==L"accent")value=L"Accent";else if(v==L"custom")value=L"Custom";else {error=L"Properties.HighlightMode expects Accent or Custom.";return false;}}
    else if(k==L"captionmode"){if(v==L"properties")value=L"Properties";else if(v==L"system")value=L"System";else {error=L"Properties.CaptionMode expects Properties or System.";return false;}}
    else if(k==L"highlightcolor"){if(value.size()!=7||value.front()!=L'#'||!std::all_of(value.begin()+1,value.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');})){
            error=L"Properties.HighlightColor expects #RRGGBB.";return false;}for(auto& c:value)c=static_cast<wchar_t>(std::towupper(c));}
    else {error=L"Unknown Properties setting.";return false;}error.clear();return true;
}
struct Configuration {std::wstring highlightMode=L"Accent",highlightColor=L"#0078D4",captionMode=L"Properties";};
template<class Reader> bool ReadConfiguration(Reader read,Configuration& output,std::wstring& error){Configuration cfg;
    for(const auto& field:Fields){auto value=read(field.key,field.fallback);if(!NormalizeSetting(field.key,value,error))return false;
        if(Lower(field.key)==L"highlightmode")cfg.highlightMode=std::move(value);else if(Lower(field.key)==L"highlightcolor")cfg.highlightColor=std::move(value);else cfg.captionMode=std::move(value);}
    output=std::move(cfg);return true;
}
inline std::uint32_t ColorRgb(const Configuration& cfg){std::uint32_t result=0;for(size_t i=1;i<cfg.highlightColor.size();++i){const auto c=cfg.highlightColor[i];result=result*16+(c<=L'9'?c-L'0':c-L'A'+10);}return result;}
inline unsigned InteractionAlpha(bool hover,bool pressed){return pressed?96u:hover?48u:0u;}
struct PointerState {
    int hover=-1,pressed=-1;
    void Move(int hit){hover=hit;}
    void Down(int hit){hover=pressed=hit;}
    int Up(int hit){const auto action=pressed>=0&&pressed==hit?hit:-1;pressed=-1;hover=hit;return action;}
    void Leave(){hover=-1;}
    void Cancel(){hover=pressed=-1;}
};
struct Branch {std::wstring key,label;int section=0;std::vector<size_t> path;std::vector<Branch> children;};
struct NavigationRow {std::wstring key,label;int section=0;std::vector<size_t> path;unsigned depth=0;bool expandable=false,expanded=false;};
inline std::vector<NavigationRow> Navigation(const std::vector<Branch>& branches,const std::set<std::wstring>& expanded){
    std::vector<NavigationRow> result;const auto append=[&](const auto& self,const std::vector<Branch>& items,unsigned depth)->void{
        if(depth>8)return;for(const auto& item:items){if(result.size()>=2048)return;const bool open=expanded.count(item.key)!=0;
            result.push_back({item.key,item.label,item.section,item.path,depth,!item.children.empty(),open});if(open)self(self,item.children,depth+1);}};
    append(append,branches,0);return result;
}
}}

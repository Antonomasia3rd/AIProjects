#pragma once
// Portable Properties preferences and interaction/navigation state.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <cmath>
#include <set>
#include <string>
#include <vector>
namespace aip { namespace properties {
struct Field {const wchar_t* key;const wchar_t* fallback;};
inline constexpr Field Fields[]={{L"HighlightMode",L"Accent"},{L"HighlightColor",L"#0078D4"},{L"HighlightSource",L"Auto"},{L"HighlightContrast",L"Readable"},{L"CaptionMode",L"Properties"}};
inline constexpr const wchar_t* HighlightSources[]={L"Auto",L"Accent",L"AccentLight1",L"AccentLight2",L"AccentLight3",L"AccentDark1",L"AccentDark2",L"AccentDark3",L"Dwm",L"SystemHighlight",L"SystemHotlight"};
inline std::wstring Lower(std::wstring text){for(auto& c:text)c=static_cast<wchar_t>(std::towlower(c));return text;}
inline bool NormalizeSetting(const std::wstring& key,std::wstring& value,std::wstring& error){
    const auto k=Lower(key);const auto start=value.find_first_not_of(L" \r\n\t"),last=value.find_last_not_of(L" \r\n\t");
    value=start==std::wstring::npos?L"":value.substr(start,last-start+1);const auto v=Lower(value);
    if(k==L"highlightmode"){if(v==L"accent")value=L"Accent";else if(v==L"custom")value=L"Custom";else {error=L"Properties.HighlightMode expects Accent or Custom.";return false;}}
    else if(k==L"highlightsource"){bool found=false;for(const auto* source:HighlightSources)if(v==Lower(source)){value=source;found=true;break;}if(!found){error=L"Properties.HighlightSource expects Auto, Accent, AccentLight1/2/3, AccentDark1/2/3, Dwm, SystemHighlight or SystemHotlight.";return false;}}
    else if(k==L"highlightcontrast"){if(v==L"readable")value=L"Readable";else if(v==L"exact")value=L"Exact";else{error=L"Properties.HighlightContrast expects Readable or Exact.";return false;}}
    else if(k==L"captionmode"){if(v==L"properties")value=L"Properties";else if(v==L"system")value=L"System";else {error=L"Properties.CaptionMode expects Properties or System.";return false;}}
    else if(k==L"highlightcolor"){if(value.size()!=7||value.front()!=L'#'||!std::all_of(value.begin()+1,value.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');})){
            error=L"Properties.HighlightColor expects #RRGGBB.";return false;}for(auto& c:value)c=static_cast<wchar_t>(std::towupper(c));}
    else {error=L"Unknown Properties setting.";return false;}error.clear();return true;
}
struct Configuration {std::wstring highlightMode=L"Accent",highlightColor=L"#0078D4",highlightSource=L"Auto",highlightContrast=L"Readable",captionMode=L"Properties";};
template<class Reader> bool ReadConfiguration(Reader read,Configuration& output,std::wstring& error){Configuration cfg;
    for(const auto& field:Fields){auto value=read(field.key,field.fallback);if(!NormalizeSetting(field.key,value,error))return false;
        if(Lower(field.key)==L"highlightmode")cfg.highlightMode=std::move(value);else if(Lower(field.key)==L"highlightcolor")cfg.highlightColor=std::move(value);
        else if(Lower(field.key)==L"highlightsource")cfg.highlightSource=std::move(value);else if(Lower(field.key)==L"highlightcontrast")cfg.highlightContrast=std::move(value);else cfg.captionMode=std::move(value);}
    output=std::move(cfg);return true;
}
inline std::uint32_t ColorRgb(const Configuration& cfg){std::uint32_t result=0;for(size_t i=1;i<cfg.highlightColor.size();++i){const auto c=cfg.highlightColor[i];result=result*16+(c<=L'9'?c-L'0':c-L'A'+10);}return result;}
inline double Luminance(std::uint32_t rgb){const auto channel=[](unsigned c){const auto v=c/255.0;return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4);};return .2126*channel((rgb>>16)&255)+.7152*channel((rgb>>8)&255)+.0722*channel(rgb&255);}
inline double Contrast(std::uint32_t a,std::uint32_t b){const auto x=Luminance(a),y=Luminance(b);return ((std::max)(x,y)+.05)/((std::min)(x,y)+.05);}
// Keep a chosen hue when possible, moving only as far toward black/white as
// necessary for normal-size text. Exact is an explicit customization opt-out.
inline std::uint32_t ReadableHighlight(std::uint32_t color,std::uint32_t background,const Configuration& cfg){
    if(cfg.highlightContrast==L"Exact"||Contrast(color,background)>=4.5)return color;
    const auto target=Contrast(0,background)>Contrast(0xFFFFFF,background)?0u:0xFFFFFFu;
    for(unsigned step=1;step<=255;++step){std::uint32_t adjusted=0;for(unsigned shift:{16u,8u,0u}){const auto a=(color>>shift)&255,b=(target>>shift)&255;
            adjusted|=((a*(255-step)+b*step+127)/255)<<shift;}if(Contrast(adjusted,background)>=4.5)return adjusted;}
    return target;
}
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

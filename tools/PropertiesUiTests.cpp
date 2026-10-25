#include "../dependencies/properties_ui_model.h"
#include <iostream>
static unsigned checks=0,failures=0;
static void Check(bool condition,const char* label){++checks;if(!condition){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
int main(){using namespace aip::properties;std::wstring error;
    for(const auto& field:Fields){std::wstring value=field.fallback;Check(NormalizeSetting(field.key,value,error)&&error.empty(),"every declared default validates");}
    for(const auto& pair:std::vector<std::pair<std::wstring,std::wstring>>{{L" highlightmode",L"Accent"},{L"HighlightMode",L"blue"},{L"CaptionMode",L"hidden"},{L"HighlightColor",L"#123"},{L"HighlightColor",L"#1234567"},{L"HighlightColor",L"#GG0000"},{L"HighlightColor",L"0078D4"},{L"Unknown",L"1"}}){auto value=pair.second;Check(!NormalizeSetting(pair.first,value,error)&&!error.empty(),"invalid preference is rejected");}
    std::wstring color=L" #aBcD09 \t";Check(NormalizeSetting(L"highlightcolor",color,error)&&color==L"#ABCD09","custom colors canonicalize without losing channels");
    std::wstring mode=L" cUsToM ";Check(NormalizeSetting(L"HighlightMode",mode,error)&&mode==L"Custom","enum matching is case insensitive");
    Configuration cfg;Check(ReadConfiguration([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},cfg,error)&&cfg.captionMode==L"Properties"&&cfg.highlightMode==L"Accent","fresh preferences select accent and Properties caption");
    cfg.highlightMode=L"Custom";cfg.highlightColor=color;Check(ColorRgb(cfg)==0xABCD09,"custom RGB stays independent from Win32 BGR storage");
    const auto before=cfg;Check(!ReadConfiguration([](const wchar_t* key,const wchar_t* fallback){return Lower(key)==L"captionmode"?L"invalid":std::wstring(fallback);},cfg,error)&&cfg.highlightMode==before.highlightMode&&cfg.highlightColor==before.highlightColor,"invalid read does not partially replace the active preferences");
    PointerState pointer;pointer.Move(4);Check(pointer.hover==4&&pointer.pressed==-1,"hover does not imply activation");Check(pointer.Up(4)==-1,"release without press cannot activate");
    pointer.Down(4);Check(pointer.pressed==4&&pointer.hover==4,"press captures the hit identity");pointer.Move(5);Check(pointer.Up(5)==-1&&pointer.pressed==-1,"dragging to another target cannot activate it");
    pointer.Down(4);pointer.Leave();Check(pointer.hover==-1&&pointer.pressed==4,"leaving clears hover while preserving capture");Check(pointer.Up(-1)==-1,"release outside does not activate");
    pointer.Down(4);pointer.Move(-1);pointer.Move(4);Check(pointer.Up(4)==4,"returning to original pressed target permits a click");
    pointer.Down(4);pointer.Cancel();Check(pointer.Up(4)==-1&&pointer.pressed==-1,"capture cancellation prevents stale activation");
    Check(InteractionAlpha(false,false)==0&&InteractionAlpha(true,false)>0&&InteractionAlpha(true,true)>InteractionAlpha(true,false),"hover and press have distinct visible strengths");
    Branch tiles{L"tiles",L"Tiles",0,{},{{L"source",L"Source",0,{0},{{L"advanced",L"Advanced",0,{0,1},{}}}},{L"appearance",L"Appearance",0,{2},{}}}};
    Branch app{L"app",L"Application",2,{},{{L"interface",L"Interface",2,{0},{}}}};
    std::vector<Branch> tree{tiles,app};std::set<std::wstring> expanded;
    auto rows=Navigation(tree,expanded);Check(rows.size()==2&&rows[0].expandable&&!rows[0].expanded,"initial navigation hides children behind explicit controls");
    expanded.insert(L"tiles");rows=Navigation(tree,expanded);Check(rows.size()==4&&rows[1].label==L"Source"&&rows[1].depth==1&&rows[2].path==std::vector<size_t>{2},"expanded children retain indentation and real page paths");
    expanded.insert(L"source");rows=Navigation(tree,expanded);Check(rows.size()==5&&rows[2].depth==2&&rows[2].path==std::vector<size_t>({0,1}),"nested groups retain independent expansion and paths");
    expanded.insert(L"app");rows=Navigation(tree,expanded);Check(rows.size()==6&&rows.back().label==L"Interface"&&rows.back().section==2,"expanding another category does not collapse the first");
    expanded.erase(L"tiles");rows=Navigation(tree,expanded);Check(rows.size()==3&&rows[0].label==L"Tiles"&&rows[1].label==L"Application","collapsing a group leaves sibling hierarchy stable");
    expanded.insert(L"tiles");rows=Navigation(tree,expanded);Check(rows.size()==6&&rows[2].label==L"Advanced","explicitly reopening restores child expansion state");
    std::vector<Branch> large(3000);Check(Navigation(large,{}).size()==2048,"navigation construction has a finite row budget");
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}

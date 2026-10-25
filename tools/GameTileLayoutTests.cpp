#include "../dependencies/game_tile_layout.h"
#include <iostream>
#include <stdexcept>
static int checks=0;
static void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
int main(){try{using namespace aip::content;std::wstring error;
    for(const auto* placement:{L"Right",L"Left",L"None"}){std::wstring value=placement;Check(NormalizeTextPresentationSetting(L"TextIconPlacement",value,error),"known icon positions validate");}
    for(const auto* value:{L"",L"Other",L"center"}){std::wstring copy=value;Check(!NormalizeTextPresentationSetting(L"TextIconPlacement",copy,error),"unsupported icon positions are rejected");}
    for(const auto* key:{L"TextIconScalePercent",L"TextRowSpacing"})for(const auto* value:{L"-1",L"NaN",L"1.5",L"9999"}){std::wstring copy=value;Check(!NormalizeTextPresentationSetting(key,copy,error),"layout sizes reject malformed or unsafe input");}
    std::wstring minimum=L"25",maximum=L"200",tooSmall=L"24",tooBig=L"201";Check(NormalizeTextPresentationSetting(L"TextIconScalePercent",minimum,error)&&NormalizeTextPresentationSetting(L"TextIconScalePercent",maximum,error)&&!NormalizeTextPresentationSetting(L"TextIconScalePercent",tooSmall,error)&&!NormalizeTextPresentationSetting(L"TextIconScalePercent",tooBig,error),"icon scale enforces both inclusive bounds");
    Snapshot frame;frame.gameSource=true;frame.detailRows={{L"Long & readable title",L"A wrapped resource value",L"",L"notes-genshin-resin_full"},{L"Second activity",L"Complete",L"",L""},{L"Third activity",L"0/4",L"",L""},{L"Fourth",L"4/5"},{L"Fifth",L"Ready"},{L"Sixth",L"Hidden by tile capacity"}};
    const auto original=frame.presentation;const auto p=GameTextPresentation(original);
    Check(p.wrap&&p.primaryStyle==L"base"&&p.primaryLines==2&&p.secondaryLines==2&&p.iconPlacement==L"Right"&&original.preset==L"Default","game Default is readable and does not mutate the source configuration");
    std::vector<MediaIndicatorImage> icons{{L"ms-appx:///row-icon.png",L"Resource"}};
    for(const int width:{150,310}){std::wstring xml;Check(AppendGameTileBinding(xml,width==150?L"TileMedium":L"TileWide",width,150,frame,L"",icons,L"ms-appx:///gap.png"),"game layout builds both horizontal sizes");
        Check(xml.find(L"Long &amp; readable title")!=std::wstring::npos&&xml.find(L"hint-maxLines=\"2\"")!=std::wstring::npos,"titles escape XML and support wrapping");
        Check(xml.find(L"Long &amp; readable title")<xml.find(L"ms-appx:///row-icon.png"),"default icon column follows the text column");
        Check(xml.find(L"Second activity")!=std::wstring::npos&&xml.find(L"Fourth")==std::wstring::npos,"horizontal tiles keep bounded readable rows");
        const auto geometry=ComputeGameRowGeometry(width,true,p);Check(geometry.iconX>geometry.textX+geometry.textWidth&&geometry.iconX+geometry.iconSide<=width-10+.001,"right icon and text have separate bounded columns");
    }
    auto left=frame;left.presentation.iconPlacement=L"Left";std::wstring xml;
    Check(AppendGameTileBinding(xml,L"TileWide",310,150,left,L"",icons)&&xml.find(L"ms-appx:///row-icon.png")<xml.find(L"Long &amp; readable title"),"Left explicitly reverses icon and text ordering");
    const auto small=ComputeGameRowGeometry(150,true,p);auto larger=p;larger.iconScalePercent=200;Check(ComputeGameRowGeometry(150,true,larger).iconSide>small.iconSide,"icon scale changes geometry without changing topic data");
    auto none=frame;none.presentation.iconPlacement=L"None";xml.clear();AppendGameTileBinding(xml,L"TileLarge",310,310,none,L"",icons);
    Check(xml.find(L"row-icon.png")==std::wstring::npos&&xml.find(L"Fifth")!=std::wstring::npos&&xml.find(L"Sixth")==std::wstring::npos,"None suppresses row icons while Large keeps five notes");
    Check(ComputeGameRowGeometry(150,false,p).textWidth==130&&ComputeGameRowGeometry(150,true,none.presentation).textWidth==130,"unavailable and disabled icons return the full width to text");
    xml.clear();AppendGameTileBinding(xml,L"TileWide",310,150,frame,L"",{});Check(xml.find(L"alt=")==std::wstring::npos&&xml.find(L"A wrapped resource value")!=std::wstring::npos,"missing WuWa artwork keeps ordinary readable text without a fabricated icon");
    auto custom=frame;custom.presentation.preset=L"Custom";custom.presentation.primaryStyle=L"title";custom.presentation.primaryLines=1;custom.presentation.wrap=false;custom.presentation.align=L"right";custom.presentation.rowSpacing=0;
    xml.clear();AppendGameTileBinding(xml,L"TileWide",310,150,custom,L"",icons,L"ms-appx:///gap.png");Check(xml.find(L"hint-style=\"title\"")!=std::wstring::npos&&xml.find(L"hint-wrap=\"false\"")!=std::wstring::npos&&xml.find(L"hint-align=\"right\"")!=std::wstring::npos&&xml.find(L"gap.png")==std::wstring::npos,"explicit typography and zero row spacing remain effective");
    auto changed=frame.presentation;changed.rowSpacing=24;Check(TextPresentationKey(changed)!=TextPresentationKey(frame.presentation),"row spacing participates in render invalidation");changed=frame.presentation;changed.iconPlacement=L"None";Check(TextPresentationKey(changed)!=TextPresentationKey(frame.presentation),"icon position participates in render invalidation");
    xml=L"sentinel";Check(!AppendGameTileBinding(xml,L"TileSmall",71,71,frame,L"")&&xml==L"sentinel","Small remains static instead of acquiring unreadable game text");
    std::cout<<checks<<" game layout checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

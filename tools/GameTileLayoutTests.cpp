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
    Check(p.wrap&&p.primaryStyle==L"base"&&p.primaryLines==3&&p.secondaryLines==2&&p.iconPlacement==L"Right"&&original.preset==L"Default","game Default follows readable Medium RSS typography without mutating source configuration");
    for(const auto size:std::vector<std::pair<int,int>>{{150,150},{310,150},{310,310}}){const auto game=GameTextPresentation(original,size.first,size.second),rss=RssRichPresentation(original,size.first,size.second);
        Check(game.primaryStyle==rss.primaryStyle&&game.secondaryStyle==rss.secondaryStyle&&game.primaryLines==rss.primaryLines&&game.secondaryLines==rss.secondaryLines&&game.wrap==rss.wrap&&game.gapLines==rss.gapLines,"default game text uses RSS's effective per-size styles and line budgets");
        Check(GameTileRows(size.first,size.second)==RssRichTileRows(size.first,size.second),"default game entries use RSS's per-size capacity");}
    std::vector<MediaIndicatorImage> icons{{L"ms-appx:///row-icon.png",L"Resource"}};
    for(const int width:{150,310}){std::wstring xml;Check(AppendGameTileBinding(xml,width==150?L"TileMedium":L"TileWide",width,150,frame,L"",icons,L"ms-appx:///gap.png"),"game layout builds both horizontal sizes");
        Check(xml.find(L"Long &amp; readable title")!=std::wstring::npos&&xml.find(L"hint-maxLines=\"2\"")!=std::wstring::npos,"titles escape XML and support wrapping");
        Check(xml.find(L"Long &amp; readable title")<xml.find(L"ms-appx:///row-icon.png"),"default icon column follows the text column");
        Check((xml.find(L"Second activity")!=std::wstring::npos)==(width>200)&&xml.find(L"Third activity")==std::wstring::npos,"Medium and Wide keep RSS's bounded one/two entry capacity");
        const auto geometry=ComputeGameRowGeometry(width,true,p);Check(geometry.iconX>geometry.textX+geometry.textWidth&&geometry.iconX+geometry.iconSide<=width-10+.001,"right icon and text have separate bounded columns");
    }
    auto left=frame;left.presentation.iconPlacement=L"Left";std::wstring xml;
    Check(AppendGameTileBinding(xml,L"TileWide",310,150,left,L"",icons)&&xml.find(L"ms-appx:///row-icon.png")<xml.find(L"Long &amp; readable title"),"Left explicitly reverses icon and text ordering");
    const auto small=ComputeGameRowGeometry(150,true,p);auto larger=p;larger.iconScalePercent=200;Check(ComputeGameRowGeometry(150,true,larger).iconSide>small.iconSide,"icon scale changes geometry without changing topic data");
    auto none=frame;none.presentation.iconPlacement=L"None";xml.clear();AppendGameTileBinding(xml,L"TileLarge",310,310,none,L"",icons);
    Check(xml.find(L"row-icon.png")==std::wstring::npos&&xml.find(L"Third activity")!=std::wstring::npos&&xml.find(L"Fourth")==std::wstring::npos,"None suppresses row icons while default Large keeps three RSS-like entries");
    Check(ComputeGameRowGeometry(150,false,p).textWidth==130&&ComputeGameRowGeometry(150,true,none.presentation).textWidth==130,"unavailable and disabled icons return the full width to text");
    xml.clear();AppendGameTileBinding(xml,L"TileWide",310,150,frame,L"",{});Check(xml.find(L"alt=")==std::wstring::npos&&xml.find(L"A wrapped resource value")!=std::wstring::npos,"missing WuWa artwork keeps ordinary readable text without a fabricated icon");
    auto custom=frame;custom.presentation.preset=L"Custom";custom.presentation.primaryStyle=L"title";custom.presentation.primaryLines=1;custom.presentation.wrap=false;custom.presentation.align=L"right";custom.presentation.rowSpacing=0;
    xml.clear();AppendGameTileBinding(xml,L"TileWide",310,150,custom,L"",icons,L"ms-appx:///gap.png");Check(xml.find(L"hint-style=\"title\"")!=std::wstring::npos&&xml.find(L"hint-wrap=\"false\"")!=std::wstring::npos&&xml.find(L"hint-align=\"right\"")!=std::wstring::npos&&xml.find(L"gap.png")==std::wstring::npos,"explicit typography and zero row spacing remain effective");
    Check(GameTileRows(150,150,custom.presentation)==2&&GameTileRows(310,150,custom.presentation)==3&&GameTileRows(310,310,custom.presentation)==5,"Custom retains its explicit compact capacity instead of being remapped to Default RSS capacity");
    auto controls=original;controls.iconPlacement=L"Left";controls.iconScalePercent=175;controls.rowSpacing=21;
    const auto retained=GameTextPresentation(controls,310,150);Check(retained.iconPlacement==L"Left"&&retained.iconScalePercent==175&&retained.rowSpacing==21,"RSS default typography preserves independent game icon and row-spacing controls");
    auto stale=frame;stale.detailRows.insert(stale.detailRows.begin(),{L"",L"Refreshed 5m ago\nRefresh delayed",L""});
    for(const auto size:std::vector<std::pair<int,int>>{{150,150},{310,150},{310,310}}){const auto count=GameVisibleRowCount(stale,size.first,size.second);
        Check(count==RssRichTileRows(size.first,size.second)+1,"one leading freshness status does not consume the RSS resource-entry budget");
        const auto style=GameTextPresentation(stale.presentation,size.first,size.second);Check(GameRowPresentation(stale,style,0,size.first,size.second).secondaryLines>=2,"freshness age and threshold error retain a two-line status budget");
        Check(GameRowPresentation(stale,style,1,size.first,size.second).primaryLines==(size.first>200&&size.second<=200?1:style.primaryLines),"Wide resources reserve room for the freshness header without shrinking the resource entry capacity");
        std::wstring statusXml;AppendGameTileBinding(statusXml,size.first==150?L"TileMedium":size.second==150?L"TileWide":L"TileLarge",size.first,size.second,stale,L"");
        Check(statusXml.find(L"Refreshed 5m ago")!=std::wstring::npos&&statusXml.find(L"Long &amp; readable title")!=std::wstring::npos,"stale native XML retains useful resource information alongside its age");}
    stale.presentation=custom.presentation;Check(GameVisibleRowCount(stale,150,150)==2,"Custom retains the old total-row capacity even with a status row");
    auto changed=frame.presentation;changed.rowSpacing=24;Check(TextPresentationKey(changed)!=TextPresentationKey(frame.presentation),"row spacing participates in render invalidation");changed=frame.presentation;changed.iconPlacement=L"None";Check(TextPresentationKey(changed)!=TextPresentationKey(frame.presentation),"icon position participates in render invalidation");
    xml=L"sentinel";Check(!AppendGameTileBinding(xml,L"TileSmall",71,71,frame,L"")&&xml==L"sentinel","Small remains static instead of acquiring unreadable game text");
    std::cout<<checks<<" game layout checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

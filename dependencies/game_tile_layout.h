#pragma once
// Pure presentation of already-fetched game notes. No provider or asset I/O.
#include "media_tile_layout.h"
#include <cmath>
namespace aip { namespace content {
inline std::size_t GameTileRows(int width,int height){return height<150?0u:height>200?5u:width>200?3u:2u;}
inline TextPresentation GameTextPresentation(const TextPresentation& supplied){auto value=supplied;
    if(!value.Active()){value.preset=L"Custom";value.primaryStyle=L"base";value.secondaryStyle=L"captionSubtle";value.primaryLines=value.secondaryLines=2;value.wrap=true;value.gapLines=0;value.align=L"left";value.stacking=L"top";}
    return value;
}
struct GameRowGeometry {double textX=10,textWidth=0,iconX=0,iconSide=0;unsigned iconWeight=0,textWeight=100;};
inline GameRowGeometry ComputeGameRowGeometry(int width,bool available,const TextPresentation& presentation){GameRowGeometry row;const double full=(std::max)(0,width-20);row.textWidth=full;
    if(!available||presentation.iconPlacement==L"None"||full<=16)return row;
    const double desired=24.0*presentation.iconScalePercent/100.0,inner=full-8;
    row.iconWeight=static_cast<unsigned>((std::max)(1,(std::min)(40,static_cast<int>(std::lround(desired*100/inner)))));row.textWeight=100-row.iconWeight;
    row.iconSide=inner*row.iconWeight/100;row.textWidth=inner-row.iconSide;row.iconX=presentation.iconPlacement==L"Left"?10:width-10-row.iconSide;
    if(presentation.iconPlacement==L"Left")row.textX=10+row.iconSide+8;return row;
}
inline void AppendGameText(std::wstring& xml,const std::wstring& text,const TextPresentation& p,bool secondary){if(text.empty())return;
    xml+=L"<text hint-style=\""+(secondary?p.secondaryStyle:p.primaryStyle)+L"\" hint-wrap=\""+(p.wrap?L"true":L"false")+
        L"\" hint-maxLines=\""+std::to_wstring(p.wrap?(secondary?p.secondaryLines:p.primaryLines):1)+L"\" hint-align=\""+p.align+L"\">"+MediaXmlEscape(text)+L"</text>";
}
inline bool AppendGameTileBinding(std::wstring& xml,const wchar_t* binding,int width,int height,const Snapshot& frame,const std::wstring& background,
    const std::vector<MediaIndicatorImage>& icons={},const std::wstring& spacingUri={}){
    if(!binding||!*binding||frame.detailRows.empty()||height<150)return false;const auto p=GameTextPresentation(frame.presentation);
    xml+=L"<binding template=\""+std::wstring(binding)+L"\" hint-textStacking=\""+p.stacking+L"\">";
    if(!background.empty())xml+=L"<image placement=\"background\" src=\""+MediaXmlEscape(background)+L"\"/>";
    const auto count=(std::min)(frame.detailRows.size(),GameTileRows(width,height));
    for(size_t index=0;index<count;++index){if(index&&p.rowSpacing>0&&!spacingUri.empty())xml+=L"<image src=\""+MediaXmlEscape(spacingUri)+L"\" hint-removeMargin=\"true\"/>";
        const auto image=index<icons.size()?icons[index]:MediaIndicatorImage{};const auto geometry=ComputeGameRowGeometry(width,!image.uri.empty(),p);
        const auto icon=[&]{if(geometry.iconWeight)xml+=L"<subgroup hint-weight=\""+std::to_wstring(geometry.iconWeight)+L"\" hint-textStacking=\"center\"><image src=\""+MediaXmlEscape(image.uri)+L"\" alt=\""+MediaXmlEscape(image.label)+L"\" hint-removeMargin=\"true\"/></subgroup>";};
        xml+=L"<group>";if(p.iconPlacement==L"Left")icon();xml+=L"<subgroup"+(geometry.iconWeight?L" hint-weight=\""+std::to_wstring(geometry.textWeight)+L"\"":std::wstring{})+L" hint-textStacking=\"top\">";
        AppendGameText(xml,frame.detailRows[index].primary,p,false);
        if(!frame.detailRows[index].secondary.empty()&&p.gapLines)xml+=L"<text hint-style=\"caption\" hint-minLines=\""+std::to_wstring(p.gapLines)+L"\" hint-maxLines=\""+std::to_wstring(p.gapLines)+L"\"> </text>";
        AppendGameText(xml,frame.detailRows[index].secondary,p,true);xml+=L"</subgroup>";if(p.iconPlacement==L"Right")icon();xml+=L"</group>";
    }xml+=L"</binding>";return true;
}
}}

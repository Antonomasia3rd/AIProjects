#pragma once
#include "prayer_tile_layout.h"
namespace aip { namespace content {
struct MailPlanText {std::wstring text,style;PrayerFieldRole role=PrayerFieldRole::Time;int lines=1;bool wrap=false;};
struct MailPlanRow {MailPlanText primary,secondary;};
inline std::vector<MailPlanRow> BuildMailTilePlan(const Snapshot& frame,int width,int height){
    std::vector<MailPlanRow> rows;if(!frame.mailSource||frame.textMode==TextMode::Off||height<150||frame.mailRows.empty()||(height>200&&!frame.mailIncludeLarge))return rows;
    const bool large=height>200;const auto begin=large?size_t(0):(std::min)(frame.mailMessageIndex,frame.mailRows.size()-1);
    if(!large&&frame.mailMessageIndex>=frame.mailRows.size())return rows;
    const auto count=large?(std::min)(frame.mailRows.size(),size_t(5)):size_t(1);
    for(size_t index=begin;index<begin+count;++index){const auto& message=frame.mailRows[index];
        rows.push_back({{message.primary,L"caption",PrayerFieldRole::Time,large?1:width>200?1:2,!large},
            {message.secondary,L"captionSubtle",PrayerFieldRole::Secondary,large?2:width>200?3:2,true}});}
    return rows;
}
inline void AppendMailPlanText(std::wstring& xml,const MailPlanText& field,const TextPresentation& presentation){
    if(field.text.empty())return;
    if(presentation.Active()){AppendPrayerClockText(xml,field.text,field.style.c_str(),field.lines,L"left",presentation,field.role);return;}
    xml+=L"<text hint-style=\""+field.style+L"\"";
    if(field.wrap)xml+=L" hint-wrap=\"true\" hint-maxLines=\""+std::to_wstring(field.lines)+L"\"";
    xml+=L">"+MediaXmlEscape(field.text)+L"</text>";
}
inline bool AppendMailTileBinding(std::wstring& xml,const wchar_t* tileTemplate,int height,const Snapshot& frame,const std::wstring& background={}){
    if(!tileTemplate||!*tileTemplate||frame.textMode!=TextMode::Auto)return false;
    const auto name=std::wstring(tileTemplate);if(name!=L"TileMedium"&&name!=L"TileWide"&&name!=L"TileLarge")return false;
    const auto rows=BuildMailTilePlan(frame,name==L"TileMedium"?150:310,height);if(rows.empty())return false;
    xml+=L"<binding template=\""+name+L"\"";
    if(!frame.secondaryBranding.empty()&&frame.secondaryBranding!=L"Auto"){
        const auto& value=frame.secondaryBranding;xml+=L" branding=\""+std::wstring(value==L"None"?L"none":value==L"Logo"?L"logo":value==L"Name"?L"name":L"nameAndLogo")+L"\"";
    }
    xml+=L" hint-textStacking=\""+(frame.presentation.Active()?frame.presentation.stacking:L"top")+L"\">";
    if(!background.empty())xml+=L"<image placement=\"background\" src=\""+MediaXmlEscape(background)+L"\"/>";
    const bool grouped=name!=L"TileMedium";
    for(const auto& row:rows){if(grouped)xml+=L"<group><subgroup>";AppendMailPlanText(xml,row.primary,frame.presentation);AppendMailPlanText(xml,row.secondary,frame.presentation);if(grouped)xml+=L"</subgroup></group>";}
    xml+=L"</binding>";return true;
}
} }

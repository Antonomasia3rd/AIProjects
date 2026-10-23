#pragma once
#include "prayer_tile_layout.h"
namespace aip { namespace content {
inline size_t DateTimeEventCount(int height) { return height > 200 ? 3u : 1u; }
inline const wchar_t* ClockTimeStyle(int width,int height) {return height>200?L"subheader":width>200?L"title":L"base";}
inline bool CalendarDateOnlyCentered(const Snapshot& frame,int width,int height) {
    return frame.calendarSource&&width<=150&&(height<150||frame.detailRows.empty());
}
inline bool DateTimeNativeBranding(const Snapshot& frame) {
    return frame.secondaryBranding.empty()||frame.secondaryBranding==L"Auto";
}
inline std::wstring DateTimeBindingBranding(const Snapshot& frame,bool centered,bool mediumSize) {
    if(!DateTimeNativeBranding(frame)){
        const auto& selected=frame.secondaryBranding;
        return selected==L"None"?L"none":selected==L"Logo"?L"logo":selected==L"Name"?L"name":L"nameAndLogo";
    }
    return centered?L"none":frame.clockSource&&mediumSize?L"name":L"nameAndLogo";
}
inline bool AppendDateTimeTileBinding(std::wstring& xml,const wchar_t* tileTemplate,int height,
    const Snapshot& frame,const std::wstring& background={}) {
    if(!tileTemplate||!*tileTemplate||(!frame.clockSource&&!frame.calendarSource))return false;
    const auto name=std::wstring(tileTemplate);const bool isSmall=name==L"TileSmall",isMedium=name==L"TileMedium";
    if(height<150&&(!isSmall||!frame.calendarSource))return false;
    const auto& p=frame.presentation;const auto& date=frame.dateTime;
    const bool centered=frame.calendarSource&&(isSmall||(isMedium&&frame.detailRows.empty()));
    xml+=L"<binding template=\""+name+L"\"";
    xml+=L" branding=\""+DateTimeBindingBranding(frame,centered,isMedium)+L"\"";
    if(frame.clockSource)xml+=L" displayName=\""+MediaXmlEscape(date.location)+L"\"";
    xml+=L" hint-textStacking=\""+(p.Active()?p.stacking:centered?L"center":L"top")+L"\">";
    if(!background.empty())xml+=L"<image placement=\"background\" src=\""+MediaXmlEscape(background)+L"\"/>";
    if(frame.clockSource) {
        const auto time=std::wstring(1,date.rightToLeft?L'\x200f':L'\x200e')+date.time;
        if(!p.Active()){
            xml+=L"<text hint-style=\""+std::wstring(ClockTimeStyle(isMedium?150:310,height))+L"\">"+MediaXmlEscape(time)+L"</text>";
            if(!date.date.empty())xml+=L"<text hint-style=\"captionsubtle\">"+MediaXmlEscape(date.weekday)+L"</text>";
        }else{
            AppendPrayerClockText(xml,time,ClockTimeStyle(isMedium?150:310,height),1,L"left",p,PrayerFieldRole::Time);
            if(!date.date.empty())AppendPrayerClockText(xml,date.weekday,L"captionSubtle",1,L"left",p,PrayerFieldRole::Secondary);
        }
    } else if(centered) {
        const auto& weekday=isSmall&&!date.shortWeekday.empty()?date.shortWeekday:date.weekday;
        if(isSmall&&!p.Active())xml+=L"<text hint-align=\"center\">"+MediaXmlEscape(weekday)+L"</text>";
        else AppendPrayerClockText(xml,weekday,L"caption",1,L"center",p,PrayerFieldRole::Secondary);
        AppendPrayerClockText(xml,date.dayNumber,isSmall?L"titleNumeral":L"headerNumeral",1,L"center",p,PrayerFieldRole::Time);
    } else {
        // Calendar keeps its familiar weekday and large day stamp on the right.
        AppendPrayerClockText(xml,date.weekday,L"caption",1,L"right",p,PrayerFieldRole::Secondary);
        AppendPrayerClockText(xml,date.dayNumber,L"headerNumeral",1,L"right",p,PrayerFieldRole::Time);
        AppendPrayerClockText(xml,date.location,L"captionSubtle",1,L"left",p,PrayerFieldRole::Secondary);
        const auto count=(std::min)(frame.detailRows.size(),DateTimeEventCount(height));
        for(size_t i=0;i<count;++i) {
            AppendPrayerClockText(xml,frame.detailRows[i].primary,L"base",height>200?2:1,L"left",p,PrayerFieldRole::Badge);
            AppendPrayerClockText(xml,frame.detailRows[i].secondary,L"captionSubtle",height>200?2:1,L"left",p,PrayerFieldRole::Secondary);
        }
        if(frame.detailRows.empty())AppendPrayerClockText(xml,frame.text.secondary,L"captionSubtle",2,L"left",p,PrayerFieldRole::Secondary);
    }
    xml+=L"</binding>";return true;
}
}}

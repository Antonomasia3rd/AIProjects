#pragma once
#include "prayer_tile_layout.h"
namespace aip { namespace content {
inline size_t DateTimeEventCount(int height) { return height > 200 ? 3u : 1u; }
inline bool AppendDateTimeTileBinding(std::wstring& xml,const wchar_t* tileTemplate,int height,
    const Snapshot& frame,const std::wstring& background={}) {
    if(!tileTemplate||!*tileTemplate||height<150||(!frame.clockSource&&!frame.calendarSource))return false;
    const auto& p=frame.presentation;const auto& date=frame.dateTime;
    xml+=L"<binding template=\""+std::wstring(tileTemplate)+L"\" hint-textStacking=\""+(p.Active()?p.stacking:L"top")+L"\">";
    if(!background.empty())xml+=L"<image placement=\"background\" src=\""+MediaXmlEscape(background)+L"\"/>";
    if(frame.clockSource) {
        AppendPrayerClockText(xml,date.time,L"title",1,L"left",p,PrayerFieldRole::Time);
        AppendPrayerClockText(xml,date.location,L"captionSubtle",2,L"left",p,PrayerFieldRole::Secondary);
        AppendPrayerClockText(xml,date.date,L"captionSubtle",2,L"left",p,PrayerFieldRole::Secondary);
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

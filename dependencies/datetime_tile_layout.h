#pragma once
#include "prayer_tile_layout.h"
namespace aip { namespace content {
inline size_t DateTimeEventCount(int height) { return height > 200 ? 3u : 1u; }
inline const wchar_t* ClockTimeStyle(int width,int height) {return StatusTilePrimaryStyle(width,height);}
inline bool CalendarDateOnlyCentered(const Snapshot& frame,int width,int height) {
    return frame.calendarSource&&width<=150&&(height<150||frame.calendarEvents.empty());
}
inline bool DateTimeNativeBranding(const Snapshot& frame) {
    return frame.secondaryBranding.empty()||frame.secondaryBranding==L"Auto";
}
inline std::wstring DateTimeBindingBranding(const Snapshot& frame,bool centered,bool mediumSize) {
    if(!DateTimeNativeBranding(frame)){
        const auto& selected=frame.secondaryBranding;
        return selected==L"None"?L"none":selected==L"Logo"?L"logo":selected==L"Name"?L"name":L"nameAndLogo";
    }
    return centered?L"none":mediumSize&&(frame.clockSource||(frame.calendarSource&&!frame.calendarEvents.empty()))?L"name":L"nameAndLogo";
}
inline std::wstring CalendarDateBranding(const Snapshot& frame){return frame.dateTime.weekday+(frame.dateTime.weekday.empty()?L"":L" ")+frame.dateTime.dayNumber;}
inline bool CalendarUsesDateBranding(const Snapshot& frame,int width,int height){return frame.calendarSource&&frame.calendarFlavor==CalendarFlavor::NextEvent&&DateTimeNativeBranding(frame)&&!frame.calendarEvents.empty()&&height>=150&&(width<=150||height>200);}
inline std::wstring DateTimeBindingDisplayName(const Snapshot& frame,int width,int height){
    return frame.clockSource?frame.dateTime.location:CalendarUsesDateBranding(frame,width,height)?CalendarDateBranding(frame):std::wstring();
}
struct CalendarPlanText {
    std::wstring text,style=L"caption",align=L"left";
    PrayerFieldRole role=PrayerFieldRole::Time;int lines=1;bool wrap=false,spacer=false;
};
struct CalendarPlanColumn {unsigned weight=1;std::vector<CalendarPlanText> texts;};
struct CalendarPlanGroup {std::vector<CalendarPlanColumn> columns;};
struct CalendarTilePlan {std::vector<CalendarPlanGroup> groups;};
inline CalendarPlanText CalendarField(std::wstring text,const wchar_t* style=L"caption",PrayerFieldRole role=PrayerFieldRole::Time,int lines=1,bool wrap=false,const wchar_t* align=L"left"){
    CalendarPlanText result;result.text=std::move(text);result.style=style;result.role=role;result.lines=lines;result.wrap=wrap;result.align=align;return result;
}
inline CalendarPlanText CalendarSpacer(){CalendarPlanText result;result.spacer=true;return result;}
// These are the installed HxOutlookBackground.dll's distinct event layouts,
// not one generic list with a date stamp added above it. The default plan is
// shared by adaptive XML and the bitmap renderer; explicit typography still
// applies through semantic text roles.
inline CalendarTilePlan BuildCalendarTilePlan(const Snapshot& frame,int width,int height){
    CalendarTilePlan plan;if(!frame.calendarSource||frame.calendarEvents.empty()||height<150)return plan;
    if(frame.calendarFlavor==CalendarFlavor::Agenda&&(width<=150||height>200))return plan;
    const auto direct=[&](std::vector<CalendarPlanText> texts){CalendarPlanColumn column;column.texts=std::move(texts);CalendarPlanGroup group;group.columns.push_back(std::move(column));plan.groups.push_back(std::move(group));};
    const auto& first=frame.calendarEvents.front();
    if(width<=150){direct({CalendarField(first.title,L"caption",PrayerFieldRole::Time,2,true),CalendarField(first.mediumWhen.empty()?first.startTime:first.mediumWhen,L"captionSubtle",PrayerFieldRole::Secondary,2,true)});return plan;}
    if(height>200){
        std::vector<CalendarPlanText> rows{CalendarField(first.title,L"body",PrayerFieldRole::Time,2,true)};
        if(!first.location.empty())rows.push_back(CalendarField(first.location,L"captionSubtle",PrayerFieldRole::Secondary));
        rows.push_back(CalendarField(first.timeRange,L"captionSubtle",PrayerFieldRole::Secondary));
        const auto count=(std::min)(frame.calendarEvents.size(),size_t(3));
        for(size_t index=1;index<count;++index){rows.push_back(CalendarSpacer());rows.push_back(CalendarField(frame.calendarEvents[index].title));
            rows.push_back(CalendarField(frame.calendarEvents[index].timeRange,L"captionSubtle",PrayerFieldRole::Secondary));}
        direct(std::move(rows));return plan;
    }
    if(frame.calendarFlavor==CalendarFlavor::Agenda){
        direct({CalendarField(frame.calendarAgendaHeader)});
        // The host prepares the agenda's day. Do not refilter locale/date
        // labels here: the native second notification can represent that day.
        std::vector<const CalendarEvent*> today;for(const auto& event:frame.calendarEvents)if(today.size()<3)today.push_back(&event);
        bool longTime=false;for(const auto* event:today)longTime=longTime||event->startTime.size()>8;
        CalendarPlanGroup rows;CalendarPlanColumn times,titles;times.weight=longTime?3u:2u;titles.weight=5;
        for(size_t index=0;index<today.size();++index){const auto style=index>frame.calendarAgendaHighlightThrough?L"captionSubtle":L"caption";
            times.texts.push_back(CalendarField(today[index]->startTime,style,PrayerFieldRole::Secondary));titles.texts.push_back(CalendarField(today[index]->title,style));}
        if(!today.empty()){rows.columns.push_back(std::move(times));rows.columns.push_back(std::move(titles));plan.groups.push_back(std::move(rows));}
        if(today.size()==1)direct({CalendarSpacer(),CalendarField(frame.calendarAgendaEmpty,L"captionSubtle",PrayerFieldRole::Secondary)});
        return plan;
    }
    CalendarPlanGroup row;CalendarPlanColumn event,date;event.weight=7;date.weight=3;
    event.texts.push_back(CalendarField(first.title,L"caption",PrayerFieldRole::Time,2,true));
    if(!first.location.empty())event.texts.push_back(CalendarField(first.location,L"captionSubtle",PrayerFieldRole::Secondary));
    event.texts.push_back(CalendarField(first.wideWhen.empty()?first.timeRange:first.wideWhen,L"captionSubtle",PrayerFieldRole::Secondary));
    date.texts.push_back(CalendarField(frame.dateTime.shortWeekday.empty()?frame.dateTime.weekday:frame.dateTime.shortWeekday,L"caption",PrayerFieldRole::Secondary,1,false,L"right"));
    date.texts.push_back(CalendarField(frame.dateTime.dayNumber,L"headerNumeral",PrayerFieldRole::Badge,1,false,L"right"));
    row.columns.push_back(std::move(event));row.columns.push_back(std::move(date));plan.groups.push_back(std::move(row));return plan;
}
inline void AppendCalendarPlanText(std::wstring& xml,const CalendarPlanText& field,const TextPresentation& presentation){
    if(field.spacer){xml+=L"<text/>";return;}if(field.text.empty())return;
    if(presentation.Active()){AppendPrayerClockText(xml,field.text,field.style.c_str(),field.lines,field.align.c_str(),presentation,field.role);return;}
    xml+=L"<text hint-style=\""+field.style+L"\"";
    if(field.wrap)xml+=L" hint-wrap=\"true\" hint-maxLines=\""+std::to_wstring(field.lines)+L"\"";
    if(field.align!=L"left")xml+=L" hint-align=\""+field.align+L"\"";
    xml+=L">"+MediaXmlEscape(field.text)+L"</text>";
}
inline void AppendCalendarTilePlan(std::wstring& xml,const CalendarTilePlan& plan,const TextPresentation& presentation){
    for(const auto& group:plan.groups){const bool columns=group.columns.size()>1;if(columns)xml+=L"<group>";
        for(const auto& column:group.columns){if(columns)xml+=L"<subgroup hint-weight=\""+std::to_wstring(column.weight)+L"\">";
            for(const auto& field:column.texts)AppendCalendarPlanText(xml,field,presentation);if(columns)xml+=L"</subgroup>";}
        if(columns)xml+=L"</group>";}
}
inline bool AppendDateTimeTileBinding(std::wstring& xml,const wchar_t* tileTemplate,int height,
    const Snapshot& frame,const std::wstring& background={}) {
    if(!tileTemplate||!*tileTemplate||(!frame.clockSource&&!frame.calendarSource))return false;
    const auto name=std::wstring(tileTemplate);const bool isSmall=name==L"TileSmall",isMedium=name==L"TileMedium";
    if(height<150&&(!isSmall||!frame.calendarSource))return false;
    const auto& p=frame.presentation;const auto& date=frame.dateTime;
    if(frame.calendarSource&&frame.calendarFlavor==CalendarFlavor::Agenda&&!isSmall&&name!=L"TileWide")return false;
    const bool centered=frame.calendarSource&&(isSmall||(isMedium&&frame.calendarEvents.empty()));
    xml+=L"<binding template=\""+name+L"\"";
    xml+=L" branding=\""+DateTimeBindingBranding(frame,centered,isMedium)+L"\"";
    const auto displayName=DateTimeBindingDisplayName(frame,isSmall?71:isMedium?150:310,height);
    if(frame.clockSource||!displayName.empty())xml+=L" displayName=\""+MediaXmlEscape(displayName)+L"\"";
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
    } else if(!frame.calendarEvents.empty()) {
        AppendCalendarTilePlan(xml,BuildCalendarTilePlan(frame,isMedium?150:310,height),p);
    } else {
        // Calendar keeps its familiar weekday and large day stamp on the right.
        AppendPrayerClockText(xml,date.weekday,L"caption",1,L"right",p,PrayerFieldRole::Secondary);
        AppendPrayerClockText(xml,date.dayNumber,L"headerNumeral",1,L"right",p,PrayerFieldRole::Time);
        AppendPrayerClockText(xml,date.location,L"captionSubtle",1,L"left",p,PrayerFieldRole::Secondary);
        AppendPrayerClockText(xml,frame.text.secondary,L"captionSubtle",2,L"left",p,PrayerFieldRole::Secondary);
    }
    xml+=L"</binding>";return true;
}
}}

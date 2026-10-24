#pragma once
// Pure setup-picker mapping: row zero combines calendars; rows one onward
// select the corresponding readable calendar. No account or UI operations.
#include "google_calendar_model.h"
#include <cstddef>
#include <utility>
namespace aip { namespace google_calendar {
using SelectionSettings=std::vector<std::pair<const wchar_t*,std::wstring>>;
inline int PickerSelection(bool allCalendars,const std::wstring& calendarId,const std::vector<CalendarChoice>& choices) {
    if(allCalendars)return 0;
    for(std::size_t index=0;index<choices.size();++index)
        if(choices[index].id==calendarId||(calendarId==L"primary"&&choices[index].primary))return static_cast<int>(index+1);
    return choices.size()==1?1:-1;
}
inline bool PickerSettings(std::ptrdiff_t row,const std::vector<CalendarChoice>& choices,SelectionSettings& settings) {
    settings.clear();if(row<0||static_cast<std::size_t>(row)>choices.size())return false;
    // Leaving CalendarId untouched makes switching All -> Single recover the
    // user's prior selection instead of replacing it with a magic identifier.
    if(row==0)settings={{L"GoogleCalendar.AllCalendars",L"1"},{L"Calendar.Source",L"Google"}};
    else {
        const auto& selected=choices[static_cast<std::size_t>(row-1)];if(selected.id.empty())return false;
        settings={{L"GoogleCalendar.AllCalendars",L"0"},{L"GoogleCalendar.CalendarId",selected.id},{L"Calendar.Source",L"Google"}};
    }
    return true;
}
}} // namespace aip::google_calendar

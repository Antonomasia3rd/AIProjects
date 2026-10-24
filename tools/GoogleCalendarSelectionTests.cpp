#include "../dependencies/content_sources/google_calendar_selection.h"
#include <iostream>
namespace {
int checks=0,failures=0;
void Check(bool result,const char* message){++checks;if(!result){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
std::wstring Read(const aip::google_calendar::SelectionSettings& settings,const wchar_t* key){
    for(const auto& value:settings)if(value.first==std::wstring(key))return value.second;return {};
}
}
int main(){
    using namespace aip::google_calendar;
    const std::vector<CalendarChoice> choices={{L"first",L"Work",L"UTC",false},{L"primary@example.invalid",L"Personal",L"Asia/Jakarta",true}};
    Check(PickerSelection(true,L"first",choices)==0,"All flag selects the first combo row despite a saved individual calendar");
    Check(PickerSelection(false,L"first",choices)==1,"first individual calendar shifts to row one");
    Check(PickerSelection(false,L"primary@example.invalid",choices)==2,"last individual calendar retains its shifted row");
    Check(PickerSelection(false,L"primary",choices)==2,"primary alias selects the actual primary calendar after the All row");
    Check(PickerSelection(false,L"removed",choices)==-1,"missing selection is not silently redirected to a different calendar");
    Check(PickerSelection(false,L"removed",{choices.front()})==1,"one remaining readable calendar remains the existing fallback selection");
    Check(PickerSelection(true,L"primary",{})==0&&PickerSelection(false,L"primary",{})==-1,"empty lists preserve All identity without inventing an individual choice");
    SelectionSettings settings;
    Check(PickerSettings(0,choices,settings)&&Read(settings,L"GoogleCalendar.AllCalendars")==L"1"&&Read(settings,L"Calendar.Source")==L"Google",
        "All row saves the explicit source flag and Google provider");
    Check(settings.size()==2&&Read(settings,L"GoogleCalendar.CalendarId").empty(),"All save leaves the previous CalendarId untouched");
    std::map<std::wstring,std::wstring> stored{{L"GoogleCalendar.CalendarId",L"keep-individual"}};
    for(const auto& setting:settings)stored[setting.first]=setting.second;
    Check(stored[L"GoogleCalendar.CalendarId"]==L"keep-individual"&&stored[L"GoogleCalendar.AllCalendars"]==L"1",
        "applying an All selection preserves the old single-calendar identity");
    Check(PickerSettings(1,choices,settings)&&Read(settings,L"GoogleCalendar.AllCalendars")==L"0"&&Read(settings,L"GoogleCalendar.CalendarId")==L"first",
        "first individual row saves its exact ID with All disabled");
    Check(PickerSettings(2,choices,settings)&&Read(settings,L"GoogleCalendar.AllCalendars")==L"0"&&Read(settings,L"GoogleCalendar.CalendarId")==L"primary@example.invalid",
        "last individual row cannot be confused with the All row");
    for(const std::ptrdiff_t invalid:{std::ptrdiff_t(-1),std::ptrdiff_t(-2),std::ptrdiff_t(3),std::ptrdiff_t(1000000)})
        Check(!PickerSettings(invalid,choices,settings)&&settings.empty(),"invalid/CB_ERR selections stage no partial settings");
    auto invalid=choices;invalid.front().id.clear();
    Check(!PickerSettings(1,invalid,settings)&&settings.empty(),"a malformed individual choice cannot save an empty ID");
    std::cout<<checks<<" Calendar selection checks, "<<failures<<" failures\n";return failures?1:0;
}

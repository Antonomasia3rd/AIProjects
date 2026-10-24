// Actual host reminder adapter with synthetic occurrences, owned temporary
// profile identity and injected persistence/notification transports only.
static std::map<std::wstring,std::wstring> calendarReminderHostFiles;
static std::vector<std::wstring> calendarReminderHostXml,calendarReminderHostRemoved;
static unsigned calendarReminderHostWrites=0;
static bool calendarReminderHostSaveSucceeds=true;
static CalendarReminderLoad CalendarReminderHostLoad(const std::wstring& path,const std::wstring& scope,aip::calendar_reminders::State& state)
{
    const auto file=calendarReminderHostFiles.find(path);if(file==calendarReminderHostFiles.end())return CalendarReminderLoad::Missing;
    return aip::calendar_reminders::Parse(file->second,scope,state)?CalendarReminderLoad::Loaded:CalendarReminderLoad::Failed;
}
static bool CalendarReminderHostSave(const std::wstring& path,const std::wstring& text)
{
    ++calendarReminderHostWrites;if(!calendarReminderHostSaveSucceeds)return false;calendarReminderHostFiles[path]=text;return true;
}
static bool CalendarReminderHostEmit(const std::wstring&,const std::wstring&,const std::wstring&,
    const std::wstring& xml,const aip::notifications::NativeNoticeOptions& options)
{
    Check(calendarReminderHostWrites>0,"Calendar host persists its occurrence claim before emitting a reminder");
    Check(!options.suppressPopup&&options.expirationUtc>0&&!options.group.empty(),"Calendar host forwards popup, owned group and event expiry");
    calendarReminderHostXml.push_back(xml);return true;
}
static bool CalendarReminderHostRemove(const std::wstring& tag,const std::wstring& group)
{
    Check(tag.compare(0,18,L"calendar-reminder-")==0&&!group.empty(),"Calendar cleanup targets an owned reminder tag and profile group");
    calendarReminderHostRemoved.push_back(tag);return true;
}
static void TestCalendarReminderHost()
{
    using namespace aip::calendar_reminders;
    auto oldRuntime=std::move(g_calendarReminders);g_calendarReminders.clear();
    auto oldFiles=std::move(calendarReminderHostFiles);auto oldXml=std::move(calendarReminderHostXml);auto oldRemoved=std::move(calendarReminderHostRemoved);
    const auto oldLoad=g_testLoadCalendarReminders;const auto oldSave=g_testSaveCalendarReminders;
    const auto oldEmit=g_testEmitCalendarReminder;const auto oldRemove=g_testRemoveCalendarReminder;
    const auto oldWrites=calendarReminderHostWrites;const bool oldSaveSucceeds=calendarReminderHostSaveSucceeds;
    const auto oldUser=g_testTileRegistrationUserScope;
    const auto oldNotifications=g_notificationsEnabled.load();const bool oldRender=g_commandLine.renderOnly,oldConfigure=g_commandLine.configureOnly;
    g_testLoadCalendarReminders=CalendarReminderHostLoad;g_testSaveCalendarReminders=CalendarReminderHostSave;
    g_testEmitCalendarReminder=CalendarReminderHostEmit;g_testRemoveCalendarReminder=CalendarReminderHostRemove;
    g_testTileRegistrationUserScope=L"4444444444444444";g_notificationsEnabled=true;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    calendarReminderHostWrites=0;calendarReminderHostSaveSucceeds=true;
    aip::content::TileInstance tile;tile.id=L"calendar-reminder-fixture";tile.name=L"Private calendar fixture";
    tile.type=aip::content::TileInstanceType::Calendar;
    tile.settings[L"calendarreminders.enabled"]=L"1";tile.settings[L"calendarreminders.leadminutes"]=L"15";
    const UTC now=1792738800;const Occurrence event{L"synthetic-calendar/event",L"Private synthetic title",L"11:30 AM - noon",L"Private fixture location",now+900,now+2700,false};
    Check(CalendarRemindersDemanded(tile),"enabled Calendar reminders demand their source even without a Start pin");
    ObserveCalendarReminders(tile,{event},now,true);
    Check(calendarReminderHostXml.size()==1&&calendarReminderHostXml.front().find(L"arguments=\"snooze\"")!=std::wstring::npos,
        "host sends the actual system Snooze/Dismiss payload through the injected transport");
    Check(calendarReminderHostFiles.size()==1&&calendarReminderHostFiles.begin()->second.find(event.title)==std::wstring::npos,
        "host ledger persistence contains no event title or location");
    ObserveCalendarReminders(tile,{event},now+1,true);Check(calendarReminderHostXml.size()==1,"repeated cached Calendar reads do not reannounce the occurrence");
    g_calendarReminders.clear();ObserveCalendarReminders(tile,{event},now+2,true);
    Check(calendarReminderHostXml.size()==1,"a fresh adapter loads the saved delivery claim and does not duplicate it");
    ObserveCalendarReminders(tile,{},now+3,false);Check(calendarReminderHostRemoved.empty(),"stale/pending empty source keeps the system-owned snooze intact");
    ObserveCalendarReminders(tile,{},now+4,true);Check(calendarReminderHostRemoved.size()==1,"completed authoritative cancellation removes the owned reminder");
    ObserveCalendarReminders(tile,{event},now+5,true);Check(calendarReminderHostXml.size()==1,"cancellation cleanup retains duplicate protection");
    auto second=event;second.identity=L"synthetic-calendar/second";second.startUtc+=100;second.endUtc+=100;
    calendarReminderHostSaveSucceeds=false;ObserveCalendarReminders(tile,{second},now+100,true);
    Check(calendarReminderHostXml.size()==1,"failed ledger persistence fails closed without a popup");
    calendarReminderHostSaveSucceeds=true;g_calendarReminders[tile.id].retrySaveAfter=0;ObserveCalendarReminders(tile,{second},now+101,true);
    Check(calendarReminderHostXml.size()==2,"an ordinary later poll can retry a failed save within the due grace");
    const auto removals=calendarReminderHostRemoved.size();tile.settings[L"calendarreminders.enabled"]=L"0";
    Check(!CalendarRemindersDemanded(tile),"disabled Calendar reminders release unpinned source demand");
    ObserveCalendarReminders(tile,{},now+102,false);Check(calendarReminderHostRemoved.size()==removals+1,"explicit disable cleans active owned reminders while source is pending");
    tile.settings[L"calendarreminders.enabled"]=L"1";g_notificationsEnabled=false;ObserveCalendarReminders(tile,{event},now,true);
    Check(calendarReminderHostXml.size()==2,"Calendar reminders obey the application notification master switch");
    g_notificationsEnabled=true;g_commandLine.configureOnly=true;Check(!CalendarRemindersDemanded(tile),"offline configure-only mode cannot demand a reminder source");
    ClearCalendarReminders();PruneCalendarReminders({});
    g_calendarReminders=std::move(oldRuntime);calendarReminderHostFiles=std::move(oldFiles);
    calendarReminderHostXml=std::move(oldXml);calendarReminderHostRemoved=std::move(oldRemoved);
    calendarReminderHostWrites=oldWrites;calendarReminderHostSaveSucceeds=oldSaveSucceeds;
    g_testLoadCalendarReminders=oldLoad;g_testSaveCalendarReminders=oldSave;g_testEmitCalendarReminder=oldEmit;g_testRemoveCalendarReminder=oldRemove;
    g_testTileRegistrationUserScope=oldUser;g_notificationsEnabled=oldNotifications;g_commandLine.renderOnly=oldRender;g_commandLine.configureOnly=oldConfigure;
}

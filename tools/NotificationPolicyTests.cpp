// Real adapter dispatch and pure payload policy with both operating-system
// transports replaced. No tray icon, notification, app identity or package API.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <optional>
#include <functional>
#include <deque>
#include "../dependencies/notification_policy.h"
#include "../dependencies/tile_source_alerts.h"
#include "../dependencies/notification_confirmation.h"

// Capture the actual source payload requested by the adapter without changing
// either operating-system transport or the production hook signature.
namespace notification_policy_fixture { static std::wstring sourceXml; }
namespace aip::notifications {
inline std::wstring NotificationPolicySourceEventXml(const std::wstring& title,const std::wstring& message,EventType type,EventSound sound) {
    auto xml=SourceEventXml(title,message,type,sound);notification_policy_fixture::sourceXml=xml;return xml;
}
}
namespace {
using aip::notifications::Progress;
int checks = 0;
void Check(bool result, const char* name) { ++checks; if (!result) throw std::runtime_error(name); }
std::atomic<bool> g_notificationsEnabled{true};
std::atomic<bool> g_running{true};
static int TestConfirmationDialog(HWND,const wchar_t*,const wchar_t*,UINT){return IDNO;}
#define DESKTOPSTUB_CONTENT_MESSAGEBOX TestConfirmationDialog
struct { bool renderOnly = false, configureOnly = false; } g_commandLine;
std::mutex g_pathMutex;
std::wstring g_iniPath = L"C:\\Profiles\\one.ini";
std::map<std::pair<std::wstring, std::wstring>, std::wstring> settings;
std::wstring IniReadS(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    auto found = settings.find({section, key}); return found == settings.end() ? fallback : found->second;
}
int IniReadI(const wchar_t* section, const wchar_t* key, int fallback) {
    const auto value = IniReadS(section, key, L""); return value.empty() ? fallback : _wtoi(value.c_str());
}
int ClampInt(int value, int minimum, int maximum) { return std::max(minimum, std::min(value, maximum)); }
bool identity = true, toastSucceeds = true;
unsigned balloonCalls = 0, toastCalls = 0;
DWORD lastBalloonIcon=0;
std::wstring lastTitle, lastMessage, lastKey;
bool hadProgress = false;
Progress lastProgress;
unsigned quietCalls=0,removeCalls=0;
bool quietSucceeds=true,removeSucceeds=true;
aip::notifications::NativeNoticeOptions lastQuietOptions;
std::wstring lastRemovedKey,lastRemovedGroup;
std::map<std::pair<std::wstring,std::wstring>,std::wstring> quietHistory;
bool TestIdentity() { return identity; }
bool TestBalloon(const std::wstring& title, const std::wstring& message, DWORD icon, bool) {
    lastBalloonIcon=icon;
    ++balloonCalls; lastTitle = title; lastMessage = message; return true;
}
bool TestToast(const std::wstring& key, const std::wstring& title, const std::wstring& message, const Progress* progress) {
    ++toastCalls; lastKey = key; lastTitle = title; lastMessage = message;
    hadProgress = progress != nullptr; if (progress) lastProgress = *progress; return toastSucceeds;
}
bool TestQuietToast(const std::wstring& key,const std::wstring& title,const std::wstring& message,const aip::notifications::NativeNoticeOptions& options) {
    ++quietCalls;lastKey=key;lastTitle=title;lastMessage=message;lastQuietOptions=options;
    if(quietSucceeds)quietHistory[{options.group,aip::notifications::StableId(key)}]=message;
    return quietSucceeds;
}
bool TestRemoveToast(const std::wstring& key,const std::wstring& group) {
    ++removeCalls;lastRemovedKey=key;lastRemovedGroup=group;
    if(removeSucceeds)quietHistory.erase({group,aip::notifications::StableId(key)});
    return removeSucceeds;
}
#define DESKTOPSTUB_TEST_NOTIFICATION_IDENTITY TestIdentity
#define DESKTOPSTUB_TEST_NOTIFICATION_BALLOON TestBalloon
#define DESKTOPSTUB_TEST_NOTIFICATION_TOAST TestToast
#define SourceEventXml NotificationPolicySourceEventXml
#include "../dependencies/DesktopStub/ga_notifications.inc"
#undef SourceEventXml

void TestModel() {
    using namespace aip::notifications;
    Check(ParseApi(L"bAlLoOn") == Api::Balloon && ParseApi(L"TOAST") == Api::Toast && ParseApi(L"unexpected") == Api::Auto, "API selection handles case and safe unknown defaults");
    for (auto requested : {Api::Auto, Api::Balloon, Api::Toast}) {
        Check(EffectiveApi(requested, false) == Api::Balloon, "unpackaged desktop always has legacy fallback without registration");
        Check(EffectiveApi(requested, true) == (requested == Api::Balloon ? Api::Balloon : Api::Toast), "forced legacy remains legacy with package identity");
    }
    Check(StableId(L"one").size() == 16 && StableId(L"one") == StableId(L"one") && StableId(L"one") != StableId(L"two"), "bounded stable notification identity");
    Check(Escape(L"<&>\"'\x01") == L"&lt;&amp;&gt;&quot;&apos;", "untrusted XML contents are escaped and invalid control characters removed");
    Check(MessageXml(L"<unsafe>", L"a&b").find(L"<unsafe>") == std::wstring::npos, "message XML cannot inject markup");
    Check(ProgressXml(L"pin<&").find(L"status=\"{progressStatus}\"") != std::wstring::npos && ProgressXml(L"pin<&").find(L"pin&lt;&amp;") != std::wstring::npos, "progress XML binds each mutable field and escapes title");
    Progress progress; progress.completed = 1; progress.total = 3;
    Check(progress.Value() == L"0.333" && progress.Count() == L"1 / 3", "fraction and count agree without locale decimal separators");
    progress.completed = 8; Check(progress.Value() == L"1.000", "overruns never exceed complete progress bar");
    progress.total = 0; Check(progress.Value() == L"indeterminate", "unknown totals are indeterminate");
    ProgressTracker tracker; Progress emitted;
    Progress input{L"batch",L"Pin all",L"Waiting",0,28,false,false};
    Check(tracker.Next(input, 1000, 750, emitted) && emitted.first && emitted.sequence == 1, "first update starts one operation");
    input.completed = 1;
    Check(!tracker.Next(input, 1100, 750, emitted), "rapid progress is throttled");
    Check(tracker.Next(input, 1750, 750, emitted) && !emitted.first && emitted.sequence == 2, "updated progress uses increasing sequence in same operation");
    Check(!tracker.Next(input, 2750, 750, emitted), "identical progress does not emit another update");
    input.completed = 28; input.finished = true;
    Check(tracker.Next(input, 1800, 750, emitted) && emitted.finished && emitted.sequence == 3, "completion bypasses throttle and is never lost");
    input.completed = 0; input.finished = false;
    Check(tracker.Next(input, 1801, 750, emitted) && emitted.first && emitted.sequence == 1, "later batch with same key restarts its sequence");
    input.completed = 40; input.finished = true; input.failed = true;
    Check(tracker.Next(input, 1802, 750, emitted) && emitted.completed == 28 && emitted.failed, "terminal failure preserves severity and bounds");
    for (int n = 0; n < 1000; ++n) { input.key = std::to_wstring(n); Check(tracker.Next(input, 2000+n, 750, emitted), "bounded abandoned operation table accepts new operations"); }
}
void TestAdapter() {
    using aip::notifications::Api;
    const auto one = NotificationGroup(); g_iniPath = L"c:\\profiles\\ONE.ini";
    Check(NotificationGroup() == one, "same Windows profile path has one notification group");
    g_iniPath = L"C:\\Profiles\\two.ini"; Check(NotificationGroup() != one, "different INI profiles cannot overwrite notification progress");
    identity = true; settings.clear(); balloonCalls = toastCalls = 0;
    DispatchUserNotification(L"one",L"Title",L"Message",NIIF_INFO,false);
    Check(toastCalls == 1 && !balloonCalls && lastKey == L"one", "Auto dispatches to existing packaged toast identity");
    settings[{L"Settings",L"NotificationApi"}] = L"Balloon";
    DispatchUserNotification(L"one",L"Title",L"Message",NIIF_INFO,false);
    Check(toastCalls == 1 && balloonCalls == 1, "explicit Balloon never invokes modern API");
    settings[{L"Settings",L"NotificationApi"}] = L"Toast"; identity = false;
    DispatchUserNotification(L"one",L"Title",L"Message",NIIF_INFO,false);
    Check(toastCalls == 1 && balloonCalls == 2 && ConfiguredNotificationApi() == Api::Balloon, "Toast without identity falls back without trying a package API");
    identity = true; toastSucceeds = false;
    DispatchUserNotification(L"one",L"Title",L"Message",NIIF_ERROR,false);
    Check(toastCalls == 2 && balloonCalls == 3, "modern transport failure falls back to legacy");
    Check(ConfiguredNotificationApi() == Api::Balloon, "failed modern transport backs off instead of retrying on every operation");
    g_notificationToastRetryAfter = 0;
    g_notificationsEnabled = false;
    DispatchUserNotification(L"one",L"Title",L"Message",NIIF_ERROR,false);
    Check(toastCalls == 2 && balloonCalls == 3, "master notification switch suppresses both backends");
    DispatchUserNotification(L"one",L"Title",L"Message",NIIF_ERROR,true);
    Check(toastCalls == 2 && balloonCalls == 3, "master off also suppresses forced warnings");
    for (bool* inertMode : {&g_commandLine.renderOnly, &g_commandLine.configureOnly}) {
        *inertMode = true;
        DispatchUserNotification(L"one",L"Title",L"Message",NIIF_ERROR,true);
        Check(toastCalls == 2 && balloonCalls == 3, "offline render/configure mode suppresses even forced notices");
        *inertMode = false;
    }
    g_notificationsEnabled = true; toastSucceeds = true; g_notificationRecent.clear();
    settings[{L"Settings",L"NotifyOnOperations"}] = L"0";
    NotifyUserOperation(L"save",L"Tile",L"Saved");
    Check(toastCalls == 2, "operations category suppresses notices");
    settings[{L"Settings",L"NotifyOnOperations"}] = L"1";
    NotifyUserOperation(L"save",L"Tile",L"Saved"); NotifyUserOperation(L"save",L"Tile",L"Saved");
    Check(toastCalls == 3, "repeated identical actionable notice is coalesced");
    NotifyUserOperation(L"save",L"Tile",L"Failed",NIIF_ERROR);
    Check(toastCalls == 4 && lastMessage == L"Failed", "changed result bypasses duplicate suppression");
    g_notificationProgress.Clear();
    NotifyOperationProgress(L"pin",L"Pin all",L"Waiting",0,28);
    Check(toastCalls == 5 && hadProgress && lastProgress.first && lastProgress.sequence == 1 && lastProgress.total == 28, "actual adapter sends bound progress initial payload");
    NotifyOperationProgress(L"pin",L"Pin all",L"Waiting",1,28);
    Check(toastCalls == 5, "adapter throttles intermediate updates");
    NotifyOperationProgress(L"pin",L"Pin all",L"Complete",28,28,true,false);
    Check(toastCalls == 6 && hadProgress && lastProgress.finished && lastMessage.find(L"28 / 28") != std::wstring::npos, "actual terminal notification includes result and full count without waiting");
    settings[{L"Settings",L"NotifyOnPinProgress"}] = L"0";
    NotifyOperationProgress(L"pin",L"Pin all",L"Waiting",0,28);
    Check(toastCalls == 6, "pin progress category can be disabled");
    settings[{L"Settings",L"NotifyOnPinProgress"}] = L"1"; settings[{L"Settings",L"NotificationApi"}] = L"Balloon";
    g_notificationProgress.Clear();
    NotifyOperationProgress(L"pin",L"Pin all",L"Waiting",2,28);
    Check(balloonCalls == 4 && lastMessage.find(L"2 / 28") != std::wstring::npos, "legacy fallback has a readable numeric progress count");
    NotifyOperationProgress(L"pin",L"Pin all",L"Stopped",2,28,true,true);
    Check(balloonCalls == 5 && lastMessage.find(L"Stopped") != std::wstring::npos, "legacy terminal status also bypasses progress throttle");
    settings[{L"Settings",L"NotifyOnOperations"}]=L"0";
    NotifyScheduledUserEvent(L"prayer",L"Prayer",L"Dhuhr in 20 minutes",aip::notifications::EventType::Reminder,aip::notifications::EventSound::Silent);
    Check(balloonCalls==6&&(lastBalloonIcon&NIIF_NOSOUND),"opted-in source reminders retain silence independently from operation notices on legacy API");
    g_notificationsEnabled=false;
    NotifyScheduledUserEvent(L"prayer",L"Prayer",L"Dhuhr",aip::notifications::EventType::Alarm,aip::notifications::EventSound::Alarm);
    Check(balloonCalls==6,"master off suppresses source alarms and their sound");
    g_notificationsEnabled=true;
    unsigned approved=0;
    ClearUserConfirmations();
    Check(!ConfirmUserOperation(nullptr,L"cancel",L"Confirm",L"Question",[&]{++approved;}),"a declined legacy confirmation is not treated as approval");
    QueueUserConfirmationAction(L"review");DrainUserConfirmations();
    Check(approved==0&&g_confirmations.Entries().empty(),"notification body activation does not approve an operation");
}
void TestQuietStatus() {
    g_notificationsEnabled=true;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    settings.clear();identity=true;toastSucceeds=true;quietSucceeds=removeSucceeds=true;
    g_notificationToastRetryAfter=0;quietCalls=removeCalls=balloonCalls=toastCalls=0;quietHistory.clear();
    g_testQuietNotificationToast=TestQuietToast;g_testRemoveNotificationToast=TestRemoveToast;
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto expires=now+3600;
    const auto ownGroup=aip::notifications::StableId(L"private-profile/prayer-one");
    const auto siblingGroup=aip::notifications::StableId(L"private-profile/prayer-two");
    const auto ownTag=std::make_pair(ownGroup,aip::notifications::StableId(L"current-prayer"));
    Check(NotifyQuietUserStatus(L"current-prayer",L"Dhuhr <fixture>",L"Until 3:00 PM & next prayer",expires,ownGroup)&&
        quietCalls==1&&toastCalls==0&&balloonCalls==0,"quiet status uses only its injected native history transport");
    Check(lastQuietOptions.suppressPopup&&lastQuietOptions.expirationUtc==expires&&lastQuietOptions.group==ownGroup,
        "quiet status suppresses popups and preserves exact expiry and owned notification group");
    const auto xml=notification_policy_fixture::sourceXml;
    Check(xml.find(L"<audio silent=\"true\"/>")!=std::wstring::npos&&xml.find(L"scenario=")==std::wstring::npos&&
        xml.find(L"loop=\"true\"")==std::wstring::npos&&xml.find(L"<actions>")==std::wstring::npos&&
        xml.find(L"Dhuhr &lt;fixture&gt;")!=std::wstring::npos&&xml.find(L"&amp; next prayer")!=std::wstring::npos,
        "actual quiet adapter payload is silent general history content with escaped text and no alarm actions");
    Check(NotifyQuietUserStatus(L"current-prayer",L"Asr",L"Updated current prayer",expires+60,ownGroup)&&quietHistory.size()==1&&
        quietHistory.at(ownTag)==L"Updated current prayer","same quiet key/group replaces its existing history entry instead of accumulating copies");
    Check(NotifyQuietUserStatus(L"current-prayer",L"Sibling",L"Keep sibling",expires,siblingGroup)&&quietHistory.size()==2,
        "identical quiet keys in separate owned groups remain independent");
    Check(RemoveQuietUserStatus(L"current-prayer",ownGroup)&&removeCalls==1&&lastRemovedKey==L"current-prayer"&&lastRemovedGroup==ownGroup&&
        quietHistory.count(ownTag)==0&&quietHistory.count({siblingGroup,aip::notifications::StableId(L"current-prayer")})==1,
        "quiet cleanup targets the stable owned key/group while preserving sibling notification history");
    const auto beforeRemove=removeCalls;
    Check(RemoveQuietUserStatus(L"",ownGroup)&&RemoveQuietUserStatus(L"current-prayer",L"")&&removeCalls==beforeRemove,
        "empty quiet removal identities cannot turn into a blanket history clear");
    const auto quietBefore=quietCalls;
    Check(!NotifyQuietUserStatus(L"expired",L"Expired",L"Do not show",now-1,ownGroup)&&
        !NotifyQuietUserStatus(L"invalid-time",L"Invalid",L"Do not show",253402300800LL,ownGroup)&&quietCalls==quietBefore,
        "expired and out-of-range quiet lifetimes reject before native transport");
    settings[{L"Settings",L"NotificationApi"}]=L"Balloon";
    Check(!NotifyQuietUserStatus(L"legacy",L"Legacy",L"Do not pop up",expires,ownGroup)&&quietCalls==quietBefore&&balloonCalls==0,
        "forced legacy mode disables quiet history instead of inventing a balloon popup equivalent");
    settings[{L"Settings",L"NotificationApi"}]=L"Toast";identity=false;
    Check(!NotifyQuietUserStatus(L"unpackaged",L"No identity",L"No fallback",expires,ownGroup)&&quietCalls==quietBefore&&balloonCalls==0,
        "quiet status without existing identity creates no package and has no balloon fallback");
    identity=true;g_notificationsEnabled=false;
    Check(!NotifyQuietUserStatus(L"muted",L"Muted",L"Do not show",expires,ownGroup)&&quietCalls==quietBefore&&balloonCalls==0,
        "master-off suppresses quiet status as well as normal notification delivery");
    Check(RemoveQuietUserStatus(L"current-prayer",siblingGroup)&&quietHistory.empty(),
        "master-off still permits targeted removal of an already-owned quiet history entry");
    g_notificationsEnabled=true;
    for(bool* offline:{&g_commandLine.renderOnly,&g_commandLine.configureOnly}){
        *offline=true;Check(!NotifyQuietUserStatus(L"offline",L"Offline",L"No native effect",expires,ownGroup)&&quietCalls==quietBefore,
            "offline render/configure modes cannot create quiet history");*offline=false;
    }
    DispatchUserNotification(L"ordinary",L"Ordinary",L"Normal notification",NIIF_INFO,false);
    Check(toastCalls==1&&quietCalls==quietBefore&&lastKey==L"ordinary"&&balloonCalls==0,
        "quiet status options do not leak into ordinary native notification dispatch");
    quietSucceeds=false;
    Check(!NotifyQuietUserStatus(L"failed",L"Failed",L"No fallback",expires,ownGroup)&&quietCalls==quietBefore+1&&balloonCalls==0&&
        g_notificationToastRetryAfter>GetTickCount64(),"quiet native failure is backoff-limited and never falls back to a popup balloon");
    Check(!NotifyQuietUserStatus(L"backoff",L"Backoff",L"No retry",expires,ownGroup)&&quietCalls==quietBefore+1&&balloonCalls==0,
        "quiet failure backoff prevents repeated native attempts");
    removeSucceeds=false;Check(!RemoveQuietUserStatus(L"failed",ownGroup)&&balloonCalls==0,"quiet cleanup failure reports failure without creating a replacement notification");
    g_testQuietNotificationToast=nullptr;g_testRemoveNotificationToast=nullptr;g_notificationToastRetryAfter=0;
    settings.clear();quietHistory.clear();identity=toastSucceeds=true;
}
}
int main() {
    try { TestModel(); TestAdapter(); TestQuietStatus(); std::cout << checks << " notification policy/adapter checks; 0 failures\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}

// Real adapter dispatch and pure payload policy with both operating-system
// transports replaced. No tray icon, notification, app identity or package API.
#define NOMINMAX
#include <windows.h>
#include <atomic>
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
bool TestIdentity() { return identity; }
bool TestBalloon(const std::wstring& title, const std::wstring& message, DWORD icon, bool) {
    lastBalloonIcon=icon;
    ++balloonCalls; lastTitle = title; lastMessage = message; return true;
}
bool TestToast(const std::wstring& key, const std::wstring& title, const std::wstring& message, const Progress* progress) {
    ++toastCalls; lastKey = key; lastTitle = title; lastMessage = message;
    hadProgress = progress != nullptr; if (progress) lastProgress = *progress; return toastSucceeds;
}
#define DESKTOPSTUB_TEST_NOTIFICATION_IDENTITY TestIdentity
#define DESKTOPSTUB_TEST_NOTIFICATION_BALLOON TestBalloon
#define DESKTOPSTUB_TEST_NOTIFICATION_TOAST TestToast
#include "../dependencies/DesktopStub/ga_notifications.inc"

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
}
int main() {
    try { TestModel(); TestAdapter(); std::cout << checks << " notification policy/adapter checks; 0 failures\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}

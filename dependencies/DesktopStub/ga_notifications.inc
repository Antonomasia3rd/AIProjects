// These functions never create an application identity, shortcut, package or
// persistent notification worker. Toasts use the identity DesktopStub already has.
#include "../notification_policy.h"
#include "../tile_source_alerts.h"
#ifndef DESKTOPSTUB_TEST_NOTIFICATION_TOAST
#include "../winrt_activation.h"
#endif

static std::mutex g_notificationProgressMutex;
static aip::notifications::ProgressTracker g_notificationProgress;
static std::map<std::wstring, std::pair<std::wstring, ULONGLONG>> g_notificationRecent;
static std::map<std::wstring, bool> g_notificationToastStarted;
static std::atomic<ULONGLONG> g_notificationToastRetryAfter(0);

static bool NotificationExistingIdentity()
{
#ifdef DESKTOPSTUB_TEST_NOTIFICATION_IDENTITY
    return DESKTOPSTUB_TEST_NOTIFICATION_IDENTITY();
#elif defined(DESKTOPSTUB_INERT_HARDWARE)
    return false;
#else
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
#endif
}

static bool NotificationAllowed(bool force)
{
    (void)force; // Force may bypass a hidden tray/category, never the master switch.
    if (g_commandLine.renderOnly || g_commandLine.configureOnly) return false;
    return g_notificationsEnabled.load();
}

static bool NotificationBalloon(const std::wstring& title, const std::wstring& message, DWORD icon, bool force)
{
#ifdef DESKTOPSTUB_TEST_NOTIFICATION_BALLOON
    return DESKTOPSTUB_TEST_NOTIFICATION_BALLOON(title, message, icon, force);
#elif defined(DESKTOPSTUB_INERT_HARDWARE)
    (void)title; (void)message; (void)icon; (void)force; return false;
#else
    NOTIFYICONDATAW nid{};
    {
        std::lock_guard<std::mutex> lock(g_trayMutex);
        if ((!force && !g_tray) || !g_nid.hWnd) return false;
        nid = g_nid;
    }
    DWORD error = ERROR_SUCCESS;
    const bool ok = aip::ShowTrayBalloon(nid, title, message, icon,
        static_cast<UINT>(ClampInt(g_notificationTimeoutMs.load(), 1000, 60000)), &error);
    if (!ok) LogWin32Failure(L"ShowTrayBalloon", error);
    return ok;
#endif
}

static std::wstring NotificationGroup()
{
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    std::transform(profile.begin(), profile.end(), profile.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return aip::notifications::StableId(profile);
}

static bool NotificationToast(const std::wstring& key, const std::wstring& title, const std::wstring& message,
    const aip::notifications::Progress* progress, const std::wstring& customXml = {})
{
#ifdef DESKTOPSTUB_TEST_NOTIFICATION_TOAST
    (void)customXml;
    return DESKTOPSTUB_TEST_NOTIFICATION_TOAST(key, title, message, progress);
#elif defined(DESKTOPSTUB_INERT_HARDWARE)
    (void)key; (void)title; (void)message; (void)progress; (void)customXml; return false;
#else
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    struct ApartmentCleanup { HRESULT result; ~ApartmentCleanup() { if (SUCCEEDED(result)) RoUninitialize(); } } cleanup{initialized};
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
    try
    {
        using namespace winrt::Windows::UI::Notifications;
        const auto manager = aip::AcquireUncachedActivationFactory<ToastNotificationManager, IToastNotificationManagerStatics>();
        UINT32 appIdLength=0;
        if(GetCurrentApplicationUserModelId(&appIdLength,nullptr)!=ERROR_INSUFFICIENT_BUFFER||appIdLength<2||appIdLength>1024)return false;
        std::vector<wchar_t> appId(appIdLength);
        if(GetCurrentApplicationUserModelId(&appIdLength,appId.data())!=ERROR_SUCCESS)return false;
        const auto notifier = manager.CreateToastNotifier(appId.data());
        const auto setting = notifier.Setting();
        if (setting == NotificationSetting::DisabledForApplication || setting == NotificationSetting::DisabledForUser ||
            setting == NotificationSetting::DisabledByGroupPolicy) return true; // Respect Windows settings; do not escape through balloons.
        if (setting == NotificationSetting::DisabledByManifest) return false; // Capability unavailable, legacy API is still supported.
        const auto tag = aip::notifications::StableId(key);
        const auto group = NotificationGroup();
        const auto operation = group + tag;
        bool toastStarted = false;
        {
            std::lock_guard<std::mutex> lock(g_notificationProgressMutex);
            toastStarted = g_notificationToastStarted.count(operation) != 0;
            if (progress && progress->first) g_notificationToastStarted.erase(operation);
        }
        NotificationData data{nullptr};
        if (progress)
        {
            const auto dataFactory = aip::AcquireUncachedActivationFactory<NotificationData, winrt::Windows::Foundation::IActivationFactory>();
            data = dataFactory.ActivateInstance<NotificationData>();
            data.SequenceNumber(progress->sequence);
            data.Values().Insert(L"progressValue", progress->Value());
            data.Values().Insert(L"progressCount", progress->Count());
            data.Values().Insert(L"progressStatus", (message.empty() ? title : message).substr(0, 1024));
            if (!progress->first && !progress->finished && toastStarted)
            {
                // Updating a dismissed toast must not create another popup.
                const auto result = notifier.Update(data, tag, group);
                return result == NotificationUpdateResult::Succeeded || result == NotificationUpdateResult::NotificationNotFound;
            }
        }
        const auto xmlFactory = aip::AcquireUncachedActivationFactory<winrt::Windows::Data::Xml::Dom::XmlDocument,
            winrt::Windows::Foundation::IActivationFactory>();
        auto xml = xmlFactory.ActivateInstance<winrt::Windows::Data::Xml::Dom::XmlDocument>();
        xml.LoadXml(!customXml.empty() ? customXml : progress && !progress->finished ? aip::notifications::ProgressXml(title) : aip::notifications::MessageXml(title, message));
        const auto toastFactory = aip::AcquireUncachedActivationFactory<ToastNotification, IToastNotificationFactory>();
        auto toast = toastFactory.CreateToastNotification(xml);
        toast.Tag(tag); toast.Group(group);
        if (progress && !progress->finished) toast.Data(data);
        notifier.Show(toast);
        if (progress)
        {
            std::lock_guard<std::mutex> lock(g_notificationProgressMutex);
            if (progress->finished) g_notificationToastStarted.erase(operation);
            else {
                if (g_notificationToastStarted.size() >= 64) g_notificationToastStarted.erase(g_notificationToastStarted.begin());
                g_notificationToastStarted[operation] = true;
            }
        }
        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        LogText(L"[!] Notification toast failed; trying the tray balloon API (HRESULT " + std::to_wstring(static_cast<long>(error.code())) + L").");
        return false;
    }
    catch (...) { return false; }
#endif
}

static aip::notifications::Api ConfiguredNotificationApi()
{
    if (GetTickCount64() < g_notificationToastRetryAfter.load()) return aip::notifications::Api::Balloon;
    return aip::notifications::EffectiveApi(
        aip::notifications::ParseApi(IniReadS(L"Settings", L"NotificationApi", L"Auto")), NotificationExistingIdentity());
}

static void DispatchUserNotification(const std::wstring& key, const std::wstring& title,
    const std::wstring& message, DWORD icon, bool force)
{
    if (!NotificationAllowed(force)) return;
    if (ConfiguredNotificationApi() == aip::notifications::Api::Toast) {
        if (NotificationToast(key, title, message, nullptr)) return;
        g_notificationToastRetryAfter = GetTickCount64() + 30000;
    }
    NotificationBalloon(title, message, icon, force);
}

static void NotifyScheduledUserEvent(const std::wstring& key, const std::wstring& title, const std::wstring& message,
    aip::notifications::EventType type, aip::notifications::EventSound sound)
{
    if (!NotificationAllowed(false)) return;
    if (ConfiguredNotificationApi() == aip::notifications::Api::Toast) {
        if (NotificationToast(key,title,message,nullptr,aip::notifications::SourceEventXml(title,message,type,sound))) return;
        g_notificationToastRetryAfter=GetTickCount64()+30000;
    }
    // Balloons have no reminder/alarm scenario or looping audio. Keep their
    // content and silence choice while leaving presentation to Explorer.
    const DWORD icon=NIIF_INFO | (sound==aip::notifications::EventSound::Silent ? NIIF_NOSOUND : 0);
    NotificationBalloon(title,message,icon,false);
}

static void NotifyUserOperation(const std::wstring& key, const std::wstring& title,
    const std::wstring& message, DWORD icon = NIIF_INFO, bool force = false)
{
    if (!NotificationAllowed(force) || (!force && IniReadI(L"Settings", L"NotifyOnOperations", 1) == 0)) return;
    const auto now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_notificationProgressMutex);
        auto found = g_notificationRecent.find(key);
        if (found != g_notificationRecent.end() && found->second.first == message && now - found->second.second < 5000) return;
        if (g_notificationRecent.size() >= 64 && found == g_notificationRecent.end()) g_notificationRecent.erase(g_notificationRecent.begin());
        g_notificationRecent[key] = {message, now};
    }
    DispatchUserNotification(key, title, message, icon, force);
}

static void NotifyOperationProgress(const std::wstring& key, const std::wstring& title,
    const std::wstring& status, size_t completed, size_t total, bool finished = false, bool failed = false)
{
    if (!NotificationAllowed(false) || IniReadI(L"Settings", L"NotifyOnPinProgress", 1) == 0) return;
    const auto api = ConfiguredNotificationApi();
    const auto interval = static_cast<ULONGLONG>(ClampInt(IniReadI(L"Settings", L"NotificationProgressIntervalMs", 750), 250, 60000));
    aip::notifications::Progress value{key, title, status, completed, total, finished, failed}, emitted;
    {
        std::lock_guard<std::mutex> lock(g_notificationProgressMutex);
        if (!g_notificationProgress.Next(std::move(value), GetTickCount64(), api == aip::notifications::Api::Balloon ? std::max<ULONGLONG>(3000, interval) : interval, emitted)) return;
    }
    if (api == aip::notifications::Api::Toast) {
        if (NotificationToast(key, title, finished ? emitted.BalloonText() : status, &emitted)) return;
        g_notificationToastRetryAfter = GetTickCount64() + 30000;
    }
    NotificationBalloon(title, emitted.BalloonText(), failed ? NIIF_WARNING : NIIF_INFO, false);
}

#include "ga_notification_confirmation.inc"

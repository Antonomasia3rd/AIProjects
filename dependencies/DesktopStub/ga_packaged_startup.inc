// Include after ga_ui_logging.inc. All StartupTask calls run off the UI thread;
// the tray reads cached status and never waits for the Windows API.
static constexpr const wchar_t* DESKTOPSTUB_STARTUP_TASK_ID = L"DesktopStubStartup";

static bool DesktopStubDefaultStartupProfile()
{
    std::wstring executable, profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); executable = g_exePath; profile = g_iniPath; }
    std::wstring expected = executable;
    const auto separator = expected.find_last_of(L"\\/");
    const auto dot = expected.find_last_of(L'.');
    if (dot != std::wstring::npos && (separator == std::wstring::npos || dot > separator)) expected.resize(dot);
    expected += L".ini";
    return aip::ToLower(aip::MakeAbsolutePath(profile)) == aip::ToLower(aip::MakeAbsolutePath(expected));
}

static bool ValidateDesktopStubPackagedStartupPreference(bool desired, std::wstring& error, const wchar_t* targetOverride)
{
    if (!desired) return true; // A custom/legacy profile may always save disabled.
    if (!DesktopStubDefaultStartupProfile()) {
        error = L"Windows startup tasks launch the default INI only. For a custom --ini profile, use Run at sign-in (Startup folder) or --startup.";
        return false;
    }
    std::wstring target = targetOverride ? targetOverride : IniReadS(L"Settings", L"AppxManifestTarget", L"Windows10");
    if (ParseManifestCompatibilityTargetValue(target) != ManifestCompatibilityTarget::Windows10) {
        error = L"Windows startup tasks require the Windows10 manifest target. Windows8/Windows81 profiles can use Run at sign-in (Startup folder).";
        return false;
    }
    return true;
}

struct DesktopStubPackagedStartupWorker
{
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    bool stop = false, pending = false, busy = false, haveDesired = false, lastDesired = false;
    bool pendingDesired = false, pendingMutate = false;
    aip::PackagedStartupStatus status;
    std::wstring error;

    void Run()
    {
        for (;;) {
            bool desired = false, mutate = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stop || pending; });
                if (stop) return;
                desired = pendingDesired; mutate = pendingMutate; pending = false; busy = true;
            }
            aip::PackagedStartupStatus result;
            std::wstring failure;
            bool ok = false;
            try {
                ok = mutate ? aip::SetPackagedStartupTaskEnabled(DESKTOPSTUB_STARTUP_TASK_ID, desired, result, &failure)
                    : aip::QueryPackagedStartupTask(DESKTOPSTUB_STARTUP_TASK_ID, result, &failure);
            } catch (...) { failure = L"The Windows startup task worker failed."; }
            {
                std::lock_guard<std::mutex> lock(mutex);
                status = result; error = failure; busy = false;
            }
            if (!ok) Log(L"[!] Windows startup task: %s. If the package predates this option, regenerate its Windows10 manifest, register it again, and relaunch the packaged entry.", failure.c_str());
            else Log(L"Windows startup task: %s", aip::PackagedStartupStateName(result.state));
        }
    }
    void Queue(bool desired, bool mayMutate, bool refresh)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stop) return;
        const bool changedDesired = !haveDesired || lastDesired != desired;
        if (!changedDesired && !refresh) return;
        haveDesired = true; lastDesired = desired;
        // A status refresh must not replace a pending configuration mutation.
        if (!pending || changedDesired) {
            pendingDesired = desired;
            pendingMutate = changedDesired && mayMutate;
        }
        pending = true;
        if (!worker.joinable()) {
            try { worker = std::thread([this] { Run(); }); }
            catch (...) { pending = false; haveDesired = false; error = L"Could not start the Windows startup task worker."; return; }
        }
        changed.notify_one();
    }
    void Stop()
    {
        { std::lock_guard<std::mutex> lock(mutex); stop = true; pending = false; }
        changed.notify_one();
        if (worker.joinable()) worker.join();
    }
    ~DesktopStubPackagedStartupWorker() { Stop(); }
};
static DesktopStubPackagedStartupWorker g_packagedStartup;

static void QueueDesktopStubPackagedStartupSync(bool refresh = false)
{
    bool desired = false;
    if (!TryParseCommandLineBoolValue(IniReadS(L"Settings", L"RunAtStartupPackaged", L"0"), desired)) {
        std::lock_guard<std::mutex> lock(g_packagedStartup.mutex);
        g_packagedStartup.error = L"Settings.RunAtStartupPackaged expects a true/false value.";
        return;
    }
    std::wstring error;
    const bool supported = ValidateDesktopStubPackagedStartupPreference(desired, error);
    const bool packaged = CurrentProcessHasPackageIdentity();
    const bool windows10 = ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10;
    if (!supported || !packaged || !windows10) {
        std::lock_guard<std::mutex> lock(g_packagedStartup.mutex);
        g_packagedStartup.haveDesired = false;
        g_packagedStartup.status = {};
        g_packagedStartup.error = !supported ? error : !windows10 ?
            L"Windows8/Windows81 profiles use the Startup folder option." :
            L"Available after launching the registered Windows10 package. The Startup folder option works when launched directly.";
        return;
    }
    // Custom profiles may inspect package-wide state, but never change it.
    g_packagedStartup.Queue(desired, DesktopStubDefaultStartupProfile(), refresh);
}

static std::wstring DesktopStubPackagedStartupStatusText()
{
    std::lock_guard<std::mutex> lock(g_packagedStartup.mutex);
    if (g_packagedStartup.busy || g_packagedStartup.pending) return L"Windows startup task: applying/checking...";
    if (!g_packagedStartup.error.empty()) return L"Windows startup task: " + g_packagedStartup.error +
        L"\r\nFor an older registered package, regenerate its Windows10 manifest, register it again, and relaunch the packaged entry.";
    return L"Windows startup task: " + std::wstring(aip::PackagedStartupStateName(g_packagedStartup.status.state));
}

static void StopDesktopStubPackagedStartup() { g_packagedStartup.Stop(); }

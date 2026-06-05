using CommandLineSetting = aip::IniSetting;

struct AppOptions
{
    bool showHelp = false;
    bool showVersion = false;
    bool dryRun = false;
    bool dryRunFull = false;
    bool verbose = false;
    bool once = false;
    bool forceNoTray = false;
    bool forceTray = false;
    bool forceShowConsole = false;
    bool forceHideConsole = false;
    bool requestExit = false;
    bool protectToken = false;
    bool reloadExistingInstance = false;
    std::wstring configPath;
    std::vector<CommandLineSetting> settings;
};

enum SingleInstanceRequest : WPARAM
{
    DRPC_INSTANCE_SHOW = 0,
    DRPC_INSTANCE_EXIT = 1,
    DRPC_INSTANCE_RELOAD = 2
};

struct IpcOptions
{
    int connectTimeoutMs = 250;
    int responseTimeoutMs = 5000;
};

struct SystemStats
{
    double cpuPercent = 0.0;
    double ramPercent = 0.0;
    double totalRamGb = 0.0;
    double usedRamGb = 0.0;
    double availRamGb = 0.0;
};

struct BatteryInfo
{
    bool hasBattery = false;
    int percent = 0;
    bool plugged = false;
    std::wstring remainingText;
};

struct ForegroundWindowInfo
{
    std::wstring title;
    std::wstring className;
    std::wstring exeName;
    std::wstring exePath;
    DWORD pid = 0;
};

struct TemplateContext
{
    std::map<std::wstring, std::wstring, std::less<>> tokens;
    int idleSeconds = 0;
};

struct AssetInfo
{
    std::wstring image;
    std::wstring text;
};

struct PresencePayload
{
    std::string activityJson;
    TemplateContext context;
};

struct GatewayOptions
{
    std::wstring systemLocale;
    std::wstring browserUserAgent;
    std::wstring browserVersion;
    std::wstring osVersion;
    std::wstring releaseChannel;
    int clientBuildNumber = 390000;
    int capabilities = 65;
    int connectTimeoutMs = 10000;
    int helloTimeoutMs = 10000;
    int readyTimeoutMs = 30000;
    int sendTimeoutMs = 10000;
    int closeTimeoutMs = 2000;
    int assetFetchTimeoutMs = 10000;
    std::wstring assetFetchUserAgent;
};

struct TrayNotification
{
    std::wstring title;
    std::wstring message;
    DWORD icon = NIIF_INFO;
};

struct RuntimeContext
{
    HWND hwnd = nullptr;
    NOTIFYICONDATAW nid = {};
    bool trayCreated = false;
    bool trayEnabled = false;
    bool acceptingNotifications = true;
    aip::ResidentShutdownState shutdown;
    std::mutex notificationMutex;
    std::deque<TrayNotification> pendingNotifications;
    std::thread worker;
    DWORD workerExitCode = 0;
};

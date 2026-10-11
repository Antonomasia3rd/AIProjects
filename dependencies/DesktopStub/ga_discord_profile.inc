// Passive advanced-profile transactions. No provider/transport/Startup entry
// point is called here. Both file mutexes are held in canonical path order.
#include "../DiscordRPC/profile_edit.h"

struct DiscordHostEditOptions {
    bool create = false, privateInput = false, optimistic = false;
    std::wstring createPath, expectedHost, expectedProfile;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    std::function<bool()> failHostWrite;
#endif
};
struct DiscordRawFileSnapshot { bool existed = false; std::vector<BYTE> bytes; std::wstring text; };
static bool CaptureDiscordRawFile(const std::wstring& path, DiscordRawFileSnapshot& value, std::wstring& error)
{
    value = {};
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
        error = L"Could not inspect a configuration file."; return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) || !aip::ReadWholeFileBytes(path, value.bytes, aip::kMaxIniFileBytes) ||
        !aip::DecodeTextBytes(value.bytes, value.text)) { error = L"Could not capture a complete configuration file."; return false; }
    value.existed = true; return true;
}
static bool StageDiscordUtf8Bytes(const std::wstring& text, std::size_t maximum,
    std::vector<BYTE>& bytes, std::wstring& error)
{
    bytes.clear(); std::string utf8;
    if (!aip::TryWideToUtf8(text, utf8) || maximum < 3 || utf8.size() > maximum - 3) {
        error = L"A staged configuration contains invalid Unicode or exceeds its byte limit (content not shown)."; return false;
    }
    bytes = {0xef, 0xbb, 0xbf}; bytes.insert(bytes.end(), utf8.begin(), utf8.end()); return true;
}
static bool RestoreDiscordRawFile(const std::wstring& path, const DiscordRawFileSnapshot& snapshot)
{
    std::vector<BYTE> current;
    if (snapshot.existed && aip::ReadWholeFileBytes(path, current, aip::kMaxIniFileBytes) && current == snapshot.bytes) return true;
    if (snapshot.existed) return WriteWholeFileBytes(path, snapshot.bytes);
    return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}
static bool DiscordSamePhysicalFile(const std::wstring& first, const std::wstring& second)
{
    if (IEquals(first, second)) return true;
    const auto open = [](const std::wstring& path) { return CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); };
    HANDLE a = open(first), b = open(second); BY_HANDLE_FILE_INFORMATION ia{}, ib{};
    const bool same = a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE && GetFileInformationByHandle(a, &ia) && GetFileInformationByHandle(b, &ib) &&
        ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber && ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
    if (a != INVALID_HANDLE_VALUE) CloseHandle(a); if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
    return same;
}
static bool WriteDiscordNewFile(const std::wstring& path, const std::vector<BYTE>& bytes)
{
    const auto temporary = MakeTempSiblingPath(path, L"discord-create");
    const bool written = WriteWholeFileBytesDirect(temporary, bytes);
    const bool published = written && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
    if (!published) DeleteFileW(temporary.c_str());
    return published;
}
static bool StageDiscordHostSettings(std::wstring& text, const std::vector<aip::IniSetting>& changes, std::wstring& error)
{
    for (const auto& change : changes) {
        auto value = change.value;
        if ((IEquals(change.section, L"Settings") && (IEquals(change.key, L"RunAtStartup") || IEquals(change.key, L"RunAtStartupPackaged"))) ||
            !NormalizeDesktopStubTypedSetting(change.section, change.key, value, error) ||
            !WriteContentProfileSetting(text, {change.section, change.key, value})) {
            if (error.empty()) error = L"A passive advanced-profile edit cannot apply Startup changes.";
            return false;
        }
    }
    const auto document = aip::ParseIniDocument(text);
    aip::content::Configuration cfg;
    return aip::content::ReadConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    }, cfg, error, ContentAsusEventsFromDocument(document));
}
static bool CommitDiscordHostProfile(const std::vector<aip::IniSetting>& hostSettings,
    const std::vector<aip::IniSetting>& profileSettings, const DiscordHostEditOptions& options, std::wstring& error)
{
    error.clear();
    const auto hostPath = aip::MakeAbsolutePath(g_iniPath);
    DiscordRawFileSnapshot initial;
    if (!CaptureDiscordRawFile(hostPath, initial, error)) return false;
    auto prospective = initial.existed ? initial.text : BuildInitialIniTemplate();
    if (!StageDiscordHostSettings(prospective, hostSettings, error)) return false;
    std::wstring reference;
    if (options.create) reference = options.createPath;
    else aip::ReadIniValueFromDoc(aip::ParseIniDocument(prospective), L"Discord", L"ProfilePath", reference);
    reference = TrimCopy(reference);
    std::wstring profilePath;
    if (reference.empty() || !aip::discord::ResolveProfileReference(reference, hostPath, profilePath, error)) {
        error = L"Choose an existing advanced Discord INI or explicitly create one before editing its options."; return false;
    }
    std::wstring hostIdentity, profileIdentity;
    if (!aip::named_objects::NormalizeFileIdentity(hostPath, hostIdentity) ||
        !aip::named_objects::NormalizeFileIdentity(profilePath, profileIdentity)) {
        error = L"Could not normalize the two configuration file identities."; return false;
    }
    if (hostIdentity == profileIdentity || DiscordSamePhysicalFile(hostPath, profilePath)) {
        error = L"The advanced profile must be a separate file from DesktopStub's INI."; return false;
    }
    if (options.create) for (const auto& setting : hostSettings) if (IEquals(setting.section, L"Discord") && IEquals(setting.key, L"ProfilePath")) {
        std::wstring explicitPath, explicitIdentity;
        if (!aip::discord::ResolveProfileReference(TrimCopy(setting.value), hostPath, explicitPath, error) ||
            !aip::named_objects::NormalizeFileIdentity(explicitPath, explicitIdentity) || explicitIdentity != profileIdentity) {
            error = L"An explicit profile binding must match the profile being created."; return false;
        }
    }
    const auto firstPath = hostIdentity < profileIdentity ? hostPath : profilePath;
    const auto secondPath = firstPath == hostPath ? profilePath : hostPath;
    aip::IniWriteMutexGuard first(firstPath, CurrentIniWriteLockWaitMs());
    if (!first.IsLocked()) { error = L"Could not lock the profile transaction."; return false; }
    aip::IniWriteMutexGuard second(secondPath, CurrentIniWriteLockWaitMs());
    if (!second.IsLocked()) { error = L"Could not lock the second profile transaction file."; return false; }
    DiscordRawFileSnapshot host, profile;
    if (!CaptureDiscordRawFile(hostPath, host, error) || !CaptureDiscordRawFile(profilePath, profile, error)) return false;
    if (profile.bytes.size() > 1048576) { error = L"The referenced profile exceeds its 1 MiB limit."; return false; }
    if (host.text != initial.text || host.existed != initial.existed ||
        (options.optimistic && (host.text != options.expectedHost || profile.text != options.expectedProfile))) {
        error = L"A configuration changed while the editor was open; reopen it and retry."; return false;
    }
    if (options.create ? profile.existed : !profile.existed) {
        error = options.create ? L"The profile already exists; creation never replaces it." : L"The advanced profile must already exist."; return false;
    }
    auto hostCandidate = host.existed ? host.text : BuildInitialIniTemplate();
    if (!StageDiscordHostSettings(hostCandidate, hostSettings, error)) return false;
    std::wstring profileBase = profile.text, profileCandidate;
    aip::discord::ProfileEditResult defaults, edited;
    if (options.create && !aip::discord::MakeDefaultProfileDocument(profileBase, defaults, error)) return false;
    aip::discord::ProfileEditPolicy policy;
    policy.allowPrivateSecretEdits = options.privateInput; policy.acknowledgeHostCadenceMapping = true;
    auto coherentProfileSettings = profileSettings;
    int explicitHostCadence = 0;
    for (const auto& setting : hostSettings) if (IEquals(setting.section, L"Discord") && IEquals(setting.key, L"RefreshSeconds"))
        aip::ParseIntValueInRange(setting.value, 1, 86400, explicitHostCadence);
    if (explicitHostCadence) {
        auto interval = std::find_if(coherentProfileSettings.begin(), coherentProfileSettings.end(), [](const auto& setting) {
            return IEquals(setting.section, L"general") && IEquals(setting.key, L"update_interval");
        });
        if (interval != coherentProfileSettings.end()) {
            int requested = 0;
            if (!aip::ParseIntValueInRange(interval->value, 1, 86400, requested) || requested != explicitHostCadence) {
                error = L"Discord.RefreshSeconds and general.update_interval must request the same cadence in one batch."; return false;
            }
        } else coherentProfileSettings.push_back({L"general", L"update_interval", std::to_wstring(explicitHostCadence)});
    }
    if (!aip::discord::ApplyProfileEditsToText(profileBase, coherentProfileSettings, policy, profileCandidate, edited, error)) return false;
    int cadence = edited.requiresHostCadenceApply ? edited.requestedRefreshSeconds : options.create ? defaults.requestedRefreshSeconds : 0;
    if (options.create && !WriteContentProfileSetting(hostCandidate, {L"Discord", L"ProfilePath", profilePath})) return false;
    // Conflicting explicit cadences were rejected; a normal host override also
    // updates the advanced preference, including creation's inherited default.
    if (cadence && !WriteContentProfileSetting(hostCandidate, {L"Discord", L"RefreshSeconds", std::to_wstring(cadence)})) return false;
    if (!StageDiscordHostSettings(hostCandidate, {}, error)) return false;
    std::vector<BYTE> profileBytes, hostBytes;
    if (!StageDiscordUtf8Bytes(profileCandidate, 1048576, profileBytes, error) ||
        !StageDiscordUtf8Bytes(hostCandidate, aip::kMaxIniFileBytes, hostBytes, error)) return false;
    const bool profileWritten = options.create ? WriteDiscordNewFile(profilePath, profileBytes) :
        (profileCandidate == profile.text || WriteWholeFileBytes(profilePath, profileBytes));
    if (!profileWritten) { error = L"The advanced profile could not be saved; the host configuration was not changed."; return false; }
    bool hostWritten = true;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if (options.failHostWrite && options.failHostWrite()) hostWritten = false;
    else
#endif
    hostWritten = hostCandidate == host.text && host.existed ? true : WriteWholeFileBytes(hostPath, hostBytes);
    if (!hostWritten) {
        const bool restoredProfile = RestoreDiscordRawFile(profilePath, profile);
        const bool restoredHost = RestoreDiscordRawFile(hostPath, host);
        error = L"The host save failed; both configuration files were rolled back.";
        if (!restoredProfile || !restoredHost) error = L"The host save failed and a configuration rollback also failed.";
        return false;
    }
    // Native cache replacement is memory-only; it never reconciles Startup.
    { std::lock_guard<std::mutex> lock(g_iniCacheMutex); g_iniCacheLoaded = false; }
    return true;
}

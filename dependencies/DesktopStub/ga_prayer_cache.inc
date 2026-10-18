// Private schedule persistence. Called only by the selected source's worker;
// configuration/render-only paths never read or create this file.
static constexpr std::size_t PrayerCacheMaximumBytes = 1024u * 1024u;
static std::wstring PrayerCachePath(const std::wstring& profile)
{
    if (profile.empty()) return {};
    const auto absolute = aip::MakeAbsolutePath(profile);
    return absolute.empty() ? std::wstring{} : absolute + L".prayer-cache";
}
static bool LoadPrayerCache(const std::wstring& profile, const aip::prayer::Configuration& cfg,
    aip::prayer::Data& data, std::wstring& error)
{
    error.clear();
    const auto path = PrayerCachePath(profile);
    if (path.empty() || cfg.city.empty() || cfg.country.empty()) return false;
    aip::IniWriteMutexGuard guard(path, 250);
    if (!guard.IsLocked()) { error = L"The saved prayer schedule is temporarily busy."; return false; }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) error = L"The saved prayer schedule could not be read.";
        return false;
    }
    struct CloseFile { HANDLE file; ~CloseFile() { CloseHandle(file); } } close{file};
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 1 ||
        static_cast<unsigned long long>(length.QuadPart) > PrayerCacheMaximumBytes) {
        error = L"The saved prayer schedule has an invalid size."; return false;
    }
    try {
        std::string bytes(static_cast<std::size_t>(length.QuadPart), '\0');
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD count = 0;
            const DWORD requested = static_cast<DWORD>((std::min)(bytes.size() - offset, std::size_t(65536)));
            if (!ReadFile(file, &bytes[offset], requested, &count, nullptr) || !count) {
                error = L"The saved prayer schedule could not be read completely."; return false;
            }
            offset += count;
        }
        char extra = 0; DWORD count = 0;
        if (!ReadFile(file, &extra, 1, &count, nullptr) || count) {
            error = L"The saved prayer schedule changed during reading."; return false;
        }
        aip::prayer::Data candidate;
        if (!aip::prayer::ParseCache(bytes, cfg, candidate, error)) return false;
        if (candidate.schedule.days.empty()) { error = L"The saved prayer schedule contains no days."; return false; }
        data = std::move(candidate); return true;
    } catch (...) { error = L"The saved prayer schedule could not be loaded."; return false; }
}
static bool SavePrayerCache(const std::wstring& profile, const aip::prayer::Configuration& cfg,
    const aip::prayer::Data& data, std::wstring& error)
{
    error.clear();
    const auto path = PrayerCachePath(profile);
    if (path.empty() || cfg.city.empty() || cfg.country.empty() || data.schedule.days.empty() ||
        data.configKey != aip::prayer::ConfigurationKey(cfg)) {
        error = L"The prayer schedule does not match the selected location/settings."; return false;
    }
    try {
        const auto bytes = aip::prayer::SerializeCache(data);
        if (bytes.empty() || bytes.size() > PrayerCacheMaximumBytes) {
            error = L"The prayer schedule could not be prepared for saving."; return false;
        }
        aip::prayer::Data validated;
        if (!aip::prayer::ParseCache(bytes, cfg, validated, error) || validated.schedule.days.empty()) {
            if (error.empty()) error = L"The prayer schedule is not valid for offline saving.";
            return false;
        }
        aip::IniWriteMutexGuard guard(path, 250);
        if (!guard.IsLocked()) { error = L"The saved prayer schedule is temporarily busy."; return false; }
        if (!WriteWholeFileBytes(path, std::vector<BYTE>(bytes.begin(), bytes.end()))) {
            error = L"The prayer schedule could not be saved for offline use."; return false;
        }
        return true;
    } catch (...) { error = L"The prayer schedule could not be saved for offline use."; return false; }
}

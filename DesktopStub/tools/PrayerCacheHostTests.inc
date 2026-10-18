// Private files and the real cache/codec boundaries only. Never selects a
// source, retrieves a location, contacts an API, or reads a user cache/profile.
static void TestPrayerCacheHost(const std::wstring& directory)
{
    namespace prayer = aip::prayer;
    const auto root = directory + L"\\prayer-cache-host";
    const bool owned = CreateDirectoryW(root.c_str(), nullptr) != FALSE;
    Check(owned, "prayer cache fixture creates a new private directory");
    if (!owned) return;
    const auto profile = root + L"\\fixture.ini", path = PrayerCachePath(profile);
    const auto directoryProfile = root + L"\\directory.ini", directoryTarget = PrayerCachePath(directoryProfile);
    bool ownsDirectoryTarget = false;
    struct Cleanup {
        const std::wstring& root; const std::wstring& path; const std::wstring& directoryTarget; const bool& ownsDirectoryTarget;
        ~Cleanup() { DeleteFileW(path.c_str()); if (ownsDirectoryTarget) RemoveDirectoryW(directoryTarget.c_str()); RemoveDirectoryW(root.c_str()); }
    } cleanup{root, path, directoryTarget, ownsDirectoryTarget};
    prayer::Configuration cfg; cfg.city = L"Fixture City"; cfg.country = L"ID"; cfg.method = 20; cfg.timeZone = L"Asia/Jakarta";
    const prayer::Date today{2026, 10, 18};
    const auto now = prayer::CivilDay(today) * 86400 + 8 * 3600;
    prayer::Data data; data.configKey = prayer::ConfigurationKey(cfg); data.lastSuccessUtc = now; data.nextRefreshUtc = now + 43200;
    for (unsigned index = 0; index < 2; ++index) {
        prayer::Day day; day.date = prayer::AddDays(today, static_cast<int>(index)); day.timeZone = cfg.timeZone;
        day.offsetMinutes = 420; day.fetchedAtUtc = now; day.hijriDate = L"Synthetic date";
        const int hours[]{5, 12, 15, 18, 19}; const auto start = prayer::CivilDay(day.date) * 86400;
        for (unsigned prayerIndex = 0; prayerIndex < 5; ++prayerIndex)
            day.prayers[prayerIndex] = {static_cast<prayer::PrayerId>(prayerIndex), start + hours[prayerIndex] * 3600 - 420 * 60, 420};
        day.sunrise = {prayer::PrayerId::Sunrise, start + 6 * 3600 - 420 * 60, 420}; day.hasSunrise = true;
        Check(prayer::ValidDay(day), "construct only a synthetic two-day schedule with ordered required prayers");
        data.schedule.days.push_back(std::move(day));
    }
    const auto encoded = prayer::SerializeCache(data);
    Check(!encoded.empty(), "portable codec accepts the synthetic cache fixture");
    if (encoded.empty()) return;
    const std::vector<BYTE> validBytes(encoded.begin(), encoded.end());
    auto sentinel = data; sentinel.configKey = L"unchanged sentinel"; sentinel.stale = true; sentinel.error = L"Keep this caller status";
    sentinel.lastSuccessUtc -= 60; sentinel.nextRefreshUtc += 60;
    const auto same = [](const prayer::Data& left, const prayer::Data& right) {
        return prayer::SerializeCache(left) == prayer::SerializeCache(right) && left.stale == right.stale && left.error == right.error;
    };
    std::wstring error = L"old error"; auto loaded = sentinel;
    Check(PrayerCachePath(L"").empty() && !LoadPrayerCache(L"", cfg, loaded, error) && same(loaded, sentinel),
        "an empty cache profile cannot turn into a relative filesystem target");
    Check(!LoadPrayerCache(profile, cfg, loaded, error) && error.empty() && same(loaded, sentinel),
        "a missing private cache returns an empty error and preserves caller data");
    Check(SavePrayerCache(profile, cfg, data, error) && error.empty() && GetFileAttributesW(profile.c_str()) == INVALID_FILE_ATTRIBUTES,
        "schedule saving creates only the cache and never creates its INI-like profile");
    loaded = sentinel;
    Check(LoadPrayerCache(profile, cfg, loaded, error) && error.empty() && same(loaded, data),
        "a valid private schedule round-trips through the real atomic cache helpers");
    std::vector<BYTE> before;
    Check(ReadWholeFileBytes(path, before) && before == validBytes, "saved private bytes equal the portable codec output");
    for (unsigned mismatch = 0; mismatch < 3; ++mismatch) {
        auto other = cfg;
        if (mismatch == 0) other.city = L"Other Fixture City";
        else if (mismatch == 1) other.method = 3;
        else other.timeZone = L"Asia/Bangkok";
        loaded = sentinel;
        Check(!LoadPrayerCache(profile, other, loaded, error) && !error.empty() && same(loaded, sentinel),
            "cache data for another city, method or requested time zone refuses without changing caller data");
    }
    std::vector<std::vector<BYTE>> invalid;
    invalid.push_back({}); invalid.push_back({'b', 'a', 'd', '\n'});
    invalid.emplace_back(validBytes.begin(), validBytes.begin() + validBytes.size() / 2);
    auto trailing = validBytes; trailing.push_back('x'); invalid.push_back(std::move(trailing));
    invalid.emplace_back(PrayerCacheMaximumBytes + 1, static_cast<BYTE>('x'));
    auto noDays = data; noDays.schedule.days.clear(); const auto emptySchedule = prayer::SerializeCache(noDays);
    if (!emptySchedule.empty()) invalid.emplace_back(emptySchedule.begin(), emptySchedule.end());
    for (const auto& bytes : invalid) {
        Check(WriteWholeFileBytes(path, bytes), "replace only the owned cache with bounded synthetic invalid bytes");
        loaded = sentinel;
        Check(!LoadPrayerCache(profile, cfg, loaded, error) && !error.empty() && same(loaded, sentinel),
            "empty, malformed, truncated, trailing, oversized or empty-schedule caches fail without changing the caller");
    }
    Check(SavePrayerCache(profile, cfg, data, error) && ReadWholeFileBytes(path, before), "restore a valid private cache before write-failure checks");
    auto changed = data; changed.nextRefreshUtc += 60;
    const auto held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(held != INVALID_HANDLE_VALUE, "hold only the owned cache without sharing deletion for atomic replacement failure");
    if (held != INVALID_HANDLE_VALUE) {
        std::vector<BYTE> after;
        Check(!SavePrayerCache(profile, cfg, changed, error) && !error.empty() && ReadWholeFileBytes(path, after) && after == before,
            "failed atomic replacement leaves the previous valid cache bytes intact");
        CloseHandle(held);
    }
    Check(SavePrayerCache(profile, cfg, changed, error) && ReadWholeFileBytes(path, before), "a later valid save succeeds after its owned reader releases the file");
    ownsDirectoryTarget = CreateDirectoryW(directoryTarget.c_str(), nullptr) != FALSE;
    Check(ownsDirectoryTarget, "create one owned directory at an alternate cache target");
    if (ownsDirectoryTarget) {
        std::vector<BYTE> after; loaded = sentinel;
        Check(!SavePrayerCache(directoryProfile, cfg, data, error) && !error.empty() && ReadWholeFileBytes(path, after) && after == before,
            "a directory target cannot be replaced and cannot affect another valid cache");
        Check(!LoadPrayerCache(directoryProfile, cfg, loaded, error) && !error.empty() && same(loaded, sentinel),
            "an unreadable directory cache target leaves the caller unchanged");
    }
    auto wrong = cfg; wrong.city = L"Wrong saved location"; std::vector<BYTE> after;
    Check(!SavePrayerCache(profile, wrong, data, error) && ReadWholeFileBytes(path, after) && after == before,
        "mismatched schedule identity cannot overwrite the last valid cache");
    Check(!SavePrayerCache(profile, cfg, noDays, error) && !error.empty() && ReadWholeFileBytes(path, after) && after == before,
        "an empty schedule cannot replace the previous valid cache");
    auto invalidDay = data; invalidDay.schedule.days.front().prayers[1].utcSeconds = invalidDay.schedule.days.front().prayers[0].utcSeconds;
    Check(!SavePrayerCache(profile, cfg, invalidDay, error) && !error.empty() && ReadWholeFileBytes(path, after) && after == before,
        "invalid prayer ordering is rejected before a cache write");
    std::atomic<bool> ready{false}, locked{false}, release{false};
    std::thread holder([&] {
        aip::IniWriteMutexGuard guard(path, 1000); locked = guard.IsLocked(); ready = true;
        const auto deadline = steady_clock::now() + milliseconds(5000);
        while (!release.load() && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(1));
    });
    struct ReleaseAndJoin {
        std::atomic<bool>& release; std::thread& thread;
        ~ReleaseAndJoin() { release = true; if (thread.joinable()) thread.join(); }
    } releaseAndJoin{release, holder};
    const auto waitDeadline = steady_clock::now() + milliseconds(1500);
    while (!ready.load() && steady_clock::now() < waitDeadline) std::this_thread::sleep_for(milliseconds(1));
    Check(ready.load() && locked.load(), "one owned thread acquires only the private cache mutex");
    if (ready.load() && locked.load()) {
        loaded = sentinel; const auto readStart = steady_clock::now();
        const bool read = LoadPrayerCache(profile, cfg, loaded, error);
        const auto readElapsed = duration_cast<milliseconds>(steady_clock::now() - readStart).count();
        Check(!read && !error.empty() && same(loaded, sentinel) && readElapsed < 2000,
            "a busy cache read returns within a bounded wait and leaves caller data unchanged");
        const auto writeStart = steady_clock::now(); const bool written = SavePrayerCache(profile, cfg, data, error);
        const auto writeElapsed = duration_cast<milliseconds>(steady_clock::now() - writeStart).count();
        Check(!written && !error.empty() && writeElapsed < 2000 && ReadWholeFileBytes(path, after) && after == before,
            "a busy cache save returns within a bounded wait and preserves the valid file");
    }
    release = true; if (holder.joinable()) holder.join();
    loaded = sentinel;
    Check(LoadPrayerCache(profile, cfg, loaded, error) && same(loaded, changed), "private cache loading recovers after the owned mutex is released");
}

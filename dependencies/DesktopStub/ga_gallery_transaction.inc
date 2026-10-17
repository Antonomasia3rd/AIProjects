// Included after gallery configuration helpers. Recovery never rewrites the
// INI: unrelated/account changes stay in the current document.
#ifdef DESKTOPSTUB_INERT_HARDWARE
static std::function<bool(const wchar_t*)> g_galleryTransactionTestEvent;
#endif
static bool GalleryTransactionEvent(const wchar_t* stage)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return !g_galleryTransactionTestEvent || g_galleryTransactionTestEvent(stage);
#else
    (void)stage; return true;
#endif
}
static bool CreateGalleryTransactionRecord(const std::wstring& profile, const aip::gallery::transaction::Record& record, std::wstring& error)
{
    std::string bytes; if (!aip::gallery::transaction::Serialize(record, bytes, error)) return false;
    const auto path = aip::gallery::transaction::Path(profile), temporary = MakeTempSiblingPath(path, L"gallery-transaction");
    if (!WriteWholeFileBytesDirect(temporary, std::vector<BYTE>(bytes.begin(), bytes.end()))) {
        DeleteFileW(temporary.c_str()); error = L"The gallery recovery record could not be durably written. Nothing was reset."; return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); error = L"An existing recovery record must be recovered before another gallery change."; return false;
    }
    return true;
}
// Caller holds the profile INI lock. The ledger lock spans comparison/write so
// recovery cannot replace a cooperating writer's newer ownership.
static bool RecoverLockedGalleryTransaction(const std::wstring& profile, const std::wstring& text, std::wstring& error)
{
    if (!GalleryTransactionPending(profile)) { error.clear(); return true; }
    const auto path = aip::gallery::transaction::Path(profile);
    std::vector<BYTE> bytes;
    aip::gallery::transaction::Record record;
    if (!aip::ReadWholeFileBytes(path, bytes, aip::gallery::transaction::MaxJournalBytes) ||
        !aip::gallery::transaction::Parse(std::string(bytes.begin(), bytes.end()), record, error)) {
        if (error.empty()) error = L"The gallery recovery record cannot be read. It was kept; gallery publication remains paused.";
        return false;
    }
    aip::gallery::Configuration cfg; std::string currentConfiguration;
    if (!ReadGalleryMenuText(text, cfg, error)) return false;
    ResolveGalleryConfigurationFolder(cfg, profile);
    if (!GalleryConfigurationRecoveryBytes(cfg, currentConfiguration, error)) return false;
    const auto ledgerPath = aip::gallery::runtime::LedgerPath(profile);
    aip::IniWriteMutexGuard lock(ledgerPath, CurrentIniWriteLockWaitMs());
    if (!lock.IsLocked()) { error = L"The gallery ledger is busy; retry recovery shortly."; return false; }
    aip::gallery::Ledger ledger; std::string currentLedger;
    if (ReadGalleryLedgerNative(ledgerPath, ledger, error) != aip::gallery::runtime::ReadResult::Ok ||
        !aip::gallery::SerializeLedger(ledger, currentLedger, error)) return false;
    const auto decision = aip::gallery::transaction::Decide(record, currentConfiguration, currentLedger, error);
    if (decision == aip::gallery::transaction::Recovery::Conflict) return false;
    if (decision == aip::gallery::transaction::Recovery::Restore &&
        (!GalleryTransactionEvent(L"before-restore") || !WriteWholeFileBytes(ledgerPath, std::vector<BYTE>(record.ledger.begin(), record.ledger.end())))) {
        error = L"The prior gallery ownership could not be restored. The recovery record was kept and publication remains paused."; return false;
    }
    if (!GalleryTransactionEvent(L"before-remove") || !DeleteFileW(path.c_str())) {
        error = L"Gallery files are consistent, but the recovery record could not be removed. Retry recovery before gallery publication resumes."; return false;
    }
    error.clear(); return true;
}
static bool RecoverPendingGalleryTransaction(std::wstring& error)
{
    const auto profile = GalleryProfilePath();
    if (!GalleryTransactionPending(profile)) { error.clear(); return true; }
    std::unique_lock<std::mutex> generation(g_generationMutex, std::try_to_lock);
    if (!generation.owns_lock()) { error = L"A tile update is finishing. Retry gallery recovery shortly."; return false; }
    SuspendGalleryPublication();
    bool recovered = false;
    {
        DesktopStubIniMutationGuard guard(CurrentIniWriteLockWaitMs()); std::wstring text;
        if (!guard.IsLocked() || !ReadIniFileForMutation(text)) error = L"The selected INI is unavailable; gallery recovery kept all files.";
        else if (GalleryProfilePath() != profile) error = L"The selected INI changed; gallery recovery kept all files.";
        else recovered = RecoverLockedGalleryTransaction(profile, text, error);
    }
    generation.unlock(); ResumeGalleryPublication(true); WakePollThread(); return recovered;
}

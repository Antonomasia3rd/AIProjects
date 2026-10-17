// Real atomic ledger/config writes use the host's private fixture directory;
// every Windows clear/confirmation is injected. No photographs or native pins.
static void TestGalleryUiFlowHost() {
    std::wstring original,error;Check(ReadIniFileForMutation(original),"resize flow captures private fixture INI");
    const auto profile=GalleryProfilePath(),path=aip::gallery::runtime::LedgerPath(profile);
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES){Check(false,"resize fixture refuses existing history");return;}
    aip::gallery::Configuration before;before.enabled=true;before.groupId=L"resize_fixture";before.folder=L"C:\\synthetic\\gallery";
    Check(AppendGalleryRows(before,before.folder,L"Photos",8),"resize fixture starts with eight configured photos tiles");
    auto text=original;std::vector<aip::IniSetting> fields;
    Check(StageGalleryInitialLayout(text,error)&&PrepareGalleryConfigurationSettings(before,fields,error)&&StageGalleryMenuSettings(text,fields,error)&&WriteContentFixture(text),"resize fixture writes only its private profile");
    const bool created=CreateGalleryLedgerForProfile(profile,before,L"before_resize",true,error);
    Check(created,"resize fixture creates private history");if(!created){WriteContentFixture(original);return;}
    aip::gallery::Ledger initial;galleryUiClearSucceeds=false;
    Check(ReadGalleryLedgerNative(path,initial,error)==aip::gallery::runtime::ReadResult::Ok,"resize fixture reads initial history");
    std::vector<std::wstring> images;for(int n=0;n<56;++n)images.push_back(L"synthetic_"+std::to_wstring(n));
    aip::gallery::OwnershipPlan assigned;
    Check(aip::gallery::ReconcileOwnership(before,images,initial,assigned,error)&&SaveGalleryLedgerNative(path,initial,assigned.ledger,error)==aip::gallery::runtime::SaveResult::Saved,"eight old pools own all synthetic images before expansion");
    auto expanded=before;Check(AppendGalleryRows(expanded,before.folder,L"Photos",20),"resize prepares twenty-eight tiles");
    const int oldDialogs=contentDialogs,oldResponse=contentDialogResponse;contentDialogResponse=IDYES;
    const auto savedEvent = g_galleryTransactionTestEvent;
    const auto edit = GalleryEdit(before);
    auto concurrent = before; concurrent.tiles.front().name = L"Concurrent renamed tile";
    auto concurrentText = text; std::vector<aip::IniSetting> concurrentSettings;
    Check(PrepareGalleryConfigurationSettings(concurrent, concurrentSettings, error) && StageGalleryMenuSettings(concurrentText, concurrentSettings, error) &&
        WriteContentFixture(concurrentText), "another private writer changes gallery settings while a dialog is open");
    galleryUiClearSucceeds = true; galleryUiClearedIds.clear();
    auto added = before; Check(AppendGalleryRows(added, L"C:\\synthetic\\another", L"Another", 3), "stale add draft has additional folder rows");
    Check(!SaveGalleryConfiguration(nullptr, added, false, &edit) && !SaveGalleryConfiguration(nullptr, expanded, true, &edit) && galleryUiClearedIds.empty(),
        "stale add and resize refuse before clearing queues or replacing another writer's gallery settings");
    auto moved = before; moved.tiles.front().folder = L"C:\\synthetic\\moved";
    Check(!SaveGalleryConfiguration(nullptr, moved, false, &edit), "a stale folder chooser cannot overwrite a newer gallery");
    GalleryEditContext differentProfile = edit; differentProfile.profile += L".other";
    Check(!SaveGalleryConfiguration(nullptr, moved, false, &differentProfile), "a dialog bound to another profile cannot write the selected profile");
    std::wstring currentText;
    Check(ReadIniFileForMutation(currentText) && currentText == concurrentText && !GalleryTransactionPending(profile),
        "stale dialog failures preserve exact newer text and create no recovery record");
    Check(WriteContentFixture(text), "restore the baseline private gallery after conflict fixtures");
    auto unrelated = text; Check(WriteContentProfileSetting(unrelated, {L"Notes.Resin", L"UID", L"800000001"}) && WriteContentFixture(unrelated),
        "an unrelated private account edit is made after capturing gallery expectations");
    Check(SaveGalleryMenuSettings(nullptr, {{L"Gallery", L"QueueSize", L"4"}}, &edit) &&
        IniReadS(L"Notes.Resin", L"UID", L"") == L"800000001", "expected-gallery guards allow and preserve unrelated account changes");
    Check(WriteContentFixture(text), "restore baseline private profile before transaction failure fixtures");
    galleryUiClearSucceeds = false;
    Check(!SaveGalleryConfiguration(nullptr,expanded,true),"failed logical queue clearing refuses redistribution");
    aip::gallery::Ledger preserved;std::wstring preservedIni;
    Check(ReadGalleryLedgerNative(path,preserved,error)==aip::gallery::runtime::ReadResult::Ok&&preserved.epochId==L"before_resize"&&preserved.images.size()==56&&ReadIniFileForMutation(preservedIni)&&preservedIni==text,"failed clear preserves every reservation and original configuration");
    galleryUiClearSucceeds=true;
    g_galleryTransactionTestEvent = [](const wchar_t* stage) { return std::wstring(stage) != L"before-ini"; };
    Check(!SaveGalleryConfiguration(nullptr, expanded, true, &edit), "final INI publication failure triggers gallery rollback");
    aip::gallery::Ledger recovered; std::string exactBefore, exactAfter;
    Check(aip::gallery::SerializeLedger(assigned.ledger, exactBefore, error) && ReadGalleryLedgerNative(path,recovered,error)==aip::gallery::runtime::ReadResult::Ok &&
        aip::gallery::SerializeLedger(recovered, exactAfter, error) && exactAfter == exactBefore && ReadIniFileForMutation(currentText) && currentText == text &&
        !GalleryTransactionPending(profile), "final INI failure restores every original owner, ordinal, pending window and exact epoch without overwriting INI");
    g_galleryTransactionTestEvent = [](const wchar_t* stage) { return std::wstring(stage) != L"after-journal"; };
    Check(!SaveGalleryConfiguration(nullptr, expanded, true, &edit) && GalleryTransactionPending(profile), "interruption immediately after durable intent leaves a recoverable record");
    g_galleryTransactionTestEvent = {};
    Check(RecoverPendingGalleryTransaction(error) && !GalleryTransactionPending(profile), "restart recovery cancels an unstarted change while preserving original ownership");
    g_galleryTransactionTestEvent = [](const wchar_t* stage) { return std::wstring(stage) != L"after-reset"; };
    Check(!SaveGalleryConfiguration(nullptr, expanded, true, &edit) && GalleryTransactionPending(profile), "interruption after reset retains durable rollback data");
    aip::gallery::runtime::Input blocked;
    Check(!CaptureGalleryConfiguration(blocked,error), "pending recovery fences all gallery capture and allocation before any provider runs");
    std::vector<BYTE> journalBytes;
    aip::gallery::transaction::Record journal;
    Check(aip::ReadWholeFileBytes(aip::gallery::transaction::Path(profile), journalBytes, aip::gallery::transaction::MaxJournalBytes) &&
        aip::gallery::transaction::Parse(std::string(journalBytes.begin(), journalBytes.end()),journal,error), "interrupted transaction has a complete bounded versioned journal");
    auto parsedJournal = journal;
    Check(!aip::gallery::transaction::Parse("AIPGALLERYTX1\n9999999999999999999999999\nx", parsedJournal, error) &&
        !aip::gallery::transaction::Parse(std::string(journalBytes.begin(), journalBytes.end()) + "trailing", parsedJournal, error),
        "malformed length and trailing journal bytes cannot be accepted as recovery history");
    g_galleryTransactionTestEvent = [](const wchar_t* stage) { return std::wstring(stage) != L"before-restore"; };
    Check(!RecoverPendingGalleryTransaction(error) && GalleryTransactionPending(profile), "failed rollback writes keep publication fenced and retain recovery data");
    g_galleryTransactionTestEvent = {};
    Check(RecoverPendingGalleryTransaction(error) && !GalleryTransactionPending(profile) && ReadGalleryLedgerNative(path,recovered,error)==aip::gallery::runtime::ReadResult::Ok &&
        aip::gallery::SerializeLedger(recovered,exactAfter,error) && exactAfter==exactBefore && ReadIniFileForMutation(currentText) && currentText==text,
        "a later recovery retry restores exact historical ownership and leaves the original INI untouched");
    g_galleryTransactionTestEvent = [](const wchar_t* stage) { return std::wstring(stage) != L"after-reset"; };
    Check(!SaveGalleryConfiguration(nullptr, expanded, true, &edit), "prepare a conflicting interrupted transaction fixture");
    Check(WriteContentFixture(concurrentText), "an external gallery edit after interruption is preserved");
    g_galleryTransactionTestEvent = {};
    Check(!RecoverPendingGalleryTransaction(error) && GalleryTransactionPending(profile) && ReadIniFileForMutation(currentText) && currentText==concurrentText,
        "recovery refuses ambiguous external gallery changes rather than guessing a replacement allocation");
    Check(WriteContentFixture(text) && RecoverPendingGalleryTransaction(error), "restoring the matching profile permits safe retry of the same recovery record");
    Check(SaveGalleryConfiguration(nullptr,expanded,true)&&galleryUiClearedIds.size()==8,"confirmed resize clears every historical destination before resetting");
    aip::gallery::Ledger reset;aip::gallery::Configuration saved;
    Check(ReadGalleryLedgerNative(path,reset,error)==aip::gallery::runtime::ReadResult::Ok&&reset.epochId!=L"before_resize"&&reset.images.empty()&&ReadGalleryConfiguration(saved,error)&&saved.tiles.size()==28&&saved.tiles.front().id==before.tiles.front().id,"successful resize keeps native identities and atomically publishes fresh allocation history");
    const auto afterEdit = GalleryEdit(saved); auto next = saved; Check(AppendGalleryRows(next, saved.folder, L"Photos", 1), "post-commit fixture prepares one more tile");
    g_galleryTransactionTestEvent = [](const wchar_t* stage) { return std::wstring(stage) != L"after-ini"; };
    Check(!SaveGalleryConfiguration(nullptr,next,true,&afterEdit)&&GalleryTransactionPending(profile), "interruption after INI commit leaves publication fenced until cleanup");
    g_galleryTransactionTestEvent = {};
    Check(RecoverPendingGalleryTransaction(error)&&!GalleryTransactionPending(profile)&&ReadGalleryConfiguration(saved,error)&&saved.tiles.size()==29,
        "startup recovery finishes a committed new configuration without reverting it or its reset ledger");
    const auto sameEdit = GalleryEdit(saved);
    g_galleryTransactionTestEvent = [](const wchar_t* stage) { return std::wstring(stage) != L"after-reset"; };
    Check(!SaveGalleryConfiguration(nullptr,saved,true,&sameEdit)&&GalleryTransactionPending(profile), "redistribution with unchanged settings can also be interrupted");
    g_galleryTransactionTestEvent = {};
    Check(RecoverPendingGalleryTransaction(error)&&!GalleryTransactionPending(profile), "identical before/after gallery settings finish the explicitly requested reset safely");
    g_galleryTransactionTestEvent = savedEvent;
    contentDialogs=oldDialogs;contentDialogResponse=oldResponse;galleryUiClearSucceeds=false;galleryUiClearedIds.clear();
    Check(DeleteFileW(path.c_str())&&WriteContentFixture(original),"resize flow removes only its owned private ledger and restores private INI");
}

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
    Check(!SaveGalleryConfiguration(nullptr,expanded,true),"failed logical queue clearing refuses redistribution");
    aip::gallery::Ledger preserved;std::wstring preservedIni;
    Check(ReadGalleryLedgerNative(path,preserved,error)==aip::gallery::runtime::ReadResult::Ok&&preserved.epochId==L"before_resize"&&preserved.images.size()==56&&ReadIniFileForMutation(preservedIni)&&preservedIni==text,"failed clear preserves every reservation and original configuration");
    galleryUiClearSucceeds=true;
    Check(SaveGalleryConfiguration(nullptr,expanded,true)&&galleryUiClearedIds.size()==8,"confirmed resize clears every historical destination before resetting");
    aip::gallery::Ledger reset;aip::gallery::Configuration saved;
    Check(ReadGalleryLedgerNative(path,reset,error)==aip::gallery::runtime::ReadResult::Ok&&reset.epochId!=L"before_resize"&&reset.images.empty()&&ReadGalleryConfiguration(saved,error)&&saved.tiles.size()==28&&saved.tiles.front().id==before.tiles.front().id,"successful resize keeps native identities and atomically publishes fresh allocation history");
    contentDialogs=oldDialogs;contentDialogResponse=oldResponse;galleryUiClearSucceeds=false;galleryUiClearedIds.clear();
    Check(DeleteFileW(path.c_str())&&WriteContentFixture(original),"resize flow removes only its owned private ledger and restores private INI");
}

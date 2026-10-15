// Explicit native file tests use only the host fixture's freshly-created
// private directory. They do not enable a gallery or call inventory/pin APIs.
static void TestGalleryLedgerHost(const std::wstring& directory)
{
    namespace gallery = aip::gallery;
    namespace runtime = aip::gallery::runtime;
    const auto profile = directory + L"\\native-gallery-ledger.ini";
    const auto path = runtime::LedgerPath(profile);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Check(false, "native gallery ledger fixture refuses a pre-existing path"); return;
    }
    gallery::Configuration configuration;
    configuration.enabled = true; configuration.groupId = L"NativeFixture";
    configuration.folder = directory; configuration.tiles = {{L"first", L"Photos", true}};
    std::wstring error;
    Check(!CreateGalleryLedgerForProfile(profile, configuration, L"epoch", false, error) &&
        GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES,
        "native ledger initialization requires explicit no-history confirmation");
    const bool created = CreateGalleryLedgerForProfile(profile, configuration, L"epoch", true, error);
    Check(created, "native ledger initialization creates one private ownership file");
    if (!created) return;
    struct OwnedLedgerCleanup {
        std::wstring path;
        ~OwnedLedgerCleanup() { DeleteFileW(path.c_str()); }
    } cleanup{path};
    Check(GetFileAttributesW(profile.c_str()) == INVALID_FILE_ATTRIBUTES,
        "native ownership initialization does not create or modify the INI itself");
    gallery::Ledger original;
    Check(ReadGalleryLedgerNative(path, original, error) == runtime::ReadResult::Ok &&
        original.groupId == configuration.groupId && original.images.empty(),
        "native ownership bytes parse back into the initial ledger");
    Check(!CreateGalleryLedgerForProfile(profile, configuration, L"replacement_epoch", true, error),
        "create-only native initialization cannot overwrite existing history");
    gallery::OwnershipPlan ownership;
    Check(gallery::ReconcileOwnership(configuration, {L"synthetic_photo_1", L"synthetic_photo_2"}, original, ownership, error) &&
        ownership.requiresLedgerCommit, "synthetic image ownership prepares a durable candidate");
    Check(SaveGalleryLedgerNative(path, original, ownership.ledger, error) == runtime::SaveResult::Saved,
        "native CAS atomically saves the expected ownership revision");
    gallery::Ledger saved;
    Check(ReadGalleryLedgerNative(path, saved, error) == runtime::ReadResult::Ok && saved.images.size() == 2,
        "native CAS ownership survives a fresh file read");
    if (saved.images.empty()) return; // Preserve a readable failure instead of dereferencing an invalid fixture result.
    Check(SaveGalleryLedgerNative(path, original, ownership.ledger, error) == runtime::SaveResult::Conflict,
        "stale native CAS cannot replace a newer ownership revision");
    auto reassigned = saved;
    ++reassigned.revision;
    reassigned.tiles.emplace(L"second", gallery::TileLedger{});
    reassigned.images.begin()->second.owner = L"second";
    Check(SaveGalleryLedgerNative(path, saved, reassigned, error) == runtime::SaveResult::Error,
        "native CAS rejects reassignment even when its candidate ledger is structurally valid");
    gallery::Ledger preserved;
    Check(ReadGalleryLedgerNative(path, preserved, error) == runtime::ReadResult::Ok &&
        preserved.revision == saved.revision && preserved.images.begin()->second.owner == L"first",
        "rejected native writes preserve the stored owner and revision");
    const std::vector<BYTE> corrupt{'n','o','t',' ','a',' ','l','e','d','g','e','r'};
    Check(WriteWholeFileBytes(path, corrupt), "fixture replaces only its owned ledger with synthetic corrupt bytes");
    Check(!CreateGalleryLedgerForProfile(profile, configuration, L"new_epoch", true, error),
        "native initialization also preserves corrupt existing history");
    std::vector<BYTE> remaining;
    Check(aip::ReadWholeFileBytes(path, remaining, gallery::MaxLedgerBytes) && remaining == corrupt,
        "failed initialization leaves corrupt bytes intact for explicit recovery");
}

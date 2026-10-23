// Chaseable metadata is separate from the process command line. Never infer
// which rotating image was clicked from the most recently submitted frame.
#include "../gallery_image_activation.h"
#include <winrt/Windows.ApplicationModel.Activation.h>
struct GalleryShownActivation {std::wstring tileId,arguments;};
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testGalleryShownActivation)(GalleryShownActivation&)=nullptr;
static bool (*g_testGalleryClickLoad)(const std::wstring&,std::wstring&)=nullptr;
static bool (*g_testGalleryClickSave)(const std::wstring&,const std::wstring&)=nullptr;
static bool (*g_testGalleryClickFile)(const aip::gallery::activation::Record&,const std::wstring&)=nullptr;
static bool (*g_testGalleryClickOpen)(const std::wstring&)=nullptr;
static bool (*g_testGalleryClickOriginal)(const std::wstring&,const std::wstring&,aip::gallery::InventoryFile&)=nullptr;
static unsigned g_testGalleryClickOwnerHashes=0;
#endif
static bool ReadGalleryShownActivation(GalleryShownActivation& output)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testGalleryShownActivation&&g_testGalleryShownActivation(output);
#else
    const HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);
    struct Cleanup {HRESULT result;~Cleanup(){if(SUCCEEDED(result))RoUninitialize();}} cleanup{initialized};
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
    try {
        using namespace winrt::Windows::ApplicationModel;
        using namespace winrt::Windows::ApplicationModel::Activation;
        const auto factory=aip::AcquireUncachedActivationFactory<AppInstance,IAppInstanceStatics>();
        const auto activated=factory.GetActivatedEventArgs();
        const auto launch=activated.try_as<LaunchActivatedEventArgs>();if(!launch)return false;
        output.tileId=launch.TileId().c_str();const auto info=launch.TileActivatedInfo();
        if(info){const auto shown=info.RecentlyShownNotifications();if(shown&&shown.Size())output.arguments=shown.GetAt(0).Arguments().c_str();}
        return !output.tileId.empty();
    }catch(...){return false;}
#endif
}
static std::wstring GalleryClickOwner(const std::wstring& profile)
{
    static std::mutex mutex;static std::wstring cachedProfile,cachedUser,cachedOwner;
    const auto user=CurrentTileRegistrationUserScope();if(user.empty())return {};
    std::lock_guard<std::mutex> lock(mutex);
    if(IEquals(profile,cachedProfile)&&user==cachedUser&&!cachedOwner.empty())return cachedOwner;
    std::wstring normalized,hash;
    if(!aip::named_objects::NormalizeFileIdentity(profile,normalized)||!aip::named_objects::StableIdentityHash(user+L"|"+normalized,hash))return {};
#ifdef DESKTOPSTUB_INERT_HARDWARE
    ++g_testGalleryClickOwnerHashes;
#endif
    cachedProfile=profile;cachedUser=user;cachedOwner=hash;return hash;
}
static aip::gallery::activation::Token GalleryClickToken(const aip::content::Snapshot& frame,const std::wstring& profile)
{
    return {GalleryClickOwner(profile),frame.galleryGroupId,frame.galleryTileId,frame.galleryEpoch,frame.galleryImageId};
}
static std::wstring GalleryClickArguments(const aip::content::Snapshot& frame,const std::wstring& profile)
{
    const auto token=aip::gallery::activation::Encode(GalleryClickToken(frame,profile));
    return token.empty()?std::wstring{}:L"--ini "+aip::QuoteCommandLineArg(profile)+L" --gallery-image "+aip::QuoteCommandLineArg(token);
}
static std::wstring GalleryClickRecordPath(const std::wstring& profile,const aip::gallery::activation::Token& token)
{
    if(!aip::gallery::activation::Valid(token)||token.owner!=GalleryClickOwner(profile))return {};
    std::wstring identity;
    if(!aip::named_objects::StableIdentityHash(aip::gallery::NativeTileId(token.group,token.tile),identity))return {};
    return profile+L".gallery-clicks\\"+token.owner+L"."+identity+L"."+token.image.substr(7)+L".txt";
}
static bool ReadGalleryClickRecord(const std::wstring& profile,const aip::gallery::activation::Token& token,aip::gallery::activation::Record& record)
{
    const auto path=GalleryClickRecordPath(profile,token);if(path.empty())return false;std::wstring text;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!g_testGalleryClickLoad||!g_testGalleryClickLoad(path,text))return false;
#else
    const auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    std::vector<BYTE> bytes;if(!aip::ReadWholeFileBytes(path,bytes,160004)||!DecodeTextBytes(bytes,text))return false;
#endif
    return aip::gallery::activation::Parse(text,token,record);
}
static bool WriteGalleryClickRecord(const std::wstring& profile,const aip::gallery::activation::Record& record)
{
    const auto path=GalleryClickRecordPath(profile,record.token),text=aip::gallery::activation::Serialize(record);
    if(path.empty()||text.empty())return false;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testGalleryClickSave&&g_testGalleryClickSave(path,text);
#else
    const auto directory=ParentDirectoryOfPath(path);const auto attributes=GetFileAttributesW(directory.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    if(!EnsureDirectoryExists(directory))return false;
    aip::IniWriteMutexGuard guard(path,250);return guard.IsLocked()&&WriteUtf8BomTextFile(path,text);
#endif
}
static bool PrepareGalleryImageActivation(const aip::content::Snapshot& frame)
{
    if(!frame.galleryFrame||frame.galleryImageId.empty()||g_commandLine.renderOnly||g_commandLine.configureOnly)return false;
    std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=g_iniPath;}
#ifndef DESKTOPSTUB_INERT_HARDWARE
    if(!g_galleryRuntime.IsProfile(profile))return false;
#endif
    aip::gallery::activation::Record record;record.token=GalleryClickToken(frame,profile);
    if(!aip::gallery::activation::Valid(record.token))return false;
    aip::gallery::InventoryFile source;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!g_testGalleryClickOriginal||!g_testGalleryClickOriginal(frame.galleryTileId,frame.galleryImageId,source))return false;
#else
    if(!g_galleryRuntime.OriginalImageSource(frame.galleryTileId,frame.galleryImageId,source))return false;
#endif
    record.path=source.canonicalPath;record.fileIdentity=source.fileIdentity;record.bytes=source.size;record.modified=source.modified;
    aip::gallery::activation::Record existing;
    if(ReadGalleryClickRecord(profile,record.token,existing)&&existing.path==record.path&&existing.fileIdentity==record.fileIdentity&&
        existing.bytes==record.bytes&&existing.modified==record.modified)return true;
    return WriteGalleryClickRecord(profile,record);
}
static bool GalleryClickSourceMatches(const aip::gallery::activation::Record& record,const std::wstring& folder)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testGalleryClickFile&&g_testGalleryClickFile(record,folder);
#else
    if(!aip::gallery::activation::PhotoPath(record.path))return false;
    HANDLE file=CreateFileW(record.path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};aip::gallery::InventoryFile current;
    const bool matched=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
        aip::gallery::inventory_native::Metadata(file,current);CloseHandle(file);if(!matched)return false;
    auto root=aip::gallery::inventory_native::DirectoryPath(folder);if(root.empty())return false;if(root.back()!=L'\\')root+=L'\\';
    return current.canonicalPath.size()>root.size()&&CompareStringOrdinal(current.canonicalPath.c_str(),static_cast<int>(root.size()),root.c_str(),static_cast<int>(root.size()),TRUE)==CSTR_EQUAL&&
        IEquals(current.canonicalPath,record.path)&&current.fileIdentity==record.fileIdentity&&current.size==record.bytes&&current.modified==record.modified;
#endif
}
static bool ResolveGalleryImageToken(const std::wstring& encoded,const std::wstring& nativeTileId,std::wstring& path,std::wstring& error)
{
    using namespace aip::gallery;activation::Token token;
    if(!activation::Decode(encoded,token)||(!nativeTileId.empty()&&NativeTileId(token.group,token.tile)!=nativeTileId)) {error=L"The clicked photo identity is invalid.";return false;}
    std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=g_iniPath;}
    if(token.owner!=GalleryClickOwner(profile)){error=L"This clicked photo belongs to a different user or settings profile.";return false;}
    std::wstring issue;const auto view=CaptureGalleryConfigurationView(issue,true);
    if(!view||view->configuration.groupId!=token.group){error=L"The clicked photo's gallery is no longer configured.";return false;}
    const auto tile=std::find_if(view->configuration.tiles.begin(),view->configuration.tiles.end(),[&](const auto& row){return row.id==token.tile;});
    if(tile==view->configuration.tiles.end()){error=L"The clicked photo's tile was removed.";return false;}
    Ledger ledger;if(ReadGalleryLedgerNative(runtime::LedgerPath(profile),ledger,issue)!=runtime::ReadResult::Ok||!ValidateLedger(ledger,issue)||!activation::Owned(token,ledger)){
        error=L"The clicked photo is no longer owned by this tile.";return false;
    }
    activation::Record record;auto folder=EffectiveFolder(view->configuration,*tile);
    if(PathIsRelativeW(folder.c_str()))folder=ParentDirectoryOfPath(profile)+L"\\"+folder;folder=aip::MakeAbsolutePath(folder);
    if(!ReadGalleryClickRecord(profile,token,record)||!GalleryClickSourceMatches(record,folder)){
        error=L"The original photo changed, moved or is unavailable. Its gallery folder can still be opened.";return false;
    }
    path=record.path;error.clear();return true;
}
static bool OpenResolvedGalleryImage(const std::wstring& path)
{
    if(!aip::gallery::activation::PhotoPath(path))return false;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testGalleryClickOpen&&g_testGalleryClickOpen(path);
#else
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
#endif
}
static bool ReadGalleryClickPayload(const std::wstring& arguments,const std::wstring& nativeTileId,std::wstring& token)
{
    int count=0;auto argv=CommandLineToArgvW(arguments.c_str(),&count);std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=g_iniPath;}
    const bool matched=argv&&count==4&&IEquals(argv[0],L"--ini")&&IEquals(argv[2],L"--gallery-image")&&
        IEquals(aip::MakeAbsolutePath(argv[1]),aip::MakeAbsolutePath(profile));
    bool valid=false;
    if(matched){aip::gallery::activation::Token parsed;valid=aip::gallery::activation::Decode(argv[3],parsed)&&parsed.owner==GalleryClickOwner(profile)&&
        aip::gallery::NativeTileId(parsed.group,parsed.tile)==nativeTileId;if(valid)token=argv[3];}
    if(argv)LocalFree(argv);return valid;
}
static void NotifyGalleryImageFallback()
{
    if(!IniReadI(L"Settings",L"NotificationsEnabled",1))return;
    NotifyUserOperation(L"gallery-image-fallback",IniReadS(L"Strings",L"NotificationTitle",L"DesktopStub"),
        IniReadS(L"Strings",L"NotificationGalleryImageFallback",L"Windows did not provide an exact available photo for this click. Opening the gallery folder instead."),NIIF_INFO,true);
}

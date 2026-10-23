// Read-only preset preflight must model the exact factory before any creation.
static void TestFreshPresetPreview(const std::wstring& directory){
    const auto oldPath=g_iniPath,oldCommandText=contentTestCommandLine;const auto oldCommand=g_commandLine;
    const auto path=directory+L"\\fresh-preset-preview-"+std::to_wstring(GetTickCount64())+L".ini";
    g_iniPath=path;{std::lock_guard<std::mutex> lock(g_iniCacheMutex);g_iniCacheLoaded=false;g_iniCacheDoc.clear();}
    std::wstring error;std::vector<BYTE> before,after;
    for(const auto* preset:{L"Wallpaper",L"WallpaperRSS"}){
        g_commandLine={};g_commandLine.configureOnly=true;g_commandLine.presetName=preset;
        if(g_commandLine.presetName==L"WallpaperRSS")g_commandLine.settings.push_back({L"RssFeed",L"FeedUrl",L"https://preset-fixture.invalid/feed.atom"});
        Check(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES&&!ValidateDesktopStubContentProfile(true,error)&&
            error.find(L"Add tile")!=std::wstring::npos&&!g_commandLine.presetPreviewLoaded,
            "missing-profile preset preview sees independent factory and returns add-tile guidance before creation");
        Check(RunRenderOnlyCommand()==2&&GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES,
            "fresh offline Wallpaper/RSS preset rejects atomically without creating an INI");
    }
    Check(WriteWholeFileBytes(path,{}),"create zero-byte private preset target");
    g_commandLine={};g_commandLine.configureOnly=true;g_commandLine.presetName=L"WallpaperRSS";
    g_commandLine.settings.push_back({L"RssFeed",L"FeedUrl",L"https://preset-fixture.invalid/feed.atom"});
    Check(!ValidateDesktopStubContentProfile(true,error)&&error.find(L"Add tile")!=std::wstring::npos&&
        RunRenderOnlyCommand()==2&&ReadWholeFileBytes(path,after)&&after.empty(),
        "zero-byte target uses independent factory only for preview and remains exactly empty after rejection");
    const auto typed=BuildInitialIniTemplate();
    Check(WriteUtf8BomTextFile(path,typed)&&ReadWholeFileBytes(path,before),"write reviewed typed factory to private preset target");
    {std::lock_guard<std::mutex> lock(g_iniCacheMutex);g_iniCacheLoaded=false;g_iniCacheDoc.clear();}
    Check(!ValidateDesktopStubContentProfile(true,error)&&error.find(L"Add tile")!=std::wstring::npos&&RunRenderOnlyCommand()==2&&
        ReadWholeFileBytes(path,after)&&before==after,"existing independent factory rejects a legacy RSS preset with exact byte preservation");
    Check(WriteWholeFileBytes(path,{0xef,0xbb,0xbf}),"create BOM-only existing legacy preset target");
    g_commandLine={};g_commandLine.presetName=L"Wallpaper";
    Check(PrepareCommandLinePresetPreview(error)&&g_commandLine.presetPreviewLoaded&&ReadWholeFileBytes(path,after)&&
        after==std::vector<BYTE>({0xef,0xbb,0xbf}),
        "BOM-only existing document stays legacy exactly like byte-based factory initialization and is not rewritten by preview");
    DeleteFileW(path.c_str());g_iniPath=oldPath;g_commandLine=oldCommand;contentTestCommandLine=oldCommandText;
    {std::lock_guard<std::mutex> lock(g_iniCacheMutex);g_iniCacheLoaded=false;g_iniCacheDoc.clear();}
}

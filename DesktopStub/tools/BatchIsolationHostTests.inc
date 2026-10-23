// Actual worker batch coordinator with injected generators. No rendering,
// package deployment, notification API, account/provider or real image is used.
static void TestBatchIsolationHost()
{
    using aip::content::Snapshot;
    const auto oldLogging=g_logging.exchange(false),oldNotify=g_notifyOnFailure.exchange(false);
    const auto oldFailures=galleryHostFailures;
    const auto oldShutdown=g_shutdownState.IsRequested();g_shutdownState.Cancel();
    const auto oldMode=g_liveTileUpdateMode.load(),oldRelaunch=g_relaunchAfterExit.load();
    const auto oldPins=CachedSecondaryTileIds();
    const auto frame=[](const wchar_t* owner,const wchar_t* id,const wchar_t* mode){
        Snapshot value;value.active=true;value.name=owner;value.secondaryTileId=id;value.secondaryFrameKey=owner;
        value.backgroundPath=L"Synthetic batch sentinel; never opened";value.policy.included=true;value.policy.owner=owner;
        value.policy.values[L"settings.experimentallivetileupdate"]=mode;return value;
    };
    const auto desktop=frame(L"desktop",L"",L"Registration"),clock=frame(L"clock",L"AipSource_clock",L"LiveTile"),
        calendar=frame(L"calendar",L"AipSource_calendar",L"LiveTile");
    std::vector<std::wstring> called;
    const auto mixed=RunFrozenGenerationBatch({desktop,clock,calendar},[&](const Snapshot& value){called.push_back(value.name);return value.name!=L"desktop";});
    Check(!mixed.success&&mixed.attempted==3&&mixed.completed==2&&called==std::vector<std::wstring>{L"desktop",L"clock",L"calendar"},
        "failed primary Registration does not starve independent Clock and Calendar Live Tile generation");
    Check(g_liveTileUpdateMode==oldMode&&g_relaunchAfterExit==oldRelaunch&&CachedSecondaryTileIds()==oldPins&&desktop.secondaryTileId.empty(),
        "mixed delivery batch cannot switch host mode, request relaunch or alter pins/primary destination");
    auto nativePrimary=desktop;nativePrimary.policy.values[L"settings.experimentallivetileupdate"]=L"LiveTile";
    Check(FrozenGenerationDestination(desktop)!=FrozenGenerationDestination(nativePrimary),
        "Registration owner and native primary remain separate delivery destinations");
    auto otherStatic=desktop;otherStatic.policy.owner=L"other-static";
    Check(FrozenGenerationDestination(desktop)!=FrozenGenerationDestination(otherStatic),
        "different static owners do not share failure suppression even with empty notification IDs");
    auto slots=GallerySyntheticWindow(L"Synthetic queue sentinel; never opened",L"failed-window");
    galleryHostFailures.clear();called.clear();
    auto queued=slots;queued.push_back(clock);
    const auto queuedResult=RunFrozenGenerationBatch(queued,[&](const Snapshot& value){
        called.push_back(value.secondaryTileId);GalleryGenerationGuard guard(&value,false);
        if(value.galleryFrame)return false;guard.delivered=true;return true;
    });
    Check(!queuedResult.success&&queuedResult.attempted==2&&queuedResult.completed==1&&called.size()==2&&called.back()==L"AipSource_clock",
        "failed reset-bearing queue skips its later slots while another destination still generates");
    Check(galleryHostFailures.size()==5&&slots[0].nativeQueueReset&&slots[0].gallerySlotCount==5,
        "attempted failed slot and every suppressed gallery receipt retain failure bookkeeping without mutating frozen reset metadata");
    called.clear();
    const auto exception=RunFrozenGenerationBatch({desktop,clock,calendar},[&](const Snapshot& value){
        called.push_back(value.name);if(value.name==L"desktop")throw std::runtime_error("Synthetic isolated deployment failure");return true;
    });
    Check(!exception.success&&exception.attempted==3&&exception.completed==2&&called.size()==3,
        "exception in one destination is contained and cannot prevent other tile destinations");
    galleryHostFailures.clear();called.clear();
    auto interruptedFrames=std::vector<Snapshot>{clock};interruptedFrames.insert(interruptedFrames.end(),slots.begin(),slots.end());
    const auto interrupted=RunFrozenGenerationBatch(interruptedFrames,[&](const Snapshot& value){called.push_back(value.name);g_shutdownState.Request();return true;});
    Check(!interrupted.success&&interrupted.attempted==1&&interrupted.completed==1&&called.size()==1&&galleryHostFailures.size()==5,
        "shutdown still aborts all remaining work and retires every unattempted gallery receipt");
    g_shutdownState.Cancel();
    const auto empty=RunFrozenGenerationBatch({},[](const Snapshot&){return false;});
    Check(empty.success&&empty.attempted==0&&empty.completed==0,"empty batch is inert");
    galleryHostFailures=oldFailures;g_logging=oldLogging;g_notifyOnFailure=oldNotify;
    if(oldShutdown)g_shutdownState.Request();
}

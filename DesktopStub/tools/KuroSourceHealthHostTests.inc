// Real typed settings, menu commands, WuWa composition, notification transition
// gates and semantic XML; all account responses and clocks are injected.
static void TestKuroSourceHealthHost()
{
    using namespace aip::content;std::wstring original,error;ReadIniFileForMutation(original);
    const auto enabled=g_notificationsEnabled.load();g_notificationsEnabled=false;
    const std::wstring text=L"[Tiles]\r\nInstances=health-left,health-right\r\n[Tile.health-left]\r\nType=WuWa\r\nName=Left fixture\r\nKuro.GameToken=SYNTHETIC-HEALTH-TOKEN\r\nKuro.PlayerId=12345\r\nKuro.Region=Europe\r\n"
        L"[Tile.health-right]\r\nType=WuWa\r\nSourceHealth.PopupFailureAttempts=2\r\nSourceHealth.TileFailureMode=Off\r\n";
    Check(WriteContentFixture(text),"health fixture owns only the host's private INI");TileInstanceConfiguration cfg;
    Check(ReadTileInstanceMenuConfiguration(cfg,error)&&cfg.tiles.size()==2&&cfg.tiles[0].sourceHealth.popupFailureAttempts==5&&cfg.tiles[1].sourceHealth.popupFailureAttempts==2&&cfg.tiles[1].sourceHealth.tileFailureMode==L"Off","WuWa source health defaults and overrides stay per tile");
    if(cfg.tiles.size()!=2){WriteContentFixture(original);g_notificationsEnabled=enabled;return;}
    ResetTileInstanceMenuCommands();HMENU menu=CreatePopupMenu();AppendTileInstanceDetails(menu,cfg.tiles[0]);
    for(const auto& field:aip::source_health::Settings){const std::wstring expected=L"SourceHealth."+std::wstring(field.key);
        Check(std::any_of(g_tileInstanceMenuCommands.begin(),g_tileInstanceMenuCommands.end(),[&](const auto& item){return item.second.id==cfg.tiles[0].id&&item.second.key==expected;}),"each reusable source policy field is reachable in the actual tray/Properties menu model");}
    UINT toggle=0;for(const auto& item:g_tileInstanceMenuCommands)if(item.second.id==cfg.tiles[0].id&&item.second.key==L"SourceHealth.PopupFailureEnabled")toggle=item.first;
    Check(toggle&&DispatchTileInstanceCommand(nullptr,toggle)&&ReadTileInstanceMenuConfiguration(cfg,error)&&!cfg.tiles[0].sourceHealth.popupFailureEnabled&&cfg.tiles[1].sourceHealth.popupFailureEnabled,"real menu toggle changes only the selected tile's popup policy");DestroyMenu(menu);
    Check(SaveTileInstanceSettings(nullptr,{{L"Tile.health-left",L"SourceHealth.TileFailureAttempts",L"2"},{L"Tile.health-left",L"SourceHealth.PopupFailureAttempts",L"3"},{L"Tile.health-left",L"SourceHealth.PopupFailureEnabled",L"1"}},L"health-left")&&ReadTileInstanceMenuConfiguration(cfg,error),"typed generic CLI/INI/GUI policy assignments share atomic validation");
    std::wstring before,after;ReadIniFileForMutation(before);
    Check(!SaveTileInstanceSettings(nullptr,{{L"Tile.health-left",L"SourceHealth.TileFailureAttempts",L"-1"}},L"health-left")&&ReadIniFileForMutation(after)&&after==before,"invalid source-health write cannot partially change the profile");
    TileInstanceProviderState state;state.type=TileInstanceType::Wuwa;state.kuro=std::make_unique<ContentKuroProvider>();std::atomic<std::uint64_t> now{0};state.kuro->testNow=[&]{return now.load();};
    bool offline=false;unsigned calls=0;state.kuro->testTransport=[&](const auto& request,auto& response,auto&){++calls;if(offline)return false;
        const auto details=L"{\"Base\":{\"Id\":12345,\"Name\":\"Rover\",\"Energy\":120,\"MaxEnergy\":240,\"StoreEnergy\":20,\"MaxStoreEnergy\":480}}";
        const auto body=request.url.find(L"oauth/code")!=std::wstring::npos?std::string("{\"codes\":0,\"oauthCode\":\"FIXTURE-CODE\"}"):std::string("{\"code\":0,\"data\":{\"Europe\":")+aip::JsonEscape(details)+"}}";
        response.statusCode=200;response.body.assign(body.begin(),body.end());return true;};
    SmtcMediaSnapshot media;const auto healthy=ComposeTileInstance(cfg.tiles[0],state,media,true,false);
    Check(healthy.detailRows.size()==2&&healthy.detailRows[0].primary==L"Waveplates","healthy WuWa layout retains original resource rows without stale-age clutter");
    offline=true;now=300000;state.forceRefresh=true;const auto stale=ComposeTileInstance(cfg.tiles[0],state,media,true,false);state.forceRefresh=false;
    Check(stale.detailRows.size()==3&&stale.detailRows.front().primary.empty()&&stale.detailRows.front().secondary==L"Refreshed 5m ago"&&stale.detailRows[1].secondary.find(L"120/240")!=std::wstring::npos,"first failed composition keeps source data with a subtle last-success age row");
    Check(stale.detailRows.front().secondary.find(L"Refresh delayed")==std::wstring::npos&&stale.sourceDiagnostic.find(L"last successful")!=std::wstring::npos,"quiet tile retry still exposes a separately labeled detailed diagnostic");
    const auto requests=calls;now=359999;const auto sameBucket=ComposeTileInstance(cfg.tiles[0],state,media,false,false);
    Check(calls==requests&&sameBucket.detailRows.front().secondary==stale.detailRows.front().secondary&&ContentSnapshotKey(sameBucket)==ContentSnapshotKey(stale),"same-minute stale polling neither refetches nor churns the rendered content key");
    now=360000;const auto nextMinute=ComposeTileInstance(cfg.tiles[0],state,media,false,false);
    Check(calls==requests&&nextMinute.detailRows.front().secondary==L"Refreshed 6m ago"&&ContentSnapshotKey(nextMinute)!=ContentSnapshotKey(stale),"an age bucket crossing invalidates the real publication snapshot without incrementing attempts");
    state.forceRefresh=true;const auto delayed=ComposeTileInstance(cfg.tiles[0],state,media,true,false);state.forceRefresh=false;
    Check(delayed.detailRows.front().secondary.find(L"Refresh delayed")!=std::wstring::npos&&!state.kuro->ShouldNotifyFailure(cfg.tiles[0].sourceHealth),"tile error can appear on the second failure while popup waits for the third");
    std::wstring xml;Check(AppendGameTileBinding(xml,L"TileWide",310,150,delayed,L"")&&xml.find(L"Refreshed")!=std::wstring::npos&&xml.find(L"120/240")!=std::wstring::npos&&xml.find(L"SYNTHETIC-HEALTH-TOKEN")==std::wstring::npos,"actual semantic game binding contains stale age and retained values without account tokens");
    state.forceRefresh=true;ComposeTileInstance(cfg.tiles[0],state,media,true,false);state.forceRefresh=false;
    Check(state.kuro->ShouldNotifyFailure(cfg.tiles[0].sourceHealth)&&state.notificationTransitions.Observe(aip::notifications::SourceState::Healthy)==aip::notifications::SourceNotice::Recovered,"third completed failure opens the independent coalesced popup transition");
    auto muted=cfg.tiles[0];muted.sourceHealth.popupFailureEnabled=false;ComposeTileInstance(muted,state,media,false,false);
    Check(!state.kuro->ShouldNotifyFailure(muted.sourceHealth)&&state.kuro->PopupFailureReached(muted.sourceHealth)&&state.notificationTransitions.Observe(aip::notifications::SourceState::Healthy)==aip::notifications::SourceNotice::Recovered,"failure popup can be disabled while the independent recovery transition remains available");
    offline=false;now=420000;state.forceRefresh=true;const auto recovered=ComposeTileInstance(cfg.tiles[0],state,media,true,false);state.forceRefresh=false;
    Check(recovered.detailRows.size()==2&&state.kuro->HealthState().consecutiveFailures==0&&!state.kuro->ShouldNotifyFailure(cfg.tiles[0].sourceHealth),"successful source recovery removes stale/error rows and resets health gates");
    state.Stop();g_notificationsEnabled=enabled;Check(WriteContentFixture(original),"health fixture restores the private profile after stopping its injected worker");
}

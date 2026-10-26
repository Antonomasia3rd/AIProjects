// Independent tile demand, provider state and immutable publication. A tile's
// settings never replace another tile's provider key or refresh deadline.
#include "../source_notification_state.h"
static bool EnsureTileInstanceProfile(std::wstring& error)
{
    return MutateDesktopStubIniWithWait([&](std::wstring& text) {
        const auto document = aip::ParseIniDocument(text);
        const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
        };
        aip::content::TileInstanceMigrationPlan plan;
        if (!aip::content::PlanLegacyTileInstanceMigration(read, plan, error)) return false;
        if(plan.legacyModeRequired){
            static std::wstring reported;
            const auto issue=g_iniPath+L"|"+plan.legacyReason;
            if(reported!=issue){Log(L"Using saved legacy contents without conversion: %ls",plan.legacyReason.c_str());reported=issue;}
            return true;
        }
        if (!read(L"Gallery",L"GroupId",L"").empty()) {
            aip::gallery::Configuration gallery;
            if (!aip::gallery::ReadConfiguration(read,gallery,error)) return false;
            for (std::size_t index=0;index<gallery.tiles.size();++index) {
                const auto section=L"Gallery.Tile."+std::to_wstring(index+1);
                for (const auto& field:aip::content::TilePolicyFields) {
                    if (aip::IniHasKeyInDoc(document,section.c_str(),field.key)) continue;
                    auto value=read(field.section,field.setting,field.fallback);
                    // Galleries previously used their group's fitting mode,
                    // independently of the desktop wallpaper's fitting switch.
                    if (std::wstring(field.key)==L"Wallpaper.Fit") value=L"Auto";
                    if (std::wstring(field.key)==L"Wallpaper.DisableFitting") value=L"0";
                    if (!aip::content::NormalizeTilePolicySetting(field.key,value,error)) return false;
                    plan.settings.push_back({section,field.key,std::move(value)});plan.changed=true;
                }
            }
        }
        if (!plan.changed) return true;
        auto candidate = text;
        for (const auto& setting : plan.settings)
            if (!aip::WriteIniValueToText(candidate, setting.section.c_str(), setting.key.c_str(), setting.value)) {
                error = L"Could not stage the tile configuration."; return false;
            }
        const auto staged = aip::ParseIniDocument(candidate);
        aip::content::TileInstanceConfiguration validated;
        if (!ValidateTileInstanceHostConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(staged, section, key, value) ? value : std::wstring(fallback);
        }, validated, error)) return false;
        // This immutable pre-conversion backup contains the exact original
        // account/configuration bytes. Never overwrite a previous backup.
        const auto backup = g_iniPath + L".before-tile-instances.bak";
        const auto attributes = GetFileAttributesW(backup.c_str());
        std::wstring priorBackup;
        if (attributes != INVALID_FILE_ATTRIBUTES && ((attributes & FILE_ATTRIBUTE_DIRECTORY) || !ReadTextFile(backup, priorBackup))) {
            error = L"The pre-conversion backup path is not a readable file."; return false;
        }
        if (attributes == INVALID_FILE_ATTRIBUTES && !CopyFileW(g_iniPath.c_str(), backup.c_str(), TRUE)) {
            error = L"Could not save the pre-conversion profile backup."; return false;
        }
        text = std::move(candidate); error.clear(); return true;
    }, CurrentIniWriteLockWaitMs());
}
struct TileInstanceProviderState {
    aip::content::TileInstanceType type = aip::content::TileInstanceType::Wallpaper;
    bool forceRefresh = false;
    aip::notifications::SourceTransitions notificationTransitions;
    std::unique_ptr<ContentRssProvider> rss;
    std::unique_ptr<ContentPrayerProvider> prayer;
    std::unique_ptr<ContentNotesProvider> notes;
    std::unique_ptr<ContentDiscordProvider> discord;
    std::unique_ptr<ContentDateTimeProvider> datetime;
    std::unique_ptr<ContentGoogleCalendarProvider> googleCalendar;
    std::unique_ptr<ContentGmailProvider> gmail;
    std::unique_ptr<ContentBatteryProvider> battery;
    std::unique_ptr<ContentKuroProvider> kuro;
    std::uint64_t lastKuroDiagnosticSequence=0;
    std::wstring backgroundKey, backgroundPath;
    ULONGLONG nextBackgroundRead = 0;
    std::wstring fitKey;
    int capturedFit = -1;
    ULONGLONG nextFitRead = 0;
    void Stop() {
        if (rss) rss->Cancel(); if (prayer) prayer->Cancel(); if (notes) notes->Cancel();
        if (discord) discord->Cancel();
        if (kuro) kuro->Cancel();
        if (datetime) datetime->Cancel();
        if (googleCalendar) googleCalendar->Cancel();
        if (gmail) gmail->Cancel();
        if (battery) battery->Cancel();
        if (rss) rss->Stop(); if (prayer) prayer->Stop(); if (notes) notes->Stop(); if (discord) discord->Stop();
        if (kuro) kuro->Stop();
        if (datetime) datetime->Stop();
        if (googleCalendar) googleCalendar->Stop();
        if (gmail) gmail->Stop();
        if (battery) battery->Stop();
    }
};
static void NotifyTileSourceTransition(const aip::content::TileInstance& tile,TileInstanceProviderState& state,
    aip::notifications::SourceState current)
{
    using namespace aip::notifications;
    const auto notice=state.notificationTransitions.Observe(current);
    if(notice==SourceNotice::None||g_shutdownState.IsRequested()||!IniReadI(L"Settings",L"NotifySourceErrors",1))return;
    const bool failed=notice==SourceNotice::Failed;
    const auto title=IniReadS(L"Strings",failed?L"NotificationSourceFailed":L"NotificationSourceRecovered",
        failed?L"A tile source needs attention":L"A tile source is available again");
    const auto message=NotificationSubjectText(failed?L"NotificationSourceFailedMessage":L"NotificationSourceRecoveredMessage",
        failed?L"%s could not refresh. Open this tile's settings or status for details. DesktopStub will retry automatically.":
            L"%s refreshed successfully. Its tile will update when Windows displays the new content.",tile.name);
    DispatchUserNotification(L"source-"+tile.id,title,message,failed?NIIF_WARNING:NIIF_INFO,false);
}
static std::map<std::wstring, std::unique_ptr<TileInstanceProviderState>> g_tileInstanceProviders;
static std::mutex g_tileInstanceRefreshMutex;
static std::set<std::wstring> g_tileInstanceRefreshRequests;
static void StopTileInstanceProviders(bool preserveCurrentStatuses=false)
{
    StopSelectedSmtcProviders();
    for (auto& item : g_tileInstanceProviders) item.second->Stop();
    g_tileInstanceProviders.clear();
    // Ordinary process exit leaves the quiet entry to its native expiration.
    // Its persisted delivery ID intentionally prevents resurrecting a dismissed
    // card on restart. Configuration failure/provider reset retires it now.
    if(!preserveCurrentStatuses&&!g_shutdownState.IsRequested()&&g_running.load()){ClearPrayerCurrentStatuses();ClearCalendarReminders();}
    g_tileSourceAlerts.clear();
}
static bool ReadTileInstancePlugins(aip::content::Configuration& result)
{
    static std::wstring profile;
    static unsigned long long revision = ~0ull;
    static aip::content::Configuration cached;
    static bool valid = false;
    std::vector<aip::IniSectionData> document;
    {
        std::lock_guard<std::mutex> lock(g_iniCacheMutex);
        if (profile != g_iniCacheProfile || revision != g_iniCacheRevision) {
            document = g_iniCacheDoc; profile = g_iniCacheProfile; revision = g_iniCacheRevision;
        } else { result = cached; return valid; }
    }
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        const auto s = aip::content::Lower(section);
        std::wstring value;
        return (s == L"capsblink" || s == L"asusblink") && aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    std::wstring error;
    valid = aip::content::ReadConfiguration(read, cached, error, ContentAsusEventsFromDocument(document));
    result = cached; return valid;
}
static void RefreshTileInstanceNow(const std::wstring& id)
{
    // UI only signals; the poll thread alone owns the provider map.
    { std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex); g_tileInstanceRefreshRequests.insert(id); }
    WakePollThread();
}
static std::wstring TileInstanceProfileActivation(const std::wstring& arguments)
{
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    return L"--ini " + aip::QuoteCommandLineArg(profile) + L" " + arguments;
}
static std::wstring TileInstanceActivation(const aip::content::TileInstance& tile)
{
    // A live notification overrides the static pin's activation arguments.
    // Preserve the profile identity there too, including paths with spaces.
    return TileInstanceProfileActivation(L"--tile-setup " + tile.id);
}
#if defined(DESKTOPSTUB_INERT_HARDWARE)
static FitMode (*g_testTileInstanceFitReader)() = nullptr;
#endif
static FitMode ReadTileInstanceDesktopFit()
{
#if defined(DESKTOPSTUB_INERT_HARDWARE)
    return g_testTileInstanceFitReader ? g_testTileInstanceFitReader() : FitMode::Fill;
#else
    return ReadUncapturedWallpaperFit();
#endif
}
static int ResolveTileInstanceFit(const aip::content::TileInstance& tile,
    TileInstanceProviderState& state,aip::content::BackgroundId background,bool synchronous)
{
    using namespace aip::content;
    const auto requested=Lower(tile.policy.Read(L"Settings",L"TileWallpaperFit",L"Auto"));
    const bool disabled=tile.policy.Read(L"Settings",L"DisableFitting",L"0")==L"1";
    const bool listen=tile.policy.Read(L"Settings",L"ListenFit",L"1")==L"1";
    const auto key=requested+L":"+std::to_wstring(static_cast<int>(background))+L":"+std::to_wstring(listen)+
        L":"+std::to_wstring(disabled)+L":"+std::to_wstring(tile.configuration.refreshSeconds);
    const auto capture=[&](FitMode fit){state.fitKey=key;state.capturedFit=static_cast<int>(fit);return state.capturedFit;};
    if(disabled)return capture(FitMode::Fill);
    for(const auto& explicitFit:{std::make_pair(L"fill",FitMode::Fill),std::make_pair(L"fit",FitMode::Fit),
        std::make_pair(L"stretch",FitMode::Stretch),std::make_pair(L"center",FitMode::Center),
        std::make_pair(L"tile",FitMode::Tile),std::make_pair(L"span",FitMode::Span)})
        if(requested==explicitFit.first)return capture(explicitFit.second);
    // Auto refers to desktop fitting only when this frame actually uses the
    // desktop. Other sources never consult its registry on render/key paths.
    if(background!=BackgroundId::Wallpaper&&background!=BackgroundId::LiveWallpaper)
        return capture(FitMode::Fill);
    const auto now=GetTickCount64();
    if(state.capturedFit<0||state.fitKey!=key||synchronous||state.forceRefresh||(listen&&now>=state.nextFitRead)) {
        state.capturedFit=static_cast<int>(ReadTileInstanceDesktopFit());state.fitKey=key;
        state.nextFitRead=now+static_cast<ULONGLONG>(tile.configuration.refreshSeconds)*1000;
    }
    return state.capturedFit;
}
static aip::content::Snapshot ComposeTileInstance(const aip::content::TileInstance& tile,
    TileInstanceProviderState& state, const aip::content::SmtcMediaSnapshot& media, bool synchronous,bool publishTile=true)
{
    using namespace aip::content;
    TilePolicyReadScope policyScope(tile.policy);
    Snapshot frame; frame.active = true; frame.name = tile.name;
    frame.policy = tile.policy;
    frame.registrationAppearance = tile.appearance;
    frame.artworkTopic = TileInstanceArtworkTopic(tile);
    frame.secondaryTileId = TileInstanceNativeId(tile); frame.secondaryBranding = tile.appearance.branding;
    frame.presentation = tile.presentation; frame.textMode = tile.configuration.textMode;
    frame.activationArguments = TileInstanceActivation(tile);
    if (!tile.error.empty()) {
        RemovePrayerCurrentStatus(tile.id);
        NotifyTileSourceTransition(tile,state,aip::notifications::SourceState::Failed);
        state.Stop();
        state.rss.reset(); state.prayer.reset(); state.notes.reset(); state.discord.reset(); state.kuro.reset();state.datetime.reset();state.googleCalendar.reset();state.gmail.reset();state.battery.reset();
        Entry background; background.background = L"Transparent";
        frame.backgroundId = BackgroundId::Transparent;
        frame.capturedWallpaperFit = static_cast<int>(FitMode::Fill);
        if (publishTile&&(state.backgroundKey != L"configuration-error" || !IsUsableWallpaperPath(state.backgroundPath))) {
            state.backgroundPath = ContentBackgroundPath(background); state.backgroundKey = L"configuration-error";
        }
        if(publishTile)frame.backgroundPath = state.backgroundPath;
        frame.text = {L"Check tile settings", tile.error, L"Click this tile to repair its settings."};
        frame.wrapSourceText = true; frame.secondaryFrameKey = ContentSnapshotKey(frame); return frame;
    }
    const auto& entry = tile.configuration.entries.front();
    frame.backgroundId = BackgroundIdFor(entry.background);
    if(publishTile)frame.capturedWallpaperFit = ResolveTileInstanceFit(tile,state,frame.backgroundId,synchronous);
    const auto backgroundKey = TileDeliveryPolicyKey(tile.policy) + entry.background + L"\n" + entry.imagePath + L"\n" +
        (frame.backgroundId == BackgroundId::SmtcArtwork && media.artwork ? media.artwork->path : L"");
    const bool wallpaper = frame.backgroundId == BackgroundId::Wallpaper || frame.backgroundId == BackgroundId::LiveWallpaper;
    if (publishTile&&(state.backgroundKey != backgroundKey || !IsUsableWallpaperPath(state.backgroundPath) || synchronous || state.forceRefresh ||
        (wallpaper && IniReadI(L"Settings", L"ListenWallpaper", 1) && GetTickCount64() >= state.nextBackgroundRead))) {
        state.backgroundPath = ContentBackgroundPath(entry, &media); state.backgroundKey = backgroundKey;
        state.nextBackgroundRead = GetTickCount64() + static_cast<ULONGLONG>(tile.configuration.refreshSeconds) * 1000;
    }
    if(publishTile)frame.backgroundPath = state.backgroundPath;
    if (frame.backgroundId == BackgroundId::SmtcArtwork) frame.backgroundLease = media.artwork;
    switch (tile.type) {
    case TileInstanceType::Mail: {
        frame.mailSource=true;frame.mailBadgeEnabled=tile.mail.badgeEnabled;
        if(!state.gmail)state.gmail=std::make_unique<ContentGmailProvider>();
        const auto cfg=GmailConfigurationForTile(tile);
        state.gmail->Refresh(cfg,synchronous,state.forceRefresh);bool completed=false;
        const auto view=state.gmail->Read(cfg,&completed);
        NotifyTileSourceTransition(tile,state,!view.configured?aip::notifications::SourceState::Unconfigured:view.pending?
            aip::notifications::SourceState::Pending:view.ok&&!view.stale?aip::notifications::SourceState::Healthy:aip::notifications::SourceState::Failed);
        frame.sourceDiagnostic=view.status;frame.mailboxAddress=view.mailboxAddress;
        frame.mailUnreadCount=view.ok?view.unreadMessages:0;
        for(const auto& message:view.messages){
            const auto display=aip::gmail::DisplayMessage(message,cfg,
                IniReadS(L"Strings",L"MailUnknownSender",L"Unknown sender"),IniReadS(L"Strings",L"MailNoSubject",L"(No subject)"));
            frame.mailRows.push_back({display.first,display.second,L""});
        }
        // Setup/status belongs in the settings window. An empty mailbox uses
        // the normal static icon; private account data never enters launch args.
        frame.text=frame.mailRows.empty()?Text{}:frame.mailRows.front();
        if(frame.mailRows.empty())frame.textMode=TextMode::Off;
        break;
    }
    case TileInstanceType::Wallpaper:
        frame.text = entry.custom;
        break;
    case TileInstanceType::Rss: {
        if (!state.rss) state.rss = std::make_unique<ContentRssProvider>();
        const auto settings = ReadRssFeedSettingsWith([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            return TileInstanceRead(tile, IniReadS, section, key, fallback);
        });
        // An unconfigured feed is a local setup state, not a polling worker.
        RssFeedSnapshot feed;
        bool completed=false;
        if (!settings.valid || settings.feedUrl.empty()) {
            state.rss->Cancel(); feed.status = settings.valid ? L"Choose a feed URL in this tile's settings." : settings.validationError;
            NotifyTileSourceTransition(tile,state,settings.valid?aip::notifications::SourceState::Unconfigured:aip::notifications::SourceState::Failed);
        } else {
            state.rss->Refresh(settings, synchronous, state.forceRefresh); feed = state.rss->Read(settings,&completed);
            NotifyTileSourceTransition(tile,state,!completed?aip::notifications::SourceState::Pending:
                feed.ok&&!feed.items.empty()?aip::notifications::SourceState::Healthy:aip::notifications::SourceState::Failed);
        }
        if (!feed.ok || feed.items.empty()) { frame.text = {L"Set up RSS", feed.status, L""}; break; }
        if(completed)ObserveRssTileAlerts(tile,feed);
        for (std::size_t i = 0; i < (std::min)(feed.items.size(), std::size_t(5)); ++i)
            frame.rssHeadlines.push_back({feed.items[i].title, feed.items[i].summary, L""});
        const auto selected = (std::min)(static_cast<std::size_t>(entry.rssItem - 1), feed.items.size() - 1);
        frame.text = {feed.items[selected].title, feed.sourceTitle, feed.items[selected].summary};
        std::wstring url, error;
        if (tile.clickAction == L"source" && ValidateRssFeedUrlSetting(feed.items[selected].link, false, url, error))
            frame.activationArguments = TileInstanceProfileActivation(L"--open-url " + aip::QuoteCommandLineArg(url));
        break;
    }
    case TileInstanceType::NowPlaying:
        NotifyTileSourceTransition(tile,state,media.text.primary==L"Media source unavailable"||media.text.primary==L"Media refresh timed out"?
            aip::notifications::SourceState::Failed:media.hasSession?aip::notifications::SourceState::Healthy:aip::notifications::SourceState::Pending);
        frame.smtcSource = true; frame.smtcState = media.playbackState;
        frame.smtcIndicator = tile.configuration.smtc.indicatorMode;
        frame.smtcShowRepeatShuffle = tile.configuration.smtc.showRepeatShuffle;
        frame.smtcRepeat = media.repeat; frame.smtcShuffle = media.shuffle; frame.smtcAppName = media.source;
        if (media.appIcon) { frame.smtcAppIconPath = media.appIcon->path; frame.smtcAppIconLease = media.appIcon; }
        frame.text = media.hasSession ? Text{media.title, media.artist, MediaStatusCaption(frame)} : media.text;
        if (!media.hasSession) frame.text.badge = MediaStatusCaption(frame);
        break;
    case TileInstanceType::PrayerTimes: {
        if (!state.prayer) state.prayer = std::make_unique<ContentPrayerProvider>();
        if (state.forceRefresh) state.prayer->forceRequested = true;
        state.prayer->Refresh(tile.configuration.prayer, synchronous);
        bool completed=false;
        const auto view = state.prayer->Read(tile.configuration.prayer,&completed);
        if(completed&&view.available)ObservePrayerTileAlerts(tile,state.prayer->ReadSchedule(),state.prayer->Now());
        else if(completed)RemovePrayerCurrentStatus(tile.id);
        NotifyTileSourceTransition(tile,state,tile.configuration.prayer.city.empty()||tile.configuration.prayer.country.empty()?
            aip::notifications::SourceState::Unconfigured:!completed?aip::notifications::SourceState::Pending:
            !view.error.empty()?aip::notifications::SourceState::Failed:aip::notifications::SourceState::Healthy);
        frame.prayerSource = true; frame.prayerClock = view.clock;
        frame.text = view.clock.Available() ? Text{view.clock.time, view.clock.prayer + L"\nat " + view.clock.location,
            view.clock.countdown + (view.clock.details.empty() ? L"" : L"\n" + view.clock.details)} : Text{view.title, view.secondary, view.badge};
        for (const auto& row : view.detailRows) frame.detailRows.push_back({row.label, row.value, L""});
        break;
    }
    case TileInstanceType::Battery: {
        if(!state.battery)state.battery=std::make_unique<ContentBatteryProvider>();
        state.battery->Refresh(tile.battery,synchronous,state.forceRefresh);bool completed=false;
        const auto view=state.battery->Read(tile.battery,completed,frame.sourceDiagnostic);
        frame.batterySource=true;
        frame.batteryText={view.charge,view.status,view.runtime,view.wear,view.metricLabel,view.metricValue,view.details,view.rightColumnPercent,view.criticalView};
        frame.text={view.charge,view.status,view.runtime};frame.wrapSourceText=true;
        NotifyTileSourceTransition(tile,state,!completed?aip::notifications::SourceState::Pending:
            !view.hasBattery&&!view.noBattery?aip::notifications::SourceState::Failed:aip::notifications::SourceState::Healthy);
        break;
    }
    case TileInstanceType::Clock: case TileInstanceType::Calendar: {
        if(!state.datetime)state.datetime=std::make_unique<ContentDateTimeProvider>(WakePollThread);
        SYSTEMTIME now{};GetSystemTime(&now);const auto utc=aip::datetime::Seconds(DateTimeCivil(now));
        ContentDateTimeView view;
        const bool reminders=CalendarRemindersDemanded(tile);
        if(tile.type==TileInstanceType::Clock){
            if(state.googleCalendar){state.googleCalendar->Stop();state.googleCalendar.reset();}
            view=state.datetime->Clock(tile.clock,utc,now.wMilliseconds);
        }else if(tile.calendar.source==L"Google"){
            state.datetime->Cancel();
            if(!state.googleCalendar)state.googleCalendar=std::make_unique<ContentGoogleCalendarProvider>();
            view=state.googleCalendar->Calendar(tile.calendar,GoogleCalendarConfigurationForTile(tile),utc,now.wMilliseconds,state.forceRefresh,synchronous,reminders);
        }else{
            if(state.googleCalendar){state.googleCalendar->Stop();state.googleCalendar.reset();}
            view=state.datetime->Calendar(tile.calendar,utc,now.wMilliseconds,state.forceRefresh,synchronous,reminders);
        }
        const bool googleUnconfigured=tile.type==TileInstanceType::Calendar&&tile.calendar.source==L"Google"&&
            (tile.googleCalendar.clientId.empty()||tile.googleCalendar.refreshTokenProtected.empty());
        NotifyTileSourceTransition(tile,state,googleUnconfigured?aip::notifications::SourceState::Unconfigured:view.pending?aip::notifications::SourceState::Pending:
            view.ok?aip::notifications::SourceState::Healthy:aip::notifications::SourceState::Failed);
        if(tile.type==TileInstanceType::Calendar){
            std::vector<aip::calendar_reminders::Occurrence> occurrences;
            aip::calendar_reminders::Settings reminderSettings;ReadCalendarReminderSettings(tile,reminderSettings);
            if(reminders){DYNAMIC_TIME_ZONE_INFORMATION zone{};ResolveContentTimeZone(tile.calendar.timeZone,zone);
                for(const auto& event:view.reminderOccurrences){
                    aip::calendar_reminders::Occurrence notice;notice.identity=event.uid.empty()?aip::notifications::StableId(event.summary+L"|"+std::to_wstring(event.start)+L"|"+std::to_wstring(event.end)):event.uid;
                    notice.title=event.summary;notice.location=event.location;notice.startUtc=event.start;notice.endUtc=event.end;notice.allDay=event.allDay;
                    const auto due=event.start-static_cast<aip::datetime::UTC>(reminderSettings.leadMinutes)*60;
                    if(due<=utc&&utc-due<=90){ContentDateTimeEvent formatted;if(BuildContentCalendarEvent(event,tile.calendar,zone,utc,formatted))notice.timeText=formatted.when;}
                    occurrences.push_back(std::move(notice));
                }
            }
            ObserveCalendarReminders(tile,occurrences,utc,view.ok&&!view.pending&&view.configured);
        }
        if(!view.ok){frame.text={L"Check tile settings",view.status,L""};frame.wrapSourceText=true;break;}
        frame.clockSource=tile.type==TileInstanceType::Clock;frame.calendarSource=!frame.clockSource;
        frame.dateTime={frame.clockSource?view.time:L"",view.weekday,view.dayNumber,view.date,view.location,view.shortWeekday,view.rightToLeft};
        // Optional account setup, loading and empty-calendar notes belong in
        // Tile contents. Keep the ordinary Calendar face focused on its date
        // and event rows, including when no external account is connected.
        if(frame.calendarSource)frame.sourceDiagnostic=view.status;
        frame.text={frame.clockSource?view.time:view.weekday+L" "+view.dayNumber,frame.clockSource?view.location:L"",view.date};
        frame.calendarAgendaHeader=IniReadS(L"Strings",L"CalendarToday",L"Today");
        frame.calendarAgendaEmpty=IniReadS(L"Strings",L"CalendarNoOtherEventsToday",L"No other events today");
        for(const auto& event:view.events){
            frame.detailRows.push_back({event.title,event.when+(event.location.empty()?L"":L"\n"+event.location),L""});
            if(frame.calendarSource)frame.calendarEvents.push_back(event.presentation);
        }
        for(std::size_t index=0;index<view.events.size()&&index<3;++index)if(view.events[index].start<=view.events.front().start)frame.calendarAgendaHighlightThrough=static_cast<unsigned>(index);
        break;
    }
    case TileInstanceType::Wuwa: {
        frame.gameSource=true;
        if(!state.kuro)state.kuro=std::make_unique<ContentKuroProvider>();
        state.kuro->Refresh(tile.kuro,synchronous,state.forceRefresh);bool completed=false;
        const auto notes=state.kuro->Read(tile.kuro,&completed);
        const bool configured=!tile.kuro.gameToken.empty()||!tile.kuro.gameTokenProtected.empty();
        NotifyTileSourceTransition(tile,state,!configured?aip::notifications::SourceState::Unconfigured:!completed?aip::notifications::SourceState::Pending:
            notes.failure!=aip::kuro::FailureKind::None?(state.kuro->ShouldNotifyFailure()?aip::notifications::SourceState::Failed:aip::notifications::SourceState::Pending):
            notes.state==aip::kuro::State::Error?aip::notifications::SourceState::Failed:aip::notifications::SourceState::Healthy);
        frame.sourceDiagnostic=notes.diagnostic;
        if(!notes.diagnostic.empty()&&notes.refreshSequence!=state.lastKuroDiagnosticSequence){
            state.lastKuroDiagnosticSequence=notes.refreshSequence;
            Log(L"Wuthering Waves refresh: %ls",notes.diagnostic.c_str());
        }
        frame.text={notes.line1,notes.line2,L""};frame.wrapSourceText=true;
        for(const auto& row:notes.rows){Text text{row.label,row.value,L""};text.iconTopic=row.iconTopic;frame.detailRows.push_back(std::move(text));}
        break;
    }
    case TileInstanceType::Genshin: case TileInstanceType::Hsr: case TileInstanceType::Zzz: {
        frame.gameSource=true;
        if (!state.notes) state.notes = std::make_unique<ContentNotesProvider>();
        const auto resource = DescribeTileInstanceType(tile.type).notesResource;
        if (state.forceRefresh) state.notes->refreshRequested.fetch_or(1u << resource);
        state.notes->Refresh(tile.configuration.notes, resource, synchronous);
        const auto notes = state.notes->ReadSnapshot(tile.configuration.notes, resource);
        NotifyTileSourceTransition(tile,state,notes.state==aip::notes::StateKind::Loading?aip::notifications::SourceState::Pending:
            notes.state==aip::notes::StateKind::Error?aip::notifications::SourceState::Failed:aip::notifications::SourceState::Healthy);
        frame.text = ContentNotesProvider::FormatSnapshot(notes, entry.notesDetail);
        frame.detailRows = aip::notes::BuildNoteTileRows(notes, entry.notesDetail,
            [](const wchar_t* key, const wchar_t* fallback) { return IniReadS(L"Strings", key, fallback); });
        frame.detailTopic = aip::notes::ResourceTileTopic(notes.resource);
        if (notes.state == aip::notes::StateKind::Error) frame.activationArguments += L" --tile-sign-in";
        break;
    }
    case TileInstanceType::Discord: {
        if (!state.discord) state.discord = std::make_unique<ContentDiscordProvider>();
        if (state.forceRefresh) state.discord->Cancel();
        bool completed=false,failed=false;
        state.discord->Refresh(tile.configuration.discord, synchronous); frame.text = state.discord->Read(&completed,&failed); frame.wrapSourceText = true;
        NotifyTileSourceTransition(tile,state,!completed?aip::notifications::SourceState::Pending:
            failed?aip::notifications::SourceState::Failed:aip::notifications::SourceState::Healthy);
        break;
    }
    case TileInstanceType::QuickAction:
        frame.textMode = TextMode::Off;
        break;
    case TileInstanceType::AppLauncher:
        frame.appLauncherSource=true;frame.launcher=tile.launcher;frame.textMode=TextMode::Off;
        break;
    case TileInstanceType::Gallery:
        frame.text = {L"Photo gallery", L"Choose a folder and gallery tiles in Photo galleries.", L""};
        break;
    }
    state.forceRefresh = false;
    frame.secondaryFrameKey = ContentSnapshotKey(frame);
    return frame;
}
static std::vector<aip::content::Snapshot> BuildCalendarTileFrames(aip::content::Snapshot frame,bool nativeDelivery)
{
    using namespace aip::content;
    if(!frame.calendarSource||!nativeDelivery||frame.secondaryTileId.empty())return {std::move(frame)};
    const bool agenda=frame.textMode!=TextMode::Off&&!frame.calendarEvents.empty();
    frame.nativeQueue=true;frame.nativeQueueTag=L"cal1";frame.nativeQueueReset=true;frame.calendarFlavor=CalendarFlavor::NextEvent;
    frame.nativeQueueLayout=L"CalendarNative2:"+aip::notifications::StableId(ContentSnapshotKey(frame));
    frame.secondaryFrameKey=ContentSnapshotKey(frame);frame.nativeQueueFrameKey=frame.secondaryFrameKey;
    std::vector<Snapshot> frames;frames.push_back(frame);
    if(agenda){frame.calendarFlavor=CalendarFlavor::Agenda;frame.nativeQueueTag=L"cal2";frame.nativeQueueReset=false;
        frame.calendarAgendaHeader=frame.calendarEvents.front().dateLabel;
        if(!frame.calendarEvents.front().isToday)frame.calendarAgendaEmpty=IniReadS(L"Strings",L"CalendarNoOtherEvents",L"No other events");
        const auto day=frame.calendarEvents.front().dateLabel;
        const auto dayKey=frame.calendarEvents.front().dayKey;
        frame.calendarEvents.erase(std::remove_if(frame.calendarEvents.begin(),frame.calendarEvents.end(),[&](const CalendarEvent& event){return dayKey.empty()?event.dateLabel!=day:event.dayKey!=dayKey;}),frame.calendarEvents.end());
        frame.secondaryFrameKey=ContentSnapshotKey(frame);frame.nativeQueueFrameKey=frame.secondaryFrameKey;frames.push_back(std::move(frame));}
    return frames;
}
static std::vector<aip::content::Snapshot> BuildMailTileFrames(aip::content::Snapshot frame,bool nativeDelivery)
{
    if(!frame.mailSource||!nativeDelivery||frame.secondaryTileId.empty())return {std::move(frame)};
    if(frame.mailRows.empty()||frame.textMode!=aip::content::TextMode::Auto){
        frame.nativeQueue=false;frame.nativeQueueTag.clear();frame.nativeQueueReset=true;
        frame.secondaryFrameKey=ContentSnapshotKey(frame);return {std::move(frame)};
    }
    const auto count=(std::min)(frame.mailRows.size(),std::size_t(5));
    const auto layout=L"MailNative1:"+aip::notifications::StableId(ContentSnapshotKey(frame));
    std::vector<aip::content::Snapshot> frames;
    for(std::size_t i=0;i<count;++i){
        auto message=frame;message.nativeQueue=true;message.nativeQueueTag=L"mail"+std::to_wstring(i+1);
        message.nativeQueueReset=i==0;message.mailMessageIndex=i;message.mailIncludeLarge=i+1==count;
        message.nativeQueueLayout=layout;message.secondaryFrameKey=ContentSnapshotKey(message);
        message.nativeQueueFrameKey=message.secondaryFrameKey;frames.push_back(std::move(message));
    }
    return frames;
}
static bool TileInstanceEngineTick(bool synchronous, bool queueGeneration)
{
    using namespace aip::content;
    if (g_shutdownState.IsRequested()) return false;
    TileInstanceConfiguration cfg; std::wstring error;
    static std::wstring reportedError;
    if (!ReadCurrentTileInstances(cfg, error)) {
        StopTileInstanceProviders(); g_contentSmtc.Cancel(); g_contentCaps.Cancel(); g_contentAsus.Cancel();
        PublishSecondarySnapshots({}); PublishContentSnapshot({});
        if (error != reportedError) {
            Log(L"Tile configuration: %ls", error.c_str());
            if(reportedError.empty()&&IniReadI(L"Settings",L"NotifySourceErrors",1))
                NotifyUserOperation(L"tile-configuration",IniReadS(L"Strings",L"NotificationSourceFailed",L"A tile source needs attention"),
                    IniReadS(L"Strings",L"NotificationTileConfigurationFailed",L"The tile configuration could not be read. Open Tiles to repair its settings; existing pins and saved accounts were kept."),NIIF_WARNING);
            reportedError = error;
        }
        return false;
    }
    reportedError.clear();
    const auto pins = CachedSecondaryTileIds();
    std::set<std::wstring> refresh;
    { std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex); refresh.swap(g_tileInstanceRefreshRequests); }
    const auto demanded = [&](const TileInstance& tile) {
        TilePolicyReadScope scope(tile.policy);
        if(tile.primary&&tile.policy.Read(L"Settings",L"PrimaryRegistration",L"Separate")==L"Main")return true;
        if (TilePolicyUsesRegistration(tile.policy)) return TileRegistrationReadyForId(tile.id);
        return tile.primary || pins.count(TileInstanceNativeId(tile)) != 0;
    };
    const auto alertsDemanded=[&](const TileInstance& tile){
        aip::notifications::AlertSettings settings;
        return CalendarRemindersDemanded(tile)||((tile.type==TileInstanceType::PrayerTimes||tile.type==TileInstanceType::Rss)&&ReadTileSourceAlerts(tile,settings));
    };
    std::set<std::wstring> used;
    bool smtcNeeded = false, artworkNeeded = false, appIconNeeded = false, nativeMediaNeeded = false;
    int smtcSeconds = 86400;
    struct SmtcDemand {SmtcSelection selection;int seconds=86400;bool artwork=false,icon=false,events=false;};
    std::map<std::wstring,SmtcDemand> selectedDemand;
    for (const auto& tile : cfg.tiles) if (demanded(tile)||alertsDemanded(tile)) {
        if ((tile.type == TileInstanceType::QuickAction||tile.type==TileInstanceType::AppLauncher) && tile.error.empty()) {
            TilePolicyReadScope scope(tile.policy);
            if (!TilePolicyUsesRegistration(tile.policy)) continue;
        }
        used.insert(tile.id);
        if (demanded(tile)&&tile.error.empty() && (tile.type == TileInstanceType::NowPlaying || (!tile.configuration.entries.empty() &&
            BackgroundIdFor(tile.configuration.entries.front().background) == BackgroundId::SmtcArtwork))) {
            if(tile.configuration.smtc.selection.mode==L"Application"){
                const auto& selection=tile.configuration.smtc.selection;auto& demand=selectedDemand[SmtcSelectionKey(selection)];demand.selection=selection;
                demand.seconds=(std::min)(demand.seconds,tile.configuration.refreshSeconds);
                demand.artwork|=BackgroundIdFor(tile.configuration.entries.front().background)==BackgroundId::SmtcArtwork;
                demand.icon|=tile.configuration.smtc.indicatorMode==MediaIndicatorMode::AppIcon;
                TilePolicyReadScope scope(tile.policy);demand.events|=!TilePolicyUsesRegistration(tile.policy)&&ContentDirectLiveDeliveryAvailable();
                continue;
            }
            smtcNeeded = true; smtcSeconds = (std::min)(smtcSeconds, tile.configuration.refreshSeconds);
            artworkNeeded |= BackgroundIdFor(tile.configuration.entries.front().background) == BackgroundId::SmtcArtwork;
            appIconNeeded |= tile.configuration.smtc.indicatorMode == MediaIndicatorMode::AppIcon;
            TilePolicyReadScope scope(tile.policy);
            nativeMediaNeeded |= !TilePolicyUsesRegistration(tile.policy) && ContentDirectLiveDeliveryAvailable();
        }
    }
    for (auto it = g_tileInstanceProviders.begin(); it != g_tileInstanceProviders.end();)
        if (!used.count(it->first)) { it->second->Stop(); it = g_tileInstanceProviders.erase(it); } else ++it;
    std::set<std::wstring> activePrayerStatuses;
    for(const auto& tile:cfg.tiles)if(tile.type==TileInstanceType::PrayerTimes&&used.count(tile.id)&&alertsDemanded(tile))activePrayerStatuses.insert(tile.id);
    PrunePrayerCurrentStatuses(activePrayerStatuses);
    std::set<std::wstring> activeCalendars;for(const auto& tile:cfg.tiles)if(tile.type==TileInstanceType::Calendar&&CalendarRemindersDemanded(tile))activeCalendars.insert(tile.id);
    PruneCalendarReminders(activeCalendars);
    for(auto it=g_tileSourceAlerts.begin();it!=g_tileSourceAlerts.end();)
        if(!used.count(it->first))it=g_tileSourceAlerts.erase(it);else ++it;
    if (smtcNeeded) g_contentSmtc.Refresh(smtcSeconds, synchronous, artworkNeeded, nativeMediaNeeded, appIconNeeded);
    else g_contentSmtc.Cancel();
    const auto media = smtcNeeded ? g_contentSmtc.Read() : SmtcMediaSnapshot{};
    for(auto it=g_selectedSmtcProviders.begin();it!=g_selectedSmtcProviders.end();)if(!selectedDemand.count(it->first)){it->second->Stop();it=g_selectedSmtcProviders.erase(it);}else ++it;
    std::map<std::wstring,SmtcMediaSnapshot> selectedMedia;
    for(const auto& item:selectedDemand){const auto& demand=item.second;auto& provider=g_selectedSmtcProviders[item.first];if(!provider)provider=std::make_unique<ContentSmtcProvider>();
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(g_testSelectedSmtcReader)provider->testReader=g_testSelectedSmtcReader(demand.selection);
#endif
        provider->Refresh(demand.seconds,synchronous,demand.artwork,demand.events,demand.icon,demand.selection);selectedMedia[item.first]=provider->Read();
    }
    if(!smtcNeeded&&selectedDemand.empty()){std::lock_guard<std::mutex> lock(g_smtcCatalogMutex);g_smtcApplications.clear();}
    // Independent plugins intentionally don't contribute text to any tile.
    Configuration plugins;
    if (ReadTileInstancePlugins(plugins)) {
        if (plugins.caps.actionsEnabled) g_contentCaps.Refresh(plugins.caps); else g_contentCaps.Cancel();
        if (plugins.asus.hardwareEnabled) g_contentAsus.Refresh(plugins.asus); else g_contentAsus.Cancel();
    } else { g_contentCaps.Cancel(); g_contentAsus.Cancel(); }
    std::vector<Snapshot> frames; Snapshot primary;
    for (const auto& tile : cfg.tiles) if (demanded(tile)||alertsDemanded(tile)) {
        // Quick Actions are static native topic logos. They need metadata and
        // activation only; sending a transparent notification would hide them.
        if ((tile.type == TileInstanceType::QuickAction||tile.type==TileInstanceType::AppLauncher) && tile.error.empty()) {
            TilePolicyReadScope scope(tile.policy);
            if (!TilePolicyUsesRegistration(tile.policy)) continue;
        }
        auto& state = g_tileInstanceProviders[tile.id];
        if (state && state->type != tile.type) { state->Stop(); state.reset(); }
        if (!state) { state = std::make_unique<TileInstanceProviderState>(); state->type = tile.type; }
        state->forceRefresh = refresh.count(tile.id) != 0;
        if (state->forceRefresh) ForgetSecondaryTileDelivery(TileInstanceNativeId(tile));
        const bool publishTile=demanded(tile);
        const auto selected=selectedMedia.find(SmtcSelectionKey(tile.configuration.smtc.selection));
        auto frame = ComposeTileInstance(tile, *state, selected==selectedMedia.end()?media:selected->second, synchronous,publishTile);
        if(!publishTile)continue;
        frame.deliveryDiagnostic=ObserveLiveTilePermission(frame);
        if (tile.primary) primary = frame;
        if (IsUsableWallpaperPath(frame.backgroundPath)) {
            TilePolicyReadScope scope(frame.policy);
            const bool calendarNative=frame.calendarSource&&!TileSnapshotUsesRegistration(frame)&&ContentDirectLiveDeliveryAvailable()&&LiveTileUsesAdaptiveXml();
            const bool mailNative=frame.mailSource&&!TileSnapshotUsesRegistration(frame)&&ContentDirectLiveDeliveryAvailable()&&LiveTileUsesAdaptiveXml();
            auto prepared=mailNative?BuildMailTileFrames(std::move(frame),true):BuildCalendarTileFrames(std::move(frame),calendarNative);
            frames.insert(frames.end(),std::make_move_iterator(prepared.begin()),std::make_move_iterator(prepared.end()));
        }
    }
    for (auto gallery : ContentGalleryFrames()) if (IsUsableWallpaperPath(gallery.backgroundPath)){gallery.deliveryDiagnostic=ObserveLiveTilePermission(gallery);frames.push_back(std::move(gallery));}
    PublishContentSnapshot(primary);
    static bool primarySeen = false;
    static Snapshot cleared;
    if (primary.active) cleared = {};
    if (primarySeen && !primary.active && !cleared.active) {
        // Removing the primary wallpaper type stops observation immediately,
        // and replaces its old notification once without removing its pin.
        Entry blank; blank.background = L"Transparent";
        cleared.active = true; cleared.textMode = TextMode::Off; cleared.backgroundId = BackgroundId::Transparent;
        cleared.backgroundPath = ContentBackgroundPath(blank); cleared.secondaryBranding = L"Name";
        cleared.secondaryFrameKey = ContentSnapshotKey(cleared);
    }
    bool clearUsable = cleared.active && IsUsableWallpaperPath(cleared.backgroundPath);
    if (cleared.active && !clearUsable) {
        Entry blank; blank.background = L"Transparent";
        cleared.backgroundPath = ContentBackgroundPath(blank);
        clearUsable = IsUsableWallpaperPath(cleared.backgroundPath);
    }
    if (clearUsable) frames.push_back(cleared);
    std::set<std::wstring> permissionOwners;for(const auto& frame:frames)permissionOwners.insert(frame.secondaryTileId);
    PruneLiveTilePermissions(permissionOwners);
    PublishSecondarySnapshots(frames);
    if (primary.active && !primarySeen && g_hwnd && !g_contentFrameReadyPosted.exchange(true)) {
        if (!PostMessageW(g_hwnd, WM_APP_CONTENT_FRAME_READY, 0, 0)) g_contentFrameReadyPosted = false;
    }
    primarySeen = primary.active;
    if (synchronous || !queueGeneration) return !frames.empty();
    if (!GenerationWorkPending() && GetTickCount64() >= g_contentQueueRetryAfter.load()) {
        auto changed = PrepareSecondaryBatch(frames);
        changed.erase(std::remove_if(changed.begin(), changed.end(), [](const auto& frame) {
            if (TileSnapshotUsesRegistration(frame)) return false;
            TilePolicyReadScope scope(frame.policy);
            return !ContentDirectLiveDeliveryAvailable();
        }), changed.end());
        if (!changed.empty())
            DESKTOPSTUB_SECONDARY_BATCH_GENERATE(std::move(changed), L"Changed tile instances");
    }
    return !frames.empty();
}

// Offline rendering uses the real asset pipeline but never enters the resident
// application lifecycle. All provider input is explicit, local and immutable.
static int RunRenderOnlyCommand()
{
    const auto fail = [](const std::wstring& message) {
        aip::WriteCommandLineText(L"Offline command: " + message, true);
        return 2;
    };
    if (!g_commandLine.openUrl.empty() || !g_commandLine.tileSetupKey.empty() || !g_commandLine.mediaAction.empty() || g_commandLine.exitRequested || g_commandLine.comRegistrationHelper ||
        g_commandLine.appxActivationServer || g_commandLine.liveTileClearOnly ||
        g_commandLine.launchedFromAppxStub || g_commandLine.launchedFromModeSwitch ||
        g_commandLine.waitForPid)
        return fail(L"activation, resident-control and helper options cannot be combined with an offline command.");
    if (g_commandLine.configureOnly && (g_commandLine.renderOnly || g_commandLine.generateOnce || g_commandLine.forceGenerateOnStartup))
        return fail(L"--configure-only cannot be combined with rendering commands.");
    for (const auto& setting : g_commandLine.settings)
    {
        if (IEquals(setting.section, L"Settings") &&
            (IEquals(setting.key, L"RunAtStartup") || IEquals(setting.key, L"RunAtStartupPackaged")))
            return fail(L"startup changes cannot be combined with an offline command.");
        if (CommandLineSettingTargetsObsoleteManifestSection(setting))
            return fail(L"the obsolete Manifest INI section cannot be changed; use Settings.Manifest* keys.");
        if (CommandLineSettingChangesManifest(setting)) g_commandLine.regenerateManifest = true;
    }
    if (!g_commandLine.discordProfileSettings.empty() || !g_commandLine.discordCreatePath.empty()) {
        if (!g_commandLine.configureOnly || !g_commandLine.notesImports.empty() || !g_commandLine.hardwareImports.empty() || !g_commandLine.nowPlayingImports.empty())
            return fail(L"advanced Discord edits are configuration-only and cannot be combined with other donor imports.");
        DiscordHostEditOptions options;
        options.privateInput = g_commandLine.discordPrivateInput;
        options.create = !g_commandLine.discordCreatePath.empty(); options.createPath = g_commandLine.discordCreatePath;
        std::wstring error;
        if (!CommitDiscordHostProfile(g_commandLine.settings, g_commandLine.discordProfileSettings, options, error)) return fail(error);
        aip::WriteCommandLineText(L"Saved Discord profile and host preferences; no provider or Startup operation was performed.", false); return 0;
    }
    if (g_commandLine.discordPrivateInput) return fail(L"--discord-private-input requires an explicit advanced profile edit.");
    const bool manifestOnly = !g_commandLine.configureOnly && g_commandLine.regenerateManifest &&
        !g_commandLine.wallpaperSpecified && !g_commandLine.generateOnce && !g_commandLine.forceGenerateOnStartup;

    std::wstring error;
    if (!ValidateCommandLineWallpaperPath() || !ValidateDesktopStubContentProfile(true, error))
        return fail(error.empty() ? g_commandLine.applyError : error);
    aip::content::Configuration composition;
    aip::content::TileInstance offlineInstance;
    bool instanceMode = false;
    const auto readOfflineConfiguration = [&](auto read, const auto& events) {
        aip::content::TileInstanceConfiguration instances;
        if (!ValidateTileInstanceHostConfiguration(read, instances, error)) return false;
        instanceMode = instances.configured;
        if (!instanceMode) return aip::content::ReadConfiguration(read, composition, error, events);
        if (g_commandLine.configureOnly || manifestOnly) { composition = {}; return true; }
        if (instances.tiles.empty()) { error = L"no tile instance is configured for offline rendering."; return false; }
        auto selected = std::find_if(instances.tiles.begin(), instances.tiles.end(), [](const auto& tile) { return tile.primary; });
        if (selected == instances.tiles.end()) selected = std::find_if(instances.tiles.begin(), instances.tiles.end(),
            [](const auto& tile) { return tile.type == aip::content::TileInstanceType::Wallpaper; });
        if (selected == instances.tiles.end()) { error = L"offline rendering needs a Wallpaper tile with explicit local input; external providers are not started."; return false; }
        offlineInstance = *selected; composition = selected->configuration; return true;
    };
    if (!readOfflineConfiguration([](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            return DesktopStubContentProfileValue(section, key, fallback, true);
        }, ReadContentAsusEvents(true))) return fail(error);

    aip::content::Snapshot snapshot;
    aip::content::Entry selectedEntry;
    std::wstring source;
    const auto selectOfflineInput = [&]() {
    snapshot = {}; selectedEntry = {}; source.clear();
    CommandLineWallpaper(source);
    if (!g_commandLine.configureOnly && !manifestOnly && composition.enabled)
    {
        const auto selected = std::find_if(composition.entries.begin(), composition.entries.end(),
            [](const aip::content::Entry& entry) { return entry.enabled; });
        if (selected == composition.entries.end()) { error = L"no content entry is enabled."; return false; }
        for (const auto& provider : selected->sources)
            if (aip::content::SourceIdFor(provider) != aip::content::SourceId::CustomText)
                { error = L"offline rendering supports CustomText only; external providers are not started."; return false; }
        const auto background = aip::content::BackgroundIdFor(selected->background);
        if (background != aip::content::BackgroundId::Image &&
            background != aip::content::BackgroundId::None &&
            background != aip::content::BackgroundId::Transparent && source.empty())
            { error = L"supply --wallpaper explicitly, or select an Image/None/Transparent background."; return false; }
        snapshot.active = true;
        snapshot.name = selected->name;
        snapshot.textMode = composition.textMode;
        snapshot.presentation = composition.presentation;
        snapshot.backgroundId = background;
        if (instanceMode) {
            snapshot.secondaryTileId = aip::content::TileInstanceNativeId(offlineInstance);
            snapshot.secondaryBranding = offlineInstance.appearance.branding;
        }
        selectedEntry = *selected;
        if (aip::content::EntryUsesSource(*selected, aip::content::SourceId::CustomText))
            snapshot.text = selected->custom;
        if (background == aip::content::BackgroundId::Image) {
            source = TrimCopy(selected->imagePath);
            if (PathIsRelativeW(source.c_str())) source = ParentDirectoryOfPath(g_iniPath) + L"\\" + source;
        } else if (background == aip::content::BackgroundId::Transparent) {
            source.clear(); // Explicit transparency takes priority over --wallpaper.
        }
    }
    else if (!g_commandLine.configureOnly && !manifestOnly && source.empty())
        { error = L"--wallpaper is required when composition is disabled."; return false; }
    return true;
    };
    if (!selectOfflineInput()) return fail(error);

    // Persist only the requested local INI values. Deliberately do not call the
    // normal settings transaction, which also owns external Startup changes.
    bool initialIniCreated = false;
    if (!TryEnsureInitialIniTemplate(initialIniCreated) ||
        !EnsureIniDefaults() || !EnsureIniStringDefaults())
        return fail(L"could not initialize or update the local configuration file: " + g_iniPath);
    if ((!g_commandLine.settings.empty() || !g_commandLine.presetName.empty()) && !MutateDesktopStubIniWithWait([](std::wstring& text) {
            if (!ApplyCommandLinePresetToDocument(text, g_commandLine.applyError)) return false;
            if (!g_commandLine.nowPlayingImports.empty() && !aip::now_playing_profile::RemoveContentEntriesFromText(text)) return false;
            if (ReplacesCommandLineAsusEvents() && !RemoveContentAsusEvents(text)) return false;
            for (const auto& setting : g_commandLine.settings)
                if (!WriteContentProfileSetting(text, setting)) return false;
            if (!StageExplicitSourceTileNameIntents(text, g_commandLine.settings, g_commandLine.applyError)) return false;
            // Notes imports/clear validate the complete prospective profile
            // again under the INI mutation lock, including concurrent changes.
            if (!g_commandLine.notesImports.empty() || !g_commandLine.hardwareImports.empty() || !g_commandLine.nowPlayingImports.empty() || std::any_of(g_commandLine.settings.begin(), g_commandLine.settings.end(),
                [](const auto& setting) { return aip::content::Lower(setting.section).compare(0, 6, L"notes.") == 0 ||
                    IEquals(setting.section, L"AsusBlink") || IEquals(setting.section, L"CapsBlink"); })) {
                const auto document = aip::ParseIniDocument(text);
                aip::content::Configuration prospective; std::wstring issue;
                const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
                    std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
                };
                aip::content::TileInstanceConfiguration instances;
                if (!ValidateTileInstanceHostConfiguration(read, instances, issue) || (!instances.configured &&
                    !aip::content::ReadConfiguration(read, prospective, issue, ContentAsusEventsFromDocument(document)))) return false;
            }
            if (!ValidateCommandLinePresetDocument(text, g_commandLine.applyError)) return false;
            if (!g_commandLine.presetName.empty()) {
                g_commandLine.presetPreviewDocument = text;
                g_commandLine.presetPreviewLoaded = true;
            }
            return true;
        }, CurrentIniWriteLockWaitMs())) return fail(g_commandLine.applyError.empty() ? L"could not save the local setting batch." : g_commandLine.applyError);
    // A preset can copy fields/allocate slots differently if another writer
    // changed the profile after preflight. Use the exact committed snapshot,
    // not its earlier preview, for this offline rendering request.
    if (!g_commandLine.presetName.empty() && !g_commandLine.configureOnly && !manifestOnly) {
        const auto document = aip::ParseIniDocument(g_commandLine.presetPreviewDocument);
        if (!readOfflineConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
        }, ContentAsusEventsFromDocument(document)) || !selectOfflineInput()) return fail(error);
    }
    if (g_commandLine.configureOnly)
    {
        for (const auto& issue : g_commandLine.nowPlayingImportDiagnostics)
            aip::WriteCommandLineText(L"NowPlaying import: " + issue + L"\n", false);
        aip::WriteCommandLineText(L"Saved local configuration; no resident or Startup operation was performed.", false);
        return 0;
    }
    LoadUiStrings();
    ValidateFormatStrings();
    NormalizeSeparatorStrings();
    LoadRuntimeSettingsFromIni(false, false);
    g_tray = false;
    g_logging = false; // A profile-selected log may point outside the output tree.
    g_notificationsEnabled = false;
    g_notifyOnStart = g_notifyOnSuccess = g_notifyOnFailure = g_notifyOnBusy = false;
    if (snapshot.active && source.empty()) {
        source = ContentBackgroundPath(selectedEntry); // only synthesizes a local BMP/PNG
    }
    if (!manifestOnly && !IsUsableWallpaperPath(source)) return fail(L"the explicit image is unavailable.");
    snapshot.backgroundPath = source;
    PublishContentSnapshot(snapshot); // local in-memory snapshot; no provider refresh
    std::wstring manifest;
    DWORD manifestError = ERROR_SUCCESS;
    bool created = false;
    const bool manifestReady = g_commandLine.regenerateManifest
        ? RegenerateAppxManifestFromConfig(manifest, manifestError)
        : EnsureAppxManifest(manifest, created, manifestError);
    if (!manifestReady) return fail(L"could not prepare the local manifest file.");
    if (manifestOnly)
    {
        aip::WriteCommandLineText(L"Wrote local manifest; no package operation was performed.", false);
        return 0;
    }
    const bool generated = Generate(source.c_str(), L"offline render-only", true);
    PublishContentSnapshot({});
    ShutdownGdiPlusForExit();
    if (!generated) return fail(L"asset rendering failed.");
    aip::WriteCommandLineText(L"Rendered local tile assets; no package operation was performed.", false);
    return 0;
}

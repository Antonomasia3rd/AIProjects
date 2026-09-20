// Offline rendering uses the real asset pipeline but never enters the resident
// application lifecycle. All provider input is explicit, local and immutable.
static int RunRenderOnlyCommand()
{
    const auto fail = [](const std::wstring& message) {
        aip::WriteCommandLineText(L"Offline command: " + message, true);
        return 2;
    };
    if (g_commandLine.exitRequested || g_commandLine.comRegistrationHelper ||
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
    const bool manifestOnly = !g_commandLine.configureOnly && g_commandLine.regenerateManifest &&
        !g_commandLine.wallpaperSpecified && !g_commandLine.generateOnce && !g_commandLine.forceGenerateOnStartup;

    std::wstring error;
    if (!ValidateCommandLineWallpaperPath() || !ValidateDesktopStubContentProfile(true, error))
        return fail(error.empty() ? g_commandLine.applyError : error);
    aip::content::Configuration composition;
    if (!aip::content::ReadConfiguration([](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            return DesktopStubContentProfileValue(section, key, fallback, true);
        }, composition, error)) return fail(error);

    aip::content::Snapshot snapshot;
    std::wstring source;
    CommandLineWallpaper(source);
    if (!g_commandLine.configureOnly && !manifestOnly && composition.enabled)
    {
        const auto selected = std::find_if(composition.entries.begin(), composition.entries.end(),
            [](const aip::content::Entry& entry) { return entry.enabled; });
        if (selected == composition.entries.end()) return fail(L"no content entry is enabled.");
        for (const auto& provider : selected->sources)
            if (provider != L"CustomText")
                return fail(L"offline rendering supports CustomText only; external providers are not started.");
        if (selected->background != L"Image" && selected->background != L"None" && source.empty())
            return fail(L"supply --wallpaper explicitly, or select an Image/None background.");
        snapshot.active = true;
        snapshot.name = selected->name;
        snapshot.textMode = composition.textMode;
        if (std::find(selected->sources.begin(), selected->sources.end(), L"CustomText") != selected->sources.end())
            snapshot.text = selected->custom;
        if (selected->background == L"Image") {
            source = TrimCopy(selected->imagePath);
            if (PathIsRelativeW(source.c_str())) source = ParentDirectoryOfPath(g_iniPath) + L"\\" + source;
        }
    }
    else if (!g_commandLine.configureOnly && !manifestOnly && source.empty())
        return fail(L"--wallpaper is required when composition is disabled.");

    // Persist only the requested local INI values. Deliberately do not call the
    // normal settings transaction, which also owns external Startup changes.
    EnsureInitialIniTemplate();
    EnsureIniDefaults();
    EnsureIniStringDefaults();
    if (!g_commandLine.settings.empty() && !MutateDesktopStubIniWithWait([](std::wstring& text) {
            for (const auto& setting : g_commandLine.settings)
                if (!WriteIniValueToText(text, setting.section.c_str(), setting.key.c_str(), setting.value.c_str())) return false;
            return true;
        }, CurrentIniWriteLockWaitMs())) return fail(L"could not save the local setting batch.");
    if (g_commandLine.configureOnly)
    {
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
        aip::content::Entry blank; blank.background = L"None";
        source = ContentBackgroundPath(blank); // only synthesizes a local BMP
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

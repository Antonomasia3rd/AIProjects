// Optional raw-hardware providers for a full DesktopStub host build.
// This file is intentionally included only when
// DESKTOPSTUB_ENABLE_HARDWARE_SOURCES is 1.
#include "../content_sources/asus.h"
#ifndef DESKTOPSTUB_INERT_HARDWARE
#include "../hardware/caps_blink_windows.inc"
#endif

struct ContentCapsProvider
{
    std::unique_ptr<aip::caps::Controller> controller;
    std::wstring key, error;
    std::atomic<bool> restart{false};
    void Refresh(const aip::caps::Config& cfg)
    {
        auto validated = cfg;
        std::wstring validationError;
        if (!aip::caps::Normalize(validated, validationError)) {
            error = validationError;
            if (controller) controller->RequestStop();
            key.clear(); return;
        }
        if (!controller) {
#ifdef DESKTOPSTUB_INERT_HARDWARE
            controller = std::make_unique<aip::caps::Controller>(nullptr);
#else
            controller = std::make_unique<aip::caps::Controller>(aip::caps::MakeWindowsBackend(ReadContentLogicalCaps));
#endif
        }
        if (restart.exchange(false)) { controller->RequestStop(); key.clear(); }
        const auto state = controller->GetSnapshot();
        if (state.running && (!cfg.actionsEnabled || state.phase == aip::caps::Phase::Stopping)) {
            controller->RequestStop(); key.clear(); return; // never join pending driver cleanup on the poll/UI thread
        }
        const auto requested = cfg.target + L":" + std::to_wstring(cfg.intervalMs) + L":" + std::to_wstring(cfg.actionsEnabled);
        if (key == requested) return;
        key = requested;
        if (controller->Update(cfg, error)) error.clear();
    }
    aip::content::Text Read() const
    {
        if (!error.empty()) return {L"Caps indicator", L"Error: " + error, L""};
        if (!controller) return {L"Caps indicator", L"Preview: Actions disabled", L""};
        const auto state = controller->GetSnapshot();
        aip::content::Text text{L"Caps indicator", std::wstring(state.actionsEnabled ? L"Control: " : L"Preview: ") + aip::caps::PhaseText(state.phase), L""};
        aip::content::AppendBounded(text.secondary, state.error);
        aip::content::AppendBounded(text.secondary, state.cleanupError);
        if (state.cleanupPending) aip::content::AppendBounded(text.secondary, L"Waiting for driver completion before restoring/releasing the device.");
        return text;
    }
    void Cancel() { if (controller) controller->RequestStop(); key.clear(); }
    void Stop() { if (controller) controller->Stop(); }
};
static ContentCapsProvider g_contentCaps;

struct ContentAsusProvider
{
    std::unique_ptr<aip::asus::Service> service;
    std::wstring key, error;
    bool started = false, stopping = false;
    std::atomic<bool> restart{false};
#ifdef DESKTOPSTUB_INERT_HARDWARE
    std::function<void(aip::asus::Options&)> configureTestOptions;
#endif
    void RequestStop()
    {
        stopping = service && !service->Stop(0);
        if (!stopping) started = false;
    }
    void Refresh(const aip::content::AsusConfiguration& cfg)
    {
        if (restart.exchange(false)) Cancel();
        if (stopping) { RequestStop(); if (stopping) return; }
        if (started && !service->Read().running) {
            RequestStop(); if (stopping) return;
            // A service's Stop/Fault error policy stays terminal for the same
            // profile. Explicit cancel/restart clears the profile key below.
        }
        auto path = TrimCopy(cfg.profilePath);
        if (!path.empty() && PathIsRelativeW(path.c_str())) path = ParentDirectoryOfPath(g_iniPath) + L"\\" + path;
        auto requested = std::to_wstring(cfg.hardwareEnabled) + L":" + std::to_wstring(cfg.readDiskActivity);
        for (const auto& value : {path, FileFingerprint(path)}) requested += L":" + std::to_wstring(value.size()) + L":" + value;
        for (const auto& setting : cfg.settings) requested += L":" + setting.first + L":" + std::to_wstring(setting.second.size()) + L":" + setting.second;
        if (key == requested) { if (service && started) service->Pause(cfg.paused); return; }
        key = requested;
        auto settings = cfg.settings;
        if (!path.empty()) {
            std::vector<aip::IniSectionData> profile;
            if (!aip::LoadIniDocument(path, profile)) { RequestStop(); error = L"The ASUS pattern profile could not be read."; return; }
            settings.clear();
            for (const auto& section : profile) if (IEquals(section.name, L"Settings"))
                for (const auto& entry : section.entries) settings[entry.key] = entry.value;
        }
        aip::asus::Options options;
        options.hardwareEnabled = cfg.hardwareEnabled; options.readDiskActivity = cfg.readDiskActivity;
        if (!aip::asus::ParseConfiguration(settings, options.configuration, error)) { RequestStop(); return; }
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if (configureTestOptions) configureTestOptions(options);
#endif
        if (!service) service = std::make_unique<aip::asus::Service>();
        if (started ? service->Reload(options, error) : service->Start(options, error)) { started = true; error.clear(); service->Pause(cfg.paused); }
        else RequestStop();
    }
    aip::content::Text Read() const
    {
        if (!error.empty()) return {L"ASUS indicators", L"Error: " + error, L""};
        if (stopping) return {L"ASUS indicators", L"Stopping: waiting for source cleanup", L""};
        return aip::content::AsusContent(service ? service->Read() : aip::asus::Snapshot{});
    }
    void Cancel() { key.clear(); error.clear(); RequestStop(); }
    void Stop() { Cancel(); if (service && service->Stop(5000)) { service.reset(); stopping = false; } started = false; }
};
static ContentAsusProvider g_contentAsus;

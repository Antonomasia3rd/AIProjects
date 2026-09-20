#pragma once
#include "pattern.h"
#include <functional>
#include <memory>

namespace aip { namespace asus {
using Cancel = std::function<bool()>;
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool Apply(Device device, int state, const Cancel& cancel, std::wstring& error) = 0;
    virtual bool ReadDiskBytes(std::uint64_t& bytesPerSecond, const Cancel& cancel, std::wstring& error) = 0;
};
struct Options {
    Configuration configuration;
    bool hardwareEnabled = false;
    bool readDiskActivity = false;
    unsigned pollMs = 20, requestTimeoutMs = 1000;
    std::function<std::unique_ptr<Backend>()> backendFactory;
};
struct Snapshot {
    std::uint64_t revision = 0;
    PatternSnapshot pattern;
    bool running = false, hardwareEnabled = false, faulted = false;
    int appliedMic = -1, appliedKeyboard = -1;
    std::wstring status = L"Stopped", error;
};
class Service {
public:
    Service();
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    bool Start(const Options& options, std::wstring& error);
    bool Reload(const Options& options, std::wstring& error);
    void Pause(bool paused);
    bool Stop(unsigned timeoutMs = 2000);
    Snapshot Read() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::unique_ptr<Backend> CreateNativeBackend(); // construction performs no I/O
} }

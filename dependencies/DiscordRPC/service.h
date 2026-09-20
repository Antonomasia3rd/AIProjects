#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace aip { namespace discord {
using Cancel = std::function<bool()>;
struct Profile {
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> values;
};
struct Context {
    std::map<std::wstring, std::wstring, std::less<>> tokens;
    int idleSeconds = 0;
};
// Injection seam for deterministic tests or a host-provided transport.
// Implementations must honor cancellation; the service owns their lifetime.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void Send(const std::string& activityJson, const std::wstring& status, const Cancel& cancel) = 0;
    virtual void Clear(const std::wstring& status, const Cancel& cancel) = 0;
};
struct Options {
    std::wstring profilePath;
    bool sendEnabled = false;
    int refreshMs = 5000;
    int operationTimeoutMs = 5000;
    bool clearOnStop = true;
    // Null means read the explicit file and actual foreground/system context.
    // Overrides permit testing real template/presence code without user data.
    std::shared_ptr<const Profile> profileOverride;
    std::shared_ptr<const Context> contextOverride;
    std::function<std::unique_ptr<Transport>()> transportFactory;
};
enum class Phase { Stopped, Starting, Preview, Sending, Active, Error, Stopping };
struct Snapshot {
    std::uint64_t revision = 0;
    Phase phase = Phase::Stopped;
    bool running = false;
    bool sendingEnabled = false;
    std::wstring name, details, state, status, error;
    std::string activityJson;
};
class Service {
public:
    Service();
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    bool Start(const Options& options, std::wstring& error);
    bool Reload(const Options& options, std::wstring& error);
    bool Stop(unsigned timeoutMs = 5000);
    Snapshot Read() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} }

#pragma once
// Account/channel reading is separate from presence writing. Nothing starts
// until Start is called with enabled=true and a validated account binding.
#include "service.h"
#include "../discord_tile_model.h"
#include <vector>
namespace aip { namespace discord {
struct ReaderHttpRequest {
    std::wstring method=L"GET",url;
    std::vector<std::pair<std::wstring,std::wstring>> headers;
    std::string body;
    int timeoutMs=5000;
    std::size_t maximumBytes=262144;
    Cancel canceled;
};
struct ReaderHttpResponse {unsigned statusCode=0;std::string body;};
using ReaderHttpTransport=std::function<bool(const ReaderHttpRequest&,ReaderHttpResponse&,std::wstring&)>;
using ReaderDispatch=std::function<void(const std::string&)>;
class ReaderGateway {
public:
    virtual ~ReaderGateway()=default;
    virtual void Connect(const ReaderDispatch& dispatch,const Cancel& canceled)=0;
    virtual bool IsConnected() const=0;
    virtual discord_tiles::Failure LastFailure()const{return discord_tiles::Failure::Disconnected;}
    virtual void Close()=0;
};
struct ReaderOptions {
    bool enabled=false;
    discord_tiles::Binding binding;
    discord_tiles::Configuration configuration;
    std::optional<discord_tiles::Acknowledgement> acknowledgement;
    std::wstring profilePath;
    std::shared_ptr<const Profile> profileOverride;
    int refreshMs=30000,operationTimeoutMs=5000,reconnectMinimumMs=1000,reconnectMaximumMs=60000;
    std::size_t maximumBytes=262144;
    bool useGateway=true;
    bool allowReviewedSend=false;
    ReaderHttpTransport http;
    CredentialDecoder credentialDecoder;
    std::function<std::unique_ptr<ReaderGateway>()> gatewayFactory;
};
struct ReaderSnapshot {
    bool running=false,connected=false;
    std::uint64_t revision=0;
    discord_tiles::Snapshot data;
    std::wstring status;
};
enum class ReaderSendState {Sent,Rejected,RateLimited,Unknown};
struct ReaderSendResult {
    ReaderSendState state=ReaderSendState::Rejected;
    std::wstring messageId,status;
    unsigned retryAfterMs=0;
};
struct ReaderIdentityProbeOptions {
    bool enabled=false,experimentalWarningReviewed=false;
    std::wstring profileKey,warningProfileKey,connection=L"OAuth",profilePath;
    std::shared_ptr<const Profile> profileOverride;
    int operationTimeoutMs=5000;
    std::size_t maximumBytes=262144;
    ReaderHttpTransport http;
    CredentialDecoder credentialDecoder;
    Cancel canceled;
};
struct ReaderIdentityProbeResult {
    bool ok=false;discord_tiles::UserPatch user;discord_tiles::Failure failure=discord_tiles::Failure::None;
    unsigned retryAfterMs=0;std::wstring status;
};
ReaderIdentityProbeResult ProbeReaderIdentity(const ReaderIdentityProbeOptions& options);
// These decode/project already supplied payloads; no HTTP, mutation or logging.
// Incoming wire sequence is tracked separately from reducer receipt sequence.
struct ReaderDecodeState {std::optional<std::uint64_t> wireSequence;};
bool DecodeReaderDispatch(const std::string& json,const discord_tiles::Binding& binding,
    const discord_tiles::Configuration& configuration,const discord_tiles::Capabilities& verified,
    ReaderDecodeState& decoder,std::uint64_t& receiptSequence,discord_tiles::Milliseconds now,
    std::vector<discord_tiles::Event>& events,std::wstring& error);
bool DecodeReaderIdentity(const std::string& json,discord_tiles::UserPatch& user);
bool DecodeReaderHistory(const std::string& json,const discord_tiles::Binding& binding,
    const discord_tiles::Configuration& configuration,discord_tiles::Origin origin,
    std::uint64_t& receiptSequence,discord_tiles::Milliseconds now,
    std::vector<discord_tiles::Event>& events,std::wstring& error);
class AccountReader {
public:
    AccountReader();~AccountReader();
    AccountReader(const AccountReader&)=delete;AccountReader& operator=(const AccountReader&)=delete;
    bool Start(const ReaderOptions& options,std::wstring& error);
    bool Reload(const ReaderOptions& options,std::wstring& error);
    bool Stop(unsigned timeoutMs=5000);
    ReaderSnapshot Read()const;
    ReaderSendResult SendReviewed(const discord_tiles::ComposeIntent& intent);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} }

#include "../dependencies/content_sources/kuro_notes.inc"
#include "../dependencies/content_source_host.h"
#include <iostream>
#define DESKTOPSTUB_INERT_HARDWARE 1
static std::atomic<bool> g_running{true};
static void WakePollThread() {}
#include "../dependencies/DesktopStub/ga_kuro_provider.inc"

namespace {
int checks = 0, failures = 0;
void Check(bool value, const char* message) { ++checks; if (!value) { ++failures; std::cout << "FAIL: " << message << '\n'; } }
using namespace aip::kuro;
std::string RoleDocument(std::string base) {
    const auto details = L"{\"Base\":" + aip::Utf8ToWide(base) + L"}";
    return "{\"code\":0,\"data\":{\"Europe\":" + aip::JsonEscape(details) + "}}";
}
const std::string normalBase = R"({"Id":12345,"Name":"Rover","Energy":120,"MaxEnergy":240,"EnergyRecoverTime":1700003600,"StoreEnergy":20,"MaxStoreEnergy":480,"StoreEnergyRecoverTime":1700007200000,"Liveness":60,"LivenessMaxCount":100,"LivenessUnlock":true,"WeeklyInstCount":2})";
const std::string roleBody = RoleDocument(normalBase);
const Role role{L"12345",L"Europe",L"Rover",60};
void ParseTests() {
    auto result = ParseRoleResponse(roleBody, role, 1700000000);
    Check(result.state == State::Ok && result.current == 120 && result.maximum == 240 && result.recoverySeconds == 3600, "parse upstream Base waveplate fields and recovery timestamp");
    Check(result.refined == 20 && result.refinedMaximum == 480 && result.refinedRecoverySeconds == 7200, "millisecond refined-waveplate recovery timestamps");
    Check(result.activity == 60 && result.activityMaximum == 100 && result.activityUnlocked && result.weeklyChallengesRemaining == 2, "parse activity and remaining weekly challenges");
    Check(result.rows.size() == 4 && result.rows[0].label == L"Waveplates" && result.rows[2].value == L"60/100", "compose game-specific detail rows without fabricated maxima");
    Check(result.playerId == L"12345" && result.playerName == L"Rover" && result.region == L"Europe", "retain selected role identity");
    result = ParseRoleResponse(RoleDocument(R"({"Id":12345,"Name":"Rover","Energy":240,"MaxEnergy":240})"), role, 1700000000);
    Check(result.state == State::Full && result.recoverySeconds == 0 && result.line2 == L"Full", "full waveplates do not require a recovery timestamp");
    result = ParseRoleResponse(RoleDocument(R"({"Id":12345,"Name":"Rover","Energy":1,"MaxEnergy":240})"), role, 1700000000);
    Check(result.state == State::Ok && result.recoverySeconds == -1 && result.line2 == L"Recovery unavailable", "absent recovery remains unavailable instead of falsely Full");
    result = ParseRoleResponse(RoleDocument(R"({"Id":12345,"Name":"Rover","Energy":1,"MaxEnergy":240,"Liveness":0,"LivenessMaxCount":100,"LivenessUnlock":false})"), role);
    Check(result.rows.size() == 2 && result.rows[1].value == L"Not unlocked", "locked activities have explicit state");
    result = ParseRoleResponse(RoleDocument(R"({"Id":12345,"Name":"Rover","Energy":1,"MaxEnergy":240,"EnergyRecoverTime":0,"StoreEnergy":0,"MaxStoreEnergy":0,"Liveness":0,"LivenessMaxCount":0,"LivenessUnlock":false})"), role);
    Check(result.state == State::Ok && result.line2 == L"Recovery unavailable" && result.rows.size() == 2 && result.rows[1].value == L"Not unlocked", "unlocked-only maxima and expired countdowns never falsely report resource Full");
    for (const auto& base : std::vector<std::string>{
        R"({"Id":999,"Name":"Rover","Energy":1,"MaxEnergy":240})",
        R"({"Id":12345,"Name":"Rover","Energy":-1,"MaxEnergy":240})",
        R"({"Id":12345,"Name":"Rover","Energy":1,"MaxEnergy":0})",
        R"({"Id":12345,"Name":"Rover","Energy":2147483648,"MaxEnergy":240})",
        R"({"Id":12345,"Name":"Rover","Energy":1,"Energy":2,"MaxEnergy":240})",
        R"({"Id":12345,"Name":"Rover","nested":{"Energy":1,"MaxEnergy":240}})",
        R"({"Id":12345,"Name":"Rover","Energy":1,"MaxEnergy":240,"EnergyRecoverTime":"unknown"})",
        R"({"Id":12345,"Name":"Rover","Energy":1,"MaxEnergy":240,"StoreEnergy":10})",
        R"({"Id":12345,"Name":"Rover","Energy":1,"MaxEnergy":240,"Liveness":1,"LivenessMaxCount":100,"LivenessUnlock":"false"})"
    }) Check(ParseRoleResponse(RoleDocument(base), role).state == State::Error, "reject corrupt, duplicated, nested-only, mismatched, and unbounded game fields");
    Check(ParseRoleResponse("[]", role).state == State::Error && ParseRoleResponse(roleBody + "x", role).state == State::Error, "strict bounded JSON envelope");
    auto other = role; other.region = L"Asia";
    Check(ParseRoleResponse(roleBody, other).state == State::Error, "never silently take another region's data");
    std::int64_t epoch = 0;
    Check(detail::Timestamp("\"2023-11-14T22:13:20Z\"", epoch) && epoch == 1700000000, "UTC date recovery timestamp");
    Check(detail::Timestamp("\"2023-11-15T05:13:20.123+07:00\"", epoch) && epoch == 1700000000, "offset and fraction recovery timestamp");
    for (const auto* raw : {"\"2023-02-29T00:00:00Z\"", "\"2023-11-14T22:13:20\"", "\"2023-11-14T25:13:20Z\"", "\"2023-11-14T22:13:20+24:00\"", "-1"})
        Check(!detail::Timestamp(raw, epoch), "reject invalid or ambiguous recovery dates");
    Check(Recovery(INT_MAX - 59).size() < 32, "bounded duration formatting");
}
Configuration Settings() { Configuration cfg; cfg.gameToken = L"PRIVATE+TOKEN&VALUE"; cfg.deviceId = L"01234567-89AB-4DEF-8123-456789ABCDEF"; return cfg; }
std::string Discovery(bool multiple = false) {
    return "{\"code\":0,\"data\":{\"Europe\":" + aip::JsonEscape(L"{\"roleId\":12345,\"roleName\":\"Rover\",\"level\":60}") +
        (multiple ? ",\"Asia\":" + aip::JsonEscape(L"{\"roleId\":54321,\"roleName\":\"Other\",\"level\":10}") : "") + "}}";
}
void Respond(aip::HttpFetchResponse& response, const std::string& body) { response.statusCode = 200; response.body.assign(body.begin(), body.end()); }
void TransportTests() {
    auto cfg = Settings(); int calls = 0;
    const HttpTransport transport = [&](const aip::HttpFetchOptions& options, aip::HttpFetchResponse& response, std::wstring&) {
        Check(options.method == L"POST" && !options.followRedirects && options.url.find(cfg.gameToken) == std::wstring::npos, "all credential requests POST to fixed endpoints with redirects disabled");
        Check(options.timeout.count() > 0 && options.timeout <= std::chrono::seconds(cfg.timeoutSeconds) && options.maximumBytes == 1048576, "each request uses remaining whole-flow deadline and bounded bytes");
        if (++calls == 1) {
            Check(options.url == L"https://sdkapi.kurogame-service.com/sdkcom/v2/user/oauth/code/generate.lg" && options.body.find("PRIVATE%2BTOKEN%26VALUE") != std::string::npos && options.body.find("scope=launcher") != std::string::npos, "OAuth access token form encoding matches verified launcher flow");
            Respond(response, R"({"codes":0,"oauthCode":"CODE"})");
        } else if (calls == 2) {
            Check(options.url == L"https://pc-launcher-sdk-api.kurogame.net/game/queryPlayerInfo" && options.body == "{\"oauthCode\":\"CODE\"}", "role discovery request matches upstream");
            Respond(response, Discovery());
        } else {
            Check(options.url == L"https://pc-launcher-sdk-api.kurogame.net/game/queryRole" && options.body == "{\"oauthCode\":\"CODE\",\"playerId\":12345,\"region\":\"Europe\"}", "role query carries discovered region and numeric player ID");
            Respond(response, roleBody);
        }
        return true;
    };
    auto result = Fetch(cfg, transport);
    Check(calls == 3 && result.state == State::Ok && result.current == 120, "single overseas role auto-discovered and rendered through full adapter");
    cfg.playerId = L"12345"; cfg.region = L"Europe"; calls = 0;
    result = Fetch(cfg, [&](const auto& options, auto& response, auto&) {
        ++calls; Respond(response, calls == 1 ? R"({"codes":null,"oauthCode":"CODE"})" : roleBody);
        Check(calls == 1 || options.url.find(L"queryRole") != std::wstring::npos, "explicit role bypasses discovery"); return true;
    });
    Check(calls == 2 && result.state == State::Ok, "explicit UID and region use two requests");
    cfg = Settings(); calls = 0;
    result = Fetch(cfg, [&](const auto&, auto& response, auto&) { Respond(response, ++calls == 1 ? R"({"codes":0,"oauthCode":"CODE"})" : Discovery(true)); return true; });
    Check(calls == 2 && result.state == State::Error && result.line1 == L"Select a Wuthering Waves role", "multiple roles require explicit selection and never choose wrong account");
    std::vector<Role> roles; std::wstring error; calls = 0;
    Check(DiscoverRoles(cfg, roles, error, [&](const auto&, auto& response, auto&) { Respond(response, ++calls == 1 ? R"({"codes":0,"oauthCode":"CODE"})" : Discovery(true)); return true; }) && roles.size() == 2, "explicit setup can obtain game-identified role choices");
    calls = 0; result = Fetch(cfg, [&](const auto&, auto&, auto&) { ++calls; return false; }, {}, [] { return true; });
    Check(calls == 0 && result.state == State::Error, "cancel before OAuth never executes a transport");
    result = Fetch(cfg, [&](const auto&, auto&, auto& transportError) { transportError = cfg.gameToken + L" leaked server diagnostic"; return false; });
    Check(result.state == State::Error && result.failure==FailureKind::Transient && result.line2.find(cfg.gameToken) == std::wstring::npos, "transport diagnostics never echo game credentials and classify a temporary connection failure");
    result = Fetch(cfg, [&](const auto&, auto& response, auto&) { Respond(response, R"({"codes":-1,"message":"PRIVATE+TOKEN&VALUE"})"); return true; });
    Check(result.state == State::Error && result.line2.find(cfg.gameToken) == std::wstring::npos, "upstream API errors never echo game credentials");
    Check(result.failure==FailureKind::Rejected&&result.line2.find(L"code -1")!=std::wstring::npos&&result.line2.find(L"expired")==std::wstring::npos,
        "numeric OAuth rejection does not invent credential expiration or expose upstream messages");
    cfg.gameTokenProtected = L"protected-test"; calls = 0;
    result = Fetch(cfg, [&](const auto&, auto&, auto&) { ++calls; return false; }, [](const auto&, auto&) { return false; });
    Check(calls == 0 && result.state == State::Error, "protected credential failure never falls back to plaintext or starts network");
    result = Fetch(cfg, [&](const auto& options, auto& response, auto&) { ++calls; Check(options.body.find("RESOLVED") != std::string::npos, "decoder supplies token only to OAuth body"); Respond(response, R"({"codes":-1})"); return true; }, [](const auto&, auto& token) { token = L"RESOLVED"; return true; });
    Check(calls == 1 && result.state == State::Error, "optional account decoder is used");
    cfg = Settings(); cfg.playerId = L"12345"; cfg.region = L"Europe"; calls = 0;
    result = Fetch(cfg, [&](const auto&, auto& response, auto&) { Respond(response, ++calls == 1 ? R"({"codes":0,"oauthCode":"CODE"})" : R"({"code":1005})"); return true; });
    Check(calls == 5 && result.state == State::Error && result.failure==FailureKind::Transient && result.line2.find(L"1005")!=std::wstring::npos,
        "launcher retry matches the initial request plus three retries and reports a redacted stage/status");
    calls=0;result=Fetch(cfg,[&](const auto&,auto& response,auto&){++calls;
        Respond(response,calls==1?R"({"codes":0,"oauthCode":"CODE"})":calls<5?R"({"code":1005})":roleBody);return true;});
    Check(calls==5&&result.state==State::Ok,"a launcher result becoming ready on the fourth attempt is accepted");
    calls=0;bool returnedPending=false;unsigned cancelChecks=0;
    result=Fetch(cfg,[&](const auto&,auto& response,auto&){++calls;returnedPending=calls>1;
        Respond(response,calls==1?R"({"codes":0,"oauthCode":"CODE"})":R"({"code":1005})");return true;},{},[&]{return returnedPending&&++cancelChecks>=3;});
    Check(calls==2&&result.state==State::Error&&cancelChecks>=3,"cancellation interrupts retry spacing before another launcher request");
    auto shortDeadline=cfg;shortDeadline.timeoutSeconds=1;calls=0;
    result=Fetch(shortDeadline,[&](const auto&,auto& response,auto&){Respond(response,++calls==1?R"({"codes":0,"oauthCode":"CODE"})":R"({"code":1005})");return true;});
    Check(calls<=4&&result.state==State::Error,"retry spacing never extends the source's whole-flow deadline");
    cfg.gameToken.clear(); calls = 0;
    result = Fetch(cfg, [&](const auto&, auto&, auto&) { ++calls; return false; });
    Check(calls == 0 && result.line1 == L"Wuthering Waves setup", "unconfigured tile is inert and labels exact game credential");
}
void SettingsTests() {
    std::wstring error;
    for (auto item : {std::make_pair(L"GameToken", L"value\r\nInjected"), {L"PlayerId", L"hsr-123"}, {L"RefreshSeconds", L"1"}, {L"TimeoutSeconds", L"121"}, {L"MaximumBytes", L"999999999999"}}) {
        std::wstring value = item.second; Check(!NormalizeSetting(item.first, value, error), "reject unsafe or unbounded account settings");
    }
    std::wstring region = L" Europe "; Check(NormalizeSetting(L"Region", region, error) && region == L"Europe", "region preserves exact discovered dictionary key");
    auto cfg = Settings(), changed = cfg; changed.region = L"Asia";
    Check(ConfigurationKey(cfg) != ConfigurationKey(changed), "role changes invalidate provider state");
    changed = cfg; changed.gameTokenProtected = L"x";
    Check(ConfigurationKey(cfg) != ConfigurationKey(changed), "protected account changes invalidate provider state");
    aip::HttpFetchOptions options; Check(options.method == L"GET" && options.body.empty() && options.followRedirects, "shared HTTP defaults preserve existing callers");
    aip::HttpFetchResponse response; options.method = L"DELETE";
    Check(!aip::FetchHttpBytes(options, response, error), "shared HTTP rejects unsupported verbs before opening network");
    options.method = L"GET"; options.body = "credentials";
    Check(!aip::FetchHttpBytes(options, response, error), "shared HTTP rejects accidental GET bodies before opening network");
}
void ProviderTests() {
    ContentKuroProvider provider; auto cfg = Settings(); int calls = 0;
    provider.testTransport = [&](const auto&, auto& response, auto&) {
        Respond(response, ++calls == 1 ? R"({"codes":0,"oauthCode":"CODE"})" : calls == 2 ? Discovery() : roleBody); return true;
    };
    provider.Refresh(cfg, true); bool completed = false;
    auto result = provider.Read(cfg, &completed);
    Check(completed && result.state == State::Ok && calls == 3, "host per-tile provider completes injected token/discovery/query flow");
    provider.Refresh(cfg, true); result = provider.Read(cfg, &completed);
    Check(completed && calls == 3, "unchanged provider refresh uses result cache rather than repeating account/network work");
    auto changed = cfg; changed.region = L"Other";
    Check(provider.Read(changed).state == State::Loading, "old role result never publishes under changed settings");
    provider.Cancel();
    Check(provider.Read(cfg).state == State::Loading && !provider.source.HasPendingWork(), "cancel discards role result and pending demand");
    cfg.gameToken.clear(); provider.Refresh(cfg, true); result = provider.Read(cfg, &completed);
    Check(completed && result.state == State::Error && calls == 3 && !provider.source.HasPendingWork(), "unconfigured host tile is inert without creating account work");
    provider.Stop();

    ContentKuroProvider retained;auto selected=Settings();selected.playerId=L"12345";selected.region=L"Europe";
    bool offline=false,invalid=false;
    retained.testTransport=[&](const auto& options,auto& response,auto& transportError){
        if(offline){transportError=L"PRIVATE+TOKEN&VALUE raw upstream account diagnostic";return false;}
        Respond(response,invalid?"[]":options.url.find(L"oauth/code")!=std::wstring::npos?R"({"codes":0,"oauthCode":"CODE"})":roleBody);return true;
    };
    retained.Refresh(selected,true);result=retained.Read(selected,&completed);
    Check(completed&&result.state==State::Ok&&!result.stale&&!retained.ShouldNotifyFailure(),"healthy per-account baseline has no failure notice");
    offline=true;retained.Refresh(selected,true,true);result=retained.Read(selected,&completed);
    Check(completed&&result.state==State::Ok&&result.stale&&result.current==120&&result.rows.size()==4&&result.failure==FailureKind::Transient,
        "temporary refresh failure retains the last successful same-account notes with explicit stale metadata");
    Check(result.diagnostic.find(L"last successful")!=std::wstring::npos&&result.diagnostic.find(selected.gameToken)==std::wstring::npos&&!retained.ShouldNotifyFailure(),
        "first transient failure is diagnostic-only and never leaks raw transport details");
    for(int i=0;i<10;++i)retained.Read(selected);
    Check(!retained.ShouldNotifyFailure(),"poll reads of one completed failure do not count as additional failed attempts");
    retained.Refresh(selected,true,true);result=retained.Read(selected);
    Check(result.stale&&retained.ShouldNotifyFailure(),"a second completed transient refresh can produce one coalesced source failure notice");
    offline=false;retained.Refresh(selected,true,true);result=retained.Read(selected);
    Check(!result.stale&&result.failure==FailureKind::None&&!retained.ShouldNotifyFailure(),"successful refresh clears stale state and resets transient failure threshold");
    const auto normalTransport=retained.testTransport;
    retained.testTransport=[](const auto&,auto&,auto&)->bool{throw std::runtime_error("PRIVATE+TOKEN&VALUE upstream exception");};
    retained.Refresh(selected,true,true);result=retained.Read(selected);
    const auto exceptionSequence=result.refreshSequence;
    Check(result.stale&&result.failure==FailureKind::Transient&&exceptionSequence!=0&&!retained.ShouldNotifyFailure()&&result.diagnostic.find(selected.gameToken)==std::wstring::npos,
        "throwing transport has one nonzero completed attempt identity and a sanitized transient diagnostic");
    for(int i=0;i<10;++i)result=retained.Read(selected);
    Check(result.refreshSequence==exceptionSequence&&!retained.ShouldNotifyFailure(),"repeated reads of a throwing transport result cannot inflate its failure attempt count");
    retained.Refresh(selected,true,true);result=retained.Read(selected);
    Check(result.refreshSequence!=exceptionSequence&&retained.ShouldNotifyFailure(),"a new throwing transport attempt advances generation and reaches the transient notice threshold");
    retained.testTransport=normalTransport;retained.Refresh(selected,true,true);retained.Read(selected);
    invalid=true;retained.Refresh(selected,true,true);result=retained.Read(selected);
    Check(result.stale&&result.failure==FailureKind::InvalidData&&retained.ShouldNotifyFailure(),"invalid service data retains prior notes but immediately reports a nontransient error");
    auto otherAccount=selected;otherAccount.playerId=L"99999";retained.Refresh(otherAccount,true,true);result=retained.Read(otherAccount);
    Check(result.state==State::Error&&!result.stale&&result.rows.empty(),"account changes cannot expose another role's retained notes");
    retained.Cancel();Check(retained.Read(selected).state==State::Loading&&!retained.ShouldNotifyFailure(),"cancel releases retained account data and notice state");retained.Stop();
}
}
int main() {
    ParseTests(); TransportTests(); SettingsTests(); ProviderTests();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

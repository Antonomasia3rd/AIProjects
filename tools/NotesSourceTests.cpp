#include "../dependencies/content_sources/notes_profile.inc"
#include "../dependencies/content_sources/notes_accounts.inc"
#include "../dependencies/content_sources/notes_signin_workers.h"
#include <iostream>
#include <thread>
#include <atomic>

namespace {
int failures = 0;
int checks = 0;
using namespace aip::notes;
void Check(bool passed, const char* message)
{
    ++checks;
    std::cout << (passed ? "ok - " : "FAIL - ") << message << '\n';
    if (!passed) ++failures;
}
const std::string resin = R"json({"retcode":0,"message":"OK","data":{
    "current_resin":20,"max_resin":200,"resin_recovery_time":"7200",
    "finished_task_num":2,"total_task_num":4,
    "expeditions":[{"status":"Finished","name":"A"},{"status":"Ongoing","name":"Finished"}],
    "max_expedition_num":5,"current_home_coin":100,"max_home_coin":2400,
    "remain_resin_discount_num":2,"resin_discount_num_limit":3}})json";
const std::string stamina = R"json({"retcode":0,"data":{
    "current_stamina":240,"max_stamina":240,"stamina_recover_time":0,
    "current_train_score":100,"max_train_score":500,
    "expeditions":[{"status":"Finished"}],"total_expedition_num":4,
    "current_reserve_stamina":12,"weekly_cocoon_cnt":1,"weekly_cocoon_limit":3}})json";
const std::string charge = R"json({"retcode":0,"data":{
    "energy":{"progress":{"current":80,"max":240},"restore":3600},
    "vitality":{"current":20,"max":400},"card_sign":"CardSignNo",
    "vhs_sale":{"sale_state":"SaleStateDoing"}}})json";
bool HasLine(const Snapshot& result, const wchar_t* line)
{ return std::find(result.menuLines.begin(), result.menuLines.end(), line) != result.menuLines.end(); }
void TestResponses()
{
    auto result = ParseResponse(ResourceKind::Resin, resin);
    Check(result.state == StateKind::Ok && result.current == 20 && result.maximum == 200 &&
        result.recoverySeconds == 7200 && result.line1 == L"20/200" && result.line2 == L"2h 0m",
        "Genshin fixture preserves the existing count and recovery display");
    Check(HasLine(result, L"Commissions: 2/4") && HasLine(result, L"Expeditions: 1/5") &&
        HasLine(result, L"Realm: 100/2400") && HasLine(result, L"Weekly Bosses: 2/3"),
        "Genshin detail lines preserve semantics and count only expedition status fields");
    result = ParseResponse(ResourceKind::Stamina, stamina, 1);
    Check(result.state == StateKind::Full && result.line1 == L"240/240" && result.line2 == L"Full" &&
        result.refreshSeconds == 30 && HasLine(result, L"Training: 100/500") &&
        HasLine(result, L"Reserve: 12/2400") && HasLine(result, L"Echo of War: 1/3"),
        "Star Rail fixture preserves full state, training, reserve, and weekly detail lines");
    result = ParseResponse(ResourceKind::Charge, charge, 999999);
    Check(result.state == StateKind::Ok && result.line1 == L"80/240" && result.line2 == L"1h 0m" &&
        result.refreshSeconds == 86400 && HasLine(result, L"Engagement: 20/400") &&
        HasLine(result, L"Scratch Card: Incomplete") && HasLine(result, L"Video Store: Open"),
        "ZZZ fixture preserves nested energy and activity status semantics");
    result = ParseResponse(ResourceKind::Resin, R"({"retcode":-100,"message":"Please log in"})");
    Check(result.state == StateKind::Error && result.line1 == L"API error" && result.line2 == L"Please log in",
        "API error status and readable diagnostics remain available to every host");
    for (const auto& malformed : std::vector<std::string>{
        R"({"nested":{"retcode":0},"data":{"current_resin":20,"max_resin":200}})",
        R"({"retcode":0,"data":{"unrelated":{"current_resin":20,"max_resin":200,"resin_recovery_time":"10"}}})",
        R"({"retcode":0,"data":{"current_resin":20,"max_resin":200}})",
        R"({"retcode":0,"data":{"current_resin":-1,"max_resin":200,"resin_recovery_time":"10"}})",
        R"({"retcode":0,"data":{"current_resin":2147483648,"max_resin":200,"resin_recovery_time":"10"}})",
        R"({"retcode":0,"data":{"current_resin":20,"max_resin":200,"resin_recovery_time":"-1"}})",
        R"({"retcode":0,"data":{"current_resin":20,"max_resin":200,"resin_recovery_time":"10junk"}})",
        R"({"retcode":0,"data":{"current_resin":20,"max_resin":200,"resin_recovery_time":"10"},})",
        R"({"retcode":0,"data":{"current_resin":20,"max_resin":200,"resin_recovery_time":"10"},"unused":"\q"})",
        "{\"retcode\":0,\"unused\":\"\xC3\x28\"}",
        "[]", "", "{\"retcode\":0} trailing"
    })
        Check(ParseResponse(ResourceKind::Resin, malformed).state == StateKind::Error,
            "malformed, nested-only, missing, negative, or overflowed resource data reports an error");
    result = ParseResponse(ResourceKind::Resin, R"({"retcode":0,"data":{"current_resin":20,"max_resin":200,"resin_recovery_time":"0"}})");
    Check(result.state == StateKind::Ok && result.line2 == L"Recovery unavailable",
        "a partial resource with zero recovery no longer falsely displays Full");
    result = ParseResponse(ResourceKind::Resin, R"({"retcode":0,"data":{"current_resin":200,"max_resin":200}})");
    Check(result.state == StateKind::Full && result.line2 == L"Full",
        "a fully recovered resource can omit a recovery countdown");
    Check(ParseResponse(ResourceKind::Resin, std::string("\xEF\xBB\xBF") + resin).state == StateKind::Ok,
        "UTF-8 BOM response compatibility is preserved");
    Check(ParseResponse(static_cast<ResourceKind>(999), resin).state == StateKind::Error,
        "invalid resource identifiers cannot silently select another game's parser");
    result = MakeError(ResourceKind::Resin, L"Error", std::wstring(511, L'x') + L"\xD83D\xDE00");
    Check(result.line2.size() == 511, "bounded error text does not split a Unicode surrogate pair");
}
Configuration FixtureConfiguration()
{
    Configuration configuration;
    configuration.resource = ResourceKind::Resin;
    configuration.uid = "800000001";
    configuration.ltoken = "fixture_token";
    configuration.ltuid = "fixture_account";
    return configuration;
}
void TestRequestsAndTransport()
{
    Check(detail::GenerateDS(1234567890, "abcDEF") == "1234567890,abcDEF,21b90fee4fc5da996dfe2cbce1490366",
        "the extracted DS signature retains the deskband salt and exact hashing format");
    auto configuration = FixtureConfiguration();
    aip::HttpFetchOptions request;
    std::wstring error;
    Check(BuildRequest(configuration, request, error) &&
        request.url == L"https://bbs-api-os.hoyolab.com/game_record/genshin/api/dailyNote?server=os_asia&role_id=800000001" &&
        request.timeout == std::chrono::milliseconds(15000) && request.maximumBytes == 1024u * 1024u,
        "the extracted Genshin request preserves endpoint, query, and bounded request policy");
    bool cookie = false, signature = false, clientVersion = false;
    for (const auto& header : request.headers)
    {
        if (header.first == L"Cookie") cookie = header.second == L"ltoken_v2=fixture_token; ltuid_v2=fixture_account";
        if (header.first == L"DS") signature = header.second.size() > 32;
        if (header.first == L"x-rpc-app_version") clientVersion = header.second == L"1.5.0";
    }
    Check(cookie && signature && clientVersion, "request headers retain the existing authentication/client protocol without printing credentials");
    configuration.resource = ResourceKind::Stamina;
    Check(BuildRequest(configuration, request, error) && request.url.find(L"server=prod_official_asia") != std::wstring::npos,
        "Star Rail UID-to-server mapping is reused");
    configuration.resource = ResourceKind::Charge;
    configuration.uid = "130000001";
    Check(BuildRequest(configuration, request, error) && request.url.find(L"sg-act-nap-api.hoyolab.com") != std::wstring::npos &&
        request.url.find(L"server=prod_gf_jp") != std::wstring::npos,
        "ZZZ endpoint and second-digit server mapping are reused");

    configuration = FixtureConfiguration();
    int calls = 0;
    HttpTransport fake = [&](const aip::HttpFetchOptions& options, aip::HttpFetchResponse& response, std::wstring&) {
        ++calls;
        if (options.url.empty()) return false;
        response.statusCode = 200;
        response.body.assign(resin.begin(), resin.end());
        return true;
    };
    Check(Fetch(configuration, fake).state == StateKind::Ok && calls == 1,
        "the complete provider fetches and parses through an injected transport");
    configuration.canceled = [] { return true; };
    Check(Fetch(configuration, fake).line1 == L"Canceled" && calls == 1,
        "cancellation before refresh performs no HTTP work");
    configuration = FixtureConfiguration();
    bool cancel = false;
    configuration.canceled = [&] { return cancel; };
    HttpTransport canceling = [&](const aip::HttpFetchOptions& options, aip::HttpFetchResponse& response, std::wstring& transportError) {
        Check(static_cast<bool>(options.canceled), "the host cancellation callback reaches the transport");
        const bool ok = fake(options, response, transportError);
        cancel = true;
        return ok;
    };
    Check(Fetch(configuration, canceling).line1 == L"Canceled", "late results are discarded after host cancellation");
    configuration = FixtureConfiguration();
    configuration.maximumBytes = 10;
    Check(Fetch(configuration, fake).state == StateKind::Error, "oversized injected responses cannot bypass the provider byte limit");
    configuration = FixtureConfiguration();
    HttpTransport throwing = [](const aip::HttpFetchOptions&, aip::HttpFetchResponse&, std::wstring&) -> bool { throw std::runtime_error("fixture_token"); };
    Check(Fetch(configuration, throwing).line2.find(L"fixture_token") == std::wstring::npos,
        "transport exceptions are contained without exposing credential-bearing exception text");
    const int beforeInvalid = calls;
    configuration.ltoken = "bad\r\nheader";
    Check(Fetch(configuration, fake).state == StateKind::Error && calls == beforeInvalid,
        "malformed cookie input is rejected before invoking any transport");
    request = {};
    request.headers = {{L"Invalid:Name", L"fixture"}};
    std::wstring serialized;
    Check(!aip::http_detail::BuildRequestHeaders(request, serialized, error) && error.find(L"fixture") == std::wstring::npos,
        "shared header serialization validates names without echoing values");
    request.headers = {{L"Cookie", L"fixture\r\nInjected: value"}};
    Check(!aip::http_detail::BuildRequestHeaders(request, serialized, error),
        "shared header serialization rejects line breaks without network access");
    request = {};
    request.accept.assign(65527, L'x');
    Check(!aip::http_detail::BuildRequestHeaders(request, serialized, error),
        "header size limits also cover the default Accept header before network access");
    request = {};
    request.headers = {{L"X-Test", std::wstring(65536, L'x')}};
    Check(!aip::http_detail::BuildRequestHeaders(request, serialized, error),
        "custom header serialization accounts for names and separators in its size limit");
}
void TestConcurrentSnapshots()
{
    std::atomic<int> errors{0};
    std::vector<std::thread> workers;
    for (int thread = 0; thread < 4; ++thread)
        workers.emplace_back([&] {
            for (int iteration = 0; iteration < 250; ++iteration)
                if (ParseResponse(ResourceKind::Resin, resin).line1 != L"20/200" ||
                    ParseResponse(ResourceKind::Charge, charge).line1 != L"80/240") ++errors;
        });
    for (auto& thread : workers) thread.join();
    Check(errors == 0, "two thousand concurrent fixture parses have no host globals or shared mutable state");
}
void TestDetailsAndImports()
{
    auto snapshot = ParseResponse(ResourceKind::Resin, resin);
    Check(snapshot.details[DetailId::Commissions] == L"2/4" && snapshot.details[DetailId::Realm] == L"100/2400" &&
        snapshot.details[DetailId::Expeditions] == L"1/5", "canonical detail values come from the same parsed Notes snapshot");
    snapshot = ParseResponse(ResourceKind::Charge, charge);
    Check(snapshot.details[DetailId::ScratchCard] == L"Incomplete" && snapshot.details[DetailId::VideoStore] == L"Open",
        "non-numeric Notes statuses remain selectable details");
    std::vector<aip::IniSetting> changes;
    std::wstring error;
    const std::wstring profile = L"[Settings]\nRunAtStartup=1\nRefreshIntervalSeconds=600\n[Account.Resin]\nUID=800000001\nLTokenV2Protected=dpapi:0102\nLTuidV2Protected=dpapi:v1:utf8:0304\nRefreshIntervalSeconds=90\n";
    Check(ImportAccountProfile(profile, changes, error), "protected donor account aliases import without decryption");
    bool protectedToken = false, protectedUser = false, interval = false, unsafe = false;
    for (const auto& change : changes) {
        if (change.key == L"LTokenProtected") protectedToken = change.value == L"dpapi:0102";
        if (change.key == L"LTUidProtected") protectedUser = change.value == L"dpapi:v1:utf8:0304";
        if (change.key == L"RefreshSeconds" && change.section == L"Notes.Resin") interval = change.value == L"90";
        if (change.key == L"RunAtStartup" || change.section == L"Settings") unsafe = true;
    }
    Check(protectedToken && protectedUser && interval && !unsafe, "imports preserve blobs and per-account refresh while ignoring host activation metadata");
    Check(ImportCookieJson(1, L"{\"uid\":\"800000001\",\"ltoken_v2\":\"fixture-token\",\"ltuid_v2\":\"fixture-user\",\"refresh_interval\":120,\"install\":true}", changes, error),
        "legacy cookie JSON and its refresh alias import as one passive setting batch");
    for (const auto& malformed : {L"{}", L"{\"uid\":\"800000001\",\"uid\":\"800000002\",\"ltoken_v2\":\"a\",\"ltuid_v2\":\"b\"}",
        L"{\"uid\":\"800000001\",\"ltoken_v2\":\"a\",\"ltuid_v2\":\"b\",\"refresh_interval\":\"bad\"}", L"[]", L"{bad}"})
        Check(!ImportCookieJson(0, malformed, changes, error) && changes.empty() && !error.empty(), "malformed or ambiguous cookie sources publish no partial batch");
    for (const auto& malformed : {L"[Account.Resin]\nUID=800000001\nLTokenV2Protected=dpapi:ZZ\nLTuidV2Protected=dpapi:01\n",
        L"[Account.Resin]\nUID=800000001\nLTokenV2=a\nLTuidV2=c\nRefreshIntervalSeconds 90\n", L"[Account.Charge]\nUID=800000001\n"})
        Check(!ImportAccountProfile(malformed, changes, error) && changes.empty(), "invalid protected/duplicate/incomplete profiles publish no partial batch");
    Check(ImportAccountProfile(L"[Account.Resin]\nUID=800000001\nLTokenV2=primary\nltoken_v2=fallback\nLTuidV2=account\n[account.resin]\nUID=800000009\n", changes, error),
        "repeated account sections preserve the baseline's last effective assignment");
    bool lastUid = false, primaryAlias = false;
    for (const auto& change : changes) {
        if (change.key == L"UID") lastUid = change.value == L"800000009";
        if (change.key == L"LToken") primaryAlias = change.value == L"primary";
    }
    Check(lastUid && primaryAlias, "native credential alias precedence is preserved without rejecting a valid fallback alias");
    aip::content::NotesAccount saved;
    saved.uid = L"800000001"; saved.tokenProtected = L"dpapi:0102"; saved.userIdProtected = L"dpapi:0304";
    int decryptions = 0;
    Account resolved;
    Check(ResolveStoredAccount(saved, resolved, error, [&](const auto&, auto& plain) { ++decryptions; plain = L"fixture-only"; return true; }) &&
        decryptions == 2 && resolved.uid == "800000001", "credential resolution occurs only through the injected fetch-time decoder");
    saved.uid.clear();
    Check(!ResolveStoredAccount(saved, resolved, error, [&](const auto&, auto&) { ++decryptions; return true; }) && decryptions == 2,
        "an incomplete account starts no decryption");
    Check(aip::content::SensitiveSetting(L"Notes.Resin", L"LTokenV2Protected") &&
        aip::content::SensitiveSetting(L"Notes.Resin", L"LTUidProtected"), "all protected and legacy credential fields share log redaction");
}
void TestBoundGameAccounts()
{
    const std::string regions = R"({"retcode":0,"data":{"list":[{"region":"os_asia"},{"region":"os_usa"}]}})";
    const std::string asia = R"({"retcode":0,"data":{"list":[{"game_biz":"hk4e_global","game_uid":"800000001","region":"os_asia","region_name":"Asia","nickname":"Fixture A"}]}})";
    const std::string america = R"({"retcode":0,"data":{"list":[{"game_biz":"hk4e_global","game_uid":"600000001","region":"os_usa","region_name":"America","nickname":"Fixture B"}]}})";
    std::vector<BoundGameAccount> accounts;
    std::vector<std::wstring> servers;
    std::wstring error;
    Check(ParseBoundGameRegions(ResourceKind::Resin, regions, servers, error) && servers.size() == 2 && servers[1] == L"os_usa",
        "official getAllRegions grammar yields separate known servers");
    Check(ParseBoundGameAccounts(ResourceKind::Resin, asia, accounts, error) && accounts.size() == 1 &&
        accounts[0].uid == L"800000001" && accounts[0].server == L"os_asia" && accounts[0].nickname == L"Fixture A",
        "official role-list grammar preserves UID server and nickname for selection");
    for (const auto& input : std::vector<std::string>{"", "[]", R"({"retcode":-100,"message":"fixture-secret"})",
        R"({"retcode":0,"data":{"list":[{"game_uid":"x","region":"os_asia"}]}})",
        R"({"retcode":0,"data":{"list":[{"game_uid":"800000001","region":"unsupported"}]}})",
        R"({"retcode":0,"data":{"list":[{"game_uid":"800000001","region":"os_asia"},{}]}})",
        R"({"retcode":0,"data":{"list":[{"game_uid":"800000001","region":"os_asia"}],"other":"\q"}})"}) {
        accounts.push_back({L"stale", L"stale", L"", L""});
        Check(!ParseBoundGameAccounts(ResourceKind::Resin, input, accounts, error) && accounts.empty() &&
            error.find(L"fixture-secret") == std::wstring::npos,
            "invalid or API-error account responses publish no partial accounts or untrusted diagnostics");
    }
    Check(!ParseBoundGameRegions(ResourceKind::Resin, R"({"retcode":0,"data":{"list":[{"region":"os_asia"},{"region":"prod_gf_us"}]}})", servers, error) && servers.empty(),
        "a different game's server cannot enter the requested game's account choices");
    Check(ParseBoundGameAccounts(ResourceKind::Charge, R"({"retcode":0,"data":{"list":[{"game_biz":"hk4e_global","game_uid":"800000001","region":"os_asia"}]}})", accounts, error) && accounts.empty(),
        "cross-game roles are excluded rather than guessed as this game's UID");
    Check(ParseBoundGameAccounts(ResourceKind::Resin, R"({"retcode":0,"data":{"list":[{"game_uid":"800000001","region":"os_asia"},{"game_uid":"800000001","region":"os_asia"}]}})", accounts, error) && accounts.size() == 1,
        "duplicate UID-server rows do not create ambiguous duplicate choices");
    std::string many = R"({"retcode":0,"data":{"list":[)";
    for (int i = 0; i < 65; ++i) { if (i) many += ','; many += R"({"game_uid":"800000001","region":"os_asia"})"; }
    many += "]}}";
    Check(!ParseBoundGameAccounts(ResourceKind::Resin, many, accounts, error) && accounts.empty(), "role parsing has a hard input row bound even for duplicates");
    aip::HttpFetchOptions request;
    Check(BuildBoundGameAccountsRequest(ResourceKind::Resin, L"fixture-token", L"fixture-user", true, request, error) &&
        request.url == L"https://api-account-os.hoyolab.com/account/binding/api/getAllRegions?game_biz=hk4e_global" &&
        request.maximumBytes == 1048576 && request.timeout.count() == 15000,
        "discovery starts at the verified official region endpoint with bounded limits");
    int calls = 0; bool bounded = true, cookie = false;
    const auto fake = [&](const aip::HttpFetchOptions& next, aip::HttpFetchResponse& response, std::wstring&) {
        ++calls; bounded = bounded && next.maximumBytes <= 1048576 && next.timeout.count() > 0 && next.timeout.count() <= 15000;
        for (const auto& header : next.headers) if (header.first == L"Cookie") cookie = header.second == L"ltoken_v2=fixture-token; ltuid_v2=fixture-user";
        const auto body = calls == 1 ? regions : calls == 2 ? asia : america;
        if (calls == 2) bounded = bounded && next.url == L"https://api-account-os.hoyolab.com/binding/api/getUserGameRolesByLtoken?game_biz=hk4e_global&region=os_asia";
        if (calls == 3) bounded = bounded && next.url.find(L"&region=os_usa") != std::wstring::npos;
        response.statusCode = 200; response.body.assign(body.begin(), body.end()); return true;
    };
    Check(FetchBoundGameAccounts(request, ResourceKind::Resin, accounts, error, fake) && calls == 3 && bounded && cookie && accounts.size() == 2,
        "injected discovery follows regions then each server within one time and aggregate byte budget");
    bool canceled = false; calls = 0; request.canceled = [&] { return canceled; };
    const auto canceling = [&](const auto& next, auto& response, auto& issue) {
        const auto result = fake(next, response, issue); if (calls == 2) canceled = true; return result;
    };
    Check(!FetchBoundGameAccounts(request, ResourceKind::Resin, accounts, error, canceling) && calls == 2 && accounts.empty(),
        "cancellation between server replies discards all old-account results");
    request.canceled = {}; calls = 0;
    const auto inconsistent = [&](const auto& next, auto& response, auto& issue) {
        const auto result = fake(next, response, issue); if (calls == 3) response.body.assign(asia.begin(), asia.end()); return result;
    };
    Check(!FetchBoundGameAccounts(request, ResourceKind::Resin, accounts, error, inconsistent) && accounts.empty(),
        "a server-mismatched role reply cannot publish a guessed selection");
    const auto oversized = [](const auto&, auto& response, auto&) { response.statusCode = 200; response.body.assign(1048577, 'x'); return true; };
    Check(!FetchBoundGameAccounts(request, ResourceKind::Resin, accounts, error, oversized) && accounts.empty(), "oversize injected replies cannot bypass the total byte limit");
    const auto throwing = [](const auto&, auto&, auto&) -> bool { throw std::runtime_error("fixture-secret"); };
    Check(!FetchBoundGameAccounts(request, ResourceKind::Resin, accounts, error, throwing) && accounts.empty() && error.find(L"fixture-secret") == std::wstring::npos,
        "transport exceptions cannot leak authentication data or publish partial choices");
    request.url = L"https://example.invalid/"; calls = 0;
    Check(!FetchBoundGameAccounts(request, ResourceKind::Resin, accounts, error, fake) && calls == 0, "discovery rejects an unrelated request before invoking transport");
    Check(!BuildBoundGameAccountsRequest(ResourceKind::Resin, L"fixture\r\nInjected: value", L"user", true, request, error), "cookie header injection is rejected before transport");
    Check(BuildBoundGameAccountsRequest(ResourceKind::Stamina, L"plain", L"user", false, request, error) &&
        request.headers.back().second == L"ltoken=plain; ltuid=user" && request.url.find(L"hkrpg_global") != std::wstring::npos,
        "legacy cookie pairs and the selected game retain explicit protocol identity");
    auto configuration = FixtureConfiguration(); configuration.server = L"os_usa";
    Check(BuildRequest(configuration, request, error) && request.url.find(L"server=os_usa") != std::wstring::npos,
        "an explicitly selected supported server takes precedence over a UID-prefix guess");
    configuration.server = L"prod_gf_us";
    Check(!BuildRequest(configuration, request, error), "the Notes fetch rejects a server belonging to another game");
    configuration.server.clear();
    Check(BuildRequest(configuration, request, error) && request.url.find(L"server=os_asia") != std::wstring::npos,
        "old accounts without Server keep UID-prefix inference");
    std::vector<aip::IniSetting> changes;
    Check(ImportCookieJson(0, L"{\"uid\":\"800000001\",\"region\":\"os_usa\",\"ltoken_v2\":\"fixture\",\"ltuid_v2\":\"fixture-user\"}", changes, error) &&
        std::any_of(changes.begin(), changes.end(), [](const auto& item) { return item.key == L"Server" && item.value == L"os_usa"; }),
        "cookie import stages the selected server atomically with account fields");
    Check(!ImportCookieJson(0, L"{\"uid\":\"800000001\",\"region\":\"prod_gf_us\",\"ltoken_v2\":\"fixture\",\"ltuid_v2\":\"fixture-user\"}", changes, error) && changes.empty(),
        "an invalid selected server rejects the entire account import");
    changes = ClearAccountSettings(0);
    Check(std::any_of(changes.begin(), changes.end(), [](const auto& item) { return item.key == L"Server" && item.value.empty(); }),
        "explicit account replacement or clearing removes an old discovered server");
    aip::content::NotesAccount saved; saved.uid = L"800000001"; saved.server = L"os_usa"; saved.token = L"fixture"; saved.userId = L"fixture-user";
    Account resolved;
    Check(ResolveStoredAccount(saved, resolved, error, {}) && resolved.server == L"os_usa", "stored server selection survives fetch-time account resolution without credential decoding");
}
void TestSignInPolicy()
{
    std::wstring error, value = L" yes ";
    Check(NormalizeNotesSignInSetting(L"notes.signin.resin", L"RetainSession", value, error) && value == L"1", "retained browser sessions are an explicit typed per-game preference");
    value = L"0";
    Check(NormalizeNotesSignInSetting(L"Notes.SignIn.Charge", L"RetainSession", value, error) && value == L"0", "fresh private sign-in is available independently for each game");
    value = L"perhaps";
    Check(!NormalizeNotesSignInSetting(L"Notes.SignIn.Stamina", L"RetainSession", value, error), "invalid browser retention preferences cannot silently become retained mode");
    Check(!KnownSignInSection(L"Notes.SignIn.Unknown") && !KnownSignInSection(L"Notes.Resin"), "browser preference sections cannot alias credential or unknown game sections");
    Check(ValidateNotesSignInConfiguration([](const auto*, const auto*, const auto* fallback) { return std::wstring(fallback); }, error), "absent retention preferences select fresh sessions without rewriting an INI");
    Check(!ValidateNotesSignInConfiguration([](const wchar_t* section, const auto*, const auto* fallback) { return std::wstring(section) == L"Notes.SignIn.Stamina" ? std::wstring(L"invalid") : std::wstring(fallback); }, error), "whole-document preference validation catches a bad inactive game's retention setting");
    Check(IsHoYoLabCookieDomain(L".hoyolab.com") && IsHoYoLabCookieDomain(L"act.hoyolab.com") &&
        !IsHoYoLabCookieDomain(L"evilhoyolab.com") && !IsHoYoLabCookieDomain(L"hoyolab.com.example.invalid") && !IsHoYoLabCookieDomain(L"hoyoverse.com"),
        "cookie import accepts only the exact HoYoLAB domain or proper subdomains");
    Check(SignInResultCurrent(3, 3, false, false) && !SignInResultCurrent(2, 3, false, false) &&
        !SignInResultCurrent(3, 3, true, false) && !SignInResultCurrent(3, 3, false, true),
        "closed reset or canceled session generations cannot publish stale account results");
    Check(std::wstring(SignInGames[0].name) == L"Genshin Impact" && std::wstring(SignInGames[1].page).find(L"#/hsr") != std::wstring::npos &&
        std::wstring(SignInGames[2].profile) == L"zzz", "game context binds names official pages API business IDs and isolated browser profile keys");
}
void TestLocalBrowserCleanup()
{
    LocalBrowserCleanup cleanup;
    Check(cleanup.Begin(1) && cleanup.Pending() && !cleanup.Begin(2), "cleanup cannot restart over a pending clear operation");
    Check(!cleanup.Complete(1, true), "browser data cannot be declared cleared before blank navigation completes");
    Check(!cleanup.AllowNavigation(1, L"https://act.hoyolab.com/", 9) && !cleanup.AllowNavigation(2, L"about:blank", 10),
        "ordinary page navigation and stale browser generations are blocked during cleanup");
    Check(cleanup.AllowNavigation(1, L"about:blank", 10) && !cleanup.AllowNavigation(1, L"about:blank", 11),
        "cleanup binds one exact blank navigation rather than accepting a later unrelated completion");
    Check(cleanup.NavigationCompleted(1, 9, true) == LocalCleanupStep::None && cleanup.NavigationCompleted(2, 10, true) == LocalCleanupStep::None,
        "old page completions cannot trigger clearing for a current session");
    Check(!cleanup.RequestClose() && cleanup.CloseRequested(), "normal Close is deferred while browser cleanup is pending");
    Check(cleanup.NavigationCompleted(1, 10, true) == LocalCleanupStep::ClearData && cleanup.Phase() == LocalCleanupPhase::Clearing,
        "successful completion of the expected blank page authorizes the clear API exactly once");
    Check(cleanup.NavigationCompleted(1, 10, true) == LocalCleanupStep::None && !cleanup.AllowNavigation(1, L"https://act.hoyolab.com/", 12),
        "clearing neither starts twice nor allows page navigation to refill browser storage");
    Check(!cleanup.Complete(2, true) && cleanup.Pending(), "an old cleanup callback cannot complete another browser generation");
    Check(cleanup.Complete(1, true) && !cleanup.Pending() && cleanup.CloseRequested() && cleanup.RequestClose(),
        "confirmed terminal cleanup releases a previously deferred close");
    Check(cleanup.Begin(3) && cleanup.AllowNavigation(3, L"about:blank", 30) &&
        cleanup.NavigationCompleted(3, 30, false) == LocalCleanupStep::Failed && !cleanup.Pending(),
        "failed blank navigation terminates cleanup without claiming browser data was cleared");
    Check(cleanup.Begin(4) && cleanup.AllowNavigation(4, L"about:blank", 40) &&
        cleanup.NavigationCompleted(4, 40, true) == LocalCleanupStep::ClearData && cleanup.Complete(4, false) && cleanup.Phase() == LocalCleanupPhase::Failed,
        "failed clear completion remains a terminal failure rather than a success");
    Check(cleanup.Begin(5), "cleanup can be explicitly retried after a terminal failure");
    cleanup.Abandon();
    Check(!cleanup.Pending() && cleanup.Phase() == LocalCleanupPhase::Abandoned && !cleanup.Complete(5, true) &&
        cleanup.NavigationCompleted(5, 50, true) == LocalCleanupStep::None,
        "explicit cancellation or forced shutdown rejects late navigation and cleanup callbacks without waiting");
    Check(cleanup.Begin(6) && cleanup.Fail(6) && !cleanup.Pending() && !cleanup.Complete(6, true),
        "synchronous navigation or API failure is terminal and cannot later become success");
}
void TestOwnedSignInWorkers()
{
    std::mutex mutex; std::condition_variable changed;
    bool started = false;
    std::atomic<bool> canceled{false}, exited{false};
    SignInWorkers workers;
    Check(workers.Start([&] { { std::lock_guard<std::mutex> lock(mutex); canceled = true; } changed.notify_all(); }, [&] {
        std::unique_lock<std::mutex> lock(mutex); started = true; changed.notify_all();
        changed.wait_for(lock, std::chrono::seconds(2), [&] { return canceled.load(); });
        exited = true;
    }), "lookup worker ownership is published before its thread starts");
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(1), [&] { return started; });
    }
    Check(started && workers.ReapCompleted() == 0 && workers.Size() == 1 && !exited.load() && !canceled.load(),
        "ordinary UI reaping neither joins nor cancels a still-running lookup");
    workers.CancelAndJoin();
    Check(canceled.load() && exited.load() && workers.Size() == 0,
        "global shutdown returns only after the owned lookup observes cancellation and exits");
    int finishedCancelCalls = 0;
    Check(workers.Start([&] { ++finishedCancelCalls; }, [] {}), "worker owner can start a new lookup after a completed shutdown cycle");
    std::size_t reaped = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (workers.Size() && std::chrono::steady_clock::now() < deadline) {
        reaped += workers.ReapCompleted();
        if (workers.Size()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(reaped == 1 && workers.Size() == 0 && finishedCancelCalls == 0,
        "completed thread records are joined and released without canceling a finished lookup");
    int cancelCalls = 0, taskCalls = 0;
    SignInWorkers failed([](SignInWorkers::Task) -> std::thread { throw std::runtime_error("injected thread-start failure"); });
    Check(!failed.Start([&] { ++cancelCalls; }, [&] { ++taskCalls; }) && cancelCalls == 1 && taskCalls == 0 && failed.Size() == 0,
        "thread construction failure cancels the lookup and leaves no owned record or task behind");
    std::atomic<bool> destructorCanceled{false}, destructorExited{false};
    {
        SignInWorkers scoped;
        Check(scoped.Start([&] { { std::lock_guard<std::mutex> lock(mutex); destructorCanceled = true; } changed.notify_all(); }, [&] {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, std::chrono::seconds(1), [&] { return destructorCanceled.load(); });
            destructorExited = true;
        }), "scope teardown fixture starts an owned cancellable lookup");
    }
    Check(destructorCanceled.load() && destructorExited.load(), "owner destruction joins work before captured state can leave scope");
    std::atomic<int> allCanceled{0}, allExited{0}, missedCancellation{0};
    SignInWorkers batch;
    bool batchStarted = true;
    for (int i = 0; i < 2; ++i) batchStarted = batch.Start([&] { { std::lock_guard<std::mutex> lock(mutex); ++allCanceled; } changed.notify_all(); }, [&] {
        std::unique_lock<std::mutex> lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(1), [&] { return allCanceled.load() == 2; })) ++missedCancellation;
        ++allExited;
    }) && batchStarted;
    batch.CancelAndJoin();
    Check(batchStarted && allCanceled == 2 && allExited == 2 && missedCancellation == 0 && batch.Size() == 0,
        "shutdown cancels every lookup before joining any lookup rather than serializing cancellation behind an active join");
}
}
int main()
{
    TestResponses();
    TestRequestsAndTransport();
    TestConcurrentSnapshots();
    TestDetailsAndImports();
    TestBoundGameAccounts();
    TestSignInPolicy();
    TestLocalBrowserCleanup();
    TestOwnedSignInWorkers();
    std::cout << checks << " Notes checks; " << failures << " failures\n";
    std::cout << (failures ? "Notes source tests failed.\n" : "Notes source tests passed.\n");
    return failures ? 1 : 0;
}

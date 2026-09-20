#include "../dependencies/content_sources/notes.inc"
#include <iostream>
#include <thread>
#include <atomic>

namespace {
int failures = 0;
using namespace aip::notes;
void Check(bool passed, const char* message)
{
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
}
int main()
{
    TestResponses();
    TestRequestsAndTransport();
    TestConcurrentSnapshots();
    std::cout << (failures ? "Notes source tests failed.\n" : "Notes source tests passed.\n");
    return failures ? 1 : 0;
}

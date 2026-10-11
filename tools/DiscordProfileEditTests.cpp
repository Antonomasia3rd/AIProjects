#include "../dependencies/DiscordRPC/profile_edit.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace aip::discord;
namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* message) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
const ProfileOptionDescriptor* Find(const std::vector<ProfileOptionDescriptor>& options, const wchar_t* section, const wchar_t* key) {
    for (const auto& option : options) if (option.section == section && option.key == key) return &option;
    return nullptr;
}
bool Wait(const std::function<bool()>& ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!ready() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return ready();
}
}
int wmain()
{
    const auto options = ProfileOptions();
    const auto* activity = Find(options, L"general", L"activity_type");
    const auto* cadence = Find(options, L"general", L"update_interval");
    const auto* text = Find(options, L"general", L"details_template");
    Check(activity && activity->kind == ProfileOptionKind::ActivityType && cadence && cadence->hostCadence && text,
        "explicit activity and host-cadence metadata comes from the real source catalog");
    Check(!Find(options, L"app", L"run_at_startup") && Find(options, L"general", L"token") && Find(options, L"gateway", L"close_timeout_ms"),
        "source options include credentials/transports while excluding donor host lifecycle fields");
    ProfileEditPolicy policy;
    std::wstring error;
    for (const auto* value : {L"0", L"1", L"2", L"3", L"5"}) {
        aip::IniSetting setting{L"GENERAL", L"ACTIVITY_TYPE", value};
        Check(NormalizeProfileSetting(setting, policy, error) && setting.value == value, "all supported activity values remain integers rather than booleans");
    }
    aip::IniSetting invalid{L"general", L"activity_type", L"4"};
    Check(!NormalizeProfileSetting(invalid, policy, error), "unsupported custom-status activity is rejected");
    invalid = {L"general", L"token", L"fixture-private"};
    Check(!NormalizeProfileSetting(invalid, policy, error), "credential edits require explicit masked/private input policy");
    policy.allowPrivateSecretEdits = true;
    Check(NormalizeProfileSetting(invalid, policy, error), "explicit private input may retain plaintext without forced DPAPI migration");
    policy.allowPrivateSecretEdits = false;
    const std::wstring original = L"; preserved comment\r\n[general]\r\nactivity_name=Fixture\r\ntoken=legacy-untouched\r\ntoken_protected=dpapi:v1:utf8:0102ABCD\r\n[app]\r\nrun_at_startup=true\r\n[unrelated]\r\nvalue=preserve-me\r\n";
    std::wstring candidate;
    ProfileEditResult result;
    const std::wstring decoded = L"First line\nC:\\new\\target\\tail\tSecond line";
    Check(ApplyProfileEditsToText(original, {{L"GENERAL", L"details_template", decoded}, {L"layout", L"show_buttons", L"1"}, {L"general", L"activity_type", L"3"}},
        policy, candidate, result, error) && result.changed, "a valid complete advanced batch stages atomically");
    std::wstring reread;
    Check(text && ReadProfileOptionForEditor(result.profile, *text, reread) && reread == decoded,
        "literal backslashes, Windows paths, newlines and tabs round-trip through both Discord and INI escaping layers");
    Check(candidate.find(L"token=legacy-untouched\r\n") != std::wstring::npos &&
        candidate.find(L"token_protected=dpapi:v1:utf8:0102ABCD\r\n") != std::wstring::npos &&
        candidate.find(L"[app]\r\nrun_at_startup=true") != std::wstring::npos && candidate.find(L"; preserved comment") == 0,
        "unrelated host sections/comments and exact plaintext/protected blobs remain untouched");
    Check(!ApplyProfileEditsToText(original, {{L"general", L"activity_name", L"would change"}, {L"ipc", L"connect_timeout_ms", L"1"}}, policy, candidate, result, error) &&
        candidate.empty() && !result.changed, "an invalid complete batch publishes no partial candidate");
    Check(!ApplyProfileEditsToText(original, {{L"censor_map", L"pattern_replace", L"[=>hidden"}}, policy, candidate, result, error),
        "the real presence regex grammar rejects invalid changed patterns without exposing their text");
    Check(ApplyProfileEditsToText(original, {{L"censor_map", L"pattern_replace", L"private.*=>hidden"}, {L"censor_map", L"rule_order", L"pattern_replace,full_replace"}},
        policy, candidate, result, error), "real replacement and rule-order grammar accepts a valid private masking rule");
    Check(!ApplyProfileEditsToText(original, {{L"general", L"update_interval", L"15"}}, policy, candidate, result, error),
        "an apparently writable cadence cannot be committed without host mapping acknowledgement");
    policy.acknowledgeHostCadenceMapping = true;
    Check(ApplyProfileEditsToText(original, {{L"general", L"update_interval", L"15"}}, policy, candidate, result, error) &&
        result.requiresHostCadenceApply && result.requestedRefreshSeconds == 15, "cadence result explicitly tells the host what effective source preference to apply");
    wchar_t temporary[32768]{};
    const auto count = GetTempPathW(_countof(temporary), temporary);
    if (!count || count >= _countof(temporary)) return 2;
    const auto directory = std::wstring(temporary) + L"DiscordProfileEditFixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) return 2;
    const auto path = directory + L"\\advanced.ini";
    const auto createdPath = directory + L"\\created.ini";
    const auto absentPath = directory + L"\\not-created-by-edit.ini";
    Check(!CommitProfileEdits(absentPath, {{L"general", L"activity_name", L"fixture"}}, policy, result, error) &&
        GetFileAttributesW(absentPath.c_str()) == INVALID_FILE_ATTRIBUTES, "ordinary editing never creates a missing advanced profile");
    Check(CreateProfileDocument(createdPath, policy, result, error) && result.requiresHostCadenceApply && result.requestedRefreshSeconds == 5,
        "an explicit setup action creates the real functional defaults and reports cadence mapping");
    ProfileDocument created;
    Check(ReadProfileDocument(createdPath, created, error) && created.profile.values[{L"general", L"activity_type"}] == L"0" &&
        created.profile.values[{L"general", L"token_protected"}].empty() && !created.profile.values.count({L"app", L"run_at_startup"}),
        "creation adds no token, standalone Startup or resident lifecycle settings");
    const auto createdText = created.text;
    Check(!CreateProfileDocument(createdPath, policy, result, error) && ReadProfileDocument(createdPath, created, error) && created.text == createdText,
        "explicit creation never replaces an existing profile");
    Check(aip::WriteTextFileUtf8Bom(path, original), "create synthetic advanced profile");
    std::wstring resolved;
    Check(ResolveProfileReference(L".\\advanced.ini", directory + L"\\host.ini", resolved, error) && aip::IniEquals(resolved, path),
        "referenced paths resolve against the host INI rather than process working directory");
    ProfileDocument document;
    Check(ReadProfileDocument(path, document, error) && document.text == original, "read-only document loading neither normalizes nor migrates credentials");
    Check(!CommitProfileEdits(path, {{L"general", L"activity_name", L"partial"}, {L"general", L"activity_type", L"4"}}, policy, result, error),
        "invalid file batch returns failure");
    ProfileDocument unchanged;
    Check(ReadProfileDocument(path, unchanged, error) && unchanged.text == original, "invalid atomic file batch changes zero bytes");
    policy.requireExpectedText = true; policy.expectedText = original;
    Check(CommitProfileEdits(path, {{L"general", L"activity_name", L"first writer"}}, policy, result, error), "optimistic save commits its complete expected version");
    Check(!CommitProfileEdits(path, {{L"general", L"activity_name", L"stale writer"}}, policy, result, error), "a stale editor cannot overwrite another writer");
    policy.requireExpectedText = false;
    std::atomic<int> failed{0};
    std::vector<std::thread> writers;
    const std::pair<const wchar_t*, const wchar_t*> independent[] = {
        {L"large_assets", L"morning_text"}, {L"afk", L"afk_message"},
        {L"buttons", L"button_1_label"}, {L"small_assets", L"default_text"}
    };
    for (int index = 0; index < 4; ++index) writers.emplace_back([&, index] {
        for (int iteration = 0; iteration < 10; ++iteration) {
            ProfileEditResult saved; std::wstring failure;
            const auto value = L"writer-" + std::to_wstring(index) + L"-" + std::to_wstring(iteration);
            if (!CommitProfileEdits(path, {{L"general", L"details_template", value}, {L"general", L"state_template", value},
                {independent[index].first, independent[index].second, value}}, policy, saved, failure)) ++failed;
        }
    });
    for (auto& writer : writers) writer.join();
    Check(failed == 0 && ReadProfileDocument(path, document, error) &&
        document.profile.values[{L"general", L"details_template"}] == document.profile.values[{L"general", L"state_template"}],
        "concurrent complete batches retain paired fields under the native profile mutex");
    for (int index = 0; index < 4; ++index)
        Check(document.profile.values[{independent[index].first, independent[index].second}] == L"writer-" + std::to_wstring(index) + L"-9",
            "fresh locked updates retain every independent writer's final field instead of losing unrelated changes");
    Check(document.profile.values[{L"general", L"token_protected"}] == L"dpapi:v1:utf8:0102ABCD" &&
        document.profile.values[{L"general", L"token"}] == L"legacy-untouched", "concurrent updates never alter either credential representation");
    std::atomic<int> transports{0};
    Service first, second;
    Options preview;
    preview.profileOverride = std::make_shared<Profile>(document.profile);
    preview.contextOverride = std::make_shared<Context>();
    preview.transportFactory = [&]() -> std::unique_ptr<Transport> { ++transports; return {}; };
    Check(first.Start(preview, error) && second.Start(preview, error) &&
        Wait([&] { return first.Read().phase == Phase::Preview && second.Read().phase == Phase::Preview; }) && transports == 0,
        "two independent preview clients need no client ID, real context, credential resolution or transport");
    Check(first.Stop(2000) && second.Stop(2000), "injected preview workers stop within the fixture deadline");
    DeleteFileW(path.c_str());
    DeleteFileW(createdPath.c_str());
    Check(RemoveDirectoryW(directory.c_str()) != FALSE, "fixture leaves no profile or temporary sibling behind");
    std::cout << checks << " Discord profile edit checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}

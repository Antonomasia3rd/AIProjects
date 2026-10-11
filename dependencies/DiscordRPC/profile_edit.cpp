#include "profile_edit.h"
#include <shlwapi.h>
#include <algorithm>
#include <set>
#include <atomic>

namespace aip { namespace discord {
namespace {
std::wstring LowerName(const std::wstring& value) { return aip::ToLower(aip::Trim(value)); }
bool ValidText(const std::wstring& value, std::size_t maximum) {
    if (value.size() > maximum) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto ch = value[index];
        if (!ch || (ch >= 0xdc00 && ch <= 0xdfff) || (ch >= 0xd800 && ch <= 0xdbff &&
            (++index == value.size() || value[index] < 0xdc00 || value[index] > 0xdfff))) return false;
    }
    return true;
}
const ProfileOptionDescriptor* FindOption(const std::vector<ProfileOptionDescriptor>& options, const std::wstring& section, const std::wstring& key) {
    for (const auto& option : options) if (option.section == section && option.key == key) return &option;
    return nullptr;
}
std::wstring EncodeText(const std::wstring& value) {
    std::wstring result;
    for (wchar_t ch : value) {
        if (ch == L'\\') result += L"\\\\";
        else if (ch == L'\n') result += L"\\n";
        else if (ch == L'\r') result += L"\\r";
        else if (ch == L'\t') result += L"\\t";
        else result += ch;
    }
    return result;
}
bool ExistingAbsoluteFile(const std::wstring& path, std::wstring& error) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos || PathIsRelativeW(path.c_str())) { error = L"An explicit absolute profile file path is required."; return false; }
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) { error = L"The referenced profile must be an existing file."; return false; }
    return true;
}
}
bool ResolveProfileReference(const std::wstring& reference, const std::wstring& hostIniPath, std::wstring& absolutePath, std::wstring& error) {
    absolutePath.clear(); error.clear();
    if (reference.empty() || reference.find(L'\0') != std::wstring::npos || hostIniPath.empty()) { error = L"A profile reference and host INI path are required."; return false; }
    const auto candidate = PathIsRelativeW(reference.c_str()) ? aip::GetDirectoryName(aip::MakeAbsolutePath(hostIniPath)) + L"\\" + reference : reference;
    absolutePath = aip::MakeAbsolutePath(candidate);
    return !absolutePath.empty();
}
bool ParseProfileDocument(const std::wstring& text, Profile& result, std::wstring& error) {
    error.clear(); result = {};
    if (!ValidText(text, 1048576)) { error = L"The profile contains invalid or oversized text."; return false; }
    Profile profile;
    for (const auto& section : aip::ParseIniDocument(text))
        for (const auto& entry : section.entries) profile.values[{LowerName(section.name), LowerName(entry.key)}] = entry.value;
    result = std::move(profile); return true;
}
bool ReadProfileDocument(const std::wstring& path, ProfileDocument& result, std::wstring& error) {
    result = {}; error.clear();
    if (!ExistingAbsoluteFile(path, error)) return false;
    std::vector<BYTE> bytes;
    if (!aip::ReadWholeFileBytes(path, bytes, 1048576)) { error = L"The referenced profile could not be read within its 1 MiB limit."; return false; }
    std::wstring text;
    if (!aip::DecodeTextBytes(bytes, text)) { error = L"The referenced profile could not be decoded."; return false; }
    Profile profile;
    if (!ParseProfileDocument(text, profile, error)) return false;
    result.text = std::move(text); result.profile = std::move(profile); return true;
}
bool ReadProfileOptionForEditor(const Profile& profile, const ProfileOptionDescriptor& descriptor, std::wstring& value) {
    const auto found = profile.values.find({descriptor.section, descriptor.key});
    const auto raw = found == profile.values.end() ? descriptor.defaultValue : found->second;
    value = descriptor.kind == ProfileOptionKind::OpaqueSecret ? raw : aip::DecodeBackslashEscapes(raw);
    return found != profile.values.end();
}
bool NormalizeProfileSetting(aip::IniSetting& setting, const ProfileEditPolicy& policy, std::wstring& error) {
    error.clear(); setting.section = LowerName(setting.section); setting.key = LowerName(setting.key);
    const auto options = ProfileOptions();
    const auto* option = FindOption(options, setting.section, setting.key);
    if (!option) { error = L"The setting is not an editable Discord source option."; return false; }
    if (!ValidText(setting.value, option->Sensitive() ? 65536 : 4096)) { error = L"The setting contains invalid or oversized text (value not shown)."; return false; }
    if (option->Sensitive() && !policy.allowPrivateSecretEdits) { error = L"Credential changes require an explicit private/masked input policy."; return false; }
    if (option->hostCadence && !policy.acknowledgeHostCadenceMapping) { error = L"The host owns source cadence; map the requested interval to host settings before committing this option."; return false; }
    switch (option->kind) {
    case ProfileOptionKind::Boolean: {
        bool parsed = false;
        if (!aip::ParseBoolValue(setting.value, parsed)) { error = L"The option requires a true/false value."; return false; }
        setting.value = parsed ? L"true" : L"false"; break;
    }
    case ProfileOptionKind::Integer: {
        int parsed = 0;
        if (!aip::ParseIntValueInRange(setting.value, option->minimum, option->maximum, parsed)) { error = L"The integer option is outside its supported range."; return false; }
        setting.value = std::to_wstring(parsed); break;
    }
    case ProfileOptionKind::ActivityType: {
        int parsed = 0;
        if (!aip::ParseIntValueInRange(setting.value, 0, 5, parsed) || parsed == 4) { error = L"Activity type must be 0, 1, 2, 3, or 5."; return false; }
        setting.value = std::to_wstring(parsed); break;
    }
    case ProfileOptionKind::Enumeration:
        setting.value = LowerName(setting.value);
        if (std::find(option->choices.begin(), option->choices.end(), setting.value) == option->choices.end()) { error = L"Unknown enum option."; return false; }
        break;
    case ProfileOptionKind::ClientId:
        setting.value = aip::Trim(setting.value);
        if (!setting.value.empty() && (setting.value.size() < 17 || setting.value.size() > 20 ||
            std::any_of(setting.value.begin(), setting.value.end(), [](wchar_t ch) { return ch < L'0' || ch > L'9'; }))) { error = L"Client ID must be empty for preview or a 17-20 digit application ID."; return false; }
        break;
    case ProfileOptionKind::OpaqueSecret:
        if (std::any_of(setting.value.begin(), setting.value.end(), [](wchar_t ch) { return ch < 0x20 || ch == 0x7f; })) { error = L"An opaque credential cannot contain control characters."; return false; }
        break;
    case ProfileOptionKind::Text:
    case ProfileOptionKind::SecretText:
        break;
    }
    return ValidateProfileFieldGrammar(setting.section, setting.key, setting.value, error);
}
bool ApplyProfileEditsToText(const std::wstring& original, const std::vector<aip::IniSetting>& settings,
    const ProfileEditPolicy& policy, std::wstring& candidate, ProfileEditResult& result, std::wstring& error) {
    candidate.clear(); result = {}; error.clear();
    if (policy.requireExpectedText && original != policy.expectedText) { error = L"The profile changed since it was opened; reload before saving."; return false; }
    Profile originalProfile;
    if (!ParseProfileDocument(original, originalProfile, error)) return false;
    std::vector<aip::IniSetting> normalized;
    const auto options = ProfileOptions();
    std::set<std::pair<std::wstring, std::wstring>> seen;
    ProfileEditResult prepared;
    bool credentialsTouched = false, replacePlain = false, replaceProtected = false;
    for (auto setting : settings) {
        if (!NormalizeProfileSetting(setting, policy, error)) return false;
        if (!seen.emplace(setting.section, setting.key).second) { error = L"Duplicate setting in an advanced profile batch."; return false; }
        const auto* option = FindOption(options, setting.section, setting.key);
        if (setting.section == L"general" && (setting.key == L"token" || setting.key == L"token_protected")) {
            credentialsTouched = true;
            if (setting.key == L"token") replacePlain = !aip::Trim(setting.value).empty();
            else replaceProtected = !aip::Trim(setting.value).empty();
        }
        if (option->hostCadence) { prepared.requiresHostCadenceApply = true; prepared.requestedRefreshSeconds = std::stoi(setting.value); }
        if (option->kind != ProfileOptionKind::OpaqueSecret) setting.value = EncodeText(setting.value);
        normalized.push_back(std::move(setting));
    }
    if (replacePlain && replaceProtected) { error = L"Choose one nonempty Discord credential representation per private batch (values not shown)."; return false; }
    if (credentialsTouched) {
        const auto clear = [&](const wchar_t* key) {
            auto found = std::find_if(normalized.begin(), normalized.end(), [&](const aip::IniSetting& setting) {
                return setting.section == L"general" && setting.key == key;
            });
            if (found == normalized.end()) normalized.push_back({L"general", key, L""});
            else found->value.clear();
        };
        // An explicit private choice replaces the effective credential. Blank
        // clears the full family rather than reactivating stale fallback data.
        if (!replacePlain) clear(L"token");
        if (!replaceProtected) clear(L"token_protected");
    }
    auto text = original;
    for (const auto& setting : normalized)
        if (!aip::WriteIniValueToText(text, setting.section.c_str(), setting.key.c_str(), setting.value)) { error = L"Could not stage an advanced profile setting."; return false; }
    std::string bytes;
    if (!aip::TryWideToUtf8(text, bytes) || bytes.size() > 1048576 - 3) { error = L"The edited profile has invalid Unicode or exceeds its 1 MiB byte limit."; return false; }
    if (!ParseProfileDocument(text, prepared.profile, error)) return false;
    prepared.changed = text != original;
    candidate = std::move(text); result = std::move(prepared); return true;
}
bool CommitProfileEdits(const std::wstring& path, const std::vector<aip::IniSetting>& settings,
    const ProfileEditPolicy& policy, ProfileEditResult& result, std::wstring& error) {
    result = {}; error.clear();
    if (policy.lockWaitMs > 60000) { error = L"The profile lock wait must be at most 60000 milliseconds."; return false; }
    if (settings.empty()) { error = L"An advanced profile edit batch is required."; return false; }
    if (!ExistingAbsoluteFile(path, error)) return false;
    aip::IniWriteMutexGuard boundedLock(path, policy.lockWaitMs);
    if (!boundedLock.IsLocked()) { error = L"Could not lock the bounded advanced profile capture."; return false; }
    // Keep the source-byte limit and the native fresh mutation under the same
    // mutex. MutateFresh recursively acquires it on this thread.
    ProfileDocument bounded;
    if (!ReadProfileDocument(path, bounded, error)) return false;
    ProfileEditResult staged;
    const bool ok = aip::IniConfigStore(path, L"", policy.lockWaitMs).MutateFresh([&](std::wstring& text) {
        if (!ExistingAbsoluteFile(path, error)) return false;
        std::wstring candidate;
        ProfileEditResult prepared;
        if (!ApplyProfileEditsToText(text, settings, policy, candidate, prepared, error)) return false;
        text = std::move(candidate); staged = std::move(prepared); return true;
    });
    if (!ok) { if (error.empty()) error = L"The advanced profile transaction could not be committed."; return false; }
    result = std::move(staged); return true;
}
bool MakeDefaultProfileDocument(std::wstring& text, ProfileEditResult& result, std::wstring& error) {
    text.clear(); result = {}; error.clear();
    std::wstring candidate = L"; Discord source profile, explicitly created from the shared functional defaults.\r\n";
    ProfileEditResult prepared;
    for (const auto& option : ProfileOptions()) {
        if (!aip::WriteIniValueToText(candidate, option.section.c_str(), option.key.c_str(), option.defaultValue)) { error = L"Could not stage the real source defaults."; return false; }
        if (option.hostCadence) { prepared.requiresHostCadenceApply = true; prepared.requestedRefreshSeconds = std::stoi(option.defaultValue); }
    }
    if (!ParseProfileDocument(candidate, prepared.profile, error)) return false;
    prepared.changed = true; text = std::move(candidate); result = std::move(prepared); return true;
}
bool CreateProfileDocument(const std::wstring& path, const ProfileEditPolicy& policy,
    ProfileEditResult& result, std::wstring& error) {
    result = {}; error.clear();
    if (path.empty() || path.find(L'\0') != std::wstring::npos || PathIsRelativeW(path.c_str()) || policy.lockWaitMs > 60000) {
        error = L"Explicit profile creation requires an absolute path and a bounded lock wait."; return false;
    }
    if (!policy.acknowledgeHostCadenceMapping) { error = L"Creating a source profile requires acknowledgement of its default host cadence mapping."; return false; }
    std::wstring text; ProfileEditResult prepared;
    if (!MakeDefaultProfileDocument(text, prepared, error)) return false;
    aip::IniWriteMutexGuard lock(path, policy.lockWaitMs);
    if (!lock.IsLocked()) { error = L"Could not lock the explicit profile creation."; return false; }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) { error = L"The profile already exists; creation never replaces it."; return false; }
    static std::atomic<unsigned long> sequence{0};
    const auto temporary = path + L".create-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence);
    const bool written = aip::WriteTextFileUtf8Bom(temporary, text);
    // No REPLACE_EXISTING: a non-cooperating writer that wins the race retains
    // its file too. Binding a host/source preference occurs only after success.
    const bool created = written && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
    if (!created) { DeleteFileW(temporary.c_str()); error = L"The explicit profile could not be published; any existing target was preserved."; return false; }
    prepared.changed = true; result = std::move(prepared); return true;
}
} }

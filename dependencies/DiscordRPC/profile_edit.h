#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "service.h"
#include "../desktop_app_baseline.h"
#include <vector>

namespace aip { namespace discord {
enum class ProfileOptionKind { Boolean, Integer, ActivityType, Enumeration, ClientId, Text, SecretText, OpaqueSecret };
struct ProfileOptionDescriptor {
    std::wstring section, key, label, defaultValue;
    ProfileOptionKind kind = ProfileOptionKind::Text;
    int minimum = 0, maximum = 0;
    std::vector<std::wstring> choices;
    bool hostCadence = false;
    bool Sensitive() const { return kind == ProfileOptionKind::SecretText || kind == ProfileOptionKind::OpaqueSecret; }
};
// Implemented beside the real presence grammar/defaults. No app, context,
// credential, transport or file operation occurs while querying/validating it.
std::vector<ProfileOptionDescriptor> ProfileOptions();
bool ValidateProfileFieldGrammar(const std::wstring& section, const std::wstring& key,
    const std::wstring& decodedValue, std::wstring& error);

struct ProfileDocument { std::wstring text; Profile profile; };
struct ProfileEditPolicy {
    bool allowPrivateSecretEdits = false;
    // The embedded service uses Options.refreshMs. The caller must map a saved
    // profile interval to its own source cadence; this API cannot mutate a host.
    bool acknowledgeHostCadenceMapping = false;
    bool requireExpectedText = false;
    std::wstring expectedText;
    DWORD lockWaitMs = 5000;
};
struct ProfileEditResult {
    Profile profile;
    bool changed = false;
    bool requiresHostCadenceApply = false;
    int requestedRefreshSeconds = 0;
};
bool ReadProfileDocument(const std::wstring& absolutePath, ProfileDocument& result, std::wstring& error);
bool ResolveProfileReference(const std::wstring& reference, const std::wstring& hostIniPath, std::wstring& absolutePath, std::wstring& error);
bool ParseProfileDocument(const std::wstring& text, Profile& result, std::wstring& error);
bool ReadProfileOptionForEditor(const Profile& profile, const ProfileOptionDescriptor& descriptor, std::wstring& value);
bool NormalizeProfileSetting(aip::IniSetting& decodedSetting, const ProfileEditPolicy& policy, std::wstring& error);
// decodedSettings contain editor/CLI text, not Discord's serialized escape
// layer. Only selected fields are re-encoded; opaque/untouched data is preserved.
bool ApplyProfileEditsToText(const std::wstring& original, const std::vector<aip::IniSetting>& decodedSettings,
    const ProfileEditPolicy& policy, std::wstring& candidate, ProfileEditResult& result, std::wstring& error);
bool CommitProfileEdits(const std::wstring& absolutePath, const std::vector<aip::IniSetting>& decodedSettings,
    const ProfileEditPolicy& policy, ProfileEditResult& result, std::wstring& error);
// Explicit setup only. Uses the real functional descriptor defaults and never
// replaces an existing file. Ordinary read/edit paths still cannot create one.
bool CreateProfileDocument(const std::wstring& absolutePath, const ProfileEditPolicy& policy,
    ProfileEditResult& result, std::wstring& error);
} }

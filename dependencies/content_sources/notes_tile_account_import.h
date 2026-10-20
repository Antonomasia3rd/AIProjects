#pragma once
// Pure staging for an explicit embedded sign-in import. No file, browser,
// account fetch, DPAPI operation, or caller mutation happens while planning.
#include "../tile_instances.h"
#include <map>

namespace aip { namespace notes {
struct TileNotesImportSetting { std::wstring section, key, value; };
struct TileNotesImportPlan {
    std::wstring accountSection;
    std::vector<TileNotesImportSetting> settings, removeKeys;
};
inline std::wstring TileNotesImportKey(std::wstring key) {
    key = aip::content::Lower(aip::content::Trim(key));
    key.erase(std::remove_if(key.begin(), key.end(), [](wchar_t ch) { return ch == L'_' || ch == L'-'; }), key.end());
    if (key == L"uid") return L"UID";
    if (key == L"server" || key == L"region") return L"Server";
    if (key == L"ltoken" || key == L"ltokenv2") return L"LToken";
    if (key == L"ltuid" || key == L"ltuidv2") return L"LTUid";
    if (key == L"ltokenprotected" || key == L"ltokenv2protected") return L"LTokenProtected";
    if (key == L"ltuidprotected" || key == L"ltuidv2protected") return L"LTUidProtected";
    if (key == L"refreshseconds" || key == L"refreshintervalseconds" || key == L"refreshinterval") return L"RefreshSeconds";
    return {};
}
template<class ImportedSettings> bool BuildTileNotesImportPlan(const std::wstring& id, const std::wstring& accountRef,
        const ImportedSettings& imported, TileNotesImportPlan& output, std::wstring& error) {
    using namespace aip::content;
    if (!ValidTileInstanceId(id) || !ValidTileAccountReference(aip::content::Trim(accountRef))) {
        error = L"The selected tile or account reference is invalid."; return false;
    }
    TileNotesImportPlan candidate;
    candidate.accountSection = aip::content::Trim(accountRef).empty() ? L"Account." + id : aip::content::Trim(accountRef);
    const bool shared = aip::content::Lower(candidate.accountSection).compare(0, 8, L"account.") == 0;
    const auto tileSection = TileInstanceSection(id);
    std::map<std::wstring, std::wstring> fields;
    for (const auto& setting : imported) {
        const auto key = TileNotesImportKey(setting.key);
        auto value = setting.value;
        if (key.empty()) { error = L"The imported account contains an unsupported field."; return false; }
        if (key == L"Server") {
            value = aip::content::Lower(aip::content::Trim(value));
            if (value.size() > 128 || std::any_of(value.begin(), value.end(), [](wchar_t ch) {
                return !((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'_');
            })) { error = L"The imported game server is invalid."; return false; }
        } else {
            std::wstring issue;
            if (!NormalizeSetting(L"Notes.Resin", key, value, issue)) { error = L"The imported account field is invalid (value not shown)."; return false; }
        }
        // Existing import plans clear aliases first and append their canonical
        // replacement values. Preserve that explicit ordered-write semantics.
        fields[key] = std::move(value);
    }
    if (fields[L"UID"].empty() || (fields[L"LToken"].empty() && fields[L"LTokenProtected"].empty()) ||
        (fields[L"LTUid"].empty() && fields[L"LTUidProtected"].empty())) {
        error = L"The import needs a game UID and both login fields (values not shown)."; return false;
    }
    // Explicit blanks hide inherited credentials. Remove local overrides and
    // aliases before applying the complete replacement account selection.
    for (const auto* key : {L"UID", L"Server", L"Region", L"LToken", L"LTUid", L"LTokenV2", L"LTuidV2", L"ltoken_v2", L"ltuid_v2",
            L"LTokenProtected", L"LTUidProtected", L"LTokenV2Protected", L"LTuidV2Protected", L"ltoken_v2_protected", L"ltuid_v2_protected"})
        candidate.removeKeys.push_back({tileSection, L"Notes." + std::wstring(key), L""});
    for (const auto* key : {L"LToken", L"LTUid", L"LTokenV2", L"LTuidV2", L"ltoken_v2", L"ltuid_v2",
            L"LTokenProtected", L"LTUidProtected", L"LTokenV2Protected", L"LTuidV2Protected", L"ltoken_v2_protected", L"ltuid_v2_protected"})
        candidate.removeKeys.push_back({candidate.accountSection, key, L""});
    // Write all canonical credential forms, including empty alternate forms,
    // so a previously protected/plain login cannot survive a replacement.
    for (const auto* key : {L"LToken", L"LTUid", L"LTokenProtected", L"LTUidProtected"}) fields.emplace(key, L"");
    for (const auto& field : fields) {
        const bool gameIdentity = field.first == L"UID" || field.first == L"Server";
        candidate.settings.push_back({shared && gameIdentity ? tileSection : candidate.accountSection,
            shared && gameIdentity ? L"Notes." + field.first : field.first, field.second});
    }
    if (fields.find(L"Server") == fields.end()) {
        candidate.settings.push_back({shared ? tileSection : candidate.accountSection, shared ? L"Notes.Server" : L"Server", L""});
    }
    candidate.settings.push_back({tileSection, L"Account", candidate.accountSection});
    output = std::move(candidate); error.clear(); return true;
}
}} // namespace aip::notes

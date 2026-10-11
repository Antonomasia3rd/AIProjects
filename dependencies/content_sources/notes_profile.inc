#pragma once
#include "notes.inc"
#include "../content_engine.h"
#include "../dpapi.inc"

namespace aip { namespace notes {
inline std::wstring HostAccountSection(int resource) { return L"Notes." + std::wstring(aip::content::NotesResources[resource].value); }
inline bool ParseHostResource(const std::wstring& value, int& result, bool allowAll = false) {
    for (int resource = 0; resource < 3; ++resource)
        if (aip::ToLower(aip::Trim(value)) == aip::ToLower(aip::content::NotesResources[resource].value)) { result = resource; return true; }
    if (allowAll && aip::ToLower(aip::Trim(value)) == L"all") { result = 3; return true; }
    return false;
}
inline std::vector<aip::IniSetting> ClearAccountSettings(int resource) {
    std::vector<aip::IniSetting> result;
    if (resource < 0 || resource >= 3) return result;
    for (const auto* key : {L"UID", L"LToken", L"LTUid", L"LTokenV2", L"LTuidV2", L"ltoken_v2", L"ltuid_v2", L"LTokenProtected", L"LTUidProtected", L"LTokenV2Protected", L"LTuidV2Protected"})
        result.push_back({HostAccountSection(resource), key, L""});
    return result;
}
inline bool ValidateAccountImport(int resource, std::map<std::wstring, std::wstring> fields,
    std::vector<aip::IniSetting>& result, std::wstring& error) {
    for (auto& field : fields) if (!aip::content::NormalizeSetting(HostAccountSection(resource), field.first, field.second, error)) return false;
    if (fields[L"UID"].empty() || (fields[L"LToken"].empty() && fields[L"LTokenProtected"].empty()) ||
        (fields[L"LTUid"].empty() && fields[L"LTUidProtected"].empty())) {
        error = L"The import requires UID and both account cookies (values are not shown)."; return false;
    }
    auto changes = ClearAccountSettings(resource);
    for (const auto& field : fields) changes.push_back({HostAccountSection(resource), field.first, field.second});
    result.insert(result.end(), changes.begin(), changes.end()); return true;
}
inline std::wstring CredentialField(std::wstring name) {
    name = aip::ToLower(aip::Trim(name));
    name.erase(std::remove_if(name.begin(), name.end(), [](wchar_t c) { return c == L'_' || c == L'-'; }), name.end());
    if (name == L"uid") return L"UID";
    if (name == L"ltoken" || name == L"ltokenv2") return L"LToken";
    if (name == L"ltuid" || name == L"ltuidv2") return L"LTUid";
    if (name == L"ltokenprotected" || name == L"ltokenv2protected") return L"LTokenProtected";
    if (name == L"ltuidprotected" || name == L"ltuidv2protected") return L"LTUidProtected";
    if (name == L"refreshintervalseconds" || name == L"refreshinterval" || name == L"refreshseconds") return L"RefreshSeconds";
    return {};
}
inline bool NormalizeLegacyRefresh(std::wstring& value, std::wstring& error) {
    int seconds = 0;
    if (!aip::content::ParseInt(value, 0, INT_MAX, seconds)) { error = L"The legacy refresh interval is not a nonnegative integer."; return false; }
    value = seconds ? std::to_wstring(ClampRefresh(static_cast<UINT>(seconds))) : L"0";
    return true;
}
inline bool ImportAccountProfile(const std::wstring& text, std::vector<aip::IniSetting>& output, std::wstring& error) {
    output.clear(); error.clear();
    // The baseline parser intentionally skips malformed assignments. An import
    // must not silently turn a damaged account line into a default. Reuse its
    // line/header/name helpers for this structural check; value and effective
    // repeated-section semantics still come entirely from ParseIniDocument.
    bool accountSection = false;
    const auto lines = aip::SplitIniLines(text);
    for (auto line : lines) {
        aip::StripLeadingIniBom(line);
        const auto trimmed = aip::Trim(line);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#') continue;
        std::wstring section;
        if (aip::TryParseIniSectionHeader(line, section)) {
            const auto name = aip::ToLower(section);
            int resource = -1;
            accountSection = (name.compare(0, 8, L"account.") == 0 && ParseHostResource(section.substr(8), resource)) ||
                (name.compare(0, 6, L"notes.") == 0 && ParseHostResource(section.substr(6), resource));
            continue;
        }
        if (trimmed.front() == L'[') { error = L"The import contains a malformed section header (values are not shown)."; return false; }
        const auto equal = line.find(L'=');
        if (accountSection && (equal == std::wstring::npos || aip::ParseIniName(line.substr(0, equal)).empty())) {
            error = L"The import contains a malformed account assignment (values are not shown)."; return false;
        }
    }
    const auto document = aip::ParseIniDocument(text);
    std::vector<aip::IniSetting> changes;
    int imported = 0;
    for (int resource = 0; resource < 3; ++resource) {
        const auto legacyName = L"Account." + std::wstring(aip::content::NotesResources[resource].value);
        const auto hostName = HostAccountSection(resource);
        const auto legacy = std::find_if(document.begin(), document.end(), [&](const auto& section) { return aip::IniEquals(section.name, legacyName); });
        const auto host = std::find_if(document.begin(), document.end(), [&](const auto& section) { return aip::IniEquals(section.name, hostName); });
        if (legacy == document.end() && host == document.end()) continue;
        const bool oldAccount = legacy != document.end();
        const auto sectionName = oldAccount ? legacyName : hostName;
        const auto read = [&](std::initializer_list<const wchar_t*> aliases) {
            std::wstring value;
            for (const auto* key : aliases) if (aip::ReadIniValueFromDoc(document, sectionName.c_str(), key, value) && !aip::Trim(value).empty()) return value;
            return std::wstring();
        };
        std::map<std::wstring, std::wstring> fields;
        fields[L"UID"] = read({L"UID"});
        fields[L"LToken"] = read({oldAccount ? L"LTokenV2" : L"LToken", oldAccount ? L"ltoken_v2" : L"LTokenV2", oldAccount ? L"LToken" : L"ltoken_v2"});
        fields[L"LTUid"] = read({oldAccount ? L"LTuidV2" : L"LTUid", oldAccount ? L"ltuid_v2" : L"LTuidV2", oldAccount ? L"LTUid" : L"ltuid_v2"});
        fields[L"LTokenProtected"] = read({oldAccount ? L"LTokenV2Protected" : L"LTokenProtected", oldAccount ? L"LTokenProtected" : L"LTokenV2Protected"});
        fields[L"LTUidProtected"] = read({oldAccount ? L"LTuidV2Protected" : L"LTUidProtected", oldAccount ? L"LTUidProtected" : L"LTuidV2Protected"});
        auto refresh = read({oldAccount ? L"RefreshIntervalSeconds" : L"RefreshSeconds", oldAccount ? L"RefreshSeconds" : L"RefreshIntervalSeconds", L"refresh_interval"});
        if (refresh.empty()) refresh = L"0";
        if (!NormalizeLegacyRefresh(refresh, error)) return false;
        fields[L"RefreshSeconds"] = refresh;
        if (!ValidateAccountImport(resource, fields, changes, error)) return false;
        ++imported;
    }
    if (!imported) { error = L"No supported account section was found in the selected profile."; return false; }
    std::wstring refresh;
    if (aip::ReadIniValueFromDoc(document, L"Settings", L"RefreshIntervalSeconds", refresh) && !aip::Trim(refresh).empty() && aip::Trim(refresh) != L"0") {
        if (!NormalizeLegacyRefresh(refresh, error)) return false;
        if (!aip::content::NormalizeSetting(L"Notes", L"RefreshSeconds", refresh, error)) return false;
        changes.push_back({L"Notes", L"RefreshSeconds", refresh});
    }
    output = std::move(changes); return true;
}
inline bool ImportCookieJson(int resource, const std::wstring& text, std::vector<aip::IniSetting>& output, std::wstring& error) {
    output.clear(); error.clear();
    const auto json = aip::WideToUtf8(text);
    if (resource < 0 || resource >= 3 || json.size() > 1048576 || !detail::ValidDocument(json)) { error = L"The cookie import is not a valid bounded JSON document."; return false; }
    std::size_t position = 0; detail::SkipWhitespace(json, position);
    if (position == json.size() || json[position++] != '{') { error = L"Cookie JSON must be an object."; return false; }
    std::map<std::wstring, std::wstring> fields;
    for (;;) {
        detail::SkipWhitespace(json, position);
        if (json[position] == '}') break;
        const auto keyEnd = aip::JsonStringEnd(json, position);
        std::string key;
        if (!aip::DecodeJsonStringUtf8Range(json, position, keyEnd, key)) { error = L"Invalid JSON property."; return false; }
        position = keyEnd; detail::SkipWhitespace(json, position); ++position; detail::SkipWhitespace(json, position);
        const auto begin = position;
        if (!detail::ValidateValue(json, position, 0)) { error = L"Invalid JSON value."; return false; }
        const auto canonical = CredentialField(aip::Utf8ToWide(key));
        if (!canonical.empty()) {
            std::wstring value;
            if (canonical == L"RefreshSeconds") {
                int number = 0;
                if (!detail::ParseAsciiInt(json.substr(begin, position - begin), number)) { error = L"The JSON refresh interval must be an integer."; return false; }
                value = std::to_wstring(number);
                if (!NormalizeLegacyRefresh(value, error)) return false;
            } else {
                std::string decoded;
                if (!aip::DecodeJsonStringUtf8Range(json, begin, position, decoded)) { error = L"JSON credentials must be string values (not shown)."; return false; }
                value = aip::Utf8ToWide(decoded);
            }
            if (!fields.emplace(canonical, value).second) { error = L"Cookie JSON contains duplicate credential or refresh aliases (not shown)."; return false; }
        }
        detail::SkipWhitespace(json, position);
        if (json[position] == '}') break;
        ++position;
    }
    fields.emplace(L"RefreshSeconds", L"0");
    std::vector<aip::IniSetting> changes;
    if (!ValidateAccountImport(resource, fields, changes, error)) return false;
    output = std::move(changes); return true;
}
inline bool ReadImportFile(const std::wstring& path, std::wstring& text, std::wstring& error) {
    std::vector<BYTE> bytes;
    if (!aip::ReadWholeFileBytes(path, bytes, 1048576)) { error = L"The selected import file is unavailable or exceeds 1 MiB."; return false; }
    const bool utf16 = bytes.size() >= 2 && ((bytes[0] == 0xff && bytes[1] == 0xfe) || (bytes[0] == 0xfe && bytes[1] == 0xff));
    if (utf16 && (bytes.size() - 2) % 2) { error = L"The import file has incomplete UTF-16 data."; return false; }
    if (!utf16) {
        const std::size_t offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        if (!aip::TryUtf8ToWide(std::string(bytes.begin() + offset, bytes.end()), text)) { error = L"The import file has invalid UTF-8."; return false; }
    } else if (!aip::DecodeTextBytes(bytes, text)) { error = L"The import file has invalid UTF-16."; return false; }
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto ch = text[index];
        if (!ch || (ch >= 0xdc00 && ch <= 0xdfff) || (ch >= 0xd800 && ch <= 0xdbff &&
            (++index == text.size() || text[index] < 0xdc00 || text[index] > 0xdfff))) { error = L"The import file contains invalid text."; return false; }
    }
    return true;
}
using CredentialDecoder = std::function<bool(const std::wstring&, std::wstring&)>;
inline bool ResolveStoredAccount(const aip::content::NotesAccount& saved, Account& result, std::wstring& error, const CredentialDecoder& decoder) {
    result = {}; error.clear();
    if (saved.uid.empty() || (saved.token.empty() && saved.tokenProtected.empty()) || (saved.userId.empty() && saved.userIdProtected.empty())) {
        error = L"Configure the selected game's UID and account cookies."; return false;
    }
    std::wstring token = saved.token, user = saved.userId;
    try {
        if ((!saved.tokenProtected.empty() && (!decoder || !decoder(saved.tokenProtected, token))) ||
            (!saved.userIdProtected.empty() && (!decoder || !decoder(saved.userIdProtected, user)))) {
            error = L"The saved protected account cookies could not be opened for this user."; return false;
        }
        result.uid = aip::WideToUtf8(saved.uid); result.ltoken = aip::WideToUtf8(token); result.ltuid = aip::WideToUtf8(user);
        return true;
    } catch (...) { error = L"The saved account cookies could not be resolved (values are not shown)."; return false; }
}
inline bool DecodeNativeCredential(const std::wstring& value, std::wstring& plain) {
    try { plain = aip::UnprotectSecretForCurrentUser(value, aip::DpapiLegacyEncoding::Utf16LittleEndian); return true; }
    catch (...) { plain.clear(); return false; }
}
} }

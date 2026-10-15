#pragma once
#include "notes.inc"
#include "notes_signin_model.h"

namespace aip { namespace notes {
struct BoundGameAccount {
    std::wstring uid, server, nickname, serverName;
};
inline bool BoundGameResponseList(const std::string& body, std::string& list, std::wstring& error) {
    int status = -1; std::string data;
    if (body.size() > 1048576 || !detail::ValidDocument(body) || !detail::JsonInt(body, "retcode", status) || status != 0 ||
        !detail::JsonObject(body, "data", data) || !detail::JsonArray(data, "list", list)) {
        error = L"HoYoLAB could not list accounts. Complete sign-in or enter the game's UID manually."; return false;
    }
    return true;
}
inline bool ParseBoundGameRegions(ResourceKind resource, const std::string& body,
    std::vector<std::wstring>& output, std::wstring& error) {
    output.clear(); error.clear(); std::string list;
    if (!BoundGameResponseList(body, list, error)) return false;
    std::size_t position = 1, count = 0; std::vector<std::wstring> staged;
    for (;;) {
        detail::SkipWhitespace(list, position); if (position >= list.size() || list[position] == ']') break;
        const auto end = aip::JsonValueEnd(list, position); std::string server;
        if (++count > 8 || end == std::string::npos || list[position] != '{' ||
            !detail::JsonString(list.substr(position, end - position), "region", server) ||
            !ValidAccountServer(resource, detail::Utf8ToWide(server))) {
            error = L"HoYoLAB returned unsupported game regions. Use manual UID entry."; return false;
        }
        const auto region = detail::Utf8ToWide(server);
        if (std::find(staged.begin(), staged.end(), region) == staged.end()) staged.push_back(region);
        position = end; detail::SkipWhitespace(list, position); if (list[position] == ',') ++position;
    }
    output = std::move(staged); return true;
}
inline bool ParseBoundGameAccounts(ResourceKind resource, const std::string& body,
    std::vector<BoundGameAccount>& output, std::wstring& error) {
    output.clear(); error.clear();
    const int game = static_cast<int>(resource);
    if (game < 0 || game >= 3 || body.size() > 1048576 || !detail::ValidDocument(body)) {
        error = L"The game-account response is invalid or exceeds its limit."; return false;
    }
    std::string list;
    if (!BoundGameResponseList(body, list, error)) return false;
    std::vector<BoundGameAccount> staged;
    std::size_t position = 1, count = 0;
    for (;;) {
        detail::SkipWhitespace(list, position);
        if (position >= list.size() || list[position] == ']') break;
        if (++count > 64) { error = L"Too many game accounts were returned."; return false; }
        const auto end = aip::JsonValueEnd(list, position);
        if (end == std::string::npos || list[position] != '{') { error = L"Invalid game-account entry."; return false; }
        const auto item = list.substr(position, end - position);
        std::string uid, server, name, serverName, business;
        if (detail::JsonString(item, "game_biz", business) && business != aip::WideToUtf8(SignInGames[game].business)) {
            position = end; detail::SkipWhitespace(list, position); if (list[position] == ',') ++position; continue;
        }
        if (!detail::JsonString(item, "game_uid", uid) || !detail::JsonString(item, "region", server) ||
            uid.empty() || uid.size() > 32 || !std::all_of(uid.begin(), uid.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
            !ValidAccountServer(resource, detail::Utf8ToWide(server))) {
            error = L"A game account has an unsupported UID or server. Use the manual fallback."; return false;
        }
        detail::JsonString(item, "nickname", name); detail::JsonString(item, "region_name", serverName);
        if (name.size() > 512 || serverName.size() > 256) { error = L"A game-account label exceeds its limit."; return false; }
        BoundGameAccount account{detail::Utf8ToWide(uid), detail::Utf8ToWide(server), detail::Utf8ToWide(name), detail::Utf8ToWide(serverName)};
        for (auto* label : {&account.nickname, &account.serverName})
            for (auto& c : *label) if (c < 0x20 || c == 0x7f) c = L' ';
        const auto duplicate = std::find_if(staged.begin(), staged.end(), [&](const auto& entry) { return entry.uid == account.uid && entry.server == account.server; });
        if (duplicate == staged.end()) staged.push_back(std::move(account));
        position = end; detail::SkipWhitespace(list, position); if (list[position] == ',') ++position;
    }
    output = std::move(staged); return true;
}
inline bool BuildBoundGameAccountsRequest(ResourceKind resource, const std::wstring& token, const std::wstring& user,
    bool v2, aip::HttpFetchOptions& request, std::wstring& error) {
    request = {}; error.clear();
    const int game = static_cast<int>(resource);
    const auto cookieOk = [](const std::wstring& value) { return !value.empty() && value.size() <= 4096 &&
        std::none_of(value.begin(), value.end(), [](wchar_t c) { return c <= 0x20 || c == 0x7f || c == L';'; }); };
    if (game < 0 || game >= 3 || !cookieOk(token) || !cookieOk(user)) {
        error = L"Complete HoYoLAB sign-in before finding game accounts."; return false;
    }
    // Verified in the official Battle Chronicle role-selector JS on 2026-10-15.
    // No cookie-token endpoint, token exchange, logout, or account mutation.
    request.url = L"https://api-account-os.hoyolab.com/account/binding/api/getAllRegions?game_biz=" + std::wstring(SignInGames[game].business);
    request.userAgent = L"DesktopStub/NotesSignIn";
    request.accept = L"application/json"; request.timeout = std::chrono::milliseconds(15000); request.maximumBytes = 1048576;
    request.headers = {{L"x-rpc-language", L"en-us"}, {L"Origin", L"https://act.hoyolab.com"}, {L"Referer", L"https://act.hoyolab.com/"},
        {L"Cookie", (v2 ? L"ltoken_v2=" : L"ltoken=") + token + (v2 ? L"; ltuid_v2=" : L"; ltuid=") + user}};
    std::wstring ignored; return aip::http_detail::BuildRequestHeaders(request, ignored, error);
}
inline bool FetchBoundGameAccounts(const aip::HttpFetchOptions& request, ResourceKind resource,
    std::vector<BoundGameAccount>& output, std::wstring& error, const HttpTransport& transport = aip::FetchHttpBytes) {
    output.clear(); error.clear();
    try {
        const int game = static_cast<int>(resource);
        if (game < 0 || game >= 3 || request.url != L"https://api-account-os.hoyolab.com/account/binding/api/getAllRegions?game_biz=" + std::wstring(SignInGames[game].business) ||
            request.maximumBytes != 1048576 || request.timeout.count() < 1 || request.timeout.count() > 15000 ||
            (request.canceled && request.canceled())) { error = L"Game-account lookup was canceled or has invalid limits."; return false; }
        const auto deadline = std::chrono::steady_clock::now() + request.timeout;
        std::size_t remaining = request.maximumBytes;
        const auto fetch = [&](aip::HttpFetchOptions next, std::string& body) {
            const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (time.count() < 1 || !remaining || (request.canceled && request.canceled())) {
                error = L"Game-account lookup was canceled or timed out."; return false;
            }
            next.timeout = time; next.maximumBytes = remaining;
            aip::HttpFetchResponse response;
            if (!transport(next, response, error) || (request.canceled && request.canceled()) ||
                response.statusCode < 200 || response.statusCode >= 300) {
                error = L"Game-account lookup failed. Retry after sign-in or use manual UID entry."; return false;
            }
            if (response.body.size() > remaining) { error = L"The game-account response exceeded its total byte limit."; return false; }
            remaining -= response.body.size(); body.assign(response.body.begin(), response.body.end()); return true;
        };
        std::string body;
        if (!fetch(request, body)) return false;
        std::vector<std::wstring> regions;
        if (!ParseBoundGameRegions(resource, body, regions, error)) return false;
        std::vector<BoundGameAccount> staged;
        for (const auto& region : regions) {
            auto next = request;
            next.url = L"https://api-account-os.hoyolab.com/binding/api/getUserGameRolesByLtoken?game_biz=" + std::wstring(SignInGames[game].business) + L"&region=" + region;
            if (!fetch(next, body)) return false;
            std::vector<BoundGameAccount> accounts;
            if (!ParseBoundGameAccounts(resource, body, accounts, error)) return false;
            for (auto& account : accounts) {
                if (account.server != region) { error = L"HoYoLAB returned an unexpected account server. No account was selected."; return false; }
                const auto duplicate = std::find_if(staged.begin(), staged.end(), [&](const auto& item) { return item.uid == account.uid && item.server == account.server; });
                if (duplicate == staged.end()) staged.push_back(std::move(account));
                if (staged.size() > 64) { error = L"Too many game accounts were returned."; return false; }
            }
        }
        if ((request.canceled && request.canceled()) || std::chrono::steady_clock::now() > deadline) { error = L"Game-account lookup was canceled or timed out."; return false; }
        output = std::move(staged); return true;
    } catch (...) { output.clear(); error = L"Game-account lookup failed. No account was saved."; return false; }
}
} }

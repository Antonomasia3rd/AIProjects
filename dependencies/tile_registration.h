#pragma once
// Pure per-user/per-profile/per-tile registration identity and manifest plan.
// Executables, images, deployment and Start shortcuts are owned by host adapters.
#include "tile_appearance.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace aip { namespace tile_registration {
inline constexpr const wchar_t* LauncherFileName = L"DesktopStubTileLauncher.exe";
struct Target {
    std::wstring userScope, profileScope, tileScope, profile, tileId;
    std::wstring identityName, applicationId = L"Tile", root, manifestPath, assetsDirectory, launcherPath;
    std::wstring hostAumid, hostArguments, hostPath, hostProduct = L"DesktopStub", displayName, publisher, publisherDisplayName;
    std::wstring packageVersion = L"1.0.0.1", backgroundColor = L"transparent", branding = L"Name";
    bool hideMediumName=false;
};
inline bool ScopeKey(const std::wstring& key) {
    return key.size() == 16 && std::all_of(key.begin(),key.end(),[](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
    });
}
inline std::wstring IdentityName(const std::wstring& user, const std::wstring& profile, const std::wstring& tile) {
    if (!ScopeKey(user) || !ScopeKey(profile) || !ScopeKey(tile)) return {};
    auto value = L"aip.tile." + user.substr(0,12) + L"." + profile.substr(0,12) + L"." + tile.substr(0,12);
    std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c) { return c >= L'A' && c <= L'F' ? static_cast<wchar_t>(c + (L'a'-L'A')) : c; });
    return value; // 47 characters, below the native package-name limit of 50.
}
inline bool Text(const std::wstring& value, std::size_t maximum, bool empty = false) {
    return (empty || !value.empty()) && value.size() <= maximum && std::none_of(value.begin(),value.end(),[](wchar_t c) { return c < 0x20 || c == 0x7f; });
}
inline std::wstring Escape(const std::wstring& value) {
    std::wstring out;
    for (const auto c : value) switch (c) {
    case L'&': out += L"&amp;"; break; case L'<': out += L"&lt;"; break; case L'>': out += L"&gt;"; break;
    case L'\"': out += L"&quot;"; break; case L'\'': out += L"&apos;"; break; default: out += c; break;
    }
    return out;
}
inline std::wstring Quote(const std::wstring& value) {
    std::wstring out = L"\""; std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1,L'\\'); else out.append(slashes,L'\\');
        out += c; slashes = 0;
    }
    out.append(slashes * 2,L'\\'); out += L'\"'; return out;
}
inline bool Aumid(const std::wstring& value) {
    const auto split = value.find(L'!');
    return split != std::wstring::npos && split > 0 && split + 1 < value.size() && value.size() <= 128 &&
        value.find(L'!',split+1) == std::wstring::npos && std::all_of(value.begin(),value.end(),[](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L'_' || c == L'!';
        });
}
inline std::wstring LauncherArguments(const Target& target) {
    auto args = L"--host " + Quote(target.hostPath) + L" --profile " + Quote(target.profile) + L" --tile-id " + Quote(target.tileId) +
        L" --product " + Quote(target.hostProduct);
    if (!target.hostAumid.empty()) args += L" --activate-aumid " + Quote(target.hostAumid);
    return args;
}
inline bool Version(const std::wstring& value) {
    std::size_t start = 0;
    for (unsigned part = 0; part < 4; ++part) {
        const auto end = value.find(L'.',start), finish = end == std::wstring::npos ? value.size() : end;
        if (finish == start || finish-start > 5) return false;
        unsigned number = 0;
        for (auto i = start; i < finish; ++i) { if (value[i] < L'0' || value[i] > L'9') return false; number = number*10+value[i]-L'0'; }
        if (number > 65535 || (part == 3 ? end != std::wstring::npos : end == std::wstring::npos)) return false;
        start = finish+1;
    }
    return true;
}
inline std::wstring RevisionVersion(std::uint32_t revision) {
    return L"1.0." + std::to_wstring(revision >> 16) + L"." + std::to_wstring(revision & 0xffffu);
}
inline bool BuildManifest(const Target& target, std::wstring& output, std::wstring& error) {
    const auto expected = IdentityName(target.userScope,target.profileScope,target.tileScope);
    if (expected.empty() || target.identityName != expected || target.applicationId != L"Tile" || !Version(target.packageVersion) ||
        !Text(target.displayName,256) || !Text(target.publisher,8192) || !Text(target.publisherDisplayName,256) ||
        (!target.hostAumid.empty() && !Aumid(target.hostAumid)) || !Text(target.hostArguments,16000) ||
        !Text(target.hostPath,32767) || !Text(target.profile,32767) || !Text(target.hostProduct,128) || LauncherArguments(target).size() > 32767) {
        error = L"Registration tile identity, display text, version or activation arguments are invalid."; return false;
    }
    aip::content::TileAppearance appearance; appearance.background = target.backgroundColor == L"transparent" ? L"Transparent" : L"Color";
    appearance.backgroundColor = target.backgroundColor == L"transparent" ? L"Auto" : target.backgroundColor;
    appearance.branding = target.branding;
    auto color = appearance.backgroundColor, branding = appearance.branding;
    if (!aip::content::NormalizeTileAppearanceSetting(L"BackgroundColor",color,error) ||
        !aip::content::NormalizeTileAppearanceSetting(L"Branding",branding,error)) return false;
    std::wstring xml = L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        L"<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\" "
        L"xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\" "
        L"xmlns:uap10=\"http://schemas.microsoft.com/appx/manifest/uap/windows10/10\" "
        L"xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\" IgnorableNamespaces=\"uap uap10 rescap\">\r\n";
    xml += L"<Identity Name=\""+Escape(target.identityName)+L"\" Publisher=\""+Escape(target.publisher)+L"\" Version=\""+Escape(target.packageVersion)+L"\" ProcessorArchitecture=\"x64\"/>\r\n";
    xml += L"<Properties><DisplayName>"+Escape(target.displayName)+L"</DisplayName><PublisherDisplayName>"+Escape(target.publisherDisplayName)+L"</PublisherDisplayName><Logo>Assets\\StoreLogo.png</Logo></Properties>\r\n";
    xml += L"<Dependencies><TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.19041.0\" MaxVersionTested=\"10.0.26100.0\"/></Dependencies><Resources><Resource Language=\"en-us\"/></Resources>\r\n";
    xml += L"<Applications><Application Id=\"Tile\" Executable=\""+std::wstring(LauncherFileName)+L"\" EntryPoint=\"Windows.FullTrustApplication\" uap10:Parameters=\""+Escape(LauncherArguments(target))+L"\">\r\n";
    xml += L"<uap:VisualElements DisplayName=\""+Escape(target.displayName)+L"\" Description=\""+Escape(target.displayName)+L"\" Square150x150Logo=\"Assets\\MediumTile.png\" Square44x44Logo=\"Assets\\Square44x44Logo.png\" BackgroundColor=\""+Escape(target.backgroundColor)+L"\">\r\n";
    xml += L"<uap:DefaultTile Square71x71Logo=\"Assets\\SmallTile.png\" Wide310x150Logo=\"Assets\\WideTile.png\" Square310x310Logo=\"Assets\\LargeTile.png\">";
    if (branding == L"Name" || branding == L"NameAndLogo" || branding == L"Auto"){
        xml += L"<uap:ShowNameOnTiles>";
        if(!target.hideMediumName)xml+=L"<uap:ShowOn Tile=\"square150x150Logo\"/>";
        xml+=L"<uap:ShowOn Tile=\"wide310x150Logo\"/><uap:ShowOn Tile=\"square310x310Logo\"/></uap:ShowNameOnTiles>";
    }
    xml += L"</uap:DefaultTile></uap:VisualElements></Application></Applications><Capabilities><rescap:Capability Name=\"runFullTrust\"/></Capabilities></Package>\r\n";
    output = std::move(xml); error.clear(); return true;
}
}} // namespace aip::tile_registration

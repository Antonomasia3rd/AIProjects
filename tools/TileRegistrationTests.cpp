#include "../dependencies/tile_registration.h"
#include "../dependencies/tile_registration_binding.h"
#include <iostream>
namespace {
unsigned checks = 0, failures = 0;
void Check(bool value,const char* message) { ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
void TestManifest() {
    using namespace aip::tile_registration;
    Target target; target.userScope = L"0123456789ABCDEF"; target.profileScope = L"1111111111111111"; target.tileScope = L"2222222222222222";
    target.identityName = IdentityName(target.userScope,target.profileScope,target.tileScope);
    target.profile = L"C:\\private profile\\settings.ini"; target.tileId = L"rss-one"; target.hostPath = L"D:\\app\\DesktopStub.exe";
    target.hostAumid = L"Main.Identity_123456789abcd!App"; target.hostArguments = L"--ini \"C:\\private profile\\settings.ini\" --tile-setup rss-one";
    target.publisher = L"CN=DesktopStub"; target.publisherDisplayName = L"DesktopStub"; target.displayName = L"RSS <&> news";
    Check(target.identityName.size() == 47 && target.identityName.find(L"0123456789ab") != std::wstring::npos,"stable registration package identity fits the native name limit");
    Check(target.identityName != IdentityName(L"9999999999999999",target.profileScope,target.tileScope),"two users sharing an INI path receive separate registration identities");
    Check(target.identityName != IdentityName(target.userScope,L"9999999999999999",target.tileScope),"separate profiles receive separate registration identities");
    Check(target.identityName != IdentityName(target.userScope,target.profileScope,L"9999999999999999"),"separate tiles receive separate registration identities");
    Check(IdentityName(L"not-a-hash",target.profileScope,target.tileScope).empty(),"identity builder refuses an unscoped or malformed user identity");
    std::wstring xml,error;
    Check(BuildManifest(target,xml,error),"build a complete independent full-trust static tile manifest without deployment");
    Check(xml.find(L"RSS &lt;&amp;&gt; news") != std::wstring::npos&&xml.find(L"uap10:")==std::wstring::npos&&
        xml.find(L"MinVersion=\"10.0.15063.0\"")!=std::wstring::npos&&xml.find(target.profile)==std::wstring::npos,
        "manifest escapes display text and supports 1809 without newer activation attributes or embedded profile arguments");
    const auto expectedBinding=ActivationBindingForTarget(target);std::wstring encoded;
    ActivationBinding parsed;
    Check(WriteActivationBinding(expectedBinding,encoded,error)&&ReadActivationBinding(encoded,parsed,error)&&
        parsed.profile==target.profile&&parsed.hostAumid==target.hostAumid&&parsed.tileId==target.tileId&&parsed.publisher==target.publisher,
        "separate bounded activation record retains the exact canonical profile host tile and package binding");
    Check(encoded.find(L"--host")==std::wstring::npos&&encoded.find(L"AIP_TILE_ACTIVATION_V1")==0,
        "activation metadata is versioned data rather than an evaluated command line");
    auto unpackaged=expectedBinding;unpackaged.hostAumid.clear();std::wstring unpackagedText;ActivationBinding unpackagedRead;
    Check(WriteActivationBinding(unpackaged,unpackagedText,error)&&ReadActivationBinding(unpackagedText,unpackagedRead,error)&&unpackagedRead.hostAumid.empty(),
        "an explicitly absent main AUMID preserves the existing unpackaged-host fallback without a fabricated package identity");
    const auto rejectBinding=[&](const std::wstring& candidate){ActivationBinding kept=parsed;return !ReadActivationBinding(candidate,kept,error)&&kept.profile==parsed.profile;};
    Check(rejectBinding(encoded+L"extra\r\n"),"activation record refuses trailing fields without replacing a previous valid result");
    Check(rejectBinding(encoded.substr(0,encoded.size()-1)),"activation record requires a complete terminal line");
    Check(rejectBinding(L"AIP_TILE_ACTIVATION_V2\r\n"+encoded.substr(encoded.find(L'\n')+1)),"unknown activation record versions fail closed");
    Check(rejectBinding(std::wstring(MaximumActivationCharacters+1,L'x')),"activation records have a fixed character limit");
    auto bad=expectedBinding;bad.profile=L"\\\\server\\private\\settings.ini";
    Check(!WriteActivationBinding(bad,encoded,error),"no-argument legacy activation refuses network profile paths");
    bad=expectedBinding;bad.identityName=L"foreign.package";Check(!WriteActivationBinding(bad,encoded,error),"activation package name cannot drift from its three scopes");
    bad=expectedBinding;bad.tileId=L"../../other";Check(!WriteActivationBinding(bad,encoded,error),"activation tile ID cannot become a path");
    bad=expectedBinding;bad.hostProduct+=L"\r\nextra";Check(!WriteActivationBinding(bad,encoded,error),"activation values cannot insert extra records");
    Check(xml.find(L"Executable=\"DesktopStubTileLauncher.exe\"") != std::wstring::npos && xml.find(L"Executable=\"D:") == std::wstring::npos &&
        xml.find(L"AllowExternalContent") == std::wstring::npos,"package uses its tiny launcher rather than an invalid external host executable or sparse identity mismatch");
    Check(xml.find(L"Startup") == std::wstring::npos && xml.find(L"BackgroundColor=\"transparent\"") != std::wstring::npos &&
        xml.find(L"ForegroundText") == std::wstring::npos,"static delivery preserves transparency and adds no Startup entry or invalid Windows 10 foreground attribute");
    for (const auto* image : {L"StoreLogo.png",L"MediumTile.png",L"Square44x44Logo.png",L"SmallTile.png",L"WideTile.png",L"LargeTile.png"})
        Check(xml.find(image) != std::wstring::npos,"manifest references the ordinary shared renderer's static tile asset names");
    target.branding = L"None";
    Check(BuildManifest(target,xml,error) && xml.find(L"ShowNameOnTiles") == std::wstring::npos,"static tile name branding can be hidden independently");
    target.backgroundColor = L"#ABCDEF"; target.branding = L"Name";
    Check(BuildManifest(target,xml,error) && xml.find(L"BackgroundColor=\"#ABCDEF\"") != std::wstring::npos,"static tile explicit background color is preserved");
    const auto identity = target.identityName; target.displayName = L"Renamed feed";
    Check(BuildManifest(target,xml,error) && target.identityName == identity,"renaming a registration tile never changes its stable app identity");
    for (const auto revision : {0u,1u,65535u,65536u,0xffffffffu}) Check(Version(RevisionVersion(revision)),"all persisted update revisions map to a valid four-part native package version");
    const auto valid = target;
    const auto rejected = [&](const Target& candidate) { auto retained = std::wstring(L"keep previous manifest"); return !BuildManifest(candidate,retained,error) && retained == L"keep previous manifest"; };
    target.identityName = L"foreign.package"; Check(rejected(target),"manifest identity cannot drift from its user/profile/tile scopes");
    target = valid; target.packageVersion = L"1.0.99999.0"; Check(rejected(target),"manifest refuses out-of-range package version fields");
    target = valid; target.hostAumid = L"file:///not-an-app"; Check(rejected(target),"launcher only receives a validated canonical app identity");
    target = valid; target.displayName.clear(); Check(rejected(target),"empty app display text cannot create an invalid package");
    target = valid; target.publisher += L"\r\nInjected"; Check(rejected(target),"manifest values cannot inject additional XML through control characters");
}
}
int main() { TestManifest(); std::cout << checks << " registration tile model checks; " << failures << " failures\n"; return failures ? 1 : 0; }

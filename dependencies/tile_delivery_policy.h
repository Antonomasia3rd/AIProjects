#pragma once
// Captured per-tile rendering/transport policy. Portable and inert: no Windows,
// filesystem, package registration, native resources or mutable process state.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

namespace aip { namespace content {
enum class TilePolicyValueKind { Boolean, Integer, Choice, Directory };
struct TilePolicyField {
    const wchar_t* key;
    const wchar_t* section;
    const wchar_t* setting;
    const wchar_t* fallback;
    const wchar_t* label;
    TilePolicyValueKind kind;
    int minimum = 0, maximum = 0;
    const wchar_t* choices = L"";
};
inline constexpr TilePolicyField TilePolicyFields[] = {
    {L"Artwork.IconFont",L"Settings",L"TileIconFont",L"Auto",L"Icon font (Auto follows Windows version)",TilePolicyValueKind::Choice,0,0,L"Auto|MDL2|Fluent|Vector"},
    {L"Delivery.PrimaryRegistration",L"Settings",L"PrimaryRegistration",L"Separate",L"Primary Desktop Registration target",TilePolicyValueKind::Choice,0,0,L"Main|Separate"},
    {L"Delivery.DisabledWarning",L"Settings",L"LiveTileDisabledWarning",L"Startup",L"Warn when Windows disables this tile's updates",TilePolicyValueKind::Choice,0,0,L"Off|Startup|OnChange"},
    {L"Delivery.DisabledCheckSeconds",L"Settings",L"LiveTileDisabledCheckSeconds",L"300",L"Disabled-update check interval (seconds)",TilePolicyValueKind::Integer,30,86400},
    {L"Delivery.Mode",L"Settings",L"ExperimentalLiveTileUpdate",L"Auto",L"Tile delivery",TilePolicyValueKind::Choice,0,0,L"Auto|LiveTile|Registration"},
    {L"Delivery.Template",L"Settings",L"LiveTileTemplateStyle",L"Adaptive",L"Native notification template",TilePolicyValueKind::Choice,0,0,L"Adaptive|Windows81Preset"},
    {L"Delivery.ClearOnShutdown",L"Settings",L"ClearLiveTileOnShutdown",L"1",L"Clear this tile's native notifications on exit",TilePolicyValueKind::Boolean},
    {L"Delivery.ManifestTarget",L"Settings",L"AppxManifestTarget",L"Windows10",L"Windows manifest compatibility target",TilePolicyValueKind::Choice,0,0,L"Windows10|Windows81|Windows8"},
    {L"Delivery.UsePowerShell",L"Settings",L"UsePowerShell",L"0",L"Use PowerShell registration",TilePolicyValueKind::Boolean},
    {L"Delivery.ComUseHelper",L"Settings",L"ComRegistrationUseHelper",L"0",L"Isolate COM registration in a helper",TilePolicyValueKind::Boolean},
    {L"Delivery.ComTimeoutMs",L"Settings",L"ComRegistrationTimeoutMs",L"120000",L"COM registration timeout (milliseconds, 0=unbounded)",TilePolicyValueKind::Integer,0,600000},
    {L"Delivery.ComCancelWaitMs",L"Settings",L"ComRegistrationCancelWaitMs",L"5000",L"COM cancellation wait (milliseconds)",TilePolicyValueKind::Integer,0,60000},
    {L"Delivery.PowerShellExe",L"Settings",L"PowerShellExe",L"",L"PowerShell executable override (blank=system default)",TilePolicyValueKind::Directory},
    {L"Delivery.PowerShellTimeoutMs",L"Settings",L"PowerShellTimeoutMs",L"120000",L"PowerShell timeout (milliseconds, 0=unbounded)",TilePolicyValueKind::Integer,0,600000},
    {L"Delivery.PowerShellPollMs",L"Settings",L"PowerShellPollMs",L"50",L"PowerShell poll interval (milliseconds)",TilePolicyValueKind::Integer,10,1000},
    {L"Delivery.PowerShellTerminateWaitMs",L"Settings",L"PowerShellTerminateWaitMs",L"5000",L"PowerShell termination wait (milliseconds)",TilePolicyValueKind::Integer,0,60000},
    {L"Delivery.PowerShellOutputLimitBytes",L"Settings",L"PowerShellOutputLimitBytes",L"4194304",L"PowerShell output limit (bytes, 0=unbounded)",TilePolicyValueKind::Integer,0,67108864},
    {L"Delivery.UnresolvedBlockMs",L"Settings",L"RegistrationUnresolvedBlockMs",L"300000",L"Unresolved registration safety wait (milliseconds)",TilePolicyValueKind::Integer,0,3600000},
    {L"Cache.Enabled",L"Settings",L"GeneratedAssetCache",L"1",L"Reuse generated image assets",TilePolicyValueKind::Boolean},
    {L"Cache.MaxEntries",L"Settings",L"GeneratedAssetCacheMaxEntries",L"32",L"Cached versions to keep (0-4096)",TilePolicyValueKind::Integer,0,4096},
    {L"Cache.Precache",L"Settings",L"GeneratedAssetPrecache",L"0",L"Pre-cache slideshow siblings",TilePolicyValueKind::Boolean},
    {L"Cache.PrecacheMaxFiles",L"Settings",L"GeneratedAssetPrecacheMaxFiles",L"16",L"Slideshow files to pre-cache (0-256)",TilePolicyValueKind::Integer,0,256},
    {L"Cache.Directory",L"Settings",L"GeneratedAssetCacheDirectory",L"",L"Cache parent folder (blank uses application cache)",TilePolicyValueKind::Directory},
    {L"Assets.ScaleAuto",L"Settings",L"GenerateScaleAuto",L"1",L"Use current display scale automatically",TilePolicyValueKind::Boolean},
    {L"Assets.PngReplaceRetries",L"Settings",L"SavePngReplaceRetries",L"3",L"PNG replace retries",TilePolicyValueKind::Integer,0,20},
    {L"Assets.PngReplaceRetryDelayMs",L"Settings",L"SavePngReplaceRetryDelayMs",L"75",L"PNG replace retry delay (milliseconds)",TilePolicyValueKind::Integer,0,5000},
    {L"Assets.Scale100",L"Settings",L"GenerateScale100",L"1",L"100% scale assets",TilePolicyValueKind::Boolean},
    {L"Assets.Scale125",L"Settings",L"GenerateScale125",L"1",L"125% scale assets",TilePolicyValueKind::Boolean},
    {L"Assets.Scale150",L"Settings",L"GenerateScale150",L"1",L"150% scale assets",TilePolicyValueKind::Boolean},
    {L"Assets.Scale200",L"Settings",L"GenerateScale200",L"0",L"200% scale assets",TilePolicyValueKind::Boolean},
    {L"Assets.Scale400",L"Settings",L"GenerateScale400",L"0",L"400% scale assets",TilePolicyValueKind::Boolean},
    {L"Assets.StoreLogo",L"Assets",L"StoreLogo",L"0",L"Store logo asset",TilePolicyValueKind::Boolean},
    {L"Assets.MediumTile",L"Assets",L"MediumTile",L"1",L"Medium tile asset",TilePolicyValueKind::Boolean},
    {L"Assets.Square44x44Logo",L"Assets",L"Square44x44Logo",L"0",L"44-pixel logo asset",TilePolicyValueKind::Boolean},
    {L"Assets.Square30x30Logo",L"Assets",L"Square30x30Logo",L"0",L"30-pixel logo asset",TilePolicyValueKind::Boolean},
    {L"Assets.SmallTile",L"Assets",L"SmallTile",L"0",L"Small tile asset",TilePolicyValueKind::Boolean},
    {L"Assets.WideTile",L"Assets",L"WideTile",L"1",L"Wide tile asset",TilePolicyValueKind::Boolean},
    {L"Assets.LargeTile",L"Assets",L"LargeTile",L"1",L"Large tile asset",TilePolicyValueKind::Boolean},
    {L"Assets.SplashScreen",L"Assets",L"SplashScreen",L"0",L"Splash screen asset",TilePolicyValueKind::Boolean},
    {L"Assets.GenerateIconWhenDisabled",L"Settings",L"GenerateDesktopIconForDisabledEntries",L"1",L"Use a topic logo for disabled image sizes",TilePolicyValueKind::Boolean},
    {L"Assets.DeleteDisabled",L"Settings",L"DeleteDisabledAssets",L"0",L"Delete this tile's disabled generated assets",TilePolicyValueKind::Boolean},
    {L"Text.ApplyToMediumTile",L"TileText",L"ApplyToMediumTile",L"1",L"Show text on Medium tiles",TilePolicyValueKind::Boolean},
    {L"Text.ApplyToWideTile",L"TileText",L"ApplyToWideTile",L"1",L"Show text on Wide tiles",TilePolicyValueKind::Boolean},
    {L"Text.ApplyToLargeTile",L"TileText",L"ApplyToLargeTile",L"1",L"Show text on Large tiles",TilePolicyValueKind::Boolean},
    {L"Wallpaper.Fit",L"Settings",L"TileWallpaperFit",L"Auto",L"Image fitting",TilePolicyValueKind::Choice,0,0,L"Auto|Fill|Fit|Stretch|Center|Tile|Span"},
    {L"Wallpaper.DisableFitting",L"Settings",L"DisableFitting",L"0",L"Disable desktop fitting behavior",TilePolicyValueKind::Boolean},
    {L"Wallpaper.DecodeLowMemory",L"Settings",L"WallpaperDecodeLowMemory",L"1",L"Decode large images at reduced resolution",TilePolicyValueKind::Boolean},
    {L"Wallpaper.DecodeMaxLongEdge",L"Settings",L"WallpaperDecodeMaxLongEdge",L"0",L"Decode maximum edge (0=automatic, up to 32768)",TilePolicyValueKind::Integer,0,32768},
    {L"Wallpaper.Listen",L"Settings",L"ListenWallpaper",L"1",L"Observe desktop wallpaper changes",TilePolicyValueKind::Boolean},
    {L"Wallpaper.ListenFit",L"Settings",L"ListenFit",L"1",L"Observe desktop fit changes",TilePolicyValueKind::Boolean},
    {L"Wallpaper.DetectionMethod",L"Settings",L"WallpaperDetectionMethod",L"TranscodedImageCache",L"Desktop wallpaper detection",TilePolicyValueKind::Choice,0,0,L"Auto|TranscodedImageCache|SystemParametersInfo|DesktopWallpaperCOM|TranscodedWallpaperFile"},
    {L"Wallpaper.DetectionFallbackOnInvalid",L"Settings",L"WallpaperDetectionFallbackOnInvalid",L"1",L"Try another detection method if unavailable",TilePolicyValueKind::Boolean},
    {L"Wallpaper.Capture",L"Settings",L"LiveWallpaperCapture",L"0",L"Capture WorkerW-hosted live wallpaper",TilePolicyValueKind::Boolean},
    {L"Wallpaper.CaptureDelayMs",L"Settings",L"LiveWallpaperCaptureDelayMs",L"2500",L"Live capture settle delay (milliseconds)",TilePolicyValueKind::Integer,0,30000},
    {L"Wallpaper.CaptureRefreshMs",L"Settings",L"LiveWallpaperCaptureRefreshMs",L"10000",L"Live capture refresh (milliseconds, 0=once)",TilePolicyValueKind::Integer,0,3600000},
    {L"Wallpaper.CaptureStartupRefreshMs",L"Settings",L"LiveWallpaperCaptureStartupRefreshMs",L"1000",L"Warm-up capture interval (milliseconds)",TilePolicyValueKind::Integer,0,3600000},
    {L"Wallpaper.CaptureStartupRefreshDurationMs",L"Settings",L"LiveWallpaperCaptureStartupRefreshDurationMs",L"0",L"Warm-up capture duration (milliseconds, 0=off)",TilePolicyValueKind::Integer,0,3600000},
    {L"Wallpaper.CaptureScreenFallback",L"Settings",L"LiveWallpaperCaptureScreenFallback",L"0",L"Allow screen-DC capture fallback",TilePolicyValueKind::Boolean},
};
inline std::wstring TilePolicyLower(std::wstring value) {
    const auto first=value.find_first_not_of(L" \r\n\t"); if(first==std::wstring::npos)return {};
    value=value.substr(first,value.find_last_not_of(L" \r\n\t")-first+1);
    for(auto& ch:value)ch=static_cast<wchar_t>(std::towlower(ch));return value;
}
inline const TilePolicyField* FindTilePolicyField(const std::wstring& key) {
    static const auto fields=[] {
        std::map<std::wstring,const TilePolicyField*> result;
        for(const auto& field:TilePolicyFields)result.emplace(TilePolicyLower(field.key),&field);
        return result;
    }();
    const auto found=fields.find(TilePolicyLower(key));return found==fields.end()?nullptr:found->second;
}
inline std::vector<std::wstring> TilePolicyChoices(const TilePolicyField& field) {
    std::vector<std::wstring> values;const std::wstring text=field.choices;
    std::size_t start=0;while(start<text.size()){const auto end=text.find(L'|',start);values.push_back(text.substr(start,end==std::wstring::npos?end:end-start));if(end==std::wstring::npos)break;start=end+1;}return values;
}
inline bool NormalizeTilePolicySetting(const std::wstring& key,std::wstring& value,std::wstring& error) {
    const auto* field=FindTilePolicyField(key);if(!field){error=key+L": unknown tile rendering/delivery setting.";return false;}
    error=key+L": invalid tile rendering/delivery value.";const auto lower=TilePolicyLower(value);
    if(field->kind==TilePolicyValueKind::Boolean){
        if(lower==L"1"||lower==L"true"||lower==L"yes"||lower==L"on")value=L"1";
        else if(lower==L"0"||lower==L"false"||lower==L"no"||lower==L"off")value=L"0";else return false;
    }else if(field->kind==TilePolicyValueKind::Integer){
        if(lower.empty())return false;std::uint64_t number=0;
        for(const auto ch:lower){if(ch<L'0'||ch>L'9')return false;number=number*10+static_cast<unsigned>(ch-L'0');if(number>static_cast<std::uint64_t>(field->maximum))return false;}
        if(number<static_cast<std::uint64_t>(field->minimum))return false;value=std::to_wstring(number);
    }else if(field->kind==TilePolicyValueKind::Choice){
        auto canonical=lower;
        if(TilePolicyLower(key)==L"delivery.mode"){
            if(lower==L"1"||lower==L"true"||lower==L"yes"||lower==L"on")canonical=L"livetile";
            else if(lower==L"0"||lower==L"false"||lower==L"no"||lower==L"off")canonical=L"registration";
        }
        bool found=false;for(const auto& choice:TilePolicyChoices(*field))if(TilePolicyLower(choice)==canonical){value=choice;found=true;break;}if(!found)return false;
    }else{
        if(value.size()>32768||value.find_first_of(L"\r\n")!=std::wstring::npos||value.find(L'\0')!=std::wstring::npos)return false;
    }
    error.clear();return true;
}
struct TileDeliveryPolicy {
    bool included=false;
    std::wstring owner;
    std::map<std::wstring,std::wstring> values;
    bool Lookup(const std::wstring& section,const std::wstring& key,std::wstring& value) const {
        if(!included)return false;const auto found=values.find(TilePolicyLower(section)+L"."+TilePolicyLower(key));
        if(found==values.end())return false;value=found->second;return true;
    }
    std::wstring Read(const std::wstring& section,const std::wstring& key,const wchar_t* fallback) const {
        std::wstring value;return Lookup(section,key,value)?value:std::wstring(fallback);
    }
    std::wstring Mode() const{return Read(L"Settings",L"ExperimentalLiveTileUpdate",L"Auto");}
    std::wstring Template() const{return Read(L"Settings",L"LiveTileTemplateStyle",L"Adaptive");}
};
template<class Reader> bool ReadTileDeliveryPolicy(Reader read,const std::wstring& section,const std::wstring& owner,TileDeliveryPolicy& output,std::wstring& error) {
    TileDeliveryPolicy candidate;candidate.included=true;candidate.owner=owner;
    for(const auto& field:TilePolicyFields){auto value=read(section.c_str(),field.key,field.fallback);
        if(!NormalizeTilePolicySetting(field.key,value,error)){error=section+L"."+error;return false;}
        candidate.values[TilePolicyLower(field.section)+L"."+TilePolicyLower(field.setting)]=std::move(value);
    }
    const auto primaryFlag=TilePolicyLower(read(section.c_str(),L"Primary",L"0"));
    const bool primary=primaryFlag==L"1"||primaryFlag==L"true"||primaryFlag==L"on"||primaryFlag==L"yes";
    if(candidate.Mode()==L"Registration"&&candidate.Read(L"Settings",L"AppxManifestTarget",L"Windows10")!=L"Windows10"&&
        !(primary&&candidate.Read(L"Settings",L"PrimaryRegistration",L"Separate")==L"Main")){
        error=section+L": Separate Registration tile requires Windows10 manifest target. Use Delivery.ManifestTarget=Windows10 or select LiveTile delivery.";return false;
    }
    output=std::move(candidate);error.clear();return true;
}
inline std::wstring TileDeliveryPolicyKey(const TileDeliveryPolicy& policy) {
    if(!policy.included)return {};std::wstring result=L"TilePolicy1:"+std::to_wstring(policy.owner.size())+L":"+policy.owner;
    for(const auto& field:policy.values)result+=L":"+std::to_wstring(field.first.size())+L":"+field.first+L":"+std::to_wstring(field.second.size())+L":"+field.second;
    return result;
}
inline std::wstring TilePolicyCacheOwnerKey(const std::wstring& profile,const std::wstring& owner) {
    std::uint64_t hash=14695981039346656037ull;
    const auto append=[&](const std::wstring& text){for(const auto ch:text){hash^=static_cast<std::uint64_t>(std::towlower(ch));hash*=1099511628211ull;}};
    append(profile);hash^=0xff;hash*=1099511628211ull;append(owner);
    std::wstring result(16,L'0');for(int index=15;index>=0;--index){result[static_cast<std::size_t>(index)]=L"0123456789abcdef"[hash&15];hash>>=4;}return result;
}
} }

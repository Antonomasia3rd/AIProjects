#define NOMINMAX
#include <windows.h>
#include <algorithm>
namespace Gdiplus {using std::min;using std::max;}
#pragma warning(push)
#pragma warning(disable:4458)
#include <gdiplus.h>
#pragma warning(pop)
#include "../dependencies/app_launcher_win32.inc"
// Compile-only witness. This function is never executed by a fixture.
void CompileAppLauncherNative(HWND owner,const aip::app_launcher::Configuration& configuration,const std::wstring& directory){
    std::wstring error;aip::tile_visual::Assets assets;
    aip::app_launcher::Execute(configuration,owner,error);
    aip::app_launcher::EnsureAssets(configuration,directory,L"compile-only",{},assets,error);
    std::unique_ptr<Gdiplus::Bitmap> bitmap(aip::app_launcher::RenderAsset(configuration,310,310));
}

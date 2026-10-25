#define NOMINMAX
#define DESKTOPSTUB_INERT_HARDWARE
#include <windows.h>
#include <algorithm>
namespace Gdiplus {using std::min;using std::max;}
#pragma warning(push)
#pragma warning(disable:4458)
#include <gdiplus.h>
#pragma warning(pop)
#include "../dependencies/app_launcher_win32.inc"
#include "../dependencies/tile_instances.h"
#include <filesystem>
#include <iostream>
#include <map>
namespace {
int checks=0,failures=0;
void Check(bool condition,const char* message){++checks;if(!condition){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
using namespace aip::app_launcher;
Configuration Base(){Configuration c;c.target=L"C:\\Program Files\\Fixture\\fixture.exe";return c;}
void Model(){
    Configuration defaults;std::wstring error;Check(ReadConfiguration([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},defaults,error)&&
        defaults.target.empty()&&defaults.iconMode==L"Auto"&&defaults.iconSizePercent==64,"new launcher is inert and uses centered automatic artwork defaults");
    LaunchRequest request;Check(!BuildLaunchRequest(defaults,request,error)&&!error.empty(),"unconfigured launcher explains setup without a launch request");
    for(const auto* target:{L"C:\\Program Files\\App\\app.exe",L"D:\\Folder\\File name.docx",L"\\\\server\\share\\file.pdf",L"notepad.exe",
        L"https://example.invalid/a?q=x&b=y",L"mailto:fixture@example.invalid",L"steam://rungameid/123",L"shell:AppsFolder\\Fixture!App",L"%LOCALAPPDATA%\\Programs\\app.exe"}){
        auto value=std::wstring(target);Check(NormalizeSetting(L"Launcher.Target",value,error),"explicit file/program/URL target is accepted without evaluating a shell command");}
    for(const auto* target:{L"C:relative.exe",L"app.exe --argument",L"\"C:\\app.exe\" & bad.exe",L"javascript:alert(1)",L"vbscript:x",L"data:text/html,x",L"https://bad.invalid/\nline"}){
        auto value=std::wstring(target);Check(!NormalizeSetting(L"Target",value,error),"ambiguous combined command or script/control-character URI is rejected");}
    for(const auto& setting:std::vector<std::pair<std::wstring,std::wstring>>{{L"IconMode",L"custom"},{L"IconIndex",L"-32768"},{L"IconIndex",L"32767"},
        {L"IconSizePercent",L"10"},{L"IconSizePercent",L"100"},{L"WorkingDirectory",L"%TEMP%\\fixture"}}){auto value=setting.second;Check(NormalizeSetting(setting.first,value,error),"supported launcher field normalizes");}
    for(const auto& setting:std::vector<std::pair<std::wstring,std::wstring>>{{L"IconMode",L"Remote"},{L"IconIndex",L"32768"},{L"IconSizePercent",L"9"},
        {L"IconSizePercent",L"101"},{L"IconPath",L"https://example.invalid/icon.png"},{L"WorkingDirectory",L"relative"},{L"Arguments",L"bad\rargument"}}){
        auto value=setting.second;Check(!NormalizeSetting(setting.first,value,error),"invalid icon/argument/directory option fails before save");}
    auto c=Base();c.arguments=L"--name \"A & B\" --literal $() `x` | > out";c.workingDirectory=L"C:\\Fixture directory";
    Check(BuildLaunchRequest(c,request,error)&&request.target==c.target&&request.arguments==c.arguments&&request.workingDirectory==c.workingDirectory&&!request.uri,
        "program, raw argument string and working directory remain independent values");
    c.target=L"https://example.invalid/";Check(!BuildLaunchRequest(c,request,error),"URL target cannot smuggle a second program argument channel");
    c.arguments.clear();Check(BuildLaunchRequest(c,request,error)&&request.uri,"URL target creates only one explicit association request");
    c=Base();const auto key=ConfigurationKey(c);c.arguments=L"--other";Check(key!=ConfigurationKey(c),"launch arguments participate in immutable profile identity");
    c=Base();c.iconIndex=2;Check(key!=ConfigurationKey(c)&&ArtworkKey(Base())!=ArtworkKey(c),"icon index invalidates metadata/artwork caches");
    c=Base();c.iconMode=L"Custom";Check(!BuildLaunchRequest(c,request,error),"custom mode requires an explicit image/resource file");
    Check(!LocalArtworkFile(L"\\\\unreachable.invalid\\share\\file.exe"),"background artwork refuses UNC paths before any remote filesystem query");
}
void ActualLaunchAdapter(){
    unsigned calls=0;auto c=Base();c.arguments=L"--fixture \"literal & | $()\"";c.workingDirectory=L"C:\\Fixture";
    testLaunch=[&](const LaunchRequest& request,HWND,std::wstring&){++calls;Check(request.target==c.target&&request.arguments==c.arguments&&request.workingDirectory==c.workingDirectory,
        "actual activation adapter forwards separate fields to the injected transport");return true;};
    std::wstring error;Check(Execute(c,nullptr,error)&&calls==1,"explicit launcher activation reaches only the inert transport");
    c.target.clear();Check(!Execute(c,nullptr,error)&&calls==1,"unconfigured actual adapter does not call even an injected launch transport");
    Check(SetEnvironmentVariableW(L"DESKTOPSTUB_LAUNCH_TEST_ONLY",L"javascript:alert(1)")!=FALSE,"fixture sets only its own child-process environment variable");
    c=Base();c.target=L"%DESKTOPSTUB_LAUNCH_TEST_ONLY%\\fixture.exe";
    Check(!Execute(c,nullptr,error)&&calls==1,"post-expansion validation blocks a script URI before the injected launch boundary");
    SetEnvironmentVariableW(L"DESKTOPSTUB_LAUNCH_TEST_ONLY",nullptr);
    testLaunch={};c=Base();Check(!Execute(c,nullptr,error),"inert build has no operating-system launch fallback");
}
void Artwork(){
    using namespace Gdiplus;ULONG_PTR token=0;GdiplusStartupInput startup;Check(GdiplusStartup(&token,&startup,nullptr)==Ok,"offscreen artwork fixture initializes GDI+ only");if(!token)return;
    auto c=Base();unsigned loads=0;
    testArtwork=[&](const Configuration&){++loads;auto icon=std::make_unique<Bitmap>(64,64,PixelFormat32bppARGB);Graphics g(icon.get());g.Clear(Color(0,0,0,0));
        SolidBrush ink(Color(255,230,20,40));g.FillRectangle(&ink,0,0,64,64);return icon;};
    for(const auto& dimensions:std::vector<std::pair<int,int>>{{70,70},{150,150},{310,150},{310,310},{620,300}}){
        std::unique_ptr<Bitmap> bitmap(RenderAsset(c,dimensions.first,dimensions.second));Color center,corner;
        Check(bitmap&&bitmap->GetWidth()==static_cast<UINT>(dimensions.first)&&bitmap->GetHeight()==static_cast<UINT>(dimensions.second),"actual launcher renderer retains Small Medium Wide Large and scaled canvas dimensions");
        if(bitmap){bitmap->GetPixel(dimensions.first/2,dimensions.second/2,&center);bitmap->GetPixel(0,0,&corner);
            Check(center.GetA()>200&&center.GetR()>200&&corner.GetA()==0,"configured icon is centered over a transparent full-size tile canvas");}}
    const auto before=loads;std::unique_ptr<Bitmap> hidden(RenderAsset(c,150,150,{},false));Color pixel;if(hidden)hidden->GetPixel(75,75,&pixel);
    Check(hidden&&pixel.GetA()==0&&loads==before,"disabled icon does not load artwork and leaves transparent pixels");
    c.iconMode=L"None";std::unique_ptr<Bitmap> colored(RenderAsset(c,150,150,{true,0x123456}));if(colored)colored->GetPixel(0,0,&pixel);
    Check(colored&&pixel.GetA()==255&&pixel.GetR()==0x12&&pixel.GetG()==0x34&&pixel.GetB()==0x56,"explicit color and hidden icon remain configurable");
    c=Base();testArtwork={};std::unique_ptr<Bitmap> fallback(RenderAsset(c,150,150));bool hasInk=false;if(fallback)for(int y=0;y<150;y+=3)for(int x=0;x<150;x+=3){fallback->GetPixel(x,y,&pixel);hasInk|=pixel.GetA()>0;}
    Check(fallback&&hasInk,"missing automatic icon has an independently drawn readable app symbol");
    const auto base=std::filesystem::temp_directory_path()/L"DesktopStub-AppLauncher-fixture";
    const auto leaf=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto directory=base/leaf;std::error_code issue;std::filesystem::create_directories(base,issue);
    const auto baseAttributes=GetFileAttributesW(base.c_str());
    const bool ordinaryBase=!issue&&baseAttributes!=INVALID_FILE_ATTRIBUTES&&!(baseAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
    const bool created=ordinaryBase&&std::filesystem::create_directory(directory,issue);
    Check(created&&!issue&&directory.parent_path()==base,"artwork fixture owns a newly created uniquely named temporary directory");
    aip::tile_visual::Assets assets;std::wstring error;const bool ensured=created&&!issue&&EnsureAssets(c,directory.wstring(),L"private-profile/tile",{},assets,error);
    Check(ensured&&!assets.medium.empty()&&!assets.wide.empty()&&!assets.large.empty()&&!assets.smallTile.empty(),"actual native artwork preparer creates all four tile size logos");
    if(ensured)for(const auto& asset:std::vector<std::pair<std::wstring,std::pair<UINT,UINT>>>{{assets.medium,{150,150}},{assets.wide,{310,150}},{assets.large,{310,310}},
        {assets.smallTile,{70,70}},{assets.logo44,{44,44}},{assets.logo30,{30,30}}}){Bitmap image((directory/asset.first).c_str());
        Check(image.GetLastStatus()==Ok&&image.GetWidth()==asset.second.first&&image.GetHeight()==asset.second.second,"saved native launcher PNG has its exact required dimensions");}
    aip::tile_visual::Assets repeated;Check(EnsureAssets(c,directory.wstring(),L"private-profile/tile",{},repeated,error)&&aip::tile_visual::SameLogos(assets,repeated),"unchanged launcher metadata reuses stable asset locations");
    aip::tile_visual::Assets other;Check(EnsureAssets(c,directory.wstring(),L"another-profile/tile",{},other,error)&&assets.wide!=other.wide,"artwork namespace includes the owning profile/tile identity");
    Check(!PrepareOwnedArtworkFolder(directory.wstring(),L"../outside",error),"artwork preparer rejects parent traversal before creating a folder");
    if(created&&directory.parent_path()==base&&directory.filename()==leaf){std::filesystem::remove_all(directory,issue);Check(!issue,"fixture removes only its exact newly owned generated directory");}
    hidden.reset();colored.reset();fallback.reset();testArtwork={};testLaunch={};GdiplusShutdown(token);
}
void Instances(){
    std::map<std::wstring,std::wstring> values{{L"Tiles.SchemaVersion",L"1"},{L"Tiles.Instances",L"left,right"},{L"Tile.left.Type",L"AppLauncher"},
        {L"Tile.left.Launcher.Target",L"C:\\Fixture\\left.exe"},{L"Tile.left.Launcher.Arguments",L"--left"},{L"Tile.right.Type",L"AppLauncher"},
        {L"Tile.right.Launcher.Target",L"https://example.invalid/"},{L"Launcher.Target",L"C:\\DO-NOT-INHERIT.exe"}};
    const auto reader=[&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){const auto found=values.find(std::wstring(section)+L"."+key);return found==values.end()?std::wstring(fallback):found->second;};
    aip::content::TileInstanceConfiguration cfg;std::wstring error;Check(aip::content::ReadTileInstanceConfiguration(reader,cfg,error)&&cfg.tiles.size()==2,"real immutable model accepts independent App launcher tiles");
    if(cfg.tiles.size()==2)Check(cfg.tiles[0].launcher.arguments==L"--left"&&cfg.tiles[1].launcher.arguments.empty()&&cfg.tiles[0].clickAction==L"launch"&&
        cfg.tiles[1].launcher.target==L"https://example.invalid/","per-tile launch fields and default click action cannot inherit a global target");
    values[L"Tile.right.Type"]=L"RSS";values[L"Tile.right.ClickAction"]=L"launch";
    Check(!aip::content::ReadTileInstanceConfiguration(reader,cfg,error),"non-launcher tile cannot acquire launcher activation by setting ClickAction alone");
}
}
int main(){Model();ActualLaunchAdapter();Artwork();Instances();std::cout<<checks<<" App launcher checks, "<<failures<<" failures; process activation/icon extraction injected; no applications launched\n";return failures?1:0;}

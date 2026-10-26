// Offscreen local files/fonts only. No pin, package, account, device or profile
// API. Missing/old Windows font behavior is covered by the portable matrix.
#include "../dependencies/desktop_app_baseline.h"
#include <objidl.h>
#include <propidl.h>
namespace Gdiplus{using std::min;using std::max;}
#pragma warning(push)
#pragma warning(disable:4458)
#include <gdiplus.h>
#pragma warning(pop)
#include "../dependencies/tile_visual_assets.inc"
#include <filesystem>
#include <iostream>
#include <stdexcept>
static int checks=0;
static void Check(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
static aip::tile_visual::FontPreference preference=aip::tile_visual::FontPreference::Vector;
static unsigned reads=0;
static aip::tile_visual::FontPreference ReadPreference(){++reads;return preference;}
int main(){try{
    using namespace Gdiplus;using namespace aip::tile_visual;
    struct Session{ULONG_PTR token=0;~Session(){if(token)GdiplusShutdown(token);}}session;GdiplusStartupInput input;
    Check(GdiplusStartup(&session.token,&input,nullptr)==Ok,"offscreen GDI+ starts");
    UINT count=0,size=0;Check(GetImageEncodersSize(&count,&size)==Ok&&size,"PNG encoder size is available");
    auto bytes=std::make_unique<BYTE[]>(size);auto* encoders=reinterpret_cast<ImageCodecInfo*>(bytes.get());
    Check(GetImageEncoders(count,size,encoders)==Ok,"PNG encoder metadata is available");CLSID png{};bool found=false;
    for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0){png=encoders[i].Clsid;found=true;break;}
    Check(found,"PNG encoder is present");
    wchar_t temporary[MAX_PATH]{},reserved[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH,temporary)&&GetTempFileNameW(temporary,L"aif",0,reserved),"a unique private image fixture is reserved");
    Check(DeleteFileW(reserved)&&CreateDirectoryW(reserved,nullptr),"fixture owns its newly created directory");
    const auto root=std::filesystem::path(reserved);std::vector<std::filesystem::path> written;
    SetFontPreferenceReader(ReadPreference);
    struct ReaderReset{~ReaderReset(){SetFontPreferenceReader(nullptr);}}reset;
    const auto seed=[&](const wchar_t* topic,AssetBackground background){
        const auto* descriptor=FindTopic(topic);Check(descriptor!=nullptr,"fixture topic exists");
        for(const auto& asset:AssetPlan(*descriptor,aip::tile_visual::FontStyle::Vector,background)){
            const auto path=root/std::filesystem::path(asset.relative);std::error_code issue;std::filesystem::create_directories(path.parent_path(),issue);
            Check(!issue,"only private fixture asset directories are created");
            Bitmap blank(static_cast<INT>(asset.width),static_cast<INT>(asset.height),PixelFormat32bppARGB);
            {Graphics canvas(&blank);canvas.Clear(asset.transparent?Color(0,0,0,0):Color(255,static_cast<BYTE>(background.rgb>>16),static_cast<BYTE>(background.rgb>>8),static_cast<BYTE>(background.rgb)));}
            Check(!TopicBitmapContainsInk(blank,background,asset.transparent),"dimension-correct transparent/solid plates are not mistaken for icons");
            Check(blank.Save(path.c_str(),&png,nullptr)==Ok,"private blank PNG is deliberately seeded");written.push_back(path);
        }
        Assets assets;std::wstring error;const auto before=reads;
        Check(EnsureTileTopicAssets(topic,root.wstring(),assets,error,background)&&error.empty(),"actual generation repairs dimension-correct blank cached artwork");
        Check(reads==before+1,"one resolved preference governs all six immutable artwork files");
        const auto plan=AssetPlan(*descriptor,aip::tile_visual::FontStyle::Vector,background);
        Check(assets.medium==plan[0].relative&&assets.logo44==plan[4].relative,"forced Vector cannot reuse another font's assets");
        for(const auto& asset:plan){Bitmap image((root/std::filesystem::path(asset.relative)).c_str());
            Check(image.GetLastStatus()==Ok&&image.GetWidth()==asset.width&&image.GetHeight()==asset.height&&TopicBitmapContainsInk(image,background,asset.transparent),"all body and branding sizes contain real ink after repair");
        }
        const auto cached=assets;Check(EnsureTileTopicAssets(topic,root.wstring(),assets,error,background)&&SameLogos(assets,cached),"successful repair stays in the cheap existing cache path");
    };
    seed(L"prayer",{});seed(L"prayer",{true,0xffffff});seed(L"desktop",{});seed(L"worldclock",{});
    for(const auto requested:{FontPreference::Mdl2,FontPreference::Fluent,FontPreference::Auto,FontPreference::Vector}){
        preference=requested;
        const auto chosen=FontForGlyph(Glyph::Prayer);
        const auto expected=SelectFont(Glyph::Prayer,requested,WindowsIconFontBuild(),InstalledGlyph);
        Check(chosen==expected,"native cache separates preferences and actual build selection");
        if(requested==FontPreference::Vector)Check(chosen==aip::tile_visual::FontStyle::Vector,"Vector remains deterministic even when native fonts are installed");
        Assets assets;std::wstring error;
        Check(EnsureTileTopicAssets(L"prayer",root.wstring(),assets,error),"each real native preference can produce a complete Prayer image set");
        const auto plan=AssetPlan(*FindTopic(L"prayer"),chosen);
        Check(assets.medium==plan[0].relative&&assets.logo30==plan[5].relative,"generated paths match the verified selected font rather than the requested missing family");
        for(const auto& asset:plan){Bitmap image((root/std::filesystem::path(asset.relative)).c_str());
            Check(image.GetLastStatus()==Ok&&TopicBitmapContainsInk(image,{},true),"selected installed font or Vector produces visible body and corner PNGs");}
    }
    SetFontPreferenceReader(nullptr);
    // The only recursively removed directory is the exact unique fixture root
    // created above, resolved and checked against the temporary directory.
    const auto checked=std::filesystem::weakly_canonical(root),parent=std::filesystem::weakly_canonical(std::filesystem::path(temporary));
    Check(checked.parent_path()==parent&&checked.filename()==root.filename(),"cleanup target is verified inside the known temporary directory");
    std::error_code issue;std::filesystem::remove_all(checked,issue);Check(!issue,"private fixture files are removed");
    std::cout<<checks<<" offscreen icon font/cache checks; 0 failures\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}}

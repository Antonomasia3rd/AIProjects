#pragma once
#include "app_launcher.h"
#include "tile_visual_assets.h"
#include "notification_policy.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
namespace Gdiplus {using std::min;using std::max;}
#include <gdiplus.h>
#include <functional>
#include <memory>
#include <mutex>
namespace aip { namespace app_launcher {
#ifdef DESKTOPSTUB_INERT_HARDWARE
inline std::function<bool(const LaunchRequest&,HWND,std::wstring&)> testLaunch;
inline std::function<std::unique_ptr<Gdiplus::Bitmap>(const Configuration&)> testArtwork;
#endif
inline bool ExpandPath(const std::wstring& input,std::wstring& output,std::wstring& error){
    if(input.empty()){output.clear();return true;}const auto size=ExpandEnvironmentStringsW(input.c_str(),nullptr,0);
    if(!size||size>32761){error=L"The configured path could not be expanded within the Windows path limit.";return false;}
    std::vector<wchar_t> text(size);if(ExpandEnvironmentStringsW(input.c_str(),text.data(),size)!=size){error=L"The configured environment path changed while it was read.";return false;}
    output=text.data();return true;
}
inline bool Execute(const Configuration& configuration,HWND owner,std::wstring& error){
    LaunchRequest request;if(!BuildLaunchRequest(configuration,request,error))return false;
    if(!request.uri){
        if(!ExpandPath(request.target,request.target,error)||!NormalizeSetting(L"Target",request.target,error))return false;
        if(Uri(request.target)||EnvironmentPath(request.target)){error=L"The environment path must resolve to a filesystem/program target, not a URL or unresolved variable.";return false;}
    }
    if(!ExpandPath(request.workingDirectory,request.workingDirectory,error))return false;
    if(!request.workingDirectory.empty()&&(!PathText(request.workingDirectory)||EnvironmentPath(request.workingDirectory))){
        error=L"The working directory must resolve to a complete folder path.";return false;}
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return testLaunch&&testLaunch(request,owner,error);
#else
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    struct Apartment{HRESULT result;~Apartment(){if(SUCCEEDED(result))CoUninitialize();}}apartment{initialized};
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE){error=L"Windows could not initialize application activation.";return false;}
    SHELLEXECUTEINFOW execute{};execute.cbSize=sizeof(execute);execute.hwnd=owner;
    execute.fMask=SEE_MASK_FLAG_NO_UI|SEE_MASK_NOASYNC;execute.lpVerb=L"open";execute.lpFile=request.target.c_str();
    execute.lpParameters=request.arguments.empty()?nullptr:request.arguments.c_str();
    execute.lpDirectory=request.workingDirectory.empty()?nullptr:request.workingDirectory.c_str();execute.nShow=SW_SHOWNORMAL;
    // lpFile and lpParameters stay separate. No cmd.exe, PowerShell, combined
    // command string or script wrapper is constructed by DesktopStub.
    if(ShellExecuteExW(&execute))return true;
    error=L"Windows could not open this tile's target (error "+std::to_wstring(GetLastError())+L"). Check its program/path, arguments and working directory.";
    return false;
#endif
}
inline bool LocalArtworkFile(const std::wstring& path,WIN32_FILE_ATTRIBUTE_DATA* output=nullptr){
    if(!DrivePath(path))return false;const wchar_t root[]={path[0],L':',L'\\',L'\0'};
    const auto drive=GetDriveTypeW(root);if(drive!=DRIVE_FIXED&&drive!=DRIVE_RAMDISK&&drive!=DRIVE_CDROM)return false;
    for(std::size_t end=path.find_first_of(L"\\/",3);end!=std::wstring::npos;end=path.find_first_of(L"\\/",end+1)){
        const auto attributes=GetFileAttributesW(path.substr(0,end).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_OFFLINE|0x00400000u)))return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_OFFLINE|0x00400000u)))return false;
    if(output)*output=info;return true;
}
inline std::wstring ArtworkSource(const Configuration& configuration){
    auto source=configuration.iconMode==L"Custom"?configuration.iconPath:configuration.target;
    if(Uri(source)||SimpleProgram(source)||configuration.iconMode==L"None")return {};
    std::wstring expanded,error;return ExpandPath(source,expanded,error)?expanded:std::wstring();
}
inline std::wstring ArtworkKey(const Configuration& configuration){
    auto key=configuration.iconMode+L"|"+configuration.iconPath+L"|"+std::to_wstring(configuration.iconIndex)+L"|"+std::to_wstring(configuration.iconSizePercent);
    if(configuration.iconMode==L"Auto")key+=L"|"+configuration.target;
#ifndef DESKTOPSTUB_INERT_HARDWARE
    WIN32_FILE_ATTRIBUTE_DATA data{};const auto source=ArtworkSource(configuration);
    if(LocalArtworkFile(source,&data))key+=L"|"+std::to_wstring(data.nFileSizeHigh)+L":"+std::to_wstring(data.nFileSizeLow)+L":"+
        std::to_wstring(data.ftLastWriteTime.dwHighDateTime)+L":"+std::to_wstring(data.ftLastWriteTime.dwLowDateTime);
#endif
    return aip::notifications::StableId(key);
}
inline std::unique_ptr<Gdiplus::Bitmap> IconBitmap(HICON icon){
    using namespace Gdiplus;if(!icon)return {};ICONINFO info{};if(!GetIconInfo(icon,&info))return {};
    struct Icons{ICONINFO& value;~Icons(){if(value.hbmColor)DeleteObject(value.hbmColor);if(value.hbmMask)DeleteObject(value.hbmMask);}}cleanup{info};
    BITMAP dimensions{};if(!info.hbmColor||!GetObjectW(info.hbmColor,sizeof(dimensions),&dimensions)||dimensions.bmWidth<=0||dimensions.bmHeight<=0||
        dimensions.bmWidth>512||dimensions.bmHeight>512)return {};
    const int width=dimensions.bmWidth,height=dimensions.bmHeight;BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth=width;format.bmiHeader.biHeight=-height;format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;
    const auto screen=GetDC(nullptr);if(!screen)return {};std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width)*height);
    const bool read=GetDIBits(screen,info.hbmColor,0,height,pixels.data(),&format,DIB_RGB_COLORS)!=0;
    if(!read){ReleaseDC(nullptr,screen);return {};}
    const bool alpha=std::any_of(pixels.begin(),pixels.end(),[](std::uint32_t pixel){return(pixel&0xff000000u)!=0;});
    if(!alpha){
        struct MaskInfo{BITMAPINFOHEADER header;RGBQUAD colors[2];}mask{};mask.header.biSize=sizeof(BITMAPINFOHEADER);mask.header.biWidth=width;
        mask.header.biHeight=-height;mask.header.biPlanes=1;mask.header.biBitCount=1;mask.header.biCompression=BI_RGB;
        const auto stride=static_cast<std::size_t>((width+31)/32)*4;std::vector<BYTE> bits(stride*height,0);
        if(!info.hbmMask||!GetDIBits(screen,info.hbmMask,0,height,bits.data(),reinterpret_cast<BITMAPINFO*>(&mask),DIB_RGB_COLORS)){ReleaseDC(nullptr,screen);return {};}
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(!(bits[static_cast<std::size_t>(y)*stride+x/8]&(0x80u>>(x%8))))pixels[static_cast<std::size_t>(y)*width+x]|=0xff000000u;
    }
    ReleaseDC(nullptr,screen);
    // A PARGB Bitmap constructed over scan0 and then Clone'd can retain that
    // borrowed storage. Own the pixels before this local vector is destroyed;
    // one retained icon is rendered and PNG-encoded repeatedly by EnsureAssets.
    auto result=std::make_unique<Bitmap>(width,height,PixelFormat32bppPARGB);
    if(result->GetLastStatus()!=Ok)return {};
    Rect bounds(0,0,width,height);BitmapData destination{};
    if(result->LockBits(&bounds,ImageLockModeWrite,PixelFormat32bppPARGB,&destination)!=Ok)return {};
    const auto stride=static_cast<std::ptrdiff_t>(destination.Stride),rowBytes=static_cast<std::ptrdiff_t>(width)*4;
    const bool writable=destination.Scan0&&(stride>=rowBytes||stride<=-rowBytes);
    if(writable)for(int row=0;row<height;++row)
        memcpy(static_cast<BYTE*>(destination.Scan0)+static_cast<std::ptrdiff_t>(row)*stride,
            pixels.data()+static_cast<std::size_t>(row)*width,static_cast<std::size_t>(rowBytes));
    const auto unlocked=result->UnlockBits(&destination);
    if(!writable||unlocked!=Ok)return {};return result;
}
inline std::unique_ptr<Gdiplus::Bitmap> LoadArtwork(const Configuration& configuration){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return testArtwork?testArtwork(configuration):nullptr;
#else
    using namespace Gdiplus;const auto source=ArtworkSource(configuration);WIN32_FILE_ATTRIBUTE_DATA data{};
    if(source.empty()||!LocalArtworkFile(source,&data))return {};
    const auto dot=source.find_last_of(L'.');const auto extension=dot==std::wstring::npos?std::wstring():Lower(source.substr(dot));
    if(configuration.iconMode==L"Custom"&&(extension==L".png"||extension==L".jpg"||extension==L".jpeg"||extension==L".bmp"||extension==L".gif")){
        if(data.nFileSizeHigh||data.nFileSizeLow>16u*1024*1024)return {};
        Bitmap image(source.c_str());const auto width=image.GetWidth(),height=image.GetHeight();
        if(image.GetLastStatus()!=Ok||!width||!height||width>2048||height>2048)return {};
        auto result=std::make_unique<Bitmap>(256,256,PixelFormat32bppPARGB);Graphics graphics(result.get());graphics.Clear(Color(0,0,0,0));
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);const REAL scale=256.0f/(std::max)(width,height);
        if(graphics.DrawImage(&image,RectF((256-width*scale)/2,(256-height*scale)/2,width*scale,height*scale))!=Ok)return {};return result;
    }
    HICON icon=nullptr;
    if(extension==L".exe"||extension==L".dll"||extension==L".ico"||extension==L".com")SHDefExtractIconW(source.c_str(),configuration.iconIndex,0,&icon,nullptr,256);
    if(!icon&&configuration.iconMode==L"Auto"){
        // Association lookup does not open the document or a shortcut target,
        // invoke a thumbnail provider, or access remote target data.
        SHFILEINFOW info{};if(SHGetFileInfoW(extension.c_str(),FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_USEFILEATTRIBUTES|SHGFI_ICON|SHGFI_LARGEICON))icon=info.hIcon;
    }
    auto result=IconBitmap(icon);if(icon)DestroyIcon(icon);return result;
#endif
}
inline std::unique_ptr<Gdiplus::Bitmap> RenderWithArtwork(const Configuration& configuration,int width,int height,
    aip::tile_visual::AssetBackground background,Gdiplus::Bitmap* artwork,bool showIcon=true){
    using namespace Gdiplus;if(width<1||height<1||width>4096||height>4096)return {};
    auto bitmap=std::make_unique<Bitmap>(width,height,PixelFormat32bppARGB);Graphics graphics(bitmap.get());
    if(bitmap->GetLastStatus()!=Ok||graphics.GetLastStatus()!=Ok)return {};
    graphics.Clear(background.colored?Color(255,static_cast<BYTE>(background.rgb>>16),static_cast<BYTE>(background.rgb>>8),static_cast<BYTE>(background.rgb)):Color(0,0,0,0));
    if(!showIcon||configuration.iconMode==L"None")return bitmap;
    const REAL side=static_cast<REAL>((std::min)(width,height))*configuration.iconSizePercent/100.0f;
    RectF target((width-side)/2,(height-side)/2,side,side);graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    if(artwork){if(graphics.DrawImage(artwork,target)!=Ok)return {};}
    else{
        const auto ink=aip::tile_visual::DarkArtworkInk(background)?Color(255,20,20,20):Color(255,255,255,255);
        Pen pen(ink,(std::max)(1.0f,side*.055f));SolidBrush fill(ink);const REAL inset=side*.1f;
        if(graphics.DrawRectangle(&pen,target.X+inset,target.Y+inset,side-2*inset,side-2*inset)!=Ok)return {};
        graphics.FillRectangle(&fill,target.X+inset,target.Y+inset,side-2*inset,side*.16f);
        for(int y=0;y<2;++y)for(int x=0;x<2;++x)graphics.FillRectangle(&fill,target.X+side*(.26f+x*.28f),target.Y+side*(.43f+y*.25f),side*.15f,side*.15f);
    }
    return graphics.GetLastStatus()==Ok?std::move(bitmap):nullptr;
}
inline Gdiplus::Bitmap* RenderAsset(const Configuration& configuration,int width,int height,aip::tile_visual::AssetBackground background={},bool showIcon=true){
    const auto artwork=!showIcon||configuration.iconMode==L"None"?nullptr:LoadArtwork(configuration);
    if(showIcon&&configuration.iconMode==L"Custom"&&!artwork)return nullptr;
    return RenderWithArtwork(configuration,width,height,background,artwork.get(),showIcon).release();
}
inline bool PrepareOwnedArtworkFolder(const std::wstring& directory,const std::wstring& relative,std::wstring& error){
    const auto root=GetFileAttributesW(directory.c_str());
    if(root==INVALID_FILE_ATTRIBUTES||!(root&FILE_ATTRIBUTE_DIRECTORY)||(root&FILE_ATTRIBUTE_REPARSE_POINT)){
        error=L"The launcher package directory is not an ordinary owned folder.";return false;}
    auto path=directory;std::size_t begin=0;
    while(begin<relative.size()){
        const auto end=relative.find(L'/',begin);const auto segment=relative.substr(begin,end==std::wstring::npos?end:end-begin);
        if(segment.empty()||segment==L"."||segment==L".."||segment.find_first_of(L"\\:")!=std::wstring::npos){error=L"Invalid relative launcher artwork directory.";return false;}
        path+=L"\\"+segment;
        if(!CreateDirectoryW(path.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){error=L"The launcher artwork folder could not be created.";return false;}
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){
            error=L"The owned launcher artwork folder is linked or unavailable.";return false;}
        if(end==std::wstring::npos)break;begin=end+1;
    }
    return true;
}
inline bool EnsureAssets(const Configuration& configuration,const std::wstring& directory,const std::wstring& ownerKey,
    aip::tile_visual::AssetBackground background,aip::tile_visual::Assets& result,std::wstring& error){
    using namespace Gdiplus;result={};error.clear();if(directory.empty()||ownerKey.empty()){error=L"App launcher artwork requires an owned package directory and tile identity.";return false;}
    static std::mutex mutex;std::lock_guard<std::mutex> lock(mutex);
    const auto base=L"Assets/AppLaunchers/v1/"+aip::notifications::StableId(ownerKey)+L"/"+ArtworkKey(configuration)+L"/"+aip::tile_visual::BackgroundAssetKey(background)+L"/";
    if(!PrepareOwnedArtworkFolder(directory,base.substr(0,base.size()-1),error))return false;
    const std::vector<aip::tile_visual::Asset> plan{{base+L"Medium.png",150,150,!background.colored},{base+L"Wide.png",310,150,!background.colored},
        {base+L"Large.png",310,310,!background.colored},{base+L"Small.png",70,70,!background.colored},{base+L"Logo44.png",44,44,true},{base+L"Logo30.png",30,30,true}};
    struct Session{ULONG_PTR token=0;~Session(){if(token)GdiplusShutdown(token);}}session;GdiplusStartupInput input;
    if(GdiplusStartup(&session.token,&input,nullptr)!=Ok){error=L"Windows could not initialize launcher artwork rendering.";return false;}
    UINT count=0,bytes=0;if(GetImageEncodersSize(&count,&bytes)!=Ok||!bytes)return false;
    std::vector<BYTE> storage(bytes);auto* codecs=reinterpret_cast<ImageCodecInfo*>(storage.data());if(GetImageEncoders(count,bytes,codecs)!=Ok)return false;
    CLSID png{};bool found=false;for(UINT index=0;index<count;++index)if(wcscmp(codecs[index].MimeType,L"image/png")==0){png=codecs[index].Clsid;found=true;break;}
    if(!found){error=L"PNG artwork encoding is unavailable.";return false;}
    auto artwork=configuration.iconMode==L"None"?nullptr:LoadArtwork(configuration);
    if(configuration.iconMode==L"Custom"&&!artwork){error=L"The custom icon could not be loaded. Choose a fully downloaded local PNG/JPEG/BMP/GIF/ICO or EXE/DLL icon resource, outside linked/cloud folders.";return false;}
    for(const auto& asset:plan){auto path=directory+L"/"+asset.relative;
        const auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){error=L"The owned launcher artwork path is not an ordinary file.";return false;}
        if(attributes!=INVALID_FILE_ATTRIBUTES){Bitmap existing(path.c_str());if(existing.GetLastStatus()==Ok&&existing.GetWidth()==asset.width&&existing.GetHeight()==asset.height)continue;}
        const auto assetBackground=asset.transparent?aip::tile_visual::AssetBackground{}:background;
        auto bitmap=RenderWithArtwork(configuration,static_cast<int>(asset.width),static_cast<int>(asset.height),assetBackground,artwork.get());
        const auto temporary=path+L".tmp-"+std::to_wstring(GetCurrentProcessId());
        if(!bitmap||bitmap->Save(temporary.c_str(),&png,nullptr)!=Ok||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(temporary.c_str());error=L"The owned launcher artwork could not be saved.";return false;}
    }
    result={plan[0].relative,plan[1].relative,plan[2].relative,plan[3].relative,plan[4].relative,plan[5].relative};return true;
}
} }

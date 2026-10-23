#include "../dependencies/gallery_image_activation.h"
#include <iostream>
#include <stdexcept>
using namespace aip::gallery;using namespace aip::gallery::activation;
static int checks=0;static void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
int main(){try{
    Token first{L"0123456789ABCDEF",L"gallery",L"one",L"epoch one",L"sha256:"+std::wstring(64,L'a')},decoded;
    const auto encoded=Encode(first);
    Check(!encoded.empty()&&Decode(encoded,decoded)&&Same(first,decoded),"opaque per-image token roundtrips owned IDs without paths");
    Check(encoded.find(L"photo")==std::wstring::npos&&encoded.find(L"D:")==std::wstring::npos,"notification arguments disclose no original photo path");
    for(const auto& bad:{encoded+L".extra",std::wstring(L"D:\\photo.jpg"),std::wstring(L"aip-gallery-1.invalid"),std::wstring(4097,L'a')})Check(!Decode(bad,decoded),"malformed or oversized command tokens are rejected");
    Record record{first,L"\\\\?\\D:\\Photos\\photo.jpg",L"file-identity",12345,67890},parsed;
    const auto bytes=Serialize(record);Check(!bytes.empty()&&Parse(bytes,first,parsed)&&parsed.path==record.path&&parsed.bytes==record.bytes,"bounded private mapping preserves exact original file metadata");
    auto other=first;other.tile=L"two";Check(!Parse(bytes,other,parsed),"another tile cannot consume an existing image mapping");
    other=first;other.owner=L"fedcba9876543210";Check(!Parse(bytes,other,parsed),"another user/profile owner cannot consume image mapping");
    Check(!Parse(bytes+L"trailing",first,parsed)&&!Parse(std::wstring(40001,L'x'),first,parsed),"trailing and oversized mapping data are rejected");
    for(const auto* path:{L"D:\\photo.jpg",L"D:\\photo.PNG",L"\\\\?\\D:\\Photo folder\\photo.tiff"})Check(PhotoPath(path),"owned local photo extension supported");
    for(const auto* path:{L"D:\\program.exe",L"D:\\photo.jpg.exe",L"D:\\shortcut.lnk",L"D:\\image.svg",L"D:\\photo.jpg:stream.png",L"\\\\server\\share\\photo.png",L"https://example.invalid/photo.png",L"photo.png",L"D:\\a\nphoto.png"})
        Check(!PhotoPath(path),"paths cannot become executable, shortcut, ADS, remote or relative activation targets");
    Ledger ledger;ledger.groupId=first.group;ledger.epochId=first.epoch;ledger.images[first.image]={first.tile,0};
    Check(Owned(first,ledger),"record needs exact current durable image ownership");
    ledger.images[first.image].owner=L"two";Check(!Owned(first,ledger),"another tile's reserved photo is rejected");
    ledger.images[first.image].owner=first.tile;ledger.epochId=L"retired epoch";Check(!Owned(first,ledger),"retired ownership epoch is rejected");
    std::wstring value=L"setup",error;Check(NormalizeSetting(L"Gallery.Tile.1",L"ClickAction",value,error)&&value==L"Setup","per-tile click action is typed and canonical");
    value=L"run.exe";Check(!NormalizeSetting(L"Gallery.Tile.1",L"ClickAction",value,error),"gallery click action cannot be an arbitrary program");
    std::cout<<checks<<" gallery activation checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

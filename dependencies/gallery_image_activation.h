#pragma once
// Chaseable gallery payloads contain ownership IDs only, never a file path or
// a command to execute. Resolve a token through a validated local record.
#include "gallery_engine.h"
#include <sstream>
namespace aip::gallery::activation {
struct Token {std::wstring owner,group,tile,epoch,image;};
struct Record {Token token;std::wstring path,fileIdentity;std::uint64_t bytes=0,modified=0;};
inline bool Hex(const std::wstring& text,size_t count){return text.size()==count&&std::all_of(text.begin(),text.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');});}
inline bool Image(const std::wstring& value){return value.compare(0,7,L"sha256:")==0&&Hex(value.substr(7),64);}
inline bool Valid(const Token& t){return Hex(t.owner,16)&&!NativeTileId(t.group,t.tile).empty()&&detail::Text(t.epoch,MaxIdentityUnits,false)&&Image(t.image);}
inline std::wstring EncodedText(const std::wstring& text){const auto encoded=detail::Hex(text);return {encoded.begin(),encoded.end()};}
inline std::wstring Encode(const Token& t){return Valid(t)?L"aip-gallery-1."+t.owner+L"."+EncodedText(t.group)+L"."+
    EncodedText(t.tile)+L"."+EncodedText(t.epoch)+L"."+t.image.substr(7):std::wstring{};}
inline bool Decode(const std::wstring& text,Token& token){
    if(text.size()>4096)return false;std::vector<std::wstring> fields;size_t at=0;
    for(;;){const auto end=text.find(L'.',at);fields.push_back(text.substr(at,end==std::wstring::npos?end:end-at));if(end==std::wstring::npos)break;at=end+1;}
    if(fields.size()!=6||fields[0]!=L"aip-gallery-1")return false;Token result;result.owner=fields[1];result.image=L"sha256:"+fields[5];
    const auto decode=[](const std::wstring& value,std::wstring& target){std::string ascii;for(const auto ch:value){if(static_cast<std::uint32_t>(ch)>127)return false;ascii+=static_cast<char>(ch);}return detail::Unhex(ascii,target);};
    if(!decode(fields[2],result.group)||!decode(fields[3],result.tile)||!decode(fields[4],result.epoch)||!Valid(result))return false;
    token=std::move(result);return true;
}
inline bool Same(const Token& a,const Token& b){return a.owner==b.owner&&a.group==b.group&&a.tile==b.tile&&a.epoch==b.epoch&&a.image==b.image;}
inline bool PhotoPath(const std::wstring& path){
    if(path.empty()||path.size()>32768||path.find_first_of(L"\r\n\t")!=std::wstring::npos||path.find(L'\0')!=std::wstring::npos)return false;
    auto value=detail::Lower(path);if(value.compare(0,4,L"\\\\?\\")==0)value.erase(0,4);
    if(value.size()<4||value[1]!=L':'||value[2]!=L'\\'||value.find(L':',2)!=std::wstring::npos)return false;
    const auto dot=value.find_last_of(L'.');if(dot==std::wstring::npos)return false;value.erase(0,dot);
    return value==L".png"||value==L".jpg"||value==L".jpeg"||value==L".bmp"||value==L".gif"||value==L".tif"||value==L".tiff";
}
inline bool Valid(const Record& r){return Valid(r.token)&&PhotoPath(r.path)&&detail::Text(r.fileIdentity,256,false)&&r.bytes>0&&r.bytes<=256u*1024u*1024u&&r.modified>0;}
inline std::wstring Serialize(const Record& record){
    if(!Valid(record))return {};return L"AIP_GALLERY_CLICK_1\n"+Encode(record.token)+L"\n"+record.path+L"\n"+record.fileIdentity+L"\n"+
        std::to_wstring(record.bytes)+L" "+std::to_wstring(record.modified)+L"\n";
}
inline bool Parse(const std::wstring& text,const Token& expected,Record& output){
    if(text.empty()||text.size()>40000||!Valid(expected))return false;std::wistringstream in(text);std::wstring version,token,line;Record r;
    if(!std::getline(in,version)||version!=L"AIP_GALLERY_CLICK_1"||!std::getline(in,token)||!Decode(token,r.token)||!Same(r.token,expected)||
        !std::getline(in,r.path)||!std::getline(in,r.fileIdentity)||!(in>>r.bytes>>r.modified)||!Valid(r))return false;
    if(in>>line)return false;output=std::move(r);return true;
}
inline bool Owned(const Token& t,const Ledger& ledger){const auto image=ledger.images.find(t.image);return Valid(t)&&
    ledger.groupId==t.group&&ledger.epochId==t.epoch&&image!=ledger.images.end()&&image->second.owner==t.tile;}
}

#define NOMINMAX
#define DESKTOPSTUB_INERT_HARDWARE
#include "../dependencies/tile_registration_binding_win32.inc"
#include <iostream>
#include <map>

namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
using namespace aip::tile_registration;
using namespace aip::tile_registration::binding_native;
struct Fixture {
    Context context;ActivationBinding binding;std::map<std::wstring,File> files;unsigned profileContents=0,hostContents=0;
    Fixture(){
        context.userSid=L"SID:S-1-5-21-111-222-333-1001";binding.profile=L"C:\\User\\profile.ini";binding.hostPath=L"D:\\App\\DesktopStub.exe";
        binding.tileId=L"prayer-one";binding.hostProduct=L"DesktopStub";binding.hostAumid=L"Main.Identity_123456789abcd!App";
        binding.publisher=L"CN=DesktopStub.Test";
        std::wstring normalized;aip::named_objects::StableIdentityHash(context.userSid,binding.userScope);
        aip::named_objects::NormalizeFileIdentity(binding.profile,normalized);aip::named_objects::StableIdentityHash(normalized,binding.profileScope);
        aip::named_objects::StableIdentityHash(binding.tileId,binding.tileScope);binding.identityName=IdentityName(binding.userScope,binding.profileScope,binding.tileScope);
        context.packageName=binding.identityName;context.publisher=binding.publisher;context.applicationId=L"Tile";
        context.packageRoot=L"C:\\Cache\\"+binding.userScope+L"\\"+binding.profileScope+L"\\"+binding.tileScope;
        context.executable=context.packageRoot+L"\\"+LauncherFileName;
        Put(context.packageRoot,false,true);Put(context.executable,false,false);Put(binding.profile,false,false);Put(binding.hostPath,false,false);
        ResetText();
    }
    File& At(const std::wstring& path){std::wstring normalized;Normalize(path,normalized);return files.at(normalized);}
    void Put(const std::wstring& path,bool contents,bool directory,const std::wstring& text={}){
        (void)contents;File file;Normalize(path,file.finalPath);file.ordinary=true;file.directory=directory;file.ownerSid=context.userSid;file.text=text;files[file.finalPath]=file;
    }
    void ResetText(){
        std::wstring text,error,normalized;WriteActivationBinding(binding,text,error);aip::named_objects::NormalizeFileIdentity(binding.profile,normalized);
        Put(context.packageRoot+L"\\"+ActivationBindingFileName,true,false,text);
        Put(context.packageRoot+L"\\registration.owner",true,false,ActivationOwnerText(binding,normalized));
    }
    Hooks hooks(){Hooks hooks;
        hooks.context=[this](Context& out,std::wstring&){out=context;return true;};
        hooks.file=[this](const std::wstring& path,bool contents,bool,File& out,std::wstring& error){
            if(contents&&Equal(path,binding.profile))++profileContents;if(contents&&Equal(path,binding.hostPath))++hostContents;
            std::wstring normalized;if(!Normalize(path,normalized)||!files.count(normalized)){error=L"Fixture file absent.";return false;}out=files[normalized];return true;
        };return hooks;
    }
    bool LoadIt(){auto hook=hooks();ActivationBinding output;std::wstring error;return Load(output,error,&hook)&&output.profile==binding.profile&&output.hostAumid==binding.hostAumid;}
};
void Injected(){
    Fixture base;Check(base.LoadIt(),"owned package identity root user and exact profile/tile binding load without any native package operation");
    Fixture unpackaged;unpackaged.binding.hostAumid.clear();unpackaged.ResetText();Check(unpackaged.LoadIt(),"an owned Registration tile can bind an unpackaged main host without guessing an AUMID");
    Check(base.profileContents==0&&base.hostContents==0,"loading an activation binding never reads account INI or executable contents");
    Fixture prefixed;prefixed.context.packageRoot=L"\\\\?\\"+prefixed.context.packageRoot;prefixed.context.executable=L"\\\\?\\"+prefixed.context.executable;
    Check(prefixed.LoadIt(),"equivalent extended package context paths normalize before ordinary file validation");
    for(unsigned kind=0;kind<20;++kind){
        Fixture bad;
        switch(kind){
        case 0:bad.context.userSid=L"S-1-5-21-999-888-777-1002";break;
        case 1:bad.context.packageName=L"foreign.package";break;
        case 2:bad.context.publisher=L"CN=Foreign";break;
        case 3:bad.context.applicationId=L"Other";break;
        case 4:bad.context.executable=L"C:\\Other\\DesktopStubTileLauncher.exe";break;
        case 5:bad.At(bad.context.packageRoot).ordinary=false;break;
        case 6:bad.At(bad.context.executable).directory=true;break;
        case 7:bad.At(bad.context.packageRoot+L"\\"+ActivationBindingFileName).ownerSid=L"S-1-5-18";break;
        case 8:bad.At(bad.context.packageRoot+L"\\registration.owner").ownerSid=L"S-1-5-18";break;
        case 9:bad.At(bad.context.packageRoot+L"\\"+ActivationBindingFileName).finalPath=L"C:\\OUTSIDE\\REGISTRATION.ACTIVATION";break;
        case 10:bad.At(bad.context.packageRoot+L"\\registration.owner").text+=L"extra\r\n";break;
        case 11:bad.At(bad.binding.profile).ownerSid=L"S-1-5-18";break;
        case 12:bad.At(bad.binding.profile).ordinary=false;break;
        case 13:bad.At(bad.binding.hostPath).ordinary=false;break;
        case 14:bad.At(bad.context.packageRoot+L"\\"+ActivationBindingFileName).text=L"incomplete";break;
        case 15:bad.binding.profile=L"C:\\Other\\profile.ini";bad.ResetText();break;
        case 16:bad.binding.tileId=L"other-tile";bad.ResetText();break;
        case 17:bad.context.packageRoot=L"\\\\server\\cache";break;
        case 18:bad.context.executable=bad.context.packageRoot+L"\\Another.exe";break;
        case 19:bad.At(bad.binding.hostPath).finalPath=L"C:\\OTHER\\HOST.EXE";break;
        }
        Check(!bad.LoadIt(),"foreign malformed linked relocated or mismatched binding fails closed");
    }
    ActivationBinding value;std::wstring error;
    Check(!Load(value,error)&&error.find(L"inert")!=std::wstring::npos,"inert default rejects native package and profile reads before any platform operation");
    auto hooks=base.hooks();hooks.context={};Check(!Load(value,error,&hooks),"an omitted injected context never falls through to native discovery");
    hooks=base.hooks();hooks.file={};Check(!Load(value,error,&hooks),"an omitted injected file reader never falls through to native I/O");
    hooks=base.hooks();hooks.context=[](Context&,std::wstring& error){error=L"No packaged identity.";return false;};
    Check(!Load(value,error,&hooks)&&error==L"No packaged identity.","unsupported or absent package context preserves a useful preflight error");
    hooks=base.hooks();hooks.file=[](const std::wstring&,bool,bool,File&,std::wstring&)->bool{throw 1;};
    Check(!Load(value,error,&hooks)&&!error.empty(),"unexpected metadata adapter failure is contained before activation");
    auto text=base.At(base.context.packageRoot+L"\\"+ActivationBindingFileName).text;
    for(std::size_t index=0;index<text.size();index+=13){auto malformed=text;malformed[index]=L'\0';ActivationBinding kept=base.binding;
        Check(!ReadActivationBinding(malformed,kept,error)&&kept.profile==base.binding.profile,"bounded parser rejects embedded nulls without partially replacing output");}
}
void NativeFiles(){
    wchar_t temporary[MAX_PATH]{};Check(GetTempPathW(_countof(temporary),temporary)!=0,"private metadata fixture resolves its ordinary temporary parent");
    const auto root=std::wstring(temporary)+L"DesktopStubBindingFiles-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const bool made=CreateDirectoryW(root.c_str(),nullptr)!=FALSE;Check(made,"metadata fixture creates a unique owned root");if(!made)return;
    const auto path=root+L"\\binding.txt";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    const std::string text="AIP public metadata fixture";DWORD written=0;
    const bool saved=file!=INVALID_HANDLE_VALUE&&WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size();
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);Check(saved,"metadata fixture writes only synthetic public text");
    File metadata;std::wstring error,sid;aip::named_objects::CurrentUserIdentity(sid);
    Check(saved&&ReadFile(path,true,false,metadata,error)&&metadata.ordinary&&!metadata.directory&&metadata.text==L"AIP public metadata fixture"&&Equal(metadata.ownerSid,sid),
        "actual local reader validates file ownership final path and bounded UTF-8 contents");
    File onlyMetadata;Check(ReadFile(path,false,false,onlyMetadata,error)&&onlyMetadata.text.empty(),"metadata-only reads do not read file contents");
    Check(!ReadFile(root,true,false,metadata,error),"file reader rejects directories in place of activation records");
    Check(!ReadFile(path,false,true,metadata,error),"directory reader rejects ordinary files in place of the package root");
    Check(!PlainLocalPath(L"\\\\server\\share\\binding.txt")&&!PlainLocalPath(L"C:\\missing-binding-directory\\binding.txt"),"network and missing paths never become ordinary activation input");
    file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    const BYTE invalid[]={0xff,0xff};written=0;const bool invalidSaved=file!=INVALID_HANDLE_VALUE&&WriteFile(file,invalid,sizeof(invalid),&written,nullptr)&&written==sizeof(invalid);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    Check(invalidSaved&&!ReadFile(path,true,false,metadata,error),"actual reader refuses malformed UTF-8 instead of decoding a different binding");
    file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    const std::vector<BYTE> oversized(MaximumActivationBytes+1,'x');written=0;
    const bool oversizedSaved=file!=INVALID_HANDLE_VALUE&&WriteFile(file,oversized.data(),static_cast<DWORD>(oversized.size()),&written,nullptr)&&written==oversized.size();
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    Check(oversizedSaved&&!ReadFile(path,true,false,metadata,error),"actual reader checks its byte limit before allocating or parsing an oversized record");
    Check(DeleteFileW(path.c_str())!=FALSE&&RemoveDirectoryW(root.c_str())!=FALSE,"metadata fixture removes only its exact private file and root");
}
}
int main(){Injected();NativeFiles();std::cout<<checks<<" Registration activation binding checks; "<<failures<<" failures\n";return failures?1:0;}

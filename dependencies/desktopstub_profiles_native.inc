#ifndef AIP_DESKTOPSTUB_PROFILES_NATIVE_INC
#define AIP_DESKTOPSTUB_PROFILES_NATIVE_INC
#include "desktopstub_profiles.h"
#include "baseline_app.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <appmodel.h>
#include "core.inc"
#include "config_ini.inc"
#include <aclapi.h>
#include <shlobj.h>
#include <functional>
#include <memory>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"advapi32.lib")
namespace aip { namespace profiles { namespace native {
struct Context {std::wstring ownerSid,userScope,installScope,sourceRoot,baseRoot,mainIniPath,runtimeGeneration,runtimeId,runtimeProfileKey,runtimeIniPath;std::vector<std::wstring> legacyBaseRoots;};
inline bool PackageFamily(const PackageIdentity&,std::wstring&);
struct Operations {
    std::function<bool(const std::wstring&,bool&,std::wstring&)> exists;
    std::function<bool(const PackageIdentity&,std::vector<std::wstring>&,std::wstring&)> registeredRoots;
    std::function<bool(const PackageIdentity&,const std::wstring&,bool&,std::wstring&)> registeredAtSource;
    std::function<bool(const std::wstring&,std::wstring&,bool&,std::wstring&)> read;
    std::function<bool(const std::wstring&,const std::wstring&,bool,std::wstring&)> write;
    std::function<bool(const std::wstring&,std::wstring&)> directory;
    std::function<bool(const std::wstring&,const std::wstring&,std::wstring&)> copy;
    std::function<bool(const std::wstring&,const std::wstring&,std::wstring&)> backup;
    std::function<bool(const std::wstring&,const std::wstring&,std::wstring&)> owner;
    std::function<bool(std::wstring&)> random;
    std::function<bool(const std::wstring&,const std::function<bool()>&,std::wstring&)> locked;
    std::function<bool(const std::wstring&,const std::wstring&,DWORD&,HANDLE&,std::wstring&)> launch;
    std::function<bool(HANDLE,DWORD,DWORD&,std::wstring&)> wait;
    std::function<void(HANDLE)> close;
    std::function<bool(const std::wstring&,std::wstring&)> erase;
    std::function<HWND(const std::wstring&)> window;
    std::function<bool(HWND,WPARAM)> signal;
};
inline bool CanAdoptOwnedManifest(const Operations& io,const PackageIdentity& identity,const std::wstring& sourceRoot,bool& adopt,std::wstring& error){
    adopt=false;bool registered=false;if(!io.registeredAtSource||!io.registeredAtSource(identity,sourceRoot,registered,error))return false;
    adopt=CanAdoptLegacy(true,registered,registered);return true;
}
inline std::wstring Parent(const std::wstring& path){const auto split=path.find_last_of(L'\\');return split==std::wstring::npos?std::wstring{}:path.substr(0,split);}
inline bool PlainPath(const std::wstring& path,bool missingAllowed,std::wstring& error){
    if(!AbsoluteIniPath(path)){error=L"A profile path must be an absolute normalized Windows path.";return false;}
    std::size_t from=path[1]==L':'?3u:2u;
    for(;;){const auto end=path.find(L'\\',from);const auto current=end==std::wstring::npos?path:path.substr(0,end);
        if(current.size()>2){const auto attr=GetFileAttributesW(current.c_str());
            if(attr==INVALID_FILE_ATTRIBUTES){const auto failure=GetLastError();if(!missingAllowed||(failure!=ERROR_FILE_NOT_FOUND&&failure!=ERROR_PATH_NOT_FOUND)){error=L"The profile path is unavailable.";return false;}return true;}
            if(attr&FILE_ATTRIBUTE_REPARSE_POINT){error=L"Profile files and runtime directories cannot pass through reparse points.";return false;}
            if(end!=std::wstring::npos&&!(attr&FILE_ATTRIBUTE_DIRECTORY)){error=L"A profile parent is not a directory.";return false;}
        }
        if(end==std::wstring::npos)break;from=end+1;
    }
    return true;
}
inline bool FileOwner(const std::wstring& path,const std::wstring& ownerSid,std::wstring& error){
    if(!PlainPath(path,false,error))return false;
    HANDLE file=CreateFileW(path.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=L"The profile file's Windows owner could not be checked.";return false;}
    PSID owner=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;LPWSTR sid=nullptr;
    const auto status=GetSecurityInfo(file,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&descriptor);
    const bool converted=status==ERROR_SUCCESS&&owner&&ConvertSidToStringSidW(owner,&sid);
    const bool matches=converted&&(ownerSid==std::wstring(L"SID:")+sid||ownerSid==sid);
    if(sid)LocalFree(sid);if(descriptor)LocalFree(descriptor);CloseHandle(file);
    if(!matches)error=L"This profile file belongs to another Windows user. Create a separate profile in your own Windows session; existing settings were kept.";
    return matches;
}
inline bool RandomId(std::wstring& output){
    HCRYPTPROV provider=0;BYTE bytes[16]{};if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return false;
    const bool ok=CryptGenRandom(provider,16,bytes)!=FALSE;CryptReleaseContext(provider,0);if(!ok)return false;
    static const wchar_t hex[]=L"0123456789abcdef";output.clear();for(const auto c:bytes){output+=hex[c>>4];output+=hex[c&15];}return true;
}
inline bool ReadText(const std::wstring& path,std::wstring& text,bool& missing,std::wstring& error){
    missing=false;text.clear();if(!PlainPath(path,true,error))return false;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){const auto failure=GetLastError();missing=failure==ERROR_FILE_NOT_FOUND||failure==ERROR_PATH_NOT_FOUND;
        if(!missing)error=L"The profile file could not be opened.";return missing;}
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{};
    if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(file,&length)||length.QuadPart<0||length.QuadPart>16*1024*1024){CloseHandle(file);error=L"The profile file is not an ordinary bounded file.";return false;}
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length.QuadPart));DWORD count=0;
    const bool read=bytes.empty()||ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();CloseHandle(file);
    if(!read){error=L"The profile file could not be read completely.";return false;}if(bytes.empty())return true;
    if(bytes.size()>=2&&((bytes[0]==0xff&&bytes[1]==0xfe)||(bytes[0]==0xfe&&bytes[1]==0xff))){
        if(bytes.size()%2){error=L"The profile file has invalid UTF-16 text.";return false;}const bool big=bytes[0]==0xfe;
        for(std::size_t i=2;i<bytes.size();i+=2)text+=static_cast<wchar_t>(big?(bytes[i]<<8)|bytes[i+1]:bytes[i]|(bytes[i+1]<<8));return true;
    }
    const auto skip=bytes.size()>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf?3u:0u;
    const auto size=static_cast<int>(bytes.size()-skip);const auto countWide=size?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(bytes.data()+skip),size,nullptr,0):0;
    if(size&&!countWide){error=L"The profile file must contain valid UTF-8 or BOM-marked UTF-16 text.";return false;}
    text.resize(static_cast<std::size_t>(countWide));return !countWide||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(bytes.data()+skip),size,&text[0],countWide)==countWide;
}
inline bool EnsureDirectory(const std::wstring& path,std::wstring& error){
    if(!PlainPath(path,true,error))return false;const auto result=SHCreateDirectoryExW(nullptr,path.c_str(),nullptr);
    if(result!=ERROR_SUCCESS&&result!=ERROR_ALREADY_EXISTS&&result!=ERROR_FILE_EXISTS){error=L"The owned profile directory could not be created.";return false;}
    const auto attr=GetFileAttributesW(path.c_str());return PlainPath(path,false,error)&&attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY)!=0;
}
inline bool WriteText(const std::wstring& path,const std::wstring& text,bool newOnly,std::wstring& error){
    if(!PlainPath(path,true,error)||text.size()>16*1024*1024)return false;
    const auto existing=GetFileAttributesW(path.c_str());
    if(newOnly&&existing!=INVALID_FILE_ATTRIBUTES){error=L"The new profile destination already exists; it was not overwritten.";return false;}
    if(existing!=INVALID_FILE_ATTRIBUTES&&(existing&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){error=L"The profile destination is not an ordinary file.";return false;}
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(size==0&&!text.empty()){error=L"Profile text contains invalid Unicode.";return false;}
    std::vector<BYTE> bytes{0xef,0xbb,0xbf};bytes.resize(static_cast<std::size_t>(size)+3);
    if(size&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),reinterpret_cast<char*>(bytes.data()+3),size,nullptr,nullptr)!=size)return false;
    std::wstring nonce;if(!RandomId(nonce))return false;const auto temporary=path+L".tmp."+nonce;
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    struct ReleaseSecurity{PSECURITY_DESCRIPTOR& value;~ReleaseSecurity(){if(value)LocalFree(value);}} releaseSecurity{descriptor};
    if(existing!=INVALID_FILE_ATTRIBUTES&&GetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&descriptor)!=ERROR_SUCCESS){error=L"The existing profile's access permissions could not be preserved.";return false;}
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,descriptor?&security:nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=L"The profile staging file could not be created.";return false;}DWORD written=0;
    const bool ready=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);
    if(!ready){DeleteFileW(temporary.c_str());error=L"The profile staging file could not be saved.";return false;}
    if(existing!=INVALID_FILE_ATTRIBUTES){const auto backup=path+L".before-profile-manager.bak";const auto attr=GetFileAttributesW(backup.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES){if(!CopyFileW(path.c_str(),backup.c_str(),TRUE)){DeleteFileW(temporary.c_str());error=L"The original profile backup could not be created.";return false;}}
        else if(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){DeleteFileW(temporary.c_str());error=L"The profile backup is not an ordinary file.";return false;}}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(newOnly?0:MOVEFILE_REPLACE_EXISTING))){DeleteFileW(temporary.c_str());error=L"The staged profile could not be committed; the prior file was kept.";return false;}
    return true;
}
inline bool CopyRuntimeFile(const std::wstring& source,const std::wstring& destination,std::wstring& error){
    if(!PlainPath(source,false,error)||!PlainPath(destination,true,error))return false;
    std::wstring nonce;if(!RandomId(nonce))return false;const auto temporary=destination+L".tmp."+nonce;
    if(!CopyFileW(source.c_str(),temporary.c_str(),TRUE)){error=L"A required DesktopStub runtime file could not be staged.";return false;}
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());error=L"A profile runtime is in use or could not be updated; stop that profile and retry.";return false;}return true;
}
inline bool SameUserProcess(HANDLE process){
    HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))return false;aip::UniqueKernelHandle token(raw);
    DWORD size=0;GetTokenInformation(token.Get(),TokenUser,nullptr,0,&size);if(!size||size>65536)return false;
    std::vector<BYTE> data(size);if(!GetTokenInformation(token.Get(),TokenUser,data.data(),size,&size))return false;
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))return false;
    std::wstring current;const bool result=aip::named_objects::CurrentUserIdentity(current)&&current==std::wstring(L"SID:")+sid;LocalFree(sid);return result;
}
inline bool SameUserWindow(HWND window){
    DWORD pid=0;if(!GetWindowThreadProcessId(window,&pid)||!pid)return false;DWORD session=0,currentSession=0;
    if(!ProcessIdToSessionId(pid,&session)||!ProcessIdToSessionId(GetCurrentProcessId(),&currentSession)||session!=currentSession)return false;
    aip::UniqueKernelHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));return process&&SameUserProcess(process.Get());
}
inline bool RegisteredRoots(const PackageIdentity& identity,std::vector<std::wstring>& roots,std::wstring& error){
    roots.clear();std::wstring family;if(!PackageFamily(identity,family)){error=L"Windows could not resolve the package family.";return false;}
    UINT32 count=0,length=0;auto status=GetPackagesByPackageFamily(family.c_str(),&count,nullptr,&length,nullptr);
    if(status==ERROR_SUCCESS&&!count)return true;
    if(status!=ERROR_INSUFFICIENT_BUFFER||count>128||length>65536){error=L"Current-user package registration could not be verified.";return false;}
    std::vector<PWSTR> names(count);std::vector<wchar_t> text(length);
    if(GetPackagesByPackageFamily(family.c_str(),&count,names.data(),&length,text.data())!=ERROR_SUCCESS){error=L"Current-user package registration changed during verification. Retry shortly.";return false;}
    for(UINT32 i=0;i<count;++i){UINT32 size=0;status=GetPackagePathByFullName(names[i],&size,nullptr);
        if(status!=ERROR_INSUFFICIENT_BUFFER||size<2||size>32768){error=L"A registered package path could not be verified.";return false;}
        std::vector<wchar_t> path(size);if(GetPackagePathByFullName(names[i],&size,path.data())!=ERROR_SUCCESS){error=L"A registered package path changed during verification.";return false;}roots.emplace_back(path.data());}
    return true;
}
inline bool RegisteredAtSource(const PackageIdentity& identity,const std::wstring& source,bool& registered,std::wstring& error){
    registered=false;std::wstring expected;if(!aip::named_objects::NormalizeFileIdentity(source,expected)){error=L"The expected package source path is invalid.";return false;}
    UINT32 length=128;wchar_t current[128]{};std::wstring wanted;
    const auto status=GetCurrentPackageFamilyName(&length,current);
    if(status==ERROR_SUCCESS&&PackageFamily(identity,wanted)&&_wcsicmp(current,wanted.c_str())==0){std::vector<wchar_t> module(32768);const auto count=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));std::wstring actual;
        if(count&&count<module.size()&&aip::named_objects::NormalizeFileIdentity(Parent(std::wstring(module.data(),count)),actual)&&actual==expected){registered=true;return true;}}
    else if(status!=ERROR_SUCCESS&&status!=APPMODEL_ERROR_NO_PACKAGE){error=L"The running application's package identity could not be verified.";return false;}
    std::vector<std::wstring> roots;if(!RegisteredRoots(identity,roots,error))return false;
    for(const auto& root:roots){std::wstring actual;if(!aip::named_objects::NormalizeFileIdentity(root,actual)){error=L"A registered package path is invalid.";return false;}if(actual==expected){registered=true;return true;}}return true;
}
inline Operations SystemOperations(){
    Operations io;io.read=ReadText;io.write=WriteText;io.directory=EnsureDirectory;io.copy=CopyRuntimeFile;io.owner=FileOwner;io.random=RandomId;
    io.registeredRoots=RegisteredRoots;io.registeredAtSource=RegisteredAtSource;
    io.backup=[](const std::wstring& source,const std::wstring& destination,std::wstring& error){if(!PlainPath(source,false,error)||!PlainPath(destination,true,error))return false;
        if(!CopyFileW(source.c_str(),destination.c_str(),TRUE)){error=L"The identity migration's non-overwriting backup could not be created.";return false;}return true;};
    io.exists=[](const std::wstring& path,bool& exists,std::wstring& error){if(!PlainPath(path,true,error))return false;const auto attr=GetFileAttributesW(path.c_str());exists=attr!=INVALID_FILE_ATTRIBUTES;const auto code=exists?ERROR_SUCCESS:GetLastError();if(!exists&&code!=ERROR_FILE_NOT_FOUND&&code!=ERROR_PATH_NOT_FOUND){error=L"The profile file status could not be checked.";return false;}return true;};
    io.locked=[](const std::wstring& path,const std::function<bool()>& action,std::wstring& error){aip::IniWriteMutexGuard guard(path,3000);if(!guard.IsLocked()){error=L"Another process is changing the profile list or identity. Retry shortly.";return false;}return action();};
    io.launch=[](const std::wstring& executable,const std::wstring& arguments,DWORD& pid,HANDLE& handle,std::wstring& error){
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};auto command=QuoteArgument(executable)+L" "+arguments;
        if(!CreateProcessW(executable.c_str(),&command[0],nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,nullptr,Parent(executable).c_str(),&startup,&process)){error=L"The selected profile could not be started.";return false;}
        CloseHandle(process.hThread);pid=process.dwProcessId;handle=process.hProcess;return true;
    };
    io.wait=[](HANDLE process,DWORD timeout,DWORD& code,std::wstring& error){if(!process||WaitForSingleObject(process,timeout)!=WAIT_OBJECT_0||!GetExitCodeProcess(process,&code)){
        error=L"The profile command did not finish before its bounded wait. It was not terminated; check that profile before retrying.";return false;}return true;};
    io.close=[](HANDLE handle){if(handle)CloseHandle(handle);};
    io.erase=[](const std::wstring& path,std::wstring& error){if(!PlainPath(path,true,error))return false;if(DeleteFileW(path.c_str()))return true;const auto code=GetLastError();return code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND;};
    io.window=[](const std::wstring& ini){const auto identity=aip::BuildPathScopedInstanceIdentity(L"DesktopStub",L"DesktopStub.RestoreRunningInstance",L"DesktopStubTrayWnd",L"DesktopStub",ini);return aip::FindInstanceWindow(L"DesktopStubTrayWnd",identity.windowTitle.c_str(),0,0);};
    io.signal=[](HWND window,WPARAM command){if(!SameUserWindow(window))return false;const auto count=GetWindowTextLengthW(window);if(count<=0||count>1024)return false;
        std::wstring title(static_cast<std::size_t>(count)+1,L'\0');if(!GetWindowTextW(window,&title[0],count+1))return false;title.resize(static_cast<std::size_t>(count));
        const auto suffix=title.find_last_of(L'.');if(suffix==std::wstring::npos)return false;const auto message=RegisterWindowMessageW((L"DesktopStub.RestoreRunningInstance"+title.substr(suffix)).c_str());
        DWORD_PTR result=0;return message&&SendMessageTimeoutW(window,message,command,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result)&&result==aip::INSTANCE_REQUEST_HANDLED;
    };return io;
}
inline std::wstring IdentityGeneration(const IdentityRecord& record){std::wstring result;if(!Valid(record)||!aip::named_objects::StableIdentityHash(LowerAscii(record.package.name)+L"|"+record.package.publisher,result))return {};return LowerAscii(result);}
inline bool PackageFamily(const PackageIdentity& identity,std::wstring& family){
    if(!Valid(identity))return false;PACKAGE_ID id{};id.name=const_cast<wchar_t*>(identity.name.c_str());id.publisher=const_cast<wchar_t*>(identity.publisher.c_str());
    UINT32 length=128;wchar_t buffer[128]{};if(PackageFamilyNameFromId(&id,&length,buffer)!=ERROR_SUCCESS)return false;family=buffer;return true;
}
inline std::wstring RuntimeOwner(const Context& c,const ProfileEntry& e,const std::wstring& generation){std::wostringstream out;out<<L"AIP_PROFILE_RUNTIME_2\n"<<std::quoted(c.ownerSid)<<L' '<<std::quoted(c.installScope)<<L' '<<std::quoted(e.id)<<L' '<<std::quoted(e.profileKey)<<L' '<<std::quoted(c.sourceRoot)<<L' '<<std::quoted(generation)<<L' '<<std::quoted(e.iniPath)<<L'\n';return out.str();}
inline bool IdentityFromText(const std::wstring& text,std::optional<IdentityRecord>& record,std::wstring& error){const auto doc=aip::ParseIniDocument(text);return ReadIdentity([&](const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(doc,L"ProfileIdentity",key,value)?value:std::wstring(fallback);},record,error);}
class Store {
public:
    Context context;Operations io;
    std::optional<IdentityPlan> pendingIdentity;
    bool recoveredProfileIdentity=false;
    explicit Store(Context c,Operations operations=SystemOperations()):context(std::move(c)),io(std::move(operations)){}
    std::wstring RegistryPath()const{return context.baseRoot+L"\\profiles.ini";}
    bool ImportLegacyRegistry(std::wstring& error)const{
        if(context.legacyBaseRoots.empty())return true;
        return io.locked(RegistryPath(),[&]{
            bool exists=false;if(!io.exists||!io.exists(RegistryPath(),exists,error))return false;
            if(exists)return io.owner(RegistryPath(),context.ownerSid,error);
            std::wstring selected;
            for(const auto& base:context.legacyBaseRoots){
                const auto path=base+L"\\profiles.ini";if(!io.exists(path,exists,error))return false;if(!exists)continue;
                if(!io.owner(path,context.ownerSid,error))return false;
                std::wstring text;bool missing=false;Registry legacy;
                if(!io.read(path,text,missing,error)||missing||!ParseRegistry(text,context.ownerSid,legacy,error))return false;
                for(const auto& entry:legacy.profiles){IdentityRecord record;if(!CurrentIdentity(entry,record,error))return false;}
                const auto canonical=SerializeRegistry(legacy);if(canonical.empty())return false;
                if(!selected.empty()&&selected!=canonical){error=L"Separate old package profile lists disagree. Existing profiles were kept; choose which list to migrate explicitly.";return false;}
                selected=canonical;
            }
            return selected.empty()||(io.directory(context.baseRoot,error)&&io.write(RegistryPath(),selected,true,error));
        },error);
    }
    std::wstring ProfileRoot(const ProfileEntry& e)const{return context.baseRoot+L"\\"+LowerAscii(e.id);}
    bool CurrentIdentity(const ProfileEntry& entry,IdentityRecord& output,std::wstring& error)const{
        std::wstring text;bool missing=false;if(!io.owner(entry.iniPath,context.ownerSid,error)||!io.read(entry.iniPath,text,missing,error)||missing)return false;
        std::optional<IdentityRecord> value;if(!IdentityFromText(text,value,error)||!value||value->id!=entry.id||value->ownerSid!=context.ownerSid||value->profileKey!=entry.profileKey){error=L"The profile identity no longer matches its registered owner or INI.";return false;}output=*value;return true;
    }
    std::wstring RuntimeRootFor(const ProfileEntry& e,const IdentityRecord& record)const{const auto generation=IdentityGeneration(record);return generation.empty()?std::wstring{}:ProfileRoot(e)+L"\\runtime\\"+generation;}
    std::wstring RuntimeRoot(const ProfileEntry& e)const{IdentityRecord record;std::wstring error;return CurrentIdentity(e,record,error)?RuntimeRootFor(e,record):std::wstring{};}
    std::wstring RuntimeExe(const ProfileEntry& e)const{return RuntimeRoot(e)+L"\\DesktopStub.exe";}
    std::wstring OwnerText(const ProfileEntry& e)const{IdentityRecord record;std::wstring error;return CurrentIdentity(e,record,error)?RuntimeOwner(context,e,IdentityGeneration(record)):std::wstring{};}
    bool RegisteredFamilyAvailable(const IdentityRecord& identity,bool allowVerifiedSource,std::wstring& error)const{
        std::vector<std::wstring> roots;if(!io.registeredRoots||!io.registeredRoots(identity.package,roots,error))return false;
        if(roots.empty())return true;
        if(allowVerifiedSource){bool registered=false;if(!io.registeredAtSource||!io.registeredAtSource(identity.package,context.sourceRoot,registered,error))return false;if(registered)return true;}
        std::wstring expected;ProfileEntry entry{identity.id,L"",L"",identity.profileKey,false};
        if(!aip::named_objects::NormalizeFileIdentity(RuntimeRootFor(entry,identity),expected))return false;
        for(const auto& root:roots){std::wstring actual;if(!aip::named_objects::NormalizeFileIdentity(root,actual)||actual!=expected){error=L"This package identity is already registered at an unrelated location for the current user. Choose a different static name or Publisher.";return false;}}
        return true;
    }
    bool Load(Registry& registry,std::wstring& error)const{
        bool exists=false;if(!io.exists||!io.exists(RegistryPath(),exists,error))return false;
        if(!exists){registry={};registry.ownerSid=context.ownerSid;return true;}
        if(!io.owner(RegistryPath(),context.ownerSid,error))return false;
        std::wstring text;bool missing=false;if(!io.read(RegistryPath(),text,missing,error)||missing){error=L"The profile list changed while it was being read.";return false;}return ParseRegistry(text,context.ownerSid,registry,error);
    }
    bool Update(const std::function<bool(Registry&,std::wstring&)>& mutate,std::wstring& error){
        if(!io.directory(context.baseRoot,error))return false;
        return io.locked(RegistryPath(),[&]{Registry registry;if(!Load(registry,error)||!mutate(registry,error))return false;const auto text=SerializeRegistry(registry);return !text.empty()&&io.write(RegistryPath(),text,false,error);},error);
    }
    bool Identity(const std::wstring& ini,const std::wstring& key,const std::optional<PackageIdentity>& ownedExisting,IdentityRecord& output,std::wstring& error){
        pendingIdentity.reset();recoveredProfileIdentity=false;
        return io.locked(RegistryPath(),[&]{return io.locked(ini,[&]{if(!io.owner(ini,context.ownerSid,error))return false;std::wstring text;bool missing=false;if(!io.read(ini,text,missing,error)||missing){error=L"Create the profile INI before assigning its persistent identity.";return false;}
            const auto doc=aip::ParseIniDocument(text);const auto read=[&](const wchar_t* field,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(doc,L"ProfileIdentity",field,value)?value:std::wstring(fallback);};
            std::optional<IdentityRecord> stored;if(!ReadIdentity(read,stored,error))return false;
            // The settings file can intentionally be removed while this owned
            // coordinator metadata survives. Keep that path's persistent ID;
            // factory recreation must not append a second row for the same INI.
            Registry profiles;if(!Load(profiles,error))return false;
            const auto registeredPath=std::find_if(profiles.profiles.begin(),profiles.profiles.end(),[&](const auto& entry){return LowerAscii(entry.profileKey)==LowerAscii(key);});
            bool recovering=false;
            if(stored){
                const auto registeredId=std::find_if(profiles.profiles.begin(),profiles.profiles.end(),[&](const auto& entry){return LowerAscii(entry.id)==LowerAscii(stored->id);});
                if(registeredId!=profiles.profiles.end()&&LowerAscii(registeredId->profileKey)!=LowerAscii(key)){
                    error=L"This INI's saved profile ID belongs to a different registered INI. Existing profiles were kept; use an explicit profile migration.";return false;}
            }
            if(stored&&registeredPath!=profiles.profiles.end()&&LowerAscii(stored->id)!=LowerAscii(registeredPath->id)){
                std::wstring mainKey,requestedMode,requestedName,requestedPublisher,legacyName;
                aip::ReadIniValueFromDoc(doc,L"Profile",L"IdentityMode",requestedMode);
                aip::ReadIniValueFromDoc(doc,L"Profile",L"StaticPackageName",requestedName);
                aip::ReadIniValueFromDoc(doc,L"Profile",L"StaticPublisher",requestedPublisher);
                aip::ReadIniValueFromDoc(doc,L"Settings",L"ManifestIdentityName",legacyName);
                const bool stagedMain=aip::named_objects::NormalizeFileIdentity(context.mainIniPath,mainKey)&&LowerAscii(key)==LowerAscii(mainKey)&&
                    context.runtimeGeneration.empty()&&stored->ownerSid==context.ownerSid&&stored->profileKey==key&&
                    stored->mode==IdentityMode::Auto&&!stored->preservedLegacy&&Same(stored->package,AutomaticIdentity(context.userScope,stored->id))&&
                    (requestedMode.empty()||requestedMode==L"Auto")&&requestedName.empty()&&requestedPublisher.empty()&&legacyName.empty();
                if(!stagedMain){error=L"This INI and its registered profile path have different saved IDs. Existing profiles were kept; choose an explicit fresh profile or migration.";return false;}
                // Repair the partial first-run commit from older builds only
                // while its generated package is still entirely unregistered.
                std::vector<std::wstring> registeredRoots;
                if(!io.registeredRoots||!io.registeredRoots(stored->package,registeredRoots,error))return false;
                if(!registeredRoots.empty()){error=L"The replacement Main identity is already registered. Existing identities were kept; use an explicit profile migration.";return false;}
                stored.reset();recovering=true;
            }
            IdentityRequest request;request.ownerSid=context.ownerSid;request.userScope=context.userScope;request.profileKey=key;request.stored=stored;request.ownedExisting=ownedExisting;
            if(!stored&&registeredPath!=profiles.profiles.end()){request.candidateId=LowerAscii(registeredPath->id);recovering=true;}
            // Explicit legacy overrides are honored for a fresh profile, never
            // used to silently replace a saved package identity.
            std::wstring mode,name,publisher,legacyName,legacyPublisher;
            aip::ReadIniValueFromDoc(doc,L"Profile",L"IdentityMode",mode);
            aip::ReadIniValueFromDoc(doc,L"Profile",L"StaticPackageName",name);aip::ReadIniValueFromDoc(doc,L"Profile",L"StaticPublisher",publisher);
            aip::ReadIniValueFromDoc(doc,L"Settings",L"ManifestIdentityName",legacyName);aip::ReadIniValueFromDoc(doc,L"Settings",L"ManifestPublisher",legacyPublisher);
            if(!mode.empty()&&mode!=L"Auto"&&mode!=L"Static"){error=L"Profile.IdentityMode must be Auto or Static.";return false;}
            const bool changedLegacy=!legacyName.empty()&&(!stored||legacyName!=stored->package.name||(!legacyPublisher.empty()&&legacyPublisher.find(L'{')==std::wstring::npos&&legacyPublisher!=stored->package.publisher));
            if(mode==L"Static"||changedLegacy){request.mode=IdentityMode::Static;
                const auto fallback=stored?stored->package:ownedExisting?*ownedExisting:PackageIdentity{L"",L"CN=DesktopStub"};
                request.staticIdentity.name=!name.empty()?name:!legacyName.empty()?legacyName:fallback.name;
                request.staticIdentity.publisher=!publisher.empty()?publisher:!legacyPublisher.empty()&&legacyPublisher.find(L'{')==std::wstring::npos?legacyPublisher:fallback.publisher;}
            else if(mode==L"Auto"){request.mode=IdentityMode::Auto;request.migrateAutomatic=stored&&stored->preservedLegacy;}
            else if(stored)request.mode=stored->mode;
            if(request.mode==IdentityMode::Static&&request.staticIdentity.name.empty()&&stored)request.staticIdentity.name=stored->package.name;
            IdentityPlan plan;if(!PlanIdentity(request,plan,error))return false;
            if(plan.status==IdentityResolution::NeedsPersistentId){if(!io.random(request.candidateId)){error=L"A secure persistent profile identity could not be generated.";return false;}if(!PlanIdentity(request,plan,error))return false;}
            if(plan.status==IdentityResolution::OwnerMismatch){error=L"This saved identity belongs to another Windows user or INI path. Create a new profile; the existing identity and pins were kept.";return false;}
            const auto& selected=plan.status==IdentityResolution::MigrationRequired?plan.current:plan.proposed;
            ProfileEntry selectedEntry{selected.id,L"",ini,key,false};
            if(!RegisteredFamilyAvailable(selected,(ownedExisting&&Same(selected.package,*ownedExisting))||(stored&&Same(selected.package,stored->package)&&UsesOriginalMainRuntime(selectedEntry)),error))return false;
            if(plan.status==IdentityResolution::MigrationRequired){pendingIdentity=plan;output=plan.current;error.clear();return true;}
            if(plan.requiresWrite){
                std::wstring family;if(!PackageFamily(plan.proposed.package,family)){error=L"Windows rejected this profile's package identity or Publisher.";return false;}
                for(const auto& other:profiles.profiles)if(LowerAscii(other.profileKey)!=LowerAscii(key)){IdentityRecord identity;std::wstring otherFamily;
                    if(!CurrentIdentity(other,identity,error)||!PackageFamily(identity.package,otherFamily))return false;
                    if(_wcsicmp(family.c_str(),otherFamily.c_str())==0){error=L"Another current-user profile already uses this package identity. Choose a different static name or Publisher.";return false;}}
                for(const auto& field:IdentityValues(plan.proposed))if(!aip::WriteIniValueToText(text,L"ProfileIdentity",field.first.c_str(),field.second)){error=L"The profile identity could not be staged.";return false;}
                if(!io.write(ini,text,false,error))return false;}
            recoveredProfileIdentity=recovering;output=plan.proposed;return true;
        },error);},error);
    }
    bool Enroll(const ProfileEntry& entry,std::wstring& error){return Update([&](Registry& r,std::wstring& e){if(!AddOrUpdate(r,entry,e))return false;if(r.defaultProfileId.empty())r.defaultProfileId=entry.id;return true;},error);}
    bool Fresh(const std::wstring& name,const std::wstring& requestedIni,const std::function<std::wstring()>& initial,ProfileEntry& output,std::wstring& error){
        ProfileEntry entry;entry.name=name;if(!Plain(name,128)||!io.random(entry.id)){error=L"The new profile needs a valid name and secure identity.";return false;}
        if(!io.directory(ProfileRoot(entry),error))return false;
        entry.iniPath=aip::MakeAbsolutePath(requestedIni.empty()?ProfileRoot(entry)+L"\\DesktopStub.ini":requestedIni);
        if(!aip::named_objects::NormalizeFileIdentity(entry.iniPath,entry.profileKey)||!AbsoluteIniPath(entry.profileKey)){error=L"The new profile INI path is invalid.";return false;}
        // The initial callback contains compiled factory defaults only. Never
        // copy the current INI or another profile's accounts into this file.
        auto text=initial?initial():std::wstring{};if(text.empty()){error=L"Factory profile defaults are unavailable.";return false;}
        IdentityRecord identity{entry.id,context.ownerSid,entry.profileKey,IdentityMode::Auto,AutomaticIdentity(context.userScope,entry.id),false};
        for(const auto& field:IdentityValues(identity))if(!aip::WriteIniValueToText(text,L"ProfileIdentity",field.first.c_str(),field.second))return false;
        if(!aip::WriteIniValueToText(text,L"Profile",L"Name",entry.name)||!aip::WriteIniValueToText(text,L"Strings",L"TrayTip",L"DesktopStub — "+entry.name)||!io.directory(Parent(entry.iniPath),error)||!io.write(entry.iniPath,text,true,error))return false;
        if(!Enroll(entry,error))return false;output=std::move(entry);return true;
    }
    bool Find(const std::wstring& id,ProfileEntry& output,std::wstring& error)const{
        Registry registry;if(!Load(registry,error))return false;const auto found=std::find_if(registry.profiles.begin(),registry.profiles.end(),[&](const ProfileEntry& p){return LowerAscii(p.id)==LowerAscii(id);});
        if(found==registry.profiles.end()){error=L"That profile is not registered for this Windows user and installation.";return false;}output=*found;return true;
    }
    bool StageForIdentity(const ProfileEntry& entry,const IdentityRecord& identity,std::wstring& error){
        Registry check;check.ownerSid=context.ownerSid;check.profiles={entry};if(!Valid(check,error)||!io.owner(entry.iniPath,context.ownerSid,error))return false;
        if(!Valid(identity)||identity.id!=entry.id||identity.ownerSid!=context.ownerSid||identity.profileKey!=entry.profileKey){error=L"The proposed runtime identity does not belong to this profile.";return false;}
        IdentityRecord current;const bool verifiedOriginal=UsesOriginalMainRuntime(entry)&&CurrentIdentity(entry,current,error)&&Same(current.package,identity.package);
        if(!RegisteredFamilyAvailable(identity,verifiedOriginal,error))return false;
        const auto root=RuntimeRootFor(entry,identity),generation=IdentityGeneration(identity);
        if(root.empty()||!io.directory(root,error))return false;
        const auto marker=root+L"\\runtime.owner";std::wstring old;bool missing=false;
        if(!io.read(marker,old,missing,error))return false;if(!missing&&(old!=RuntimeOwner(context,entry,generation)||!io.owner(marker,context.ownerSid,error))){error=L"The profile runtime belongs to another profile or installation; it was kept.";return false;}
        if(missing&&!io.write(marker,RuntimeOwner(context,entry,generation),true,error))return false;
        for(const auto* name:{L"DesktopStub.exe",L"DesktopStubLiveTileBroker.exe",L"DesktopStubTileLauncher.exe"})if(!io.copy(context.sourceRoot+L"\\"+name,root+L"\\"+name,error))return false;
        return true;
    }
    bool Stage(const ProfileEntry& entry,std::wstring& error){IdentityRecord identity;return CurrentIdentity(entry,identity,error)&&StageForIdentity(entry,identity,error);}
    bool UsesOriginalMainRuntime(const ProfileEntry& entry)const{
        std::wstring key;if(!aip::named_objects::NormalizeFileIdentity(context.mainIniPath,key)||entry.profileKey!=key)return false;
        std::wstring text,error;bool missing=false;if(!io.owner(entry.iniPath,context.ownerSid,error)||!io.read(entry.iniPath,text,missing,error)||missing)return false;
        std::wstring managed;return !aip::ReadIniValueFromDoc(aip::ParseIniDocument(text),L"Profile",L"ManagedRuntime",managed)||managed!=L"1";
    }
    bool Start(const ProfileEntry& entry,const std::wstring& additional,DWORD& pid,HANDLE& process,std::wstring& error){
        if(UsesOriginalMainRuntime(entry)){IdentityRecord current;if(!CurrentIdentity(entry,current,error)||!RegisteredFamilyAvailable(current,true,error))return false;return io.launch(context.sourceRoot+L"\\DesktopStub.exe",LaunchArguments(entry)+(additional.empty()?L"":L" "+additional),pid,process,error);}
        if(!Stage(entry,error))return false;
        return io.launch(RuntimeExe(entry),LaunchArguments(entry)+(additional.empty()?L"":L" "+additional),pid,process,error);
    }
    bool RunOneShot(const ProfileEntry& entry,const std::wstring& additional,int& exitCode,std::wstring& error,bool runtime=false){
        exitCode=2;if(!io.owner(entry.iniPath,context.ownerSid,error))return false;
        if(runtime){IdentityRecord current;if(!CurrentIdentity(entry,current,error)||!RegisteredFamilyAvailable(current,UsesOriginalMainRuntime(entry),error))return false;}
        auto executable=context.sourceRoot+L"\\DesktopStub.exe";
        if(runtime&&!UsesOriginalMainRuntime(entry)){
            if(!io.window(entry.iniPath)){if(!Stage(entry,error))return false;}
            else{const auto marker=RuntimeRoot(entry)+L"\\runtime.owner";std::wstring text;bool missing=false;
                if(!io.owner(marker,context.ownerSid,error)||!io.read(marker,text,missing,error)||missing||text!=OwnerText(entry)){error=L"The running profile has no verified owned runtime.";return false;}}
            executable=RuntimeExe(entry);
        }
        DWORD pid=0,code=0;HANDLE process=nullptr;
        if(!io.launch(executable,LaunchArguments(entry)+(additional.empty()?L"":L" "+additional),pid,process,error))return false;
        const bool completed=io.wait&&io.wait(process,120000,code,error);if(io.close)io.close(process);
        if(!completed){exitCode=124;return false;}exitCode=static_cast<int>(code);return true;
    }
    bool Stop(const ProfileEntry& entry,std::wstring& error){
        if(!io.owner(entry.iniPath,context.ownerSid,error))return false;
        const auto window=io.window(entry.iniPath);if(!window){error=L"The selected profile is not running in this Windows session.";return false;}
        if(!io.signal(window,2)){error=L"The selected profile did not accept a graceful stop request.";return false;}return true;
    }
};
}}} // namespace aip::profiles::native
#endif

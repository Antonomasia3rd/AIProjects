#pragma once
// Read the public activation record from this exact independent package only.
// No INI contents, accounts, deployment, pinning or process creation occurs here.
#include "tile_registration_binding.h"
#include "named_objects.inc"
#include <appmodel.h>
#include <aclapi.h>
#include <sddl.h>
#include <functional>
#pragma comment(lib,"advapi32.lib")

namespace aip { namespace tile_registration { namespace binding_native {
struct Context {std::wstring userSid,packageName,publisher,applicationId,packageRoot,executable;};
struct File {std::wstring finalPath,ownerSid,text;bool ordinary=false,directory=false;};
struct Hooks {
    std::function<bool(Context&,std::wstring&)> context;
    std::function<bool(const std::wstring&,bool,bool,File&,std::wstring&)> file;
};
inline bool Equal(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
inline std::wstring Parent(const std::wstring& path) {const auto split=path.find_last_of(L"\\/");return split==std::wstring::npos?std::wstring():path.substr(0,split);}
inline std::wstring Leaf(const std::wstring& path) {const auto split=path.find_last_of(L"\\/");return split==std::wstring::npos?path:path.substr(split+1);}
inline bool Normalize(const std::wstring& path,std::wstring& output) {
    auto value=path;if(value.compare(0,4,L"\\\\?\\")==0)value.erase(0,4);
    return LocalAbsolutePath(value)&&aip::named_objects::NormalizeFileIdentity(value,output);
}
inline bool PlainLocalPath(const std::wstring& path) {
    if(!LocalAbsolutePath(path))return false;
    const wchar_t drive[]={path[0],L':',L'\\',0};const auto kind=GetDriveTypeW(drive);
    if(kind!=DRIVE_FIXED&&kind!=DRIVE_RAMDISK)return false;
    auto value=path;std::replace(value.begin(),value.end(),L'/',L'\\');
    for(std::size_t end=value.find(L'\\',3);;end=value.find(L'\\',end+1)){
        const auto part=end==std::wstring::npos?value:value.substr(0,end);
        const auto attributes=GetFileAttributesW(part.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        if(end==std::wstring::npos)break;
    }
    return true;
}
inline bool ReadFile(const std::wstring& path,bool contents,bool directory,File& output,std::wstring& error) {
    const auto fail=[&]{error=L"The Registration activation record or its target is not a readable ordinary local file.";return false;};
    if(!PlainLocalPath(path))return fail();
    const auto access=READ_CONTROL|FILE_READ_ATTRIBUTES|(contents?GENERIC_READ:0);
    HANDLE handle=CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
    if(handle==INVALID_HANDLE_VALUE)return fail();
    struct Close {HANDLE value;~Close(){CloseHandle(value);}}close{handle};
    BY_HANDLE_FILE_INFORMATION information{};
    if(GetFileType(handle)!=FILE_TYPE_DISK||!GetFileInformationByHandle(handle,&information)||
        (information.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||((information.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory)return fail();
    DWORD length=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!length||length>32768)return fail();
    std::vector<wchar_t> finalPath(static_cast<std::size_t>(length)+1);
    const auto copied=GetFinalPathNameByHandleW(handle,finalPath.data(),static_cast<DWORD>(finalPath.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    File candidate;if(!copied||copied>=finalPath.size()||!Normalize(std::wstring(finalPath.data(),copied),candidate.finalPath))return fail();
    PSID owner=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&descriptor)!=ERROR_SUCCESS)return fail();
    LPWSTR sid=nullptr;const bool ownerOk=owner&&ConvertSidToStringSidW(owner,&sid)!=FALSE;
    // Match the shared named-object identity key, which prefixes the SID.
    if(ownerOk)candidate.ownerSid=L"SID:"+std::wstring(sid);
    if(sid)LocalFree(sid);if(descriptor)LocalFree(descriptor);if(!ownerOk)return fail();
    if(contents){
        LARGE_INTEGER size{};if(directory||!GetFileSizeEx(handle,&size)||size.QuadPart<=0||size.QuadPart>static_cast<LONGLONG>(MaximumActivationBytes))return fail();
        std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0');std::size_t offset=0;
        while(offset<bytes.size()){DWORD read=0;if(!::ReadFile(handle,&bytes[offset],static_cast<DWORD>(bytes.size()-offset),&read,nullptr)||!read)return fail();offset+=read;}
        if(bytes.size()>=3&&static_cast<unsigned char>(bytes[0])==0xef&&static_cast<unsigned char>(bytes[1])==0xbb&&static_cast<unsigned char>(bytes[2])==0xbf)bytes.erase(0,3);
        const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
        if(count<=0||static_cast<std::size_t>(count)>MaximumActivationCharacters)return fail();
        candidate.text.resize(static_cast<std::size_t>(count));
        if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),candidate.text.data(),count)!=count)return fail();
    }
    candidate.ordinary=true;candidate.directory=directory;output=std::move(candidate);error.clear();return true;
}
inline bool ReadContext(Context& output,std::wstring& error) {
    const auto fail=[&]{error=L"Registration tile activation needs its own packaged identity on Windows 10 1703 or later.";return false;};
    Context value;if(!aip::named_objects::CurrentUserIdentity(value.userSid))return fail();
    UINT32 length=0;if(GetCurrentPackageFullName(&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||!length||length>512)return fail();
    std::vector<wchar_t> fullName(length);if(GetCurrentPackageFullName(&length,fullName.data())!=ERROR_SUCCESS)return fail();
    // FULL explicitly obtains the registered publisher; BASIC identity data is
    // not sufficient to attest the full publisher distinguished name.
    UINT32 bytes=0;const auto initial=PackageIdFromFullName(fullName.data(),PACKAGE_INFORMATION_FULL,&bytes,nullptr);
    if(initial!=ERROR_INSUFFICIENT_BUFFER||!bytes||bytes>65536)return fail();
    std::vector<BYTE> package(bytes);if(PackageIdFromFullName(fullName.data(),PACKAGE_INFORMATION_FULL,&bytes,package.data())!=ERROR_SUCCESS)return fail();
    const auto* id=reinterpret_cast<const PACKAGE_ID*>(package.data());if(!id->name||!id->publisher)return fail();
    value.packageName=id->name;value.publisher=id->publisher;
    length=0;if(GetCurrentPackagePath(&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||!length||length>32768)return fail();
    std::vector<wchar_t> root(length);if(GetCurrentPackagePath(&length,root.data())!=ERROR_SUCCESS)return fail();value.packageRoot=root.data();
    length=0;if(GetCurrentApplicationUserModelId(&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||!length||length>256)return fail();
    std::vector<wchar_t> aumid(length);if(GetCurrentApplicationUserModelId(&length,aumid.data())!=ERROR_SUCCESS)return fail();
    UINT32 familyLength=0;if(GetCurrentPackageFamilyName(&familyLength,nullptr)!=ERROR_INSUFFICIENT_BUFFER||!familyLength||familyLength>256)return fail();
    std::vector<wchar_t> family(familyLength);if(GetCurrentPackageFamilyName(&familyLength,family.data())!=ERROR_SUCCESS)return fail();
    const std::wstring app=aumid.data();if(!Equal(app,std::wstring(family.data())+L"!Tile"))return fail();value.applicationId=L"Tile";
    std::vector<wchar_t> executable(32768);const auto copied=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!copied||copied>=executable.size())return fail();value.executable.assign(executable.data(),copied);
    output=std::move(value);error.clear();return true;
}
inline bool LoadChecked(ActivationBinding& output,std::wstring& error,const Hooks* injected) {
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!injected){error=L"Native Registration binding reads are disabled in inert validation.";return false;}
#endif
    Context context;
    const auto current=injected?injected->context:std::function<bool(Context&,std::wstring&)>(ReadContext);
    const auto read=injected?injected->file:std::function<bool(const std::wstring&,bool,bool,File&,std::wstring&)>(ReadFile);
    if(!current||!read||!current(context,error))return false;
    const auto fail=[&](const wchar_t* reason){error=reason;return false;};
    std::wstring root,executable,parent;
    if(context.userSid.empty()||context.applicationId!=L"Tile"||!Normalize(context.packageRoot,root)||!Normalize(context.executable,executable)||
        !Normalize(Parent(context.executable),parent)||root!=parent||!Equal(Leaf(context.executable),LauncherFileName))
        return fail(L"This launcher is not the expected application in its own Registration package.");
    File rootFile,launcherFile,bindingFile,ownerFile;
    if(!read(root,false,true,rootFile,error)||!read(executable,false,false,launcherFile,error)||
        !rootFile.ordinary||!rootFile.directory||rootFile.finalPath!=root||!launcherFile.ordinary||launcherFile.directory||launcherFile.finalPath!=executable)
        return fail(L"The Registration package location is linked or does not match its executable.");
    const auto bindingPath=root+L"\\"+ActivationBindingFileName,ownerPath=root+L"\\registration.owner";
    if(!read(bindingPath,true,false,bindingFile,error)||!read(ownerPath,true,false,ownerFile,error)||
        !bindingFile.ordinary||bindingFile.directory||!ownerFile.ordinary||ownerFile.directory||
        !Equal(bindingFile.ownerSid,context.userSid)||!Equal(ownerFile.ownerSid,context.userSid))
        return fail(L"The Registration activation files are missing or belong to another Windows user.");
    std::wstring expectedBinding,expectedOwner;
    if(!Normalize(bindingPath,expectedBinding)||!Normalize(ownerPath,expectedOwner)||bindingFile.finalPath!=expectedBinding||ownerFile.finalPath!=expectedOwner)
        return fail(L"The Registration activation files resolve outside their package.");
    ActivationBinding binding;if(!ReadActivationBinding(bindingFile.text,binding,error))return false;
    if(binding.identityName!=context.packageName||!Equal(binding.publisher,context.publisher))
        return fail(L"The Registration activation identity does not match this package.");
    std::wstring user,profile,profileScope,tile;
    if(!aip::named_objects::StableIdentityHash(context.userSid,user)||!aip::named_objects::NormalizeFileIdentity(binding.profile,profile)||
        !aip::named_objects::StableIdentityHash(profile,profileScope)||!aip::named_objects::StableIdentityHash(binding.tileId,tile)||
        !Equal(user,binding.userScope)||!Equal(profileScope,binding.profileScope)||!Equal(tile,binding.tileScope))
        return fail(L"The Registration activation user, profile or tile scope is invalid.");
    const auto suffix=L"\\"+binding.userScope+L"\\"+binding.profileScope+L"\\"+binding.tileScope;
    if(root.size()<suffix.size()||!Equal(root.substr(root.size()-suffix.size()),suffix)||ownerFile.text!=ActivationOwnerText(binding,profile))
        return fail(L"The Registration activation owner does not match its package folder and profile.");
    File profileFile,hostFile;std::wstring host;
    if(!read(binding.profile,false,false,profileFile,error)||!profileFile.ordinary||profileFile.directory||!Equal(profileFile.ownerSid,context.userSid)||
        !Normalize(binding.hostPath,host)||!read(binding.hostPath,false,false,hostFile,error)||!hostFile.ordinary||hostFile.directory||hostFile.finalPath!=host)
        return fail(L"The Registration profile must be a current-user ordinary file and its host must be available locally.");
    output=std::move(binding);error.clear();return true;
}
inline bool Load(ActivationBinding& output,std::wstring& error,const Hooks* injected=nullptr) {
    error.clear();try{
        if(LoadChecked(output,error,injected))return true;
        if(error.empty())error=L"Registration activation context or file metadata is unavailable.";
    }catch(...){error=L"Registration activation metadata could not be read safely.";}
    return false;
}
} } }

// Transient static-tile launcher. No providers, tray, Startup, registration,
// accounts, or resident loop. Prefer the profile's existing resident IPC.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <appmodel.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "../dependencies/baseline_app.h"
#include "../dependencies/tile_registration.h"

static bool ValidTileId(const std::wstring& id) {
    return !id.empty() && id.size() <= 54 && std::all_of(id.begin(),id.end(),[](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_';
    });
}
static bool AbsolutePath(const std::wstring& path) {
    return path.size() > 3 && ((path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) || path.compare(0,2,L"\\\\") == 0) &&
        aip::tile_registration::Text(path,32767) && path.find(L'\"') == std::wstring::npos;
}
static bool ExistingResident(const std::wstring& profile,const std::wstring& product,const std::wstring& id,bool& found) {
    const auto identity = aip::BuildPathScopedInstanceIdentity(product,product+L".RestoreRunningInstance",L"DesktopStubTrayWnd",product,profile);
    const auto window = FindWindowW(L"DesktopStubTrayWnd",identity.windowTitle.c_str()); found = window != nullptr;
    if (!window) return false;
    const auto payload = L"0:" + id;
    COPYDATASTRUCT data{0x41495054,static_cast<DWORD>((payload.size()+1)*sizeof(wchar_t)),const_cast<wchar_t*>(payload.c_str())};
    DWORD_PTR accepted = 0;
    return SendMessageTimeoutW(window,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&accepted) && accepted == TRUE;
}
static bool ActivateCanonical(const std::wstring& aumid,const std::wstring& arguments) {
    if (aumid.empty()) return false;
    const auto initialized = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return false;
    IApplicationActivationManager* manager = nullptr;
    auto result = CoCreateInstance(CLSID_ApplicationActivationManager,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager));
    if (SUCCEEDED(result)) {
        DWORD process = 0;
        result = manager->ActivateApplication(aumid.c_str(),arguments.c_str(),AO_NONE,&process);
        manager->Release();
    }
    CoUninitialize(); return SUCCEEDED(result);
}
static bool StartUnpackagedCanonicalHost(const std::wstring& host,const std::wstring& arguments) {
    const auto attributes = GetFileAttributesW(host.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr,1,0,&size);
    if (!size) return false;
    std::vector<BYTE> buffer(size);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    if (!InitializeProcThreadAttributeList(list,1,0,&size)) return false;
    // OVERRIDE forces the immediate child inside the package environment. Do
    // not use it here; verify the suspended child's actual identity as well.
    DWORD policy = PROCESS_CREATION_DESKTOP_APP_BREAKAWAY_ENABLE_PROCESS_TREE;
    bool ok = UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY,&policy,sizeof(policy),nullptr,nullptr) != FALSE;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    auto command = aip::tile_registration::Quote(host) + L" " + arguments;
    if (ok) ok = CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,
        nullptr,nullptr,&startup.StartupInfo,&process) != FALSE;
    DeleteProcThreadAttributeList(list);
    if (!ok) return false;
    UINT32 length = 0;
    const auto identity = GetPackageFullName(process.hProcess,&length,nullptr);
    if (identity != APPMODEL_ERROR_NO_PACKAGE || ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.hProcess,2); ok = false;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return ok;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
    int count = 0; auto** argv = CommandLineToArgvW(GetCommandLineW(),&count);
    if (!argv) return 2;
    std::map<std::wstring,std::wstring> values; bool validateOnly = false, valid = true;
    for (int index = 1; index < count; ++index) {
        const std::wstring key = argv[index];
        if (key == L"--validate-only") { if (validateOnly) valid = false; validateOnly = true; continue; }
        if ((key != L"--host" && key != L"--profile" && key != L"--tile-id" && key != L"--product" && key != L"--activate-aumid") ||
            index+1 >= count || values.count(key)) { valid = false; break; }
        values[key] = argv[++index];
    }
    LocalFree(argv);
    const auto host = values[L"--host"], profile = values[L"--profile"], tile = values[L"--tile-id"], product = values[L"--product"], aumid = values[L"--activate-aumid"];
    valid = valid && AbsolutePath(host) && AbsolutePath(profile) && ValidTileId(tile) && aip::tile_registration::Text(product,128) &&
        (aumid.empty() || aip::tile_registration::Aumid(aumid));
    if (!valid) return 2;
    if (validateOnly) return 0; // Deterministic inert binary validation only.
    bool residentFound = false;
    if (ExistingResident(profile,product,tile,residentFound)) return 0;
    if (residentFound) return 2; // Do not start a duplicate while its UI is busy.
    const auto arguments = L"--ini " + aip::tile_registration::Quote(profile) + L" --tile-setup " + tile;
    if (ActivateCanonical(aumid,arguments) || StartUnpackagedCanonicalHost(host,arguments)) return 0;
    MessageBoxW(nullptr,L"DesktopStub could not open this tile. Open the main DesktopStub app and retry. No tile configuration was changed.",
        L"DesktopStub tile",MB_OK|MB_ICONWARNING); return 2;
}

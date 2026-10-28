// Compile the actual host without calling its application entry point. All
// Discord/account transports and dialogs are injected into private fixtures.
#define NOMINMAX
#include <windows.h>
#include <string>
static int contentDialogResponse=IDNO;
static int DiscordFixtureMessageBox(HWND,LPCWSTR,LPCWSTR,UINT flags){return flags&MB_YESNO?contentDialogResponse:IDOK;}
#define DESKTOPSTUB_CONTENT_MESSAGEBOX DiscordFixtureMessageBox
#define DESKTOPSTUB_INERT_HARDWARE
#define wWinMain DesktopStubDiscordFixtureUnusedEntry
#include "../DesktopStub.cpp"
#undef wWinMain
#include <iostream>
static unsigned checks=0,failures=0;
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
static bool WriteContentFixture(const std::wstring& text){if(!WriteUtf8BomTextFile(g_iniPath,text))return false;std::wstring loaded;return ReadIniFileForMutation(loaded)&&loaded==text;}
#include "DiscordReaderHostTests.inc"
int wmain(){
    struct Deadline{std::atomic<bool> done{false};std::thread worker;Deadline():worker([this]{const auto limit=steady_clock::now()+std::chrono::seconds(45);while(!done&&steady_clock::now()<limit)std::this_thread::sleep_for(milliseconds(20));if(!done)std::_Exit(124);}){}~Deadline(){done=true;worker.join();}}deadline;
    wchar_t temporary[MAX_PATH]{};if(!GetTempPathW(_countof(temporary),temporary))return 2;const auto directory=std::wstring(temporary)+L"DesktopStubDiscordReader-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());if(!CreateDirectoryW(directory.c_str(),nullptr))return 2;
    g_iniPath=directory+L"\\fixture.ini";g_logPath=directory+L"\\test.log";g_exePath=aip::GetCurrentExecutablePath();g_logging=false;g_notificationsEnabled=false;g_console=false;
    // A real solid area keeps the center opaque when GDI+ interpolates the
    // prepared avatar. A scaled 1x1 image samples its transparent outer edge.
    constexpr DWORD avatarSide=8,avatarStride=(avatarSide*3+3)&~3u,avatarPixelBytes=avatarStride*avatarSide;
    const auto bitmapPath=directory+L"\\avatar.bmp";BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+avatarPixelBytes;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=avatarSide;info.biHeight=avatarSide;info.biPlanes=1;info.biBitCount=24;info.biSizeImage=avatarPixelBytes;
    std::vector<BYTE> bytes(file.bfSize,64);memcpy(bytes.data(),&file,sizeof(file));memcpy(bytes.data()+sizeof(file),&info,sizeof(info));
    Check(WriteWholeFileBytes(bitmapPath,bytes)&&WriteContentFixture(L"[Tiles]\r\nSchemaVersion=1\r\nInstances=\r\n"),"private fixture files are created outside any user profile");LoadUiStrings();TestDiscordReaderHost(bitmapPath);
    StopContentProviders();PublishContentSnapshot({});DeleteFileW(g_iniPath.c_str());DeleteFileW(bitmapPath.c_str());DeleteFileW(g_logPath.c_str());RemoveDirectoryW(directory.c_str());std::cout<<checks<<" Discord reader host checks; "<<failures<<" failures\n";return failures?1:0;
}

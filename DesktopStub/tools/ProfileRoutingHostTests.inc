// No process/window/package interaction: exercise the exact early-route branch.
static void TestProfileDefaultExitRoutingHost()
{
    const auto savedContext=g_testProfileContext;auto* savedOperations=g_testProfileOperations;
    auto savedStore=std::move(g_profileStore);bool foreign=false;unsigned writes=0;
    aip::profiles::native::Operations operations;
    operations.exists=[](const auto&,bool& exists,auto&){exists=false;return true;};
    operations.read=[](const auto&,std::wstring& text,bool& missing,auto&){text=L"[Settings]\n";missing=false;return true;};
    operations.owner=[&](const auto&,const auto&,std::wstring& error){if(foreign){error=L"Injected foreign ownership";return false;}return true;};
    operations.directory=[&](const auto&,auto&){++writes;return false;};
    operations.write=[&](const auto&,const auto&,bool,auto&){++writes;return false;};
    g_testProfileContext=[](aip::profiles::native::Context& context,std::wstring&){
        context={};context.ownerSid=L"SID:S-1-5-21-100-200-300-1001";context.userScope=L"0123456789abcdef";
        context.installScope=L"fedcba9876543210";context.sourceRoot=L"C:\\ProfileRoutingFixture";
        context.mainIniPath=context.sourceRoot+L"\\DesktopStub.ini";
        context.baseRoot=L"C:\\ProfileRoutingFixtureUser\\.DesktopStub\\Profiles\\"+context.installScope;return true;
    };
    g_testProfileOperations=&operations;int result=9;std::wstring error;
    const std::wstring ini=L"C:\\ProfileRoutingFixture\\DesktopStub.ini";
    const std::vector<std::wstring> arguments{L"C:\\ProfileRoutingFixture\\DesktopStub.exe",L"--exit"};
    Check(!RouteDefaultProfileForCurrentUser(ini,arguments,result,error)&&result==0&&writes==0,
        "empty coordination registry and owned Main --exit continues into the normal singleton IPC path");
    g_profileStore.reset();foreign=true;error.clear();result=9;
    Check(RouteDefaultProfileForCurrentUser(ini,arguments,result,error)&&result==0&&writes==0,
        "empty coordination registry and foreign Main --exit does not create a fresh profile or start a resident");
    g_profileStore=std::move(savedStore);g_testProfileContext=savedContext;g_testProfileOperations=savedOperations;
}

#include "..\dependencies\desktop_app_baseline.h"

#include <iostream>
#include <thread>

static int g_failures = 0;

static void Check(bool condition, const char* name)
{
    if (condition)
    {
        std::cout << "ok - " << name << "\n";
        return;
    }
    std::cerr << "not ok - " << name << "\n";
    ++g_failures;
}

static void TestIniBehavior()
{
    std::wstring parsedValue;
    auto document = aip::ParseIniDocument(L"[]\r\n\"\" = \"empty-name-value\"\r\n");
    Check(
        aip::ReadIniValueFromDoc(document, L"", L"", parsedValue) &&
            parsedValue == L"empty-name-value",
        "INI parser preserves empty section and key names");

    std::wstring text;
    Check(
        aip::WriteIniValueToText(text, L"", L"", L"empty-name-value") &&
            text == L"[]\r\n\"\" = \"empty-name-value\"\r\n",
        "INI writer preserves DesktopStub empty-name behavior");

    text = L"; keep\r\n[General]\r\n\"First\" = \"1\"\r\n\"Second\" = \"2\"\r\n";
    Check(
        aip::WriteIniValueToText(text, L"General", L"First", L"updated") &&
            text.find(L"; keep\r\n[General]\r\n\"First\" = \"updated\"\r\n\"Second\" = \"2\"") == 0,
        "INI writer preserves comments and key order");

    std::wstring desktopStubDialectText =
        L"[App]\r\n"
        L"\"Path\" = \"C:\\Users\\Amiya\\Desktop\\DiscordRPC.log\"\r\n"
        L"\"EscapedPath\" = \"C:\\\\Users\\\\Amiya\"\r\n"
        L"\"Template\" = \"Line1\\nLine2\"\r\n"
        L"\"TrailingSpaces\" = \"Value   \"\r\n"
        L"\"UnknownEscape\" = \"Keep\\q\"\r\n";
    auto desktopStubDialect = aip::ParseIniDocument(desktopStubDialectText);
    std::wstring dialectValue;
    Check(
        aip::ReadIniValueFromDoc(desktopStubDialect, L"App", L"Path", dialectValue) &&
            dialectValue == L"C:\\Users\\Amiya\\Desktop\\DiscordRPC.log",
        "INI parser preserves raw Windows path backslashes");
    Check(
        aip::ReadIniValueFromDoc(desktopStubDialect, L"App", L"EscapedPath", dialectValue) &&
            dialectValue == L"C:\\Users\\Amiya",
        "INI parser decodes intentional escaped backslashes");
    Check(
        aip::ReadIniValueFromDoc(desktopStubDialect, L"App", L"Template", dialectValue) &&
            dialectValue == L"Line1\\nLine2",
        "INI parser keeps app-level template escapes raw");
    Check(
        aip::ReadIniValueFromDoc(desktopStubDialect, L"App", L"TrailingSpaces", dialectValue) &&
            dialectValue == L"Value   ",
        "INI parser preserves whitespace inside quoted values");
    Check(
        aip::ReadIniValueFromDoc(desktopStubDialect, L"App", L"UnknownEscape", dialectValue) &&
            dialectValue == L"Keep\\q",
        "INI parser preserves unknown backslash escapes");

    text.clear();
    Check(
        aip::WriteIniValueToText(text, L"App", L"Path", L"C:\\Users\\Amiya\\Desktop\\file.txt") &&
            text == L"[App]\r\n\"Path\" = \"C:\\\\Users\\\\Amiya\\\\Desktop\\\\file.txt\"\r\n",
        "INI writer uses DesktopStub quoted assignment and escaped path style");

    std::vector<BYTE> utf8Bom = { 0xef, 0xbb, 0xbf, 'A', 0xc3, 0xa9 };
    std::wstring decoded;
    Check(aip::DecodeTextBytes(utf8Bom, decoded) && decoded == L"A\u00e9", "INI decoder accepts UTF-8 BOM");

    std::vector<BYTE> utf16Le = { 0xff, 0xfe, 'A', 0x00, 0xe9, 0x00 };
    Check(aip::DecodeTextBytes(utf16Le, decoded) && decoded == L"A\u00e9", "INI decoder accepts UTF-16 LE");

    wchar_t tempDirectory[MAX_PATH] = {};
    DWORD length = GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory);
    std::wstring path = length != 0 && length < ARRAYSIZE(tempDirectory)
        ? std::wstring(tempDirectory) + L"AIP-SharedBaseline-" + std::to_wstring(GetCurrentProcessId()) + L".ini"
        : L"AIP-SharedBaseline.ini";
    DeleteFileW(path.c_str());
    aip::IniConfigStore store(path, L"; Shared baseline\r\n");
    bool wrote = store.WriteRaw(L"General", L"Name", L"Value");
    std::wstring persisted = store.ReadRaw(L"General", L"Name", L"");
    Check(wrote && persisted == L"Value", "INI file write/read round trip");

    WIN32_FILE_ATTRIBUTE_DATA originalAttributes = {};
    bool capturedTime = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &originalAttributes) != FALSE;
    std::wstring externalText =
        L"; Shared baseline\r\n"
        L"[General]\r\n"
        L"\"Name\" = \"Value\"\r\n"
        L"\"ExternalMarker\" = \"keep\"\r\n";
    bool externalWrite = aip::WriteTextFileUtf8Bom(path, externalText);
    HANDLE attributesHandle = CreateFileW(
        path.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    bool restoredTime = capturedTime &&
        attributesHandle != INVALID_HANDLE_VALUE &&
        SetFileTime(attributesHandle, nullptr, nullptr, &originalAttributes.ftLastWriteTime) != FALSE;
    if (attributesHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(attributesHandle);
    }

    bool mutationWrite = store.MutateFresh([](std::wstring& mutationText) {
        return aip::WriteIniValueToText(mutationText, L"General", L"ResidentMutation", L"saved");
    });
    std::wstring mutationResult;
    bool reread = store.ReadFreshText(mutationResult);
    Check(
        externalWrite &&
            restoredTime &&
            mutationWrite &&
            reread &&
            mutationResult.find(L"\"ExternalMarker\" = \"keep\"") != std::wstring::npos &&
            mutationResult.find(L"\"ResidentMutation\" = \"saved\"") != std::wstring::npos,
        "fresh INI mutation preserves external edits with unchanged timestamps");

    bool firstMutation = false;
    bool secondMutation = false;
    std::thread first([&]() {
        firstMutation = store.MutateFresh([](std::wstring& mutationText) {
            Sleep(50);
            return aip::WriteIniValueToText(mutationText, L"Concurrent", L"First", L"1");
        });
    });
    std::thread second([&]() {
        secondMutation = store.MutateFresh([](std::wstring& mutationText) {
            Sleep(50);
            return aip::WriteIniValueToText(mutationText, L"Concurrent", L"Second", L"2");
        });
    });
    first.join();
    second.join();

    std::wstring concurrentResult;
    bool concurrentReread = store.ReadFreshText(concurrentResult);
    Check(
        firstMutation &&
            secondMutation &&
            concurrentReread &&
            concurrentResult.find(L"\"First\" = \"1\"") != std::wstring::npos &&
            concurrentResult.find(L"\"Second\" = \"2\"") != std::wstring::npos,
        "fresh INI mutation serializes concurrent read-modify-write transactions");
    DeleteFileW(path.c_str());
}

static void TestCommandLineBehavior()
{
    Check(
        aip::IsOptionOrInlineValue(L"--INI=Config.ini", L"--ini") &&
            !aip::IsOptionOrInlineValue(L"--initial", L"--ini"),
        "command-line option matching is strict and case-insensitive");

    int index = 1;
    std::wstring value;
    std::wstring error;
    wchar_t arg0[] = L"app.exe";
    wchar_t arg1[] = L"--name";
    wchar_t arg2[] = L" value ";
    wchar_t* argv[] = { arg0, arg1, arg2 };
    Check(
        aip::TakeCommandLineValue(3, argv, index, L"--name", value, error) &&
            index == 2 &&
            value == L" value ",
        "command-line separate value preserves whitespace");

    index = 1;
    value.clear();
    error.clear();
    Check(
        aip::TakeCommandLineValue(2, argv, index, L"--name=inline value", value, error) &&
            value == L"inline value",
        "command-line inline value parsing");

    index = 1;
    value.clear();
    error.clear();
    Check(
        !aip::TakeCommandLineValue(2, argv, index, L"--name", value, error) &&
            error == L"Missing value after --name.",
        "command-line missing value error");

    aip::IniSetting setting;
    aip::IniSetSpecError errorKind = aip::IniSetSpecError::None;
    Check(
        aip::ParseIniSetSpec(L"section.with.dot.key=value  ", setting, error, &errorKind) &&
            setting.section == L"section.with.dot" &&
            setting.key == L"key" &&
            setting.value == L"value  ",
        "command-line INI override uses last dot and preserves value whitespace");
    Check(
        !aip::ParseIniSetSpec(L"section]\r\n[injected.key=value", setting, error, &errorKind) &&
            errorKind == aip::IniSetSpecError::UnsafeCharacters,
        "command-line INI override rejects section injection");
    Check(
        !aip::ParseIniSetSpec(L"section.key\r\ninjected=value", setting, error, &errorKind) &&
            errorKind == aip::IniSetSpecError::UnsafeCharacters,
        "command-line INI override rejects key injection");
    Check(
        !aip::ParseIniSetSpec(L"section.key=value\r\n[injected]", setting, error, &errorKind) &&
            errorKind == aip::IniSetSpecError::UnsafeCharacters,
        "command-line INI override rejects value injection");

    bool boolValue = false;
    Check(
        aip::ParseBoolValue(L" Enabled ", boolValue) && boolValue &&
            aip::ParseBoolValue(L"off", boolValue) && !boolValue,
        "command-line boolean aliases");
}

static void TestJsonBehavior()
{
    const std::string json = "{\"nested\":{\"target\":\"wrong\"},\"target\":\"right\",\"unicode\":\"A\\u00e9\"}";
    Check(aip::ExtractJsonStringValue(json, "target") == L"right", "JSON lookup stays in the current object");
    Check(aip::ExtractJsonStringValue(json, "unicode") == L"A\u00e9", "JSON string decoding handles Unicode escapes");

    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    Check(
        !aip::FindJsonFieldValue("{\"broken\":{\"x\":1]}", "broken", keyPos, valueStart, valueEnd),
        "JSON lookup rejects mismatched containers");
    Check(
        !aip::FindJsonFieldValue("{\"nested\":{\"target\":\"wrong\"}}", "target", keyPos, valueStart, valueEnd),
        "JSON lookup does not return nested fields");
}

static void TestTrayBehavior()
{
    HMENU menu = aip::CreateTrayPopupMenu();
    Check(menu != nullptr, "tray root menu creation");
    if (menu == nullptr)
        return;

    bool itemAdded = aip::AppendTrayMenuItem(menu, 100, L"Checked disabled", true, false);
    UINT state = GetMenuState(menu, 0, MF_BYPOSITION);
    Check(
        itemAdded &&
            state != static_cast<UINT>(-1) &&
            (state & MF_CHECKED) != 0 &&
            (state & (MF_DISABLED | MF_GRAYED)) != 0,
        "tray item checked and disabled state");

    HMENU nested = aip::BeginTrayNestedMenu(menu);
    bool nestedCreated = nested != menu && nested != nullptr;
    if (nestedCreated)
        aip::AppendTrayMenuItem(nested, 101, L"Nested item");
    bool nestedAdded = nestedCreated && aip::EndTrayNestedMenu(menu, nested, L" General: \t");
    wchar_t title[64] = {};
    int titleLength = GetMenuStringW(menu, 1, title, ARRAYSIZE(title), MF_BYPOSITION);
    Check(
        nestedAdded &&
            GetSubMenu(menu, 1) == nested &&
            titleLength > 0 &&
            std::wstring(title) == L"General",
        "tray nested menu title normalization and ownership");

    aip::AppendMenuSeparator(menu);
    UINT separatorState = GetMenuState(menu, GetMenuItemCount(menu) - 1, MF_BYPOSITION);
    Check((separatorState & MF_SEPARATOR) != 0, "tray separator creation");

    DestroyMenu(menu);
}


static void TestAppPathBehavior()
{
    aip::SidecarPaths paths = aip::BuildSidecarPathsFromExecutable(
        L"C:\\Tools\\DiscordRPC.exe",
        L"DiscordRPC");
    Check(
        paths.exeDir == L"C:\\Tools" &&
            paths.exeBaseName == L"DiscordRPC" &&
            paths.configPath == L"C:\\Tools\\DiscordRPC.ini" &&
            paths.defaultLogPath == L"C:\\Tools\\DiscordRPC.log",
        "sidecar paths derive default INI and log from executable name");

    aip::SidecarPaths overridden = aip::BuildSidecarPathsFromExecutable(
        L"C:\\Tools\\DiscordRPC.exe",
        L"DiscordRPC",
        L"C:\\Config\\custom.ini");
    Check(
        overridden.configPath == L"C:\\Config\\custom.ini" &&
            overridden.defaultLogPath == L"C:\\Config\\custom.log",
        "sidecar paths derive log path from configured INI override");
}

static void TestLoggingBehavior()
{
    aip::Utf8Logger logger;
    aip::Utf8LoggerOptions options;
    options.fileEnabled = false;
    options.maxRecentLines = 2;
    logger.Configure(options);
    logger.WriteRawLine(L"first");
    logger.WriteRawLine(L"second");
    logger.WriteRawLine(L"third");
    std::vector<std::wstring> recent = logger.RecentLines();
    Check(
        recent.size() == 2 && recent[0] == L"second" && recent[1] == L"third",
        "shared UTF-8 logger keeps bounded recent lines");

    options.enabled = false;
    logger.Configure(options);
    logger.WriteRawLine(L"ignored");
    recent = logger.RecentLines();
    Check(
        recent.size() == 2 && recent[0] == L"second" && recent[1] == L"third",
        "shared UTF-8 logger honors disabled logging");
}

static void TestApplicationBaseline()
{
    aip::ResidentShutdownState shutdown;
    Check(!shutdown.IsRequested() && !shutdown.IsWorkComplete(), "resident shutdown state starts idle");
    Check(shutdown.Request() && !shutdown.Request() && shutdown.IsRequested(), "resident shutdown request is idempotent");
    Check(shutdown.Cancel() && !shutdown.IsRequested(), "resident shutdown request can be cancelled");
    shutdown.MarkWorkComplete();
    Check(shutdown.IsWorkComplete(), "resident shutdown state records worker completion");

    aip::InstanceIdentity identity = aip::BuildInstanceIdentity(
        L"DesktopStub",
        L"DesktopStub.RestoreRunningInstance",
        L"DesktopStubTrayWnd",
        L"DesktopStub",
        L"0123456789abcdef");
    Check(
        identity.mutexName == L"Local\\DesktopStub.0123456789abcdef" &&
            identity.messageName == L"DesktopStub.RestoreRunningInstance.0123456789abcdef" &&
            identity.windowTitle == L"DesktopStubTrayWnd.DesktopStub.0123456789abcdef",
        "single-instance identity preserves baseline naming");

    aip::InstanceIdentity discordIdentity = aip::BuildInstanceIdentity(
        L"DiscordRPC",
        L"DiscordRPC.Stop",
        L"DiscordRPCTrayWnd",
        L"",
        L"fedcba9876543210");
    Check(
        discordIdentity.mutexName == L"Local\\DiscordRPC.fedcba9876543210" &&
            discordIdentity.messageName == L"DiscordRPC.Stop.fedcba9876543210" &&
            discordIdentity.windowTitle == L"DiscordRPCTrayWnd.fedcba9876543210",
        "single-instance identity supports product-specific window naming");

    std::wstring formatted = aip::FormatTextTemplate(
        L"Run {exe}\\nConfig: {ini}",
        {
            { L"{exe}", L"DesktopStub.exe" },
            { L"{ini}", L"DesktopStub.ini" }
        });
    Check(
        formatted == L"Run DesktopStub.exe\nConfig: DesktopStub.ini",
        "help template decoding and token replacement");

    HMENU flatMenu = CreatePopupMenu();
    aip::TraySectionLayout flat(flatMenu, false);
    HMENU flatSection = flat.Begin(L" General: ");
    bool flatEnded = flat.End(flatSection, L" General: ");
    Check(
        flatSection == flatMenu &&
            flatEnded &&
            GetMenuItemCount(flatMenu) == 2 &&
            (GetMenuState(flatMenu, 0, MF_BYPOSITION) & MF_DISABLED) != 0 &&
            (GetMenuState(flatMenu, 1, MF_BYPOSITION) & MF_SEPARATOR) != 0,
        "flat tray section layout");
    DestroyMenu(flatMenu);

    HMENU dropdownMenu = CreatePopupMenu();
    aip::TraySectionLayout dropdown(dropdownMenu, true);
    HMENU dropdownSection = dropdown.Begin(L" General: ");
    bool dropdownEnded = dropdown.End(dropdownSection, L" General: ");
    wchar_t title[32] = {};
    int titleLength = GetMenuStringW(dropdownMenu, 0, title, ARRAYSIZE(title), MF_BYPOSITION);
    Check(
        dropdownSection != dropdownMenu &&
            dropdownEnded &&
            GetSubMenu(dropdownMenu, 0) == dropdownSection &&
            titleLength > 0 &&
            std::wstring(title) == L"General",
        "dropdown tray section layout");
    DestroyMenu(dropdownMenu);
}

int wmain()
{
    TestIniBehavior();
    TestCommandLineBehavior();
    TestJsonBehavior();
    TestTrayBehavior();
    TestAppPathBehavior();
    TestLoggingBehavior();
    TestApplicationBaseline();

    if (g_failures != 0)
    {
        std::cerr << "Shared baseline tests failed: " << g_failures << "\n";
        return 1;
    }
    std::cout << "Shared baseline tests passed.\n";
    return 0;
}

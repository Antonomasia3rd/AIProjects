#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "..\dependencies\baseline_app.h"
#include "..\dependencies\core.inc"
#include "..\dependencies\config_ini.inc"
#include "..\dependencies\command_line.inc"
#include "..\dependencies\tray.inc"

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

static void TestApplicationBaseline()
{
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
    TestApplicationBaseline();

    if (g_failures != 0)
    {
        std::cerr << "Shared baseline tests failed: " << g_failures << "\n";
        return 1;
    }
    std::cout << "Shared baseline tests passed.\n";
    return 0;
}

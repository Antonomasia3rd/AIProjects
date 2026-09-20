#include "..\dependencies\desktop_app_baseline.h"
#include <wincrypt.h>

#include <iostream>
#include <stdexcept>
#include <thread>

#include "..\dependencies\dpapi.inc"
#include "..\dependencies\startup_shortcut.inc"

static int g_failures = 0;
static bool g_allowStartupIntegration = false;

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

    bool removed = false;
    Check(
        aip::RemoveIniValueFromText(text, L"General", L"First", &removed) &&
            removed &&
            text.find(L"\"First\"") == std::wstring::npos &&
            text.find(L"\"Second\" = \"2\"") != std::wstring::npos,
        "INI value removal preserves neighboring entries");

    text += L"\r\n[Account.secret]\r\n\"Token\" = \"remove\"\r\n[Other]\r\n\"Keep\" = \"yes\"\r\n";
    removed = false;
    Check(
        aip::RemoveIniSectionFromText(text, L"Account.secret", &removed) &&
            removed &&
            text.find(L"[Account.secret]") == std::wstring::npos &&
            text.find(L"[Other]") != std::wstring::npos &&
            text.find(L"\"Keep\" = \"yes\"") != std::wstring::npos,
        "INI section removal preserves following sections");

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

    std::wstring fixtureText;
    std::wstring expectedText;
    bool sharedDialectOk = aip::ReadTextFileUtf8BomAware(
        L"tools/fixtures/ini-dialect.txt", fixtureText) &&
        aip::ReadTextFileUtf8BomAware(L"tools/fixtures/ini-dialect.expected", expectedText);
    auto fixtureDocument = aip::ParseIniDocument(fixtureText);
    size_t fixtureCount = 0;
    for (const auto& line : aip::SplitIniLines(expectedText))
    {
        if (line.empty()) continue;
        const auto separator = line.find(L'\t');
        if (separator == std::wstring::npos || separator == 0)
        {
            sharedDialectOk = false;
            continue;
        }
        ++fixtureCount;
        const auto key = line.substr(0, separator);
        sharedDialectOk = aip::ReadIniValueFromDoc(
            fixtureDocument, L"Options", key.c_str(), dialectValue) &&
            dialectValue == line.substr(separator + 1) && sharedDialectOk;
    }
    Check(sharedDialectOk && fixtureCount == 16,
        "native INI reads the shared managed/native quote/comment/Unicode/path fixture");

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

    std::wstring invalidIniText;
    invalidIniText.push_back(0xD800);
    std::wstring invalidIniPath = path + L".invalid";
    DeleteFileW(invalidIniPath.c_str());
    Check(
        !aip::WriteTextFileUtf8Bom(invalidIniPath, invalidIniText) &&
            GetLastError() == ERROR_NO_UNICODE_TRANSLATION &&
            !aip::FileExists(invalidIniPath),
        "INI file writer fails non-empty text it cannot encode");

    std::wstring oversizedIniPath = path + L".oversized";
    DeleteFileW(oversizedIniPath.c_str());
    HANDLE oversizedIni = CreateFileW(
        oversizedIniPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    LARGE_INTEGER oversizedLength = {};
    oversizedLength.QuadPart = static_cast<LONGLONG>(aip::kMaxIniFileBytes) + 1;
    bool madeOversizedIni =
        oversizedIni != INVALID_HANDLE_VALUE &&
        SetFilePointerEx(oversizedIni, oversizedLength, nullptr, FILE_BEGIN) != FALSE &&
        SetEndOfFile(oversizedIni) != FALSE;
    if (oversizedIni != INVALID_HANDLE_VALUE)
    {
        CloseHandle(oversizedIni);
    }
    std::wstring oversizedIniText;
    Check(
        madeOversizedIni &&
            !aip::LoadIniText(oversizedIniPath, oversizedIniText) &&
            GetLastError() == ERROR_FILE_TOO_LARGE,
        "INI reader rejects oversized sidecar files before allocation");
    DeleteFileW(oversizedIniPath.c_str());

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

    bool boundedIniWait = false;
    {
        aip::IniWriteMutexGuard heldGuard(path, 0);
        if (heldGuard.IsLocked())
        {
            std::thread contender([&]() {
                aip::IniWriteMutexGuard blockedGuard(path, 0);
                boundedIniWait = !blockedGuard.IsLocked() && blockedGuard.Error() == ERROR_SEM_TIMEOUT;
            });
            contender.join();
        }
    }
    Check(boundedIniWait, "INI write mutex supports bounded waits");
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

    int intValue = 0;
    Check(
        aip::ParseIntValueInRange(L" 5000 ", 0, 60000, intValue) &&
            intValue == 5000 &&
            !aip::ParseIntValue(L"12abc", intValue) &&
            !aip::ParseIntValue(L"999999999999999999999999", intValue) &&
            !aip::ParseIntValue(L"-999999999999999999999999", intValue) &&
            !aip::ParseIntValueInRange(L"60001", 0, 60000, intValue),
        "command-line integer parser rejects junk, overflow, and out-of-range values");
}

static void TestJsonBehavior()
{
    const std::string json = "{\"nested\":{\"target\":\"wrong\"},\"target\":\"right\",\"unicode\":\"A\\u00e9\"}";
    Check(aip::ExtractJsonStringValue(json, "target") == L"right", "JSON lookup stays in the current object");
    Check(aip::ExtractJsonStringValue(json, "unicode") == L"A\u00e9", "JSON string decoding handles Unicode escapes");
    Check(aip::ExtractJsonStringValue("{\"application\\u005fid\":\"ok\"}", "application_id") == L"ok",
        "JSON lookup decodes escaped object keys");

    std::wstring extracted;
    Check(
        aip::TryExtractJsonStringValue("{\"empty\":\"\"}", "empty", extracted) && extracted.empty() &&
            !aip::TryExtractJsonStringValue("{\"bad\":\"bad\\qescape\"}", "bad", extracted) &&
            !aip::TryExtractJsonStringValue("{\"other\":\"value\"}", "missing", extracted),
        "JSON try-extract distinguishes empty strings from missing or invalid fields");

    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    Check(
        !aip::FindJsonFieldValue("{\"broken\":{\"x\":1]}", "broken", keyPos, valueStart, valueEnd),
        "JSON lookup rejects mismatched containers");
    Check(
        !aip::FindJsonFieldValue("{\"nested\":{\"target\":\"wrong\"}}", "target", keyPos, valueStart, valueEnd),
        "JSON lookup does not return nested fields");

    std::wstring invalidJsonText;
    invalidJsonText.push_back(0xD800);
    Check(
        aip::JsonEscape(invalidJsonText) != "\"\"",
        "JSON escaping does not silently empty invalid UTF-16");

    std::string invalidUtf8Json = "{\"bad\":\"";
    invalidUtf8Json.push_back(static_cast<char>(0xc3));
    invalidUtf8Json.push_back('(');
    invalidUtf8Json += "\"}";
    Check(
        aip::FindJsonFieldValue(invalidUtf8Json, "bad", keyPos, valueStart, valueEnd) &&
            !aip::DecodeJsonStringRange(invalidUtf8Json, valueStart, valueEnd, invalidJsonText),
        "JSON string decoding rejects invalid UTF-8 bytes");

    const std::string rawControlJson = "{\"bad\":\"line\nfeed\"}";
    Check(
        !aip::FindJsonFieldValue(rawControlJson, "bad", keyPos, valueStart, valueEnd),
        "JSON string scanning rejects unescaped control characters");

    const std::string invalidEscapeJson = "{\"bad\":\"bad\\qescape\"}";
    Check(
        !aip::FindJsonFieldValue(invalidEscapeJson, "bad", keyPos, valueStart, valueEnd),
        "JSON string scanning rejects invalid escape sequences");

    Check(
        !aip::FindJsonFieldValue("{\"bad\": ???, \"target\": \"ok\"}", "target", keyPos, valueStart, valueEnd),
        "JSON lookup rejects invalid primitive tokens before later fields");
    Check(
        !aip::FindJsonFieldValue("{,\"target\":\"ok\"}", "target", keyPos, valueStart, valueEnd) &&
            !aip::FindJsonFieldValue("{\"bad\":0,,\"target\":\"ok\"}", "target", keyPos, valueStart, valueEnd) &&
            !aip::FindJsonFieldValue("{\"bad\":0,}", "target", keyPos, valueStart, valueEnd),
        "JSON lookup rejects leading, repeated, and trailing commas");
}

static std::wstring ProtectLegacyDpapiBytesForTest(const std::vector<BYTE>& plaintext)
{
    DATA_BLOB input = {};
    input.pbData = plaintext.empty() ? nullptr : const_cast<BYTE*>(plaintext.data());
    input.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB output = {};
    if (!CryptProtectData(
            &input,
            L"AIProjects legacy DPAPI compatibility test",
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output))
    {
        throw std::runtime_error("Could not create a legacy DPAPI compatibility fixture.");
    }
    if (output.cbData == 0 || output.pbData == nullptr)
    {
        LocalFree(output.pbData);
        throw std::runtime_error("Legacy DPAPI compatibility fixture was empty.");
    }

    std::vector<BYTE> cipher;
    try
    {
        cipher.assign(output.pbData, output.pbData + output.cbData);
    }
    catch (...)
    {
        LocalFree(output.pbData);
        throw;
    }
    LocalFree(output.pbData);
    return std::wstring(aip::kDpapiPrefix) + aip::BytesToHex(cipher);
}

static void TestDpapiBehavior()
{
    std::wstring invalidSecret;
    invalidSecret.push_back(0xd800);
    bool rejectedInvalidSecret = false;
    try
    {
        (void)aip::ProtectSecretForCurrentUser(invalidSecret);
    }
    catch (const std::exception&)
    {
        rejectedInvalidSecret = true;
    }

    Check(rejectedInvalidSecret, "DPAPI protect rejects invalid UTF-16 before encrypting");

    bool versionedRoundTripOk = false;
    try
    {
        std::wstring protectedEmpty = aip::ProtectSecretForCurrentUser(L"");
        versionedRoundTripOk =
            aip::StartsWithI(protectedEmpty, aip::kDpapiV1Utf8Prefix) &&
            aip::UnprotectSecretForCurrentUser(
                protectedEmpty,
                aip::DpapiLegacyEncoding::Utf8).empty();
    }
    catch (const std::exception&)
    {
        versionedRoundTripOk = false;
    }
    Check(versionedRoundTripOk, "DPAPI writes versioned UTF-8 values and round-trips an empty secret");

    const std::wstring unicodeSecret = L"legacy-\u2603-\U0001f512";
    bool versionedUnicodeOk = false;
    try
    {
        std::wstring protectedUnicode = aip::ProtectSecretForCurrentUser(unicodeSecret);
        versionedUnicodeOk = aip::UnprotectSecretForCurrentUser(
            protectedUnicode,
            aip::DpapiLegacyEncoding::Utf16LittleEndian) == unicodeSecret;
    }
    catch (const std::exception&)
    {
        versionedUnicodeOk = false;
    }
    Check(versionedUnicodeOk, "DPAPI versioned encoding ignores the legacy policy and round-trips Unicode");

    bool legacyUtf8Ok = false;
    bool legacyUtf16Ok = false;
    try
    {
        std::string legacyUtf8;
        if (!aip::TryWideToUtf8(unicodeSecret, legacyUtf8))
        {
            throw std::runtime_error("Could not encode legacy UTF-8 fixture.");
        }
        std::vector<BYTE> utf8Bytes(legacyUtf8.begin(), legacyUtf8.end());
        std::wstring legacyUtf8Value = ProtectLegacyDpapiBytesForTest(utf8Bytes);
        legacyUtf8Ok = aip::UnprotectSecretForCurrentUser(
            legacyUtf8Value,
            aip::DpapiLegacyEncoding::Utf8) == unicodeSecret;

        std::vector<BYTE> utf16Bytes;
        utf16Bytes.reserve(unicodeSecret.size() * 2);
        for (wchar_t ch : unicodeSecret)
        {
            utf16Bytes.push_back(static_cast<BYTE>(ch & 0xff));
            utf16Bytes.push_back(static_cast<BYTE>((ch >> 8) & 0xff));
        }
        std::wstring legacyUtf16Value = ProtectLegacyDpapiBytesForTest(utf16Bytes);
        legacyUtf16Ok = aip::UnprotectSecretForCurrentUser(
            legacyUtf16Value,
            aip::DpapiLegacyEncoding::Utf16LittleEndian) == unicodeSecret;

        if (!legacyUtf8.empty())
        {
            SecureZeroMemory(legacyUtf8.data(), legacyUtf8.size());
        }
        if (!utf8Bytes.empty())
        {
            SecureZeroMemory(utf8Bytes.data(), utf8Bytes.size());
        }
        if (!utf16Bytes.empty())
        {
            SecureZeroMemory(utf16Bytes.data(), utf16Bytes.size());
        }
    }
    catch (const std::exception&)
    {
        legacyUtf8Ok = false;
        legacyUtf16Ok = false;
    }
    Check(legacyUtf8Ok, "DPAPI explicitly decodes DiscordRPC legacy UTF-8 values");
    Check(legacyUtf16Ok, "DPAPI explicitly decodes deskband legacy UTF-16LE values");

    bool rejectedUnknownVersion = false;
    try
    {
        (void)aip::UnprotectSecretForCurrentUser(
            L"dpapi:v2:utf8:00",
            aip::DpapiLegacyEncoding::Utf8);
    }
    catch (const std::exception&)
    {
        rejectedUnknownVersion = true;
    }
    Check(rejectedUnknownVersion, "DPAPI rejects unknown serialization versions before decrypting");

    bool rejectedRelabelledEnvelope = false;
    try
    {
        std::wstring protectedValue = aip::ProtectSecretForCurrentUser(L"prefix-integrity");
        protectedValue.replace(
            0,
            wcslen(aip::kDpapiV1Utf8Prefix),
            aip::kDpapiPrefix);
        (void)aip::UnprotectSecretForCurrentUser(
            protectedValue,
            aip::DpapiLegacyEncoding::Utf8);
    }
    catch (const std::exception&)
    {
        rejectedRelabelledEnvelope = true;
    }
    Check(rejectedRelabelledEnvelope, "DPAPI rejects a versioned envelope relabelled as legacy data");
}

static void TestTrayBehavior()
{
    Check(
        aip::NormalizeTrayTooltip(L"  current state  ", L"Product") ==
            L"current state",
        "tray tooltip normalization trims visible state text");
    Check(
        aip::NormalizeTrayTooltip(L" \t\r\n ", L"  Product Name  ") ==
            L"Product Name" &&
            aip::NormalizeTrayTooltip(L"", L"") == L"Application",
        "tray tooltip normalization rejects blank hover text");
    Check(
        aip::NormalizeTrayTooltip(std::wstring(L"\0 state", 7), L"Product") == L"state" &&
            aip::NormalizeTrayTooltip(std::wstring(1, L'\0'), L"  Product  ") == L"Product",
        "tray tooltip normalization keeps embedded NULs from hiding status or fallback text");
    Check(
        aip::NormalizeTrayTooltip(std::wstring(126, L'x') + L"\xD83D\xDE00", L"Product") ==
            std::wstring(126, L'x') &&
            aip::NormalizeTrayTooltip(std::wstring(128, L'x'), L"Product").size() == 127,
        "tray tooltip truncation respects the shell limit without splitting a Unicode pair");

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

    HMENU headerMenu = CreatePopupMenu();
    bool headerAppended = aip::AppendBaselineTrayMenuHeader(
        headerMenu,
        200,
        L"Show menu as dropdown",
        true,
        201,
        L"Refresh now",
        L"Version: Product-v7 (7.0.0.0)");
    wchar_t dropdownText[64] = {};
    GetMenuStringW(headerMenu, 0, dropdownText, ARRAYSIZE(dropdownText), MF_BYPOSITION);
    wchar_t versionText[64] = {};
    GetMenuStringW(headerMenu, 2, versionText, ARRAYSIZE(versionText), MF_BYPOSITION);
    Check(
        headerAppended &&
            GetMenuItemCount(headerMenu) == 4 &&
            std::wstring(dropdownText) == L"Show menu as dropdown" &&
            GetMenuItemID(headerMenu, 0) == 200 &&
            (GetMenuState(headerMenu, 0, MF_BYPOSITION) & MF_CHECKED) != 0 &&
            GetMenuItemID(headerMenu, 1) == 201 &&
            std::wstring(versionText) == L"Version: Product-v7 (7.0.0.0)" &&
            (GetMenuState(headerMenu, 2, MF_BYPOSITION) & MF_DISABLED) != 0 &&
            (GetMenuState(headerMenu, 3, MF_BYPOSITION) & MF_SEPARATOR) != 0,
        "baseline tray header preserves dropdown, primary action, version, separator order");
    DestroyMenu(headerMenu);
}


static void TestAppPathBehavior()
{
    std::wstring systemDirectory = aip::GetSystemDirectoryPath();
    DWORD systemDirectoryAttributes = GetFileAttributesW(systemDirectory.c_str());
    Check(
        !systemDirectory.empty() &&
            systemDirectoryAttributes != INVALID_FILE_ATTRIBUTES &&
            (systemDirectoryAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
        "system directory helper returns an existing directory");

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

    aip::SidecarPaths executableLogPolicy = aip::BuildSidecarPathsFromExecutable(
        L"C:\\Tools\\DiscordRPC.exe",
        L"DiscordRPC",
        L"C:\\Config\\custom.ini",
        aip::DefaultLogPathPolicy::BesideExecutable);
    Check(
        executableLogPolicy.defaultLogPath == L"C:\\Tools\\DiscordRPC.log",
        "sidecar paths expose explicit executable-side default log policy");

    Check(
        aip::BuildExecutableSidecarLogPath(overridden) == L"C:\\Tools\\DiscordRPC.log",
        "sidecar paths can preserve executable-side default log behavior");

    std::wstring absolutePath;
    Check(
        aip::TryMakeAbsolutePath(L".", absolutePath, nullptr) && !absolutePath.empty(),
        "strict absolute path helper resolves valid paths");
    Check(
        !aip::TryMakeAbsolutePath(L"", absolutePath, nullptr),
        "strict absolute path helper rejects empty paths");

    std::wstring configPathError;
    Check(
        !aip::TryResolveConfigFilePath(L"C:\\Config\\", absolutePath, &configPathError),
        "config path helper rejects trailing directory separators");

    wchar_t tempDirectory[MAX_PATH] = {};
    DWORD tempLength = GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory);
    if (tempLength != 0 && tempLength < ARRAYSIZE(tempDirectory))
    {
        Check(
            !aip::TryResolveConfigFilePath(tempDirectory, absolutePath, &configPathError),
            "config path helper rejects existing directories");
    }

    Check(
        !aip::TryResolveConfigFilePath(L"CON.ini", absolutePath, &configPathError) &&
            !aip::TryResolveConfigFilePath(L"LPT1.log", absolutePath, &configPathError) &&
            !aip::TryResolveConfigFilePath(L"aux", absolutePath, &configPathError),
        "config path helper rejects reserved Windows device names");

    Check(
        !aip::TryResolveConfigFilePath(L"config.ini:stream", absolutePath, &configPathError) &&
            !aip::TryResolveConfigFilePath(L"C:\\Config\\settings.ini:stream", absolutePath, &configPathError),
        "config path helper rejects alternate data stream names");

    Check(
        !aip::TryResolveConfigFilePath(L"bad<name.ini", absolutePath, &configPathError) &&
            !aip::TryResolveConfigFilePath(L"bad|name.ini", absolutePath, &configPathError) &&
            !aip::TryResolveConfigFilePath(L"badname.ini.", absolutePath, &configPathError) &&
            !aip::TryResolveConfigFilePath(L"badname.ini ", absolutePath, &configPathError),
        "config path helper rejects invalid Windows filename characters");
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

    aip::RecentLogBuffer recentBuffer;
    recentBuffer.SetMaxLines(2);
    recentBuffer.Push(L"one");
    recentBuffer.Push(L"two");
    recentBuffer.Push(L"three");
    std::vector<std::wstring> recentSnapshot = recentBuffer.Snapshot();
    Check(
        recentSnapshot.size() == 2 && recentSnapshot[0] == L"two" && recentSnapshot[1] == L"three",
        "shared recent log buffer preserves DesktopStub tray-log behavior");

    options.enabled = false;
    logger.Configure(options);
    logger.WriteRawLine(L"ignored");
    recent = logger.RecentLines();
    Check(
        recent.size() == 2 && recent[0] == L"second" && recent[1] == L"third",
        "shared UTF-8 logger honors disabled logging");

    wchar_t tempDirectory[MAX_PATH] = {};
    DWORD length = GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory);
    std::wstring logPath = length != 0 && length < ARRAYSIZE(tempDirectory)
        ? std::wstring(tempDirectory) + L"AIP-SharedLogger-" + std::to_wstring(GetCurrentProcessId()) + L".log"
        : L"AIP-SharedLogger.log";
    DeleteFileW(logPath.c_str());

    std::wstring invalidWideText;
    invalidWideText.push_back(0xD800);
    std::string invalidUtf8;
    Check(
        !aip::TryWideToUtf8(invalidWideText, invalidUtf8) &&
            !aip::AppendUtf8TextToFile(logPath, invalidWideText) &&
            GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
        "shared UTF-8 logger fails non-empty text it cannot encode");
    DeleteFileW(logPath.c_str());

    HANDLE held = CreateFileW(
        logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    aip::Utf8Logger fileLogger;
    aip::Utf8LoggerOptions fileOptions;
    fileOptions.filePath = logPath;
    fileOptions.maxRecentLines = 4;
    fileLogger.Configure(fileOptions);
    fileLogger.WriteRawLine(L"shared file logger concurrent append");
    if (held != INVALID_HANDLE_VALUE)
    {
        CloseHandle(held);
    }
    std::vector<BYTE> logBytes;
    std::wstring logText;
    bool logReadable = aip::ReadWholeFileBytes(logPath, logBytes) && aip::DecodeTextBytes(logBytes, logText);
    Check(
        logReadable && logText.find(L"shared file logger concurrent append") != std::wstring::npos,
        "shared UTF-8 logger allows concurrent appenders");
    Check(
        logBytes.size() >= 3 && logBytes[0] == 0xEF && logBytes[1] == 0xBB && logBytes[2] == 0xBF,
        "shared UTF-8 logger writes BOM for new log files");

    std::wstring sharingLogPath = logPath + L".sharing";
    DeleteFileW(sharingLogPath.c_str());
    Check(
        aip::AppendUtf8LineToFile(sharingLogPath, L"initial", true, 1000),
        "shared UTF-8 logger prepares sharing-retry fixture");
    HANDLE restrictiveReader = CreateFileW(
        sharingLogPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    bool sharingRetryAppendOk = false;
    std::thread sharingRetryWriter([&]() {
        sharingRetryAppendOk =
            aip::AppendUtf8LineToFile(sharingLogPath, L"after reader", true, 1000);
    });
    Sleep(100);
    if (restrictiveReader != INVALID_HANDLE_VALUE)
    {
        CloseHandle(restrictiveReader);
    }
    sharingRetryWriter.join();
    std::vector<BYTE> sharingLogBytes;
    std::wstring sharingLogText;
    bool sharingLogReadable =
        aip::ReadWholeFileBytes(sharingLogPath, sharingLogBytes) &&
        aip::DecodeTextBytes(sharingLogBytes, sharingLogText);
    Check(
        restrictiveReader != INVALID_HANDLE_VALUE &&
            sharingRetryAppendOk &&
            sharingLogReadable &&
            sharingLogText.find(L"after reader") != std::wstring::npos,
        "shared UTF-8 logger retries transient reader sharing violations");

    std::wstring utf16LogPath = logPath + L".utf16";
    DeleteFileW(utf16LogPath.c_str());
    std::vector<BYTE> utf16Bytes;
    const std::wstring expectedUtf16 = L"legacy one\r\nlegacy two\r\n";
    bool utf16AppendOk =
        aip::AppendUtf16LineToFile(utf16LogPath, L"legacy one", false, 5000) &&
        aip::AppendUtf16LineToFile(utf16LogPath, L"legacy two", false, 5000) &&
        aip::ReadWholeFileBytes(utf16LogPath, utf16Bytes);
    Check(
        utf16AppendOk &&
            utf16Bytes.size() == expectedUtf16.size() * sizeof(wchar_t) &&
            std::memcmp(
                utf16Bytes.data(),
                expectedUtf16.data(),
                utf16Bytes.size()) == 0,
        "shared UTF-16 compatibility logger appends complete lines");

    std::wstring badLogPath = logPath + L".dir";
    RemoveDirectoryW(badLogPath.c_str());
    DeleteFileW(badLogPath.c_str());
    CreateDirectoryW(badLogPath.c_str(), nullptr);
    fileLogger.SetFilePath(badLogPath);
    fileLogger.WriteRawLine(L"this should fail");
    std::vector<std::wstring> failureRecent = fileLogger.RecentLines();
    bool failureReported = false;
    for (const std::wstring& line : failureRecent)
    {
        if (line.find(L"Log file write failed") != std::wstring::npos)
        {
            failureReported = true;
            break;
        }
    }
    Check(
        fileLogger.LastFileWriteFailed() && failureReported,
        "shared UTF-8 logger reports file write failures once");

    fileLogger.SetFilePath(logPath);
    fileLogger.WriteRawLine(L"recovery write clears failure report suppression");
    fileLogger.SetFilePath(badLogPath);
    fileLogger.WriteRawLine(L"second failure after recovery should be reported");
    std::vector<std::wstring> secondFailureRecent = fileLogger.RecentLines();
    int failureReportCount = 0;
    for (const std::wstring& line : secondFailureRecent)
    {
        if (line.find(L"Log file write failed") != std::wstring::npos)
        {
            ++failureReportCount;
        }
    }
    Check(
        failureReportCount >= 2,
        "shared UTF-8 logger reports a new failure after recovery");

    fileLogger.SetFilePath(logPath);
    Check(
        !fileLogger.LastFileWriteFailed() && fileLogger.LastFileWriteError() == ERROR_SUCCESS,
        "shared UTF-8 logger resets failure state when target changes");

    HANDLE lockedHandle = CreateFileW(
        logPath.c_str(),
        FILE_APPEND_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    bool boundedLockFailure = false;
    if (lockedHandle != INVALID_HANDLE_VALUE)
    {
        aip::ExclusiveFileRangeLock heldLock(lockedHandle, 0);
        if (heldLock.IsLocked())
        {
            fileOptions.lockWaitMs = 25;
            fileOptions.filePath = logPath;
            fileLogger.Configure(fileOptions);
            DWORD before = GetTickCount();
            fileLogger.WriteRawLine(L"this should hit the bounded lock wait");
            DWORD elapsed = GetTickCount() - before;
            boundedLockFailure =
                fileLogger.LastFileWriteFailed() &&
                fileLogger.LastFileWriteError() == ERROR_LOCK_VIOLATION &&
                elapsed < 1000;
        }
        CloseHandle(lockedHandle);
    }
    Check(
        boundedLockFailure,
        "shared UTF-8 logger uses bounded append lock wait");

    RemoveDirectoryW(badLogPath.c_str());
    DeleteFileW(sharingLogPath.c_str());
    DeleteFileW(utf16LogPath.c_str());
    DeleteFileW(logPath.c_str());
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

    std::wstring pathHash = aip::StableHashHex64(L"C:\\Tools\\DiscordRPC.ini");
    aip::InstanceIdentity pathScoped = aip::BuildPathScopedInstanceIdentity(
        L"DiscordRPC",
        L"DiscordRPC.Stop",
        L"DiscordRPCTrayWnd",
        L"",
        L"C:\\Tools\\DiscordRPC.ini");
    Check(
        !pathHash.empty() &&
            pathScoped.mutexName == L"Local\\DiscordRPC." + pathHash &&
            pathScoped.messageName == L"DiscordRPC.Stop." + pathHash,
        "single-instance identity supports shared path-scoped hashing");

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

static void TestStartupShortcutBehavior()
{
    Check(
        aip::SanitizeShortcutBaseName(L"Bad<>:\"/\\|?* Name. ") ==
            L"Bad_________ Name",
        "Startup shortcut file names remove invalid characters and trailing dots");

    std::wstring longName(200, L'A');
    Check(
        aip::SanitizeShortcutBaseName(longName).size() == 80,
        "Startup shortcut file names are capped for legacy ShellLink paths");

    wchar_t tempDirectory[MAX_PATH] = {};
    DWORD length = GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory);
    if (length == 0 || length >= ARRAYSIZE(tempDirectory))
    {
        Check(false, "Startup shortcut ShellLink round trip");
        return;
    }

    std::wstring shortcutPath = std::wstring(tempDirectory) +
        L"AIP-StartupShortcut-" + std::to_wstring(GetCurrentProcessId()) + L".lnk";
    DeleteFileW(shortcutPath.c_str());

    aip::StartupShortcutComScope com;
    std::wstring error;
    std::wstring stagedShortcut;
    bool stagingResolved = aip::BuildStartupShortcutTemporaryPath(
        shortcutPath, stagedShortcut, &error);
    Check(
        stagingResolved &&
            aip::NormalizeShortcutComparisonPath(aip::GetDirectoryName(stagedShortcut)) !=
                aip::NormalizeShortcutComparisonPath(aip::GetDirectoryName(shortcutPath)) &&
            aip::NormalizeShortcutComparisonPath(aip::GetDirectoryName(
                aip::GetDirectoryName(stagedShortcut))) ==
                aip::NormalizeShortcutComparisonPath(aip::GetDirectoryName(
                    aip::GetDirectoryName(shortcutPath))) &&
            GetFileAttributesW(stagedShortcut.c_str()) == INVALID_FILE_ATTRIBUTES,
        "Startup shortcut staging stays outside the launch folder on the same parent volume");
    aip::StartupShortcutSpec spec;
    spec.executablePath = aip::GetCurrentExecutablePath();
    spec.identityPath = std::wstring(tempDirectory) + L"Profile One.ini";
    spec.fallbackBaseName = L"SharedBaselineTests";
    spec.arguments = L"--startup-test \"quoted value\"";
    spec.workingDirectory = aip::GetDirectoryName(spec.executablePath);
    spec.description = L"AIProjects shared startup helper test";

    bool wrote = com.Ready() &&
        aip::WriteStartupShortcut(spec, shortcutPath, &error);
    std::wstring target;
    std::wstring arguments;
    std::wstring workingDirectory;
    bool loaded = wrote && aip::LoadStartupShortcut(
        shortcutPath,
        target,
        arguments,
        workingDirectory,
        &error);
    Check(
        loaded &&
            aip::NormalizeShortcutComparisonPath(target) ==
                aip::NormalizeShortcutComparisonPath(spec.executablePath) &&
            arguments == spec.arguments &&
            aip::NormalizeShortcutComparisonPath(workingDirectory) ==
                aip::NormalizeShortcutComparisonPath(spec.workingDirectory),
        "Startup shortcut ShellLink round trip");

    bool installed = false;
    bool queried = aip::QueryStartupShortcutInstalledAtPath(
        spec,
        shortcutPath,
        installed,
        &error);
    aip::StartupShortcutSpec staleArguments = spec;
    staleArguments.arguments = L"--stale-arguments";
    bool staleInstalled = true;
    bool staleQueried = aip::QueryStartupShortcutInstalledAtPath(
        staleArguments,
        shortcutPath,
        staleInstalled,
        &error);
    Check(
        queried && installed && staleQueried && !staleInstalled,
        "Startup shortcut query detects stale launch metadata");

    Check(
        !aip::StartupShortcutFileName(spec).empty() &&
            aip::StartupShortcutFileName(spec).find(L"SharedBaselineTests-") == 0,
        "Startup shortcut names are path scoped");

    aip::StartupShortcutSpec secondProfile = spec;
    secondProfile.identityPath = std::wstring(tempDirectory) + L"Profile Two.ini";
    aip::StartupShortcutSpec requotedProfile = spec;
    requotedProfile.arguments = L"--startup-test=changed";
    aip::StartupShortcutSpec renamedExecutable = spec;
    renamedExecutable.executablePath = aip::PathJoin(
        aip::GetDirectoryName(spec.executablePath),
        L"RenamedSharedBaselineTests.exe");
    Check(
        aip::StartupShortcutFileName(spec) !=
            aip::StartupShortcutFileName(secondProfile) &&
            aip::StartupShortcutFileName(spec) ==
                aip::StartupShortcutFileName(requotedProfile) &&
            aip::StartupShortcutFileName(spec) ==
                aip::StartupShortcutFileName(renamedExecutable),
        "Startup shortcut identity separates profiles but survives argument and executable-name repairs");

    std::atomic<int> concurrentWrites{ 0 };
    aip::StartupShortcutSpec concurrentA = spec;
    concurrentA.arguments = L"--concurrent=A";
    aip::StartupShortcutSpec concurrentB = spec;
    concurrentB.arguments = L"--concurrent=B";
    std::thread writerA([&]() {
        std::wstring threadError;
        if (aip::SetStartupShortcutInstalledAtPath(
            concurrentA,
            shortcutPath,
            true,
            &threadError))
        {
            ++concurrentWrites;
        }
    });
    std::thread writerB([&]() {
        std::wstring threadError;
        if (aip::SetStartupShortcutInstalledAtPath(
            concurrentB,
            shortcutPath,
            true,
            &threadError))
        {
            ++concurrentWrites;
        }
    });
    writerA.join();
    writerB.join();
    target.clear();
    arguments.clear();
    workingDirectory.clear();
    bool concurrentLoaded = aip::LoadStartupShortcut(
        shortcutPath,
        target,
        arguments,
        workingDirectory,
        &error);
    Check(
        concurrentWrites == 2 && concurrentLoaded &&
            (arguments == concurrentA.arguments || arguments == concurrentB.arguments),
        "Startup shortcut replacement is safe under same-process concurrency");

    bool removed = aip::SetStartupShortcutInstalledAtPath(
        spec,
        shortcutPath,
        false,
        &error);
    bool removedAgain = aip::SetStartupShortcutInstalledAtPath(
        spec,
        shortcutPath,
        false,
        &error);
    Check(
        removed && removedAgain && !aip::FileExists(shortcutPath),
        "Startup shortcut removal is target-safe and idempotent");

    bool previousBeforeInstall = true;
    bool desiredInstall = aip::SetStartupShortcutDesiredAtPath(
        spec,
        shortcutPath,
        true,
        previousBeforeInstall,
        &error);
    bool previousBeforeNoOp = false;
    bool desiredNoOp = aip::SetStartupShortcutDesiredAtPath(
        spec,
        shortcutPath,
        true,
        previousBeforeNoOp,
        &error);
    bool previousBeforeRemove = false;
    bool desiredRemove = aip::SetStartupShortcutDesiredAtPath(
        spec,
        shortcutPath,
        false,
        previousBeforeRemove,
        &error);
    Check(
        desiredInstall && !previousBeforeInstall &&
            desiredNoOp && previousBeforeNoOp &&
            desiredRemove && previousBeforeRemove &&
            !aip::FileExists(shortcutPath),
        "Startup shortcut desired-state transaction reports and changes previous state atomically");

    wchar_t systemDirectory[MAX_PATH] = {};
    UINT systemLength = GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory));
    aip::StartupShortcutSpec foreign = spec;
    if (systemLength > 0 && systemLength < ARRAYSIZE(systemDirectory))
    {
        foreign.executablePath = aip::PathJoin(systemDirectory, L"cmd.exe");
        foreign.workingDirectory = systemDirectory;
    }
    bool wroteForeign = aip::WriteStartupShortcut(foreign, shortcutPath, &error);
    bool refusedForeignInstall = !aip::SetStartupShortcutInstalledAtPath(
        spec,
        shortcutPath,
        true,
        &error);
    target.clear();
    arguments.clear();
    workingDirectory.clear();
    bool foreignStillIntact = aip::LoadStartupShortcut(
        shortcutPath,
        target,
        arguments,
        workingDirectory,
        &error) &&
        aip::NormalizeShortcutComparisonPath(target) ==
            aip::NormalizeShortcutComparisonPath(foreign.executablePath);
    bool refusedForeignRemoval = !aip::SetStartupShortcutInstalledAtPath(
        spec,
        shortcutPath,
        false,
        &error);
    Check(
        wroteForeign && refusedForeignInstall && foreignStillIntact &&
            refusedForeignRemoval && aip::FileExists(shortcutPath),
        "Startup shortcut install and removal refuse a same-name foreign target");

    std::wstring staleDirectory = std::wstring(tempDirectory) +
        L"AIP-StartupShortcut-Stale-" +
        std::to_wstring(GetCurrentProcessId());
    std::wstring staleShortcutPath = shortcutPath + L".stale.lnk";
    DeleteFileW(staleShortcutPath.c_str());
    RemoveDirectoryW(staleDirectory.c_str());
    bool createdStaleDirectory = CreateDirectoryW(staleDirectory.c_str(), nullptr) != FALSE;
    aip::StartupShortcutSpec staleDirectorySpec = spec;
    staleDirectorySpec.workingDirectory = staleDirectory;
    bool wroteStaleDirectory = createdStaleDirectory &&
        aip::WriteStartupShortcut(
            staleDirectorySpec,
            staleShortcutPath,
            &error);
    bool removedStaleDirectory = RemoveDirectoryW(staleDirectory.c_str()) != FALSE;
    bool removedAfterDirectoryDisappeared = removedStaleDirectory &&
        aip::SetStartupShortcutInstalledAtPath(
            staleDirectorySpec,
            staleShortcutPath,
            false,
            &error);
    Check(
        wroteStaleDirectory && removedAfterDirectoryDisappeared &&
            !aip::FileExists(staleShortcutPath),
        "Startup shortcut removal works after its working directory disappears");

    std::wstring targetlessPath = shortcutPath + L".targetless.lnk";
    DeleteFileW(targetlessPath.c_str());
    bool wroteTargetless = false;
    IShellLinkW* targetlessLink = nullptr;
    HRESULT targetlessResult = CoCreateInstance(
        CLSID_ShellLink,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&targetlessLink));
    IPersistFile* targetlessPersist = nullptr;
    if (SUCCEEDED(targetlessResult) && targetlessLink != nullptr)
    {
        targetlessResult = targetlessLink->QueryInterface(
            IID_PPV_ARGS(&targetlessPersist));
    }
    if (SUCCEEDED(targetlessResult) && targetlessPersist != nullptr)
    {
        targetlessResult = targetlessPersist->Save(targetlessPath.c_str(), TRUE);
        wroteTargetless = SUCCEEDED(targetlessResult);
    }
    if (targetlessPersist != nullptr)
    {
        targetlessPersist->Release();
    }
    if (targetlessLink != nullptr)
    {
        targetlessLink->Release();
    }
    target.clear();
    arguments.clear();
    workingDirectory.clear();
    bool rejectedTargetless = wroteTargetless && !aip::LoadStartupShortcut(
        targetlessPath,
        target,
        arguments,
        workingDirectory,
        &error);
    Check(
        wroteTargetless && rejectedTargetless,
        "Startup shortcut loader rejects a targetless ShellLink");

    bool mtaScopeReady = false;
    std::thread mtaThread([&]() {
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            aip::StartupShortcutComScope nested;
            mtaScopeReady = nested.Ready();
        }
        if (SUCCEEDED(initialized))
        {
            CoUninitialize();
        }
    });
    mtaThread.join();
    Check(
        mtaScopeReady,
        "Startup shortcut COM scope accepts an existing MTA apartment");

    aip::StartupShortcutSpec relative = spec;
    relative.executablePath = L"relative.exe";
    Check(
        !aip::ValidateStartupShortcutSpec(relative, &error),
        "Startup shortcut helper rejects relative executable paths");

    aip::StartupShortcutSpec tooLong = spec;
    tooLong.executablePath = L"C:\\" + std::wstring(MAX_PATH, L'X');
    Check(
        !aip::ValidateStartupShortcutIdentity(tooLong, &error),
        "Startup shortcut helper rejects targets ShellLink cannot round-trip");

    DeleteFileW(shortcutPath.c_str());
    DeleteFileW(staleShortcutPath.c_str());
    DeleteFileW(targetlessPath.c_str());
    RemoveDirectoryW(staleDirectory.c_str());
}

static void TestCrashConsistentLaunchConfigState()
{
    std::wstring error;
    std::vector<std::wstring> order;
    bool enabled = aip::ExecuteCrashConsistentLaunchConfigState(
        true,
        false,
        [&](bool state, std::wstring&)
        {
            order.push_back(state ? L"launch=true" : L"launch=false");
            return true;
        },
        [&](std::wstring&)
        {
            order.push_back(L"persist");
            return true;
        },
        [&](std::wstring&)
        {
            order.push_back(L"restore");
            return true;
        },
        &error);
    Check(
        enabled && error.empty() &&
            order == std::vector<std::wstring>{ L"launch=true", L"persist" },
        "Startup/config transaction installs before persisting enabled state");

    order.clear();
    bool disabled = aip::ExecuteCrashConsistentLaunchConfigState(
        false,
        true,
        [&](bool state, std::wstring&)
        {
            order.push_back(state ? L"launch=true" : L"launch=false");
            return true;
        },
        [&](std::wstring&)
        {
            order.push_back(L"persist");
            return true;
        },
        [&](std::wstring&)
        {
            order.push_back(L"restore");
            return true;
        },
        &error);
    Check(
        disabled && error.empty() &&
            order == std::vector<std::wstring>{ L"persist", L"launch=false" },
        "Startup/config transaction persists disabled state before removal");

    order.clear();
    bool rejectedEnable = aip::ExecuteCrashConsistentLaunchConfigState(
        true,
        false,
        [&](bool state, std::wstring&)
        {
            order.push_back(state ? L"launch=true" : L"launch=false");
            return true;
        },
        [&](std::wstring& persistenceError)
        {
            order.push_back(L"persist");
            persistenceError = L"injected write failure";
            return false;
        },
        [&](std::wstring&)
        {
            order.push_back(L"restore");
            return true;
        },
        &error);
    Check(
        !rejectedEnable && !error.empty() &&
            order == std::vector<std::wstring>{
                L"launch=true", L"persist", L"launch=false" },
        "Startup/config transaction rolls back launch after enable persistence failure");

    order.clear();
    int disableAttempts = 0;
    bool rejectedDisable = aip::ExecuteCrashConsistentLaunchConfigState(
        false,
        true,
        [&](bool state, std::wstring& launchError)
        {
            order.push_back(state ? L"launch=true" : L"launch=false");
            if (!state && disableAttempts++ == 0)
            {
                launchError = L"injected removal failure";
                return false;
            }
            return true;
        },
        [&](std::wstring&)
        {
            order.push_back(L"persist");
            return true;
        },
        [&](std::wstring&)
        {
            order.push_back(L"restore");
            return true;
        },
        &error);
    Check(
        !rejectedDisable && !error.empty() &&
            order == std::vector<std::wstring>{
                L"persist", L"launch=false", L"launch=true", L"restore" },
        "Startup/config transaction restores launch before enabled config rollback");

    bool parsed = false;
    Check(
        aip::ReadIniBooleanFromText(
            L"[Settings]\r\nRunAtStartup=yes\r\n",
            L"Settings",
            L"RunAtStartup",
            false,
            parsed) && parsed &&
        !aip::ReadIniBooleanFromText(
            L"[Settings]\r\nRunAtStartup=maybe\r\n",
            L"Settings",
            L"RunAtStartup",
            false,
            parsed),
        "Startup/config transaction parses prior boolean state strictly");

    if (!g_allowStartupIntegration)
    {
        std::cout << "skip - real Startup-folder integration (explicit --allow-startup-integration only); fake transactions and temporary-directory ShellLinks remain covered\n";
        return;
    }

    wchar_t tempDirectory[MAX_PATH] = {};
    DWORD tempLength = GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory);
    bool integrationPassed = false;
    if (tempLength > 0 && tempLength < ARRAYSIZE(tempDirectory))
    {
        std::wstring iniPath = std::wstring(tempDirectory) +
            L"AIP-CoupledStartup-" + std::to_wstring(GetCurrentProcessId()) + L".ini";
        DeleteFileW(iniPath.c_str());

        aip::StartupShortcutSpec spec;
        spec.executablePath = aip::GetCurrentExecutablePath();
        spec.identityPath = iniPath;
        spec.fallbackBaseName = L"AIP-CoupledStartupTest";
        spec.arguments = L"--coupled-startup-test";
        spec.workingDirectory = aip::GetDirectoryName(spec.executablePath);
        spec.description = L"Temporary AIProjects coupled Startup/INI test";

        bool initialized = aip::WriteTextFileUtf8Bom(
            iniPath,
            L"[Settings]\r\nRunAtStartup=false\r\n");
        bool committedEnable = initialized && aip::CommitStartupShortcutIniState(
            spec,
            iniPath,
            L"",
            L"Settings",
            L"RunAtStartup",
            false,
            true,
            [&](std::wstring& text)
            {
                return aip::WriteIniValueToText(
                    text,
                    L"Settings",
                    L"RunAtStartup",
                    L"true");
            },
            &error,
            5000);
        bool installed = false;
        bool enabledState = committedEnable &&
            aip::QueryStartupShortcutInstalled(spec, installed, &error) &&
            installed &&
            aip::IniReadRaw(
                iniPath,
                L"Settings",
                L"RunAtStartup",
                L"") == L"true";
        bool committedDisable = enabledState && aip::CommitStartupShortcutIniState(
            spec,
            iniPath,
            L"",
            L"Settings",
            L"RunAtStartup",
            false,
            false,
            [&](std::wstring& text)
            {
                return aip::WriteIniValueToText(
                    text,
                    L"Settings",
                    L"RunAtStartup",
                    L"false");
            },
            &error,
            5000);
        installed = true;
        integrationPassed = committedDisable &&
            aip::QueryStartupShortcutInstalled(spec, installed, &error) &&
            !installed &&
            aip::IniReadRaw(
                iniPath,
                L"Settings",
                L"RunAtStartup",
                L"") == L"false";

        std::wstring cleanupError;
        aip::SetStartupShortcutInstalled(spec, false, &cleanupError);
        DeleteFileW(iniPath.c_str());
    }
    Check(
        integrationPassed,
        "shared Startup/INI transaction commits and removes a real profile-scoped ShellLink");
}

int wmain(int argc, wchar_t** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--allow-startup-integration") != 0) return 2;
        g_allowStartupIntegration = true;
    }
    TestIniBehavior();
    TestCommandLineBehavior();
    TestJsonBehavior();
    TestDpapiBehavior();
    TestTrayBehavior();
    TestAppPathBehavior();
    TestLoggingBehavior();
    TestApplicationBaseline();
    TestCrashConsistentLaunchConfigState();
    TestStartupShortcutBehavior();

    if (g_failures != 0)
    {
        std::cerr << "Shared baseline tests failed: " << g_failures << "\n";
        return 1;
    }
    std::cout << "Shared baseline tests passed.\n";
    return 0;
}

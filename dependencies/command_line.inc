#ifndef AIP_DEPENDENCIES_COMMAND_LINE_INC
#define AIP_DEPENDENCIES_COMMAND_LINE_INC

namespace aip
{
inline bool WriteCommandLineTextToHandle(HANDLE handle, const std::wstring& output)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode))
    {
        size_t offset = 0;
        while (offset < output.size())
        {
            DWORD chunk = static_cast<DWORD>((std::min<size_t>)(output.size() - offset, 16384));
            DWORD written = 0;
            if (!WriteConsoleW(handle, output.data() + offset, chunk, &written, nullptr) || written == 0)
            {
                if (GetLastError() == ERROR_SUCCESS)
                {
                    SetLastError(ERROR_WRITE_FAULT);
                }
                return false;
            }
            offset += written;
        }
        return true;
    }

    std::string utf8;
    if (!TryWideToUtf8(output, utf8))
    {
        return false;
    }
    if (utf8.size() > MAXDWORD)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return false;
    }
    return utf8.empty() ||
        WriteAllBytes(handle, utf8.data(), static_cast<DWORD>(utf8.size()));
}

inline bool WriteCommandLineText(
    const std::wstring& text,
    bool errorOutput = false,
    bool appendNewline = true)
{
    std::wstring output = text;
    if (appendNewline &&
        (output.empty() || (output.back() != L'\r' && output.back() != L'\n')))
    {
        output += L"\r\n";
    }

    HANDLE handle = GetStdHandle(errorOutput ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (WriteCommandLineTextToHandle(handle, output))
    {
        return true;
    }

    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (!attached && GetLastError() != ERROR_ACCESS_DENIED)
    {
        return false;
    }

    handle = GetStdHandle(errorOutput ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    bool ok = WriteCommandLineTextToHandle(handle, output);
    if (attached)
    {
        FreeConsole();
    }
    return ok;
}

inline errno_t BindStandardConsoleStreams()
{
    FILE* stream = nullptr;
    errno_t firstError = freopen_s(&stream, "CONOUT$", "w", stdout);
    errno_t error = freopen_s(&stream, "CONOUT$", "w", stderr);
    if (firstError == 0)
    {
        firstError = error;
    }
    error = freopen_s(&stream, "CONIN$", "r", stdin);
    if (firstError == 0)
    {
        firstError = error;
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    return firstError;
}

struct IniSetting
{
    std::wstring section;
    std::wstring key;
    std::wstring value;
};

enum class IniSetSpecError
{
    None,
    MissingEquals,
    InvalidName,
    EmptySectionOrKey,
    UnsafeCharacters
};

inline bool IsOptionOrInlineValue(const std::wstring& arg, const wchar_t* option)
{
    std::wstring inlinePrefix = std::wstring(option) + L"=";
    return EqualsI(arg, option) || StartsWithI(arg, inlinePrefix.c_str());
}

inline bool IsLikelySensitiveSettingName(const std::wstring& key)
{
    std::wstring lowered = ToLower(key);
    return lowered.find(L"token") != std::wstring::npos ||
        lowered.find(L"secret") != std::wstring::npos ||
        lowered.find(L"password") != std::wstring::npos ||
        lowered.find(L"cookie") != std::wstring::npos;
}

inline bool TakeCommandLineValue(int argc, wchar_t** argv, int& i, const std::wstring& option, std::wstring& value, std::wstring& error)
{
    size_t eq = option.find(L'=');
    if (eq != std::wstring::npos)
    {
        value = option.substr(eq + 1);
        return true;
    }

    if (i + 1 >= argc)
    {
        error = L"Missing value after " + option + L".";
        return false;
    }

    value = argv[++i] ? argv[i] : L"";
    return true;
}

inline bool ParseIniSetSpec(
    const std::wstring& spec,
    IniSetting& setting,
    std::wstring& error,
    IniSetSpecError* errorKind = nullptr)
{
    if (errorKind)
    {
        *errorKind = IniSetSpecError::None;
    }

    size_t equals = spec.find(L'=');
    if (equals == std::wstring::npos)
    {
        if (errorKind)
        {
            *errorKind = IniSetSpecError::MissingEquals;
        }
        error = L"--set expects Section.Key=Value.";
        return false;
    }

    std::wstring name = Trim(spec.substr(0, equals));
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size())
    {
        if (errorKind)
        {
            *errorKind = IniSetSpecError::InvalidName;
        }
        error = L"--set expects Section.Key=Value.";
        return false;
    }

    setting.section = Trim(name.substr(0, dot));
    setting.key = Trim(name.substr(dot + 1));
    setting.value = spec.substr(equals + 1);
    if (setting.section.empty() || setting.key.empty())
    {
        if (errorKind)
        {
            *errorKind = IniSetSpecError::EmptySectionOrKey;
        }
        error = L"--set expects Section.Key=Value.";
        return false;
    }
    auto hasLineBreakOrNul = [](const std::wstring& text)
    {
        return text.find_first_of(L"\r\n\0", 0, 3) != std::wstring::npos;
    };
    if (hasLineBreakOrNul(setting.section) ||
        hasLineBreakOrNul(setting.key) ||
        hasLineBreakOrNul(setting.value) ||
        setting.section.find_first_of(L"[]") != std::wstring::npos)
    {
        if (errorKind)
        {
            *errorKind = IniSetSpecError::UnsafeCharacters;
        }
        error = L"--set section, key, and value must stay on one INI line; section names cannot contain brackets.";
        return false;
    }
    return true;
}


inline bool ParseIntValue(const std::wstring& raw, int& value)
{
    std::wstring trimmed = Trim(raw);
    if (trimmed.empty())
    {
        return false;
    }

    errno = 0;
    wchar_t* end = nullptr;
    long long parsed = wcstoll(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
    {
        return false;
    }

    while (end != nullptr && *end != L'\0' && iswspace(*end) != 0)
    {
        ++end;
    }
    if (end != nullptr && *end != L'\0')
    {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

inline bool ParseIntValueInRange(const std::wstring& raw, int minValue, int maxValue, int& value)
{
    int parsed = 0;
    if (!ParseIntValue(raw, parsed) || parsed < minValue || parsed > maxValue)
    {
        return false;
    }

    value = parsed;
    return true;
}

inline bool ParseBoolValue(const std::wstring& raw, bool& value)
{
    std::wstring v = ToLower(Trim(raw));
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"on" || v == L"enable" || v == L"enabled")
    {
        value = true;
        return true;
    }
    if (v == L"0" || v == L"false" || v == L"no" || v == L"off" || v == L"disable" || v == L"disabled")
    {
        value = false;
        return true;
    }
    return false;
}
}

#endif

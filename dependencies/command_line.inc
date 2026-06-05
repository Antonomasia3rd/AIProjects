#ifndef AIP_DEPENDENCIES_COMMAND_LINE_INC
#define AIP_DEPENDENCIES_COMMAND_LINE_INC

namespace aip
{
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
    EmptySectionOrKey
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

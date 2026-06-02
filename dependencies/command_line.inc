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

    value = argv[++i];
    return true;
}

inline bool ParseIniSetSpec(const std::wstring& spec, IniSetting& setting, std::wstring& error)
{
    size_t equals = spec.find(L'=');
    if (equals == std::wstring::npos)
    {
        error = L"--set expects Section.Key=Value.";
        return false;
    }

    std::wstring name = spec.substr(0, equals);
    size_t dot = name.find(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size())
    {
        error = L"--set expects Section.Key=Value.";
        return false;
    }

    setting.section = name.substr(0, dot);
    setting.key = name.substr(dot + 1);
    setting.value = spec.substr(equals + 1);
    return true;
}

inline bool ParseBoolValue(const std::wstring& raw, bool& value)
{
    std::wstring v = ToLower(Trim(raw));
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"on")
    {
        value = true;
        return true;
    }
    if (v == L"0" || v == L"false" || v == L"no" || v == L"off")
    {
        value = false;
        return true;
    }
    return false;
}
}

#endif

#ifndef AIP_DEPENDENCIES_TRAY_INC
#define AIP_DEPENDENCIES_TRAY_INC

namespace aip
{
inline void AppendCheckedMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked)
{
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED), id, text.c_str());
}

inline void AppendMenuSeparator(HMENU menu)
{
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}

inline void ShowTrayBalloon(NOTIFYICONDATAW& nid, const std::wstring& title, const std::wstring& message, DWORD icon)
{
    nid.uFlags = NIF_INFO;
    wcsncpy_s(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, ARRAYSIZE(nid.szInfo), message.c_str(), _TRUNCATE);
    nid.dwInfoFlags = icon;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
}

#endif

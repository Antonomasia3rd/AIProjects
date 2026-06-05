#ifndef AIP_DEPENDENCIES_TRAY_INC
#define AIP_DEPENDENCIES_TRAY_INC

namespace aip
{
inline std::wstring TrayMenuTitle(const std::wstring& text)
{
    std::wstring title = Trim(text);
    while (!title.empty() && (title.back() == L':' || iswspace(title.back())))
    {
        title.pop_back();
    }
    return title.empty() ? text : title;
}

inline bool AppendTrayMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked = false, bool enabled = true)
{
    UINT flags = MF_STRING;
    if (checked)
    {
        flags |= MF_CHECKED;
    }
    if (!enabled)
    {
        flags |= MF_DISABLED;
    }
    return AppendMenuW(menu, flags, id, text.c_str()) != FALSE;
}

inline void AppendCheckedMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked)
{
    AppendTrayMenuItem(menu, id, text, checked);
}

inline void AppendMenuSeparator(HMENU menu)
{
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}

inline HMENU CreateTrayPopupMenu()
{
    return CreatePopupMenu();
}

inline HMENU BeginTrayNestedMenu(HMENU fallback)
{
    HMENU submenu = CreatePopupMenu();
    return submenu != nullptr ? submenu : fallback;
}

inline bool EndTrayNestedMenu(HMENU parent, HMENU child, const std::wstring& title)
{
    if (parent == nullptr || child == nullptr)
    {
        return false;
    }
    if (child == parent)
    {
        AppendMenuSeparator(parent);
        return false;
    }

    std::wstring popupTitle = TrayMenuTitle(title);
    if (!AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(child), popupTitle.c_str()))
    {
        DWORD error = GetLastError();
        DestroyMenu(child);
        SetLastError(error);
        return false;
    }
    return true;
}

inline void TrackPostAndDestroyTrayMenu(HMENU menu, HWND hwnd, const POINT& pt, UINT flags)
{
    TrackPopupMenu(menu, flags, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
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

#ifndef AIP_DEPENDENCIES_TRAY_INC
#define AIP_DEPENDENCIES_TRAY_INC

namespace aip
{
inline std::wstring NormalizeTrayTooltip(
    const std::wstring& tooltip,
    const std::wstring& fallbackTooltip)
{
    // Shell tooltips are NUL-terminated even when their source is a string.
    // Embedded NULs from dynamic status text must not hide the remaining text.
    auto visibleText = [](std::wstring text)
    {
        std::replace(text.begin(), text.end(), L'\0', L' ');
        return Trim(text);
    };
    std::wstring normalized = visibleText(tooltip);
    if (normalized.empty())
    {
        normalized = visibleText(fallbackTooltip);
    }
    if (normalized.empty())
    {
        normalized = L"Application";
    }
    constexpr size_t tooltipLimit = 127; // NOTIFYICONDATAW::szTip minus NUL.
    if (normalized.size() > tooltipLimit)
    {
        size_t length = tooltipLimit;
        if (normalized[length - 1] >= 0xD800 && normalized[length - 1] <= 0xDBFF &&
            normalized[length] >= 0xDC00 && normalized[length] <= 0xDFFF)
        {
            --length;
        }
        normalized.resize(length);
    }
    return normalized;
}

inline bool RegisterTrayIcon(
    NOTIFYICONDATAW& nid,
    HWND window,
    UINT iconId,
    UINT callbackMessage,
    HICON icon,
    const std::wstring& tooltip,
    const std::wstring& fallbackTooltip,
    bool useVersion4,
    DWORD* addError = nullptr,
    DWORD* versionError = nullptr)
{
    if (addError != nullptr)
    {
        *addError = ERROR_SUCCESS;
    }
    if (versionError != nullptr)
    {
        *versionError = ERROR_SUCCESS;
    }
    if (window == nullptr || icon == nullptr)
    {
        if (addError != nullptr)
        {
            *addError = window == nullptr ? ERROR_INVALID_WINDOW_HANDLE : ERROR_INVALID_HANDLE;
        }
        return false;
    }

    NOTIFYICONDATAW candidate = {};
    candidate.cbSize = sizeof(candidate);
    candidate.hWnd = window;
    candidate.uID = iconId;
    candidate.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    if (useVersion4)
    {
        // Version 4 suppresses the standard hover tooltip unless NIF_SHOWTIP is
        // present. Keep this coupled to version negotiation so callers cannot
        // accidentally create the silent-hover configuration.
        candidate.uFlags |= NIF_SHOWTIP;
    }
    candidate.uCallbackMessage = callbackMessage;
    candidate.hIcon = icon;
    std::wstring normalizedTooltip = NormalizeTrayTooltip(
        tooltip,
        fallbackTooltip);
    wcsncpy_s(
        candidate.szTip,
        ARRAYSIZE(candidate.szTip),
        normalizedTooltip.c_str(),
        _TRUNCATE);

    SetLastError(ERROR_SUCCESS);
    if (!Shell_NotifyIconW(NIM_ADD, &candidate))
    {
        if (addError != nullptr)
        {
            DWORD currentError = GetLastError();
            *addError = currentError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : currentError;
        }
        return false;
    }

    if (useVersion4)
    {
        candidate.uVersion = NOTIFYICON_VERSION_4;
        SetLastError(ERROR_SUCCESS);
        if (!Shell_NotifyIconW(NIM_SETVERSION, &candidate) && versionError != nullptr)
        {
            DWORD currentError = GetLastError();
            *versionError = currentError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : currentError;
        }
    }

    nid = candidate;
    return true;
}

inline bool ModifyTrayIconTooltip(
    NOTIFYICONDATAW& nid,
    const std::wstring& tooltip,
    const std::wstring& fallbackTooltip,
    bool useVersion4,
    DWORD* error = nullptr)
{
    if (error != nullptr)
    {
        *error = ERROR_SUCCESS;
    }
    if (nid.hWnd == nullptr)
    {
        if (error != nullptr)
        {
            *error = ERROR_INVALID_WINDOW_HANDLE;
        }
        return false;
    }

    NOTIFYICONDATAW updated = nid;
    updated.uFlags = NIF_TIP | (useVersion4 ? NIF_SHOWTIP : 0);
    std::wstring normalizedTooltip = NormalizeTrayTooltip(
        tooltip,
        fallbackTooltip);
    wcsncpy_s(
        updated.szTip,
        ARRAYSIZE(updated.szTip),
        normalizedTooltip.c_str(),
        _TRUNCATE);
    SetLastError(ERROR_SUCCESS);
    if (!Shell_NotifyIconW(NIM_MODIFY, &updated))
    {
        if (error != nullptr)
        {
            DWORD currentError = GetLastError();
            *error = currentError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : currentError;
        }
        return false;
    }

    wcsncpy_s(
        nid.szTip,
        ARRAYSIZE(nid.szTip),
        normalizedTooltip.c_str(),
        _TRUNCATE);
    return true;
}

inline bool UnregisterTrayIcon(const NOTIFYICONDATAW& nid, DWORD* error = nullptr)
{
    if (error != nullptr)
    {
        *error = ERROR_SUCCESS;
    }
    if (nid.hWnd == nullptr)
    {
        return true;
    }

    NOTIFYICONDATAW copy = nid;
    SetLastError(ERROR_SUCCESS);
    if (!Shell_NotifyIconW(NIM_DELETE, &copy))
    {
        if (error != nullptr)
        {
            DWORD currentError = GetLastError();
            *error = currentError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : currentError;
        }
        return false;
    }
    return true;
}

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

inline bool AppendBaselineTrayMenuHeader(
    HMENU menu,
    UINT dropdownCommand,
    const std::wstring& dropdownText,
    bool showAsDropdown,
    UINT primaryCommand,
    const std::wstring& primaryText,
    const std::wstring& versionText)
{
    bool ok = AppendTrayMenuItem(
        menu,
        dropdownCommand,
        dropdownText,
        showAsDropdown);
    ok = AppendTrayMenuItem(menu, primaryCommand, primaryText) && ok;
    ok = AppendTrayMenuItem(menu, 0, versionText, false, false) && ok;
    if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr))
    {
        ok = false;
    }
    return ok;
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

inline bool ShowTrayBalloon(
    const NOTIFYICONDATAW& nid,
    const std::wstring& title,
    const std::wstring& message,
    DWORD icon,
    UINT timeoutMs = 0,
    DWORD* error = nullptr)
{
    if (error != nullptr)
    {
        *error = ERROR_SUCCESS;
    }
    if (nid.hWnd == nullptr)
    {
        if (error != nullptr)
        {
            *error = ERROR_INVALID_WINDOW_HANDLE;
        }
        return false;
    }

    NOTIFYICONDATAW notification = nid;
    notification.uFlags = NIF_INFO;
    wcsncpy_s(
        notification.szInfoTitle,
        ARRAYSIZE(notification.szInfoTitle),
        title.c_str(),
        _TRUNCATE);
    wcsncpy_s(
        notification.szInfo,
        ARRAYSIZE(notification.szInfo),
        message.c_str(),
        _TRUNCATE);
    notification.dwInfoFlags = icon;
    notification.uTimeout = timeoutMs;
    SetLastError(ERROR_SUCCESS);
    if (!Shell_NotifyIconW(NIM_MODIFY, &notification))
    {
        DWORD currentError = GetLastError();
        currentError = currentError == ERROR_SUCCESS
            ? ERROR_GEN_FAILURE
            : currentError;
        SetLastError(currentError);
        if (error != nullptr)
        {
            *error = currentError;
        }
        return false;
    }
    return true;
}
}

#endif

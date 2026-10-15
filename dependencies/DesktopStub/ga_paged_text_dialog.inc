// Bounded native dialog for cached information. No source refresh or file access.
static constexpr int PAGED_TEXT_BODY = 1101, PAGED_TEXT_PREVIOUS = 1102,
    PAGED_TEXT_NEXT = 1103, PAGED_TEXT_POSITION = 1104;
struct PagedTextDialogState {
    std::vector<std::wstring> pages;
    std::size_t index = 0;
    UINT dpi = 96;
};
static UINT PagedTextDialogDpi(HWND window)
{
    using GetDpiFn = UINT(WINAPI*)(HWND);
#pragma warning(suppress: 4191)
    const auto get = reinterpret_cast<GetDpiFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = get ? get(window) : 0;
    return dpi ? dpi : static_cast<UINT>(MulDiv(96, CurrentDpiPercent(), 100));
}
static RECT PagedTextDialogBounds(const RECT& work, UINT dpi)
{
    const int width = (std::max)(1, (std::min)(MulDiv(720, dpi, 96), static_cast<int>(work.right - work.left - 32)));
    const int height = (std::max)(1, (std::min)(MulDiv(560, dpi, 96), static_cast<int>(work.bottom - work.top - 32)));
    const int left = work.left + ((work.right - work.left) - width) / 2;
    const int top = work.top + ((work.bottom - work.top) - height) / 2;
    return {left, top, left + width, top + height};
}
static void LayoutPagedTextDialog(HWND dialog, UINT dpi)
{
    RECT r{}; GetClientRect(dialog, &r);
    const int clientWidth = static_cast<int>(r.right), clientHeight = static_cast<int>(r.bottom);
    const int gap = (std::max)(1, (std::min)(MulDiv(10, dpi, 96), clientWidth / 12));
    const int buttonW = (std::max)(1, (std::min)(MulDiv(96, dpi, 96), (clientWidth - gap * 5) / 4));
    const int buttonH = MulDiv(28, dpi, 96);
    const int footer = clientHeight - gap - buttonH;
    MoveWindow(GetDlgItem(dialog, PAGED_TEXT_BODY), gap, gap,
        (std::max)(1, clientWidth - gap * 2), (std::max)(1, footer - gap * 2), TRUE);
    MoveWindow(GetDlgItem(dialog, PAGED_TEXT_PREVIOUS), gap, footer, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(dialog, PAGED_TEXT_NEXT), gap * 2 + buttonW, footer, buttonW, buttonH, TRUE);
    MoveWindow(GetDlgItem(dialog, PAGED_TEXT_POSITION), gap * 3 + buttonW * 2, footer + MulDiv(5, dpi, 96),
        (std::max)(1, clientWidth - buttonW * 3 - gap * 5), buttonH, TRUE);
    MoveWindow(GetDlgItem(dialog, IDCANCEL), clientWidth - gap - buttonW, footer, buttonW, buttonH, TRUE);
}
static void UpdatePagedTextDialog(HWND dialog, const PagedTextDialogState& state)
{
    SetWindowTextW(GetDlgItem(dialog, PAGED_TEXT_BODY), state.pages[state.index].c_str());
    SendDlgItemMessageW(dialog, PAGED_TEXT_BODY, EM_SETSEL, 0, 0);
    SendDlgItemMessageW(dialog, PAGED_TEXT_BODY, EM_SCROLLCARET, 0, 0);
    const auto position = std::to_wstring(state.index + 1) + L" / " + std::to_wstring(state.pages.size());
    SetWindowTextW(GetDlgItem(dialog, PAGED_TEXT_POSITION), position.c_str());
    EnableWindow(GetDlgItem(dialog, PAGED_TEXT_PREVIOUS), state.index != 0);
    EnableWindow(GetDlgItem(dialog, PAGED_TEXT_NEXT), state.index + 1 < state.pages.size());
}
static INT_PTR CALLBACK PagedTextDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<PagedTextDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<PagedTextDialogState*>(lParam);
        if (!state) return FALSE;
        if (state->pages.empty()) state->pages.emplace_back();
        state->index = (std::min)(state->index, state->pages.size() - 1);
        state->dpi = PagedTextDialogDpi(dialog);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        const auto font = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
        const auto add = [&](const wchar_t* type, const std::wstring& text, int id, DWORD style, DWORD extended = 0) {
            HWND control = CreateWindowExW(extended, type, text.c_str(), WS_CHILD | WS_VISIBLE | style,
                0, 0, 1, 1, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };
        HWND body = add(L"EDIT", L"", PAGED_TEXT_BODY, WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, WS_EX_CLIENTEDGE);
        add(L"BUTTON", IniReadS(L"Strings", L"TileStatusPrevious", L"Previous"), PAGED_TEXT_PREVIOUS, WS_TABSTOP | BS_PUSHBUTTON);
        add(L"BUTTON", IniReadS(L"Strings", L"TileStatusNext", L"Next"), PAGED_TEXT_NEXT, WS_TABSTOP | BS_PUSHBUTTON);
        add(L"STATIC", L"", PAGED_TEXT_POSITION, SS_CENTER | SS_NOPREFIX);
        add(L"BUTTON", IniReadS(L"Strings", L"TileStatusClose", L"Close"), IDCANCEL, WS_TABSTOP | BS_DEFPUSHBUTTON);
        SendMessageW(body, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        MONITORINFO info{sizeof(info)}; RECT work{};
        if (GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &info)) work = info.rcWork;
        else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const auto bounds = PagedTextDialogBounds(work, state->dpi);
        SetWindowPos(dialog, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutPagedTextDialog(dialog, state->dpi); UpdatePagedTextDialog(dialog, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_SIZE) { if (wParam != SIZE_MINIMIZED) LayoutPagedTextDialog(dialog, state->dpi); return TRUE; }
    if (message == WM_DPICHANGED) {
        state->dpi = HIWORD(wParam);
        const auto* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(dialog, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutPagedTextDialog(dialog, state->dpi); return TRUE;
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &info)) {
            limits->ptMinTrackSize.x = (std::min)(MulDiv(400, state->dpi, 96), static_cast<int>(info.rcWork.right-info.rcWork.left));
            limits->ptMinTrackSize.y = (std::min)(MulDiv(240, state->dpi, 96), static_cast<int>(info.rcWork.bottom-info.rcWork.top));
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const auto id = LOWORD(wParam);
        if (id == IDCANCEL || id == IDOK) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (id == PAGED_TEXT_PREVIOUS && state->index) --state->index;
        else if (id == PAGED_TEXT_NEXT && state->index + 1 < state->pages.size()) ++state->index;
        else return FALSE;
        UpdatePagedTextDialog(dialog, *state); return TRUE;
    }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}
static std::vector<BYTE> PagedTextDialogTemplate(const std::wstring& title)
{
    const auto translated = UiLiteralText(title, true);
    std::vector<BYTE> bytes(1024 + translated.size() * sizeof(wchar_t));
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    dialog->style = WS_POPUP | WS_SYSMENU | WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | DS_MODALFRAME | DS_SETFONT | DS_CENTER;
    dialog->cx = 360; dialog->cy = 260;
    auto* p = reinterpret_cast<WORD*>(dialog + 1); *p++ = 0; *p++ = 0;
    AppendDialogTemplateString(p, translated); *p++ = 9; AppendDialogTemplateString(p, L"Segoe UI");
    return bytes;
}
static INT_PTR ShowPagedTextDialog(HWND owner, const std::wstring& title, std::vector<std::wstring> pages)
{
    PagedTextDialogState state{std::move(pages)};
    const auto bytes = PagedTextDialogTemplate(title);
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(bytes.data()), owner,
        PagedTextDialogProc, reinterpret_cast<LPARAM>(&state));
}

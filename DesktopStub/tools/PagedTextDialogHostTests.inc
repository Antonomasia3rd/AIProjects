// Construct only a hidden modeless dialog: no UI automation or modal prompts.
static void TestPagedTextDialogHost()
{
    for (const UINT dpi : {96u, 144u, 192u, 384u}) {
        const RECT work{-800, 100, -400, 400};
        const auto bounds = PagedTextDialogBounds(work, dpi);
        Check(bounds.left >= work.left && bounds.top >= work.top && bounds.right <= work.right &&
            bounds.bottom <= work.bottom && bounds.right > bounds.left && bounds.bottom > bounds.top,
            "paged dialog stays inside a small monitor work area at each tested DPI");
    }
    PagedTextDialogState state{{L"First tile", std::wstring(12000, L'X') + L"\r\nLast detail", L"Last tile"}};
    const auto bytes = PagedTextDialogTemplate(L"Tiles");
    HWND dialog = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(bytes.data()),
        nullptr, PagedTextDialogProc, reinterpret_cast<LPARAM>(&state));
    Check(dialog != nullptr, "paged information dialog creates with native controls");
    if (!dialog) return;
    Check(!IsWindowVisible(dialog), "dialog fixture stays hidden and does not interrupt the user");
    Check(!IsWindowEnabled(GetDlgItem(dialog, PAGED_TEXT_PREVIOUS)) && IsWindowEnabled(GetDlgItem(dialog, PAGED_TEXT_NEXT)),
        "first page disables Previous and enables Next");
    const auto style = GetWindowLongPtrW(GetDlgItem(dialog, PAGED_TEXT_BODY), GWL_STYLE);
    Check((style & (ES_MULTILINE | ES_READONLY | WS_VSCROLL)) == (ES_MULTILINE | ES_READONLY | WS_VSCROLL) && !(style & ES_AUTOHSCROLL),
        "page details wrap and scroll vertically in a read-only copyable edit control");
    SendMessageW(dialog, WM_COMMAND, PAGED_TEXT_NEXT, 0);
    Check(state.index == 1 && GetWindowTextLengthW(GetDlgItem(dialog, PAGED_TEXT_BODY)) == static_cast<int>(state.pages[1].size()),
        "Next shows an untruncated long page");
    SendMessageW(dialog, WM_COMMAND, PAGED_TEXT_NEXT, 0);
    Check(state.index == 2 && IsWindowEnabled(GetDlgItem(dialog, PAGED_TEXT_PREVIOUS)) && !IsWindowEnabled(GetDlgItem(dialog, PAGED_TEXT_NEXT)),
        "last page enables Previous and disables Next");
    SendMessageW(dialog, WM_COMMAND, PAGED_TEXT_NEXT, 0);
    Check(state.index == 2, "Next cannot move beyond the final page");
    SendMessageW(dialog, WM_COMMAND, PAGED_TEXT_PREVIOUS, 0);
    SendMessageW(dialog, WM_COMMAND, PAGED_TEXT_PREVIOUS, 0);
    SendMessageW(dialog, WM_COMMAND, PAGED_TEXT_PREVIOUS, 0);
    Check(state.index == 0 && GetWindowTextLengthW(GetDlgItem(dialog, PAGED_TEXT_BODY)) == 10,
        "Previous returns to the first page without underflow");
    SetWindowPos(dialog, nullptr, 0, 0, 350, 260, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{}, edit{}, close{}; GetClientRect(dialog, &client);
    GetWindowRect(GetDlgItem(dialog, PAGED_TEXT_BODY), &edit); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&edit), 2);
    GetWindowRect(GetDlgItem(dialog, IDCANCEL), &close); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&close), 2);
    Check(edit.left >= 0 && edit.right <= client.right && edit.bottom < close.top && close.right <= client.right && close.bottom <= client.bottom,
        "resizing keeps scrollable text and the Close button inside the client area");
    Check(DestroyWindow(dialog) != FALSE, "hidden paged dialog fixture destroys all its native controls");
}

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
    const RECT work{-1200,0,800,1200},owner{-1000,100,-400,500};
    const auto shortBounds=PagedTextDialogBounds(work,96,160,&owner),longBounds=PagedTextDialogBounds(work,96,5000,&owner);
    Check(shortBounds.right-shortBounds.left==520&&shortBounds.bottom-shortBounds.top==160&&
        shortBounds.left+shortBounds.right==owner.left+owner.right&&shortBounds.top+shortBounds.bottom==owner.top+owner.bottom,
        "compact message bounds center on the owner instead of a fixed screen corner");
    Check(longBounds.bottom-longBounds.top==560&&longBounds.top>=work.top&&longBounds.bottom<=work.bottom,
        "long information pages have a bounded scrollable message height rather than growing beyond the monitor");
    Check(PagedTextDialogIcon(MB_ICONINFORMATION)==IDI_INFORMATION&&PagedTextDialogIcon(MB_ICONWARNING)==IDI_WARNING&&
        PagedTextDialogIcon(MB_ICONERROR)==IDI_ERROR&&PagedTextDialogIcon(MB_ICONQUESTION)==IDI_QUESTION,
        "message dialog selects standard native information warning error and question icons");
    PagedTextDialogState state{{L"First tile", std::wstring(12000, L'X') + L"\r\nLast detail", L"Last tile"}};
    const auto bytes = PagedTextDialogTemplate(L"Tiles");
    HWND dialog = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(bytes.data()),
        nullptr, PagedTextDialogProc, reinterpret_cast<LPARAM>(&state));
    Check(dialog != nullptr, "paged information dialog creates with native controls");
    if (!dialog) return;
    Check(!IsWindowVisible(dialog), "dialog fixture stays hidden and does not interrupt the user");
    RECT compact{};GetWindowRect(dialog,&compact);
    Check(compact.bottom-compact.top<MulDiv(260,state.dpi,96)&&!(GetWindowLongPtrW(dialog,GWL_STYLE)&WS_MAXIMIZEBOX),
        "a short information page starts compact and omits the document-like maximize affordance");
    Check(!IsWindowEnabled(GetDlgItem(dialog, PAGED_TEXT_PREVIOUS)) && IsWindowEnabled(GetDlgItem(dialog, PAGED_TEXT_NEXT)),
        "first page disables Previous and enables Next");
    const auto style = GetWindowLongPtrW(GetDlgItem(dialog, PAGED_TEXT_BODY), GWL_STYLE);
    Check((style&(ES_MULTILINE|ES_READONLY|WS_TABSTOP))==(ES_MULTILINE|ES_READONLY|WS_TABSTOP)&&!(style&(ES_AUTOHSCROLL|WS_BORDER|WS_HSCROLL))&&
        !(GetWindowLongPtrW(GetDlgItem(dialog,PAGED_TEXT_BODY),GWL_EXSTYLE)&WS_EX_CLIENTEDGE),
        "plain information text wraps and supports read-only keyboard selection without an editable-field border");
    Check((GetWindowLongPtrW(GetDlgItem(dialog,PAGED_TEXT_ICON),GWL_STYLE)&SS_TYPEMASK)==SS_ICON&&
        SendDlgItemMessageW(dialog,PAGED_TEXT_ICON,STM_GETICON,0,0)!=0&&state.font!=nullptr,
        "message-like layout uses a native information icon and DPI-scaled system message font");
    SendDlgItemMessageW(dialog,PAGED_TEXT_BODY,EM_SETSEL,0,5);DWORD start=0,end=0;
    SendDlgItemMessageW(dialog,PAGED_TEXT_BODY,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    Check(start==0&&end==5,"information text remains selectable for normal copy shortcuts without exercising the clipboard");
    const auto previousFocus=GetFocus();
    SendMessageW(dialog, WM_COMMAND, PAGED_TEXT_NEXT, 0);
    Check(state.index == 1 && GetWindowTextLengthW(GetDlgItem(dialog, PAGED_TEXT_BODY)) == static_cast<int>(state.pages[1].size()),
        "Next shows an untruncated long page");
    Check((GetWindowLongPtrW(GetDlgItem(dialog,PAGED_TEXT_BODY),GWL_STYLE)&WS_VSCROLL)!=0&&GetFocus()==previousFocus,
        "long pages enable bounded vertical scrolling without moving keyboard focus on page changes");
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
    RECT compactAgain{};GetWindowRect(dialog,&compactAgain);
    Check(compactAgain.bottom-compactAgain.top==compact.bottom-compact.top,
        "returning to a short page restores compact message height instead of retaining the long-page document size");
    SetWindowPos(dialog, nullptr, 0, 0, 350, 260, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{}, edit{}, close{}; GetClientRect(dialog, &client);
    GetWindowRect(GetDlgItem(dialog, PAGED_TEXT_BODY), &edit); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&edit), 2);
    GetWindowRect(GetDlgItem(dialog, IDCANCEL), &close); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&close), 2);
    Check(edit.left >= 0 && edit.right <= client.right && edit.bottom < close.top && close.right <= client.right && close.bottom <= client.bottom,
        "resizing keeps scrollable text and the Close button inside the client area");
    Check(DestroyWindow(dialog) != FALSE, "hidden paged dialog fixture destroys all its native controls");
    Check(state.font==nullptr,"destroying the information dialog releases its owned message font");
    PagedTextDialogState single{{L"One short status"}};const auto one=PagedTextDialogTemplate(L"Information");
    HWND oneDialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(one.data()),
        nullptr,PagedTextDialogProc,reinterpret_cast<LPARAM>(&single));
    Check(oneDialog!=nullptr,"single-page message fixture creates without showing a modal prompt");
    if(oneDialog){
        Check(!(GetWindowLongPtrW(GetDlgItem(oneDialog,PAGED_TEXT_PREVIOUS),GWL_STYLE)&WS_VISIBLE)&&
            !(GetWindowLongPtrW(GetDlgItem(oneDialog,PAGED_TEXT_NEXT),GWL_STYLE)&WS_VISIBLE)&&
            !(GetWindowLongPtrW(GetDlgItem(oneDialog,PAGED_TEXT_POSITION),GWL_STYLE)&WS_VISIBLE),
            "single-page information hides navigation and page counters that have no action");
        DestroyWindow(oneDialog);
    }
}

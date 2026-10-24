// Cached information presented like a native message dialog. The body remains
// selectable/copyable but has no editable-field border. No source/file refresh.
static constexpr int PAGED_TEXT_BODY=1101,PAGED_TEXT_PREVIOUS=1102,
    PAGED_TEXT_NEXT=1103,PAGED_TEXT_POSITION=1104,PAGED_TEXT_ICON=1105;
struct PagedTextDialogState {
    std::vector<std::wstring> pages;
    std::size_t index=0;
    UINT dpi=96,icon=MB_ICONINFORMATION;
    HFONT font=nullptr;
    bool manuallySized=false,interactive=false;
    int footerTop=0;
    ~PagedTextDialogState(){if(font)DeleteObject(font);}
};
static UINT PagedTextDialogDpi(HWND window)
{
    using GetDpiFn=UINT(WINAPI*)(HWND);
#pragma warning(suppress: 4191)
    const auto get=reinterpret_cast<GetDpiFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    const UINT dpi=get?get(window):0;
    return dpi?dpi:static_cast<UINT>(MulDiv(96,CurrentDpiPercent(),100));
}
static RECT PagedTextDialogBounds(const RECT& work,UINT dpi,int preferredHeight=0,const RECT* owner=nullptr)
{
    const int workWidth=(std::max)(1,static_cast<int>(work.right-work.left)),workHeight=(std::max)(1,static_cast<int>(work.bottom-work.top));
    const int edgeX=(std::min)(MulDiv(16,dpi,96),workWidth/8),edgeY=(std::min)(MulDiv(16,dpi,96),workHeight/8);
    const int width=(std::max)(1,(std::min)(MulDiv(520,dpi,96),workWidth-edgeX*2));
    const int wanted=preferredHeight>0?preferredHeight:MulDiv(180,dpi,96);
    const int height=(std::max)(1,(std::min)((std::min)(wanted,MulDiv(560,dpi,96)),workHeight-edgeY*2));
    const RECT anchor=owner?*owner:work;
    const int left=(std::max)(static_cast<int>(work.left)+edgeX,(std::min)(static_cast<int>(work.right)-edgeX-width,static_cast<int>(anchor.left+(anchor.right-anchor.left-width)/2)));
    const int top=(std::max)(static_cast<int>(work.top)+edgeY,(std::min)(static_cast<int>(work.bottom)-edgeY-height,static_cast<int>(anchor.top+(anchor.bottom-anchor.top-height)/2)));
    return {left,top,left+width,top+height};
}
static void PagedTextDialogFont(HWND dialog,PagedTextDialogState& state)
{
    NONCLIENTMETRICSW metrics{};metrics.cbSize=sizeof(metrics);bool loaded=false;
    using MetricsForDpi=BOOL(WINAPI*)(UINT,UINT,PVOID,UINT,UINT);
#pragma warning(suppress: 4191)
    const auto forDpi=reinterpret_cast<MetricsForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SystemParametersInfoForDpi"));
    if(forDpi)loaded=forDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,state.dpi)!=FALSE;
    if(!loaded&&SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0)){
        HDC screen=GetDC(nullptr);const int systemDpi=screen?GetDeviceCaps(screen,LOGPIXELSY):96;if(screen)ReleaseDC(nullptr,screen);
        metrics.lfMessageFont.lfHeight=MulDiv(metrics.lfMessageFont.lfHeight,state.dpi,(std::max)(1,systemDpi));loaded=true;
    }
    if(!loaded){metrics.lfMessageFont.lfHeight=-MulDiv(9,state.dpi,72);wcscpy_s(metrics.lfMessageFont.lfFaceName,L"Segoe UI");}
    HFONT font=CreateFontIndirectW(&metrics.lfMessageFont);
    if(!font)return;
    const auto previous=state.font;state.font=font;SendMessageW(dialog,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    for(const int id:{PAGED_TEXT_BODY,PAGED_TEXT_PREVIOUS,PAGED_TEXT_NEXT,PAGED_TEXT_POSITION,IDCANCEL})
        SendDlgItemMessageW(dialog,id,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    if(previous)DeleteObject(previous);
}
static const wchar_t* PagedTextDialogIcon(UINT icon)
{
    switch(icon&MB_ICONMASK){case MB_ICONWARNING:return IDI_WARNING;case MB_ICONERROR:return IDI_ERROR;
    case MB_ICONQUESTION:return IDI_QUESTION;default:return IDI_INFORMATION;}
}
static void LayoutPagedTextDialog(HWND dialog,UINT dpi)
{
    auto* state=reinterpret_cast<PagedTextDialogState*>(GetWindowLongPtrW(dialog,DWLP_USER));
    RECT r{};GetClientRect(dialog,&r);const int width=(std::max)(1,static_cast<int>(r.right)),height=(std::max)(1,static_cast<int>(r.bottom));
    const int gap=(std::max)(1,(std::min)(MulDiv(16,dpi,96),(std::min)(width/14,height/10)));
    const int buttonH=(std::max)(1,(std::min)(MulDiv(26,dpi,96),height/4));
    const int buttonW=(std::max)(1,(std::min)(MulDiv(86,dpi,96),(width-gap*5)/4));
    const int footerTop=(std::max)(0,height-buttonH-gap*2),footer=footerTop+gap;
    if(state)state->footerTop=footerTop;
    const int bodyHeight=(std::max)(1,footerTop-gap*2);
    const int iconSize=(std::max)(1,(std::min)(MulDiv(32,dpi,96),(std::min)(bodyHeight,width/6)));
    const int bodyLeft=gap*2+iconSize,bodyWidth=(std::max)(1,width-bodyLeft-gap);
    MoveWindow(GetDlgItem(dialog,PAGED_TEXT_ICON),gap,gap,iconSize,iconSize,TRUE);
    MoveWindow(GetDlgItem(dialog,PAGED_TEXT_BODY),bodyLeft,gap,bodyWidth,bodyHeight,TRUE);
    MoveWindow(GetDlgItem(dialog,PAGED_TEXT_PREVIOUS),gap,footer,buttonW,buttonH,TRUE);
    MoveWindow(GetDlgItem(dialog,PAGED_TEXT_NEXT),gap*2+buttonW,footer,buttonW,buttonH,TRUE);
    MoveWindow(GetDlgItem(dialog,PAGED_TEXT_POSITION),gap*3+buttonW*2,footer+MulDiv(5,dpi,96),
        (std::max)(1,width-buttonW*3-gap*5),(std::max)(1,buttonH-MulDiv(5,dpi,96)),TRUE);
    MoveWindow(GetDlgItem(dialog,IDCANCEL),width-gap-buttonW,footer,buttonW,buttonH,TRUE);
    HWND body=GetDlgItem(dialog,PAGED_TEXT_BODY);HDC dc=GetDC(body);TEXTMETRICW text{};
    if(dc){const auto font=reinterpret_cast<HFONT>(SendMessageW(body,WM_GETFONT,0,0));const auto old=font?SelectObject(dc,font):nullptr;
        GetTextMetricsW(dc,&text);if(old)SelectObject(dc,old);ReleaseDC(body,dc);}
    const auto lines=SendMessageW(body,EM_GETLINECOUNT,0,0);
    const bool overflow=static_cast<long long>((std::max)(LRESULT(1),lines))*(std::max)(1L,text.tmHeight)>bodyHeight;
    ShowScrollBar(body,SB_VERT,overflow);
    InvalidateRect(dialog,nullptr,TRUE);
}
static void FitPagedTextDialogToPage(HWND dialog,PagedTextDialogState& state)
{
    if(state.manuallySized)return;
    HWND owner=GetWindow(dialog,GW_OWNER);RECT ownerBounds{};const bool hasOwner=owner&&IsWindow(owner)&&!IsIconic(owner)&&GetWindowRect(owner,&ownerBounds);
    MONITORINFO info{sizeof(info)};RECT work{};
    if(GetMonitorInfoW(MonitorFromWindow(hasOwner?owner:dialog,MONITOR_DEFAULTTONEAREST),&info))work=info.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    auto bounds=PagedTextDialogBounds(work,state.dpi,0,hasOwner?&ownerBounds:nullptr);
    // Establish wrapped line widths before measuring; a long unbroken line is
    // measured by the edit control itself rather than guessed from characters.
    SetWindowPos(dialog,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutPagedTextDialog(dialog,state.dpi);
    HWND body=GetDlgItem(dialog,PAGED_TEXT_BODY);HDC dc=GetDC(body);TEXTMETRICW text{};
    if(dc){const auto font=reinterpret_cast<HFONT>(SendMessageW(body,WM_GETFONT,0,0));const auto old=font?SelectObject(dc,font):nullptr;
        GetTextMetricsW(dc,&text);if(old)SelectObject(dc,old);ReleaseDC(body,dc);}
    const auto lines=(std::max)(LRESULT(1),SendMessageW(body,EM_GETLINECOUNT,0,0));
    const auto measured=(std::min)(static_cast<long long>(MulDiv(460,state.dpi,96)),static_cast<long long>(lines)*(std::max)(1L,text.tmHeight)+MulDiv(4,state.dpi,96));
    RECT outer{},client{};GetWindowRect(dialog,&outer);GetClientRect(dialog,&client);
    const int nonclient=static_cast<int>((outer.bottom-outer.top)-(client.bottom-client.top));
    const int content=(std::max)(MulDiv(32,state.dpi,96),static_cast<int>(measured));
    const int height=nonclient+content+MulDiv(16*4+26,state.dpi,96);
    bounds=PagedTextDialogBounds(work,state.dpi,height,hasOwner?&ownerBounds:nullptr);
    SetWindowPos(dialog,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutPagedTextDialog(dialog,state.dpi);
}
static void UpdatePagedTextDialog(HWND dialog,PagedTextDialogState& state)
{
    HWND body=GetDlgItem(dialog,PAGED_TEXT_BODY);SetWindowTextW(body,state.pages[state.index].c_str());
    SendMessageW(body,EM_SETSEL,0,0);SendMessageW(body,EM_SCROLLCARET,0,0);
    const auto position=std::to_wstring(state.index+1)+L" / "+std::to_wstring(state.pages.size());
    SetWindowTextW(GetDlgItem(dialog,PAGED_TEXT_POSITION),position.c_str());
    const bool multiple=state.pages.size()>1;
    for(const int id:{PAGED_TEXT_PREVIOUS,PAGED_TEXT_NEXT,PAGED_TEXT_POSITION})ShowWindow(GetDlgItem(dialog,id),multiple?SW_SHOW:SW_HIDE);
    EnableWindow(GetDlgItem(dialog,PAGED_TEXT_PREVIOUS),state.index!=0);
    EnableWindow(GetDlgItem(dialog,PAGED_TEXT_NEXT),state.index+1<state.pages.size());
    FitPagedTextDialogToPage(dialog,state);LayoutPagedTextDialog(dialog,state.dpi);
}
static INT_PTR CALLBACK PagedTextDialogProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) noexcept
{
    try{
    auto* state=reinterpret_cast<PagedTextDialogState*>(GetWindowLongPtrW(dialog,DWLP_USER));
    if(message==WM_INITDIALOG){
        state=reinterpret_cast<PagedTextDialogState*>(lParam);if(!state)return FALSE;
        if(state->pages.empty())state->pages.emplace_back();state->index=(std::min)(state->index,state->pages.size()-1);
        state->dpi=PagedTextDialogDpi(dialog);SetWindowLongPtrW(dialog,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
        const auto add=[&](const wchar_t* type,const std::wstring& text,int id,DWORD style){
            return CreateWindowExW(0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);};
        HWND body=add(L"EDIT",L"",PAGED_TEXT_BODY,WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY);
        HWND icon=add(L"STATIC",L"",PAGED_TEXT_ICON,SS_ICON|SS_REALSIZECONTROL);
        add(L"BUTTON",IniReadS(L"Strings",L"TileStatusPrevious",L"Previous"),PAGED_TEXT_PREVIOUS,WS_TABSTOP|BS_PUSHBUTTON);
        add(L"BUTTON",IniReadS(L"Strings",L"TileStatusNext",L"Next"),PAGED_TEXT_NEXT,WS_TABSTOP|BS_PUSHBUTTON);
        add(L"STATIC",L"",PAGED_TEXT_POSITION,SS_CENTER|SS_NOPREFIX);
        add(L"BUTTON",IniReadS(L"Strings",L"TileStatusClose",L"Close"),IDCANCEL,WS_TABSTOP|BS_DEFPUSHBUTTON);
        if(!body||!icon){EndDialog(dialog,IDCANCEL);return FALSE;}
        SendMessageW(body,EM_SETLIMITTEXT,0x7ffffffe,0);PagedTextDialogFont(dialog,*state);
        const int iconPixels=MulDiv(32,state->dpi,96);
        SendMessageW(icon,STM_SETICON,reinterpret_cast<WPARAM>(LoadImageW(nullptr,PagedTextDialogIcon(state->icon),IMAGE_ICON,iconPixels,iconPixels,LR_SHARED)),0);
        UpdatePagedTextDialog(dialog,*state);
        if(state->interactive)SetFocus(GetDlgItem(dialog,IDCANCEL));return FALSE;
    }
    if(!state)return FALSE;
    if(message==WM_SIZE){if(wParam!=SIZE_MINIMIZED)LayoutPagedTextDialog(dialog,state->dpi);return TRUE;}
    if(message==WM_ENTERSIZEMOVE){state->manuallySized=true;return TRUE;}
    if(message==WM_DPICHANGED){
        state->dpi=HIWORD(wParam);PagedTextDialogFont(dialog,*state);
        const int pixels=MulDiv(32,state->dpi,96);
        SendDlgItemMessageW(dialog,PAGED_TEXT_ICON,STM_SETICON,reinterpret_cast<WPARAM>(LoadImageW(nullptr,PagedTextDialogIcon(state->icon),IMAGE_ICON,pixels,pixels,LR_SHARED)),0);
        const auto* bounds=reinterpret_cast<const RECT*>(lParam);
        if(bounds)SetWindowPos(dialog,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);
        FitPagedTextDialogToPage(dialog,*state);LayoutPagedTextDialog(dialog,state->dpi);return TRUE;
    }
    if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT){
        const HDC dc=reinterpret_cast<HDC>(wParam);const bool footer=GetDlgCtrlID(reinterpret_cast<HWND>(lParam))==PAGED_TEXT_POSITION;
        SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));SetBkColor(dc,GetSysColor(footer?COLOR_BTNFACE:COLOR_WINDOW));
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(footer?COLOR_BTNFACE:COLOR_WINDOW));
    }
    if(message==WM_ERASEBKGND){
        HDC dc=reinterpret_cast<HDC>(wParam);RECT client{};GetClientRect(dialog,&client);FillRect(dc,&client,GetSysColorBrush(COLOR_WINDOW));
        client.top=state->footerTop;FillRect(dc,&client,GetSysColorBrush(COLOR_BTNFACE));
        RECT edge{0,state->footerTop,client.right,state->footerTop+1};FillRect(dc,&edge,GetSysColorBrush(COLOR_3DLIGHT));return TRUE;
    }
    if(message==WM_GETMINMAXINFO){
        auto* limits=reinterpret_cast<MINMAXINFO*>(lParam);MONITORINFO info{sizeof(info)};
        if(limits&&GetMonitorInfoW(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&info)){
            limits->ptMinTrackSize.x=(std::min)(MulDiv(320,state->dpi,96),static_cast<int>(info.rcWork.right-info.rcWork.left));
            limits->ptMinTrackSize.y=(std::min)(MulDiv(140,state->dpi,96),static_cast<int>(info.rcWork.bottom-info.rcWork.top));}
        return TRUE;
    }
    if(message==WM_COMMAND){
        const auto id=LOWORD(wParam);if(id==IDCANCEL||id==IDOK){EndDialog(dialog,IDCANCEL);return TRUE;}
        if(id==PAGED_TEXT_PREVIOUS&&state->index)--state->index;
        else if(id==PAGED_TEXT_NEXT&&state->index+1<state->pages.size())++state->index;else return FALSE;
        UpdatePagedTextDialog(dialog,*state);return TRUE;
    }
    if(message==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}
    if(message==WM_NCDESTROY){if(state->font){DeleteObject(state->font);state->font=nullptr;}SetWindowLongPtrW(dialog,DWLP_USER,0);}
    return FALSE;
    }catch(...){EndDialog(dialog,IDCANCEL);return TRUE;}
}
static std::vector<BYTE> PagedTextDialogTemplate(const std::wstring& title)
{
    const auto translated=UiLiteralText(title,true);std::vector<BYTE> bytes(1024+translated.size()*sizeof(wchar_t));
    auto* dialog=reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    dialog->style=WS_POPUP|WS_SYSMENU|WS_CAPTION|WS_THICKFRAME|DS_MODALFRAME|DS_SETFONT;
    dialog->cx=260;dialog->cy=90;auto* p=reinterpret_cast<WORD*>(dialog+1);*p++=0;*p++=0;
    AppendDialogTemplateString(p,translated);*p++=9;AppendDialogTemplateString(p,L"Segoe UI");return bytes;
}
static INT_PTR ShowPagedTextDialog(HWND owner,const std::wstring& title,std::vector<std::wstring> pages,UINT icon=MB_ICONINFORMATION)
{
    PagedTextDialogState state{std::move(pages)};state.icon=icon;state.interactive=true;const auto bytes=PagedTextDialogTemplate(title);
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(bytes.data()),owner,PagedTextDialogProc,reinterpret_cast<LPARAM>(&state));
}

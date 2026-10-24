// Independent native implementation of the visible InputBox contract used by
// ExplorerPatcher. Verified against installed VBScript dialog resource 102;
// no script engine or upstream implementation is embedded or executed here.
static constexpr int TEXT_INPUT_EDIT_ID=1001,TEXT_INPUT_PROMPT_ID=1002;
struct TextInputDialogState {
    std::wstring* value=nullptr;
    bool allowEmpty=false,password=false,interactive=false;
};
static std::vector<BYTE> TextInputDialogTemplate(const std::wstring& title,const std::wstring& prompt,bool password)
{
    const size_t stringBytes=(title.size()+prompt.size()+g_ui.okText.size()+g_ui.cancelText.size()+64)*sizeof(wchar_t);
    std::vector<BYTE> bytes(4096+stringBytes);auto* dialog=reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    dialog->style=WS_POPUP|WS_SYSMENU|DS_MODALFRAME|WS_CAPTION|DS_SETFONT;
    dialog->cdit=4;dialog->cx=238;dialog->cy=79;
    auto* p=reinterpret_cast<WORD*>(dialog+1);*p++=0;*p++=0;AppendDialogTemplateString(p,title);
    *p++=8;AppendDialogTemplateString(p,L"MS Shell Dlg");
    const auto add=[&](short x,short y,short width,short height,WORD id,DWORD style,WORD kind,const std::wstring& text){
        auto* item=AlignDialogItem(p);item->x=x;item->y=y;item->cx=width;item->cy=height;item->id=id;item->style=WS_CHILD|WS_VISIBLE|style;
        p=reinterpret_cast<WORD*>(item+1);*p++=0xffff;*p++=kind;AppendDialogTemplateString(p,text);*p++=0;
    };
    // Edit first matches native InputBox tab order and selected default value.
    add(7,58,224,14,TEXT_INPUT_EDIT_ID,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL|(password?ES_PASSWORD:0),0x81,L"");
    add(181,6,50,14,IDOK,WS_TABSTOP|BS_DEFPUSHBUTTON,0x80,g_ui.okText);
    add(181,24,50,14,IDCANCEL,WS_TABSTOP|BS_PUSHBUTTON,0x80,g_ui.cancelText);
    add(7,6,159,48,TEXT_INPUT_PROMPT_ID,WS_GROUP|SS_LEFT|SS_NOPREFIX,0x82,prompt);
    return bytes;
}
static RECT TextInputDialogDefaultBounds(const RECT& desktop,int width,int height)
{
    const int left=desktop.left+(static_cast<int>(desktop.right-desktop.left)-width)/2;
    const int top=desktop.top+(static_cast<int>(desktop.bottom-desktop.top)-height)/3;
    return {left,top,left+width,top+height};
}
static void LayoutTextInputDialog(HWND dialog)
{
    HWND prompt=GetDlgItem(dialog,TEXT_INPUT_PROMPT_ID),edit=GetDlgItem(dialog,TEXT_INPUT_EDIT_ID);
    RECT outer{},desktop{},promptBounds{},editBounds{};GetWindowRect(dialog,&outer);
    if(!GetWindowRect(GetDesktopWindow(),&desktop))SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);
    if(prompt&&edit&&GetWindowRect(prompt,&promptBounds)&&GetWindowRect(edit,&editBounds)){
        MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&promptBounds),2);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&editBounds),2);
        const int length=GetWindowTextLengthW(prompt);
        if(length>0&&length<=32767){
            std::wstring text(static_cast<size_t>(length)+1,L'\0');GetWindowTextW(prompt,text.data(),length+1);text.resize(length);
            HDC dc=GetDC(prompt);if(dc){
                const auto font=reinterpret_cast<HFONT>(SendMessageW(prompt,WM_GETFONT,0,0));const auto previous=font?SelectObject(dc,font):nullptr;
                RECT measured{0,0,promptBounds.right-promptBounds.left,0};
                DrawTextW(dc,text.c_str(),length,&measured,DT_CALCRECT|DT_WORDBREAK|DT_EXPANDTABS|DT_NOPREFIX);
                if(previous)SelectObject(dc,previous);ReleaseDC(prompt,dc);
                // Native InputBox enlarges its prompt and moves only the edit.
                const int extra=(std::max)(0,(std::min)(static_cast<int>(measured.bottom-(promptBounds.bottom-promptBounds.top)),
                    static_cast<int>((desktop.bottom-desktop.top)-(outer.bottom-outer.top))));
                if(extra){
                    MoveWindow(prompt,promptBounds.left,promptBounds.top,promptBounds.right-promptBounds.left,promptBounds.bottom-promptBounds.top+extra,TRUE);
                    MoveWindow(edit,editBounds.left,editBounds.top+extra,editBounds.right-editBounds.left,editBounds.bottom-editBounds.top,TRUE);
                    outer.bottom+=extra;
                }
            }
        }
    }
    const auto bounds=TextInputDialogDefaultBounds(desktop,outer.right-outer.left,outer.bottom-outer.top);
    SetWindowPos(dialog,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE);
}
static bool CommitTextInputDialog(HWND dialog,TextInputDialogState& state)
{
    HWND edit=GetDlgItem(dialog,TEXT_INPUT_EDIT_ID);if(!state.value||!edit)return false;
    const int length=GetWindowTextLengthW(edit);if(length<0||length>32767||(!length&&!state.allowEmpty))return false;
    std::wstring text(static_cast<size_t>(length)+1,L'\0');
    struct Wipe{std::wstring& value;bool secret;~Wipe(){if(secret&&!value.empty())SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));}}wipe{text,state.password};
    const int copied=GetWindowTextW(edit,text.data(),length+1);
    if(copied!=length){if(state.password)SecureZeroMemory(text.data(),text.size()*sizeof(wchar_t));return false;}
    text.resize(length);*state.value=text;
    if(state.password&&!text.empty())SecureZeroMemory(text.data(),text.size()*sizeof(wchar_t));return true;
}
INT_PTR CALLBACK RenameDlgProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) noexcept
{
    try{
        auto* state=reinterpret_cast<TextInputDialogState*>(GetWindowLongPtrW(dialog,DWLP_USER));
        if(message==WM_INITDIALOG){
            state=reinterpret_cast<TextInputDialogState*>(lParam);if(!state||!state->value)return FALSE;
            SetWindowLongPtrW(dialog,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
            SetDlgItemTextW(dialog,TEXT_INPUT_EDIT_ID,state->value->c_str());LayoutTextInputDialog(dialog);
            HWND edit=GetDlgItem(dialog,TEXT_INPUT_EDIT_ID);
            if(edit){if(state->password)SendMessageW(edit,EM_SETPASSWORDCHAR,L'\x25cf',0);
                if(state->interactive)SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);SendMessageW(edit,EM_SCROLLCARET,0,0);}
            return FALSE;
        }
        if(!state)return FALSE;
        if(message==WM_COMMAND){
            const auto id=LOWORD(wParam);
            if(id==IDOK){if(CommitTextInputDialog(dialog,*state))EndDialog(dialog,IDOK);
                else MessageBoxW(dialog,g_ui.pathCannotBeEmpty.c_str(),g_ui.errorTitle.c_str(),MB_OK|MB_ICONWARNING);return TRUE;}
            if(id==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
        }
        if(message==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}
        if(message==WM_NCDESTROY)SetWindowLongPtrW(dialog,DWLP_USER,0);
        return FALSE;
    }catch(...){EndDialog(dialog,IDCANCEL);return TRUE;}
}
INT_PTR ShowTextInputDialog(HWND parent,const std::wstring& title,const std::wstring& prompt,std::wstring& value,bool allowEmpty,bool password=false)
{
    const auto caption=UiLiteralText(title.empty()?g_ui.renameDialogTitle:title,true);
    const auto question=UiLiteralText(prompt.empty()?caption:prompt,true);
    const auto bytes=TextInputDialogTemplate(caption,question,password);TextInputDialogState state{&value,allowEmpty,password,true};
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(bytes.data()),parent,RenameDlgProc,reinterpret_cast<LPARAM>(&state));
}

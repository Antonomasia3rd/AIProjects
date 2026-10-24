// Hidden native dialogs and template metadata only. No modal interaction,
// account, settings write, script engine or clipboard operation is exercised.
static std::wstring TextInputFixtureString(const WORD*& word)
{
    std::wstring value;while(*word)value+=static_cast<wchar_t>(*word++);++word;return value;
}
static void TestTextInputDialogHost()
{
    const auto bytes=TextInputDialogTemplate(L"Add tile",L"Tile name",false);
    const auto* dialogTemplate=reinterpret_cast<const DLGTEMPLATE*>(bytes.data());
    const WORD* word=reinterpret_cast<const WORD*>(dialogTemplate+1);word+=2;
    const auto title=TextInputFixtureString(word);const WORD points=*word++;const auto face=TextInputFixtureString(word);
    Check(dialogTemplate->cx==238&&dialogTemplate->cy==79&&dialogTemplate->cdit==4&&points==8&&face==L"MS Shell Dlg"&&title==L"Add tile",
        "text input independently matches the installed VBScript InputBox base dimensions and English resource font");
    const int expected[][5]={{TEXT_INPUT_EDIT_ID,7,58,224,14},{IDOK,181,6,50,14},{IDCANCEL,181,24,50,14},{TEXT_INPUT_PROMPT_ID,7,6,159,48}};
    for(const auto& row:expected){
        word=reinterpret_cast<const WORD*>((reinterpret_cast<ULONG_PTR>(word)+3)&~ULONG_PTR(3));
        const auto* item=reinterpret_cast<const DLGITEMTEMPLATE*>(word);
        Check(item->id==row[0]&&item->x==row[1]&&item->y==row[2]&&item->cx==row[3]&&item->cy==row[4],
            "input control geometry and edit-first order match the visible native InputBox contract");
        word=reinterpret_cast<const WORD*>(item+1);word+=2;TextInputFixtureString(word);++word;
    }
    for(const int dpi:{96,144,192,384}){
        const RECT desktop{-1920,-100,1920,2060};const int width=MulDiv(374,dpi,96),height=MulDiv(160,dpi,96);
        const auto bounds=TextInputDialogDefaultBounds(desktop,width,height);
        Check(bounds.right-bounds.left==width&&bounds.bottom-bounds.top==height&&
            bounds.left==desktop.left+(desktop.right-desktop.left-width)/2&&bounds.top==desktop.top+(desktop.bottom-desktop.top-height)/3,
            "native default placement remains horizontally centered and one-third down at each tested DPI");
    }
    const auto focusBefore=GetFocus();std::wstring firstValue=L"First default",secondValue=L"Second default";
    TextInputDialogState first{&firstValue,false,false,false},second{&secondValue,true,false,false};
    HWND firstDialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),dialogTemplate,nullptr,RenameDlgProc,reinterpret_cast<LPARAM>(&first));
    HWND secondDialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),dialogTemplate,nullptr,RenameDlgProc,reinterpret_cast<LPARAM>(&second));
    Check(firstDialog&&secondDialog,"parallel text input fixtures create using native controls without modal prompts");
    if(!firstDialog||!secondDialog){if(firstDialog)DestroyWindow(firstDialog);if(secondDialog)DestroyWindow(secondDialog);return;}
    Check(!IsWindowVisible(firstDialog)&&!IsWindowVisible(secondDialog)&&GetFocus()==focusBefore,
        "input fixture windows remain hidden and do not take keyboard focus from the user");
    Check(GetWindowLongPtrW(firstDialog,DWLP_USER)==reinterpret_cast<LONG_PTR>(&first)&&GetWindowLongPtrW(secondDialog,DWLP_USER)==reinterpret_cast<LONG_PTR>(&second),
        "each native input dialog owns its state instead of sharing a static callback pointer");
    DWORD start=1,end=0;SendDlgItemMessageW(firstDialog,TEXT_INPUT_EDIT_ID,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    Check(start==0&&end==firstValue.size(),"the entire native default response is selected for immediate replacement");
    Check(GetNextDlgTabItem(firstDialog,nullptr,FALSE)==GetDlgItem(firstDialog,TEXT_INPUT_EDIT_ID)&&
        GetNextDlgTabItem(firstDialog,GetDlgItem(firstDialog,TEXT_INPUT_EDIT_ID),FALSE)==GetDlgItem(firstDialog,IDOK)&&
        (GetWindowLongPtrW(GetDlgItem(firstDialog,IDOK),GWL_STYLE)&BS_DEFPUSHBUTTON),
        "edit-first tab navigation and Enter's default OK action follow the native InputBox arrangement");
    RECT expectedEdit{7,58,231,72};MapDialogRect(firstDialog,&expectedEdit);RECT actualEdit{};
    GetWindowRect(GetDlgItem(firstDialog,TEXT_INPUT_EDIT_ID),&actualEdit);MapWindowPoints(nullptr,firstDialog,reinterpret_cast<POINT*>(&actualEdit),2);
    Check(EqualRect(&expectedEdit,&actualEdit)!=FALSE,"dialog manager scales edit geometry from font-based dialog units at the actual window DPI");
    SetDlgItemTextW(firstDialog,TEXT_INPUT_EDIT_ID,L"First changed");
    Check(CommitTextInputDialog(firstDialog,first)&&firstValue==L"First changed"&&secondValue==L"Second default",
        "committing one dialog cannot overwrite the parallel dialog's target string");
    DestroyWindow(secondDialog);
    Check(GetWindowLongPtrW(firstDialog,DWLP_USER)==reinterpret_cast<LONG_PTR>(&first),
        "destroying a nested or parallel input does not clear the surviving dialog's state");
    SetDlgItemTextW(firstDialog,TEXT_INPUT_EDIT_ID,L"");
    Check(!CommitTextInputDialog(firstDialog,first)&&firstValue==L"First changed","existing nonempty validation cannot clear the caller value on rejection");
    first.allowEmpty=true;
    Check(CommitTextInputDialog(firstDialog,first)&&firstValue.empty(),"an explicitly permitted empty answer retains native InputBox value semantics");
    DestroyWindow(firstDialog);
    std::wstring password=L"Synthetic only";TextInputDialogState protectedState{&password,true,true,false};
    const auto protectedBytes=TextInputDialogTemplate(L"Protected input",L"Password",true);
    HWND protectedDialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(protectedBytes.data()),nullptr,
        RenameDlgProc,reinterpret_cast<LPARAM>(&protectedState));
    Check(protectedDialog&&(GetWindowLongPtrW(GetDlgItem(protectedDialog,TEXT_INPUT_EDIT_ID),GWL_STYLE)&ES_PASSWORD)&&
        SendDlgItemMessageW(protectedDialog,TEXT_INPUT_EDIT_ID,EM_GETPASSWORDCHAR,0,0)==L'\x25cf',
        "sensitive input retains native password masking with ExplorerPatcher's explicit black-circle character");
    if(protectedDialog)DestroyWindow(protectedDialog);
    std::wstring longPrompt;for(int i=0;i<120;++i)longPrompt+=L"Wrapped explanation ";
    const auto longBytes=TextInputDialogTemplate(L"Long prompt",longPrompt,false);std::wstring longValue=L"Unchanged";
    TextInputDialogState longState{&longValue,false,false,false};
    HWND longDialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(longBytes.data()),nullptr,
        RenameDlgProc,reinterpret_cast<LPARAM>(&longState));
    Check(longDialog!=nullptr,"long input prompts use the same independent native template");
    if(longDialog){RECT prompt{},edit{},client{};GetWindowRect(GetDlgItem(longDialog,TEXT_INPUT_PROMPT_ID),&prompt);
        GetWindowRect(GetDlgItem(longDialog,TEXT_INPUT_EDIT_ID),&edit);MapWindowPoints(nullptr,longDialog,reinterpret_cast<POINT*>(&prompt),2);
        MapWindowPoints(nullptr,longDialog,reinterpret_cast<POINT*>(&edit),2);GetClientRect(longDialog,&client);
        Check(prompt.bottom<=edit.top&&edit.bottom<=client.bottom,"long-prompt expansion moves the input down and keeps it inside the dialog");
        DestroyWindow(longDialog);Check(longValue==L"Unchanged","closing an input without commit preserves its original caller value");}
}

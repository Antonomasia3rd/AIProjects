// Literal overrides complement the named Strings catalogue. They cover dialog
// titles/prompts and app-owned message boxes without changing data/schema keys.
static std::mutex g_uiLiteralMutex;
static std::map<std::wstring,std::wstring> g_uiLiteralDefaults;
static std::wstring UiLiteralKey(const std::wstring& text) {
    unsigned long long hash=14695981039346656037ull;
    for(const auto ch:text){hash^=static_cast<unsigned>(ch);hash*=1099511628211ull;}
    return L"Text"+std::to_wstring(hash);
}
static std::wstring UiLiteralText(const std::wstring& text,bool catalogue=false) {
    const auto key=UiLiteralKey(text);
    if(catalogue){std::lock_guard<std::mutex> lock(g_uiLiteralMutex);g_uiLiteralDefaults[key]=text;}
    return IniReadS(L"Strings",key.c_str(),text.c_str());
}
static int WINAPI DesktopStubConfiguredMessageBox(HWND owner,LPCWSTR body,LPCWSTR title,UINT flags) {
    // Message bodies can contain current data. Do not export those bodies into
    // the default catalogue; explicit exact-text overrides are still supported.
    const auto b=body&&!IS_INTRESOURCE(body)?UiLiteralText(body):std::wstring();
    const auto t=title&&!IS_INTRESOURCE(title)?UiLiteralText(title,true):std::wstring();
    return ::MessageBoxW(owner,body&&!IS_INTRESOURCE(body)?b.c_str():body,title&&!IS_INTRESOURCE(title)?t.c_str():title,flags);
}
#define MessageBoxW DesktopStubConfiguredMessageBox
static bool UiIsLabelControl(const wchar_t* cls,DWORD style) {
    return cls&&(_wcsicmp(cls,L"STATIC")==0||_wcsicmp(cls,L"BUTTON")==0||
        ((style&WS_CAPTION)==WS_CAPTION&&_wcsicmp(cls,WINDOW_CLASS_NAME)!=0));
}
static HWND WINAPI DesktopStubConfiguredCreateWindow(DWORD ex,LPCWSTR cls,LPCWSTR text,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE instance,LPVOID parameter) {
    const bool named=cls&&!IS_INTRESOURCE(cls);
    const bool label=named&&UiIsLabelControl(cls,style);
    const auto translated=label&&text&&!IS_INTRESOURCE(text)?UiLiteralText(text,true):std::wstring();
    return ::CreateWindowExW(ex,cls,label&&text&&!IS_INTRESOURCE(text)?translated.c_str():text,style,x,y,w,h,parent,menu,instance,parameter);
}
static BOOL WINAPI DesktopStubConfiguredSetWindowText(HWND window,LPCWSTR text) {
    wchar_t cls[128]{};GetClassNameW(window,cls,128);
    const bool label=UiIsLabelControl(cls,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)));
    const auto translated=label&&text&&!IS_INTRESOURCE(text)?UiLiteralText(text):std::wstring();
    return ::SetWindowTextW(window,label&&text&&!IS_INTRESOURCE(text)?translated.c_str():text);
}
#define CreateWindowExW DesktopStubConfiguredCreateWindow
#define SetWindowTextW DesktopStubConfiguredSetWindowText

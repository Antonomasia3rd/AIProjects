// Independent native settings presenter, BSD-0-Clause like AIProjects.
// Visual/interaction reference inspected, NOT linked/copied: ExplorerPatcher
// 0a88a6e0ef6b1752fea36e581cffff1097e862b0, ep_gui/GUI.h and GUI.c (GPLv2).
// Observed contract: 150-DIP sidebar, 15-DIP padding, 32-DIP navigation,
// 26-DIP setting rows, 42-DIP heading; system menu/caption fonts, demibold
// underlined selection, text choices/toggles, keyboard navigation and DPI reflow.
// No upstream source, registry schema, resources or shell hooks are included.
// Include after ga_tray_dispatch.inc. No worker, network or idle timer is used.
#include <dwmapi.h>
#include <uxtheme.h>
#include <string_view>
#ifndef DESKTOPSTUB_INERT_HARDWARE
namespace desktop_settings {
enum class Kind {Info,Page,Choice,Toggle,Edit,Action,Special};
enum Special {SelectTile=1,AddTile,Back,ReloadPage,OpenIni,Contents};
struct Node {
    Kind kind=Kind::Info;std::wstring label;std::vector<Node> children;
    TileInstanceMenuCommand command{};bool typed=false,checked=false,enabled=true;UINT global=0;int special=0;
};
struct Tile {aip::content::TileInstance value;int gallery=-1;};
struct Navigation {std::wstring label;int section=0,index=-1;};
struct Hit {RECT area{};bool navigation=false;int index=0;};
// All optional compositor entry points are resolved from Windows' system DLLs.
// A failed prerequisite keeps the ordinary opaque Win32 frame and paint path.
struct Composition {
    HMODULE dwm=nullptr,ux=nullptr;HTHEME theme=nullptr;bool initialized=false;
    decltype(&DwmSetWindowAttribute) set=nullptr;decltype(&DwmGetWindowAttribute) get=nullptr;
    decltype(&DwmIsCompositionEnabled) enabled=nullptr;decltype(&DwmExtendFrameIntoClientArea) extend=nullptr;
    decltype(&DwmDefWindowProc) def=nullptr;decltype(&IsThemeActive) active=nullptr;
    decltype(&BufferedPaintInit) init=nullptr;decltype(&BufferedPaintUnInit) uninit=nullptr;
    decltype(&BeginBufferedPaint) begin=nullptr;decltype(&EndBufferedPaint) end=nullptr;
    decltype(&OpenThemeData) open=nullptr;decltype(&CloseThemeData) close=nullptr;decltype(&DrawThemeTextEx) text=nullptr;
    bool loaded=false;
    void Load(){if(loaded)return;loaded=true;
        dwm=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        ux=LoadLibraryExW(L"uxtheme.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(dwm){set=reinterpret_cast<decltype(set)>(GetProcAddress(dwm,"DwmSetWindowAttribute"));get=reinterpret_cast<decltype(get)>(GetProcAddress(dwm,"DwmGetWindowAttribute"));
            enabled=reinterpret_cast<decltype(enabled)>(GetProcAddress(dwm,"DwmIsCompositionEnabled"));extend=reinterpret_cast<decltype(extend)>(GetProcAddress(dwm,"DwmExtendFrameIntoClientArea"));def=reinterpret_cast<decltype(def)>(GetProcAddress(dwm,"DwmDefWindowProc"));}
        if(ux){active=reinterpret_cast<decltype(active)>(GetProcAddress(ux,"IsThemeActive"));init=reinterpret_cast<decltype(init)>(GetProcAddress(ux,"BufferedPaintInit"));
            uninit=reinterpret_cast<decltype(uninit)>(GetProcAddress(ux,"BufferedPaintUnInit"));begin=reinterpret_cast<decltype(begin)>(GetProcAddress(ux,"BeginBufferedPaint"));end=reinterpret_cast<decltype(end)>(GetProcAddress(ux,"EndBufferedPaint"));
            open=reinterpret_cast<decltype(open)>(GetProcAddress(ux,"OpenThemeData"));close=reinterpret_cast<decltype(close)>(GetProcAddress(ux,"CloseThemeData"));text=reinterpret_cast<decltype(text)>(GetProcAddress(ux,"DrawThemeTextEx"));}
        if(init&&uninit)initialized=SUCCEEDED(init());
    }
    ~Composition(){if(theme&&close)close(theme);if(initialized&&uninit)uninit();if(ux)FreeLibrary(ux);if(dwm)FreeLibrary(dwm);}
};
struct State {
    HWND window=nullptr,status=nullptr;HFONT regular=nullptr,heading=nullptr,sectionFont=nullptr,selectedFont=nullptr;
    UINT dpi=96;bool dark=false,contrast=false;COLORREF background=RGB(255,255,255),foreground=RGB(0,0,0);
    HBRUSH backgroundBrush=nullptr;
    Composition composition;bool glass=false,frameChanging=false,actionFailed=false;int captionHeight=42;RECT captionButtons{};std::wstring statusText;
    std::wstring profile,revision,tileId;std::vector<Tile> tiles;aip::gallery::Configuration gallery;
    std::map<std::wstring,std::wstring> wording;
    std::vector<Node> root,globalRows;std::vector<std::size_t> path;std::vector<Navigation> navigation;std::vector<Hit> hits;
    std::vector<Node*> rows;Node tileRow,addRow,backRow,reloadRow;
    int section=0,focus=0,scroll=0,navScroll=0;bool closed=false;ULONGLONG lastClick=0;int lastHit=-1;
    ~State(){for(auto font:{regular,heading,sectionFont,selectedFont})if(font)DeleteObject(font);
        if(backgroundBrush)DeleteObject(backgroundBrush);
        if(!revision.empty())SecureZeroMemory(&revision[0],revision.size()*sizeof(wchar_t));}
};
static std::shared_ptr<State> current;
static constexpr const wchar_t* FailureText=L"Settings could not complete this action. Reload before retrying.";
static std::wstring Text(const wchar_t* key,const wchar_t* fallback){return IniReadS(L"Strings",key,fallback);}
static int D(const State& s,int value){return MulDiv(value,static_cast<int>(s.dpi),96);}
static int ContentTop(const State& s){return s.glass?s.captionHeight:0;}
static int VisibleRows(const State& s,int height){return (std::max)(1,(height-D(s,38)-ContentTop(s))/D(s,26));}
static int WindowMetric(const State& s,int metric){using Metric=int(WINAPI*)(int,UINT);const auto get=reinterpret_cast<Metric>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetSystemMetricsForDpi"));return get?get(metric,s.dpi):GetSystemMetrics(metric);}
static std::wstring Profile(){std::lock_guard<std::mutex> lock(g_pathMutex);return aip::MakeAbsolutePath(g_iniPath);}
static std::wstring Caption(std::wstring text){std::wstring result;for(std::size_t i=0;i<text.size();++i){if(text[i]==L'&'){
    if(i+1<text.size()&&text[i+1]==L'&'){result+=L'&';++i;}}else result+=text[i];}return result;}
static void Status(const std::shared_ptr<State>& s,const std::wstring& text){if(s&&!s->closed&&s->status){s->statusText=text;s->actionFailed=false;SetWindowTextW(s->status,text.c_str());NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,s->status,OBJID_CLIENT,CHILDID_SELF);InvalidateRect(s->window,nullptr,FALSE);}}
static bool RevisionMatches(const std::shared_ptr<State>& s){std::wstring now;if(!s||s->closed||!IEquals(Profile(),s->profile)||!ReadIniFileForMutation(now)||now!=s->revision){
    Status(s,Text(L"SettingsWindowStale",L"The profile or settings changed. Press F5 to reload before applying another change."));return false;}return true;}
struct RegistryScope {
    std::map<UINT,TileInstanceMenuCommand> previous;UINT next=0;std::wstring section;std::size_t page=0;
    RegistryScope():previous(std::move(g_tileInstanceMenuCommands)),next(g_nextTileInstanceMenuCommand),section(g_tilePolicyMenuSection),page(g_tileInstanceMenuPage){ResetTileInstanceMenuCommands();g_tilePolicyMenuSection.clear();}
    ~RegistryScope(){for(auto& item:g_tileInstanceMenuCommands)if(item.second.secret&&!item.second.value.empty())SecureZeroMemory(&item.second.value[0],item.second.value.size()*sizeof(wchar_t));
        g_tileInstanceMenuCommands=std::move(previous);g_nextTileInstanceMenuCommand=next;g_tilePolicyMenuSection=std::move(section);g_tileInstanceMenuPage=page;}
};
struct WordingScope {std::map<std::wstring,std::wstring> previous=std::move(g_menuWordingDefaults);~WordingScope(){g_menuWordingDefaults=std::move(previous);}};
static bool GlobalToggle(UINT id){
    if(id>=ID_CAPS_SETTINGS&&id<ID_CAPS_SETTINGS+std::size(g_capsOptions))return g_capsOptions[id-ID_CAPS_SETTINGS].toggle;
    if(id>=ID_ASUS_SETTINGS&&id<ID_ASUS_SETTINGS+std::size(g_asusOptions))return g_asusOptions[id-ID_ASUS_SETTINGS].toggle;
    switch(id){case ID_RUN_AT_STARTUP:case ID_RUN_AT_STARTUP_PACKAGED:case ID_TRAYICON:case ID_HIDE_DISABLED:case ID_CONSOLE:
        case ID_NOTIF_ENABLED:case ID_NOTIFY_OPERATIONS:case ID_NOTIFICATION_CONFIRMATIONS:case ID_NOTIFY_PIN_PROGRESS:case ID_NOTIFY_SOURCE_ERRORS:
        case ID_NOTIFY_GALLERY_CHANGES:case ID_NOTIFY_START:case ID_NOTIFY_SUCCESS:case ID_NOTIFY_FAILURE:case ID_NOTIFY_BUSY:case ID_NOTIFY_ALREADY_RUNNING:
        case ID_NOTIFY_SINGLE_INSTANCE_FAILURE:case ID_SHOW_MENU_DROPDOWN:case ID_CLEAR_LIVE_TILE_ON_SHUTDOWN:case ID_LIVE_TILE_RELAUNCH_ON_SWITCH:case ID_LOG:case ID_USE_PS:return true;default:return false;}
}
static bool GlobalSettingChoice(UINT id){return id==ID_NOTIF_API_AUTO||id==ID_NOTIF_API_BALLOON||id==ID_NOTIF_API_TOAST;}
static std::vector<Node> Capture(HMENU menu,unsigned depth=0){
    std::vector<Node> nodes;if(depth>12)return nodes;const int count=GetMenuItemCount(menu);
    for(int index=0;index<count&&nodes.size()<2000;++index){wchar_t label[4096]{};MENUITEMINFOW item{};item.cbSize=sizeof(item);
        item.fMask=MIIM_STRING|MIIM_FTYPE|MIIM_STATE|MIIM_ID|MIIM_SUBMENU;item.dwTypeData=label;item.cch=static_cast<UINT>(std::size(label));
        if(!GetMenuItemInfoW(menu,index,TRUE,&item)||(item.fType&MFT_SEPARATOR))continue;
        Node node;node.label=Caption(label);node.checked=(item.fState&MFS_CHECKED)!=0;node.enabled=(item.fState&(MFS_DISABLED|MFS_GRAYED))==0;
        if(item.hSubMenu){node.kind=Kind::Page;node.children=Capture(item.hSubMenu,depth+1);
            if(!node.children.empty()&&std::all_of(node.children.begin(),node.children.end(),[&](const Node& child){return child.typed&&child.command.verb==TileInstanceMenuVerb::Set&&
                child.command.key==node.children.front().command.key&&child.command.id==node.children.front().command.id;}))node.kind=Kind::Choice;
            else if(!node.children.empty()&&std::any_of(node.children.begin(),node.children.end(),[](const Node& child){return child.checked;})&&
                std::all_of(node.children.begin(),node.children.end(),[](const Node& child){return child.global&&!GlobalToggle(child.global)&&child.children.empty();}))node.kind=Kind::Choice;
        }else{const auto command=g_tileInstanceMenuCommands.find(item.wID);
            if(command!=g_tileInstanceMenuCommands.end()){node.typed=true;node.command=command->second;
                node.kind=node.command.verb==TileInstanceMenuVerb::Toggle?Kind::Toggle:node.command.verb==TileInstanceMenuVerb::Edit?Kind::Edit:Kind::Action;
                if(node.command.secret)node.command.value.clear();
            }else if(item.wID&&item.wID!=static_cast<UINT>(-1)){node.global=item.wID;node.kind=GlobalToggle(item.wID)?Kind::Toggle:Kind::Action;}
        }
        nodes.push_back(std::move(node));
    }return nodes;
}
static Node SpecialRow(int special,std::wstring label){Node n;n.kind=Kind::Special;n.special=special;n.label=std::move(label);return n;}
static bool ContainsCommand(const Node& node,UINT id){if(node.global==id)return true;for(const auto& child:node.children)if(ContainsCommand(child,id))return true;return false;}
static void GlobalRows(const std::shared_ptr<State>& s){
    s->globalRows.clear();if(s->section==0)return;
    const auto info=[&](const std::wstring& value){Node n;n.label=value;n.enabled=false;s->globalRows.push_back(std::move(n));};
    if(s->section==4){info(L"DesktopStub "+DesktopStubVersionDisplayText());info(Text(L"SettingsWindowReference",L"ExplorerPatcher-style presentation, independently implemented for DesktopStub."));info(s->profile);return;}
    RegistryScope registry;WordingScope wording;HMENU menu=CreatePopupMenu();if(!menu)return;
    struct MenuOwner{HMENU menu;~MenuOwner(){DestroyMenu(menu);}} owned{menu};
    if(s->section==1){AppendTileInstancePluginsMenu(menu);ApplyMenuWording(menu);s->globalRows=Capture(menu);}
    else{
        bool startup=IniReadI(L"Settings",L"RunAtStartup",0)!=0;std::wstring error;
        QueryDesktopStubStartupShortcutInstalled(startup,error); // Read-only inventory; never reconciles Startup.
        PopulateDesktopStubTrayMenu(menu,true,startup);const auto nodes=Capture(menu);
        const auto id=s->section==2?ID_NOTIF_ENABLED:ID_SHOW_PSLOG;
        for(const auto& node:nodes)if(ContainsCommand(node,id)){s->globalRows=node.children;break;}
        if(s->section==2)for(const auto& node:nodes)if(ContainsCommand(node,ID_USE_PS)){s->globalRows.push_back(node);break;}
        if(s->section==2){HMENU delivery=CreatePopupMenu();if(delivery){AppendTileInstanceDeliveryMenu(delivery);ApplyMenuWording(delivery);
            auto extra=Capture(delivery);DestroyMenu(delivery);s->globalRows.insert(s->globalRows.begin(),std::make_move_iterator(extra.begin()),std::make_move_iterator(extra.end()));}}
        if(s->section==3){s->globalRows.push_back(SpecialRow(Contents,Text(L"AllTileContents",L"All tile contents...")));s->globalRows.push_back(SpecialRow(OpenIni,Text(L"SettingsWindowOpenIni",L"Open configuration file...")));}
    }
    s->wording=g_menuWordingDefaults;
}
static void RebuildNavigation(const std::shared_ptr<State>& s){
    s->navigation={{Text(L"SettingsWindowTiles",L"Tiles"),0,-1}};
    if(s->section==0)for(std::size_t i=0;i<s->root.size();++i)if(s->root[i].kind==Kind::Page)s->navigation.push_back({s->root[i].label,0,static_cast<int>(i)});
    const std::pair<const wchar_t*,const wchar_t*> pages[]={{L"SettingsWindowPlugins",L"Plugins"},{L"SettingsWindowApplication",L"Application"},{L"SettingsWindowDiagnostics",L"Diagnostics"},{L"SettingsWindowAboutTitle",L"About"}};
    for(int section=1;section<=4;++section){s->navigation.push_back({Text(pages[section-1].first,pages[section-1].second),section,-1});
        if(s->section==section)for(std::size_t i=0;i<s->globalRows.size();++i)if(s->globalRows[i].kind==Kind::Page)s->navigation.push_back({s->globalRows[i].label,section,static_cast<int>(i)});}
}
static void Reload(const std::shared_ptr<State>& s,bool preservePage=true){
    if(!s||s->closed||!IsWindow(s->window))return;
    std::vector<std::wstring> trail;auto* before=s->section==0?&s->root:&s->globalRows;
    for(const auto index:s->path){if(index>=before->size())break;trail.push_back((*before)[index].label);before=&(*before)[index].children;}
    const auto previousTile=s->tileId;const auto previousFocus=s->focus,previousScroll=s->scroll;
    s->profile=Profile();std::wstring revision;if(!ReadIniFileForMutation(revision)){Status(s,L"The settings file could not be read.");return;}
    if(!s->revision.empty())SecureZeroMemory(&s->revision[0],s->revision.size()*sizeof(wchar_t));s->revision=std::move(revision);
    aip::content::TileInstanceConfiguration cfg;std::wstring error;s->tiles.clear();
    if(ReadTileInstanceMenuConfiguration(cfg,error))for(const auto& tile:cfg.tiles)s->tiles.push_back({tile,-1});
    if(ReadGalleryConfiguration(s->gallery,error)&&!s->gallery.groupId.empty())for(std::size_t i=0;i<s->gallery.tiles.size();++i){const auto& row=s->gallery.tiles[i];
        Tile tile;tile.value.id=aip::gallery::NativeTileId(s->gallery.groupId,row.id);tile.value.name=row.name;tile.value.type=aip::content::TileInstanceType::Gallery;tile.gallery=static_cast<int>(i);s->tiles.push_back(std::move(tile));}
    auto selected=std::find_if(s->tiles.begin(),s->tiles.end(),[&](const Tile& tile){return tile.value.id==s->tileId;});
    if(selected==s->tiles.end()&&!s->tiles.empty())selected=s->tiles.begin();s->root.clear();
    if(selected!=s->tiles.end()){
        s->tileId=selected->value.id;RegistryScope registry;HMENU menu=CreatePopupMenu();if(menu){
            if(selected->gallery<0)AppendTileInstanceDetails(menu,selected->value);
            else{const auto i=static_cast<std::size_t>(selected->gallery);const auto& row=s->gallery.tiles[i];const auto base=ID_GALLERY_TILE+static_cast<UINT>(i)*GALLERY_TILE_COMMAND_STRIDE;
                aip::AppendTrayMenuItem(menu,base+1,L"Rename gallery tile...",false,true);
                if(GalleryTileUsesRegistration(row))aip::AppendTrayMenuItem(menu,base+2,L"Registration setup and pin instructions...",false,true);
                else AppendSecondaryTilePinCommands(menu,selected->value.id,base+2,base+3,base+4,true);
                HMENU click=CreatePopupMenu();if(click){aip::AppendTrayMenuItem(click,base+7,L"Open the displayed photo",row.clickAction==L"Image",true);
                    aip::AppendTrayMenuItem(click,base+8,L"Open this tile's settings",row.clickAction==L"Setup",true);aip::EndTrayNestedMenu(menu,click,L"When clicked");}
                aip::AppendTrayMenuItem(menu,base+6,L"Rendering, cache and delivery",false,false);AppendGalleryTilePolicySettings(menu,s->gallery,i);
                AppendTileAppearanceMenu(menu,L"Gallery.Tile."+std::to_wstring(i+1),base+10);
                const auto groups=GalleryGroups(s->gallery);for(std::size_t g=0;g<groups.size();++g)if(std::find(groups[g].indices.begin(),groups[g].indices.end(),i)!=groups[g].indices.end()){
                    aip::AppendTrayMenuItem(menu,ID_GALLERY_GROUP+static_cast<UINT>(g)*4+2,L"Choose gallery folder...",false,true);
                    aip::AppendTrayMenuItem(menu,ID_GALLERY_GROUP+static_cast<UINT>(g)*4+1,L"Gallery tile count: "+std::to_wstring(groups[g].indices.size())+L"...",false,true);}
            }
            s->root=Capture(menu);DestroyMenu(menu);
        }
    }else s->tileId.clear();
    s->path.clear();s->focus=s->scroll=0;GlobalRows(s);
    if(preservePage&&(s->section!=0||previousTile==s->tileId)){auto* after=s->section==0?&s->root:&s->globalRows;
        for(const auto& label:trail){const auto found=std::find_if(after->begin(),after->end(),[&](const Node& node){return node.label==label&&!node.children.empty();});
            if(found==after->end())break;s->path.push_back(static_cast<std::size_t>(found-after->begin()));after=&found->children;}s->focus=previousFocus;s->scroll=previousScroll;}
    RebuildNavigation(s);InvalidateRect(s->window,nullptr,FALSE);
}
static std::vector<Node>& Page(State& s){auto* nodes=s.section==0?&s.root:&s.globalRows;for(const auto index:s.path){if(index>=nodes->size())break;nodes=&(*nodes)[index].children;}return *nodes;}
static void Rows(const std::shared_ptr<State>& s){
    s->rows.clear();if(s->section==0){
        const auto tile=std::find_if(s->tiles.begin(),s->tiles.end(),[&](const Tile& t){return t.value.id==s->tileId;});
        s->tileRow=SpecialRow(SelectTile,Text(L"SettingsWindowTile",L"Tile")+L": "+(tile==s->tiles.end()?Text(L"SettingsWindowNoTiles",L"No tiles configured"):tile->value.name)+L" \u25be");
        s->addRow=SpecialRow(AddTile,Text(L"SettingsWindowAdd",L"Add tile..."));s->rows.push_back(&s->tileRow);s->rows.push_back(&s->addRow);
        if(!s->path.empty()){s->backRow=SpecialRow(Back,Text(L"SettingsWindowBack",L"\u2190 Back"));s->rows.push_back(&s->backRow);}
        for(auto& node:Page(*s))s->rows.push_back(&node);
    }else{if(!s->path.empty()){s->backRow=SpecialRow(Back,Text(L"SettingsWindowBack",L"\u2190 Back"));s->rows.push_back(&s->backRow);}for(auto& row:Page(*s))s->rows.push_back(&row);}
    s->reloadRow=SpecialRow(ReloadPage,Text(L"SettingsWindowReload",L"Reload settings (F5)"));s->rows.push_back(&s->reloadRow);
    if(s->focus>=static_cast<int>(s->rows.size()))s->focus=0;
}
static void Fonts(const std::shared_ptr<State>& s){
    for(auto* font:{&s->regular,&s->heading,&s->sectionFont,&s->selectedFont}){if(*font)DeleteObject(*font);*font=nullptr;}
    NONCLIENTMETRICSW metrics{};metrics.cbSize=sizeof(metrics);using GetMetrics=BOOL(WINAPI*)(UINT,UINT,PVOID,UINT,UINT);
    const auto get=reinterpret_cast<GetMetrics>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SystemParametersInfoForDpi"));
    if(!(get?get(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,s->dpi):SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0)))return;
    s->heading=CreateFontIndirectW(&metrics.lfCaptionFont);auto menu=metrics.lfMenuFont;menu.lfWeight=FW_NORMAL;menu.lfUnderline=FALSE;s->regular=CreateFontIndirectW(&menu);
    menu.lfWeight=FW_SEMIBOLD;s->sectionFont=CreateFontIndirectW(&menu);menu.lfUnderline=TRUE;s->selectedFont=CreateFontIndirectW(&menu);
    for(const auto control:{s->status})if(control)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(s->regular),TRUE);
}
static void CaptionMetrics(const std::shared_ptr<State>& s){
    s->captionHeight=D(*s,42);s->captionButtons={};
    RECT buttons{};if(s->glass&&s->composition.get&&SUCCEEDED(s->composition.get(s->window,DWMWA_CAPTION_BUTTON_BOUNDS,&buttons,sizeof(buttons)))&&
        buttons.right>buttons.left&&buttons.bottom>buttons.top){s->captionButtons=buttons;s->captionHeight=(std::max)(D(*s,24),(std::min)(D(*s,64),static_cast<int>(buttons.bottom-buttons.top)));}
}
static void FrameMode(const std::shared_ptr<State>& s,bool glass){
    const bool changed=s->glass!=glass;s->glass=glass;
    if(!glass){const MARGINS margins{};const DWORD none=1;
        if(s->composition.extend)s->composition.extend(s->window,&margins);
        if(s->composition.set)s->composition.set(s->window,static_cast<DWMWINDOWATTRIBUTE>(38),&none,sizeof(none));}
    if(changed&&!s->frameChanging){s->frameChanging=true;SetWindowPos(s->window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);s->frameChanging=false;}
    CaptionMetrics(s);if(changed)InvalidateRect(s->window,nullptr,FALSE);
}
static bool SupportsMainWindowMaterial(){
    // The documented SYSTEMBACKDROP_TYPE contract starts at Windows 11 22H2.
    // Earlier Windows builds retain the ordinary frame rather than using an
    // undocumented material attribute or relying on a manifest version lie.
    using Version=LONG(WINAPI*)(OSVERSIONINFOW*);const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto get=module?reinterpret_cast<Version>(GetProcAddress(module,"RtlGetVersion")):nullptr;
    OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
    return get&&get(&version)>=0&&version.dwMajorVersion>=10&&version.dwBuildNumber>=22621;
}
static void Theme(const std::shared_ptr<State>& s){
    HIGHCONTRASTW high{};high.cbSize=sizeof(high);SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(high),&high,0);s->contrast=(high.dwFlags&HCF_HIGHCONTRASTON)!=0;
    DWORD light=1,bytes=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    s->dark=!s->contrast&&!light;s->background=s->contrast?GetSysColor(COLOR_3DFACE):s->dark?RGB(0,0,0):RGB(255,255,255);
    s->foreground=s->contrast?GetSysColor(COLOR_WINDOWTEXT):s->dark?RGB(240,240,240):RGB(0,0,0);
    if(s->backgroundBrush)DeleteObject(s->backgroundBrush);s->backgroundBrush=CreateSolidBrush(s->background);
    auto& c=s->composition;c.Load();if(c.theme&&c.close){c.close(c.theme);c.theme=nullptr;}
    const BOOL dark=s->dark;if(c.set&&FAILED(c.set(s->window,static_cast<DWMWINDOWATTRIBUTE>(20),&dark,sizeof(dark))))c.set(s->window,static_cast<DWMWINDOWATTRIBUTE>(19),&dark,sizeof(dark));
    BOOL composed=FALSE;bool glass=!s->contrast&&SupportsMainWindowMaterial()&&c.initialized&&c.set&&c.get&&c.enabled&&c.extend&&c.def&&c.active&&c.active()&&c.open&&c.close&&c.text&&c.begin&&c.end&&
        SUCCEEDED(c.enabled(&composed))&&composed;
    if(glass){c.theme=c.open(s->window,L"ControlPanelStyle");glass=c.theme!=nullptr;}
    if(glass){const DWORD mainWindow=2;const MARGINS margins{-1,-1,-1,-1};glass=SUCCEEDED(c.set(s->window,static_cast<DWMWINDOWATTRIBUTE>(38),&mainWindow,sizeof(mainWindow)))&&SUCCEEDED(c.extend(s->window,&margins));}
    FrameMode(s,glass);
}
static void Paint(const std::shared_ptr<State>& s,HDC destination){
    RECT area{};GetClientRect(s->window,&area);if(area.right<=0||area.bottom<=0)return;Rows(s);s->hits.clear();
    struct Surface {Composition& composition;HDC target,dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;HPAINTBUFFER buffer=nullptr;bool commit=true;
        Surface(Composition& c,HDC target):composition(c),target(target){}
        ~Surface(){if(buffer)composition.end(buffer,commit);else if(dc){if(bitmap){if(commit) {RECT r{};GetClipBox(target,&r);BitBlt(target,r.left,r.top,r.right-r.left,r.bottom-r.top,dc,r.left,r.top,SRCCOPY);}SelectObject(dc,previous);DeleteObject(bitmap);}DeleteDC(dc);}}
    } surface(s->composition,destination);
    if(s->glass){BP_PAINTPARAMS params{};params.cbSize=sizeof(params);params.dwFlags=BPPF_ERASE;
        surface.buffer=s->composition.begin(destination,&area,BPBF_TOPDOWNDIB,&params,&surface.dc);
        if(!surface.buffer||!surface.dc){if(surface.buffer){s->composition.end(surface.buffer,FALSE);surface.buffer=nullptr;}surface.dc=nullptr;FrameMode(s,false);GetClientRect(s->window,&area);}}
    if(!surface.buffer){surface.dc=CreateCompatibleDC(destination);if(surface.dc)surface.bitmap=CreateCompatibleBitmap(destination,area.right,area.bottom);
        if(!surface.dc||!surface.bitmap){if(surface.dc){DeleteDC(surface.dc);surface.dc=nullptr;}FillRect(destination,&area,s->backgroundBrush);return;}
        surface.previous=SelectObject(surface.dc,surface.bitmap);FillRect(surface.dc,&area,s->backgroundBrush);}
    const auto dc=surface.dc;SetBkMode(dc,TRANSPARENT);bool textFailed=false;
    const auto draw=[&](std::wstring_view text,RECT r,HFONT font,COLORREF color){if(text.empty()||r.right<=r.left||r.bottom<=r.top)return;SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));SetTextColor(dc,color);
        const DWORD flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX;
        if(surface.buffer){DTTOPTS options{};options.dwSize=sizeof(options);options.dwFlags=DTT_COMPOSITED|DTT_TEXTCOLOR;options.crText=color;
            if(FAILED(s->composition.text(s->composition.theme,dc,0,0,text.data(),static_cast<int>(text.size()),flags,&r,&options)))textFailed=true;
        }else DrawTextW(dc,text.data(),static_cast<int>(text.size()),&r,flags);};
    const int top=ContentTop(*s);
    if(s->glass){const int iconSize=D(*s,16),inset=(std::max)(0,(top-iconSize)/2);HICON icon=reinterpret_cast<HICON>(SendMessageW(g_hwnd,WM_GETICON,ICON_SMALL2,0));
        if(!icon)icon=reinterpret_cast<HICON>(GetClassLongPtrW(g_hwnd,GCLP_HICONSM));if(!icon)icon=LoadIconW(nullptr,IDI_APPLICATION);
        if(icon)DrawIconEx(dc,inset,inset,icon,iconSize,iconSize,0,nullptr,DI_NORMAL);
        int right=area.right-D(*s,15);if(s->captionButtons.right>s->captionButtons.left)right=(std::min)(right,static_cast<int>(s->captionButtons.left)-D(*s,8));
        RECT title{inset+iconSize+inset/2,0,right,top};std::wstring caption=Text(L"SettingsWindowHeading",L"Settings");
        if(!s->path.empty()){auto* nodes=s->section==0?&s->root:&s->globalRows;for(const auto index:s->path){if(index>=nodes->size())break;caption+=L" / "+(*nodes)[index].label;nodes=&(*nodes)[index].children;}}
        draw(caption,title,s->heading,s->foreground);}
    const int bottom=static_cast<int>(area.bottom)-D(*s,38),rowHeight=D(*s,26),navHeight=D(*s,32);
    const int visible=VisibleRows(*s,area.bottom);s->scroll=(std::max)(0,(std::min)(s->scroll,static_cast<int>(s->rows.size())-visible));
    SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;scroll.nMax=(std::max)(0,static_cast<int>(s->rows.size())-1);scroll.nPage=visible;scroll.nPos=s->scroll;SetScrollInfo(s->window,SB_VERT,&scroll,TRUE);
    for(int index=s->navScroll;index<static_cast<int>(s->navigation.size());++index){RECT r{D(*s,15),top+(index-s->navScroll)*navHeight,D(*s,165),top+(index-s->navScroll+1)*navHeight};if(r.bottom>bottom)break;
        const auto& nav=s->navigation[index];const bool selected=nav.section==s->section&&(nav.index<0?s->path.empty():!s->path.empty()&&s->path[0]==static_cast<std::size_t>(nav.index));
        draw(nav.label,r,selected?s->selectedFont:s->sectionFont,s->foreground);s->hits.push_back({r,true,index});}
    for(int index=s->scroll;index<static_cast<int>(s->rows.size());++index){RECT r{D(*s,195),top+(index-s->scroll)*rowHeight,area.right-D(*s,15),top+(index-s->scroll+1)*rowHeight};if(r.bottom>bottom)break;
        const auto& node=*s->rows[index];auto label=node.label;
        if(node.kind==Kind::Toggle)label=std::wstring(node.checked?L"\u2714  ":L"\u274c  ")+label;
        else if(node.kind==Kind::Action&&node.checked)label=L"\u2714  "+label;
        if(node.kind==Kind::Choice){for(const auto& choice:node.children)if(choice.checked){label+=L" : "+choice.label;break;}label+=L" \u25be";}
        if(node.kind==Kind::Page)label+=L" \u203a";
        const auto color=!node.enabled?GetSysColor(COLOR_GRAYTEXT):index==s->focus?(s->contrast?GetSysColor(COLOR_HOTLIGHT):s->dark?RGB(255,150,150):RGB(255,0,0)):s->foreground;
        draw(label,r,s->regular,color);if(node.enabled&&node.kind!=Kind::Info)s->hits.push_back({r,false,index});
    }
    const RECT footer{D(*s,15),bottom+D(*s,6),area.right-D(*s,15),area.bottom-D(*s,6)};draw(s->actionFailed?std::wstring_view(FailureText):std::wstring_view(s->statusText),footer,s->regular,s->foreground);
    // The STATIC remains a name-change accessibility target. Its text is
    // painted here so an ordinary child control cannot cover Mica with RGB.
    if(textFailed){surface.commit=false;FrameMode(s,false);InvalidateRect(s->window,nullptr,FALSE);}
}
static void Apply(const std::shared_ptr<State>& s,Node node,const std::wstring* replacement=nullptr){
    if(!RevisionMatches(s))return;auto command=node.command;
    struct SecretCommand{TileInstanceMenuCommand& command;~SecretCommand(){if(command.secret&&!command.value.empty())SecureZeroMemory(&command.value[0],command.value.size()*sizeof(wchar_t));}} sensitive{command};
    if(replacement){command.value=*replacement;command.verb=TileInstanceMenuVerb::Set;}
    else if(node.kind==Kind::Toggle&&node.typed){command.value=node.checked?L"0":L"1";command.verb=TileInstanceMenuVerb::Set;}
    // Regular field writes keep the cooperating INI mutex through the existing
    // validated dispatcher. Windows mutex ownership is recursive on this UI
    // thread. Interactive migration/account actions retain their own guards.
    std::unique_ptr<aip::IniWriteMutexGuard> lease;
    if((node.typed&&command.verb==TileInstanceMenuVerb::Set&&aip::content::Lower(command.key)!=L"delivery.mode")||
        (!node.typed&&node.global&&(GlobalToggle(node.global)||GlobalSettingChoice(node.global)))){
        lease=std::make_unique<aip::IniWriteMutexGuard>(s->profile,CurrentIniWriteLockWaitMs());if(!lease->IsLocked()||!RevisionMatches(s))return;
    }
    if(node.typed&&command.verb==TileInstanceMenuVerb::SignIn){const auto tile=std::find_if(s->tiles.begin(),s->tiles.end(),[&](const Tile& t){return t.value.id==command.id;});
        if(tile!=s->tiles.end()&&tile->value.type==aip::content::TileInstanceType::Calendar){OpenGoogleCalendarSetup(s->window,command.id);return;}
        if(tile!=s->tiles.end()&&tile->value.type==aip::content::TileInstanceType::Wuwa){OpenKuroSignInForTile(s->window,command.id);return;}}
    struct DisableWindow{std::shared_ptr<State> state;BOOL enabled;DisableWindow(std::shared_ptr<State> s):state(std::move(s)),enabled(IsWindowEnabled(state->window)){EnableWindow(state->window,FALSE);}
        ~DisableWindow(){if(current==state&&!state->closed&&IsWindow(state->window))EnableWindow(state->window,enabled);}} disabled(s);
    if(node.typed){RegistryScope registry;const auto id=RegisterTileInstanceCommand(command);if(id)DispatchTileInstanceCommand(g_hwnd,id);}
    else if(node.global){WordingScope wording;g_menuWordingDefaults=s->wording;DispatchTrayCommand(g_hwnd,node.global);}
    lease.reset();Reload(s);Status(s,Text(L"SettingsWindowApplied",L"Settings refreshed. Any requested confirmation must finish before its change is applied."));
}
static void Activate(const std::shared_ptr<State>& s,int index){
    Rows(s);if(index<0||static_cast<std::size_t>(index)>=s->rows.size())return;const Node node=*s->rows[index];if(!node.enabled)return;
    if(node.kind==Kind::Info)return;
    if(node.kind==Kind::Page){auto& page=Page(*s);for(std::size_t i=0;i<page.size();++i)if(&page[i]==s->rows[index]){s->path.push_back(i);break;}s->focus=s->scroll=0;InvalidateRect(s->window,nullptr,FALSE);return;}
    if(node.kind==Kind::Choice){HMENU menu=CreatePopupMenu();if(!menu)return;for(std::size_t i=0;i<node.children.size();++i)aip::AppendTrayMenuItem(menu,static_cast<UINT>(i+1),node.children[i].label,node.children[i].checked,node.children[i].enabled);
        POINT point{D(*s,195),ContentTop(*s)+D(*s,(index-s->scroll+1)*26)};ClientToScreen(s->window,&point);const auto chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,s->window,nullptr);DestroyMenu(menu);
        if(chosen&&chosen<=node.children.size())Apply(s,node.children[chosen-1]);return;}
    if(node.kind==Kind::Edit){
        if(!RevisionMatches(s))return;
        auto value=node.command.value;const auto tile=std::find_if(s->tiles.begin(),s->tiles.end(),[&](const Tile& entry){return entry.value.id==node.command.id;});
        const auto title=tile==s->tiles.end()?Text(L"SettingsWindowTitle",L"DesktopStub Properties"):tile->value.name;
        struct SecretBuffer{std::wstring& text;bool secret;~SecretBuffer(){if(secret&&!text.empty())SecureZeroMemory(&text[0],text.size()*sizeof(wchar_t));}} buffer{value,node.command.secret};
        if(ShowTextInputDialog(s->window,title,node.command.prompt,value,true,node.command.secret)==IDOK&&RevisionMatches(s))Apply(s,node,&value);
        return;
    }
    if(node.kind==Kind::Special){
        if(node.special==ReloadPage){Reload(s);Status(s,Text(L"SettingsWindowReloaded",L"Settings reloaded."));return;}
        if(node.special==Back){if(!s->path.empty())s->path.pop_back();s->focus=s->scroll=0;InvalidateRect(s->window,nullptr,FALSE);return;}
        if(node.special==OpenIni){ShellExecuteW(s->window,L"open",s->profile.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(node.special==Contents){ShowPagedTextDialog(s->window,L"All tile contents",BuildCurrentTileStatusPages());return;}
        HMENU menu=CreatePopupMenu();if(!menu)return;
        if(node.special==SelectTile)for(std::size_t i=0;i<s->tiles.size();++i)aip::AppendTrayMenuItem(menu,static_cast<UINT>(i+1),s->tiles[i].value.name,s->tiles[i].value.id==s->tileId,true);
        else for(std::size_t i=0;i<std::size(aip::content::TileInstanceTypes);++i)aip::AppendTrayMenuItem(menu,static_cast<UINT>(i+1),aip::content::TileInstanceTypes[i].name,false,true);
        POINT point{D(*s,195),ContentTop(*s)+D(*s,(index-s->scroll+1)*26)};ClientToScreen(s->window,&point);const auto chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,s->window,nullptr);DestroyMenu(menu);if(!chosen)return;
        if(node.special==SelectTile&&chosen<=s->tiles.size()){s->tileId=s->tiles[chosen-1].value.id;Reload(s,false);}
        else if(node.special==AddTile&&chosen<=std::size(aip::content::TileInstanceTypes)&&RevisionMatches(s)){
            const auto& type=aip::content::TileInstanceTypes[chosen-1];if(type.type==aip::content::TileInstanceType::Gallery){Node add;add.kind=Kind::Action;add.global=ID_GALLERY_ADD;Apply(s,add);}else AddIndependentTile(s->window,type.value);Reload(s);}
        return;
    }
    Apply(s,node);
}
static void Navigate(const std::shared_ptr<State>& s,int index){if(index<0||static_cast<std::size_t>(index)>=s->navigation.size())return;const auto nav=s->navigation[index];
    s->section=nav.section;s->path.clear();if(nav.index>=0)s->path.push_back(static_cast<std::size_t>(nav.index));s->focus=s->scroll=0;GlobalRows(s);RebuildNavigation(s);InvalidateRect(s->window,nullptr,FALSE);}
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) noexcept {
    try{const auto s=current;if(!s||s->window!=window)return DefWindowProcW(window,message,wParam,lParam);
        if(s->glass&&(message==WM_NCHITTEST||message==WM_NCMOUSELEAVE||message==WM_NCMOUSEMOVE||message==WM_NCLBUTTONDOWN||message==WM_NCLBUTTONUP||message==WM_NCACTIVATE||message==WM_NCPAINT)){
            LRESULT result=0;if(s->composition.def(window,message,wParam,lParam,&result))return result;}
        if(s->glass&&message==WM_NCCALCSIZE&&wParam){auto info=reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);const auto proposed=info->rgrc[0];
            const int x=IsZoomed(window)?WindowMetric(*s,SM_CXSIZEFRAME)+WindowMetric(*s,SM_CXPADDEDBORDER):D(*s,2);
            const int y=IsZoomed(window)?WindowMetric(*s,SM_CYSIZEFRAME)+WindowMetric(*s,SM_CXPADDEDBORDER):D(*s,2);
            info->rgrc[0].left=proposed.left+x;info->rgrc[0].right=proposed.right-x;
            info->rgrc[0].top=proposed.top+(IsZoomed(window)?y:0);info->rgrc[0].bottom=proposed.bottom-y;
            if(GetWindowLongPtrW(window,GWL_STYLE)&WS_VSCROLL)info->rgrc[0].right-=WindowMetric(*s,SM_CXVSCROLL);
            return 0;}
        if(s->glass&&message==WM_NCHITTEST){POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};RECT bounds{};GetWindowRect(window,&bounds);
            if(!IsZoomed(window)){const int x=WindowMetric(*s,SM_CXSIZEFRAME)+WindowMetric(*s,SM_CXPADDEDBORDER),y=WindowMetric(*s,SM_CYSIZEFRAME)+WindowMetric(*s,SM_CXPADDEDBORDER);
                const bool left=point.x<bounds.left+x,right=point.x>=bounds.right-x,top=point.y<bounds.top+y,bottom=point.y>=bounds.bottom-y;
                if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;if(left)return HTLEFT;if(right)return HTRIGHT;}
            ScreenToClient(window,&point);const int icon=D(*s,16),inset=(std::max)(0,(ContentTop(*s)-icon)/2);
            const RECT iconArea{inset,inset,inset+icon,inset+icon};if(PtInRect(&iconArea,point))return HTSYSMENU;
            if(point.y>=0&&point.y<ContentTop(*s))return HTCAPTION;
            return DefWindowProcW(window,message,wParam,lParam);}
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);
            struct End{HWND window;PAINTSTRUCT& paint;~End(){EndPaint(window,&paint);}} end{window,paint};if(dc)Paint(s,dc);return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_CTLCOLORSTATIC){const auto dc=reinterpret_cast<HDC>(wParam);SetTextColor(dc,s->foreground);SetBkColor(dc,s->background);return reinterpret_cast<LRESULT>(s->backgroundBrush);}
        if(message==WM_GETMINMAXINFO){auto info=reinterpret_cast<MINMAXINFO*>(lParam);info->ptMinTrackSize={D(*s,600),D(*s,430)};return 0;}
        if(message==WM_SIZE){CaptionMetrics(s);InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_THEMECHANGED||message==WM_SETTINGCHANGE||message==WM_DWMCOMPOSITIONCHANGED){Theme(s);Fonts(s);InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_ACTIVATE){Theme(s);InvalidateRect(window,nullptr,FALSE);}
        if(message==WM_DPICHANGED){s->dpi=HIWORD(wParam);Fonts(s);const auto r=reinterpret_cast<const RECT*>(lParam);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);Theme(s);return 0;}
        if(message==WM_LBUTTONUP){SetFocus(window);POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};
            for(std::size_t i=0;i<s->hits.size();++i)if(PtInRect(&s->hits[i].area,point)){const auto hit=s->hits[i];const auto now=GetTickCount64();
                if(s->lastHit==static_cast<int>(i)&&now-s->lastClick<GetDoubleClickTime())return 0;s->lastHit=static_cast<int>(i);s->lastClick=now;
                if(hit.navigation)Navigate(s,hit.index);else{s->focus=hit.index;Activate(s,hit.index);}InvalidateRect(window,nullptr,FALSE);break;}return 0;}
        if(message==WM_MOUSEWHEEL||message==WM_VSCROLL){RECT area{};GetClientRect(window,&area);const int visible=VisibleRows(*s,area.bottom);
            if(message==WM_MOUSEWHEEL){POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};ScreenToClient(window,&point);const int amount=static_cast<short>(HIWORD(wParam))>0?-3:3;
                if(point.x<D(*s,180))s->navScroll=(std::max)(0,(std::min)(static_cast<int>(s->navigation.size())-1,s->navScroll+amount));else s->scroll+=amount;
            }else{switch(LOWORD(wParam)){case SB_LINEUP:--s->scroll;break;case SB_LINEDOWN:++s->scroll;break;case SB_PAGEUP:s->scroll-=visible;break;case SB_PAGEDOWN:s->scroll+=visible;break;
                case SB_THUMBTRACK:{SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_TRACKPOS;GetScrollInfo(window,SB_VERT,&scroll);s->scroll=scroll.nTrackPos;break;}}}
            InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_KEYDOWN){Rows(s);if(wParam==VK_F5){Reload(s);return 0;}if(wParam==VK_ESCAPE){if(!s->path.empty()){s->path.pop_back();s->focus=s->scroll=0;InvalidateRect(window,nullptr,FALSE);}else DestroyWindow(window);return 0;}
            if(wParam==VK_RETURN||wParam==VK_SPACE){Activate(s,s->focus);return 0;}
            if(wParam==VK_TAB||wParam==VK_DOWN||wParam==VK_UP||wParam==VK_HOME||wParam==VK_END){const int count=static_cast<int>(s->rows.size());if(!count)return 0;
                s->focus=wParam==VK_HOME?0:wParam==VK_END?count-1:(s->focus+((wParam==VK_UP||(wParam==VK_TAB&&(GetKeyState(VK_SHIFT)&0x8000)))?-1:1)+count)%count;
                RECT area{};GetClientRect(window,&area);const auto visible=VisibleRows(*s,area.bottom);if(s->focus<s->scroll)s->scroll=s->focus;else if(s->focus>=s->scroll+visible)s->scroll=s->focus-visible+1;
                Status(s,s->rows[s->focus]->label);InvalidateRect(window,nullptr,FALSE);return 0;}
            if(wParam==VK_LEFT||wParam==VK_RIGHT){int selected=0;for(std::size_t i=0;i<s->navigation.size();++i)if(s->navigation[i].section==s->section&&s->navigation[i].index==(s->path.empty()?-1:static_cast<int>(s->path[0])))selected=static_cast<int>(i);
                Navigate(s,(selected+(wParam==VK_LEFT?-1:1)+static_cast<int>(s->navigation.size()))%static_cast<int>(s->navigation.size()));return 0;}}
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){s->closed=true;current.reset();}
    }catch(...){const auto s=current;if(s&&!s->closed){s->actionFailed=true;
        // This exception boundary must not allocate a second diagnostic string.
        try{if(s->status){SetWindowTextW(s->status,FailureText);NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,s->status,OBJID_CLIENT,CHILDID_SELF);}InvalidateRect(window,nullptr,FALSE);}catch(...){}
        }return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
}
static bool OpenDesktopStubSettings(HWND owner) try {
    using namespace desktop_settings;if(current&&IsWindow(current->window)){ShowWindow(current->window,SW_RESTORE);SetForegroundWindow(current->window);return true;}
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"DesktopStubNativeSettings";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;auto s=std::make_shared<State>();current=s;
    using GetDpi=UINT(WINAPI*)(HWND);const auto get=reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));if(get&&owner)s->dpi=get(owner);
    else {const auto dc=GetDC(owner);if(dc){s->dpi=static_cast<UINT>(GetDeviceCaps(dc,LOGPIXELSX));ReleaseDC(owner,dc);}}if(!s->dpi)s->dpi=96;
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);if(!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor))SystemParametersInfoW(SPI_GETWORKAREA,0,&monitor.rcWork,0);const auto work=monitor.rcWork;
    const int width=(std::min)(D(*s,810),static_cast<int>(work.right-work.left)),height=(std::min)(D(*s,640),static_cast<int>(work.bottom-work.top));
    s->window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,Text(L"SettingsWindowTitle",L"DesktopStub Properties").c_str(),WS_OVERLAPPEDWINDOW|WS_VSCROLL,
        work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,owner,nullptr,cls.hInstance,nullptr);
    if(!s->window){current.reset();return false;}s->status=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_NOPREFIX,0,0,0,0,s->window,nullptr,cls.hInstance,nullptr);
    if(!s->status){DestroyWindow(s->window);return false;}Theme(s);Fonts(s);Reload(s);ShowWindow(s->window,SW_SHOW);SetForegroundWindow(s->window);SetFocus(s->window);return true;
}catch(...){if(desktop_settings::current&&desktop_settings::current->window)DestroyWindow(desktop_settings::current->window);desktop_settings::current.reset();return false;}
static void CloseDesktopStubSettings(){if(desktop_settings::current&&IsWindow(desktop_settings::current->window))DestroyWindow(desktop_settings::current->window);}
static bool IsDesktopStubSettingsMessage(MSG&){return false;}
#else
static bool OpenDesktopStubSettings(HWND){return false;}
static void CloseDesktopStubSettings(){}
static bool IsDesktopStubSettingsMessage(MSG&){return false;}
#endif

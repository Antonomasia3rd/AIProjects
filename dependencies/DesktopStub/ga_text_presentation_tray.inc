static constexpr UINT ID_TEXT_PRESENTATION = 55000, TEXT_PRESENTATION_STRIDE = 128;
static void AppendTextPresentationMenu(HMENU parent, const std::wstring& section, UINT slot)
{
    const UINT base = ID_TEXT_PRESENTATION + slot * TEXT_PRESENTATION_STRIDE;
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    const auto preset = IniReadS(section.c_str(), L"TextPreset", slot ? L"Inherit" : L"Default");
    const wchar_t* presets[] = {L"Default",L"Custom",L"RssHeadlines",L"Inherit"};
    for (UINT i=0; i<(slot?4u:3u); ++i) aip::AppendTrayMenuItem(menu,base+i,presets[i],IEquals(preset,presets[i]),true);
    const wchar_t* keys[] = {L"TextStyle",L"TextSecondaryStyle",L"TextBadgeStyle"};
    const wchar_t* labels[] = {L"Primary text style",L"Secondary text style",L"Badge text style"};
    for (UINT role=0;role<3;++role) {
        HMENU styles=aip::BeginTrayNestedMenu(menu);
        for (UINT i=0;i<_countof(aip::content::TextPresentationStyles);++i) aip::AppendTrayMenuItem(styles,base+16+role*17+i,
            aip::content::TextPresentationStyles[i],IEquals(IniReadS(section.c_str(),keys[role],L""),aip::content::TextPresentationStyles[i]),true);
        aip::EndTrayNestedMenu(menu,styles,IniReadS(L"Strings",keys[role],labels[role]));
    }
    const wchar_t* controls[]={L"TextMaxLines",L"TextSecondaryMaxLines",L"TextBadgeMaxLines",L"TextGapLines"};
    for(UINT i=0;i<4;++i)aip::AppendTrayMenuItem(menu,base+80+i,IniReadS(L"Strings",controls[i],controls[i])+L"...",false,true);
    aip::AppendTrayMenuItem(menu,base+84,IniReadS(L"Strings",L"TextWrap",L"Wrap text"),IniReadS(section.c_str(),L"TextWrap",L"1")==L"1",true);
    for(UINT i=0;i<3;++i) {
        const wchar_t* align[]={L"left",L"center",L"right"}; const wchar_t* stack[]={L"top",L"center",L"bottom"};
        aip::AppendTrayMenuItem(menu,base+85+i,L"Align: "+std::wstring(align[i]),false,true);
        aip::AppendTrayMenuItem(menu,base+88+i,L"Stack: "+std::wstring(stack[i]),false,true);
    }
    aip::EndTrayNestedMenu(parent,menu,IniReadS(L"Strings",L"TextPresentationTitle",L"Text size, wrapping and layout"));
}
static bool DispatchTextPresentationCommand(HWND owner,UINT command)
{
    if(command<ID_TEXT_PRESENTATION || command>=ID_TEXT_PRESENTATION+(aip::content::SecondaryTileCount+1)*TEXT_PRESENTATION_STRIDE)return false;
    const UINT slot=(command-ID_TEXT_PRESENTATION)/TEXT_PRESENTATION_STRIDE,field=(command-ID_TEXT_PRESENTATION)%TEXT_PRESENTATION_STRIDE;
    const auto section=slot?aip::content::SecondaryTileSection(aip::content::SecondaryTiles[slot-1].id):std::wstring(L"TileText");
    if(field<4) {const wchar_t* presets[]={L"Default",L"Custom",L"RssHeadlines",L"Inherit"};if(field==3&&!slot)return false;SaveContentMenuSettings(owner,{{section,L"TextPreset",presets[field]}});return true;}
    std::wstring key,value;
    if(field>=16&&field<67){const wchar_t* keys[]={L"TextStyle",L"TextSecondaryStyle",L"TextBadgeStyle"};key=keys[(field-16)/17];value=aip::content::TextPresentationStyles[(field-16)%17];}
    else if(field>=80&&field<84){const wchar_t* keys[]={L"TextMaxLines",L"TextSecondaryMaxLines",L"TextBadgeMaxLines",L"TextGapLines"};key=keys[field-80];value=IniReadS(section.c_str(),key.c_str(),field==83?L"0":L"2");if(ShowTextInputDialog(owner,L"Text layout",key,value,true)!=IDOK)return true;}
    else if(field==84){key=L"TextWrap";value=IniReadS(section.c_str(),key.c_str(),L"1")==L"1"?L"0":L"1";}
    else if(field>=85&&field<88){const wchar_t* choices[]={L"left",L"center",L"right"};key=L"TextAlign";value=choices[field-85];}
    else if(field>=88&&field<91){const wchar_t* choices[]={L"top",L"center",L"bottom"};key=L"TextStacking";value=choices[field-88];}
    else return false;
    auto preset=IniReadS(section.c_str(),L"TextPreset",L"Custom");if(preset!=L"RssHeadlines")preset=L"Custom";
    SaveContentMenuSettings(owner,{{section,L"TextPreset",preset},{section,key,value}});return true;
}

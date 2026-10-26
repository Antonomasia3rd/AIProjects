#pragma once
// Portable preference policy. A requested font is never permission to render
// a missing PUA character: the native adapter still verifies family and glyphs.
#include <cwctype>
#include <string>

namespace aip { namespace tile_visual {
enum class FontPreference { Auto, Mdl2, Fluent, Vector };
inline const wchar_t* FontPreferenceName(FontPreference value) {
    switch(value){case FontPreference::Mdl2:return L"MDL2";case FontPreference::Fluent:return L"Fluent";
    case FontPreference::Vector:return L"Vector";default:return L"Auto";}
}
inline bool ParseFontPreference(std::wstring value,FontPreference& output) {
    const auto first=value.find_first_not_of(L" \t\r\n");
    if(first==std::wstring::npos)return false;
    value=value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);
    for(auto& character:value)character=static_cast<wchar_t>(std::towlower(character));
    if(value==L"auto")output=FontPreference::Auto;
    else if(value==L"mdl2")output=FontPreference::Mdl2;
    else if(value==L"fluent")output=FontPreference::Fluent;
    else if(value==L"vector")output=FontPreference::Vector;
    else return false;
    return true;
}
inline bool NormalizeFontPreference(std::wstring& value,std::wstring& error) {
    FontPreference selected=FontPreference::Auto;
    if(!ParseFontPreference(value,selected)){error=L"Icon font must be Auto, MDL2, Fluent or Vector.";return false;}
    value=FontPreferenceName(selected);error.clear();return true;
}
inline bool PreferFluent(FontPreference preference,unsigned windowsBuild) {
    return preference==FontPreference::Fluent||(preference==FontPreference::Auto&&windowsBuild>=22000);
}
} }

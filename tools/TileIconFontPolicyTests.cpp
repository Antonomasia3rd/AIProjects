#include "../dependencies/tile_visual_assets.h"
#include <iostream>
#include <stdexcept>
static int checks=0;
static void Check(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
int main(){try{
    using namespace aip::tile_visual;
    for(const auto preference:{FontPreference::Auto,FontPreference::Mdl2,FontPreference::Fluent,FontPreference::Vector}){
        auto name=std::wstring(FontPreferenceName(preference));FontPreference parsed=FontPreference::Auto;
        Check(ParseFontPreference(L" \t"+name+L" \r\n",parsed)&&parsed==preference,"each public preference round-trips with ordinary INI whitespace");
        std::wstring error;Check(NormalizeFontPreference(name,error)&&name==FontPreferenceName(preference)&&error.empty(),"normalizer uses one canonical CLI/INI spelling");
    }
    for(const auto* bad:{L"",L" ",L"Segoe UI",L"MDL",L"Fluent|MDL2",L"Auto\nVector"}){
        auto value=std::wstring(bad),original=value;std::wstring error;FontPreference out=FontPreference::Vector;
        Check(!ParseFontPreference(value,out)&&out==FontPreference::Vector,"invalid preference does not mutate caller state");
        Check(!NormalizeFontPreference(value,error)&&value==original&&!error.empty(),"invalid preference is rejected without silently selecting Auto");
    }
    for(const auto& topic:Topics){
        const auto native=NativeGlyphFor(topic.glyph);
        for(const auto build:{0u,7601u,10240u,17763u,19045u,22000u,26100u}){
            Check(SelectFont(topic.glyph,FontPreference::Auto,build,[](FontStyle,wchar_t){return true;})==
                (!native.primary?FontStyle::Vector:build>=22000?FontStyle::Fluent:FontStyle::Mdl2),"Auto selects MDL2 through Windows10 and Fluent on Windows11 using actual build policy");
            for(const auto preference:{FontPreference::Auto,FontPreference::Mdl2,FontPreference::Fluent,FontPreference::Vector}){
                unsigned probes=0;const auto absent=[&](FontStyle,wchar_t){++probes;return false;};
                Check(SelectFont(topic.glyph,preference,build,absent)==FontStyle::Vector,"missing installed fonts/glyphs always have a visible vector backend");
                if(preference==FontPreference::Vector||!native.primary)Check(probes==0,"Vector and vector-only topics never probe native fonts");
            }
        }
        if(!native.primary)continue;
        Check(SelectFont(topic.glyph,FontPreference::Mdl2,26100,[](FontStyle,wchar_t){return true;})==FontStyle::Mdl2,"explicit MDL2 overrides the Windows11 preference");
        Check(SelectFont(topic.glyph,FontPreference::Fluent,17763,[](FontStyle,wchar_t){return true;})==FontStyle::Fluent,"explicit Fluent can be used on Windows10 only if its glyph exists");
        Check(SelectFont(topic.glyph,FontPreference::Mdl2,17763,[](FontStyle font,wchar_t){return font==FontStyle::Fluent;})==FontStyle::Fluent,"missing requested MDL2 safely tries verified Fluent before Vector");
        Check(SelectFont(topic.glyph,FontPreference::Fluent,26100,[](FontStyle font,wchar_t){return font==FontStyle::Mdl2;})==FontStyle::Mdl2,"missing requested Fluent safely tries verified MDL2 before Vector");
        if(native.secondary)for(const auto preference:{FontPreference::Auto,FontPreference::Mdl2,FontPreference::Fluent})
            Check(SelectFont(topic.glyph,preference,17763,[&](FontStyle,wchar_t glyph){return glyph!=native.secondary;})==FontStyle::Vector,"a composite native icon requires every component in the selected font");
    }
    Check(NativeGlyphFor(Glyph::Prayer).primary==0xe708,"Prayer references the documented QuietHours crescent, not a newer unknown glyph");
    std::cout<<checks<<" icon font policy checks; 0 failures\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}}

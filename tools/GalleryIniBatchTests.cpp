#include "../dependencies/gallery_ini_batch.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void Check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
void Equivalent(const std::wstring& original,const std::vector<aip::IniSetting>& fields){auto slow=original,fast=original;for(const auto& f:fields)Check(aip::WriteIniValueToText(slow,f.section.c_str(),f.key.c_str(),f.value),"reference setting valid");Check(aip::gallery::ApplySettingsToText(fast,fields),"batch valid");if(slow!=fast){std::wcerr<<L"REFERENCE:\n"<<slow<<L"\nBATCH:\n"<<fast<<L'\n';throw std::runtime_error("batch preserves exact reference writer result");}++checks;}
}
int wmain(){try{
    const std::wstring fixture=L"; untouched comment\r\n[Other]\r\nLiteral = keep me\r\n\r\n[Gallery.Tile.1]\r\nId = old\r\n; row comment\r\nName=old\r\n\r\n[Untouched]\r\nValue=keep\r\n[gallery.tile.1]\r\nNAME = duplicate\r\n";
    const std::vector<aip::IniSetting> fields={{L"Gallery.Tile.1",L"Name",L"New"},{L"Gallery.Tile.1",L"Cache.Enabled",L"1"},{L"Gallery.Tile.2",L"Id",L"tile2"},{L"Gallery.Tile.2",L"Name",L"Second"},{L"Gallery.Tile.1",L"Delivery.Mode",L"Auto"}};
    Equivalent(fixture,fields);Equivalent(L"",fields);Equivalent(L"\xFEFF"+fixture+L"\r\n\r\n",fields);
    Equivalent(L"[A]\nX=old\n\n; final comment\n\n",{{L"A",L"Y",L"new"}});
    Equivalent(fixture,{{L"Gallery.Tile.1",L"Name",L"First"},{L"gallery.tile.1",L"NAME",L"Last"}});
    Equivalent(L"[A]\r\nx=1\r\n[A]\r\ny=2\r\n[B]\r\n",{{L"A",L"Y",L"new"},{L"A",L"Z",L"append first section"}});
    for(int seed=0;seed<100;++seed){std::wstring text=L"; preserve\n";for(int n=0;n<12;++n){const auto section=std::to_wstring((n*7+seed)%5);text+=L"[S"+section+L"]\n; note\nK"+std::to_wstring((n+seed)%4)+L"=old\n\n";}std::vector<aip::IniSetting> changes;for(int n=0;n<20;++n)changes.push_back({L"S"+std::to_wstring((n*3+seed)%7),L"K"+std::to_wstring((n*5+seed)%6),L"value="+std::to_wstring(n)});Equivalent(text,changes);}
    auto original=fixture;Check(!aip::gallery::ApplySettingsToText(original,{{L"Gallery",L"Name",L"bad\nvalue"}})&&original==fixture,"invalid batch leaves exact document unchanged");
    std::vector<aip::IniSetting> many;for(int row=1;row<=28;++row)for(int field=0;field<70;++field)many.push_back({L"Gallery.Tile."+std::to_wstring(row),L"Field."+std::to_wstring(field),L"value"});
    auto slow=std::wstring(L"; preserve unrelated text\n[Account]\nSecret=synthetic-only\n"),fast=slow;
    const auto started=std::chrono::steady_clock::now();for(const auto& f:many)Check(aip::WriteIniValueToText(slow,f.section.c_str(),f.key.c_str(),f.value),"large reference valid");const auto referenceFinished=std::chrono::steady_clock::now();Check(aip::gallery::ApplySettingsToText(fast,many)&&fast==slow,"28 tile bulk result is byte-exact reference equivalent");const auto batchFinished=std::chrono::steady_clock::now();std::cout<<"28-tile serialization: reference "<<std::chrono::duration_cast<std::chrono::microseconds>(referenceFinished-started).count()<<" us; batch "<<std::chrono::duration_cast<std::chrono::microseconds>(batchFinished-referenceFinished).count()<<" us\n";
    std::cout<<checks<<" gallery batch INI checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<" after "<<checks<<" checks\n";return 1;}}

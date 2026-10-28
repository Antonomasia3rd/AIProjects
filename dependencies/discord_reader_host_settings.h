#pragma once
#include "discord_tile_model.h"
namespace aip { namespace discord_tiles {
inline bool HostSetting(const std::wstring& key){const auto k=Lower(key);return k==L"credentialprotected"||k==L"credentialconnection"||k==L"credentialrevision"||k==L"acknowledgedcredentialrevision";}
inline bool NormalizeHostSetting(const std::wstring& key,std::wstring& value,std::wstring& error){const auto k=Lower(key);value=Trim(value);
    const auto fail=[&]{error=L"DiscordReader."+key+L": invalid protected credential metadata (value not shown).";return false;};
    if(k==L"credentialprotected"){if(value.size()>65536||(!value.empty()&&Lower(value).compare(0,6,L"dpapi:")!=0)||!SafeSingleLine(value,65536))return fail();}
    else if(k==L"credentialconnection"){if(!value.empty()&&value!=L"OAuth"&&value!=L"Bot"&&value!=L"UserTokenExperimental")return fail();}
    else if(k==L"credentialrevision"||k==L"acknowledgedcredentialrevision"){if(!SafeSingleLine(value,512))return fail();}
    else return fail();error.clear();return true;
}
inline bool NormalizeReaderSetting(const std::wstring& key,std::wstring& value,std::wstring& error){return HostSetting(key)?NormalizeHostSetting(key,value,error):NormalizeSetting(key,value,error);}
} }

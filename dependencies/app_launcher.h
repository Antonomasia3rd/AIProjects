#pragma once
// Explicit launch descriptions and artwork plans only. This portable model
// never starts a process, resolves an association or reads an application file.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>
namespace aip { namespace app_launcher {
struct Configuration {
    std::wstring target,arguments,workingDirectory,iconMode=L"Auto",iconPath;
    int iconIndex=0,iconSizePercent=64;
};
struct Field {const wchar_t* key;const wchar_t* fallback;const wchar_t* label;};
inline constexpr Field Fields[]={
    {L"Target",L"",L"Program, document, folder or URL"},{L"Arguments",L"",L"Program arguments"},
    {L"WorkingDirectory",L"",L"Working directory (optional)"},
    {L"IconMode",L"Auto",L"Icon source"},{L"IconPath",L"",L"Custom icon/image file"},
    {L"IconIndex",L"0",L"Icon resource index"},{L"IconSizePercent",L"64",L"Icon size (% of shorter tile side)"}
};
inline std::wstring Trim(const std::wstring& text){const auto first=text.find_first_not_of(L" \t\r\n");return first==std::wstring::npos?std::wstring():text.substr(first,text.find_last_not_of(L" \t\r\n")-first+1);}
inline std::wstring Lower(std::wstring text){for(auto& c:text)c=static_cast<wchar_t>(towlower(c));return text;}
inline bool Text(const std::wstring& text,std::size_t limit,bool tabs=false){
    if(text.size()>limit)return false;for(const auto c:text)if((c<0x20&&(!tabs||c!=L'\t'))||c==0x7f)return false;return true;
}
inline bool DrivePath(const std::wstring& text){return text.size()>=3&&((text[0]>=L'A'&&text[0]<=L'Z')||(text[0]>=L'a'&&text[0]<=L'z'))&&text[1]==L':'&&(text[2]==L'\\'||text[2]==L'/');}
inline bool UncPath(const std::wstring& text){return text.size()>4&&text.compare(0,2,L"\\\\")==0&&text.compare(0,4,L"\\\\.\\")!=0;}
inline bool EnvironmentPath(const std::wstring& text){if(text.empty()||text[0]!=L'%')return false;const auto end=text.find(L'%',1);return end>1&&end<80&&end+1<text.size()&&(text[end+1]==L'\\'||text[end+1]==L'/');}
inline bool ExplicitPath(const std::wstring& text){return DrivePath(text)||UncPath(text)||EnvironmentPath(text);}
inline bool Uri(const std::wstring& target){
    const auto colon=target.find(L':');if(colon==std::wstring::npos||colon<2||colon>32)return false;
    for(std::size_t i=0;i<colon;++i){const auto c=target[i];if(!((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(i&&((c>=L'0'&&c<=L'9')||c==L'+'||c==L'-'||c==L'.'))))return false;}
    const auto scheme=Lower(target.substr(0,colon));return scheme!=L"javascript"&&scheme!=L"vbscript"&&scheme!=L"data"&&colon+1<target.size();
}
inline bool SimpleProgram(const std::wstring& target){
    if(target.empty()||target.find_first_of(L"\\/: \t\"<>|?*")!=std::wstring::npos)return false;
    const auto dot=target.find_last_of(L'.');if(dot==std::wstring::npos)return false;
    const auto extension=Lower(target.substr(dot));return extension==L".exe"||extension==L".com";
}
inline bool PathText(const std::wstring& value){return Text(value,32760)&&value.find_first_of(L"\"<>|?*")==std::wstring::npos&&ExplicitPath(value);}
inline const Field* FindField(std::wstring key){key=Lower(Trim(key));if(key.compare(0,9,L"launcher.")==0)key.erase(0,9);for(const auto& field:Fields)if(Lower(field.key)==key)return &field;return nullptr;}
inline bool Number(const std::wstring& text,int minimum,int maximum,int& value){
    const auto input=Trim(text);if(input.empty()||input.size()>12)return false;std::size_t at=0;bool negative=false;
    if(input[0]==L'-'){negative=true;at=1;}if(at==input.size())return false;
    std::int64_t result=0;for(;at<input.size();++at){const auto c=input[at];if(c<L'0'||c>L'9')return false;result=result*10+c-L'0';if(result>2147483648LL)return false;}
    if(negative)result=-result;if(result<minimum||result>maximum)return false;value=static_cast<int>(result);return true;
}
inline bool NormalizeSetting(const std::wstring& key,std::wstring& value,std::wstring& error){
    error.clear();const auto* field=FindField(key);if(!field){error=L"Unknown App launcher setting.";return false;}const auto name=std::wstring(field->key);
    if(name==L"Arguments"){if(!Text(value,16384,true)){error=L"Program arguments must be bounded single-line text.";return false;}return true;}
    value=Trim(value);if(value.size()>1&&value.front()==L'\"'&&value.back()==L'\"')value=value.substr(1,value.size()-2);
    if(name==L"Target"){
        if(!value.empty()&&(!Text(value,32760)||value.find(L'\"')!=std::wstring::npos||(!PathText(value)&&!SimpleProgram(value)&&!Uri(value)))){
            error=L"Choose a full program/document path, a program name ending in .exe/.com, or a URL. Put arguments in Program arguments.";return false;}
    }else if(name==L"WorkingDirectory"||name==L"IconPath"){
        if(!value.empty()&&!PathText(value)){error=L"Use a full file/folder path or a path beginning with an environment variable.";return false;}
    }else if(name==L"IconMode"){
        const auto mode=Lower(value);if(mode==L"auto")value=L"Auto";else if(mode==L"custom")value=L"Custom";else if(mode==L"none")value=L"None";
        else{error=L"Icon source must be Auto, Custom or None.";return false;}
    }else{
        int number=0;if(!Number(value,name==L"IconIndex"?-32768:10,name==L"IconIndex"?32767:100,number)){
            error=name==L"IconIndex"?L"Icon index must be between -32768 and 32767.":L"Icon size must be between 10 and 100 percent.";return false;}
        value=std::to_wstring(number);
    }
    return true;
}
template<class Reader> bool ReadConfiguration(Reader read,Configuration& output,std::wstring& error){
    Configuration value;for(const auto& field:Fields){std::wstring text=read(field.key,field.fallback);if(!NormalizeSetting(field.key,text,error))return false;
        const auto key=std::wstring(field.key);if(key==L"Target")value.target=text;else if(key==L"Arguments")value.arguments=text;
        else if(key==L"WorkingDirectory")value.workingDirectory=text;else if(key==L"IconMode")value.iconMode=text;else if(key==L"IconPath")value.iconPath=text;
        else if(key==L"IconIndex")Number(text,-32768,32767,value.iconIndex);else Number(text,10,100,value.iconSizePercent);}
    if(value.iconMode==L"Custom"&&value.iconPath.empty()){error=L"Choose a custom icon/image file before selecting the Custom icon source.";return false;}
    if(Uri(value.target)&&!value.arguments.empty()){error=L"URL targets cannot have separate program arguments. Include URL parameters in the URL itself.";return false;}
    output=std::move(value);return true;
}
inline std::wstring ConfigurationKey(const Configuration& value){
    std::wstring key=L"AppLauncher1";for(const auto* text:{&value.target,&value.arguments,&value.workingDirectory,&value.iconMode,&value.iconPath})key+=L"|"+std::to_wstring(text->size())+L":"+*text;
    return key+L"|"+std::to_wstring(value.iconIndex)+L"|"+std::to_wstring(value.iconSizePercent);
}
struct LaunchRequest {std::wstring target,arguments,workingDirectory;bool uri=false;};
inline bool BuildLaunchRequest(const Configuration& value,LaunchRequest& output,std::wstring& error){
    Configuration validated;if(!ReadConfiguration([&](const wchar_t* key,const wchar_t*){
        const auto name=std::wstring(key);return name==L"Target"?value.target:name==L"Arguments"?value.arguments:name==L"WorkingDirectory"?value.workingDirectory:
            name==L"IconMode"?value.iconMode:name==L"IconPath"?value.iconPath:std::to_wstring(name==L"IconIndex"?value.iconIndex:value.iconSizePercent);
    },validated,error))return false;
    if(validated.target.empty()){error=L"Choose the program, document, folder or URL for this App launcher tile first.";return false;}
    output={validated.target,validated.arguments,validated.workingDirectory,Uri(validated.target)};return true;
}
} }

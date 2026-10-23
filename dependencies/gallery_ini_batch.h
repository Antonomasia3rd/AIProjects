#pragma once
// Gallery configuration can contain tens of thousands of policy assignments.
// Mutate those in one document pass while retaining the single-setting INI
// writer's duplicate-update and unrelated-line preservation semantics.
#include "desktop_app_baseline.h"
#include <map>
#include <set>

namespace aip { namespace gallery {
struct IniNameLess {
    bool operator()(const std::wstring& a,const std::wstring& b) const {return _wcsicmp(a.c_str(),b.c_str())<0;}
};
inline bool ApplySettingsToText(std::wstring& text,const std::vector<aip::IniSetting>& input) {
    if(input.empty())return true;
    struct Section {std::wstring name;std::vector<std::wstring> order;std::map<std::wstring,aip::IniSetting,IniNameLess> fields;std::map<std::wstring,std::size_t,IniNameLess> first;std::set<std::wstring,IniNameLess> seen;std::size_t firstInput=0;bool present=false,inserted=false;};
    std::vector<Section> sections;std::map<std::wstring,std::size_t,IniNameLess> sectionIndex;
    std::size_t inputIndex=0;for(const auto& setting:input){
        if(!aip::ValidateIniAssignmentInput(setting.section,setting.key,setting.value))return false;
        auto found=sectionIndex.find(setting.section);
        if(found==sectionIndex.end()){found=sectionIndex.emplace(setting.section,sections.size()).first;Section section;section.name=setting.section;section.firstInput=inputIndex;sections.push_back(std::move(section));}
        auto& section=sections[found->second];
        if(section.fields.find(setting.key)==section.fields.end()){section.order.push_back(setting.key);section.first.emplace(setting.key,inputIndex);}
        section.fields[setting.key]=setting;++inputIndex;
    }
    auto lines=aip::SplitIniLines(text);if(!lines.empty())aip::StripLeadingIniBom(lines.front());
    Section* current=nullptr;
    for(const auto& line:lines){std::wstring name;
        if(aip::TryParseIniSectionHeader(line,name)){const auto found=sectionIndex.find(name);current=found==sectionIndex.end()?nullptr:&sections[found->second];if(current)current->present=true;continue;}
        if(!current)continue;const auto trimmed=aip::Trim(line);if(trimmed.empty()||trimmed.front()==L';'||trimmed.front()==L'#')continue;
        const auto eq=line.find(L'=');if(eq==std::wstring::npos)continue;const auto key=aip::ParseIniName(line.substr(0,eq));
        if(current->fields.find(key)!=current->fields.end())current->seen.insert(key);
    }
    std::vector<std::wstring> result;result.reserve(lines.size()+input.size()+sections.size()*2);current=nullptr;
    const auto insertMissing=[&](Section* section){if(!section||section->inserted)return;section->inserted=true;for(const auto& key:section->order)if(!section->seen.count(key)){const auto& setting=section->fields.at(key);result.push_back(aip::FormatIniAssignment(setting.key,setting.value));}};
    for(const auto& line:lines){std::wstring name;
        if(aip::TryParseIniSectionHeader(line,name)){insertMissing(current);const auto found=sectionIndex.find(name);current=found==sectionIndex.end()?nullptr:&sections[found->second];result.push_back(line);continue;}
        if(current){const auto trimmed=aip::Trim(line);const auto eq=line.find(L'=');if(!trimmed.empty()&&trimmed.front()!=L';'&&trimmed.front()!=L'#'&&eq!=std::wstring::npos){const auto found=current->fields.find(aip::ParseIniName(line.substr(0,eq)));if(found!=current->fields.end()){result.push_back(aip::FormatIniAssignment(found->second.key,found->second.value));continue;}}}
        result.push_back(line);
    }
    // For a final section, the single-setting writer trims trailing blank
    // lines before appending missing entries.
    aip::TrimTrailingIniBlankLines(result);
    std::vector<Section*> newSections;for(auto& section:sections)if(!section.present)newSections.push_back(&section);
    const auto finishLastSection=[&](Section* section,std::size_t separatorInput,bool separator){
        if(section&&!section->inserted){for(const auto& key:section->order)if(!section->seen.count(key)&&section->first.at(key)<separatorInput){const auto& setting=section->fields.at(key);result.push_back(aip::FormatIniAssignment(setting.key,setting.value));}}
        if(separator&&!result.empty()&&!result.back().empty())result.push_back(L"");
        if(section&&!section->inserted){for(const auto& key:section->order)if(!section->seen.count(key)&&section->first.at(key)>=separatorInput){const auto& setting=section->fields.at(key);result.push_back(aip::FormatIniAssignment(setting.key,setting.value));}section->inserted=true;}
    };
    finishLastSection(current,newSections.empty()?input.size():newSections.front()->firstInput,!newSections.empty());
    for(std::size_t index=0;index<newSections.size();++index){auto* section=newSections[index];result.push_back(L"["+section->name+L"]");const bool next=index+1<newSections.size();finishLastSection(section,next?newSections[index+1]->firstInput:input.size(),next);}
    aip::TrimTrailingIniBlankLines(result);auto candidate=aip::JoinIniLines(result);if(!candidate.empty())candidate+=L"\r\n";text=std::move(candidate);return true;
}
} }

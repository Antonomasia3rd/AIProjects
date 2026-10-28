#pragma once
// Pure presentation of an already-prepared battery view. This header neither
// queries hardware nor interprets capacities, power policies or display text.
#include "tile_text_presentation.h"
#include "standard_tile_layout.h"
#include <utility>
namespace aip { namespace content {
struct BatteryTileText {
    std::wstring charge,status,runtime,wear,metricLabel,metricValue,details;
    int rightColumnPercent=30;
    bool criticalView=false;
};
// Ordinals deliberately match the shared native field renderer: a fixed unit
// label keeps its caption size while charge, body and metric remain editable.
enum class BatteryFieldRole {Primary=0,Secondary=1,Badge=2,Fixed=3};
struct BatteryPlanText {
    std::wstring text,style=L"caption",align=L"left";
    BatteryFieldRole role=BatteryFieldRole::Secondary;
    int lines=1;bool wrap=true;
};
struct BatteryPlanColumn {unsigned weight=100;std::vector<BatteryPlanText> texts;};
struct BatteryPlanGroup {std::vector<BatteryPlanColumn> columns;};
struct BatteryTilePlan {
    std::vector<BatteryPlanGroup> groups;
    bool smallTile=false;int margin=10,footer=26,columnGap=8;
    std::wstring stacking=L"top";
};
inline BatteryPlanText BatteryField(std::wstring text,const wchar_t* style,BatteryFieldRole role,int lines=1,const wchar_t* align=L"left") {
    BatteryPlanText field;field.text=std::move(text);field.style=style;field.role=role;field.lines=lines;field.align=align;return field;
}
inline const wchar_t* BatteryMetricStyle(const std::wstring& value) {
    return value.size()>4?L"titleNumeral":value.size()>2?L"subheaderNumeral":L"headerNumeral";
}
inline BatteryTilePlan BuildBatteryTilePlan(const BatteryTileText& text,int width,int height,const TextPresentation& presentation={}) {
    BatteryTilePlan plan;if(width<48||height<48)return plan;
    plan.smallTile=width<150||height<150;plan.stacking=presentation.Active()?presentation.stacking:plan.smallTile?L"center":L"top";
    const auto add=[](BatteryPlanColumn& column,BatteryPlanText field){if(!field.text.empty())column.texts.push_back(std::move(field));};
    BatteryPlanGroup top;BatteryPlanColumn main;
    if(plan.smallTile){plan.margin=6;plan.footer=0;
        add(main,BatteryField(text.charge,text.charge.size()>4?L"base":L"titleNumeral",BatteryFieldRole::Primary,1,L"center"));
        if(!main.texts.empty()){top.columns.push_back(std::move(main));plan.groups.push_back(std::move(top));}return plan;
    }
    add(main,BatteryField(text.charge,StatusTilePrimaryStyle(width,height),BatteryFieldRole::Primary));
    if(text.criticalView)add(main,BatteryField(text.runtime,L"captionSubtle",BatteryFieldRole::Secondary));
    add(main,BatteryField(text.status,L"caption",BatteryFieldRole::Secondary,2));
    if(!text.criticalView)add(main,BatteryField(text.runtime,L"captionSubtle",BatteryFieldRole::Secondary));
    // Medium trades its numeral column for useful power/runtime/wear text.
    // Wide/Large keep a separate capacity (or critical-delta) metric column.
    if(height<=200)add(main,BatteryField(text.wear,L"captionSubtle",BatteryFieldRole::Secondary));
    const bool column=width>200&&!text.metricValue.empty();
    if(column){const auto percent=static_cast<unsigned>((std::max)(20,(std::min)(45,text.rightColumnPercent)));
        main.weight=100-percent;BatteryPlanColumn metric;metric.weight=percent;
        add(metric,BatteryField(text.metricLabel,L"captionSubtle",BatteryFieldRole::Fixed,2,L"right"));
        add(metric,BatteryField(text.metricValue,BatteryMetricStyle(text.metricValue),BatteryFieldRole::Badge,1,L"right"));
        top.columns.push_back(std::move(main));top.columns.push_back(std::move(metric));
    }else if(!main.texts.empty())top.columns.push_back(std::move(main));
    if(!top.columns.empty())plan.groups.push_back(std::move(top));
    if(height>200){BatteryPlanGroup bottom;BatteryPlanColumn details;
        add(details,BatteryField(text.wear,L"captionSubtle",BatteryFieldRole::Secondary));
        add(details,BatteryField(text.details,L"captionSubtle",BatteryFieldRole::Secondary,3));
        if(!details.texts.empty()){bottom.columns.push_back(std::move(details));plan.groups.push_back(std::move(bottom));}
    }
    return plan;
}
inline std::wstring BatteryXmlEscape(const std::wstring& text) {
    std::wstring escaped;for(const auto c:text)switch(c){case L'&':escaped+=L"&amp;";break;case L'<':escaped+=L"&lt;";break;
        case L'>':escaped+=L"&gt;";break;case L'\"':escaped+=L"&quot;";break;case L'\'':escaped+=L"&apos;";break;default:escaped+=c;}return escaped;
}
inline void AppendBatteryPlanText(std::wstring& xml,const BatteryPlanText& field,const TextPresentation& presentation) {
    if(field.text.empty())return;const bool editable=presentation.Active()&&field.role!=BatteryFieldRole::Fixed;
    const auto style=!editable?field.style:field.role==BatteryFieldRole::Primary?presentation.primaryStyle:
        field.role==BatteryFieldRole::Badge?presentation.badgeStyle:presentation.secondaryStyle;
    const auto lines=!editable?field.lines:!presentation.wrap?1:field.role==BatteryFieldRole::Primary?presentation.primaryLines:
        field.role==BatteryFieldRole::Badge?presentation.badgeLines:presentation.secondaryLines;
    const bool wrap=editable?presentation.wrap:field.wrap;
    const auto align=presentation.Active()&&field.align!=L"right"?presentation.align:field.align;
    if(editable&&field.role!=BatteryFieldRole::Primary&&presentation.gapLines)
        xml+=L"<text hint-style=\"caption\" hint-minLines=\""+std::to_wstring(presentation.gapLines)+L"\" hint-maxLines=\""+std::to_wstring(presentation.gapLines)+L"\"> </text>";
    xml+=L"<text hint-style=\""+style+L"\" hint-wrap=\""+(wrap?L"true":L"false")+L"\" hint-maxLines=\""+std::to_wstring(lines)+
        L"\" hint-align=\""+align+L"\">"+BatteryXmlEscape(field.text)+L"</text>";
}
inline bool AppendBatteryTileBinding(std::wstring& xml,const wchar_t* binding,int width,int height,const BatteryTileText& text,
    const TextPresentation& presentation={},const std::wstring& background={}) {
    if(!binding)return false;const std::wstring name=binding;
    if(name!=L"TileSmall"&&name!=L"TileMedium"&&name!=L"TileWide"&&name!=L"TileLarge")return false;
    const auto plan=BuildBatteryTilePlan(text,width,height,presentation);if(plan.groups.empty())return false;
    xml+=L"<binding template=\""+name+L"\""+(plan.smallTile?L" branding=\"none\"":L"")+L" hint-textStacking=\""+plan.stacking+L"\">";
    if(!background.empty())xml+=L"<image placement=\"background\" src=\""+BatteryXmlEscape(background)+L"\"/>";
    for(const auto& group:plan.groups){const bool columns=group.columns.size()>1;if(columns)xml+=L"<group>";
        for(const auto& column:group.columns){if(columns)xml+=L"<subgroup hint-weight=\""+std::to_wstring(column.weight)+L"\">";
            for(const auto& field:column.texts)AppendBatteryPlanText(xml,field,presentation);if(columns)xml+=L"</subgroup>";}
        if(columns)xml+=L"</group>";}
    xml+=L"</binding>";return true;
}
}}

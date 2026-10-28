#pragma once
// Shared status-heading defaults traced from Windows Clock's Medium/Wide/Large
// bindings. Image-only tiles have no status heading; RSS/Games and native
// Calendar/Mail retain their own source-specific plans. No settings are written.
#include "tile_text_presentation.h"
namespace aip { namespace content {
inline const wchar_t* StatusTilePrimaryStyle(int width,int height) {
    return height>200?L"subheader":width>200?L"title":L"base";
}
inline int StatusTilePrimaryPixels(int width,int height) {
    return TextStylePixels(StatusTilePrimaryStyle(width,height));
}
inline const wchar_t* StatusTilePrimaryStyle(const std::wstring& binding) {
    return binding==L"TileLarge"?StatusTilePrimaryStyle(310,310):
        binding==L"TileWide"?StatusTilePrimaryStyle(310,150):StatusTilePrimaryStyle(150,150);
}
struct StatusTextBudget {int primary=2,secondary=1,badge=3;};
inline StatusTextBudget StatusMediaTextBudget(int width,int height,bool extras=false) {
    if(height>200)return {2,2,4};
    if(width>200)return extras?StatusTextBudget{1,1,3}:StatusTextBudget{2,1,1};
    return extras?StatusTextBudget{1,1,4}:StatusTextBudget{2,1,3};
}
inline StatusTextBudget StatusMediaTextBudget(const std::wstring& binding,bool extras=false) {
    return binding==L"TileLarge"?StatusMediaTextBudget(310,310,extras):
        binding==L"TileWide"?StatusMediaTextBudget(310,150,extras):StatusMediaTextBudget(150,150,extras);
}
inline int StatusWrappedSecondaryLines(int width,int height) {return height>200?5:width>200?2:4;}
inline int StatusWrappedSecondaryLines(const std::wstring& binding) {return binding==L"TileLarge"?5:binding==L"TileWide"?2:4;}
template<class Frame> bool HasDefaultStatusHeading(const Frame& frame) {
    return !frame.presentation.Active()&&(frame.prayerSource||frame.batterySource||frame.smtcSource||
        (frame.wrapSourceText&&!frame.gameSource&&!frame.calendarSource&&!frame.mailSource));
}
}}

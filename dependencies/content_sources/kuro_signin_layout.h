#pragma once
// Pure geometry for the compact sign-in dialog and optional human challenge.
#include <algorithm>
namespace aip { namespace kuro { namespace signin {
inline constexpr int CompactHeight=300,ExpandedHeight=680,MinimumWidth=640,BrowserTop=240;
struct Box {int x=0,y=0,width=0,height=0;int Right()const{return x+width;}int Bottom()const{return y+height;}};
struct Layout {Box heading,emailLabel,email,passwordLabel,password,login,retry,save,close,roles,status,browser;};
inline Layout Compute(int width,int height,bool challengeVisible) {
    Layout layout;const int half=(width-36)/2;
    layout.heading={12,10,(std::max)(0,width-24),36};
    layout.emailLabel={12,56,80,20};layout.email={100,52,(std::max)(0,half-88),26};
    layout.passwordLabel={half+24,56,80,20};layout.password={half+112,52,(std::max)(0,half-88),26};
    layout.login={12,90,100,30};layout.retry={122,90,185,30};layout.save={317,90,185,30};layout.close={width-97,90,85,30};
    layout.roles={12,132,(std::max)(0,width-24),230};
    layout.status={12,175,(std::max)(0,width-24),challengeVisible?52:(std::max)(44,(std::min)(80,height-187))};
    if(challengeVisible)layout.browser={12,BrowserTop,(std::max)(0,width-24),(std::max)(0,height-BrowserTop-12)};
    return layout;
}
}}}

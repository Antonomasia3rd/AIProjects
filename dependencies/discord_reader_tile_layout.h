#pragma once
// Presentation of prepared account data only. No account, network or send work.
#include "media_tile_layout.h"
#include "discord_tile_model.h"
#include <functional>
namespace aip { namespace content {
struct DiscordAccountGeometry {double margin=10,bodyBottom=0,textWidth=0,avatarX=0,avatarSide=0;unsigned avatarWeight=0,textWeight=100;};
inline DiscordAccountGeometry ComputeDiscordAccountGeometry(int width,int height,bool avatar,int scalePercent=100){
    DiscordAccountGeometry g;g.bodyBottom=(std::max)(0,height-26);const auto full=(std::max)(0,width-20);g.textWidth=full;
    if(!avatar||full<48)return g;
    const auto wanted=(std::min)(48.0*(std::clamp)(scalePercent,25,200)/100.0,full*.35);
    const auto inner=full-8;g.avatarWeight=static_cast<unsigned>((std::max)(1,(std::min)(40,static_cast<int>(wanted*100/inner+.5))));
    g.textWeight=100-g.avatarWeight;g.avatarSide=inner*g.avatarWeight/100;g.textWidth=inner-g.avatarSide;g.avatarX=width-10-g.avatarSide;return g;
}
inline int DiscordReaderBodyLines(int width,int height){return height>200?5:width>200?2:3;}
inline std::wstring DiscordAccountPresentationKey(const Snapshot& f){
    std::wstring key=L"DiscordAccount1:"+std::to_wstring(f.discordProfileSource)+L":"+std::to_wstring(f.discordAvatarScalePercent);
    for(const auto& row:f.discordAccountRows)key+=L":"+std::to_wstring(row.primary.size())+L":"+row.primary;
    key+=L":"+std::to_wstring(f.discordAvatarPath.size())+L":"+f.discordAvatarPath;return key;
}
inline std::size_t DiscordReaderVisibleRows(const Snapshot& f,int width,int height){(void)width;return (std::min<std::size_t>)(f.discordAccountRows.size(),f.discordProfileSource?(height>200?6u:3u):(height>200?3u:1u));}
inline void AppendDiscordReaderText(std::wstring& xml,const std::wstring& text,const TextPresentation& p,unsigned role,int lines,const wchar_t* style){
    if(text.empty())return;const auto selected=p.Active()?(role==0?p.primaryStyle:role==1?p.secondaryStyle:p.badgeStyle):std::wstring(style);
    const auto maximum=p.Active()?(p.wrap?(role==0?p.primaryLines:role==1?p.secondaryLines:p.badgeLines):1):lines;
    xml+=L"<text hint-style=\""+selected+L"\" hint-wrap=\""+std::wstring(p.Active()&&!p.wrap?L"false":L"true")+L"\" hint-maxLines=\""+std::to_wstring(maximum)+L"\">"+MediaXmlEscape(text)+L"</text>";
}
inline bool AppendDiscordAccountTileBinding(std::wstring& xml,const wchar_t* binding,int width,int height,const Snapshot& f,const std::wstring& background,const std::wstring& avatar={}){
    if(!binding||!*binding||!f.discordAccountSource)return false;
    xml+=L"<binding template=\""+std::wstring(binding)+L"\" hint-textStacking=\"top\">";
    if(!background.empty())xml+=L"<image placement=\"background\" src=\""+MediaXmlEscape(background)+L"\"/>";
    if(height<100){xml+=L"</binding>";return true;}
    const auto g=ComputeDiscordAccountGeometry(width,height,f.discordProfileSource&&!avatar.empty(),f.discordAvatarScalePercent);
    if(g.avatarWeight)xml+=L"<group><subgroup hint-weight=\""+std::to_wstring(g.textWeight)+L"\">";
    AppendDiscordReaderText(xml,f.text.primary,f.presentation,0,height>200?2:1,StatusTilePrimaryStyle(width,height));
    AppendDiscordReaderText(xml,f.text.secondary,f.presentation,1,f.discordProfileSource?1:DiscordReaderBodyLines(width,height),L"captionSubtle");
    if(g.avatarWeight)xml+=L"</subgroup><subgroup hint-weight=\""+std::to_wstring(g.avatarWeight)+L"\"><image src=\""+MediaXmlEscape(avatar)+L"\" alt=\"Profile picture\" hint-removeMargin=\"true\"/></subgroup></group>";
    AppendDiscordReaderText(xml,f.text.badge,f.presentation,2,1,L"captionSubtle");
    for(std::size_t i=0;i<DiscordReaderVisibleRows(f,width,height);++i)AppendDiscordReaderText(xml,f.discordAccountRows[i].primary,f.presentation,1,1,L"captionSubtle");
    xml+=L"</binding>";return true;
}
struct DiscordReaderLabels {
    std::wstring profile=L"Discord profile",channel=L"Discord channel",unread=L"Unread",mentions=L"Mentions",localNew=L"New since tile baseline",localMentions=L"Mentions since tile baseline",onlineFriends=L"Online friends",refreshed=L"Refreshed",ago=L"ago";
    std::wstring setupTitle=L"Discord \u2014 connect an account",setupInstructions=L"Choose a tile view and connect in Source settings.",presenceStopped=L"Rich Presence is not running",presenceInstructions=L"Choose a tile view or configure Rich Presence in Source settings.";
};
inline Text DiscordSetupText(bool presence=false,const DiscordReaderLabels& labels={}){
    return {presence?labels.presenceStopped:labels.setupTitle,presence?labels.presenceInstructions:labels.setupInstructions,L""};
}
inline const wchar_t* DiscordPresenceText(discord_tiles::Status status){using S=discord_tiles::Status;switch(status){case S::Online:return L"Online";case S::Idle:return L"Idle";case S::DoNotDisturb:return L"Do not disturb";case S::Offline:return L"Offline";case S::Invisible:return L"Invisible";default:return L"";}}
inline void ProjectDiscordAccount(Snapshot& frame,const discord_tiles::Snapshot& snapshot,const discord_tiles::Configuration& cfg,discord_tiles::Milliseconds now,const DiscordReaderLabels& labels={},const std::function<std::wstring(const discord_tiles::Timestamp&)>& time={}){
    using namespace discord_tiles;frame.discordAccountSource=true;frame.discordProfileSource=cfg.view==L"Profile";frame.discordAvatarScalePercent=cfg.avatarScalePercent;
    frame.wrapSourceText=false;frame.text={};frame.discordAccountRows.clear();
    const auto append=[&](const std::wstring& text){if(!text.empty())frame.discordAccountRows.push_back({text,L"",L""});};
    const auto count=[&](const std::wstring& label,const std::optional<Count>& value){if(value&&(value->exact||value->value))append(label+L": "+(value->exact?L"":L"\u2265")+std::to_wstring(value->value));};
    if(frame.discordProfileSource){
        frame.text.primary=PreferredUserName(snapshot.user.displayName,snapshot.user.username,labels.profile);
        if(cfg.showUsername&&snapshot.user.username)frame.text.secondary=L"@"+*snapshot.user.username;
        if(cfg.showPresence)append(DiscordPresenceText(snapshot.presence.status));
        if(cfg.showMentionCount)count(labels.mentions,snapshot.personalMentions);
        if(cfg.showPlatforms&&snapshot.presence.platforms){std::wstring platforms;const auto part=[&](const wchar_t* name,const std::optional<Status>& status){if(status&&VisibleOnline(*status)){if(!platforms.empty())platforms+=L", ";platforms+=name;}};part(L"Desktop",snapshot.presence.platforms->desktop);part(L"Mobile",snapshot.presence.platforms->mobile);part(L"Web",snapshot.presence.platforms->web);if(!platforms.empty())append(platforms);}
        count(labels.onlineFriends,snapshot.onlineFriends);
        for(const auto& person:snapshot.friends){const auto name=PreferredUserName(person.user.displayName,person.user.username);if(!name.empty())append(name+L" \u2014 "+DiscordPresenceText(person.presence.status));}
    }else{
        frame.text.primary=frame.name.empty()?labels.channel:frame.name;
        if(!snapshot.messages.empty()){const auto& message=snapshot.messages.front();frame.text.secondary=MessagePreview(message,cfg);
            if(cfg.showMessageAuthor&&message.authorName)frame.text.badge=*message.authorName;
            if(cfg.showMessageTime&&message.sentAt&&time){const auto sent=time(*message.sentAt);if(!sent.empty()){if(!frame.text.badge.empty())frame.text.badge+=L" \u00b7 ";frame.text.badge+=sent;}}
        }
        if(snapshot.personalUnread)count(labels.unread,snapshot.personalUnread);else count(labels.localNew,snapshot.localNewMessages);
        if(cfg.showMentionCount){if(snapshot.personalMentions)count(labels.mentions,snapshot.personalMentions);else count(labels.localMentions,snapshot.localMentions);}
    }
    if(cfg.showLastSuccessAge&&snapshot.stale){const auto age=AgeSeconds(snapshot,now);if(age)append(labels.refreshed+L" "+std::to_wstring(*age/60)+L"m "+labels.ago);}
    if(!snapshot.hasSuccess&&!snapshot.capabilities.identity&&snapshot.messages.empty()){
        frame.text=DiscordSetupText(false,labels);frame.discordAccountRows.clear();frame.discordAvatarPath.clear();frame.discordAvatarLease.reset();
    }
}
} }

// Inert projection and native tile XML only. No Windows, account, file or HTTP.
#include "../dependencies/discord_reader_tile_layout.h"
#include "../dependencies/discord_reader_host_settings.h"
#include "../dependencies/tile_instances.h"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void TestProjection(){
    namespace d=aip::discord_tiles;namespace c=aip::content;
    d::Configuration cfg;cfg.view=L"Channel";d::Snapshot data;data.hasSuccess=true;data.capabilities.identity=true;
    d::Message message;message.id=L"123";message.authorName=L"Author";message.content=L"e & <b>";message.sentAt=d::Timestamp{100,0};data.messages={message};
    c::Snapshot frame;frame.name=L"Channel e";c::ProjectDiscordAccount(frame,data,cfg,200000,{},[](const auto&){return L"10:00 AM";});
    Check(frame.discordAccountSource&&!frame.discordProfileSource&&frame.text.primary==L"Channel e"&&frame.text.secondary==L"e & <b>","channel projection keeps literal message content and exact selected tile name");
    Check(frame.text.badge==L"Author \u00b7 10:00 AM"&&frame.discordAccountRows.empty(),"unknown unread, mentions and platforms do not become zero counters or synthetic fields");
    data.localNewMessages=d::Count{3,false};data.localMentions=d::Count{1,true};data.stale=true;data.lastSuccess=140000;
    c::ProjectDiscordAccount(frame,data,cfg,200000);
    Check(frame.discordAccountRows.size()==3&&frame.discordAccountRows[0].primary==L"New since tile baseline: \u22653", "local counts are explicitly labelled as local lower bounds");
    Check(frame.discordAccountRows.back().primary==L"Refreshed 1m ago","stale source age uses last successful receive time");
    cfg.showMessageAuthor=cfg.showMessageTime=cfg.showMessageContent=false;c::ProjectDiscordAccount(frame,data,cfg,200000);Check(frame.text.badge.empty()&&frame.text.secondary.empty(),"content/author/time privacy switches apply independently");
    cfg.view=L"Profile";data.user.id=L"123";data.user.displayName=L"Visible name";data.user.username=L"account";data.presence.status=d::Status::Online;
    data.presence.platforms=d::Platforms{d::Status::Idle,{},d::Status::Offline};data.personalMentions=d::Count{4,true};data.onlineFriends=d::Count{2,true};
    c::ProjectDiscordAccount(frame,data,cfg,200000);
    Check(frame.discordProfileSource&&frame.text.primary==L"Visible name"&&frame.text.secondary==L"@account", "profile uses display name and optional username without inventing server nicknames");
    Check(frame.discordAccountRows[0].primary==L"Online"&&frame.discordAccountRows[2].primary==L"Desktop", "only supplied visible platform statuses appear; offline web and missing mobile stay hidden");
    d::UserPatch clearedDisplayName;clearedDisplayName.id=data.user.id;clearedDisplayName.displayName=L"";d::MergeUser(data.user,clearedDisplayName);
    d::Friend friendWithoutDisplayName;friendWithoutDisplayName.user.id=L"456";friendWithoutDisplayName.user.displayName=L"";friendWithoutDisplayName.user.username=L"named-friend";friendWithoutDisplayName.presence.status=d::Status::Online;data.friends={friendWithoutDisplayName};
    c::ProjectDiscordAccount(frame,data,cfg,200000);
    Check(frame.text.primary==L"account"&&frame.discordAccountRows.back().primary==L"Refreshed 1m ago"&&std::any_of(frame.discordAccountRows.begin(),frame.discordAccountRows.end(),[](const auto& row){return row.primary==L"named-friend \u2014 Online";}),"cleared/null global names fall back to usernames for both profile and friends");
    Check(d::PreferredUserName({},L"account",L"fallback")==L"account"&&d::PreferredUserName(L"",L"",L"fallback")==L"fallback","account review name shares the same absent/empty fallback contract");
    const auto originalKey=c::DiscordAccountPresentationKey(frame);frame.discordAvatarPath=L"prepared-avatar";Check(originalKey!=c::DiscordAccountPresentationKey(frame),"prepared avatar identity invalidates presentation cache");
    cfg.showPresence=cfg.showUsername=cfg.showMentionCount=cfg.showPlatforms=false;c::ProjectDiscordAccount(frame,data,cfg,200000);
    Check(frame.text.secondary.empty()&&frame.discordAccountRows.front().primary==L"Online friends: 2", "profile display switches preserve independently available online-friend count");
    data={};c::ProjectDiscordAccount(frame,data,cfg,200000);Check(frame.text.primary==L"Discord \u2014 connect an account"&&frame.text.secondary==L"Choose a tile view and connect in Source settings."&&frame.text.badge.empty()&&frame.discordAccountRows.empty()&&frame.discordAvatarPath.empty(),"unconfigured sources give concise setup guidance without account fields, old avatars or synthetic counters");
    cfg.view=L"Channel";cfg.accountId=L"123456789";cfg.channelId=L"987654321";data.failure=d::Failure::InvalidData;c::ProjectDiscordAccount(frame,data,cfg,200000);
    Check(frame.text.primary==L"Discord \u2014 connect an account"&&frame.text.secondary.find(cfg.accountId)==std::wstring::npos&&frame.text.secondary.find(cfg.channelId)==std::wstring::npos&&frame.discordAccountRows.empty(),"unsuccessful readers show setup guidance without exposing selected account or channel IDs");
    data.hasSuccess=true;data.stale=true;data.lastSuccess=140000;data.messages={message};cfg.showMessageContent=true;c::ProjectDiscordAccount(frame,data,cfg,200000);
    Check(frame.text.primary==L"Channel e"&&frame.text.secondary==L"e & <b>"&&frame.discordAccountRows.back().primary==L"Refreshed 1m ago","last-good content survives a later reader failure and keeps its successful refresh age");
    const auto presence=c::DiscordSetupText(true);Check(presence.primary==L"Rich Presence is not running"&&presence.secondary.find(L"Source settings")!=std::wstring::npos&&presence.badge.empty(),"inactive Presence has a distinct state and setup instructions without invented activity");
}
void TestLayout(){namespace c=aip::content;c::Snapshot frame;frame.discordAccountSource=frame.discordProfileSource=true;frame.text={L"Visible <name>",L"@username",L""};frame.discordAccountRows={{L"Online",L"",L""},{L"Mentions: 4",L"",L""}};
    for(const auto& size:std::vector<std::pair<int,int>>{{71,71},{150,150},{310,150},{310,310}}){for(int scale:{25,100,200}){
        const auto geometry=c::ComputeDiscordAccountGeometry(size.first,size.second,true,scale);
        Check(geometry.textWidth>0&&geometry.avatarX+geometry.avatarSide<=size.first-10.0&&geometry.textWeight+geometry.avatarWeight==100,"right avatar geometry is bounded at all supported sizes and scales");
        std::wstring xml;Check(c::AppendDiscordAccountTileBinding(xml,L"TileSquare150x150",size.first,size.second,frame,L"",L"ms-appx:///Assets/avatar.png"),"prepared profile produces native adaptive tile XML");
        if(size.second<100){Check(xml.find(L"<text")==std::wstring::npos&&xml.find(L"<image")==std::wstring::npos,"Small preserves the static icon without squeezed account text");continue;}
        Check(xml.find(L"Visible &lt;name&gt;")!=std::wstring::npos&&xml.find(L"hint-removeMargin=\"true\"")!=std::wstring::npos,"text is escaped and the bounded right avatar has no unrelated margins");
        Check(xml.find(L"@username")<xml.find(L"Assets/avatar.png"),"profile text comes before a separate right avatar column");
    }}
    std::wstring xml;c::AppendDiscordAccountTileBinding(xml,L"TileSquare150x150",150,150,frame,L"");Check(xml.find(L"<image")==std::wstring::npos&&xml.find(L"<subgroup")==std::wstring::npos,"missing artwork produces neither a broken URI nor empty avatar column");
    frame.presentation.preset=L"Custom";frame.presentation.primaryStyle=L"title";frame.presentation.secondaryStyle=L"caption";frame.presentation.primaryLines=2;
    xml.clear();c::AppendDiscordAccountTileBinding(xml,L"TileSquare150x150",150,150,frame,L"");Check(xml.find(L"hint-style=\"title\"")!=std::wstring::npos,"explicit per-tile custom text settings override shared defaults");
}
void TestConfiguration(){namespace d=aip::discord_tiles;std::wstring error,value=L"true";Check(d::NormalizeReaderSetting(L"RememberCredential",value,error)&&value==L"1","remember protected credential is validated by the shared configuration schema");
    value=L"plaintext-private-value";Check(!d::NormalizeReaderSetting(L"CredentialProtected",value,error)&&error.find(value)==std::wstring::npos,"protected-only setting never accepts or echoes plaintext");
    value=L"dpapi:synthetic";Check(d::NormalizeReaderSetting(L"CredentialProtected",value,error),"synthetic protected envelope validates without a Windows account or decryption");
    value=L"Token";Check(!d::NormalizeReaderSetting(L"Token",value,error),"reader namespace has no generic plaintext token field");
    d::Configuration cfg;Check(!cfg.enabled&&!cfg.rememberCredential&&!cfg.allowReviewedSend&&!cfg.directSendEnabled&&cfg.clickAction==L"Compose","fresh readers require connection and default to temporary credentials and reviewed compose");
}
}
int main(){try{TestProjection();TestLayout();TestConfiguration();std::cout<<checks<<" Discord reader layout checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}

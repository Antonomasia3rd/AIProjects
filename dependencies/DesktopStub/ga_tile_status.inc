// Describes immutable prepared snapshots only. Never reads providers, pins,
// accounts, image files, or Windows' private choice of visible queue item.
#include "../media_tile_layout.h"
#include "../rss_tile_layout.h"
#include "../prayer_tile_layout.h"
#include "../datetime_tile_layout.h"
#include <set>
#include <map>
struct TileStatusLabels {
    std::wstring primary = L"Primary tile", mediumWide = L"Medium / Wide", medium = L"Medium", wide = L"Wide", large = L"Large";
    std::wstring noSnapshot = L"No prepared tile snapshot. Pin the tile and complete its source settings.";
    std::wstring textHidden = L"Text is hidden for this tile.", image = L"Image";
    std::wstring queue = L"Prepared image queue (Windows chooses the visible image)", slot = L"Slot";
    std::wstring staticImage = L"Rendered image (Registration)";
};
static void AppendTileStatusLine(std::wstring& output, const std::wstring& line)
{
    if (line.empty()) return;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == L'\r') { output += L"\r\n"; if (i + 1 < line.size() && line[i + 1] == L'\n') ++i; }
        else if (line[i] == L'\n') output += L"\r\n";
        else output += line[i];
    }
    output += L"\r\n";
}
static std::wstring TileSnapshotVisibleText(const aip::content::Snapshot& frame, int height, const TileStatusLabels& labels, int width = 150)
{
    std::wstring output;
    aip::content::TilePolicyReadScope policyScope(frame.policy);
    const auto* sizeSetting = height > 200 ? L"ApplyToLargeTile" : width > 200 ? L"ApplyToWideTile" : L"ApplyToMediumTile";
    if (!IniReadI(L"TileText",sizeSetting,1)) { AppendTileStatusLine(output,labels.textHidden); return output; }
    if (frame.textMode == aip::content::TextMode::Off) { AppendTileStatusLine(output, labels.textHidden); return output; }
    if(frame.mailSource){
        for(const auto& row:aip::content::BuildMailTilePlan(frame,width,height)){
            AppendTileStatusLine(output,row.primary.text);AppendTileStatusLine(output,row.secondary.text);
        }
    }else if((frame.clockSource||frame.calendarSource)&&frame.textMode!=aip::content::TextMode::Off){
        const auto& date=frame.dateTime;
        if(frame.clockSource){
            AppendTileStatusLine(output,date.time);if(!date.date.empty())AppendTileStatusLine(output,date.weekday);
            const auto branding=aip::content::DateTimeBindingBranding(frame,false,width<=150);
            if(branding==L"name"||branding==L"nameAndLogo")AppendTileStatusLine(output,date.location);
        }
        else {
            const bool centered=aip::content::CalendarDateOnlyCentered(frame,width,height);
            if(!frame.calendarEvents.empty()&&height>=150){
                for(const auto& group:aip::content::BuildCalendarTilePlan(frame,width,height).groups)for(const auto& column:group.columns)for(const auto& field:column.texts)if(!field.spacer)AppendTileStatusLine(output,field.text);
                const auto branding=aip::content::DateTimeBindingBranding(frame,false,width<=150);
                if(branding==L"name"||branding==L"nameAndLogo")AppendTileStatusLine(output,aip::content::DateTimeBindingDisplayName(frame,width,height));
                return output;
            }
            AppendTileStatusLine(output,height<150&&!date.shortWeekday.empty()?date.shortWeekday:date.weekday);AppendTileStatusLine(output,date.dayNumber);
            if(!centered)AppendTileStatusLine(output,date.location);
            for(size_t i=0;i<(std::min)(frame.detailRows.size(),aip::content::DateTimeEventCount(height));++i){
                AppendTileStatusLine(output,frame.detailRows[i].primary);AppendTileStatusLine(output,frame.detailRows[i].secondary);
            }
            if(!centered&&frame.detailRows.empty())AppendTileStatusLine(output,frame.text.secondary);
        }
    } else if (frame.textMode == aip::content::TextMode::Auto && frame.prayerSource) {
        if (frame.prayerClock.Available()) {
            AppendTileStatusLine(output,frame.prayerClock.time); AppendTileStatusLine(output,frame.prayerClock.prayer);
            AppendTileStatusLine(output,aip::content::PrayerLocationText(frame.prayerClock));
            AppendTileStatusLine(output,frame.prayerClock.countdown); AppendTileStatusLine(output,frame.prayerClock.details);
        } else {
            AppendTileStatusLine(output, frame.text.primary); AppendTileStatusLine(output, frame.text.secondary); AppendTileStatusLine(output, frame.text.badge);
        }
        if (aip::content::PrayerTileShowsSchedule(height, frame.detailRows, frame.prayerClock)) for (const auto& row : frame.detailRows) {
            AppendTileStatusLine(output, row.primary + L" " + row.secondary);
        }
    } else if (frame.textMode == aip::content::TextMode::Auto && !frame.smtcSource && !frame.detailRows.empty()) {
        for (std::size_t i = 0; i < (std::min)(frame.detailRows.size(),frame.gameSource?aip::content::GameTileRows(width,height):aip::content::DetailTileRows(height)); ++i) {
            AppendTileStatusLine(output, frame.detailRows[i].primary);
            AppendTileStatusLine(output, frame.detailRows[i].secondary);
        }
    } else if (frame.textMode == aip::content::TextMode::Auto && !frame.smtcSource && !frame.rssHeadlines.empty()) {
        const bool rich = aip::content::RssUsesRichLayout(frame.presentation);
        const auto limit = rich ? aip::content::RssRichTileRows(width, height) : aip::content::RssTileRows(height);
        for (std::size_t i = 0; i < (std::min)(frame.rssHeadlines.size(), limit); ++i) {
            AppendTileStatusLine(output, rich ? frame.rssHeadlines[i].primary : std::to_wstring(i + 1) + L". " + frame.rssHeadlines[i].primary);
            if (rich || height > 200) AppendTileStatusLine(output, frame.rssHeadlines[i].secondary);
        }
    } else {
        AppendTileStatusLine(output, frame.text.primary);
        AppendTileStatusLine(output, frame.text.secondary);
        AppendTileStatusLine(output, frame.text.badge);
        if (frame.smtcSource && frame.text.badge != aip::content::MediaStatusCaption(frame))
            AppendTileStatusLine(output, aip::content::MediaStatusCaption(frame));
    }
    return output;
}
static std::wstring BuildTileStatusTextWithLabels(const std::vector<aip::content::Snapshot>& frames, const TileStatusLabels& labels)
{
    if (frames.empty()) return labels.noSnapshot;
    std::wstring output;
    std::set<std::wstring> galleryTargets;
    for (const auto& frame : frames) {
        if (!frame.active) continue;
        if (frame.galleryFrame && !galleryTargets.insert(frame.secondaryTileId).second) continue;
        if (!output.empty()) output += L"\r\n";
        AppendTileStatusLine(output, frame.name.empty() ? labels.primary : frame.name);
        if(!frame.sourceDiagnostic.empty())AppendTileStatusLine(output,IniReadS(L"Strings",L"TileSourceDiagnostic",L"Source note")+L": "+frame.sourceDiagnostic);
        if(!frame.deliveryDiagnostic.empty())AppendTileStatusLine(output,IniReadS(L"Strings",L"TileDeliveryDiagnostic",L"Delivery note")+L": "+frame.deliveryDiagnostic);
        if (frame.galleryFrame) {
            AppendTileStatusLine(output, TileSnapshotUsesRegistration(frame) ? labels.staticImage : labels.queue);
            for (const auto& image : frames) if (image.galleryFrame && image.secondaryTileId == frame.secondaryTileId && image.galleryTicket == frame.galleryTicket) {
                const auto leaf = image.backgroundPath.substr(image.backgroundPath.find_last_of(L"\\/") + 1);
                AppendTileStatusLine(output, labels.slot + L" " + std::to_wstring(image.gallerySlot + 1) + L" [" + image.nativeQueueTag + L"] " + leaf);
            }
            continue;
        }
        const auto medium = TileSnapshotVisibleText(frame, 150, labels), wide = TileSnapshotVisibleText(frame, 150, labels, 310),
            large = TileSnapshotVisibleText(frame, 310, labels, 310);
        if (medium != wide) {
            AppendTileStatusLine(output, labels.medium + L":"); output += medium;
            AppendTileStatusLine(output, labels.wide + L":"); output += wide;
            AppendTileStatusLine(output, labels.large + L":"); output += large;
        } else if (medium != large) {
            AppendTileStatusLine(output, labels.mediumWide + L":"); output += medium;
            AppendTileStatusLine(output, labels.large + L":"); output += large;
        } else output += large;
        if (large.empty() && !frame.backgroundPath.empty()) AppendTileStatusLine(output, labels.image);
    }
    return output.empty() ? labels.noSnapshot : output;
}
static TileStatusLabels ReadTileStatusLabels()
{
    TileStatusLabels labels;
    labels.primary = IniReadS(L"Strings", L"TileStatusPrimary", labels.primary.c_str());
    labels.staticImage = IniReadS(L"Strings", L"TileStatusRegistrationImage", labels.staticImage.c_str());
    labels.mediumWide = IniReadS(L"Strings", L"TileStatusMediumWide", labels.mediumWide.c_str());
    labels.medium = IniReadS(L"Strings", L"TileStatusMedium", labels.medium.c_str());
    labels.wide = IniReadS(L"Strings", L"TileStatusWide", labels.wide.c_str());
    labels.large = IniReadS(L"Strings", L"TileStatusLarge", labels.large.c_str());
    labels.noSnapshot = IniReadS(L"Strings", L"TileStatusNoSnapshot", labels.noSnapshot.c_str());
    labels.textHidden = IniReadS(L"Strings", L"TileStatusTextHidden", labels.textHidden.c_str());
    labels.image = IniReadS(L"Strings", L"TileStatusImage", labels.image.c_str());
    labels.queue = IniReadS(L"Strings", L"TileStatusQueue", labels.queue.c_str());
    labels.slot = IniReadS(L"Strings", L"TileStatusSlot", labels.slot.c_str());
    return labels;
}
static std::vector<std::wstring> BuildTileStatusPagesWithLabels(const std::vector<aip::content::Snapshot>& frames,
    const TileStatusLabels& labels)
{
    std::vector<std::vector<aip::content::Snapshot>> destinations;
    std::map<std::wstring, std::size_t> indices;
    for (const auto& frame : frames) {
        if (!frame.active) continue;
        const auto found = indices.find(frame.secondaryTileId);
        if (found == indices.end()) {
            indices.emplace(frame.secondaryTileId, destinations.size());
            destinations.push_back({frame});
        } else destinations[found->second].push_back(frame);
    }
    std::vector<std::wstring> pages;
    for (const auto& destination : destinations) pages.push_back(BuildTileStatusTextWithLabels(destination, labels));
    if (pages.empty()) pages.push_back(labels.noSnapshot);
    return pages;
}
static std::wstring BuildTileStatusText(const std::vector<aip::content::Snapshot>& frames)
{
    return BuildTileStatusTextWithLabels(frames, ReadTileStatusLabels());
}
static std::vector<std::wstring> BuildTileStatusPages(const std::vector<aip::content::Snapshot>& frames)
{
    return BuildTileStatusPagesWithLabels(frames, ReadTileStatusLabels());
}

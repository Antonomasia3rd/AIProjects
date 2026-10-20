// Describes immutable prepared snapshots only. Never reads providers, pins,
// accounts, image files, or Windows' private choice of visible queue item.
#include "../media_tile_layout.h"
#include "../rss_tile_layout.h"
#include "../prayer_tile_layout.h"
#include <set>
#include <map>
struct TileStatusLabels {
    std::wstring primary = L"Primary tile", mediumWide = L"Medium / Wide", large = L"Large";
    std::wstring noSnapshot = L"No prepared tile snapshot. Pin the tile and complete its source settings.";
    std::wstring textHidden = L"Text is hidden for this tile.", image = L"Image";
    std::wstring queue = L"Prepared image queue (Windows chooses the visible image)", slot = L"Slot";
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
static std::wstring TileSnapshotVisibleText(const aip::content::Snapshot& frame, int height, const TileStatusLabels& labels)
{
    std::wstring output;
    if (frame.textMode == aip::content::TextMode::Off) { AppendTileStatusLine(output, labels.textHidden); return output; }
    if (frame.textMode == aip::content::TextMode::Auto && frame.prayerSource) {
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
        for (std::size_t i = 0; i < (std::min)(frame.detailRows.size(), aip::content::DetailTileRows(height)); ++i) {
            AppendTileStatusLine(output, frame.detailRows[i].primary);
            AppendTileStatusLine(output, frame.detailRows[i].secondary);
        }
    } else if (frame.textMode == aip::content::TextMode::Auto && !frame.smtcSource && !frame.rssHeadlines.empty()) {
        const bool rich = frame.presentation.preset == L"RssHeadlines";
        const auto limit = rich ? (height > 200 ? std::size_t(3) : std::size_t(1)) : aip::content::RssTileRows(height);
        for (std::size_t i = 0; i < (std::min)(frame.rssHeadlines.size(), limit); ++i) {
            AppendTileStatusLine(output, std::to_wstring(i + 1) + L". " + frame.rssHeadlines[i].primary);
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
        if (frame.galleryFrame) {
            AppendTileStatusLine(output, labels.queue);
            for (const auto& image : frames) if (image.galleryFrame && image.secondaryTileId == frame.secondaryTileId && image.galleryTicket == frame.galleryTicket) {
                const auto leaf = image.backgroundPath.substr(image.backgroundPath.find_last_of(L"\\/") + 1);
                AppendTileStatusLine(output, labels.slot + L" " + std::to_wstring(image.gallerySlot + 1) + L" [" + image.nativeQueueTag + L"] " + leaf);
            }
            continue;
        }
        const auto medium = TileSnapshotVisibleText(frame, 150, labels), large = TileSnapshotVisibleText(frame, 310, labels);
        if (medium != large) {
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
    labels.mediumWide = IniReadS(L"Strings", L"TileStatusMediumWide", labels.mediumWide.c_str());
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

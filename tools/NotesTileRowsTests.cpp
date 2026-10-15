#include "../dependencies/content_sources/notes_tile_rows.h"
#include <iostream>
#include <stdexcept>
using namespace aip::notes;
struct CapturedNotes {
    ResourceKind resource = ResourceKind::Resin;
    StateKind state = StateKind::Ok;
    std::wstring line1 = L"20/200", line2 = L"2h 0m";
    int recoverySeconds = 7200;
    std::map<DetailId, std::wstring> details;
};
static unsigned checks = 0;
static void Check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        CapturedNotes data;
        data.details = {{DetailId::Commissions,L"2/4"},{DetailId::Expeditions,L"1/5"},
            {DetailId::Realm,L"100/2400"},{DetailId::WeeklyBosses,L"2/3"},{DetailId::ScratchCard,L"Done"}};
        auto rows = BuildNoteTileRows(data);
        Check(rows.size() == 5 && rows.front().primary == L"Resin 20/200", "Genshin overview includes resource plus four available details");
        Check(rows.front().secondary == L"Full in 2h 0m", "partial resource shows recovery");
        Check(rows[2].primary == L"Expeditions ready" && rows[2].secondary == L"1/5", "expedition count describes ready expeditions");
        Check(rows.back().primary == L"Weekly Bosses", "another game's scratch card is not mixed into Genshin");
        data.state = StateKind::Full;
        Check(BuildNoteTileRows(data).front().secondary == L"Full", "full resource does not show a misleading countdown");
        data.state = StateKind::Error; data.line1 = L"Sign-in required"; data.line2 = L"Open account setup";
        rows = BuildNoteTileRows(data);
        Check(rows.size() == 1 && rows[0].primary == data.line1 && rows[0].secondary == data.line2, "errors replace stale resource and activity rows");
        data.state = StateKind::Loading;
        Check(BuildNoteTileRows(data).size() == 1, "loading cannot leak stale details");
        data.state = StateKind::Ok; data.resource = ResourceKind::Stamina;
        data.details = {{DetailId::Training,L"100/500"},{DetailId::Expeditions,L"2/4"},
            {DetailId::Reserve,L"12/2400"},{DetailId::EchoOfWar,L"1/3"}};
        rows = BuildNoteTileRows(data);
        Check(rows.size() == 5 && rows.front().primary.find(L"Trailblaze Power") == 0, "HSR includes its own resource and activities");
        Check(rows.back().primary == L"Echo of War", "HSR weekly activity remains available");
        data.resource = ResourceKind::Charge;
        data.details = {{DetailId::Engagement,L"20/400"},{DetailId::ScratchCard,L"Incomplete"},{DetailId::VideoStore,L"Open"}};
        rows = BuildNoteTileRows(data);
        Check(rows.size() == 4 && rows.front().primary.find(L"Battery Charge") == 0, "ZZZ overview has resource and three supported activities");
        Check(rows[2].primary == L"Scratch Card" && rows[2].secondary == L"Incomplete", "scratch card state is retained");
        rows = BuildNoteTileRows(data, DetailId::VideoStore);
        Check(rows.size() == 1 && rows[0].primary == L"Video Store", "explicit individual-detail preference remains a single row");
        Check(BuildNoteTileRows(data, DetailId::Commissions)[0].secondary.find(L"does not provide") != std::wstring::npos, "unsupported selection reports absence");
        data.details.clear();
        Check(BuildNoteTileRows(data).size() == 1, "missing activity fields are omitted");
        Check(BuildNoteTileRows(data, DetailId::ScratchCard)[0].secondary == L"Unavailable", "missing selected field is not shown as completed");
        Check(std::wstring(ResourceTileTopic(ResourceKind::Resin)) == L"resin" &&
            std::wstring(ResourceTileTopic(ResourceKind::Stamina)) == L"stamina" &&
            std::wstring(ResourceTileTopic(ResourceKind::Charge)) == L"charge", "resource icons have independent topics");
        std::cout << "Notes tile rows: " << checks << " checks passed; no accounts or Windows APIs\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

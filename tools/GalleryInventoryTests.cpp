// Injected policy only: no filesystem, photos, Windows, codec, hashing API,
// clock, package, configuration, resident or network operation is requested.
#define AIP_GALLERY_INVENTORY_PURE
#include "../dependencies/gallery_inventory.inc"
#include <iostream>
#include <stdexcept>
namespace {
using namespace aip::gallery;
int checks = 0;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
struct FakeFile { std::wstring identity; std::uint64_t modified = 1; std::vector<std::uint8_t> bytes; bool inspect = true, readable = true; };
struct Fixture {
    std::map<std::wstring, FakeFile> files;
    std::set<std::wstring> thumbnails;
    std::uint64_t time = 1, metadataCost = 0;
    int reads = 0, hashes = 0, decodes = 0, metadata = 0;
    bool canceled = false, cancelRead = false, deferHash = false, deferDecode = false;
    InventoryOptions options;
    void Add(const std::wstring& path, const std::wstring& identity, std::uint8_t code) { files[path] = {identity, 1, {code}, true, true}; }
    std::vector<InventoryEntry> Listing() const {
        std::vector<InventoryEntry> result;
        for (const auto& pair : files) result.push_back({pair.first, pair.second.bytes.size(), pair.second.modified});
        return result;
    }
    InventoryCallbacks Callbacks() {
        InventoryCallbacks callbacks;
        callbacks.metadata = [&](const auto& path, auto& result, auto&) {
            ++metadata; time += metadataCost;
            const auto found = files.find(path);
            if (found == files.end() || !found->second.inspect) return false;
            result.canonicalPath = L"canonical/" + path; result.fileIdentity = found->second.identity;
            result.size = found->second.bytes.size(); result.modified = found->second.modified; return true;
        };
        callbacks.read = [&](const auto& path, const auto&, auto& bytes, auto&) {
            ++reads;
            if (cancelRead) { canceled = true; return false; }
            const auto& file = files.at(path);
            if (!file.readable) return false;
            bytes = file.bytes; return true;
        };
        callbacks.hash = [&](const auto& bytes) {
            ++hashes;
            if (deferHash) { time += options.maxScanMilliseconds; return std::wstring{}; }
            return std::wstring(64, static_cast<wchar_t>(L'a' + bytes.front() % 6));
        };
        callbacks.thumbnail = [&](const auto& bytes, const auto& target, auto&, bool& permanent) {
            ++decodes;
            if (deferDecode) { time += options.maxScanMilliseconds; return false; }
            if (bytes.front() == 5) { permanent = true; return false; }
            thumbnails.insert(target); return true;
        };
        callbacks.exists = [&](const auto& path) { return thumbnails.count(path) != 0; };
        callbacks.now = [&] { return time; };
        return callbacks;
    }
    InventoryResult Scan(const InventoryIndex& old = {}, const std::wstring& cache = L"synthetic-cache", bool listingComplete = true) {
        return ScanGalleryInventoryInjected(Listing(), listingComplete, L"synthetic-folder", cache, old, [&] { return canceled; }, Callbacks(), options);
    }
};
void TestCacheAndDedup() {
    Fixture fixture;
    fixture.Add(L"a", L"physical-one", 0); fixture.Add(L"alias", L"physical-one", 0);
    fixture.Add(L"copy", L"physical-copy", 0); fixture.Add(L"other", L"physical-other", 1);
    auto first = fixture.Scan();
    Check(first.complete && first.images.size() == 2 && first.index.files.size() == 4 &&
        fixture.reads == 3 && fixture.hashes == 3 && fixture.decodes == 2,
        "file aliases avoid duplicate reads while exact copies share one decoded content-addressed thumbnail");
    Check(first.hashedFiles == 3 && first.decodedFiles == 2 && first.images[0].id == L"sha256:" + std::wstring(64, L'a'),
        "returned image IDs expose only canonical encoded-content digests and bounded work counts");
    auto same = fixture.Scan(first.index);
    Check(same.complete && same.images.size() == 2 && !same.hashedFiles && !same.decodedFiles && fixture.reads == 3,
        "unchanged metadata and existing thumbnails need no image reads, hashes or decodes");
    fixture.files[L"other"].bytes = {2}; ++fixture.files[L"other"].modified;
    auto changed = fixture.Scan(same.index);
    Check(changed.complete && changed.hashedFiles == 1 && changed.decodedFiles == 1 && fixture.reads == 4 &&
        changed.images[1].id == L"sha256:" + std::wstring(64, L'c'), "only changed encoded files are rehashed and decoded");
    fixture.thumbnails.erase(changed.images[0].thumbnailPath);
    auto repaired = fixture.Scan(changed.index);
    Check(repaired.complete && repaired.hashedFiles == 0 && repaired.decodedFiles == 1 && fixture.reads == 5,
        "missing thumbnails regenerate from an unchanged file without hashing it again");
    const auto oldPaths = fixture.thumbnails;
    fixture.options.thumbnailLongEdge = 310;
    auto smaller = fixture.Scan(repaired.index);
    Check(smaller.complete && smaller.hashedFiles == 0 && smaller.decodedFiles == 2 &&
        smaller.images.front().thumbnailPath.find(L"-310.png") != std::wstring::npos,
        "thumbnail-size changes reuse source digests and publish new immutable paths");
    Check(std::includes(fixture.thumbnails.begin(), fixture.thumbnails.end(), oldPaths.begin(), oldPaths.end()),
        "size changes never remove older thumbnail paths that notifications may reference");
    const auto beforeReads = fixture.reads;
    auto moved = fixture.Scan(smaller.index, L"another-cache");
    Check(moved.complete && moved.hashedFiles == 0 && moved.decodedFiles == 2 && fixture.reads > beforeReads &&
        moved.images.front().thumbnailPath.find(L"another-cache") == 0, "a different cache destination does not reuse paths from the old directory");
    fixture.files.erase(L"other");
    auto removed = fixture.Scan(moved.index, L"another-cache");
    Check(removed.complete && removed.images.size() == 1 && removed.index.files.size() == 3 &&
        fixture.thumbnails.count(moved.images[1].thumbnailPath), "removed sources leave the metadata cache without deleting their immutable published thumbnail");
}
void TestFailuresAndBudgets() {
    Fixture fixture; fixture.Add(L"bad", L"bad-image", 5);
    auto corrupt = fixture.Scan();
    Check(corrupt.complete && corrupt.images.empty() && corrupt.skippedFiles == 1 && fixture.hashes == 1 && fixture.decodes == 1,
        "corrupt images are omitted with a stable bounded diagnostic");
    auto sameBad = fixture.Scan(corrupt.index);
    Check(sameBad.complete && fixture.hashes == 1 && fixture.decodes == 1 && fixture.reads == 1,
        "unchanged corrupt files do not create repeated hash/decode work");
    fixture.files[L"bad"].bytes = {0}; ++fixture.files[L"bad"].modified;
    auto fixed = fixture.Scan(sameBad.index);
    Check(fixed.complete && fixed.images.size() == 1 && fixture.hashes == 2 && fixture.decodes == 2,
        "changed metadata retries a formerly corrupt image");
    fixture.options.maxFileBytes = 1; fixture.files[L"bad"].bytes = {0, 0}; ++fixture.files[L"bad"].modified;
    auto tooLarge = fixture.Scan(fixed.index);
    Check(tooLarge.complete && tooLarge.images.empty() && tooLarge.hashedFiles == 0 && tooLarge.decodedFiles == 0,
        "encoded size limits apply before any image read, hash or decode");
    Fixture progress; progress.options.maxChangedFiles = 1;
    for (int index = 0; index < 4; ++index) progress.Add(std::to_wstring(index), L"id" + std::to_wstring(index), static_cast<std::uint8_t>(index));
    InventoryIndex index;
    for (int pass = 0; pass < 4; ++pass) {
        auto result = progress.Scan(index);
        Check(result.hashedFiles == 1 && result.decodedFiles == 1 && result.images.size() == static_cast<std::size_t>(pass + 1) &&
            result.complete == (pass == 3), "partial scans retain ready images and advance the cursor through the bounded changed-file budget");
        index = std::move(result.index);
    }
    progress.metadataCost = 1; progress.options.maxScanMilliseconds = 2;
    auto partial = progress.Scan(index);
    Check(!partial.complete && partial.index.cursor == 2 && partial.hashedFiles == 0, "metadata-only work respects the elapsed pass budget");
    auto tail = progress.Scan(partial.index);
    Check(tail.complete && tail.hashedFiles == 0, "slow metadata scans resume after the processed prefix instead of starving the tail");
    Fixture canceled; canceled.Add(L"one", L"one", 0); canceled.cancelRead = true;
    auto cancel = canceled.Scan();
    Check(cancel.cancelled && !cancel.complete && !cancel.hashedFiles && canceled.decodes == 0,
        "cancellation during read stops before hashing or decoding and cannot publish a complete result");
    Fixture timeout; timeout.Add(L"one", L"one", 0); timeout.deferDecode = true;
    auto deferred = timeout.Scan();
    Check(!deferred.complete && deferred.index.cursor == 0 && deferred.hashedFiles == 1 &&
        !deferred.index.files.at(L"one").id.empty(), "an expired decode stores its source identity but keeps the current file pending");
    timeout.deferDecode = false;
    auto retry = timeout.Scan(deferred.index);
    Check(retry.complete && retry.images.size() == 1 && retry.hashedFiles == 0, "a deferred decode resumes without repeating its completed source hash");
    Fixture hashTimeout; hashTimeout.Add(L"one", L"one", 0); hashTimeout.deferHash = true;
    auto partialHash = hashTimeout.Scan();
    Check(!partialHash.complete && partialHash.index.cursor == 0 && partialHash.decodedFiles == 0,
        "a hash that exhausts its deadline stays pending rather than silently dropping the file from a complete inventory");
    hashTimeout.deferHash = false;
    Check(hashTimeout.Scan(partialHash.index).complete, "unfinished hashes are retried on the following bounded pass");
    auto incomplete = progress.Scan(index, L"synthetic-cache", false);
    Check(!incomplete.complete, "a truncated or failed directory enumeration never masquerades as complete inventory");
    progress.options.maxDiagnostics = 1;
    auto limited = progress.Scan(index, L"synthetic-cache", false);
    Check(limited.diagnostics.size() <= 1, "diagnostic output is bounded independently of directory size");
    progress.options.thumbnailLongEdge = 621;
    const auto beforeInvalid = progress.metadata;
    auto invalid = progress.Scan(index);
    Check(!invalid.complete && invalid.images.empty() && progress.metadata == beforeInvalid, "out-of-contract thumbnail bounds reject before filesystem callbacks");
}
void TestExifOrientation() {
    const char* expected[] = {"ABCDEF", "CBAFED", "FEDCBA", "DEFABC", "ADBECF", "DAEBFC", "FCEBDA", "CFBEAD"};
    for (unsigned tag = 1; tag <= 8; ++tag) {
        const auto plan = InventoryExifOrientation(tag);
        const unsigned width = 3, height = 2;
        const unsigned outWidth = plan.clockwise == 90 || plan.clockwise == 270 ? height : width;
        const unsigned outHeight = plan.clockwise == 90 || plan.clockwise == 270 ? width : height;
        std::string result(outWidth * outHeight, '?');
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
            unsigned ox = x, oy = y;
            if (plan.clockwise == 90) { ox = height - 1 - y; oy = x; }
            else if (plan.clockwise == 180) { ox = width - 1 - x; oy = height - 1 - y; }
            else if (plan.clockwise == 270) { ox = y; oy = width - 1 - x; }
            if (plan.horizontal) ox = outWidth - 1 - ox;
            if (plan.vertical) oy = outHeight - 1 - oy;
            result[oy * outWidth + ox] = "ABCDEF"[y * width + x];
        }
        Check(result == expected[tag - 1], "all EXIF orientations preserve the expected rotation/reflection of an asymmetric pixel grid");
    }
    Check(InventoryExifOrientation(0).clockwise == 0 && !InventoryExifOrientation(99).horizontal && !InventoryExifOrientation(99).vertical,
        "absent or invalid EXIF orientation is treated as normal rather than guessed");
}
}
int main() {
    try { TestCacheAndDedup(); TestFailuresAndBudgets(); TestExifOrientation(); std::cout << checks << " gallery inventory checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1; }
}

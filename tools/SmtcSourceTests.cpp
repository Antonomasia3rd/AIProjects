#define NOMINMAX
#include "../dependencies/content_sources/smtc.inc"
#include <iostream>
#include <atomic>
#include <thread>

namespace {
int failures = 0;
int checks = 0;
std::atomic<int> successfulReads{0}, unavailableReads{0};
void Check(bool condition, const char* message)
{
    ++checks;
    std::cout << (condition ? "ok - " : "FAIL - ") << message << '\n';
    if (!condition) ++failures;
}

std::vector<std::uint8_t> BitmapBytes(UINT width = 2, UINT height = 2, BYTE color = 64)
{
    const UINT stride = (width * 3u + 3u) & ~3u;
    std::vector<std::uint8_t> bytes(54 + stride * height, color);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42; file.bfSize = static_cast<DWORD>(bytes.size()); file.bfOffBits = 54;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width); info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = stride * height;
    memcpy(bytes.data(), &file, sizeof(file)); memcpy(bytes.data() + 14, &info, sizeof(info));
    return bytes;
}

void TestInjectedMediaAndArtwork()
{
    using namespace aip::content;
    Check(smtc_artwork_detail::AllowedDimensions(8192, 2048) &&
        !smtc_artwork_detail::AllowedDimensions(8192, 2049) &&
        !smtc_artwork_detail::AllowedDimensions(0, 1) && !smtc_artwork_detail::AllowedDimensions(1, 8193),
        "decoded dimensions independently enforce zero, dimension, and total pixel limits");
    SmtcSessionData data;
    data.hasSession = true; data.title = L"First song"; data.albumArtist = L"Album artist";
    data.appId = L"Vendor.Player_123!App"; data.status = L"Playing"; data.artworkBytes = BitmapBytes();
    int reads = 0;
    const auto reader = [&](auto deadline, bool includeArtwork, const auto&) {
        ++reads;
        Check(deadline > smtc_detail::Clock::now(), "injected media read receives the shared deadline");
        auto value = data;
        if (!includeArtwork) value.artworkBytes.clear();
        return value;
    };
    SmtcMediaSnapshot media;
    std::wstring error;
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader) &&
        reads == 1 && media.text.primary == L"First song" && media.artist == L"Album artist" &&
        media.text.secondary == L"Album artist\nPlaying - Player" && media.artworkBytes == data.artworkBytes,
        "one injected session read pairs title/status and its thumbnail without querying the OS");
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader) &&
        media.artworkBytes.empty() && !media.artwork && media.text.primary == L"First song",
        "text-only media reads retain their presentation without artwork I/O");
    SmtcArtworkCache cache(L"SmtcFixture", L"C:\\fixture-only\\profile-one.ini");
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        media.artwork && media.artworkBytes.empty() && media.artwork->width == 2 && media.artwork->height == 2,
        "the same injected media read decodes bounded artwork into its profile cache");
    auto first = media.artwork;
    const auto firstPath = first ? first->path : L"";
    Check(!firstPath.empty() && GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
        firstPath.find(L"AIProjects-SMTC-") != std::wstring::npos,
        "media artwork is writable temporary content instead of an executable-directory artifact");
    auto same = cache.Store(data.artworkBytes, error, smtc_detail::Clock::now() + std::chrono::seconds(2));
    Check(same == first, "unchanged artwork reuses the immutable leased file");
    SmtcArtworkCache otherProfile(L"SmtcFixture", L"C:\\fixture-only\\profile-two.ini");
    auto other = otherProfile.Store(data.artworkBytes, error, smtc_detail::Clock::now() + std::chrono::seconds(2));
    Check(other && other->path != firstPath, "profiles never share artwork-file ownership");
    data.title = L"Second song"; data.artworkBytes = BitmapBytes(3, 2, 128);
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        media.artwork && media.artwork->path != firstPath && media.title == L"Second song",
        "a track change publishes its new text and image together");
    Check(GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES,
        "an older rendered snapshot's artwork remains valid while its lease is retained");
    data.artworkBytes.clear();
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && media.artworkBytes.empty() && media.title == L"Second song",
        "a track without a thumbnail clears preceding artwork instead of showing stale art");
    data.hasSession = false; data.artworkBytes = BitmapBytes();
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && media.artworkBytes.empty() && media.text.primary == L"No media playing",
        "idle ignores stale injected properties and thumbnail data");
    Check(!ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, [] { return true; }, &cache) &&
        !media.artwork && media.text.primary.empty(), "cancellation clears all earlier media fields");
    data.hasSession = true; data.artworkBytes.assign(SmtcMaximumArtworkBytes + 1, 0);
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && media.artworkBytes.empty() && !media.artworkError.empty() && media.title == L"Second song",
        "oversized thumbnails fail separately from usable media text");
    data.artworkBytes = {1, 2, 3, 4};
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && !media.artworkError.empty() && media.title == L"Second song",
        "invalid image decode produces a text fallback with an artwork diagnostic");
    auto wide = cache.Store(BitmapBytes(2048, 1), error, smtc_detail::Clock::now() + std::chrono::seconds(2));
    Check(wide && wide->width == SmtcCachedArtworkDimension && wide->height == 1,
        "large permitted images are resized to a bounded cache bitmap while preserving aspect ratio");
    auto enormous = BitmapBytes();
    BITMAPINFOHEADER info{}; memcpy(&info, enormous.data() + 14, sizeof(info));
    info.biWidth = 8193; memcpy(enormous.data() + 14, &info, sizeof(info));
    Check(!cache.Store(enormous, error, smtc_detail::Clock::now() + std::chrono::seconds(2)) && !error.empty(),
        "oversized decoded dimensions are rejected before copying image pixels");
    Check(!cache.Store(BitmapBytes(), error, smtc_detail::Clock::now() - std::chrono::milliseconds(1)) && !error.empty(),
        "an expired deadline starts no image decode or cache write");
    Check(!cache.Store(BitmapBytes(), error, smtc_detail::Clock::now() + std::chrono::seconds(2), [] { return true; }),
        "canceled artwork storage starts no decode or cache write");
    other.reset(); wide.reset();
    first.reset();
    Check(GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES,
        "a second immutable snapshot lease still keeps old artwork readable");
    same.reset();
    Check(GetFileAttributesW(firstPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "releasing the final snapshot removes its obsolete artwork file");
}

struct FakeOperation {
    mutable bool canceled = false, waited = false;
    mutable std::chrono::milliseconds wait{0};
    winrt::Windows::Foundation::AsyncStatus status;
    bool completes = false;
    auto Status() const { return status; }
    auto wait_for(std::chrono::milliseconds duration) const {
        waited = true;
        wait = duration;
        return completes ? winrt::Windows::Foundation::AsyncStatus::Completed : status;
    }
    void Cancel() const { canceled = true; }
    int GetResults() const {
        if (status == winrt::Windows::Foundation::AsyncStatus::Error)
            throw winrt::hresult_error(E_ACCESSDENIED);
        return 42;
    }
};

void TestDeadline()
{
    using namespace aip::content::smtc_detail;
    using winrt::Windows::Foundation::AsyncStatus;
    FakeOperation pending{ false, false, std::chrono::milliseconds(0), AsyncStatus::Started };
    bool timedOut = false;
    try { Await(pending, Clock::now() + std::chrono::milliseconds(250)); }
    catch (const winrt::hresult_error& ex) { timedOut = ex.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
    Check(timedOut && pending.canceled && pending.waited && pending.wait.count() <= 250,
        "pending SMTC operations use their remaining deadline and request cancellation");

    FakeOperation expired{ false, false, std::chrono::milliseconds(0), AsyncStatus::Started };
    try { Await(expired, Clock::now() - std::chrono::milliseconds(1)); } catch (...) {}
    Check(expired.canceled && !expired.waited, "an expired SMTC deadline starts no additional wait");

    FakeOperation completed{ false, false, std::chrono::milliseconds(0), AsyncStatus::Completed };
    Check(Await(completed, Clock::now()) == 42 && !completed.waited && !completed.canceled,
        "completed SMTC operations read results without an unbounded get or second completion handler");

    FakeOperation denied{ false, false, std::chrono::milliseconds(0), AsyncStatus::Error };
    bool propagated = false;
    try { Await(denied, Clock::now()); }
    catch (const winrt::hresult_error& ex) { propagated = ex.code() == E_ACCESSDENIED; }
    Check(propagated && !denied.waited, "SMTC terminal errors retain their HRESULT");
}

void TestContract()
{
    using namespace aip::content;
    Text text{ L"stale", L"stale", L"stale" };
    std::wstring error;
    Check(!ReadSmtcContent(text, error, std::chrono::milliseconds(0)) &&
        text.primary.empty() && text.secondary.empty() && text.badge.empty() && !error.empty(),
        "invalid SMTC timeout clears the previous snapshot and reports a useful error");
    Check(!ReadSmtcContent(text, error, std::chrono::milliseconds(60001)),
        "SMTC rejects unbounded timeout requests");
    Check(smtc_detail::Clean(std::wstring(L"\0 artist ", 9)) == L"artist" &&
        smtc_detail::Clean(std::wstring(MaxText - 1, L'x') + L"\xD83D\xDE00").size() == MaxText - 1,
        "SMTC text is visible, bounded, and preserves complete Unicode pairs");
    Check(smtc_detail::SourceName(L"Vendor.Player_123!App") == L"Player" &&
        smtc_detail::SourceName(L"") == L"SMTC",
        "SMTC app identifiers become readable source names");

    HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    Check(SUCCEEDED(initialized), "SMTC test initializes an STA fixture");
    if (SUCCEEDED(initialized))
    {
        Check(!ReadSmtcContent(text, error) && error.find(L"multithreaded") != std::wstring::npos,
            "SMTC rejects UI-thread STA blocking before requesting media data");
        RoUninitialize();
    }
}

bool ReadOnlySmoke()
{
    aip::content::Text text;
    std::wstring error;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = aip::content::ReadSmtcContent(text, error, std::chrono::milliseconds(400));
    if (ok) ++successfulReads;
    else ++unavailableReads;
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // No playback, registration, file, tray, or configuration mutation occurs.
    // Do not print the user's song title/artist in a test log.
    const bool contract = ok
        ? !text.primary.empty() && !text.secondary.empty() && text.badge.empty() && error.empty()
        : text.primary.empty() && text.secondary.empty() && text.badge.empty() && !error.empty();
    return contract && elapsed < std::chrono::seconds(5) &&
        text.primary.size() <= aip::content::MaxText &&
        text.secondary.size() <= aip::content::MaxText &&
        text.badge.size() <= aip::content::MaxText;
}

void TestSystemReadOnly()
{
    Check(ReadOnlySmoke(), "read-only SMTC smoke returns media, idle, or a useful bounded error");
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    std::atomic<int> errors{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker)
        workers.emplace_back([&errors] {
            for (int iteration = 0; iteration < 5; ++iteration)
                if (!ReadOnlySmoke()) ++errors;
        });
    for (auto& worker : workers) worker.join();
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    Check(errors == 0, "twenty concurrent SMTC snapshot reads leave each result isolated");
    std::cout << "SMTC successful/idle snapshots: " << successfulReads.load() <<
        "; unavailable/error snapshots: " << unavailableReads.load() << '\n';
    Check(handlesAfter <= handlesBefore + 128,
        "SMTC concurrent smoke has no runaway handle growth (allows WinRT cache warmup)");
}
}

int main(int argc, char** argv)
{
    bool allowSystemMedia = false;
    for (int argument = 1; argument < argc; ++argument) {
        if (std::string(argv[argument]) == "--allow-system-media") allowSystemMedia = true;
        else { std::cerr << "Unknown test argument\n"; return 2; }
    }
    TestDeadline();
    TestContract();
    TestInjectedMediaAndArtwork();
    if (allowSystemMedia) TestSystemReadOnly();
    std::cout << checks << " checks; " << failures << " failures; system media reads " <<
        (allowSystemMedia ? "explicitly enabled" : "disabled") << ".\n";
    std::cout << (failures == 0 ? "SMTC source tests passed.\n" : "SMTC source tests failed.\n");
    return failures == 0 ? 0 : 1;
}

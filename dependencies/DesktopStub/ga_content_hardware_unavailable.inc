// A DesktopStub build can omit raw-hardware providers entirely.  Keep the
// configuration contract intact and report the omission instead of treating a
// selected source as an empty, successful result.
struct ContentCapsProvider
{
    std::atomic<bool> restart{false};
    void Refresh(const aip::caps::Config&) { restart = false; }
    aip::content::Text Read() const
    {
        return {L"Caps indicator", L"This DesktopStub build excludes optional hardware sources.", L""};
    }
    void Cancel() {}
    void Stop() {}
};
static ContentCapsProvider g_contentCaps;

struct ContentAsusProvider
{
    std::atomic<bool> restart{false};
    void Refresh(const aip::content::AsusConfiguration&) { restart = false; }
    aip::content::Text Read() const
    {
        return {L"ASUS indicators", L"This DesktopStub build excludes optional hardware sources.", L""};
    }
    void Cancel() {}
    void Stop() {}
};
static ContentAsusProvider g_contentAsus;

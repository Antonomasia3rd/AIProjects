// -----------------------------------------------------------------------------
// Build/version metadata
// -----------------------------------------------------------------------------
#ifndef DESKTOPSTUB_VERSION_MAJOR
#define DESKTOPSTUB_VERSION_MAJOR 1
#endif

#ifndef DESKTOPSTUB_VERSION_MINOR
#define DESKTOPSTUB_VERSION_MINOR 0
#endif

#ifndef DESKTOPSTUB_VERSION_BUILD
#define DESKTOPSTUB_VERSION_BUILD 0
#endif

#ifndef DESKTOPSTUB_VERSION_REVISION
#define DESKTOPSTUB_VERSION_REVISION 0
#endif

#ifndef DESKTOPSTUB_RELEASE_TAG
#define DESKTOPSTUB_RELEASE_TAG "DesktopStub-v1"
#endif

#define DESKTOPSTUB_STRINGIZE2(x) #x
#define DESKTOPSTUB_STRINGIZE(x) DESKTOPSTUB_STRINGIZE2(x)
#define DESKTOPSTUB_WIDEN2(x) L##x
#define DESKTOPSTUB_WIDEN(x) DESKTOPSTUB_WIDEN2(x)

#define DESKTOPSTUB_VERSION_TEXT_W \
    DESKTOPSTUB_WIDEN(DESKTOPSTUB_STRINGIZE(DESKTOPSTUB_VERSION_MAJOR)) L"." \
    DESKTOPSTUB_WIDEN(DESKTOPSTUB_STRINGIZE(DESKTOPSTUB_VERSION_MINOR)) L"." \
    DESKTOPSTUB_WIDEN(DESKTOPSTUB_STRINGIZE(DESKTOPSTUB_VERSION_BUILD)) L"." \
    DESKTOPSTUB_WIDEN(DESKTOPSTUB_STRINGIZE(DESKTOPSTUB_VERSION_REVISION))

static std::wstring DesktopStubPackageVersion()
{
    return DESKTOPSTUB_VERSION_TEXT_W;
}

static std::wstring DesktopStubReleaseTag()
{
    return DESKTOPSTUB_WIDEN(DESKTOPSTUB_RELEASE_TAG);
}

static std::wstring DesktopStubVersionDisplayText()
{
    std::wstring tag = DesktopStubReleaseTag();
    std::wstring version = DesktopStubPackageVersion();
    if (tag.empty())
        return version;
    return tag + L" (" + version + L")";
}


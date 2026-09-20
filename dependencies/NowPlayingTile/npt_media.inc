static int GetEncoderClsid(const wchar_t* mimeType, CLSID* clsid)
{
    UINT count = 0;
    UINT size = 0;
    GetImageEncodersSize(&count, &size);
    if (size == 0)
    {
        return -1;
    }

    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, size, encoders) != Ok)
    {
        return -1;
    }

    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(encoders[i].MimeType, mimeType) == 0)
        {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }

    return -1;
}

static std::wstring HashBytes(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t byte : bytes)
    {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 1099511628211ull;
    }

    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%016llX", static_cast<unsigned long long>(hash));
    return buffer;
}

static void CleanupOldArtwork(const std::wstring& keepPath)
{
    WIN32_FIND_DATAW fd = {};
    std::wstring pattern = PathJoin(g_exeDir, L"tile-artwork-*.*");
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            continue;
        }

        std::wstring path = PathJoin(g_exeDir, fd.cFileName);
        if (_wcsicmp(path.c_str(), keepPath.c_str()) != 0)
        {
            DeleteFileW(path.c_str());
        }
    } while (FindNextFileW(find, &fd));

    FindClose(find);
}

static bool SaveArtworkAsJpeg(const std::vector<uint8_t>& bytes, const std::wstring& path)
{
    if (bytes.empty() || bytes.size() > static_cast<size_t>(UINT_MAX))
    {
        return false;
    }

    IStream* stream = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
    if (stream == nullptr)
    {
        return false;
    }

    Image image(stream);
    CLSID jpegClsid = {};
    std::wstring temporaryPath = BuildTemporarySiblingPath(path);
    DeleteFileW(temporaryPath.c_str());
    bool ok =
        image.GetLastStatus() == Ok &&
        GetEncoderClsid(L"image/jpeg", &jpegClsid) >= 0 &&
        image.Save(temporaryPath.c_str(), &jpegClsid, nullptr) == Ok;
    stream->Release();
    if (ok)
    {
        ok = CommitTemporaryFile(temporaryPath, path);
    }
    else
    {
        DeleteFileW(temporaryPath.c_str());
    }
    return ok;
}

static bool TryLoadArtworkUri(
    const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& thumbnail,
    std::wstring& artworkHash,
    std::wstring& artworkUri,
    std::wstring& artworkPath)
{
    artworkHash.clear();
    artworkUri.clear();
    artworkPath.clear();

    if (thumbnail == nullptr)
    {
        return false;
    }

    try
    {
        auto stream = thumbnail.OpenReadAsync().get();
        if (stream == nullptr || stream.Size() == 0 || stream.Size() > 10ull * 1024ull * 1024ull)
        {
            return false;
        }

        uint32_t size = static_cast<uint32_t>(stream.Size());
        winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
        uint32_t loaded = reader.LoadAsync(size).get();
        if (loaded == 0)
        {
            return false;
        }

        std::vector<uint8_t> bytes(loaded);
        reader.ReadBytes(winrt::array_view<uint8_t>(bytes));

        artworkHash = HashBytes(bytes);
        std::wstring path = PathJoin(g_exeDir, L"tile-artwork-" + artworkHash + L".jpg");
        if (!FileExists(path))
        {
            if (!SaveArtworkAsJpeg(bytes, path))
            {
                Log(L"Artwork decode/save failed; Live Tile will use text-only layout for this update.");
                return false;
            }
        }

        CleanupOldArtwork(path);
        artworkPath = path;
        artworkUri = FilePathToUri(path);
        return true;
    }
    catch (const winrt::hresult_error& ex)
    {
        Log(L"Artwork load failed: 0x%08X %s", static_cast<unsigned int>(ex.code()), ex.message().c_str());
        return false;
    }
    catch (...)
    {
        Log(L"Artwork load failed with an unknown exception.");
        return false;
    }
}

static std::wstring PlaybackStatusText(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus status)
{
    using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch (status)
    {
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed: return L"Closed";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened: return L"Opened";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing: return L"Changing";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped: return L"Stopped";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing: return L"Playing";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused: return L"Paused";
    default: return L"Unknown";
    }
}

static std::wstring ShortSourceName(std::wstring source)
{
    source = Trim(source);
    if (source.empty())
    {
        return L"Unknown app";
    }

    size_t bang = source.find(L'!');
    if (bang != std::wstring::npos)
    {
        source = source.substr(0, bang);
    }

    size_t dot = source.find_last_of(L'.');
    if (dot != std::wstring::npos && dot + 1 < source.size())
    {
        source = source.substr(dot + 1);
    }

    size_t suffix = source.find(L'_');
    if (suffix != std::wstring::npos && suffix > 0)
    {
        source = source.substr(0, suffix);
    }

    return source.empty() ? L"Unknown app" : source;
}

static MediaSnapshot ReadMediaSnapshot()
{
    using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;

    try
    {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        auto session = manager.GetCurrentSession();
        if (session == nullptr)
        {
            return MediaSnapshot::Empty(L"No current SMTC session");
        }

        auto props = session.TryGetMediaPropertiesAsync().get();
        auto playback = session.GetPlaybackInfo();

        std::wstring sourceApp = session.SourceAppUserModelId().c_str();
        std::wstring title = Trim(std::wstring(props.Title().c_str()));
        if (title.empty())
        {
            title = L"(untitled media)";
        }

        std::wstring artist = FirstNonEmpty({
            std::wstring(props.Artist().c_str()),
            std::wstring(props.AlbumArtist().c_str()),
            std::wstring(props.Subtitle().c_str()),
            sourceApp
        });

        MediaSnapshot snapshot;
        snapshot.source = ShortSourceName(sourceApp);
        snapshot.title = title;
        snapshot.artist = artist;
        snapshot.status = PlaybackStatusText(playback.PlaybackStatus());
        snapshot.lastUpdated = MediaSnapshot::FormatClockTime();

        TryLoadArtworkUri(props.Thumbnail(), snapshot.artworkHash, snapshot.artworkUri, snapshot.artworkPath);
        return snapshot;
    }
    catch (const winrt::hresult_error& ex)
    {
        std::wstring message = ex.message().c_str();
        Log(L"SMTC read failed: 0x%08X %s", static_cast<unsigned int>(ex.code()), message.c_str());
        return MediaSnapshot::Empty(message.empty() ? L"SMTC read failed" : message);
    }
    catch (...)
    {
        Log(L"SMTC read failed with an unknown exception.");
        return MediaSnapshot::Empty(L"SMTC read failed");
    }
}

// The donor retains its persistent notification-artwork location until retired.
// Session selection, metadata, thumbnail reads, byte limits and decoding are
// shared with DesktopStub; there is no second SMTC/property reader here.
static void CleanupOldArtwork(const std::wstring& keepPath)
{
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(PathJoin(g_exeDir, L"tile-artwork-*.*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            const auto path = PathJoin(g_exeDir, data.cFileName);
            if (_wcsicmp(path.c_str(), keepPath.c_str()) != 0) DeleteFileW(path.c_str());
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static bool SaveSharedArtwork(const std::vector<std::uint8_t>& bytes, const std::wstring& path,
    aip::content::smtc_detail::Clock::time_point deadline, std::wstring& error)
{
    std::vector<BYTE> bitmap;
    UINT width = 0, height = 0;
    if (!aip::content::smtc_artwork_detail::Decode(bytes, bitmap, width, height, error, deadline, {})) return false;
    // Keep the retained donor's notification format: its image URI points to a
    // JPEG, while DesktopStub uses the shared BMP only as renderer input.
    IStream* stream = SHCreateMemStream(bitmap.data(), static_cast<UINT>(bitmap.size()));
    if (!stream) { error = L"Could not prepare notification artwork."; return false; }
    const auto temporaryPath = BuildTemporarySiblingPath(path);
    bool saved = false;
    {
        Image image(stream);
        UINT count = 0, size = 0;
        if (image.GetLastStatus() == Ok && GetImageEncodersSize(&count, &size) == Ok && size) {
            std::vector<BYTE> codecs(size);
            auto* encoders = reinterpret_cast<ImageCodecInfo*>(codecs.data());
            if (GetImageEncoders(count, size, encoders) == Ok)
                for (UINT index = 0; index < count; ++index)
                    if (wcscmp(encoders[index].MimeType, L"image/jpeg") == 0) {
                        saved = image.Save(temporaryPath.c_str(), &encoders[index].Clsid, nullptr) == Ok;
                        break;
                    }
        }
    }
    stream->Release();
    if (saved && aip::content::smtc_detail::Clock::now() < deadline && CommitTemporaryFile(temporaryPath, path)) return true;
    DeleteFileW(temporaryPath.c_str()); error = L"Could not finish saving notification artwork."; return false;
}

static MediaSnapshot ReadMediaSnapshot()
{
    const auto deadline = aip::content::smtc_detail::Clock::now() + milliseconds(SMTC_OPERATION_TIMEOUT_MS);
    aip::content::SmtcMediaSnapshot media;
    std::wstring error;
    if (!aip::content::ReadSmtcMedia(media, error, milliseconds(SMTC_OPERATION_TIMEOUT_MS))) {
        Log(L"SMTC read failed: %s", error.c_str());
        return MediaSnapshot::Empty(error);
    }
    MediaSnapshot snapshot;
    snapshot.source = media.source; snapshot.title = media.title;
    snapshot.artist = media.artist; snapshot.status = media.status;
    snapshot.lastUpdated = MediaSnapshot::FormatClockTime();
    if (!media.artworkBytes.empty()) {
        const auto hash = aip::content::smtc_artwork_detail::Hash(media.artworkBytes.data(), media.artworkBytes.size());
        if (!hash.empty()) {
            const auto path = PathJoin(g_exeDir, L"tile-artwork-" + hash + L".jpg");
            if (FileExists(path) || SaveSharedArtwork(media.artworkBytes, path, deadline, error)) {
                snapshot.artworkHash = hash; snapshot.artworkPath = path; snapshot.artworkUri = FilePathToUri(path);
                CleanupOldArtwork(path);
            }
        }
    }
    if (!media.artworkError.empty()) Log(L"Artwork unavailable: %s", media.artworkError.c_str());
    else if (!error.empty()) Log(L"Artwork unavailable: %s", error.c_str());
    return snapshot;
}

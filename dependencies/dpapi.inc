#ifndef AIP_DEPENDENCIES_DPAPI_INC
#define AIP_DEPENDENCIES_DPAPI_INC

namespace aip
{
// Historical builds used the same unversioned prefix for two incompatible
// plaintext encodings: DiscordRPC encrypted UTF-8 bytes, while
// RealTimeNotesDeskband encrypted raw UTF-16LE bytes. Callers therefore have
// to state which legacy format belongs to their product. Guessing from the
// decrypted bytes is unsafe because some byte sequences are valid in both
// encodings.
enum class DpapiLegacyEncoding
{
    Utf8,
    Utf16LittleEndian,
};

inline constexpr const wchar_t* kDpapiPrefix = L"dpapi:";
inline constexpr const wchar_t* kDpapiV1Utf8Prefix = L"dpapi:v1:utf8:";

namespace dpapi_detail
{
inline constexpr char kV1Utf8Envelope[] = "AIProjects.DPAPI.v1.UTF8";

class SensitiveBytes
{
public:
    ~SensitiveBytes()
    {
        if (!value.empty())
        {
            SecureZeroMemory(value.data(), value.size());
        }
    }

    std::vector<BYTE> value;
};

class SensitiveUtf8
{
public:
    ~SensitiveUtf8()
    {
        if (!value.empty())
        {
            SecureZeroMemory(value.data(), value.size());
        }
    }

    std::string value;
};

inline std::wstring DecodeUtf8(const BYTE* bytes, size_t byteCount, const char* invalidMessage)
{
    SensitiveUtf8 utf8;
    if (byteCount != 0)
    {
        utf8.value.assign(
            reinterpret_cast<const char*>(bytes),
            reinterpret_cast<const char*>(bytes) + byteCount);
    }

    std::wstring result;
    if (!TryUtf8ToWide(utf8.value, result))
    {
        throw std::runtime_error(invalidMessage);
    }
    return result;
}

inline std::wstring DecodeUtf16LittleEndian(const BYTE* bytes, size_t byteCount)
{
    static_assert(sizeof(wchar_t) == 2, "DPAPI UTF-16LE compatibility requires Windows wchar_t.");
    if ((byteCount % 2) != 0)
    {
        throw std::runtime_error("DPAPI unprotect failed: legacy UTF-16LE data has an odd byte count.");
    }

    std::wstring result;
    result.reserve(byteCount / 2);
    for (size_t i = 0; i < byteCount; i += 2)
    {
        result.push_back(static_cast<wchar_t>(
            static_cast<unsigned int>(bytes[i]) |
            (static_cast<unsigned int>(bytes[i + 1]) << 8)));
    }

    // WideCharToMultiByte with WC_ERR_INVALID_CHARS provides strict surrogate
    // validation. Do not return malformed legacy data as a corrupted secret.
    SensitiveUtf8 validation;
    if (!TryWideToUtf8(result, validation.value))
    {
        if (!result.empty())
        {
            SecureZeroMemory(result.data(), result.size() * sizeof(wchar_t));
        }
        throw std::runtime_error("DPAPI unprotect failed: legacy data is not valid UTF-16LE.");
    }
    return result;
}
}

inline bool IsDpapiProtectedValue(const std::wstring& value)
{
    return StartsWithI(Trim(value), kDpapiPrefix);
}

// New writes use a versioned, encoding-labelled prefix and also carry an
// encrypted envelope marker. The marker prevents a changed/mistyped external
// label from silently decoding the plaintext with the wrong rules.
inline std::wstring ProtectSecretForCurrentUser(const std::wstring& secret)
{
    dpapi_detail::SensitiveUtf8 utf8;
    if (!TryWideToUtf8(secret, utf8.value))
    {
        throw std::runtime_error("DPAPI protect failed: secret cannot be encoded as UTF-8.");
    }

    constexpr size_t envelopeSize = sizeof(dpapi_detail::kV1Utf8Envelope);
    if (utf8.value.size() > static_cast<size_t>(MAXDWORD) - envelopeSize)
    {
        throw std::runtime_error("DPAPI protect failed: secret is too large.");
    }

    dpapi_detail::SensitiveBytes plaintext;
    plaintext.value.reserve(envelopeSize + utf8.value.size());
    const BYTE* envelopeBegin = reinterpret_cast<const BYTE*>(dpapi_detail::kV1Utf8Envelope);
    plaintext.value.insert(plaintext.value.end(), envelopeBegin, envelopeBegin + envelopeSize);
    plaintext.value.insert(plaintext.value.end(), utf8.value.begin(), utf8.value.end());

    DATA_BLOB plain = {};
    plain.pbData = plaintext.value.data();
    plain.cbData = static_cast<DWORD>(plaintext.value.size());

    DATA_BLOB cipher = {};
    if (!CryptProtectData(
            &plain,
            L"AIProjects DPAPI v1 UTF-8 secret",
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &cipher))
    {
        throw std::runtime_error(WideToUtf8(L"DPAPI protect failed: " + GetLastErrorText(GetLastError())));
    }

    if (cipher.cbData == 0 || cipher.pbData == nullptr)
    {
        LocalFree(cipher.pbData);
        throw std::runtime_error("DPAPI protect failed: encrypted buffer is empty.");
    }

    std::vector<BYTE> bytes;
    try
    {
        bytes.assign(cipher.pbData, cipher.pbData + cipher.cbData);
    }
    catch (...)
    {
        LocalFree(cipher.pbData);
        throw;
    }
    LocalFree(cipher.pbData);
    return std::wstring(kDpapiV1Utf8Prefix) + BytesToHex(bytes);
}

inline std::wstring UnprotectSecretForCurrentUser(
    const std::wstring& protectedValue,
    DpapiLegacyEncoding legacyEncoding)
{
    std::wstring text = Trim(protectedValue);
    bool isVersionedUtf8 = StartsWithI(text, kDpapiV1Utf8Prefix);
    size_t prefixLength = 0;
    if (isVersionedUtf8)
    {
        prefixLength = wcslen(kDpapiV1Utf8Prefix);
    }
    else if (StartsWithI(text, kDpapiPrefix))
    {
        prefixLength = wcslen(kDpapiPrefix);
        if (text.find(L':', prefixLength) != std::wstring::npos)
        {
            throw std::runtime_error("Protected value uses an unsupported DPAPI serialization version or encoding.");
        }
    }
    else
    {
        throw std::runtime_error("Protected value must start with dpapi:.");
    }

    std::vector<BYTE> cipherBytes;
    if (!HexToBytes(text.substr(prefixLength), cipherBytes))
    {
        throw std::runtime_error("Protected value contains invalid hex.");
    }
    if (cipherBytes.empty())
    {
        throw std::runtime_error("Protected value has an empty encrypted payload.");
    }
    if (cipherBytes.size() > MAXDWORD)
    {
        throw std::runtime_error("DPAPI unprotect failed: encrypted value is too large.");
    }

    DATA_BLOB cipher = {};
    cipher.pbData = cipherBytes.data();
    cipher.cbData = static_cast<DWORD>(cipherBytes.size());

    DATA_BLOB decrypted = {};
    if (!CryptUnprotectData(
            &cipher,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &decrypted))
    {
        throw std::runtime_error(WideToUtf8(L"DPAPI unprotect failed: " + GetLastErrorText(GetLastError())));
    }

    dpapi_detail::SensitiveBytes plaintext;
    try
    {
        if (decrypted.cbData != 0)
        {
            if (decrypted.pbData == nullptr)
            {
                throw std::runtime_error("DPAPI unprotect failed: decrypted buffer is null.");
            }
            plaintext.value.assign(decrypted.pbData, decrypted.pbData + decrypted.cbData);
        }
    }
    catch (...)
    {
        if (decrypted.pbData != nullptr && decrypted.cbData != 0)
        {
            SecureZeroMemory(decrypted.pbData, decrypted.cbData);
        }
        LocalFree(decrypted.pbData);
        throw;
    }

    if (decrypted.pbData != nullptr && decrypted.cbData != 0)
    {
        SecureZeroMemory(decrypted.pbData, decrypted.cbData);
    }
    LocalFree(decrypted.pbData);

    if (isVersionedUtf8)
    {
        constexpr size_t envelopeSize = sizeof(dpapi_detail::kV1Utf8Envelope);
        if (plaintext.value.size() < envelopeSize ||
            memcmp(
                plaintext.value.data(),
                dpapi_detail::kV1Utf8Envelope,
                envelopeSize) != 0)
        {
            throw std::runtime_error("DPAPI unprotect failed: v1 UTF-8 envelope marker is missing or invalid.");
        }
        return dpapi_detail::DecodeUtf8(
            plaintext.value.data() + envelopeSize,
            plaintext.value.size() - envelopeSize,
            "DPAPI unprotect failed: v1 plaintext is not valid UTF-8.");
    }

    constexpr size_t envelopeSize = sizeof(dpapi_detail::kV1Utf8Envelope);
    if (plaintext.value.size() >= envelopeSize &&
        memcmp(
            plaintext.value.data(),
            dpapi_detail::kV1Utf8Envelope,
            envelopeSize) == 0)
    {
        throw std::runtime_error("DPAPI unprotect failed: v1 envelope was stored with a legacy serialization prefix.");
    }

    if (legacyEncoding == DpapiLegacyEncoding::Utf16LittleEndian)
    {
        return dpapi_detail::DecodeUtf16LittleEndian(
            plaintext.value.data(),
            plaintext.value.size());
    }
    return dpapi_detail::DecodeUtf8(
        plaintext.value.data(),
        plaintext.value.size(),
        "DPAPI unprotect failed: legacy plaintext is not valid UTF-8.");
}
}

#endif

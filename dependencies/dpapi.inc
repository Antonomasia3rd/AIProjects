#ifndef AIP_DEPENDENCIES_DPAPI_INC
#define AIP_DEPENDENCIES_DPAPI_INC

namespace aip
{
inline constexpr const wchar_t* kDpapiPrefix = L"dpapi:";

inline bool IsDpapiProtectedValue(const std::wstring& value)
{
    return StartsWithI(Trim(value), kDpapiPrefix);
}

inline std::wstring ProtectSecretForCurrentUser(const std::wstring& secret)
{
    std::string utf8;
    if (!TryWideToUtf8(secret, utf8))
    {
        throw std::runtime_error("DPAPI protect failed: secret cannot be encoded as UTF-8.");
    }

    DATA_BLOB plain = {};
    plain.pbData = utf8.empty() ? nullptr : reinterpret_cast<BYTE*>(utf8.data());
    plain.cbData = static_cast<DWORD>(utf8.size());

    DATA_BLOB cipher = {};
    if (!CryptProtectData(&plain, nullptr, nullptr, nullptr, nullptr, 0, &cipher))
    {
        throw std::runtime_error(WideToUtf8(L"DPAPI protect failed: " + GetLastErrorText(GetLastError())));
    }

    std::vector<BYTE> bytes;
    if (cipher.cbData != 0)
    {
        if (cipher.pbData == nullptr)
        {
            LocalFree(cipher.pbData);
            throw std::runtime_error("DPAPI protect failed: encrypted buffer is null.");
        }
        bytes.assign(cipher.pbData, cipher.pbData + cipher.cbData);
    }
    LocalFree(cipher.pbData);
    return std::wstring(kDpapiPrefix) + BytesToHex(bytes);
}

inline std::wstring UnprotectSecretForCurrentUser(const std::wstring& protectedValue)
{
    std::wstring text = Trim(protectedValue);
    if (!IsDpapiProtectedValue(text))
    {
        throw std::runtime_error("Protected value must start with dpapi:.");
    }

    std::vector<BYTE> cipherBytes;
    if (!HexToBytes(text.substr(wcslen(kDpapiPrefix)), cipherBytes))
    {
        throw std::runtime_error("Protected value contains invalid hex.");
    }

    DATA_BLOB cipher = {};
    cipher.pbData = cipherBytes.empty() ? nullptr : cipherBytes.data();
    cipher.cbData = static_cast<DWORD>(cipherBytes.size());

    DATA_BLOB plain = {};
    if (!CryptUnprotectData(&cipher, nullptr, nullptr, nullptr, nullptr, 0, &plain))
    {
        throw std::runtime_error(WideToUtf8(L"DPAPI unprotect failed: " + GetLastErrorText(GetLastError())));
    }

    std::string utf8;
    if (plain.cbData != 0)
    {
        if (plain.pbData == nullptr)
        {
            throw std::runtime_error("DPAPI unprotect failed: decrypted buffer is null.");
        }
        utf8.assign(
            reinterpret_cast<char*>(plain.pbData),
            reinterpret_cast<char*>(plain.pbData) + plain.cbData);
    }
    LocalFree(plain.pbData);

    std::wstring result;
    if (!TryUtf8ToWide(utf8, result))
    {
        throw std::runtime_error("DPAPI unprotect failed: decrypted value is not valid UTF-8.");
    }
    return result;
}
}

#endif

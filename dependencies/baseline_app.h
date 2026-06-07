#ifndef AIP_DEPENDENCIES_BASELINE_APP_H
#define AIP_DEPENDENCIES_BASELINE_APP_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <atomic>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace aip
{
struct InstanceIdentity
{
    std::wstring mutexName;
    std::wstring messageName;
    std::wstring windowTitle;
};

class ResidentShutdownState
{
public:
    bool Request()
    {
        bool expected = false;
        return requested_.compare_exchange_strong(expected, true);
    }

    bool Cancel()
    {
        return requested_.exchange(false);
    }

    bool IsRequested() const
    {
        return requested_.load();
    }

    void MarkWorkComplete()
    {
        workComplete_ = true;
    }

    bool IsWorkComplete() const
    {
        return workComplete_.load();
    }

private:
    std::atomic<bool> requested_{ false };
    std::atomic<bool> workComplete_{ false };
};

inline InstanceIdentity BuildInstanceIdentity(
    const std::wstring& mutexStem,
    const std::wstring& messageStem,
    const std::wstring& windowClass,
    const std::wstring& windowProduct,
    const std::wstring& suffix)
{
    InstanceIdentity identity;
    identity.mutexName = L"Local\\" + mutexStem + L"." + suffix;
    identity.messageName = messageStem + L"." + suffix;
    identity.windowTitle = windowClass;
    if (!windowProduct.empty())
    {
        identity.windowTitle += L"." + windowProduct;
    }
    identity.windowTitle += L"." + suffix;
    return identity;
}


inline std::wstring StableHashHex64(const std::wstring& value, bool caseInsensitive = true)
{
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t ch : value)
    {
        wchar_t normalized = caseInsensitive ? towlower(ch) : ch;
        hash ^= static_cast<uint64_t>(normalized);
        hash *= 1099511628211ull;
    }

    wchar_t buffer[32] = {};
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

inline InstanceIdentity BuildPathScopedInstanceIdentity(
    const std::wstring& mutexStem,
    const std::wstring& messageStem,
    const std::wstring& windowClass,
    const std::wstring& windowProduct,
    const std::wstring& scopePath)
{
    return BuildInstanceIdentity(
        mutexStem,
        messageStem,
        windowClass,
        windowProduct,
        StableHashHex64(scopePath));
}

inline HWND FindInstanceWindow(
    const wchar_t* windowClass,
    const wchar_t* windowTitle,
    int retries,
    int delayMs,
    bool includeMessageOnlyWindows = true)
{
    retries = retries < 0 ? 0 : retries;
    delayMs = delayMs < 0 ? 0 : delayMs;
    for (int attempt = 0; attempt <= retries; ++attempt)
    {
        HWND existing = FindWindowW(windowClass, windowTitle);
        if (existing == nullptr && includeMessageOnlyWindows)
        {
            existing = FindWindowExW(HWND_MESSAGE, nullptr, windowClass, windowTitle);
        }
        if (existing != nullptr)
        {
            return existing;
        }
        if (attempt < retries && delayMs > 0)
        {
            Sleep(static_cast<DWORD>(delayMs));
        }
    }
    return nullptr;
}

inline bool SignalInstanceWindow(
    const wchar_t* windowClass,
    const wchar_t* windowTitle,
    UINT message,
    WPARAM request,
    int retries,
    int delayMs,
    bool includeMessageOnlyWindows = true)
{
    if (message == 0)
    {
        return false;
    }
    HWND existing = FindInstanceWindow(
        windowClass,
        windowTitle,
        retries,
        delayMs,
        includeMessageOnlyWindows);
    return existing != nullptr && PostMessageW(existing, message, request, 0) != FALSE;
}

inline UINT RegisterTaskbarCreatedMessage()
{
    return RegisterWindowMessageW(L"TaskbarCreated");
}

inline std::wstring DecodeBackslashEscapes(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        wchar_t ch = value[i];
        if (ch != L'\\' || i + 1 >= value.size())
        {
            out.push_back(ch);
            continue;
        }

        wchar_t next = value[++i];
        if (next == L'r') out.push_back(L'\r');
        else if (next == L'n') out.push_back(L'\n');
        else if (next == L't') out.push_back(L'\t');
        else if (next == L'\\') out.push_back(L'\\');
        else
        {
            out.push_back(L'\\');
            out.push_back(next);
        }
    }
    return out;
}

inline void ReplaceTemplateToken(std::wstring& text, const std::wstring& token, const std::wstring& value)
{
    if (token.empty())
    {
        return;
    }
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::wstring::npos)
    {
        text.replace(pos, token.size(), value);
        pos += value.size();
    }
}

inline std::wstring FormatTextTemplate(
    const std::wstring& rawTemplate,
    const std::vector<std::pair<std::wstring, std::wstring>>& replacements)
{
    std::wstring text = DecodeBackslashEscapes(rawTemplate);
    for (const auto& replacement : replacements)
    {
        ReplaceTemplateToken(text, replacement.first, replacement.second);
    }
    return text;
}

inline std::wstring NormalizeTraySectionTitle(const std::wstring& text)
{
    size_t begin = 0;
    while (begin < text.size() && iswspace(text[begin]))
    {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && (text[end - 1] == L':' || iswspace(text[end - 1])))
    {
        --end;
    }
    return begin == end ? text : text.substr(begin, end - begin);
}

class TraySectionLayout
{
public:
    TraySectionLayout(HMENU root, bool dropdown)
        : root_(root),
          dropdown_(dropdown)
    {
    }

    HMENU Begin(const std::wstring& title, bool* created = nullptr) const
    {
        if (dropdown_)
        {
            HMENU submenu = CreatePopupMenu();
            if (created != nullptr)
            {
                *created = submenu != nullptr;
            }
            return submenu != nullptr ? submenu : root_;
        }

        bool appended = root_ != nullptr &&
            AppendMenuW(root_, MF_STRING | MF_DISABLED, 0, title.c_str()) != FALSE;
        if (created != nullptr)
        {
            *created = appended;
        }
        return root_;
    }

    bool End(HMENU section, const std::wstring& title) const
    {
        if (!dropdown_)
        {
            return root_ != nullptr && AppendMenuW(root_, MF_SEPARATOR, 0, nullptr) != FALSE;
        }
        if (root_ == nullptr || section == nullptr || section == root_)
        {
            return false;
        }

        std::wstring normalizedTitle = NormalizeTraySectionTitle(title);
        if (AppendMenuW(
                root_,
                MF_POPUP,
                reinterpret_cast<UINT_PTR>(section),
                normalizedTitle.c_str()) != FALSE)
        {
            return true;
        }

        DWORD error = GetLastError();
        DestroyMenu(section);
        SetLastError(error);
        return false;
    }

    bool AppendFinalSeparator() const
    {
        return !dropdown_ ||
            (root_ != nullptr && AppendMenuW(root_, MF_SEPARATOR, 0, nullptr) != FALSE);
    }

    bool IsDropdown() const
    {
        return dropdown_;
    }

private:
    HMENU root_ = nullptr;
    bool dropdown_ = true;
};
}

#endif

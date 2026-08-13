#ifndef AIP_TOOLS_SOURCE_CHECK_COMMON_H
#define AIP_TOOLS_SOURCE_CHECK_COMMON_H

// Shared I/O and assertion helpers for the "read source files, assert
// specific text patterns are present/absent/ordered" style of regression
// check used by DiscordRPCSourceCheck.cpp and SharedBaselineSourceCheck.cpp.
// Those two files had independently drifted into near-identical copies of
// this exact code (same function names and signatures) under different
// per-tool label text; this header is the single copy both now use.
//
// DesktopStubSourceCheck.cpp uses a meaningfully different, regex-driven
// approach (data-driven Check structs, ordered-alternative extraction from
// regex patterns) and RssLiveTileSourceCheck.cpp compiles and calls real
// product functions directly rather than pattern-matching text -- neither
// is a good fit for this simpler paradigm, so neither was forced onto it.
//
// Before including this header, define SOURCE_CHECK_LABEL to a string
// literal naming the tool for error/summary output, e.g.:
//   #define SOURCE_CHECK_LABEL "DiscordRPC"
//   #include "../../tools/source_check_common.h"

#ifndef SOURCE_CHECK_LABEL
#error "Define SOURCE_CHECK_LABEL before including source_check_common.h"
#endif

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

static int g_checks = 0;

inline std::string NormalizeNewlines(std::string text)
{
    std::string normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r')
        {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            normalized.push_back('\n');
        }
        else
        {
            normalized.push_back(text[i]);
        }
    }
    return normalized;
}

inline std::string ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::string portablePath = path;
        for (char& ch : portablePath)
        {
            if (ch == '\\')
                ch = '/';
        }
        in.open(portablePath, std::ios::binary);
    }
    if (!in)
        throw std::runtime_error("missing source file: " + path);
    return NormalizeNewlines(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

inline void RequireContains(const std::string& name, const std::string& sourceName, const std::string& source, const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) == std::string::npos)
    {
        std::cerr << SOURCE_CHECK_LABEL << " source regression: " << name << " missing " << needle
            << " in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

inline void RequireNotContains(const std::string& name, const std::string& sourceName, const std::string& source, const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) != std::string::npos)
    {
        std::cerr << SOURCE_CHECK_LABEL << " source regression: " << name << " found forbidden " << needle
            << " in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

inline void RequireOrderedContains(
    const std::string& name,
    const std::string& sourceName,
    const std::string& source,
    const std::vector<std::string>& needles)
{
    ++g_checks;
    size_t pos = 0;
    for (const auto& needle : needles)
    {
        size_t found = source.find(needle, pos);
        if (found == std::string::npos)
        {
            std::cerr << SOURCE_CHECK_LABEL << " source regression: " << name << " missing/out-of-order "
                << needle << " in " << sourceName << "\n";
            throw std::runtime_error("source regression");
        }
        pos = found + needle.size();
    }
    std::cout << "ok - " << name << "\n";
}

#endif

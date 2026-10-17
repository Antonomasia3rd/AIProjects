static void TestIdleMaintenanceHost()
{
    std::atomic<std::uint64_t> last(aip::idle::Never);
    unsigned claims = 0;
    for (std::uint64_t tick = 0; tick < 800; ++tick) if (aip::idle::Claim(last, tick, 60000)) ++claims;
    Check(claims == 1 && last == 0, "hundreds of frame-completion requests share one idle maintenance interval even at clock zero");
    Check(!aip::idle::Claim(last, 59999, 60000) && aip::idle::Claim(last, 60000, 60000),
        "automatic maintenance is eligible precisely at the configured interval");
    Check(!aip::idle::Claim(last, 120000, 0), "zero disables automatic idle maintenance");
    Check(aip::idle::Claim(last, 61000, 1000), "a shorter interval takes effect without waiting for the old deadline");
    Check(!aip::idle::Claim(last, 5, 1000), "a delayed caller with an older time sample cannot move the shared maintenance budget backward");
    last = 1100;
    Check(!aip::idle::Claim(last, 1000, 100) && !aip::idle::Claim(last, 1100, 100) &&
        aip::idle::Claim(last, 1200, 100), "out-of-order samples do not allow duplicate maintenance inside the interval");
    last = aip::idle::Never;
    Check(!aip::idle::Claim(last, aip::idle::Never, 1), "the uninitialized sentinel cannot also be claimed as a timestamp");
    last = aip::idle::Never; std::atomic<unsigned> concurrent(0);
    std::vector<std::thread> callers;
    for (int i = 0; i < 8; ++i) callers.emplace_back([&] { for (int n = 0; n < 1000; ++n)
        if (aip::idle::Claim(last, 50, 60000)) ++concurrent; });
    for (auto& caller : callers) caller.join();
    Check(concurrent == 1, "concurrent callers cannot compact or empty the same working set repeatedly");
    const auto savedLast = g_lastIdleMemoryTrim.load(); const auto savedInterval = g_idleTrimIntervalMs.load();
    const auto savedCompact = g_compactCrtHeapOnIdle.load(), savedTrim = g_trimWorkingSetOnIdle.load();
    g_idleTrimIntervalMs = 60000; g_compactCrtHeapOnIdle = true; g_trimWorkingSetOnIdle = true;
    g_lastIdleMemoryTrim = aip::idle::Never; const auto before = idleTestTrimCalls.load();
    const auto savedActive = g_generationActive.exchange(true);
    TrimIdleWorkingSetIfConfigured();
    Check(idleTestTrimCalls == before && g_lastIdleMemoryTrim == aip::idle::Never,
        "rendering in progress defers heap compaction and working-set emptying without consuming the idle budget");
    g_generationActive = savedActive;
    for (int i = 0; i < 80; ++i) TrimIdleWorkingSetIfConfigured();
    Check(idleTestTrimCalls == before + 1, "production trim entry coalesces an eighty-frame batch into one injected maintenance operation");
    g_idleTrimIntervalMs = 0; TrimIdleWorkingSetIfConfigured();
    Check(idleTestTrimCalls == before + 1, "production trim entry honors the existing disabled interval");
    g_lastIdleMemoryTrim = savedLast; g_idleTrimIntervalMs = savedInterval;
    g_compactCrtHeapOnIdle = savedCompact; g_trimWorkingSetOnIdle = savedTrim;
}

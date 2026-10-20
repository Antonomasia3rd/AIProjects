// Inert lifecycle fixture: real threads/events/message queues, no windows,
// WinEvent hooks, Windhawk APIs, shell actions, or installed mod changes.
#define CENTER_GEOMETRY_LIFECYCLE_TEST
#include "local@center-window-geometry.wh.cpp"

#include <cstdlib>

namespace {
std::atomic<int> checks{0};
void Check(bool result, const char* message) {
    ++checks;
    if (!result) {
        fprintf(stderr, "FAIL: %s\n", message);
        ExitProcess(1);
    }
}

struct Event {
    HANDLE value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Event() { Check(value != nullptr, "create fixture event"); }
    ~Event() { CloseHandle(value); }
    void Set() { Check(SetEvent(value) != FALSE, "signal fixture event"); }
    void Wait() { Check(WaitForSingleObject(value, 3000) == WAIT_OBJECT_0,
                        "fixture operation completes within three seconds"); }
};

struct Context {
    CenterGeometryWorkerLifecycle worker;
    Event entered, gate, ready, consumed;
    std::atomic<DWORD> id{0};
    std::atomic<bool> sawStop{false};
    std::atomic<int> messages{0};
    bool pauseStartup = false;
};

DWORD WINAPI Worker(void* parameter) {
    auto& context = *static_cast<Context*>(parameter);
    context.entered.Set();
    if (context.pauseStartup) context.gate.Wait();
    if (context.worker.StopRequested()) {
        context.sawStop = true;
        return 0;
    }
    MSG message;
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    context.id = GetCurrentThreadId();
    context.ready.Set();
    while (!context.worker.StopRequested()) {
        const DWORD wait = context.worker.WaitForMessages();
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_OBJECT_0 + 1) return 2;
        while (!context.worker.StopRequested() &&
               PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return 0;
            if (message.message == WM_APP + 9) {
                ++context.messages;
                context.consumed.Set();
            }
        }
    }
    context.sawStop = context.worker.StopRequested();
    return 0;
}

DWORD WINAPI StopWorker(void* parameter) {
    static_cast<Context*>(parameter)->worker.Stop();
    return 0;
}

DWORD WINAPI FailedStartup(void*) { return 1; }
}

int main() {
    // Warm up this process's own message queue before counting handles.
    MSG message;
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    DWORD before = 0, after = 0;
    Check(GetProcessHandleCount(GetCurrentProcess(), &before) != FALSE,
          "read initial process handle count");

    {
        Context context;
        context.pauseStartup = true;
        Check(context.worker.Start(Worker, &context), "start worker before publication");
        context.entered.Wait();
        HANDLE stopper = CreateThread(nullptr, 0, StopWorker, &context, 0, nullptr);
        Check(stopper != nullptr, "start unload while worker is gated before publication");
        const ULONGLONG deadline = GetTickCount64() + 3000;
        while (!context.worker.StopRequested() && GetTickCount64() < deadline) Sleep(1);
        Check(context.worker.StopRequested(), "unload records stop before any queue/window exists");
        context.gate.Set();
        Check(WaitForSingleObject(stopper, 3000) == WAIT_OBJECT_0,
              "unload joins gated worker after publication resumes");
        CloseHandle(stopper);
        Check(context.sawStop, "worker observes prepublication stop");
        Check(context.id == 0, "prepublication stop avoids message-queue setup");
        context.worker.Stop();
    }

    {
        Context context;
        Check(context.worker.Start(Worker, &context), "start idle message worker");
        context.ready.Wait();
        Check(!context.worker.Start(Worker, &context), "reject duplicate initialization");
        context.worker.Stop();
        Check(context.sawStop, "stop event wakes idle message wait");
    }

    {
        Context context;
        Check(context.worker.Start(Worker, &context), "start queued-work worker");
        context.ready.Wait();
        Check(PostThreadMessageW(context.id, WM_APP + 9, 0, 0) != FALSE,
              "post inert work to fixture queue");
        context.consumed.Wait();
        Check(context.messages == 1, "ordinary queued work is dispatched");
        for (int index = 0; index < 64; ++index)
            Check(PostThreadMessageW(context.id, WM_APP + 9, 0, 0) != FALSE,
                  "queue work before stop");
        context.worker.Stop();
        Check(context.sawStop, "stop remains observable while work is queued");
    }

    {
        CenterGeometryWorkerLifecycle worker;
        Check(!worker.Start(nullptr), "reject missing worker without allocating handles");
        worker.Stop();
        for (int index = 0; index < 100; ++index) {
            Check(worker.Start(FailedStartup), "initialize worker that immediately fails startup");
            worker.Stop();
            worker.Stop();
        }
    }

    Check(GetProcessHandleCount(GetCurrentProcess(), &after) != FALSE,
          "read final process handle count");
    Check(after == before, "repeated startup/unload releases worker and stop-event handles");
    printf("Center geometry lifecycle: %d checks passed. No windows or hooks created.\n", checks.load());
    return 0;
}

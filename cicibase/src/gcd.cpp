// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>
#if CICI_OS_MACOS
#include <dispatch/dispatch.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif  //CICI_OS_MACOS

#include "cicibase.h"
#include "gcd.h"
#include "log.h"

#if CICI_OS_WINDOWS
const wchar_t *wndClsName = L"DispatcherWnd";
const uint32_t kExecuteTaskOnMainThread = WM_USER + 100;

struct TaskPayload {
    std::function<void()> task;
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == kExecuteTaskOnMainThread) {
        assert(isMainThread());
        auto* payload = reinterpret_cast<TaskPayload*>(wParam);
        if (payload) {
            payload->task();
            delete payload; // Clean up the heap allocation
        }
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HWND createMsgWindow() {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = wndClsName;
    RegisterClassExW(&wc);

    return CreateWindowExW(0, wndClsName, L"cici", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
}

static HWND gHwnd = nullptr;
#endif

bool initCiciGCDEnviroment() {
#if CICI_OS_WINDOWS
    gHwnd = createMsgWindow();
    return gHwnd != nullptr;
#else
    return true;
#endif
}

#if CICI_OS_MACOS
// 1. Immediately on a background thread
bool asyncRun(std::function<void()>&& task) {
    auto taskPtr = new std::function<void()>(std::move(task));
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        assert(!isMainThread());
        (*taskPtr)();
        delete taskPtr;
    });
    return true;
}

// 2. Immediately on the main thread
void asyncRunUI(std::function<void()>&& task) {
    auto taskPtr = new std::function<void()>(std::move(task));
    dispatch_async(dispatch_get_main_queue(), ^{
        assert(isMainThread());
        (*taskPtr)();
        delete taskPtr;
    });
}

// 3. Delayed on a background thread
void asyncRunLater(std::function<void()>&& task, uint32_t ms) {
    auto taskPtr = new std::function<void()>(std::move(task));
    dispatch_time_t delay = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(ms * NSEC_PER_MSEC));
    dispatch_after(delay, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        assert(!isMainThread());
        (*taskPtr)();
        delete taskPtr;
    });
}

// 4. Delayed on the main thread
void asyncRunUILater(std::function<void()>&& task, uint32_t ms) {
    auto taskPtr = new std::function<void()>(std::move(task));
    dispatch_time_t delay = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(ms * NSEC_PER_MSEC));
    dispatch_after(delay, dispatch_get_main_queue(), ^{
        assert(isMainThread());
        (*taskPtr)();
        delete taskPtr;
    });
}
#elif CICI_OS_WINDOWS
// 1. Immediately on a background thread
bool asyncRun(std::function<void()>&& task) {
    auto* payload = new TaskPayload{ std::move(task) };
    if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, PVOID context) {
        assert(!isMainThread());
        auto* p = static_cast<TaskPayload*>(context);
        p->task();
        delete p;
    }, payload, nullptr)) {
        delete payload;
        return false;
    }
    return true;
}

// 2. Immediately on the main thread
void asyncRunUI(std::function<void()>&& task) {
    auto* payload = new TaskPayload{ std::move(task) };
    // Post to the main thread's message queue
    if (!PostMessage(gHwnd, kExecuteTaskOnMainThread, reinterpret_cast<WPARAM>(payload), 0)) {
        const auto error = GetLastError();
        delete payload; // Clean up if PostMessage fails
        LogError << "PostMessage for UI task failed:" << error << logEnd;
    }
}

// 3. Delayed on a background thread
void asyncRunLater(std::function<void()>&& task, uint32_t ms) {
    auto* payload = new TaskPayload{ std::move(task) };
    PTP_TIMER timer = CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER timer) {
        assert(!isMainThread());
        auto* p = static_cast<TaskPayload*>(context);
        p->task();
        CloseThreadpoolTimer(timer);
        delete p;
    }, payload, nullptr);

    ULARGE_INTEGER ul;
    ul.QuadPart = (ULONGLONG)(-(static_cast<LONGLONG>(ms) * 10000LL)); // Convert ms to 100ns units
    FILETIME ft = { ul.LowPart, ul.HighPart };
    SetThreadpoolTimer(timer, &ft, 0, 0);
}

// 4. Delayed on the main thread
void asyncRunUILater(std::function<void()>&& task, uint32_t ms) {
    auto* payload = new TaskPayload{ std::move(task) };
    // We use a Thread Pool Timer to handle the delay, then PostMessage to the UI
    PTP_TIMER timer = CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER timer) {
        assert(!isMainThread());
        // When timer expires, post the task to the UI thread
        PostMessage(gHwnd, kExecuteTaskOnMainThread, reinterpret_cast<WPARAM>(context), 0);
        CloseThreadpoolTimer(timer);
    }, payload, nullptr);

    ULARGE_INTEGER ul;
    ul.QuadPart = (ULONGLONG)(-(static_cast<LONGLONG>(ms) * 10000LL)); // Convert ms to 100ns units
    FILETIME ft = { ul.LowPart, ul.HighPart };
    SetThreadpoolTimer(timer, &ft, 0, 0);
}
#else
#error "Unsupported platform"
#endif

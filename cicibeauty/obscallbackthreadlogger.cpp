// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "obscallbackthreadlogger.h"

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <QCoreApplication>
#include <QThread>

#include "log.h"

namespace {

struct CallbackThreadLoggerState {
    std::mutex mutex;
    std::unordered_map<std::string, std::thread::id> threads;
    bool stopped = false;
};

CallbackThreadLoggerState& callbackThreadLoggerState() {
    // OBS can deliver teardown callbacks during static destruction.
    // Keep both the lock and its protected data alive until process exit.
    static auto* state = new CallbackThreadLoggerState;
    return *state;
}

const void* threadPtr(QThread* thread) {
    return static_cast<const void*>(thread);
}

}  // namespace

void stopObsCallbackThreadLogging() {
    auto& state = callbackThreadLoggerState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.stopped = true;
}

void logObsCallbackThread(const char* callbackName) {
    auto& state = callbackThreadLoggerState();
    // Serialize shutdown with the entire diagnostic, including Qt access and logging.
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.stopped) {
        return;
    }

    const std::string name = callbackName != nullptr ? callbackName : "(null)";
    const auto currentThread = std::this_thread::get_id();

    std::thread::id previousThread;
    bool firstSeen = false;
    bool changed = false;

    auto it = state.threads.find(name);
    if (it == state.threads.end()) {
        state.threads.emplace(name, currentThread);
        firstSeen = true;
    } else if (it->second != currentThread) {
        previousThread = it->second;
        it->second = currentThread;
        changed = true;
    }

    if (!firstSeen && !changed) {
        return;
    }

    QCoreApplication* app = QCoreApplication::instance();
    QThread* currentQtThread = QThread::currentThread();
    QThread* mainQtThread = app != nullptr ? app->thread() : nullptr;
    const bool isQtMainThread = mainQtThread != nullptr && currentQtThread == mainQtThread;

    if (firstSeen) {
        LogInfo << "OBS callback thread first seen callback=" << name
                << " currentStdThread=" << currentThread
                << " currentQThread=" << threadPtr(currentQtThread)
                << " qtMainQThread=" << threadPtr(mainQtThread)
                << " isQtMainThread=" << isQtMainThread << logEnd;
        return;
    }

    LogWarn << "OBS callback thread changed callback=" << name
            << " previousStdThread=" << previousThread
            << " currentStdThread=" << currentThread
            << " currentQThread=" << threadPtr(currentQtThread)
            << " qtMainQThread=" << threadPtr(mainQtThread)
            << " isQtMainThread=" << isQtMainThread << logEnd;
}

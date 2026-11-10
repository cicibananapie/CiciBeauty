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

std::mutex& callbackThreadMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, std::thread::id>& callbackThreads() {
    static std::unordered_map<std::string, std::thread::id> threads;
    return threads;
}

const void* threadPtr(QThread* thread) {
    return static_cast<const void*>(thread);
}

}  // namespace

void logObsCallbackThread(const char* callbackName) {
    const std::string name = callbackName != nullptr ? callbackName : "(null)";
    const auto currentThread = std::this_thread::get_id();

    std::thread::id previousThread;
    bool firstSeen = false;
    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(callbackThreadMutex());
        auto& threads = callbackThreads();
        auto it = threads.find(name);
        if (it == threads.end()) {
            threads.emplace(name, currentThread);
            firstSeen = true;
        } else if (it->second != currentThread) {
            previousThread = it->second;
            it->second = currentThread;
            changed = true;
        }
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

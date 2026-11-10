// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>
#include <climits>
#include <chrono>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <thread>

#include "log.h"
#include "misc.h"
#include "sync.h"

Semaphore::~Semaphore() {
    close();
}

bool Semaphore::create(const std::u8string& name, uint32_t initialCount) {
    if (name.empty() || initialCount >= INT_MAX) {
        return false;
    }
#if CICI_OS_MACOS
    if (sem_ != SEM_FAILED) {
        return false;
    }
    sem_ = sem_open(to_char(name), O_CREAT | O_EXCL, 0600, initialCount);
    if (sem_ == SEM_FAILED) {
        LogError << "sem_open(create) failed: " << strerror(errno) << logEnd;
        return false;
    }
    name_ = name;
    creator_ = true;
#elif CICI_OS_WINDOWS
    if (handle_) {
        return false;
    }
    handle_ = CreateSemaphoreW(nullptr, static_cast<LONG>(initialCount), LONG_MAX, utf8ToWide(name).c_str());
    const DWORD error = GetLastError();
    if (!handle_ || error == ERROR_ALREADY_EXISTS) {
        if (handle_) {
            CloseHandle(handle_);
        }
        handle_ = nullptr;
        LogError << "CreateSemaphoreW failed: " << error << logEnd;
        return false;
    }
#endif
    return true;
}

bool Semaphore::open(const std::u8string& name) {
    if (name.empty()) {
        return false;
    }
#if CICI_OS_MACOS
    if (sem_ != SEM_FAILED) {
        return false;
    }
    sem_ = sem_open(to_char(name), 0);
    if (sem_ == SEM_FAILED) {
        LogError << "sem_open(open) failed: " << strerror(errno) << logEnd;
        return false;
    }
#elif CICI_OS_WINDOWS
    if (handle_) {
        return false;
    }
    handle_ = OpenSemaphoreW(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE, utf8ToWide(name).c_str());
    if (!handle_) {
        LogError << "OpenSemaphoreW failed: " << GetLastError() << logEnd;
        return false;
    }
#endif
    return true;
}

void Semaphore::close() {
#if CICI_OS_MACOS
    if (sem_ != SEM_FAILED) {
        if (creator_) {
            sem_unlink(to_char(name_));
        }
        sem_close(sem_);
        sem_ = SEM_FAILED;
    }
    creator_ = false;
    name_.clear();
#elif CICI_OS_WINDOWS
    if (handle_) {
        CloseHandle(handle_);
    }
    handle_ = nullptr;
#endif
}

Semaphore::Result Semaphore::tryWait() {
#if CICI_OS_MACOS
    if (sem_ == SEM_FAILED) {
        return Result::Error;
    }
    for (;;) {
        if (sem_trywait(sem_) == 0) {
            return Result::Acquired;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN) {
            return Result::WouldBlock;
        }
        LogError << "sem_trywait failed: " << strerror(errno) << logEnd;
        return Result::Error;
    }
#elif CICI_OS_WINDOWS
    if (!handle_) {
        return Result::Error;
    }
    const DWORD result = WaitForSingleObject(handle_, 0);
    if (result == WAIT_OBJECT_0) {
        return Result::Acquired;
    }
    if (result == WAIT_TIMEOUT) {
        return Result::WouldBlock;
    }
    LogError << "semaphore wait failed: " << GetLastError() << logEnd;
    return Result::Error;
#endif
}

Semaphore::Result Semaphore::wait() {
#if CICI_OS_MACOS
    if (sem_ == SEM_FAILED) {
        return Result::Error;
    }
    for (;;) {
        if (sem_wait(sem_) == 0) {
            return Result::Acquired;
        }
        if (errno == EINTR) {
            continue;
        }
        LogError << "sem_wait failed: " << strerror(errno) << logEnd;
        return Result::Error;
    }
#elif CICI_OS_WINDOWS
    if (!handle_) {
        return Result::Error;
    }
    if (WaitForSingleObject(handle_, INFINITE) == WAIT_OBJECT_0) {
        return Result::Acquired;
    }
    LogError << "semaphore wait failed: " << GetLastError() << logEnd;
    return Result::Error;
#endif
}

bool Semaphore::signal() {
#if CICI_OS_MACOS
    if (sem_ == SEM_FAILED) {
        return false;
    }
    if (sem_post(sem_) == 0) {
        return true;
    }
    LogError << "sem_post failed: " << strerror(errno) << logEnd;
#elif CICI_OS_WINDOWS
    if (!handle_) {
        return false;
    }
    if (ReleaseSemaphore(handle_, 1, nullptr)) {
        return true;
    }
    LogError << "ReleaseSemaphore failed: " << GetLastError() << logEnd;
#endif
    return false;
}

Mutex::Mutex(const char8_t* name) {
#if CICI_OS_MACOS
	name_ = std::u8string(name);
    sem_ = sem_open(to_char(name), O_CREAT, 0666, 1);
    if (sem_ == SEM_FAILED) {
        LogError<<"Open failed: "<<strerror(errno)<<" (Error code: "<<errno<<")"<<logEnd;
    }
    assert(sem_ != SEM_FAILED);
#elif CICI_OS_WINDOWS
	handle_ = CreateMutexW(NULL, FALSE, utf8ToWide(name).c_str());
#else
#error "Unsupported platform"
#endif
}

Mutex::~Mutex() {
#if CICI_OS_MACOS
    sem_unlink(to_char(name_));
    sem_close(sem_);
#elif CICI_OS_WINDOWS
    CloseHandle(handle_);
#else
#error "Unsupported platform"
#endif
}

void Mutex::lock() {
#if CICI_OS_MACOS
    sem_wait(sem_);
#elif CICI_OS_WINDOWS
    WaitForSingleObject(handle_, INFINITE);
#else
#error "Unsupported platform"
#endif
}

bool Mutex::lockFor(uint32_t timeoutMs) {
#if CICI_OS_MACOS
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        if (sem_trywait(sem_) == 0) {
            return true;
        }
        const auto error = errno;
        if (error != EAGAIN && error != EINTR) {
            LogError << "sem_trywait failed: " << strerror(error) << " (Error code: " << error << ")" << logEnd;
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        if (error == EINTR) {
            continue;
        }

        const auto nextAttempt = now + std::chrono::milliseconds(10);
        std::this_thread::sleep_until(nextAttempt < deadline ? nextAttempt : deadline);
    }
#elif CICI_OS_WINDOWS
    const auto result = WaitForSingleObject(handle_, timeoutMs);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
        return true;
    }
    if (result != WAIT_TIMEOUT) {
        LogError << "WaitForSingleObject failed: " << GetLastError() << logEnd;
    }
    return false;
#else
#error "Unsupported platform"
#endif
}

void Mutex::unlock() {
#if CICI_OS_MACOS
    sem_post(sem_);
#elif CICI_OS_WINDOWS
    ReleaseMutex(handle_);
#else
#error "Unsupported platform"
#endif
}

Event::Event(const char8_t* name, bool manual)
#if CICI_OS_MACOS
    : manual_(manual)
#endif
{
#if CICI_OS_MACOS
	assert(!manual_);
    pipe_fd_ = open(to_char(name), O_RDWR);
    if (pipe_fd_ == -1) {
        // Create the pipe file if it does not exist.
        pipe_fd_ = open(to_char(name), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (pipe_fd_ == -1) {
            LogError<<"Open failed: "<<strerror(errno)<<" (Error code: "<<errno<<")"<<logEnd;
        }
        assert(pipe_fd_ != -1);
    }
        
    // Initialize kqueue.
    kq_ = kqueue();
    assert(kq_ != -1);
    if (kq_ == -1) {
        LogError<<"kqueue failed: "<<strerror(errno)<<" (Error code: "<<errno<<")"<<logEnd;
    }
        
    // Monitor readable events on the pipe.
    struct kevent ev;
    EV_SET(&ev, pipe_fd_, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    int r = kevent(kq_, &ev, 1, NULL, 0, NULL);
    assert(r != -1);
    if (r == -1) {
        LogError<<"kevent failed: "<<strerror(errno)<<" (Error code: "<<errno<<")"<<logEnd;
    }
#elif CICI_OS_WINDOWS
	handle_ = CreateEventW(NULL, manual ? TRUE : FALSE, FALSE, utf8ToWide(name).c_str());
    if (handle_ == nullptr) {
        LogError<<"CreateEventW failed"<<logEnd;
    }
    assert(handle_ != nullptr);
#else
#error "Unsupported platform"
#endif
}

Event::~Event() {
#if CICI_OS_MACOS
    close(kq_);
    close(pipe_fd_);
#elif CICI_OS_WINDOWS
    CloseHandle(handle_);
#else
#error "Unsupported platform"
#endif
}

void Event::signal() {
#if CICI_OS_MACOS
    char c = '1';
    ssize_t r = write(pipe_fd_, &c, 1);
    if (r == -1) {
        LogError<<"write failed"<<logEnd;
    }
    assert(r != -1);
#elif CICI_OS_WINDOWS
    SetEvent(handle_);
#else
#error "Unsupported platform"
#endif
}

void Event::wait() {
#if CICI_OS_MACOS
    struct kevent ev;
    // Block until the event is triggered.
    int nev = kevent(kq_, NULL, 0, &ev, 1, NULL);
    assert(nev > 0);
    if (!manual_) {
        reset();
    }
#elif CICI_OS_WINDOWS
    WaitForSingleObject(handle_, INFINITE);
#else
#error "Unsupported platform"
#endif
}

bool Event::waitFor(uint32_t timeoutMs) {
#if CICI_OS_MACOS
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = now < deadline
            ? deadline - now
            : std::chrono::steady_clock::duration::zero();
        const auto remainingNs = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
        const struct timespec timeout = {
            static_cast<time_t>(remainingNs / 1000000000LL),
            static_cast<long>(remainingNs % 1000000000LL)
        };

        struct kevent ev = {};
        const int result = kevent(kq_, NULL, 0, &ev, 1, &timeout);
        if (result > 0) {
            if ((ev.flags & EV_ERROR) != 0) {
                LogError << "kevent event error: " << ev.data << logEnd;
                return false;
            }
            if (!manual_) {
                reset();
            }
            return true;
        }
        if (result == 0) {
            return false;
        }

        const auto error = errno;
        if (error == EINTR) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            continue;
        }

        LogError << "kevent failed: " << strerror(error) << " (Error code: " << error << ")" << logEnd;
        return false;
    }
#elif CICI_OS_WINDOWS
    const auto result = WaitForSingleObject(handle_, timeoutMs);
    if (result == WAIT_OBJECT_0) {
        return true;
    }
    if (result != WAIT_TIMEOUT) {
        LogError << "WaitForSingleObject failed: " << GetLastError() << logEnd;
    }
    return false;
#else
#error "Unsupported platform"
#endif
}

void Event::reset() {
#if CICI_OS_MACOS
    char buf[1024];
    while (read(pipe_fd_, buf, sizeof(buf)) > 0);
#elif CICI_OS_WINDOWS
    ResetEvent(handle_);
#else
#error "Unsupported platform"
#endif
}

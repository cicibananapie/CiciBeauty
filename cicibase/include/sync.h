// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>
#include <string>

#if CICI_OS_MACOS
#include <semaphore.h>
#include <sys/event.h>
#include <unistd.h>
#include <fcntl.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

// Named, cross-process counting semaphore. Only the creator unlinks the name
// on macOS; existing open handles remain usable. create() never reuses a name.
class Semaphore {
public:
    enum class Result {
        Acquired,
        WouldBlock,
        Error
    };

    Semaphore() = default;
    ~Semaphore();
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    bool create(const std::u8string& name, uint32_t initialCount);
    bool open(const std::u8string& name);
    void close(); // Local handle only; callers must first end local waits.
    Result tryWait();
    Result wait();
    bool signal();

private:
#if CICI_OS_MACOS
    sem_t* sem_ = SEM_FAILED;
    std::u8string name_;
    bool creator_ = false;
#elif CICI_OS_WINDOWS
    HANDLE handle_ = nullptr;
#endif
};

class Mutex {
public:
    Mutex(const char8_t* name);
    Mutex(const std::u8string& name): Mutex(name.c_str()) {}
    ~Mutex();
    
public:
    void lock();
    bool lockFor(uint32_t timeoutMs);
    void unlock();
    
private:
#if CICI_OS_MACOS
    sem_t* sem_ = SEM_FAILED;
    std::u8string name_;
#elif CICI_OS_WINDOWS
    HANDLE handle_ = nullptr;
#else
#error "Unsupported platform"
#endif
};

class AutoLocker {
public:
    AutoLocker(Mutex& m):m_(m) {
        m.lock();
    }
    ~AutoLocker() {
        m_.unlock();
    }
    
private:
    Mutex& m_;
};

class Event {
public:
    Event(const char8_t* name, bool manual);
    Event(const std::u8string& name, bool manual): Event(name.c_str(), manual) {}
    ~Event();
    
public:
    void signal();
    void wait();
    bool waitFor(uint32_t timeoutMs);
    
protected:
    void reset();
    
private:
#if CICI_OS_MACOS
    int pipe_fd_ = -1;        // Pipe file descriptor.
    int kq_ = -1;             // kqueue handle.
    bool manual_ = false;
#elif CICI_OS_WINDOWS
    HANDLE handle_ = nullptr;
#else
#error "Unsupported platform"
#endif
};

class AutoEvent : public Event{
public:
    AutoEvent(const char8_t* name):Event(name, false) {}
    AutoEvent(const std::u8string& name):Event(name, false) {}
};

class ManualEvent : public Event{
public:
    ManualEvent(const char8_t* name):Event(name, true) {}
    ManualEvent(const std::u8string& name):Event(name, true) {}
    
public:
    void reset() {
        Event::reset();
    }
};

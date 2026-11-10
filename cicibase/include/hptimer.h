// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>
#include <time.h>

#if CICI_OS_WINDOWS
#include <Windows.h>
#endif

class HPTimer {
public:
    HPTimer() {
#if CICI_OS_WINDOWS
        QueryPerformanceFrequency(&freq_);
#endif
    }
    
    ~HPTimer() {}
    
public:
    void start() {
#if CICI_OS_MACOS
        clock_gettime(CLOCK_MONOTONIC, &startTime_);
#elif CICI_OS_WINDOWS
        QueryPerformanceCounter(&startTime_);
#else
#error "Unsupported platform"
#endif
        lastTime_ = startTime_;
    }
    
    uint64_t elapsedNS() {
#if CICI_OS_MACOS
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t start_ns = (uint64_t)startTime_.tv_sec * 1000000000 + startTime_.tv_nsec;
        uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
        lastTime_ = now;
        
        return now_ns - start_ns;
#elif CICI_OS_WINDOWS
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        lastTime_ = now;
        
        return (uint64_t)((now.QuadPart - startTime_.QuadPart) * 1000000000.0 / freq_.QuadPart);
#else
#error "Unsupported platform"
#endif
    }
    
    uint64_t elapsedUS() {
        return elapsedNS() / 1000;
    }
    
    uint64_t elapsedMS() {
        return elapsedNS() / 1000000;
    }
    
    double elapsedS() {
        return elapsedNS() / 1000000000.0;
    }
    
    uint64_t lastElapsedNS() {
#if CICI_OS_MACOS
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t start_ns = (uint64_t)lastTime_.tv_sec * 1000000000 + lastTime_.tv_nsec;
        uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
        lastTime_ = now;
        
        return now_ns - start_ns;
#elif CICI_OS_WINDOWS
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        auto t = (uint64_t)((now.QuadPart - lastTime_.QuadPart) * 1000000000.0 / freq_.QuadPart);
        lastTime_ = now;
        
        return t;
#else
#error "Unsupported platform"
#endif
    }
    
    uint64_t lastElapsedUS() {
        return lastElapsedNS() / 1000;
    }
    
    uint64_t lastElapsedMS() {
        return lastElapsedNS() / 1000000;
    }
    
    double lastElapsedS() {
        return lastElapsedNS() / 1000000000.0;
    }
    
private:
#if CICI_OS_MACOS
    struct timespec startTime_;
    struct timespec lastTime_;
#elif CICI_OS_WINDOWS
	LARGE_INTEGER startTime_;
	LARGE_INTEGER lastTime_;
	LARGE_INTEGER freq_;
#else
#error "Unsupported platform"
#endif
};

// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <string>

#include "sync.h"

// Fixed-slot, single-producer/single-consumer cross-process FIFO. Slot contents
// are opaque: image formats, timestamps and dropping policies belong to users.
// One producer and one consumer across ALL mappings; at most one outstanding
// write and one outstanding read. Views expire on commit/cancel/release.
class SHMFrame {
public:
    enum class Result {
        Success,
        WouldBlock,
        Closed,
        InvalidOperation,
        Error
    };

    SHMFrame() = default;
    ~SHMFrame();
    SHMFrame(const SHMFrame&) = delete;
    SHMFrame& operator=(const SHMFrame&) = delete;

    // Portable name: 1..24 ASCII letters, digits, '_' or '-'. Use a fresh name
    // for every channel generation. Publish it only after create() succeeds.
    bool create(const std::u8string& name, uint32_t capacity, uint32_t frameBytes);
    bool open(const std::u8string& name);
    uint32_t capacity() const;
    uint32_t frameBytes() const;
    bool isClosed() const;
    // All committed records have been released, including any held read view.
    // For retirement, the producer must have permanently stopped writing first.
    bool isDrained() const;
    // Committed records not yet released, INCLUDING the currently held read.
    uint32_t readableCount() const;

    // wait=false never waits for data/space. wait=true is for worker threads.
    Result acquireWrite(std::span<uint8_t>& frame, bool wait = false);
    Result commitWrite(uint32_t bytes);
    Result cancelWrite();
    Result acquireRead(bool wait = false);     // A held read must be explicitly released.
    std::span<const uint8_t> readView() const; // View of the held read; empty without acquireRead().
    Result releaseRead();

    // May run concurrently with queue operations. Permanently closes BOTH
    // endpoints and wakes blocked acquisitions; does not invalidate held views.
    // Call on peer exit too. End local operations/leases before destruction.
    void close();

private:
    struct Header;
    Result acquire(Semaphore& semaphore, bool wait);
    Result publish(Semaphore& semaphore);
    bool openSemaphores(bool create);
    void reset(); // No concurrent local operations; also cleans partial opens.
    uint8_t* slot(uint64_t position) const;

    Header* header_ = nullptr;
    size_t mappedBytes_ = 0;
    std::u8string name_;
    bool creator_ = false;
    std::atomic<bool> closeNotified_{false};
#if CICI_OS_MACOS
    int fd_ = -1;
#elif CICI_OS_WINDOWS
    HANDLE mapping_ = nullptr;
#endif
    Semaphore readable_;
    Semaphore writable_;
    bool writing_ = false;
    bool reading_ = false;
    uint64_t writePosition_ = 0;
    uint64_t readPosition_ = 0;
};

// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "shmframe.h"

#include <climits>
#include <cstring>
#include <limits>
#include <string_view>

#include "log.h"
#include "misc.h"

#if CICI_OS_MACOS
#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

namespace {
constexpr uint64_t kMagic = 0x4349434946524d31ULL;
constexpr uint32_t kVersion = 1;
constexpr uint64_t kAlignment = 64;
// Each slot starts with its committed byte count; payload starts on a cache line.
constexpr uint64_t kSlotHeaderBytes = kAlignment;

uint64_t load(const uint64_t* value) {
#if CICI_OS_MACOS
    static_assert(__atomic_always_lock_free(sizeof(uint64_t), nullptr));
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
#elif CICI_OS_WINDOWS
    return static_cast<uint64_t>(
        InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(const_cast<uint64_t*>(value)), 0, 0));
#endif
}

uint64_t exchange(uint64_t* value, uint64_t next) {
#if CICI_OS_MACOS
    return __atomic_exchange_n(value, next, __ATOMIC_ACQ_REL);
#elif CICI_OS_WINDOWS
    return static_cast<uint64_t>(
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(value), static_cast<LONG64>(next)));
#endif
}

void store(uint64_t* value, uint64_t next) {
#if CICI_OS_MACOS
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
#elif CICI_OS_WINDOWS
    exchange(value, next);
#endif
}

bool validName(std::u8string_view name) {
    if (name.empty() || name.size() > 24) {
        return false;
    }
    for (char8_t c : name) {
        if (!((c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') || (c >= u8'0' && c <= u8'9') || c == u8'_' ||
              c == u8'-')) {
            return false;
        }
    }
    return true;
}

std::u8string objectName(const std::u8string& name) {
#if CICI_OS_MACOS
    return u8"/" + name;
#elif CICI_OS_WINDOWS
    return u8"Local\\" + name;
#endif
}

uint64_t slotStride(uint32_t bytes) {
    return (kSlotHeaderBytes + bytes + kAlignment - 1) & ~(kAlignment - 1);
}
} // namespace

struct alignas(64) SHMFrame::Header {
    uint64_t magic;
    uint32_t version;
    uint32_t capacity;
    uint32_t frameBytes;
    uint32_t reserved;
    uint64_t mappingBytes;
    uint64_t slotStride;
    alignas(64) uint64_t readPosition;
    alignas(64) uint64_t writePosition;
    uint64_t closed;
};

SHMFrame::~SHMFrame() {
    close();
    reset();
}

bool SHMFrame::openSemaphores(bool create) {
    const auto prefix = objectName(name_);
    if (create) {
        return readable_.create(prefix + u8"_r", 0) && writable_.create(prefix + u8"_w", header_->capacity);
    }
    return readable_.open(prefix + u8"_r") && writable_.open(prefix + u8"_w");
}

bool SHMFrame::create(const std::u8string& name, uint32_t count, uint32_t bytes) {
    static_assert(sizeof(Header) == 192);
    if (header_ || !validName(name) || count == 0 || count >= INT_MAX || bytes == 0) {
        return false;
    }
    const uint64_t stride = slotStride(bytes);
    const uint64_t size = sizeof(Header) + stride * count;
    if (size > (std::numeric_limits<size_t>::max)() ||
        size > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
        return false;
    }
    name_ = name;
    const auto nativeName = objectName(name);
#if CICI_OS_MACOS
    fd_ = shm_open(to_char(nativeName), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd_ != -1) {
        creator_ = true;
    }
    if (fd_ == -1 || ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        LogError << "create frame mapping failed: " << strerror(errno) << logEnd;
        reset();
        return false;
    }
    void* memory = mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (memory != MAP_FAILED) {
        header_ = static_cast<Header*>(memory);
    }
#elif CICI_OS_WINDOWS
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(size >> 32),
                                  static_cast<DWORD>(size), utf8ToWide(nativeName).c_str());
    const DWORD error = GetLastError();
    if (!mapping_ || error == ERROR_ALREADY_EXISTS) {
        LogError << "create frame mapping failed: " << error << logEnd;
        reset();
        return false;
    }
    creator_ = true;
    header_ = static_cast<Header*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(size)));
#endif
    if (!header_) {
        LogError << "map frame memory failed" << logEnd;
        reset();
        return false;
    }
    mappedBytes_ = static_cast<size_t>(size);
    std::memset(header_, 0, sizeof(Header));
    header_->version = kVersion;
    header_->capacity = count;
    header_->frameBytes = bytes;
    header_->mappingBytes = size;
    header_->slotStride = stride;
    if (!openSemaphores(true)) {
        reset();
        return false;
    }
    // Opening is allowed only after all immutable layout fields and semaphores exist.
    store(&header_->magic, kMagic);
    return true;
}

bool SHMFrame::open(const std::u8string& name) {
    if (header_ || !validName(name)) {
        return false;
    }
    name_ = name;
    const auto nativeName = objectName(name);
#if CICI_OS_MACOS
    fd_ = shm_open(to_char(nativeName), O_RDWR, 0);
    struct stat info{};
    if (fd_ == -1 || fstat(fd_, &info) != 0 || info.st_size < static_cast<off_t>(sizeof(Header))) {
        LogError << "open frame mapping failed" << logEnd;
        reset();
        return false;
    }
    const uint64_t actualBytes = static_cast<uint64_t>(info.st_size);
    void* memory = mmap(nullptr, sizeof(Header), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (memory != MAP_FAILED) {
        header_ = static_cast<Header*>(memory);
    }
#elif CICI_OS_WINDOWS
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, utf8ToWide(nativeName).c_str());
    if (mapping_) {
        header_ = static_cast<Header*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Header)));
    }
#endif
    if (!header_) {
        LogError << "map frame header failed" << logEnd;
        reset();
        return false;
    }
    mappedBytes_ = sizeof(Header);
    if (load(&header_->magic) != kMagic) {
        LogError << "frame mapping is not initialized" << logEnd;
        reset();
        return false;
    }
    const uint64_t stride = slotStride(header_->frameBytes);
    const uint64_t size = sizeof(Header) + stride * header_->capacity;
    bool valid = header_->version == kVersion && header_->capacity > 0 && header_->capacity < INT_MAX &&
                 header_->frameBytes > 0 && header_->slotStride == stride && header_->mappingBytes == size &&
                 size <= (std::numeric_limits<size_t>::max)() && load(&header_->closed) == 0;
#if CICI_OS_MACOS
    // Darwin rounds a POSIX shared-memory object's size up to a VM page.
    valid = valid && size <= actualBytes;
#endif
    if (!valid) {
        LogError << "invalid or closed frame mapping" << logEnd;
        reset();
        return false;
    }
#if CICI_OS_MACOS
    munmap(header_, mappedBytes_);
    header_ = nullptr;
    memory = mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (memory != MAP_FAILED) {
        header_ = static_cast<Header*>(memory);
    }
#elif CICI_OS_WINDOWS
    UnmapViewOfFile(header_);
    header_ = static_cast<Header*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(size)));
#endif
    mappedBytes_ = static_cast<size_t>(size);
    if (!header_ || !openSemaphores(false) || isClosed()) {
        LogError << "open frame channel failed" << logEnd;
        reset();
        return false;
    }
    return true;
}

void SHMFrame::reset() {
    readable_.close();
    writable_.close();
#if CICI_OS_MACOS
    if (header_) {
        munmap(header_, mappedBytes_);
    }
    if (fd_ != -1) {
        ::close(fd_);
    }
    if (creator_) {
        shm_unlink(to_char(objectName(name_)));
    }
    fd_ = -1;
#elif CICI_OS_WINDOWS
    if (header_) {
        UnmapViewOfFile(header_);
    }
    if (mapping_) {
        CloseHandle(mapping_);
    }
    mapping_ = nullptr;
#endif
    header_ = nullptr;
    mappedBytes_ = 0;
    creator_ = false;
    closeNotified_.store(false, std::memory_order_relaxed);
    name_.clear();
    writing_ = reading_ = false;
}

uint32_t SHMFrame::capacity() const {
    return header_ ? header_->capacity : 0;
}
uint32_t SHMFrame::frameBytes() const {
    return header_ ? header_->frameBytes : 0;
}
bool SHMFrame::isClosed() const {
    return !header_ || load(&header_->closed) != 0;
}
bool SHMFrame::isDrained() const {
    return header_ && load(&header_->readPosition) == load(&header_->writePosition);
}
uint32_t SHMFrame::readableCount() const {
    if (!header_) {
        return 0;
    }
    const auto read = load(&header_->readPosition);
    return static_cast<uint32_t>(load(&header_->writePosition) - read);
}

uint8_t* SHMFrame::slot(uint64_t position) const {
    return reinterpret_cast<uint8_t*>(header_) + sizeof(Header) + (position % header_->capacity) * header_->slotStride;
}

SHMFrame::Result SHMFrame::acquire(Semaphore& semaphore, bool wait) {
    if (!header_) {
        return Result::Error;
    }
    if (isClosed()) {
        return Result::Closed;
    }
    const auto result = wait ? semaphore.wait() : semaphore.tryWait();
    if (isClosed()) {
        return Result::Closed; // close() posts wakeups, not frames.
    }
    if (result == Semaphore::Result::WouldBlock) {
        return Result::WouldBlock;
    }
    if (result == Semaphore::Result::Error) {
        close();
        return Result::Error;
    }
    return Result::Success;
}

SHMFrame::Result SHMFrame::acquireWrite(std::span<uint8_t>& frame, bool wait) {
    frame = {};
    if (writing_) {
        return Result::InvalidOperation;
    }
    const auto result = acquire(writable_, wait);
    if (result != Result::Success) {
        return result;
    }
    writePosition_ = load(&header_->writePosition);
    // Acquire the consumer's release before reusing a previously read slot.
    if (writePosition_ - load(&header_->readPosition) >= header_->capacity) {
        LogError << "invalid shared frame write position" << logEnd;
        close();
        return Result::Error;
    }
    writing_ = true;
    frame = {slot(writePosition_) + kSlotHeaderBytes, header_->frameBytes};
    return Result::Success;
}

SHMFrame::Result SHMFrame::publish(Semaphore& semaphore) {
    if (semaphore.signal()) {
        return Result::Success;
    }
    close();
    return Result::Error;
}

SHMFrame::Result SHMFrame::commitWrite(uint32_t bytes) {
    if (!writing_ || bytes > frameBytes()) {
        return Result::InvalidOperation;
    }
    writing_ = false;
    if (isClosed()) {
        return Result::Closed;
    }
    std::memcpy(slot(writePosition_), &bytes, sizeof(bytes));
    store(&header_->writePosition, writePosition_ + 1);
    return publish(readable_);
}

SHMFrame::Result SHMFrame::cancelWrite() {
    if (!writing_) {
        return Result::InvalidOperation;
    }
    writing_ = false;
    return isClosed() ? Result::Closed : publish(writable_);
}

SHMFrame::Result SHMFrame::acquireRead(bool wait) {
    if (reading_) {
        return Result::InvalidOperation;
    }
    if (!header_) {
        return Result::Error;
    }
    // Readability comes from the committed cursor. Drain one notification if
    // present; its post may lag the cursor, so WouldBlock does not deny a read.
    const auto result = acquire(readable_, false);
    if (result != Result::Success && result != Result::WouldBlock) {
        return result;
    }
    readPosition_ = load(&header_->readPosition);
    while (load(&header_->writePosition) == readPosition_) {
        if (!wait) {
            return Result::WouldBlock;
        }
        const auto wake = acquire(readable_, true);
        if (wake != Result::Success) {
            return wake;
        }
    }
    // The acquire load pairs with commitWrite's release before reading payload.
    const uint64_t available = load(&header_->writePosition) - readPosition_;
    uint32_t bytes = 0;
    std::memcpy(&bytes, slot(readPosition_), sizeof(bytes));
    if (available == 0 || available > header_->capacity || bytes > header_->frameBytes) {
        LogError << "invalid shared frame slot" << logEnd;
        close();
        return Result::Error;
    }
    reading_ = true;
    return Result::Success;
}

std::span<const uint8_t> SHMFrame::readView() const {
    if (!reading_) {
        return {};
    }
    uint32_t bytes;
    std::memcpy(&bytes, slot(readPosition_), sizeof(bytes));
    return {slot(readPosition_) + kSlotHeaderBytes, bytes};
}

SHMFrame::Result SHMFrame::releaseRead() {
    if (!reading_) {
        return Result::InvalidOperation;
    }
    reading_ = false;
    store(&header_->readPosition, readPosition_ + 1);
    return isClosed() ? Result::Closed : publish(writable_);
}

void SHMFrame::close() {
    if (!header_ || load(&header_->magic) != kMagic) {
        return;
    }
    exchange(&header_->closed, 1);
    if (!closeNotified_.exchange(true, std::memory_order_relaxed)) {
        // One producer and one consumer: at most one waiter on each semaphore.
        // Wake locally even if the peer set closed then died before signaling.
        readable_.signal();
        writable_.signal();
    }
}

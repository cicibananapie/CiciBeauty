// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <string.h>

#if CICI_OS_MACOS
#include <pthread.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

#include "log.h"
#include "misc.h"
#include "shmem.h"

const uint32_t KMemorySize = 4096;
const uint32_t KMaxSpace = KMemorySize - 2*sizeof(uint32_t);

ShareMemoryIPC::ShareMemoryIPC(const std::u8string& shmem, const std::u8string& mutex, const std::u8string& readEvt, const std::u8string& writeEvt, bool bServer)
: name_(shmem)
, bServer_(bServer)
, ipcMtx_(mutex)
, readEvt_(readEvt)
, writeEvt_(writeEvt)
{
#if CICI_OS_MACOS
    shmemFd_ = shm_open(to_char(name_), O_CREAT|O_RDWR, 0666);
    if (shmemFd_ == -1) {
        LogError<<"Open failed: "<<strerror(errno)<<" (Error code: "<<errno<<")"<<logEnd;
        return;
    }
    assert(shmemFd_ != -1);
    int r = ftruncate(shmemFd_, KMemorySize);
    shareMem_ = static_cast<char*>(mmap(NULL, KMemorySize, PROT_READ|PROT_WRITE, MAP_SHARED, shmemFd_, 0));
    if (shareMem_ == MAP_FAILED) {
        int err = errno;
        LogError <<"mmap failed:" << err << "\t" << strerror(err) <<logEnd;
        return;
    }
    assert(shareMem_ != nullptr);
#elif CICI_OS_WINDOWS
	handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, KMemorySize, utf8ToWide(shmem).c_str());
    if (handle_ == nullptr) {
        LogError << "CreateFileMappingW failed:" << GetLastError() << logEnd;
        return;
    }
    assert(handle_ != nullptr);
    shareMem_ = static_cast<char*>(MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (shareMem_ == nullptr) {
        LogError << "MapViewOfFile failed:" << GetLastError() << logEnd;
        return;
    }
    assert(shareMem_ != nullptr);
#else
#error "Unsupported platform"
#endif
    pReadPos_ = reinterpret_cast<int32_t*>(shareMem_);
    pWritePos_ = pReadPos_ + 1;
    bServer_ = bServer;
    if (bServer_) {
        *pReadPos_ = 0;
        *pWritePos_ = 0;
    }
}

ShareMemoryIPC::~ShareMemoryIPC() {
#if CICI_OS_MACOS
    munmap(shareMem_, KMemorySize);
    close(shmemFd_);
    shm_unlink(to_char(name_));
#elif CICI_OS_WINDOWS
    UnmapViewOfFile(shareMem_);
    CloseHandle(handle_);
#else
#error "Unsupported platform"
#endif
}

bool ShareMemoryIPC::empty() const {
    return *pReadPos_ == *pWritePos_;
}

bool ShareMemoryIPC::full() const {
    return (*pWritePos_ + 1)%(KMaxSpace) == *pReadPos_;
}

uint32_t ShareMemoryIPC::availableSpace() const {
    return (*pReadPos_ - *pWritePos_ -1 + KMaxSpace)%KMaxSpace;
}

int32_t ShareMemoryIPC::read(BinaryStream &buf) {
    assert(!empty());
    int32_t n = 0;
    if (*pWritePos_ > *pReadPos_) {
        const char* msg = shareMem_ + 2*sizeof(uint32_t) + *pReadPos_;
        int32_t len = *pWritePos_ - *pReadPos_;
        buf.append(msg, len);
        *pReadPos_ = *pWritePos_;
        
        n = len;
    }
    else if (*pWritePos_ < *pReadPos_) {
        const char* msg = shareMem_ + 2*sizeof(uint32_t) + *pReadPos_;
        int32_t len = KMaxSpace - *pReadPos_;
        assert(len >= 0);
        buf.append(msg, len);
        n = len;
        msg = shareMem_ + 2*sizeof(uint32_t);
        len = *pWritePos_;
        n += len;
        if (len > 0) {
            buf.append(msg, len);
        }
        *pReadPos_ = *pWritePos_;
    }
    else {
        assert(false);
    }
    
    return n;
}

int32_t ShareMemoryIPC::write(const BinaryStream &buf) {
    assert(!full());
    const uint32_t available = availableSpace();
    const uint32_t writable = static_cast<uint32_t>(
        (std::min<size_t>)(available, buf.size()));
    const uint32_t writePos = static_cast<uint32_t>(*pWritePos_);
    const uint32_t first = (std::min)(writable, KMaxSpace - writePos);
    const uint32_t second = writable - first;

    if (first > 0) {
        char* dst = shareMem_ + 2*sizeof(uint32_t) + writePos;
        memcpy(dst, buf.c_str(), first);
    }
    if (second > 0) {
        char* dst = shareMem_ + 2*sizeof(uint32_t);
        memcpy(dst, buf.c_str() + first, second);
    }
    *pWritePos_ = static_cast<int32_t>((writePos + writable) % KMaxSpace);
    
    return static_cast<int32_t>(writable);
}

void ShareMemoryIPC::signalAll() {
    writeEvt_.signal();
    readEvt_.signal();
}

bool ShareMemoryIPC::lockRead(const std::function<bool()>& runnable) {
    constexpr uint32_t KWaitSliceMs = 50;

    while (runnable()) {
        if (!ipcMtx_.lockFor(KWaitSliceMs)) {
            continue;
        }
        if (!empty()) { //at least there is something to read, so return.
            return true;
        }
        
        ipcMtx_.unlock();   //empty, so release lock
        if (!runnable()) {
            break;
        }
        if (readEvt_.waitFor(KWaitSliceMs)) {
		    ;
        }
    }
    return false;
}

void ShareMemoryIPC::unlockRead() {
    assert(!full());
    assert(empty());
    writeEvt_.signal();
    ipcMtx_.unlock();
}

bool ShareMemoryIPC::lockWrite(const std::function<bool()>& runnable) {
    constexpr uint32_t KWaitSliceMs = 50;

    while (runnable()) {
        if (!ipcMtx_.lockFor(KWaitSliceMs)) {
            continue;
        }
        if (!full()) {  //at least there is something space for write, so return.
            return true;
        }
        
        ipcMtx_.unlock();   //full, so release lock
        if (!runnable()) {
            break;
        }
        if (writeEvt_.waitFor(KWaitSliceMs)) {
		    ;
        }
    }
    return false;
}

void ShareMemoryIPC::unlockWrite() {
    if (!empty()) {
        readEvt_.signal();
    }
    ipcMtx_.unlock();
}

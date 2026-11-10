// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <stdint.h>
#include <functional>

#if CICI_OS_MACOS
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

#include <string>

#include "sync.h"

#define ssize_t intptr_t

class ShareMemoryIPC {
public:
    ShareMemoryIPC(const std::u8string& shmem, const std::u8string& mutex, const std::u8string& readEvt, const std::u8string& writeEvt, bool bServer);
    ~ShareMemoryIPC();
    
public:
    bool empty() const;
    bool full() const;
    uint32_t availableSpace() const;
    
    int32_t read(BinaryStream& buf);
    int32_t write(const BinaryStream& buf);
    
    bool lockRead(const std::function<bool()>& runnable = []{return true;});
    void unlockRead();
    bool lockWrite(const std::function<bool()>& runnable = []{return true;});
    void unlockWrite();
    
    void signalAll();   //force stop read wait and write wait
    
protected:
    std::u8string name_;
#if CICI_OS_MACOS
    int shmemFd_ = -1;
#elif CICI_OS_WINDOWS
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
#error "Unsupported platform"
#endif
    char* shareMem_ = nullptr;
    int32_t* pReadPos_ = nullptr;
    int32_t* pWritePos_ = nullptr;
    bool bServer_ = false;
    Mutex ipcMtx_;                          //used for inter process
    AutoEvent readEvt_;    //used for inter process
    AutoEvent writeEvt_;                    //used for inter process
};

struct WriteLocker {
    ShareMemoryIPC* shm_ = nullptr;
    bool locked_ = false;
    
    WriteLocker(ShareMemoryIPC* shm, const std::function<bool()>& runnable = []{return true;}):shm_(shm) {
        locked_ = shm_->lockWrite(runnable);
    }
    ~WriteLocker() {
        if (locked_) {
            shm_->unlockWrite();
        }
    }

    explicit operator bool() const {
        return locked_;
    }
};

struct ReadLocker {
    ShareMemoryIPC* shm_ = nullptr;
    bool locked_ = false;
    
    ReadLocker(ShareMemoryIPC* shm, const std::function<bool()>& runnable = []{return true;}):shm_(shm) {
        locked_ = shm_->lockRead(runnable);
    }
    
    ~ReadLocker() {
        if (locked_) {
            shm_->unlockRead();
        }
    }

    explicit operator bool() const {
        return locked_;
    }
};

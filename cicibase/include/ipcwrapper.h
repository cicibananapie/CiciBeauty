// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <functional>

#if CICI_OS_MACOS
#include <dispatch/dispatch.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

#include "shmem.h"
#include "gcd.h"

/**
 callback will be invoked on main thread.
 */
struct ISimpleIPCCallback {
    virtual ~ISimpleIPCCallback() = default;
    virtual void onConnected() = 0;
    virtual void onClosed() = 0;
    virtual void onIPCMsg(const BinaryStream& rawMsg) = 0;
};

struct IPCMessage;
struct RemoteConnectMsg;
struct RemoteConnectAckMsg;
struct RemoteClosedMsg;
// Construct, destroy and call public methods on the UI thread, except postMsg().
// postMsg() may run on any thread; callers must keep the IPC object alive.
// Wrong-thread calls log and assert. Without assertions, methods return and
// construction/destruction terminate.
class SimpleIPC {
    enum ThreadStatus {
        Stoped,
        Running,
        Stoping
    };
protected:
    enum IPCStatus {
        WaitForConnecting,
        Connected,
        Closed
    };
public:
    SimpleIPC(bool bServer);
    ~SimpleIPC();
    
public:
    void start();
    void stop();
    
    bool connected() const;
    bool postMsg(const IPCMessage& msg);

    // Configure only while stopped; captures must remain alive until stop() returns.
    // The message handler runs on the read thread; true consumes the message.
    // The close handler may run on any thread. Neither handler may stop/destroy IPC.
    void setReadCallbacks(std::function<bool(const BinaryStream&)> message, std::function<void()> closed);

    void addCallback(ISimpleIPCCallback *cb);
    void removeCallback(ISimpleIPCCallback *cb);
    
protected:
    void initReaderShmem(const std::u8string &shmem, const std::u8string &mutex, const std::u8string &readEvt,
			     const std::u8string &writeEvt);
	void initWriterShmem(const std::u8string &shmem, const std::u8string &mutex, const std::u8string &readEvt,
			 const std::u8string &writeEvt);
    
private:
    void doWrite();
    void doRead();
    void onIPCMsg();
    bool monitorRemote(uint32_t pid);
    void onIPCMsgInMainThread(const BinaryStream &msg);
    void handleRemoteConnect(const RemoteConnectMsg& rawMsg);
    void handleRemoteConnectAck(const RemoteConnectAckMsg& rawMsg);
    void handleRemoteClosed(const RemoteClosedMsg& rawMsg);
    void notifyReadClosed();
    template<typename Notify>
    void notifyCallbacks(Notify&& notify);
    
private:
    struct RemoteMonitorState;
    std::shared_ptr<RemoteMonitorState> remoteMonitorState_; // UI thread only
    void stopRemoteMonitor();

#if CICI_OS_WINDOWS
    static VOID CALLBACK onProcessExit(PVOID lpParameter, BOOLEAN TimerOrWaitIndicator);
#endif
    
protected:
  std::atomic<IPCStatus> ipcStatus_{WaitForConnecting};

private:
    std::vector<ISimpleIPCCallback *> cbs_;
    uint32_t callbackDispatchDepth_ = 0;
    bool callbacksDirty_ = false;
    ShareMemoryIPC* reader_ = nullptr;      //to me this is reader, but to remote this writer
    ShareMemoryIPC* writer_ = nullptr;      //to me this is writer, but to remote this reader
    
    std::mutex threadMtx_;                  //used for threads in the same process
    std::condition_variable threadEvt_;     //used for threads in the same process
    bool acceptingWrites_ = true;           //guarded by threadMtx_
    std::thread* readThread_ = nullptr;
    std::thread* writeThread_ = nullptr;
    std::atomic<ThreadStatus> status_ { Stoped };

    std::function<bool(const BinaryStream&)> readMessage_;
    std::function<void()> readClosed_;
    std::atomic<bool> readClosedNotified_{false};

    BinaryStream readBuffer_;                //only used in read thread
    BinaryStream writeBuffer_; //used in multi-thread
    bool bServer_ = false;
    uint32_t remotePid_ = 0;
};

class IPCClient: public SimpleIPC {
public:
	IPCClient(const char8_t*unique);
};

class IPCServer: public SimpleIPC {
public:
	IPCServer(const char8_t* unique);
};

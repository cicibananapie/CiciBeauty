// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <string_view>
#include <utility>
#if CICI_OS_MACOS
#include <sys/select.h>
#include <dispatch/dispatch.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif  //CICI_OS_MACOS

#include "log.h"
#include "misc.h"
#include "cicibase.h"
#include "ipcwrapper.h"
#include "binarymsg.h"

struct SimpleIPC::RemoteMonitorState : std::enable_shared_from_this<RemoteMonitorState> {
    explicit RemoteMonitorState(SimpleIPC* ipc) : owner(ipc) {}

    std::mutex mutex;
    SimpleIPC* owner = nullptr;
#if CICI_OS_MACOS
    dispatch_source_t remoteMonitor_ = nullptr;
#elif CICI_OS_WINDOWS
    HANDLE hProcess_ = nullptr;
    HANDLE hWaitHandle_ = nullptr;
#endif
};

namespace {

bool checkIPCMainThread(const char* method) {
    if (isMainThread()) {
        return true;
    }
    LogError << "SimpleIPC::" << method << " must be called on the UI thread" << logEnd;
    assert(false && "SimpleIPC must be called on the UI thread");
    return false;
}

std::u8string ipcObjectSuffix(const char8_t* unique) {
    assert(unique != nullptr);
    const std::u8string_view value(unique);
#if CICI_OS_MACOS
    // Darwin's semaphore-name limit is 31 bytes including the terminating NUL.
    // The longest IPC prefix is 15 bytes, leaving 15 bytes for the suffix.
    constexpr size_t kMaxSuffixLength = 15;
    const bool validPosixName = value.size() <= kMaxSuffixLength
        && std::find(value.begin(), value.end(), u8'/') == value.end();
    if (validPosixName) {
        return std::u8string(value);
    }

    constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    constexpr char8_t kHex[] = u8"0123456789abcdef";
    uint64_t hash = kFnvOffset;
    for (const char8_t character : value) {
        hash ^= static_cast<uint8_t>(character);
        hash *= kFnvPrime;
    }

    std::u8string result(kMaxSuffixLength, u8'0');
    for (size_t index = 0; index < kMaxSuffixLength; ++index) {
        result[kMaxSuffixLength - index - 1] = kHex[hash & 0x0f];
        hash >>= 4;
    }
    return result;
#else
    return std::u8string(value);
#endif
}

} // namespace

template<typename Notify>
void SimpleIPC::notifyCallbacks(Notify&& notify) {
    ++callbackDispatchDepth_;
    const size_t dispatchCount = cbs_.size();
    for (size_t index = 0; index < dispatchCount; ++index) {
        if (auto* callback = cbs_[index]) {
            notify(callback);
        }
    }

    assert(callbackDispatchDepth_ > 0);
    --callbackDispatchDepth_;
    if (callbackDispatchDepth_ == 0 && callbacksDirty_) {
        cbs_.erase(std::remove(cbs_.begin(), cbs_.end(), nullptr), cbs_.end());
        callbacksDirty_ = false;
    }
}

#if CICI_OS_MACOS
const char8_t *KShmemName1 = u8"/shmem_02B5F119";
const char8_t *KShmemName2 = u8"/shmem_50145656";
const char8_t *KMutexName1 = u8"/mutex_B5A4C38C"; //limited to 31!!
const char8_t *KMutexName2 = u8"/mutex_5A6323F7";
const char8_t *KEvtReadName1 = u8"/tmp/event_E4F58755_8CC0_4ABE_AA23_BCDCA92C779D";
const char8_t *KEvtReadName2 = u8"/tmp/event_0F164C21_E4F6_41C9_A1F7_AF90E3E1382A";
const char8_t *KEvtWriteName1 = u8"/tmp/event_294DF951_016F_4419_861B_B6EC6A20A856";
const char8_t *KEvtWriteName2 = u8"/tmp/event_8C5DD119_C882_4FD3_8EC8_D0DC27EC1F15";
#elif CICI_OS_WINDOWS
const char8_t *KShmemName1 = u8"Local\\tmp_shmem_02B5F119_3C94_4A73_A800_47B4FA5C5C3C";
const char8_t *KShmemName2 = u8"Local\\tmp_shmem_50145656_4BA5_4C1C_BB65_E27B7F509D3F";
const char8_t *KMutexName1 = u8"Local\\mutex_B5A4C38C_EE53_4872_B6CE_484AD374AC2C";
const char8_t *KMutexName2 = u8"Local\\mutex_5A6323F7_F924_40C0_9B76_07216C958500";
const char8_t *KEvtReadName1 = u8"Local\\event_E4F58755_8CC0_4ABE_AA23_BCDCA92C779D";
const char8_t *KEvtReadName2 = u8"Local\\event_0F164C21_E4F6_41C9_A1F7_AF90E3E1382A";
const char8_t *KEvtWriteName1 = u8"Local\\event_294DF951_016F_4419_861B_B6EC6A20A856";
const char8_t *KEvtWriteName2 = u8"Local\\event_8C5DD119_C882_4FD3_8EC8_D0DC27EC1F15";
#else
#error "Unsupported platform"
#endif

SimpleIPC::SimpleIPC(bool bServer)
	:bServer_(bServer) {
    if (!checkIPCMainThread(__func__)) {
        LogImpl::instance().flush();
        std::terminate();
    }
}

SimpleIPC::~SimpleIPC() {
    if (!checkIPCMainThread(__func__)) {
        LogImpl::instance().flush();
        std::terminate();
    }
    stop();
    delete reader_;
    delete writer_;
}

void SimpleIPC::initReaderShmem(const std::u8string &shmem, const std::u8string &mutex, const std::u8string &readEvt,
				const std::u8string &writeEvt) {
    reader_ = new ShareMemoryIPC(shmem, mutex, readEvt, writeEvt, bServer_);
}

void SimpleIPC::initWriterShmem(const std::u8string &shmem, const std::u8string &mutex, const std::u8string &readEvt,
				const std::u8string &writeEvt) {
    writer_ = new ShareMemoryIPC(shmem, mutex, readEvt, writeEvt, bServer_);
}

void SimpleIPC::start() {
    if (!checkIPCMainThread(__func__)) {
        return;
    }
    assert(readThread_ == nullptr);
    assert(writeThread_ == nullptr);
    assert(status_.load() == Stoped);
    readClosedNotified_ = false;

    {
        std::unique_lock<std::mutex> guard(threadMtx_);
        acceptingWrites_ = true;
        status_.store(Running);
    }
    readThread_ = new std::thread([](SimpleIPC* p){
        p->doRead();
    }, this);
    
    writeThread_ = new std::thread([](SimpleIPC* p){
        p->doWrite();
    }, this);
}

void SimpleIPC::stop() {
    if (!checkIPCMainThread(__func__)) {
        return;
    }
    if (status_.load() == Stoped) {
        std::unique_lock<std::mutex> guard(threadMtx_);
        acceptingWrites_ = false;
        assert(readThread_ == nullptr);
        assert(writeThread_ == nullptr);
        return;
    }

    notifyReadClosed();

    {   //send RemoteClosedMsg to peer
        std::unique_lock<std::mutex> guard(threadMtx_);
        if (acceptingWrites_ && ipcStatus_ == Connected) {
            RemoteClosedMsg closed;
            BinaryPack bin;
            closed.toBinary(bin);
            writeBuffer_.append(bin);
        }
        acceptingWrites_ = false;
        status_.store(Stoping);
    }

    threadEvt_.notify_all();
    if (reader_) {
        reader_->signalAll();   //wake the thread
    }
    if (writer_) {
        writer_->signalAll();
    }

    if (readThread_) {
        readThread_->join();
        delete readThread_;
        readThread_ = nullptr;
    }
    if (writeThread_) {
        writeThread_->join();
        delete writeThread_;
        writeThread_ = nullptr;
    }
    
    stopRemoteMonitor();
    
    status_.store(Stoped);
}

bool SimpleIPC::connected() const {
    if (!checkIPCMainThread(__func__)) {
        return false;
    }
    return ipcStatus_ == Connected;
}

void SimpleIPC::setReadCallbacks(std::function<bool(const BinaryStream&)> message, std::function<void()> closed) {
    if (!checkIPCMainThread(__func__)) {
        return;
    }
    assert(status_.load() == Stoped);
    readMessage_ = std::move(message);
    readClosed_ = std::move(closed);
}

void SimpleIPC::notifyReadClosed() {
    if (!readClosedNotified_.exchange(true) && readClosed_) {
        readClosed_();
    }
}

bool SimpleIPC::postMsg(const IPCMessage& msg) {
    {
        std::unique_lock<std::mutex> guard(threadMtx_);
        if (!acceptingWrites_ || ipcStatus_ != Connected) {
            return false;
        }
        
        BinaryPack bin;
        msg.toBinary(bin);
        writeBuffer_.append(bin);
    }
    threadEvt_.notify_all();
    
    return true;
}

void SimpleIPC::addCallback(ISimpleIPCCallback* cb) {
    if (!checkIPCMainThread(__func__)) {
        return;
    }
    assert(cb != nullptr);
    assert(std::find(cbs_.begin(), cbs_.end(), cb) == cbs_.end());
    cbs_.push_back(cb);
}

void SimpleIPC::removeCallback(ISimpleIPCCallback* cb) {
    if (!checkIPCMainThread(__func__)) {
        return;
    }
    assert(cb != nullptr);
    auto it = std::find(cbs_.begin(), cbs_.end(), cb);
    if (it == cbs_.end()) {
        return;
    }
    if (callbackDispatchDepth_ > 0) {
        *it = nullptr;
        callbacksDirty_ = true;
    }
    else {
        cbs_.erase(it);
    }
}

void SimpleIPC::onIPCMsgInMainThread(const BinaryStream& rawMsg) {
    auto id = BinaryUnpack::whitchMsg(rawMsg);
    if (id == KRemoteConnected) {
        RemoteConnectMsg msg;
        msg.fromBinary(rawMsg);
        handleRemoteConnect(msg);
    }
    else if (id == KRemoteConnectedAck) {
        RemoteConnectAckMsg msg;
        msg.fromBinary(rawMsg);
        handleRemoteConnectAck(msg);
    } else if (id == KRemoteClosed) {   //this message did not implemented yet!
        RemoteClosedMsg msg;
        msg.fromBinary(rawMsg);
        handleRemoteClosed(msg);
    } else {
        notifyCallbacks([&rawMsg](ISimpleIPCCallback* callback) {
            callback->onIPCMsg(rawMsg);
        });
    }
}

void SimpleIPC::handleRemoteConnect(const RemoteConnectMsg& msg) {
    assert(ipcStatus_ == WaitForConnecting);
    ipcStatus_ = Connected;
    
    if (!monitorRemote(msg.pid_)) {
        RemoteClosedMsg closed;
        postMsg(closed);
        handleRemoteClosed(closed);
        return;
    }
    RemoteConnectAckMsg ack(ProcessThreadOp::pid());
    postMsg(ack);
    
    notifyCallbacks([](ISimpleIPCCallback* callback) {
        callback->onConnected();
    });
}

void SimpleIPC::handleRemoteConnectAck(const RemoteConnectAckMsg& msg) {
    if (!monitorRemote(msg.pid_)) {
        RemoteClosedMsg closed;
        postMsg(closed);
        handleRemoteClosed(closed);
        return;
    }
    notifyCallbacks([](ISimpleIPCCallback* callback) {
        callback->onConnected();
    });
}

void SimpleIPC::handleRemoteClosed(const RemoteClosedMsg& msg) {
    assert(ipcStatus_ == Connected || ipcStatus_ == Closed);
    if (ipcStatus_ != Closed) {
        ipcStatus_ = Closed;
        notifyCallbacks([](ISimpleIPCCallback* callback) {
            callback->onClosed();
        });
    }
}

#if CICI_OS_MACOS
bool SimpleIPC::monitorRemote(uint32_t pid) {
    stopRemoteMonitor();
    remotePid_ = pid;
    auto state = std::make_shared<RemoteMonitorState>(this);
    state->remoteMonitor_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, // Type: Monitor a Process
                                                   pid,                       // Handle: The PID of the child process
                                                   DISPATCH_PROC_EXIT,        // Event: Trigger when the process EXITs
                                                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));

    if (state->remoteMonitor_ == nullptr) {
        LogError << "Failed to monitor remote process, pid=" << pid << logEnd;
        return false;
    }

    dispatch_source_set_event_handler(state->remoteMonitor_, ^{
      {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (state->owner != nullptr) {
              state->owner->notifyReadClosed();
          }
      }
      asyncRunUI([state, pid]() {
          // Only the UI thread changes owner, so no lock is needed here.
          auto* owner = state->owner;
          if (owner == nullptr) {
              return;
          }
          int status = 0;
          waitpid(pid, &status, WNOHANG);
          owner->stopRemoteMonitor();
          RemoteClosedMsg msg;
          owner->handleRemoteClosed(msg);
      });
    });

    remoteMonitorState_ = state;
    dispatch_resume(state->remoteMonitor_);
    return true;
}
#elif CICI_OS_WINDOWS
// 1. The Callback Function
// This runs on a thread pool thread when the child process exits
VOID CALLBACK SimpleIPC::onProcessExit(PVOID lpParameter, BOOLEAN TimerOrWaitIndicator) {
    auto* rawState = static_cast<RemoteMonitorState*>(lpParameter);
    auto state = rawState->shared_from_this();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto* owner = state->owner;
        if (owner == nullptr) {
            return;
        }

        DWORD exitCode = 0;
        if (state->hProcess_ != nullptr && GetExitCodeProcess(state->hProcess_, &exitCode)) {
            LogInfo << "Child process exited with code:" << exitCode << logEnd;
        }
        owner->notifyReadClosed();
    }
    asyncRunUI([state = std::move(state)]() {
        auto* owner = state->owner;
        if (owner == nullptr) {
            return;
        }
        owner->stopRemoteMonitor();
        RemoteClosedMsg msg;
        owner->handleRemoteClosed(msg);
    });
}

bool SimpleIPC::monitorRemote(uint32_t pid) {
    stopRemoteMonitor();

    remotePid_ = pid;
    auto state = std::make_shared<RemoteMonitorState>(this);
    state->hProcess_ = OpenProcess(SYNCHRONIZE, FALSE, remotePid_);
    if (state->hProcess_ == nullptr) {
        const auto error = GetLastError();
        LogError << "Failed to open remote process, pid=" << remotePid_
                 << " error=" << error << logEnd;
        return false;
    }

    // Register our process handle with the thread pool
    // WT_EXECUTEONLYONCE: Only trigger the callback once
    BOOL success = RegisterWaitForSingleObject(&state->hWaitHandle_, // Output wait handle
                                               state->hProcess_,     // Process to monitor
                                               onProcessExit,        // Callback function
                                               state.get(),          // Parameter passed to callback
                                               INFINITE,             // Timeout (wait forever)
                                               WT_EXECUTEONLYONCE    // Flags
    );

    if (!success) {
        const auto error = GetLastError();
        LogError << "Failed to register wait. Error:" << error << logEnd;
        if (!CloseHandle(state->hProcess_)) {
            LogError << "Failed to close monitored process handle. Error:" << GetLastError() << logEnd;
        }
        return false;
    }

    remoteMonitorState_ = state;
    return true;
}
#else
#error "Unsupported platform"
#endif

void SimpleIPC::stopRemoteMonitor() {
    auto state = std::move(remoteMonitorState_);
    if (!state) {
        return;
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    state->owner = nullptr;
#if CICI_OS_MACOS
    auto monitor = std::exchange(state->remoteMonitor_, nullptr);
    lock.unlock();
    if (monitor != nullptr) {
        dispatch_source_cancel(monitor);
    }
#elif CICI_OS_WINDOWS
    auto waitHandle = std::exchange(state->hWaitHandle_, nullptr);
    auto process = std::exchange(state->hProcess_, nullptr);
    lock.unlock();
    // UnregisterWaitEx may wait for a callback that also takes state->mutex.
    if (waitHandle != nullptr && !UnregisterWaitEx(waitHandle, INVALID_HANDLE_VALUE)) {
        LogError << "Failed to unregister wait. Error:" << GetLastError() << logEnd;
    }
    if (process != nullptr && !CloseHandle(process)) {
        LogError << "Failed to close monitored process handle. Error:" << GetLastError() << logEnd;
    }
#else
#error "Unsupported platform"
#endif
}

void SimpleIPC::onIPCMsg() {
    do {
        BinaryStream rawMsg;
        auto b = BinaryUnpack::peekRawMsg(readBuffer_, rawMsg);
        if (!b) {
            break;
        }

        const auto id = BinaryUnpack::whitchMsg(rawMsg);
        if (id == KRemoteClosed) {
            notifyReadClosed();
        } else if (id != KRemoteConnected && id != KRemoteConnectedAck && readMessage_ && readMessage_(rawMsg)) {
            continue;
        }

        asyncRunUI([this, rawMsg](){
            onIPCMsgInMainThread(rawMsg);
        });
    }
    while (true);
}

void SimpleIPC::doWrite() {
    using Clock = std::chrono::steady_clock;
    constexpr auto KDrainTimeout = std::chrono::milliseconds(200);

    BinaryStream data;
    bool draining = false;
    Clock::time_point drainDeadline;

    auto writeRunnable = [this, &draining, &drainDeadline, KDrainTimeout]() {
        if (status_.load() == Running) {
            return true;
        }
        //if stopping, try 200ms to drain msg to peer, RemoteCloseMsg is important!
        const auto now = Clock::now();
        if (!draining) {
            draining = true;
            drainDeadline = now + KDrainTimeout;
        }
        return now < drainDeadline;
    };

    while (true) {
        if (data.empty()) {
            std::unique_lock<std::mutex> guard(threadMtx_);
            threadEvt_.wait(guard, [this]() {return !writeBuffer_.empty() || status_.load() != Running;});
            data.swap(writeBuffer_);

            if (data.empty() && status_.load() != Running) {
                return;
            }
        }

        while (!data.empty()) {
            WriteLocker guard(writer_, writeRunnable);
            if (!guard) {
                LogInfo << "IPC write drain timed out, dropped " << data.size() << " bytes" << logEnd;
                return;
            }

            //write to shmem, write all of them.
            auto len = writer_->write(data);
            auto tmp = data.substr(len);
            data.swap(tmp);
        }
    }
}

void SimpleIPC::doRead() {
    while (status_.load() == Running) {
        {
            ReadLocker guard(reader_, [this]() {return status_.load() == Running;});
            if (!guard) {
                break;
            }
            if (status_.load() != Running) {
                BinaryStream discarded;
                reader_->read(discarded);
                break;
            }
            reader_->read(readBuffer_);
        }
        onIPCMsg();
    }
}

IPCClient::IPCClient(const char8_t*unique) : SimpleIPC(false) {
    const std::u8string suffix = ipcObjectSuffix(unique);
    std::u8string shmem1(KShmemName1);
	std::u8string mtx1(KMutexName1);
    std::u8string evtRead1(KEvtReadName1);
	std::u8string evtWrite1(KEvtWriteName1);
    shmem1 += suffix;
    mtx1 += suffix;
    evtRead1 += suffix;
    evtWrite1 += suffix;
    this->initReaderShmem(shmem1, mtx1, evtRead1, evtWrite1);
    
    std::u8string shmem2(KShmemName2);
    std::u8string mtx2(KMutexName2);
    std::u8string evtRead2(KEvtReadName2);
    std::u8string evtWrite2(KEvtWriteName2);
    shmem2 += suffix;
    mtx2 += suffix;
    evtRead2 += suffix;
    evtWrite2 += suffix;
    this->initWriterShmem(shmem2, mtx2, evtRead2, evtWrite2);
    
    ipcStatus_ = Connected;
    RemoteConnectMsg msg(ProcessThreadOp::pid());
    postMsg(msg);  //I'm client, I need tell server that I connect to server
}

IPCServer::IPCServer(const char8_t *unique) : SimpleIPC(true) {
    const std::u8string suffix = ipcObjectSuffix(unique);
	std::u8string shmem1(KShmemName1);
	std::u8string mtx1(KMutexName1);
	std::u8string evtRead1(KEvtReadName1);
	std::u8string evtWrite1(KEvtWriteName1);
    shmem1 += suffix;
    mtx1 += suffix;
    evtRead1 += suffix;
    evtWrite1 += suffix;
    this->initWriterShmem(shmem1, mtx1, evtRead1, evtWrite1);
    
    std::u8string shmem2(KShmemName2);
    std::u8string mtx2(KMutexName2);
    std::u8string evtRead2(KEvtReadName2);
    std::u8string evtWrite2(KEvtWriteName2);
    shmem2 += suffix;
    mtx2 += suffix;
    evtRead2 += suffix;
    evtWrite2 += suffix;
    this->initReaderShmem(shmem2, mtx2, evtRead2, evtWrite2);
}

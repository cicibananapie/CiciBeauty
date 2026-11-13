// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "lumaclient.h"

#include <cassert>
#include <condition_variable>
#include <cstring>

#include "binarymsg.h"
#include "cicibase.h"
#include "log.h"
#include "misc.h"
#include "uuid.h"

struct LumaClient::PendingCall {
    std::condition_variable ready;
    bool completed = false;
    cici::protocol::CallResult result;
};

namespace {

// Replay a serialized message without knowing its business-specific type.
struct BufferedMessage : IPCMessage {
    explicit BufferedMessage(const BinaryStream& data) : data_(data) {
    }

protected:
    uint32_t id() const override {
        uint32_t result;
        std::memcpy(&result, data_.data() + sizeof(uint32_t), sizeof(result));
        return result;
    }

    uint32_t pack(BinaryPack& bin) const override {
        constexpr size_t headerSize = 2 * sizeof(uint32_t);
        bin.append(data_, headerSize, BinaryStream::npos);
        return static_cast<uint32_t>(data_.size() - headerSize);
    }

    bool unpack(BinaryUnpack&) override {
        return false;
    }

private:
    const BinaryStream& data_;
};

} // namespace

LumaClient& LumaClient::instance() {
    static LumaClient client;
    return client;
}

LumaClient::~LumaClient() {
    stop();
}

bool LumaClient::start() {
    makesureMainThread();
    const auto key = CiciUUID::generate();
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        if (ipc_) {
            LogWarn << "ciciluma IPC has already been started" << logEnd;
            return false;
        }
        connectionTimer_ = std::make_unique<QTimer>();
        connectionTimer_->setSingleShot(true);
        QObject::connect(connectionTimer_.get(), &QTimer::timeout, connectionTimer_.get(), [this]() {
            LogError << "ciciluma IPC handshake timed out" << logEnd;
            stop();
        });
        ipc_ = std::make_unique<IPCServer>(key.c_str());
        ipc_->addCallback(this);
        ipc_->setReadCallbacks(
            [this](const BinaryStream& message) {
                return onReadMessage(message);
            },
            [this]() {
                onTransportClosed();
            });
        ipc_->start();
        active_ = true;
        sessionId_ = key;
    }

#if CICI_OS_MACOS
    const auto executable = FileSystemOp::bundlePath() / "Contents" / "Helpers" / "ciciluma";
    const std::vector<std::u8string> arguments{key};
#elif CICI_OS_WINDOWS
    const auto executable = FileSystemOp::binPath().parent_path() / "cicirunner.exe";
    const std::vector<std::u8string> arguments{(FileSystemOp::binPath() / "ciciluma.dll").u8string(), key};
#else
#error "Unsupported platform"
#endif
    uint32_t pid = 0;
    const int error = ProcessThreadOp::launchDetached(executable, arguments, pid);
    if (error != 0) {
        LogError << "launch ciciluma failed, error=" << error << logEnd;
        stop();
        return false;
    }

    connectionTimer_->start(30000);
    return true;
}

void LumaClient::stop() {
    makesureMainThread();
    if (connectionTimer_) {
        connectionTimer_->stop();
    }
    onTransportClosed();
    if (ipc_) {
        ipc_->removeCallback(this);
        ipc_->stop();
    }
    const auto asyncCalls = asyncCalls_;
    for (const auto& [id, complete] : asyncCalls) {
        if (asyncCalls_.contains(id)) {
            complete({cici::protocol::CallStatus::Closed});
        }
    }
    const auto listeners = listeners_;
    for (const auto& [owner, listener] : listeners) {
        if (listeners_.contains(owner) && listener.closed) {
            listener.closed();
        }
    }
    // Keep IPC alive for main-thread callbacks already queued by SimpleIPC.
}

bool LumaClient::postMsg(const IPCMessage& message) {
    IPCServer* ipc;
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        if (!active_) {
            LogWarn << "ciciluma IPC is unavailable" << logEnd;
            return false;
        }
        if (!connected_) {
            BinaryPack data;
            message.toBinary(data);
            pending_.push_back(std::move(data));
            return true;
        }
        ipc = ipc_.get();
    }
    const bool sent = ipc->postMsg(message);
    if (!sent) {
        LogWarn << "send ciciluma IPC message failed" << logEnd;
    }
    return sent;
}

cici::protocol::CallResult LumaClient::call(std::string_view method, const nlohmann::json& body,
                                            std::chrono::milliseconds timeout) {
    using namespace cici::protocol;
    if (method.empty() || !body.is_object() || timeout.count() <= 0) {
        return {CallStatus::InvalidRequest};
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(stateMtx_);
    if (!active_ || !connected_) {
        return {CallStatus::NotConnected};
    }

    const auto requestId = ++nextRequestId_;
    auto pending = std::make_shared<PendingCall>();
    calls_.emplace(requestId, pending);
    if (!ipc_->postMsg(callRequest(requestId, method, body))) {
        calls_.erase(requestId);
        return {CallStatus::Closed};
    }
    if (!pending->ready.wait_until(lock, deadline, [&pending]() {
            return pending->completed;
        })) {
        calls_.erase(requestId);
        return {CallStatus::Timeout};
    }
    calls_.erase(requestId);
    return std::move(pending->result);
}

void LumaClient::callAsync(QObject* owner, std::string_view method, const nlohmann::json& body,
                           std::function<void(cici::protocol::CallResult)> callback) {
    makesureMainThread();
    using namespace cici::protocol;
    assert(owner && callback);
    uint64_t requestId;
    CallStatus status = CallStatus::Success;
    {
        std::lock_guard<std::mutex> lock(stateMtx_);
        requestId = ++nextRequestId_;
        if (method.empty() || !body.is_object()) {
            status = CallStatus::InvalidRequest;
        } else if (!active_ || !connected_) {
            status = CallStatus::NotConnected;
        }
    }
    const auto destroyed = QObject::connect(owner, &QObject::destroyed, [this, requestId]() {
        asyncCalls_.erase(requestId);
    });
    auto complete = [this, requestId, destroyed, callback = std::move(callback)](CallResult result) {
        QObject::disconnect(destroyed);
        asyncCalls_.erase(requestId);
        callback(std::move(result));
    };
    asyncCalls_.emplace(requestId, complete);
    if (status == CallStatus::Success && !postMsg(callRequest(requestId, method, body))) {
        status = CallStatus::Closed;
    }
    if (status != CallStatus::Success) {
        QMetaObject::invokeMethod(owner, [this, requestId, status]() {
            const auto it = asyncCalls_.find(requestId);
            if (it != asyncCalls_.end()) {
                auto complete = it->second;
                complete({status});
            }
        }, Qt::QueuedConnection);
    }
}

bool LumaClient::onReadMessage(const BinaryStream& rawMsg) {
    using namespace cici::protocol;
    JsonMessage message;
    if (!message.fromBinary(rawMsg) || !message.isResponse()) {
        return false;
    }

    const auto requestId = message.value["requestId"].get<uint64_t>();
    std::lock_guard<std::mutex> lock(stateMtx_);
    const auto it = calls_.find(requestId);
    if (it == calls_.end()) {
        return false; // Asynchronous responses are dispatched on the main thread.
    }
    if (it->second->completed) {
        return true; // Duplicate response to a synchronous call.
    }
    auto& pending = *it->second;
    if (message.value["ok"].get<bool>()) {
        pending.result = {CallStatus::Success, std::move(message.value["body"])};
    } else {
        const auto& error = message.value["error"];
        pending.result = {CallStatus::RemoteError, nlohmann::json::object(), error["code"].get<std::string>(),
                          error["message"].get<std::string>()};
    }
    pending.completed = true;
    pending.ready.notify_one();
    return true;
}

void LumaClient::onTransportClosed() {
    std::lock_guard<std::mutex> lock(stateMtx_);
    active_ = false;
    connected_ = false;
    pending_.clear();
    for (const auto& [id, pending] : calls_) {
        if (!pending->completed) {
            pending->result = {cici::protocol::CallStatus::Closed};
            pending->completed = true;
            pending->ready.notify_one();
        }
    }
    calls_.clear();
}

uint64_t LumaClient::connectionId() {
    std::lock_guard guard(stateMtx_);
    return connected_ ? connectionId_ : 0;
}

void LumaClient::onConnected() {
    connectionTimer_->stop();
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        if (!active_) {
            return;
        }
        LogInfo << "ciciluma IPC connected" << logEnd;
        for (const auto& data : pending_) {
            if (!ipc_->postMsg(BufferedMessage(data))) {
                LogWarn << "send queued ciciluma IPC message failed" << logEnd;
            }
        }
        pending_.clear();
        connected_ = true;
        ++connectionId_;
    }
    const auto listeners = listeners_;
    for (const auto& [owner, listener] : listeners) {
        if (listeners_.contains(owner) && listener.connected) {
            listener.connected();
        }
    }
}

void LumaClient::onClosed() {
    LogWarn << "ciciluma IPC closed" << logEnd;
    stop();
}

void LumaClient::onIPCMsg(const BinaryStream& message) {
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        if (!active_) {
            return; // Ignore notifications queued before closure.
        }
    }
    cici::protocol::JsonMessage decoded;
    if (!decoded.fromBinary(message) || decoded.isRequest()) {
        LogWarn << "invalid ciciluma IPC notification" << logEnd;
        return;
    }
    if (decoded.isResponse()) {
        const auto it = asyncCalls_.find(decoded.value["requestId"].get<uint64_t>());
        if (it != asyncCalls_.end()) {
            using namespace cici::protocol;
            auto complete = it->second;
            if (decoded.value["ok"].get<bool>()) {
                complete({CallStatus::Success, std::move(decoded.value["body"])});
            } else {
                const auto& error = decoded.value["error"];
                complete({CallStatus::RemoteError, nlohmann::json::object(), error["code"].get<std::string>(),
                          error["message"].get<std::string>()});
            }
        }
        return;
    }
    const auto listeners = listeners_;
    for (const auto& [owner, listener] : listeners) {
        if (listeners_.contains(owner) && listener.message) {
            listener.message(decoded);
        }
    }
}

void LumaClient::addListener(void* owner, std::function<void(const cici::protocol::JsonMessage&)> message,
                             std::function<void()> closed, std::function<void()> connected) {
    makesureMainThread();
    listeners_[owner] = {std::move(message), std::move(closed), connected};
    bool isConnected;
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        isConnected = connected_;
    }
    if (isConnected && connected) {
        connected();
    }
}

void LumaClient::removeListener(void* owner) {
    makesureMainThread();
    listeners_.erase(owner);
}

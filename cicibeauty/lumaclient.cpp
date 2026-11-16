// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "lumaclient.h"

#include <cassert>
#include <cstring>
#include <QPointer>

#include "binarymsg.h"
#include "cicibase.h"
#include "log.h"
#include "misc.h"
#include "modelapp.h"
#include "uuid.h"

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

LumaClient::~LumaClient() {
    stop();
}

bool LumaClient::start() {
    makesureMainThread();
    const auto key = CiciUUID::generate();
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        if (state_ != State::NotStarted) {
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
            {},
            [this]() {
                onTransportClosed();
            });
        ipc_->start();
        state_ = State::Starting;
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
    {
        std::lock_guard guard(stateMtx_);
        if (state_ == State::Stopping || state_ == State::Stopped) {
            return;
        }
        state_ = State::Stopping;
        pending_.clear();
    }
    if (connectionTimer_) {
        connectionTimer_->stop();
    }
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
    listeners_.clear();
    {
        std::lock_guard guard(stateMtx_);
        state_ = State::Stopped;
    }
}

bool LumaClient::postMsg(const IPCMessage& message) {
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        if (state_ != State::Starting && state_ != State::Connected) {
            LogWarn << "ciciluma IPC is unavailable" << logEnd;
            return false;
        }
        if (state_ == State::Starting) {
            BinaryPack data;
            message.toBinary(data);
            pending_.push_back(std::move(data));
            return true;
        }
    }
    const bool sent = ipc_->postMsg(message);
    if (!sent) {
        LogWarn << "send ciciluma IPC message failed" << logEnd;
    }
    return sent;
}

void LumaClient::callAsync(QObject* owner, std::string_view method, const nlohmann::json& body,
                           std::function<void(cici::protocol::CallResult)> callback) {
    makesureMainThread();
    using namespace cici::protocol;
    assert(owner && callback);
    State state;
    uint64_t requestId;
    {
        std::lock_guard lock(stateMtx_);
        state = state_;
        requestId = ++nextRequestId_;
    }
    if (state == State::Stopping || state == State::Stopped) {
        QMetaObject::invokeMethod(owner, [callback = std::move(callback)]() {
            callback({CallStatus::NotConnected});
        }, Qt::QueuedConnection);
        return;
    }
    CallStatus status = CallStatus::Success;
    if (method.empty() || !body.is_object()) {
        status = CallStatus::InvalidRequest;
    } else if (state != State::Starting && state != State::Connected) {
        status = CallStatus::NotConnected;
    }
    const auto destroyed = QObject::connect(owner, &QObject::destroyed, this, [this, requestId]() {
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
        QMetaObject::invokeMethod(this, [this, requestId, status]() {
            const auto it = asyncCalls_.find(requestId);
            if (it != asyncCalls_.end()) {
                auto complete = it->second;
                complete({status});
            }
        }, Qt::QueuedConnection);
    }
}

void LumaClient::onTransportClosed() {
    std::lock_guard<std::mutex> lock(stateMtx_);
    if (state_ == State::Starting || state_ == State::Connected) {
        state_ = State::Disconnected;
    }
    pending_.clear();
}

uint64_t LumaClient::connectionId() {
    std::lock_guard guard(stateMtx_);
    return state_ == State::Connected ? connectionId_ : 0;
}

void LumaClient::onConnected() {
    connectionTimer_->stop();
    {
        std::lock_guard<std::mutex> guard(stateMtx_);
        if (state_ != State::Starting && state_ != State::Connected) {
            return;
        }
        LogInfo << "ciciluma IPC connected" << logEnd;
        for (const auto& data : pending_) {
            if (!ipc_->postMsg(BufferedMessage(data))) {
                LogWarn << "send queued ciciluma IPC message failed" << logEnd;
            }
        }
        pending_.clear();
        state_ = State::Connected;
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
        if (state_ != State::Starting && state_ != State::Connected) {
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
    State state;
    {
        std::lock_guard guard(stateMtx_);
        state = state_;
    }
    if (state == State::Stopping || state == State::Stopped) {
        if (closed) {
            closed();
        }
        return;
    }
    listeners_[owner] = {std::move(message), std::move(closed), connected};
    if (state == State::Connected && connected) {
        connected();
    }
}

void LumaClient::removeListener(void* owner) {
    makesureMainThread();
    listeners_.erase(owner);
}

void LumaRequestScope::reset() {
    makesureMainThread();
    ++generation_;
    versions_.clear();
}

void LumaRequestScope::callAsync(GlobalModel* app, const std::u8string& key, std::string_view method, const nlohmann::json& body,
                                Callback apply, Callback complete) {
    makesureMainThread();
    assert(apply);
    const auto version = ++versions_[key];
    auto reply = [this, key, version, generation = generation_, apply = std::move(apply), complete = std::move(complete)]
        (cici::protocol::CallResult result) {
            const QPointer<LumaRequestScope> alive(this);
            if (generation == generation_ && versions_.at(key) == version) {
                apply(result);
            }
            if (!alive) {
                return;
            }
            if (complete) {
                if (generation != generation_) {
                    result = {cici::protocol::CallStatus::Closed};
                }
                complete(std::move(result));
            }
        };
    if (auto client = app->luma()) {
        client->callAsync(this, method, body, std::move(reply));
    } else {
        QMetaObject::invokeMethod(this, [reply = std::move(reply)]() mutable {
            reply({cici::protocol::CallStatus::NotConnected});
        }, Qt::QueuedConnection);
    }
}

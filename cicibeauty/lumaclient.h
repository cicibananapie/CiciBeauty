// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <functional>
#include <map>
#include <utility>

#include <QTimer>

#include "ciciglobal_pch.h"
#include "ipcwrapper.h"
#include "protocol/call.h"

class GlobalModel;

// Lifecycle and request callbacks run on the main thread; postMsg accepts any thread.
class LumaClient : public QObject, public ISimpleIPCCallback {
public:
    LumaClient() = default;
    ~LumaClient() override;

    bool start();
    void stop();
    // Runs once after request completions and listener notifications.
    std::function<void()> closed;
    const std::u8string& sessionId() const {
        return sessionId_;
    }

    // True means accepted locally; it does not acknowledge remote processing.
    bool postMsg(const IPCMessage& message);
    // Thread-safe connection generation; zero while disconnected.
    uint64_t connectionId();

    // Main-thread request and callback. Destroying owner cancels local delivery.
    // Requests wait for connection while the service is starting.
    void callAsync(QObject* owner, std::string_view method, const nlohmann::json& body,
                   std::function<void(cici::protocol::CallResult)> callback);

    // Main-thread listeners. connected also runs immediately if already connected.
    void addListener(void* owner, std::function<void(const cici::protocol::JsonMessage&)> message,
                     std::function<void()> closed, std::function<void()> connected = {});
    void removeListener(void* owner);

    LumaClient(const LumaClient&) = delete;
    LumaClient& operator=(const LumaClient&) = delete;

private:
    enum class State {
        NotStarted,
        Starting,
        Connected,
        Disconnected, // Transport closed; main-thread cleanup has not started.
        Stopping,
        Stopped,
    };

    void onConnected() override;
    void onClosed() override;
    void onIPCMsg(const BinaryStream& message) override;
    void onTransportClosed();

    struct Listener {
        std::function<void(const cici::protocol::JsonMessage&)> message;
        std::function<void()> closed;
        std::function<void()> connected;
    };
    std::map<void*, Listener> listeners_;
    std::map<uint64_t, std::function<void(cici::protocol::CallResult)>> asyncCalls_;

    std::unique_ptr<IPCServer> ipc_;
    std::unique_ptr<QTimer> connectionTimer_;
    std::mutex stateMtx_;
    std::vector<BinaryStream> pending_;
    uint64_t nextRequestId_ = 0;
    State state_ = State::NotStarted; // Protected by stateMtx_.
    uint64_t connectionId_ = 0;
    std::u8string sessionId_;
};

// Borrows the client while holding its owner's mutex on the calling thread.
// The owner and mutex must outlive this handle.
class LumaAccess {
public:
    LumaAccess(std::recursive_mutex& mutex, const std::unique_ptr<LumaClient>& client)
        : lock_(mutex), client_(client.get()) {
    }

    LumaAccess(const LumaAccess&) = delete;
    LumaAccess& operator=(const LumaAccess&) = delete;

    LumaAccess(LumaAccess&& other) noexcept
        : lock_(std::move(other.lock_)), client_(std::exchange(other.client_, nullptr)) {
    }

    LumaAccess& operator=(LumaAccess&& other) noexcept {
        if (this != &other) {
            lock_ = std::move(other.lock_);
            client_ = std::exchange(other.client_, nullptr);
        }
        return *this;
    }

    LumaClient* operator->() const {
        return client_;
    }

    explicit operator bool() const {
        return client_ != nullptr;
    }

private:
    // Member order ensures the mutex is locked before the owner is read.
    std::unique_lock<std::recursive_mutex> lock_;
    LumaClient* client_;
};

// Main-thread requests that update independent pieces of state.
// Older replies still complete, but only the latest reply for a key may apply.
// reset() invalidates every key; old completions receive Closed.
// Versions stay local: requestId associates each reply with its captured version.
class LumaRequestScope : public QObject {
public:
    using Callback = std::function<void(cici::protocol::CallResult)>;

    void reset();
    void callAsync(GlobalModel* app, const std::u8string& key, std::string_view method, const nlohmann::json& body,
                   Callback apply, Callback complete = {});

private:
    uint64_t generation_ = 0;
    std::map<std::u8string, uint64_t> versions_;
};

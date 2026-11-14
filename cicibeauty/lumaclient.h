// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <functional>
#include <map>

#include <QTimer>

#include "ciciglobal_pch.h"
#include "ipcwrapper.h"
#include "protocol/call.h"

// Lifecycle and request callbacks run on the main thread; postMsg accepts any thread.
class LumaClient : public ISimpleIPCCallback {
public:
    static LumaClient& instance();

    bool start();
    void stop();
    const std::u8string& sessionId() const {
        return sessionId_;
    }

    // True means accepted locally; it does not acknowledge remote processing.
    bool postMsg(const IPCMessage& message);
    // Thread-safe connection generation; zero while disconnected.
    uint64_t connectionId();

    // Main-thread request and callback. Destroying owner cancels local delivery.
    void callAsync(QObject* owner, std::string_view method, const nlohmann::json& body,
                   std::function<void(cici::protocol::CallResult)> callback);

    // Main-thread listeners. connected also runs immediately if already connected.
    void addListener(void* owner, std::function<void(const cici::protocol::JsonMessage&)> message,
                     std::function<void()> closed, std::function<void()> connected = {});
    void removeListener(void* owner);

    LumaClient(const LumaClient&) = delete;
    LumaClient& operator=(const LumaClient&) = delete;

private:
    LumaClient() = default;
    ~LumaClient() override;

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
    bool active_ = false;
    bool connected_ = false;
    uint64_t connectionId_ = 0;
    std::u8string sessionId_;
};

// Main-thread requests that update independent pieces of state.
// Older replies still complete, but only the latest reply for a key may apply.
// reset() invalidates every key; old completions receive Closed.
// Versions stay local: requestId associates each reply with its captured version.
class LumaRequestScope : public QObject {
public:
    using Callback = std::function<void(cici::protocol::CallResult)>;

    void reset();
    void callAsync(const std::u8string& key, std::string_view method, const nlohmann::json& body,
                   Callback apply, Callback complete = {});

private:
    uint64_t generation_ = 0;
    std::map<std::u8string, uint64_t> versions_;
};

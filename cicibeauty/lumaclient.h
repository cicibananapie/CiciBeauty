// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <map>

#include <QTimer>

#include "ciciglobal_pch.h"
#include "ipcwrapper.h"
#include "protocol/call.h"

// Lifecycle and ordinary callbacks run on the main thread; postMsg/call accept any thread.
class LumaClient : public ISimpleIPCCallback {
    struct PendingCall;

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

    // Requires a completed connection. Blocks without pumping UI events.
    // Timeout stops waiting locally; it does not cancel the remote operation.
    cici::protocol::CallResult call(std::string_view method, const nlohmann::json& body,
                                    std::chrono::milliseconds timeout);

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
    bool onReadMessage(const BinaryStream& message);
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
    std::unordered_map<uint64_t, std::shared_ptr<PendingCall>> calls_;
    uint64_t nextRequestId_ = 0;
    bool active_ = false;
    bool connected_ = false;
    uint64_t connectionId_ = 0;
    std::u8string sessionId_;
};

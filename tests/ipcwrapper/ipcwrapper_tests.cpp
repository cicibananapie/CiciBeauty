// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "ciciglobal_pch.h"

#include "binarymsg.h"
#include "cicibase.h"
#include "ipcwrapper.h"
#include "log.h"
#include "sync.h"
#include "uuid.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if CICI_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr uint32_t KTestMessageId = 0x43494349;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::u8string uniqueSuffix(const std::string&) {
    return u8"_" + CiciUUID::generate().substr(0, 20);
}

void pumpMainThreadTasks() {
#if CICI_OS_MACOS
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, true);
#elif CICI_OS_WINDOWS
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
#endif
}

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        pumpMainThreadTasks();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    pumpMainThreadTasks();
    return predicate();
}

struct TestMessage : IPCMessage {
    uint32_t sequence_ = 0;
    BinaryStream payload_;

    TestMessage() = default;
    TestMessage(uint32_t sequence, BinaryStream payload)
        : sequence_(sequence), payload_(std::move(payload)) {}

protected:
    uint32_t id() const override {
        return KTestMessageId;
    }

    uint32_t pack(BinaryPack& bin) const override {
        uint32_t size = bin.push(sequence_);
        size += bin.push(payload_);
        return size;
    }

    bool unpack(BinaryUnpack& bin) override {
        return bin.pop(sequence_) && bin.pop(payload_);
    }
};

TestMessage makeMessage(uint32_t sequence, size_t payloadSize) {
    return TestMessage(sequence, BinaryStream(payloadSize, static_cast<char>('a' + sequence % 26)));
}

BinaryStream patternedData(size_t size, uint32_t seed) {
    BinaryStream data(size, '\0');
    for (size_t index = 0; index < size; ++index) {
        data[index] = static_cast<char>((index * 31 + seed) % 251);
    }
    return data;
}

class InspectableShareMemoryIPC : public ShareMemoryIPC {
public:
    using ShareMemoryIPC::ShareMemoryIPC;

    int32_t readPos() const {
        return *pReadPos_;
    }

    int32_t writePos() const {
        return *pWritePos_;
    }
};

class SharedMemoryPair {
private:
    std::u8string shmemName_;
    std::u8string mutexName_;
    std::u8string readEventName_;
    std::u8string writeEventName_;

public:
    explicit SharedMemoryPair(const std::string& label)
#if CICI_OS_MACOS
        : shmemName_(u8"/cis" + uniqueSuffix(label))
        , mutexName_(u8"/cim" + uniqueSuffix(label))
        , readEventName_(u8"/tmp/cici_ipcwrapper_test_read" + uniqueSuffix(label))
        , writeEventName_(u8"/tmp/cici_ipcwrapper_test_write" + uniqueSuffix(label))
#elif CICI_OS_WINDOWS
        : shmemName_(u8"Local\\cici_ipcwrapper_test_shmem" + uniqueSuffix(label))
        , mutexName_(u8"Local\\cici_ipcwrapper_test_mutex" + uniqueSuffix(label))
        , readEventName_(u8"Local\\cici_ipcwrapper_test_read" + uniqueSuffix(label))
        , writeEventName_(u8"Local\\cici_ipcwrapper_test_write" + uniqueSuffix(label))
#endif
        , server_(shmemName_, mutexName_, readEventName_, writeEventName_, true)
        , client_(shmemName_, mutexName_, readEventName_, writeEventName_, false) {}

    InspectableShareMemoryIPC server_;
    InspectableShareMemoryIPC client_;
};

struct Recorder : ISimpleIPCCallback {
    int connectedCount_ = 0;
    int closedCount_ = 0;
    std::vector<TestMessage> messages_;
    std::vector<std::string> events_;
    std::vector<std::thread::id> callbackThreads_;
    std::string error_;

    void onConnected() override {
        ++connectedCount_;
        events_.push_back("connected");
        callbackThreads_.push_back(std::this_thread::get_id());
    }

    void onClosed() override {
        ++closedCount_;
        events_.push_back("closed");
        callbackThreads_.push_back(std::this_thread::get_id());
    }

    void onIPCMsg(const BinaryStream& rawMsg) override {
        if (BinaryUnpack::whitchMsg(rawMsg) != KTestMessageId) {
            error_ = "received an unexpected IPC message id";
            return;
        }

        TestMessage message;
        if (!message.fromBinary(rawMsg)) {
            error_ = "failed to decode a test IPC message";
            return;
        }

        messages_.push_back(std::move(message));
        events_.push_back("message");
        callbackThreads_.push_back(std::this_thread::get_id());
    }
};

struct ReplyingCallback : ISimpleIPCCallback {
    explicit ReplyingCallback(SimpleIPC* endpoint) : endpoint_(endpoint) {}

    void onConnected() override {}
    void onClosed() override {}

    void onIPCMsg(const BinaryStream& rawMsg) override {
        TestMessage request;
        if (!request.fromBinary(rawMsg)) {
            error_ = "replying callback failed to decode the request";
            return;
        }

        ++requestCount_;
        TestMessage response(request.sequence_ + 1, request.payload_ + "_reply");
        if (!endpoint_->postMsg(response)) {
            error_ = "replying callback could not post its response";
            return;
        }
        responses_.push_back(std::move(response));
    }

    SimpleIPC* endpoint_ = nullptr;
    uint32_t requestCount_ = 0;
    std::vector<TestMessage> responses_;
    std::string error_;
};

struct SelfRemovingCallback : ISimpleIPCCallback {
    explicit SelfRemovingCallback(SimpleIPC* endpoint) : endpoint_(endpoint) {}

    void onConnected() override {}
    void onClosed() override {}

    void onIPCMsg(const BinaryStream&) override {
        ++messageCount_;
        endpoint_->removeCallback(this);
    }

    SimpleIPC* endpoint_ = nullptr;
    uint32_t messageCount_ = 0;
};

struct ActionCallback : ISimpleIPCCallback {
    explicit ActionCallback(std::function<void()> action = {})
        : action_(std::move(action)) {}

    void onConnected() override {}
    void onClosed() override {}

    void onIPCMsg(const BinaryStream&) override {
        ++messageCount_;
        if (action_) {
            action_();
        }
    }

    std::function<void()> action_;
    uint32_t messageCount_ = 0;
};

class EndpointPair {
private:
    std::u8string unique_;

public:
    explicit EndpointPair(const std::string& label)
        : EndpointPair(uniqueSuffix(label)) {}

    explicit EndpointPair(std::u8string unique)
        : unique_(std::move(unique))
        , server_(unique_.c_str())
        , client_(unique_.c_str()) {
        server_.addCallback(&serverRecorder_);
        client_.addCallback(&clientRecorder_);
    }

    ~EndpointPair() {
        client_.stop();
        waitUntil([this]() {
            return serverRecorder_.closedCount_ > 0;
        }, 500ms);
        server_.stop();
        pumpMainThreadTasks();
        client_.removeCallback(&clientRecorder_);
        server_.removeCallback(&serverRecorder_);
    }

    void startAndConnect() {
        server_.start();
        client_.start();
        waitForConnection();
    }

    void waitForConnection() {
        require(waitUntil([this]() {
            return serverRecorder_.connectedCount_ == 1
                && clientRecorder_.connectedCount_ == 1;
        }, 5s), "client and server did not complete the IPC handshake");
        require(server_.connected(), "server did not enter Connected state");
        require(client_.connected(), "client did not enter Connected state");
    }

    Recorder serverRecorder_;
    Recorder clientRecorder_;
    IPCServer server_;
    IPCClient client_;
};

void requireMessages(const std::vector<TestMessage>& actual, const std::vector<TestMessage>& expected,
                     const std::string& endpoint) {
    require(actual.size() == expected.size(), endpoint + " received the wrong number of messages");
    for (size_t index = 0; index < expected.size(); ++index) {
        require(actual[index].sequence_ == expected[index].sequence_,
            endpoint + " received messages out of order");
        require(actual[index].payload_ == expected[index].payload_,
            endpoint + " received a corrupted payload");
    }
}

void requireCallbacksOnThread(const Recorder& recorder, std::thread::id expectedThread,
                              const std::string& endpoint) {
    require(!recorder.callbackThreads_.empty(), endpoint + " did not receive any callbacks");
    require(std::all_of(recorder.callbackThreads_.begin(), recorder.callbackThreads_.end(),
        [expectedThread](std::thread::id actualThread) { return actualThread == expectedThread; }),
        endpoint + " callback did not run on the main thread");
}

void testEventWaitFor() {
    const auto suffix = uniqueSuffix("event");
#if CICI_OS_MACOS
    const std::u8string name = u8"/tmp/cici_ipcwrapper_test_event" + suffix;
#elif CICI_OS_WINDOWS
    const std::u8string name = u8"Local\\cici_ipcwrapper_test_event" + suffix;
#endif
    AutoEvent waiter(name);
    AutoEvent signaler(name);

    const auto timeoutStart = Clock::now();
    require(!waiter.waitFor(30), "an unsignaled event did not time out");
    require(Clock::now() - timeoutStart >= 15ms, "event timeout returned too early");

    signaler.signal();
    require(waiter.waitFor(200), "a pre-signaled event was not observed");
    require(!waiter.waitFor(0), "an auto-reset event was not consumed");

    std::thread signalThread([&signaler]() {
        std::this_thread::sleep_for(20ms);
        signaler.signal();
    });
    const bool signaled = waiter.waitFor(500);
    signalThread.join();
    require(signaled, "an event signaled during wait was not observed");
}

void testSharedMemoryRingBoundaries() {
    SharedMemoryPair pair("ring");
    auto& server = pair.server_;
    auto& client = pair.client_;

    const size_t capacity = server.availableSpace();
    require(capacity > 0, "shared-memory ring reported zero capacity");

    const auto oversized = patternedData(capacity + 1024, 1);
    require(server.write(oversized) == static_cast<int32_t>(capacity),
        "oversized write did not stop at ring capacity");
    BinaryStream actual;
    require(client.read(actual) == static_cast<int32_t>(capacity),
        "oversized write produced the wrong readable byte count");
    require(actual == oversized.substr(0, capacity),
        "oversized write corrupted the stored prefix");

    const auto exact = patternedData(capacity, 2);
    require(server.write(exact) == static_cast<int32_t>(capacity),
        "exact-capacity write did not fill the ring");
    actual.clear();
    client.read(actual);
    require(actual == exact, "exact-capacity write corrupted data");

    const size_t tailAdvance = capacity - 87;
    const auto advance = patternedData(tailAdvance, 3);
    require(server.write(advance) == static_cast<int32_t>(tailAdvance),
        "failed to advance the ring write position");
    actual.clear();
    client.read(actual);
    require(actual == advance, "advance write corrupted data");

    const auto wrapped = patternedData(256, 4);
    require(server.write(wrapped) == static_cast<int32_t>(wrapped.size()),
        "wrapped write returned the wrong byte count");
    actual.clear();
    client.read(actual);
    require(actual == wrapped, "write spanning the ring boundary corrupted data");

    const auto first = patternedData(capacity - 1000, 5);
    const auto overflow = patternedData(1500, 6);
    require(server.write(first) == static_cast<int32_t>(first.size()),
        "initial partial-capacity write failed");
    require(server.write(overflow) == 1000,
        "write larger than remaining space returned the wrong byte count");
    actual.clear();
    client.read(actual);
    require(actual == first + overflow.substr(0, 1000),
        "write larger than remaining space corrupted ring contents");
}

void testSharedMemoryPositionRelations() {
    {
        SharedMemoryPair pair("positions_equal");
        auto& server = pair.server_;
        auto& client = pair.client_;

        require(server.readPos() == 0 && server.writePos() == 0,
            "new ring did not start with equal zero positions");
        require(server.empty(), "ring with equal positions was not empty");

        const auto payload = patternedData(321, 9);
        require(server.write(payload) == static_cast<int32_t>(payload.size()),
            "write from equal positions returned the wrong byte count");
        require(server.readPos() < server.writePos(),
            "non-wrapping write did not produce readPos < writePos");

        BinaryStream actual;
        client.read(actual);
        require(actual == payload, "read from readPos < writePos corrupted data");
        require(server.readPos() == server.writePos() && server.readPos() == 321,
            "draining the ring did not restore equal nonzero positions");
        require(server.empty(), "drained ring with equal positions was not empty");
    }

    {
        SharedMemoryPair pair("positions_read_less");
        auto& server = pair.server_;
        auto& client = pair.client_;
        const size_t capacity = server.availableSpace();

        const auto advance = patternedData(512, 10);
        server.write(advance);
        BinaryStream actual;
        client.read(actual);
        require(actual == advance && server.readPos() == server.writePos(),
            "failed to establish equal nonzero positions");

        const auto first = patternedData(3000, 11);
        require(server.write(first) == static_cast<int32_t>(first.size()),
            "initial readPos < writePos write failed");
        require(server.readPos() < server.writePos(),
            "test setup did not produce readPos < writePos");

        const size_t remaining = capacity - first.size();
        const auto crossing = patternedData(remaining + 256, 12);
        require(server.write(crossing) == static_cast<int32_t>(remaining),
            "write from readPos < writePos did not stop at available space");
        require(server.readPos() > server.writePos(),
            "wrapped write did not change the relation to readPos > writePos");
        require(server.full(), "wrapped readPos < writePos write did not fill the ring");

        actual.clear();
        client.read(actual);
        require(actual == first + crossing.substr(0, remaining),
            "write starting with readPos < writePos corrupted wrapped data");
        require(server.readPos() == server.writePos(),
            "draining wrapped data did not restore equal positions");
    }

    {
        SharedMemoryPair pair("positions_read_greater");
        auto& server = pair.server_;
        auto& client = pair.client_;
        const size_t capacity = server.availableSpace();
        const size_t ringSize = capacity + 1;

        const auto advance = patternedData(ringSize - 64, 13);
        server.write(advance);
        BinaryStream actual;
        client.read(actual);
        require(actual == advance && server.readPos() == server.writePos(),
            "failed to move equal positions near the end of the ring");

        const auto wrapped = patternedData(128, 14);
        require(server.write(wrapped) == static_cast<int32_t>(wrapped.size()),
            "write from equal positions failed to wrap");
        require(server.readPos() > server.writePos(),
            "wrapped write did not produce readPos > writePos");

        const auto middle = patternedData(256, 15);
        require(server.write(middle) == static_cast<int32_t>(middle.size()),
            "write while readPos > writePos returned the wrong byte count");
        require(server.readPos() > server.writePos(),
            "contiguous write unexpectedly left the readPos > writePos state");

        const size_t remaining = server.availableSpace();
        const auto overflow = patternedData(remaining + 256, 16);
        require(server.write(overflow) == static_cast<int32_t>(remaining),
            "write while readPos > writePos exceeded available space");
        require(server.readPos() > server.writePos() && server.full(),
            "filling the readPos > writePos gap produced the wrong final state");

        actual.clear();
        client.read(actual);
        require(actual == wrapped + middle + overflow.substr(0, remaining),
            "writes starting with readPos > writePos corrupted data");
        require(server.readPos() == server.writePos() && server.empty(),
            "final read did not restore the equal empty state");
    }
}

void testSharedMemoryRandomizedModel() {
    SharedMemoryPair pair("randomized_ring");
    auto& writer = pair.server_;
    auto& reader = pair.client_;
    const size_t capacity = writer.availableSpace();
    const int32_t ringSize = static_cast<int32_t>(capacity + 1);
    BinaryStream expected;
    uint32_t randomState = 0x7A31B92Du;

    const auto nextRandom = [&randomState]() {
        randomState = randomState * 1664525u + 1013904223u;
        return randomState;
    };

    for (uint32_t operation = 0; operation < 5000; ++operation) {
        require(writer.readPos() >= 0 && writer.readPos() < ringSize
            && writer.writePos() >= 0 && writer.writePos() < ringSize,
            "randomized ring position escaped the valid range");
        require(writer.availableSpace() + expected.size() == capacity,
            "randomized ring available-space accounting diverged from the model");
        require(writer.empty() == expected.empty(),
            "randomized ring empty state diverged from the model");
        require(writer.full() == (expected.size() == capacity),
            "randomized ring full state diverged from the model");

        const bool shouldRead = !expected.empty()
            && (writer.full() || nextRandom() % 4 == 0);
        if (shouldRead) {
            BinaryStream actual;
            require(reader.read(actual) == static_cast<int32_t>(expected.size()),
                "randomized ring read returned the wrong byte count");
            require(actual == expected,
                "randomized ring contents diverged from the reference model");
            expected.clear();
            continue;
        }

        const size_t requested = nextRandom() % (2 * capacity + 1);
        const auto data = patternedData(requested, nextRandom());
        const size_t writable = (std::min<size_t>)(writer.availableSpace(), requested);
        require(writer.write(data) == static_cast<int32_t>(writable),
            "randomized ring write returned the wrong byte count");
        expected.append(data.data(), writable);
    }

    if (!expected.empty()) {
        BinaryStream actual;
        reader.read(actual);
        require(actual == expected,
            "randomized ring final drain diverged from the reference model");
    }
    require(writer.empty() && writer.readPos() == writer.writePos(),
        "randomized ring did not finish in the equal empty state");
}

void testMutexLockFor() {
    const auto suffix = uniqueSuffix("mutex_wait");
#if CICI_OS_MACOS
    const std::u8string name = u8"/cim" + suffix;
#elif CICI_OS_WINDOWS
    const std::u8string name = u8"Local\\cici_ipcwrapper_test_mutex" + suffix;
#endif
    Mutex owner(name);
    Mutex contender(name);
    owner.lock();

    auto timedAttempt = std::async(std::launch::async, [&contender]() {
        const auto start = Clock::now();
        const bool acquired = contender.lockFor(40);
        const auto duration = Clock::now() - start;
        if (acquired) {
            contender.unlock();
        }
        return std::make_pair(acquired, duration);
    });
    require(timedAttempt.wait_for(500ms) == std::future_status::ready,
        "mutex timed-lock attempt did not finish");
    const auto [acquiredWhileHeld, waitDuration] = timedAttempt.get();
    require(!acquiredWhileHeld, "mutex was acquired while another thread owned it");
    require(waitDuration >= 20ms, "mutex timeout returned substantially too early");

    owner.unlock();
    require(contender.lockFor(200), "mutex was not acquired after its owner released it");
    contender.unlock();
}

void testSharedMemoryLockWaits() {
    SharedMemoryPair pair("lock_waits");
    auto& server = pair.server_;
    auto& client = pair.client_;

    {
        ReadLocker stopped(&client, []() { return false; });
        require(!stopped, "read locker acquired a lock after cancellation");
    }

    {
        WriteLocker owner(&server);
        require(static_cast<bool>(owner), "failed to acquire the initial write lock");
        auto readContender = std::async(std::launch::async, [&client]() {
            int attempts = 0;
            ReadLocker guard(&client, [&attempts]() { return attempts++ == 0; });
            return static_cast<bool>(guard);
        });
        require(readContender.wait_for(500ms) == std::future_status::ready,
            "cancelled read-lock attempt did not finish after mutex timeout");
        require(!readContender.get(),
            "read locker acquired a mutex held by another thread");

        auto contender = std::async(std::launch::async, [&client]() {
            int attempts = 0;
            WriteLocker guard(&client, [&attempts]() { return attempts++ == 0; });
            return static_cast<bool>(guard);
        });
        require(contender.wait_for(500ms) == std::future_status::ready,
            "cancelled write-lock attempt did not finish");
        require(!contender.get(),
            "failed read locker released a mutex that it did not own");
    }

    const auto payload = patternedData(512, 7);
    auto reader = std::async(std::launch::async, [&client, &payload]() {
        const auto deadline = Clock::now() + 1s;
        ReadLocker guard(&client, [deadline]() { return Clock::now() < deadline; });
        if (!guard) {
            return false;
        }
        BinaryStream actual;
        client.read(actual);
        return actual == payload;
    });
    require(reader.wait_for(20ms) == std::future_status::timeout,
        "read lock did not wait while the ring was empty");
    {
        WriteLocker guard(&server);
        require(static_cast<bool>(guard), "failed to acquire write lock for reader wake-up");
        require(server.write(payload) == static_cast<int32_t>(payload.size()),
            "failed to write reader wake-up payload");
    }
    require(reader.wait_for(1s) == std::future_status::ready && reader.get(),
        "read lock was not woken by newly written data");

    const auto fullPayload = patternedData(server.availableSpace(), 8);
    {
        WriteLocker guard(&server);
        require(static_cast<bool>(guard), "failed to acquire write lock before filling the ring");
        require(server.write(fullPayload) == static_cast<int32_t>(fullPayload.size()),
            "failed to fill the ring");
    }
    auto writer = std::async(std::launch::async, [&server]() {
        const auto deadline = Clock::now() + 1s;
        WriteLocker guard(&server, [deadline]() { return Clock::now() < deadline; });
        return static_cast<bool>(guard);
    });
    require(writer.wait_for(20ms) == std::future_status::timeout,
        "write lock did not wait while the ring was full");
    {
        ReadLocker guard(&client);
        require(static_cast<bool>(guard), "failed to acquire read lock for writer wake-up");
        BinaryStream actual;
        client.read(actual);
        require(actual == fullPayload, "full-ring payload was corrupted");
    }
    require(writer.wait_for(1s) == std::future_status::ready && writer.get(),
        "write lock was not woken after the reader freed space");
}

void testBidirectionalMessaging() {
    EndpointPair endpoints("round_trip");
    endpoints.startAndConnect();

    std::vector<TestMessage> clientToServer;
    std::vector<TestMessage> serverToClient;
    for (uint32_t sequence = 0; sequence < 12; ++sequence) {
        clientToServer.push_back(makeMessage(sequence, sequence == 5 ? 12000 : 137 + sequence));
        require(endpoints.client_.postMsg(clientToServer.back()), "client rejected a message while running");

        serverToClient.push_back(makeMessage(100 + sequence, sequence == 7 ? 9000 : 173 + sequence));
        require(endpoints.server_.postMsg(serverToClient.back()), "server rejected a message while running");
    }

    require(waitUntil([&endpoints, &clientToServer, &serverToClient]() {
        return endpoints.serverRecorder_.messages_.size() == clientToServer.size()
            && endpoints.clientRecorder_.messages_.size() == serverToClient.size();
    }, 5s), "bidirectional IPC messages did not arrive");
    require(endpoints.serverRecorder_.error_.empty(), endpoints.serverRecorder_.error_);
    require(endpoints.clientRecorder_.error_.empty(), endpoints.clientRecorder_.error_);
    requireMessages(endpoints.serverRecorder_.messages_, clientToServer, "server");
    requireMessages(endpoints.clientRecorder_.messages_, serverToClient, "client");
}

void testMessageFramingBoundaries() {
    size_t ringCapacity = 0;
    {
        SharedMemoryPair sizing("framing_capacity");
        ringCapacity = sizing.server_.availableSpace();
    }

    const auto emptyMessage = makeMessage(2000, 0);
    BinaryPack emptyFrame;
    emptyMessage.toBinary(emptyFrame);
    const size_t frameOverhead = emptyFrame.size();
    require(frameOverhead < ringCapacity, "test message header exceeded ring capacity");

    const std::vector<size_t> frameSizes = {
        frameOverhead,
        ringCapacity - 1,
        ringCapacity,
        ringCapacity + 1,
        2 * ringCapacity + 17,
    };

    EndpointPair endpoints("framing_boundaries");
    endpoints.startAndConnect();

    std::vector<TestMessage> expected;
    for (size_t index = 0; index < frameSizes.size(); ++index) {
        expected.push_back(makeMessage(
            2000 + static_cast<uint32_t>(index), frameSizes[index] - frameOverhead));

        BinaryPack frame;
        expected.back().toBinary(frame);
        require(frame.size() == frameSizes[index],
            "test setup produced the wrong serialized frame size");
        require(endpoints.client_.postMsg(expected.back()),
            "client rejected a framing-boundary message");
    }

    require(waitUntil([&endpoints, &expected]() {
        return endpoints.serverRecorder_.messages_.size() == expected.size();
    }, 5s), "framing-boundary messages did not arrive");
    require(endpoints.serverRecorder_.error_.empty(), endpoints.serverRecorder_.error_);
    requireMessages(endpoints.serverRecorder_.messages_, expected, "framing-boundary server");
}

void testConcurrentProducers() {
    constexpr uint32_t KProducerCount = 4;
    constexpr uint32_t KMessagesPerProducer = 48;

    EndpointPair endpoints("concurrent_producers");
    endpoints.startAndConnect();

    std::vector<TestMessage> expected;
    expected.reserve(KProducerCount * KMessagesPerProducer);
    for (uint32_t producer = 0; producer < KProducerCount; ++producer) {
        for (uint32_t index = 0; index < KMessagesPerProducer; ++index) {
            const uint32_t sequence = 3000 + producer * KMessagesPerProducer + index;
            const size_t payloadSize = index % 13 == 0
                ? 6000
                : (index % 7 == 0 ? 0 : 31 + (producer * 17 + index) % 193);
            expected.push_back(makeMessage(sequence, payloadSize));
        }
    }

    std::atomic<bool> allAccepted{true};
    std::vector<std::thread> producers;
    producers.reserve(KProducerCount);
    for (uint32_t producer = 0; producer < KProducerCount; ++producer) {
        producers.emplace_back([&endpoints, &expected, &allAccepted, producer]() {
            const size_t first = producer * KMessagesPerProducer;
            for (size_t index = first; index < first + KMessagesPerProducer; ++index) {
                if (!endpoints.client_.postMsg(expected[index])) {
                    allAccepted.store(false, std::memory_order_release);
                    return;
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    require(allAccepted.load(std::memory_order_acquire),
        "a concurrent producer message was rejected while connected");

    require(waitUntil([&endpoints, &expected]() {
        return endpoints.serverRecorder_.messages_.size() == expected.size();
    }, 10s), "concurrent producer messages did not all arrive");
    require(endpoints.serverRecorder_.error_.empty(), endpoints.serverRecorder_.error_);

    std::vector<int32_t> lastIndex(KProducerCount, -1);
    for (const auto& message : endpoints.serverRecorder_.messages_) {
        require(message.sequence_ >= 3000
            && message.sequence_ < 3000 + KProducerCount * KMessagesPerProducer,
            "concurrent producer delivered an unexpected sequence number");
        const uint32_t offset = message.sequence_ - 3000;
        const uint32_t producer = offset / KMessagesPerProducer;
        const int32_t index = static_cast<int32_t>(offset % KMessagesPerProducer);
        require(index == lastIndex[producer] + 1,
            "messages from one producer arrived out of order");
        lastIndex[producer] = index;
    }

    auto actualSorted = endpoints.serverRecorder_.messages_;
    auto expectedSorted = expected;
    const auto bySequence = [](const TestMessage& left, const TestMessage& right) {
        return left.sequence_ < right.sequence_;
    };
    std::sort(actualSorted.begin(), actualSorted.end(), bySequence);
    std::sort(expectedSorted.begin(), expectedSorted.end(), bySequence);
    requireMessages(actualSorted, expectedSorted, "concurrent producer server");
}

void testDelayedPeerStartup() {
    {
        EndpointPair endpoints("server_starts_first");
        endpoints.server_.start();
        std::this_thread::sleep_for(75ms);
        pumpMainThreadTasks();
        require(endpoints.serverRecorder_.connectedCount_ == 0,
            "server connected before the client started");

        endpoints.client_.start();
        endpoints.waitForConnection();
        const auto message = makeMessage(4000, 97);
        require(endpoints.client_.postMsg(message),
            "client rejected a message after delayed startup");
        require(waitUntil([&endpoints]() {
            return endpoints.serverRecorder_.messages_.size() == 1;
        }, 5s), "message did not arrive after delayed client startup");
        requireMessages(endpoints.serverRecorder_.messages_, {message},
            "delayed-client server");
    }

    {
        EndpointPair endpoints("client_starts_first");
        endpoints.client_.start();
        std::this_thread::sleep_for(75ms);
        pumpMainThreadTasks();
        require(endpoints.serverRecorder_.connectedCount_ == 0
            && endpoints.clientRecorder_.connectedCount_ == 0,
            "handshake completed before the server started");

        endpoints.server_.start();
        endpoints.waitForConnection();
        const auto message = makeMessage(4001, 113);
        require(endpoints.server_.postMsg(message),
            "server rejected a message after delayed startup");
        require(waitUntil([&endpoints]() {
            return endpoints.clientRecorder_.messages_.size() == 1;
        }, 5s), "message did not arrive after delayed server startup");
        requireMessages(endpoints.clientRecorder_.messages_, {message},
            "delayed-server client");
    }
}

void testServerInitiatedClose() {
    EndpointPair endpoints("server_close");
    endpoints.startAndConnect();

    const auto finalMessage = makeMessage(5000, 12000);
    require(endpoints.server_.postMsg(finalMessage),
        "server rejected its final pre-close message");

    const auto stopStart = Clock::now();
    endpoints.server_.stop();
    require(Clock::now() - stopStart < 2s,
        "server stop exceeded the bounded drain time");

    require(waitUntil([&endpoints]() {
        return endpoints.clientRecorder_.messages_.size() == 1
            && endpoints.clientRecorder_.closedCount_ == 1;
    }, 5s), "client did not receive the server's final message and close notification");
    requireMessages(endpoints.clientRecorder_.messages_, {finalMessage},
        "client during server close");
    require(!endpoints.clientRecorder_.events_.empty()
        && endpoints.clientRecorder_.events_.back() == "closed",
        "server close notification arrived before its final message");
    require(!endpoints.client_.connected(),
        "client remained connected after the server close notification");
    require(!endpoints.client_.postMsg(makeMessage(5001, 1)),
        "client accepted a message after the server closed");

    endpoints.client_.stop();
    pumpMainThreadTasks();
    require(endpoints.clientRecorder_.closedCount_ == 1,
        "server close was reported more than once");
}

void testSequentialAndRepeatedStop() {
    EndpointPair endpoints("sequential_stop");
    endpoints.startAndConnect();

    for (uint32_t index = 0; index < 16; ++index) {
        require(endpoints.client_.postMsg(makeMessage(6000 + index, 512)),
            "client rejected a pre-stop message");
        require(endpoints.server_.postMsg(makeMessage(6100 + index, 512)),
            "server rejected a pre-stop message");
    }

    const auto stopStart = Clock::now();
    endpoints.client_.stop();
    endpoints.server_.stop();
    require(Clock::now() - stopStart < 2s, "sequential client/server stop did not finish promptly");

    const auto repeatedStopStart = Clock::now();
    endpoints.client_.stop();
    endpoints.server_.stop();
    require(Clock::now() - repeatedStopStart < 100ms,
        "repeated stop was not idempotent and immediate");
    require(!endpoints.client_.postMsg(makeMessage(6200, 1)) && !endpoints.server_.postMsg(makeMessage(6201, 1)),
            "an endpoint accepted a message after sequential stop");
}

void testCallbackThreadAndRemoval() {
    const auto mainThread = std::this_thread::get_id();
    EndpointPair endpoints("callback_contract");
    Recorder removedRecorder;
    endpoints.server_.addCallback(&removedRecorder);
    endpoints.startAndConnect();

    require(removedRecorder.connectedCount_ == 1,
        "additional callback missed the connection notification");
    endpoints.server_.removeCallback(&removedRecorder);

    const auto clientMessage = makeMessage(7000, 257);
    const auto serverMessage = makeMessage(7001, 263);
    require(endpoints.client_.postMsg(clientMessage),
        "client rejected callback-contract message");
    require(endpoints.server_.postMsg(serverMessage),
        "server rejected callback-contract message");
    require(waitUntil([&endpoints]() {
        return endpoints.serverRecorder_.messages_.size() == 1
            && endpoints.clientRecorder_.messages_.size() == 1;
    }, 5s), "callback-contract messages did not arrive");

    endpoints.client_.stop();
    require(waitUntil([&endpoints]() {
        return endpoints.serverRecorder_.closedCount_ == 1;
    }, 5s), "server did not receive callback-contract close notification");

    require(removedRecorder.messages_.empty() && removedRecorder.closedCount_ == 0,
        "removed callback continued receiving notifications");
    requireCallbacksOnThread(removedRecorder, mainThread, "removed recorder");
    requireCallbacksOnThread(endpoints.serverRecorder_, mainThread, "server recorder");
    requireCallbacksOnThread(endpoints.clientRecorder_, mainThread, "client recorder");
}

void testHandshakeOrderingAndAdmission() {
    EndpointPair endpoints("handshake_ordering");
    const auto rejected = makeMessage(8000, 1);
    require(!endpoints.server_.postMsg(rejected),
        "server accepted a business message before the handshake");

    endpoints.server_.start();
    endpoints.client_.start();
    const auto immediate = makeMessage(8001, 333);
    require(endpoints.client_.postMsg(immediate),
        "client rejected a message queued immediately after start");

    require(waitUntil([&endpoints]() {
        return endpoints.serverRecorder_.connectedCount_ == 1
            && endpoints.clientRecorder_.connectedCount_ == 1
            && endpoints.serverRecorder_.messages_.size() == 1;
    }, 5s), "handshake and immediate business message did not complete");
    requireMessages(endpoints.serverRecorder_.messages_, {immediate},
        "handshake-ordering server");
    require(endpoints.serverRecorder_.events_.size() >= 2
        && endpoints.serverRecorder_.events_[0] == "connected"
        && endpoints.serverRecorder_.events_[1] == "message",
        "business message callback ran before the connection callback");

    const auto accepted = makeMessage(8002, 347);
    require(endpoints.server_.postMsg(accepted),
        "server rejected a business message after the handshake");
    require(waitUntil([&endpoints]() {
        return endpoints.clientRecorder_.messages_.size() == 1;
    }, 5s), "post-handshake server message did not arrive");
    requireMessages(endpoints.clientRecorder_.messages_, {accepted},
        "handshake-ordering client");
}

void testIndependentEndpointPairs() {
    EndpointPair first("isolated_first");
    EndpointPair second("isolated_second");
    first.startAndConnect();
    second.startAndConnect();

    const auto firstToServer = makeMessage(8100, 401);
    const auto firstToClient = makeMessage(8101, 409);
    const auto secondToServer = makeMessage(8200, 419);
    const auto secondToClient = makeMessage(8201, 421);
    require(first.client_.postMsg(firstToServer)
        && first.server_.postMsg(firstToClient)
        && second.client_.postMsg(secondToServer)
        && second.server_.postMsg(secondToClient),
        "an isolated endpoint rejected a message while connected");

    require(waitUntil([&first, &second]() {
        return first.serverRecorder_.messages_.size() == 1
            && first.clientRecorder_.messages_.size() == 1
            && second.serverRecorder_.messages_.size() == 1
            && second.clientRecorder_.messages_.size() == 1;
    }, 5s), "isolated endpoint messages did not all arrive");
    requireMessages(first.serverRecorder_.messages_, {firstToServer}, "first server");
    requireMessages(first.clientRecorder_.messages_, {firstToClient}, "first client");
    requireMessages(second.serverRecorder_.messages_, {secondToServer}, "second server");
    requireMessages(second.clientRecorder_.messages_, {secondToClient}, "second client");
}

void testMegabyteBidirectionalTransfer() {
    EndpointPair endpoints("megabyte_transfer");
    endpoints.startAndConnect();

    const TestMessage clientToServer(8300, patternedData(1024 * 1024, 17));
    const TestMessage serverToClient(8301, patternedData(768 * 1024, 18));
    require(endpoints.client_.postMsg(clientToServer),
        "client rejected a one-megabyte message");
    require(endpoints.server_.postMsg(serverToClient),
        "server rejected a large reverse-direction message");

    require(waitUntil([&endpoints]() {
        return endpoints.serverRecorder_.messages_.size() == 1
            && endpoints.clientRecorder_.messages_.size() == 1;
    }, 15s), "megabyte bidirectional transfer did not complete");
    requireMessages(endpoints.serverRecorder_.messages_, {clientToServer},
        "megabyte-transfer server");
    requireMessages(endpoints.clientRecorder_.messages_, {serverToClient},
        "megabyte-transfer client");
}

void testSharedObjectNameReuse() {
    const auto reusedUnique = uniqueSuffix("reused_objects");
    {
        EndpointPair first(reusedUnique);
        first.startAndConnect();
        const auto firstMessage = makeMessage(8400, 503);
        require(first.client_.postMsg(firstMessage),
            "first reused-name cycle rejected a message");
        require(waitUntil([&first]() {
            return first.serverRecorder_.messages_.size() == 1;
        }, 5s), "first reused-name cycle did not deliver its message");
        requireMessages(first.serverRecorder_.messages_, {firstMessage},
            "first reused-name server");
        first.client_.stop();
        require(waitUntil([&first]() {
            return first.serverRecorder_.closedCount_ == 1;
        }, 5s), "first reused-name cycle did not close cleanly");
        first.server_.stop();
    }

    {
        EndpointPair second(reusedUnique);
        require(second.serverRecorder_.events_.empty()
            && second.clientRecorder_.events_.empty(),
            "recreated endpoints observed callbacks before start");
        second.startAndConnect();
        require(second.serverRecorder_.messages_.empty()
            && second.clientRecorder_.messages_.empty(),
            "recreated endpoints received stale shared-memory data");

        const auto secondMessage = makeMessage(8401, 509);
        require(second.server_.postMsg(secondMessage),
            "second reused-name cycle rejected a message");
        require(waitUntil([&second]() {
            return second.clientRecorder_.messages_.size() == 1;
        }, 5s), "second reused-name cycle did not deliver its message");
        requireMessages(second.clientRecorder_.messages_, {secondMessage},
            "second reused-name client");
    }
}

void testLongEndpointName() {
    std::u8string unique = uniqueSuffix("long_endpoint_name");
    unique.append(512, u8'x');
    EndpointPair endpoints(std::move(unique));
    endpoints.startAndConnect();

    const auto message = makeMessage(8450, 521);
    require(endpoints.client_.postMsg(message),
        "long-name client rejected a message");
    require(waitUntil([&endpoints]() {
        return endpoints.serverRecorder_.messages_.size() == 1;
    }, 5s), "long-name endpoint did not deliver its message");
    requireMessages(endpoints.serverRecorder_.messages_, {message},
        "long-name server");
}

void testUnstartedStopAdmission() {
    {
        const auto unique = uniqueSuffix("unstarted_server");
        IPCServer server(unique.c_str());
        const auto message = makeMessage(8500, 1);
        require(!server.postMsg(message),
            "unstarted server accepted a message before connection");

        const auto stopStart = Clock::now();
        server.stop();
        server.stop();
        require(Clock::now() - stopStart < 100ms,
            "stopping an unstarted server was not immediate");
        require(!server.postMsg(message),
            "unstarted server accepted a message after stop");
    }

    {
        const auto unique = uniqueSuffix("unstarted_client");
        IPCClient client(unique.c_str());
        const auto stopStart = Clock::now();
        client.stop();
        client.stop();
        require(Clock::now() - stopStart < 100ms,
            "stopping an unstarted client was not immediate");
        require(!client.postMsg(makeMessage(8501, 1)),
            "unstarted client accepted a message after stop");
    }

    {
        EndpointPair endpoints("start_after_unstarted_stop");
        endpoints.client_.stop();
        require(!endpoints.client_.postMsg(makeMessage(8502, 1)),
            "client accepted a message between pre-start stop and start");

        endpoints.server_.start();
        endpoints.client_.start();
        endpoints.waitForConnection();
        const auto message = makeMessage(8503, 127);
        require(endpoints.client_.postMsg(message),
            "client did not reopen writes when started after a pre-start stop");
        require(waitUntil([&endpoints]() {
            return endpoints.serverRecorder_.messages_.size() == 1;
        }, 5s), "client lost its handshake after a pre-start stop");
        requireMessages(endpoints.serverRecorder_.messages_, {message},
            "start-after-pre-start-stop server");
    }
}

void testBidirectionalConcurrentProducers() {
    constexpr uint32_t KProducerCount = 3;
    constexpr uint32_t KMessagesPerProducer = 32;
    constexpr uint32_t KClientBase = 11000;
    constexpr uint32_t KServerBase = 12000;

    EndpointPair endpoints("bidirectional_producers");
    endpoints.startAndConnect();

    const auto makeExpected = [](uint32_t base) {
        std::vector<TestMessage> messages;
        messages.reserve(KProducerCount * KMessagesPerProducer);
        for (uint32_t producer = 0; producer < KProducerCount; ++producer) {
            for (uint32_t index = 0; index < KMessagesPerProducer; ++index) {
                const uint32_t sequence = base + producer * KMessagesPerProducer + index;
                const size_t payloadSize = index % 11 == 0
                    ? 5000
                    : 23 + (producer * 29 + index * 7) % 211;
                messages.push_back(makeMessage(sequence, payloadSize));
            }
        }
        return messages;
    };
    auto clientExpected = makeExpected(KClientBase);
    auto serverExpected = makeExpected(KServerBase);

    std::atomic<bool> allAccepted{true};
    std::vector<std::thread> producers;
    producers.reserve(2 * KProducerCount);
    for (uint32_t producer = 0; producer < KProducerCount; ++producer) {
        producers.emplace_back([&endpoints, &clientExpected, &allAccepted, producer]() {
            const size_t first = producer * KMessagesPerProducer;
            for (size_t index = first; index < first + KMessagesPerProducer; ++index) {
                if (!endpoints.client_.postMsg(clientExpected[index])) {
                    allAccepted.store(false, std::memory_order_release);
                    return;
                }
            }
        });
        producers.emplace_back([&endpoints, &serverExpected, &allAccepted, producer]() {
            const size_t first = producer * KMessagesPerProducer;
            for (size_t index = first; index < first + KMessagesPerProducer; ++index) {
                if (!endpoints.server_.postMsg(serverExpected[index])) {
                    allAccepted.store(false, std::memory_order_release);
                    return;
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    require(allAccepted.load(std::memory_order_acquire),
        "a bidirectional concurrent producer message was rejected");

    require(waitUntil([&endpoints, &clientExpected, &serverExpected]() {
        return endpoints.serverRecorder_.messages_.size() == clientExpected.size()
            && endpoints.clientRecorder_.messages_.size() == serverExpected.size();
    }, 10s), "bidirectional concurrent producer messages did not all arrive");

    const auto validate = [](const std::vector<TestMessage>& actual,
                             std::vector<TestMessage> expected, uint32_t base,
                             const std::string& endpoint) {
        std::vector<int32_t> lastIndex(KProducerCount, -1);
        for (const auto& message : actual) {
            require(message.sequence_ >= base
                && message.sequence_ < base + KProducerCount * KMessagesPerProducer,
                endpoint + " received an unexpected producer sequence");
            const uint32_t offset = message.sequence_ - base;
            const uint32_t producer = offset / KMessagesPerProducer;
            const int32_t index = static_cast<int32_t>(offset % KMessagesPerProducer);
            require(index == lastIndex[producer] + 1,
                endpoint + " reordered messages from one producer");
            lastIndex[producer] = index;
        }

        auto sortedActual = actual;
        const auto bySequence = [](const TestMessage& left, const TestMessage& right) {
            return left.sequence_ < right.sequence_;
        };
        std::sort(sortedActual.begin(), sortedActual.end(), bySequence);
        std::sort(expected.begin(), expected.end(), bySequence);
        requireMessages(sortedActual, expected, endpoint);
    };
    validate(endpoints.serverRecorder_.messages_, clientExpected, KClientBase,
        "bidirectional producer server");
    validate(endpoints.clientRecorder_.messages_, serverExpected, KServerBase,
        "bidirectional producer client");
}

void testRemoteCloseWithOutboundBacklog() {
    EndpointPair endpoints("close_with_backlog");
    endpoints.startAndConnect();

    const TestMessage backlog(13000, patternedData(4 * 1024 * 1024, 19));
    require(endpoints.client_.postMsg(backlog),
        "client rejected the outbound backlog message");
    std::this_thread::sleep_for(10ms);

    const auto serverStopStart = Clock::now();
    endpoints.server_.stop();
    require(Clock::now() - serverStopStart < 2s,
        "server stop blocked behind the client's outbound backlog");
    require(waitUntil([&endpoints]() {
        return endpoints.clientRecorder_.closedCount_ == 1;
    }, 5s), "client did not observe remote close while its writer was backlogged");
    require(!endpoints.client_.postMsg(makeMessage(13001, 1)),
        "client accepted a message after remote close with outbound backlog");

    const auto clientStopStart = Clock::now();
    endpoints.client_.stop();
    require(Clock::now() - clientStopStart < 2s,
        "client stop blocked while abandoning outbound backlog");
}

void testCallbackReentrantPost() {
    EndpointPair endpoints("callback_reply");
    ReplyingCallback replying(&endpoints.server_);
    endpoints.server_.addCallback(&replying);
    endpoints.startAndConnect();

    const TestMessage request(14000, patternedData(777, 20));
    require(endpoints.client_.postMsg(request),
        "client rejected the callback-reply request");
    require(waitUntil([&endpoints, &replying]() {
        return replying.requestCount_ == 1
            && endpoints.clientRecorder_.messages_.size() == 1;
    }, 5s), "callback did not post and deliver its response");
    require(replying.error_.empty(), replying.error_);
    requireMessages(endpoints.clientRecorder_.messages_, replying.responses_,
        "callback-reply client");
    require(replying.responses_[0].sequence_ == request.sequence_ + 1
        && replying.responses_[0].payload_ == request.payload_ + "_reply",
        "callback response contents were incorrect");
    endpoints.server_.removeCallback(&replying);
}

void testCallbackSelfRemoval() {
    EndpointPair endpoints("callback_self_removal");
    SelfRemovingCallback selfRemoving(&endpoints.server_);
    endpoints.server_.removeCallback(&endpoints.serverRecorder_);
    endpoints.server_.addCallback(&selfRemoving);
    endpoints.server_.addCallback(&endpoints.serverRecorder_);
    endpoints.startAndConnect();

    const auto first = makeMessage(15000, 101);
    const auto second = makeMessage(15001, 103);
    require(endpoints.client_.postMsg(first) && endpoints.client_.postMsg(second),
        "client rejected a callback self-removal message");
    require(waitUntil([&endpoints]() {
        return endpoints.serverRecorder_.messages_.size() == 2;
    }, 5s), "primary callback missed messages during callback self-removal");
    require(selfRemoving.messageCount_ == 1,
        "self-removing callback was invoked after removing itself");
    requireMessages(endpoints.serverRecorder_.messages_, {first, second},
        "callback-self-removal server");
}

void testCallbackMutationDuringDispatch() {
    {
        EndpointPair endpoints("callback_removes_other");
        ActionCallback removed;
        ActionCallback remover([&endpoints, &removed]() {
            endpoints.server_.removeCallback(&removed);
        });
        endpoints.server_.removeCallback(&endpoints.serverRecorder_);
        endpoints.server_.addCallback(&remover);
        endpoints.server_.addCallback(&removed);
        endpoints.server_.addCallback(&endpoints.serverRecorder_);
        endpoints.startAndConnect();

        require(endpoints.client_.postMsg(makeMessage(15100, 107)),
            "client rejected callback-removes-other message");
        require(waitUntil([&endpoints]() {
            return endpoints.serverRecorder_.messages_.size() == 1;
        }, 5s), "primary callback missed callback-removes-other message");
        require(remover.messageCount_ == 1 && removed.messageCount_ == 0,
            "callback removed during dispatch still received the current message");
        endpoints.server_.removeCallback(&remover);
    }

    {
        EndpointPair endpoints("callback_adds_other");
        ActionCallback added;
        ActionCallback adder;
        adder.action_ = [&endpoints, &adder, &added]() {
            if (adder.messageCount_ == 1) {
                endpoints.server_.addCallback(&added);
            }
        };
        endpoints.server_.removeCallback(&endpoints.serverRecorder_);
        endpoints.server_.addCallback(&adder);
        endpoints.server_.addCallback(&endpoints.serverRecorder_);
        endpoints.startAndConnect();

        require(endpoints.client_.postMsg(makeMessage(15101, 109)),
            "client rejected callback-adds-other first message");
        require(waitUntil([&endpoints]() {
            return endpoints.serverRecorder_.messages_.size() == 1;
        }, 5s), "primary callback missed callback-adds-other first message");
        require(adder.messageCount_ == 1 && added.messageCount_ == 0,
            "callback added during dispatch received the current message");

        require(endpoints.client_.postMsg(makeMessage(15102, 111)),
            "client rejected callback-adds-other second message");
        require(waitUntil([&endpoints, &added]() {
            return endpoints.serverRecorder_.messages_.size() == 2
                && added.messageCount_ == 1;
        }, 5s), "new callback did not receive the next message");
        endpoints.server_.removeCallback(&adder);
        endpoints.server_.removeCallback(&added);
    }
}

void testStopDrainsAcceptedMessages() {
    EndpointPair endpoints("drain");
    endpoints.startAndConnect();

    std::vector<TestMessage> accepted;
    for (uint32_t sequence = 0; sequence < 48; ++sequence) {
        accepted.push_back(makeMessage(sequence, sequence == 17 ? 16000 : 256));
        require(endpoints.client_.postMsg(accepted.back()), "client rejected a pre-stop message");
    }

    std::atomic<uint32_t> concurrentAccepted{0};
    std::thread producer([&endpoints, &accepted, &concurrentAccepted]() {
        for (uint32_t sequence = 48; sequence < 128; ++sequence) {
            auto message = makeMessage(sequence, 128);
            if (!endpoints.client_.postMsg(message)) {
                return;
            }
            accepted.push_back(std::move(message));
            concurrentAccepted.fetch_add(1, std::memory_order_release);
            std::this_thread::sleep_for(100us);
        }
    });
    require(waitUntil([&concurrentAccepted]() {
        return concurrentAccepted.load(std::memory_order_acquire) >= 4;
    }, 1s), "concurrent producer did not enqueue messages before stop");

    const auto stopStart = Clock::now();
    endpoints.client_.stop();
    const auto stopDuration = Clock::now() - stopStart;
    producer.join();
    require(stopDuration < 2s, "client stop exceeded the bounded drain time");

    require(waitUntil([&endpoints, &accepted]() {
        return endpoints.serverRecorder_.messages_.size() == accepted.size()
            && endpoints.serverRecorder_.closedCount_ == 1;
    }, 5s), "server did not receive all accepted messages and the close notification");
    require(endpoints.serverRecorder_.error_.empty(), endpoints.serverRecorder_.error_);
    requireMessages(endpoints.serverRecorder_.messages_, accepted, "server during stop drain");
    require(!endpoints.serverRecorder_.events_.empty()
        && endpoints.serverRecorder_.events_.back() == "closed",
        "close notification arrived before queued messages");

    const auto rejected = makeMessage(999, 32);
    require(!endpoints.client_.postMsg(rejected), "client accepted a message after stop began");
    endpoints.server_.stop();
}

void testStopReturnsWhenPeerDoesNotRead() {
    const auto unique = uniqueSuffix("stalled_peer");
    IPCClient client(unique.c_str());
    client.start();

    const auto largeMessage = makeMessage(1, 256 * 1024);
    require(client.postMsg(largeMessage), "client rejected the stalled-peer test message");
    std::this_thread::sleep_for(100ms);

    const auto stopStart = Clock::now();
    client.stop();
    const auto stopDuration = Clock::now() - stopStart;
    require(stopDuration < 2s, "stop blocked when the peer did not read shared memory");
    require(!client.postMsg(largeMessage), "stopped client accepted another message");
}

void testImmediateStop() {
    const auto unique = uniqueSuffix("immediate_stop");
    IPCServer server(unique.c_str());
    server.start();

    const auto stopStart = Clock::now();
    server.stop();
    require(Clock::now() - stopStart < 1s, "an idle IPC server did not stop promptly");
}

void runTest(const char* name, const std::function<void()>& test) {
    std::cout << "[ RUN      ] " << name << std::endl;
    test();
    std::cout << "[       OK ] " << name << std::endl;
}

} // namespace

int main() {
    try {
        const auto logFile = std::filesystem::temp_directory_path() / "cici_ipcwrapper_tests.log";
        initCiciLogConfig(6, logFile.u8string());
        require(initCiciBase(), "failed to initialize the main-thread dispatcher");

        runTest("EventWaitFor", testEventWaitFor);
        runTest("SharedMemoryRingBoundaries", testSharedMemoryRingBoundaries);
        runTest("SharedMemoryPositionRelations", testSharedMemoryPositionRelations);
        runTest("SharedMemoryRandomizedModel", testSharedMemoryRandomizedModel);
        runTest("MutexLockFor", testMutexLockFor);
        runTest("SharedMemoryLockWaits", testSharedMemoryLockWaits);
        runTest("BidirectionalMessaging", testBidirectionalMessaging);
        runTest("MessageFramingBoundaries", testMessageFramingBoundaries);
        runTest("ConcurrentProducers", testConcurrentProducers);
        runTest("DelayedPeerStartup", testDelayedPeerStartup);
        runTest("ServerInitiatedClose", testServerInitiatedClose);
        runTest("SequentialAndRepeatedStop", testSequentialAndRepeatedStop);
        runTest("CallbackThreadAndRemoval", testCallbackThreadAndRemoval);
        runTest("HandshakeOrderingAndAdmission", testHandshakeOrderingAndAdmission);
        runTest("IndependentEndpointPairs", testIndependentEndpointPairs);
        runTest("MegabyteBidirectionalTransfer", testMegabyteBidirectionalTransfer);
        runTest("SharedObjectNameReuse", testSharedObjectNameReuse);
        runTest("LongEndpointName", testLongEndpointName);
        runTest("UnstartedStopAdmission", testUnstartedStopAdmission);
        runTest("BidirectionalConcurrentProducers", testBidirectionalConcurrentProducers);
        runTest("RemoteCloseWithOutboundBacklog", testRemoteCloseWithOutboundBacklog);
        runTest("CallbackReentrantPost", testCallbackReentrantPost);
        runTest("CallbackSelfRemoval", testCallbackSelfRemoval);
        runTest("CallbackMutationDuringDispatch", testCallbackMutationDuringDispatch);
        runTest("StopDrainsAcceptedMessages", testStopDrainsAcceptedMessages);
        runTest("StopReturnsWhenPeerDoesNotRead", testStopReturnsWhenPeerDoesNotRead);
        runTest("ImmediateStop", testImmediateStop);
    } catch (const std::exception& error) {
        std::cerr << "[  FAILED  ] " << error.what() << std::endl;
        return 1;
    }

    std::cout << "[  PASSED  ] all ipcwrapper tests" << std::endl;
    return 0;
}

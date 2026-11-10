// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "shmframe.h"
#include "log.h"
#include "misc.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

#if CICI_OS_MACOS
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

using Result = SHMFrame::Result;
using namespace std::chrono_literals;

namespace {
int failures = 0;
std::filesystem::path logFile;
constexpr uint32_t kFrameBytes = 8192;
constexpr uint32_t kFrames = 3000;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::u8string uniqueName() {
    static std::atomic<uint32_t> serial{
        static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
#if CICI_OS_MACOS
    const auto pid = static_cast<uint32_t>(getpid());
#else
    const auto pid = static_cast<uint32_t>(GetCurrentProcessId());
#endif
    char name[24];
    std::snprintf(name, sizeof(name), "sf%08x%08x", pid, serial.fetch_add(1));
    return to_u8(name);
}

std::u8string semaphoreName() {
#if CICI_OS_MACOS
    return u8"/" + uniqueName();
#else
    return u8"Local\\" + uniqueName();
#endif
}

std::u8string completionName(const std::u8string& name) {
#if CICI_OS_MACOS
    return u8"/" + name + u8"_done";
#else
    return u8"Local\\" + name + u8"_done";
#endif
}

void cleanupLog() {
    std::error_code error;
    std::filesystem::remove(logFile, error);
}

// Fail boundedly if the peer errors out instead of leaving a test in sem_wait.
class QueueDeadline {
public:
    explicit QueueDeadline(SHMFrame& queue)
        : thread_([this, &queue] {
              std::unique_lock lock(mutex_);
              if (!changed_.wait_for(lock, 8s, [this] {
                      return finished_;
                  })) {
                  queue.close();
              }
          }) {
    }
    ~QueueDeadline() {
        {
            std::lock_guard lock(mutex_);
            finished_ = true;
        }
        changed_.notify_one();
        thread_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool finished_ = false;
    std::thread thread_;
};

bool produce(SHMFrame& queue) {
    for (uint64_t seq = 0; seq < kFrames; ++seq) {
        std::span<uint8_t> frame;
        if (queue.acquireWrite(frame, true) != Result::Success) {
            return false;
        }
        const uint32_t bytes = 8 + static_cast<uint32_t>((seq * 37) % (kFrameBytes - 8));
        std::memcpy(frame.data(), &seq, sizeof(seq));
        for (uint32_t i = 8; i < bytes; ++i) {
            frame[i] = static_cast<uint8_t>(seq * 13 + i);
        }
        if (queue.commitWrite(bytes) != Result::Success) {
            return false;
        }
        if (seq % 97 == 0) {
            std::this_thread::sleep_for(100us);
        }
    }
    return true;
}

bool consume(SHMFrame& queue) {
    for (uint64_t seq = 0; seq < kFrames; ++seq) {
        // Alternate blocking reads and count-then-acquire across processes.
        if (seq % 2 == 0) {
            while (!queue.readableCount() && !queue.isClosed()) {
                std::this_thread::yield();
            }
        }
        if (queue.acquireRead(seq % 2 != 0) != Result::Success) {
            return false;
        }
        const auto frame = queue.readView();
        uint64_t actual = UINT64_MAX;
        if (frame.size() >= 8) {
            std::memcpy(&actual, frame.data(), sizeof(actual));
        }
        const uint32_t bytes = 8 + static_cast<uint32_t>((seq * 37) % (kFrameBytes - 8));
        bool valid = actual == seq && frame.size() == bytes;
        for (uint32_t i = 8; valid && i < bytes; ++i) {
            valid = frame[i] == static_cast<uint8_t>(seq * 13 + i);
        }
        if (queue.releaseRead() != Result::Success || !valid) {
            return false;
        }
        if (seq % 71 == 0) {
            std::this_thread::sleep_for(100us);
        }
    }
    return true;
}

void testSemaphore() {
    const auto name = semaphoreName();
    Semaphore owner;
    check(owner.create(name, 0), "create semaphore");
    check(owner.tryWait() == Semaphore::Result::WouldBlock, "empty semaphore is nonblocking");
    {
        Semaphore peer;
        check(peer.open(name), "open semaphore");
        check(peer.signal() && peer.signal() && peer.signal(), "post before waiter starts");
    }
    Semaphore later;
    check(later.open(name), "opener destruction does not unlink semaphore");
    Semaphore duplicate;
    check(!duplicate.create(name, 0), "exclusive semaphore creation");
    for (int i = 0; i < 3; ++i) {
        check(owner.wait() == Semaphore::Result::Acquired, "notifications retain their count");
    }
    check(later.tryWait() == Semaphore::Result::WouldBlock, "no extra semaphore tokens");
    Semaphore missing;
    check(!missing.open(semaphoreName()), "opening missing semaphore fails");
}

void testSlots() {
    const auto name = uniqueName();
    SHMFrame writer, reader;
    if (!writer.create(name, 2, 64) || !reader.open(name)) {
        check(false, "create/open queue");
        return;
    }
    check(reader.capacity() == 2 && reader.frameBytes() == 64, "immutable queue layout");
    SHMFrame duplicate;
    check(!duplicate.create(name, 2, 64), "duplicate queue does not overwrite existing layout");
    std::span<uint8_t> write, otherWrite;
    check(reader.acquireRead() == Result::WouldBlock && reader.readView().empty(), "empty queue read is nonblocking");
    check(reader.readableCount() == 0, "empty readable count");
    check(writer.acquireWrite(write) == Result::Success, "reserve write slot");
    check(writer.acquireWrite(otherWrite) == Result::InvalidOperation, "only one outstanding write");
    check(reader.acquireRead() == Result::WouldBlock, "uncommitted data stays invisible");
    check(writer.commitWrite(65) == Result::InvalidOperation, "oversized commit rejected");
    check(writer.cancelWrite() == Result::Success, "cancel restores empty slot");
    check(writer.commitWrite(0) == Result::InvalidOperation, "commit requires a lease");
    check(reader.releaseRead() == Result::InvalidOperation, "release requires a lease");
    for (uint8_t value = 1; value <= 2; ++value) {
        check(writer.acquireWrite(write) == Result::Success, "fill all queue slots");
        write[0] = value;
        check(writer.commitWrite(1) == Result::Success, "publish one byte");
    }
    check(writer.acquireWrite(write) == Result::WouldBlock, "full queue write is nonblocking");
    check(reader.acquireRead() == Result::Success && reader.readView().size() == 1 && reader.readView()[0] == 1,
          "FIFO head");
    check(reader.acquireRead() == Result::InvalidOperation && reader.readView()[0] == 1,
          "reacquire cannot skip held frame");
    check(reader.readableCount() == 2, "readable count includes held frame and next frame");
    check(writer.acquireWrite(write) == Result::WouldBlock, "borrowed read slot cannot be overwritten");
    check(!writer.isDrained(), "held read still counts as unconsumed");
    check(reader.releaseRead() == Result::Success && reader.readView().empty(), "release clears read view");
    check(reader.readableCount() == 1 && writer.isDrained() == false, "release leaves next frame readable");
    check(writer.acquireWrite(write) == Result::Success, "wrap around to released slot");
    write[0] = 3;
    check(writer.commitWrite(1) == Result::Success, "publish wrapped frame");
    for (uint8_t value = 2; value <= 3; ++value) {
        check(reader.acquireRead() == Result::Success && reader.readView()[0] == value, "FIFO after wrap");
        check(reader.releaseRead() == Result::Success, "release wrapped frame");
    }
    check(writer.acquireWrite(write) == Result::Success && writer.commitWrite(0) == Result::Success,
          "zero-length frame can be committed");
    check(reader.acquireRead() == Result::Success && reader.readView().empty(),
          "zero-length frame is distinct from empty queue");
    check(!writer.isDrained() && reader.readableCount() == 1, "final held read prevents retirement");
    check(reader.releaseRead() == Result::Success, "release zero-length frame");
    check(writer.isDrained() && reader.readableCount() == 0, "all released records permit retirement");
    writer.close();
    check(reader.isClosed() && reader.acquireRead() == Result::Closed, "shared close state");
    check(writer.acquireWrite(write) == Result::Closed, "closed channel cannot be reused");
    SHMFrame closed;
    check(!closed.open(name), "cannot attach to closed queue");
}

void testCloseWait(bool writerWaits) {
    SHMFrame owner, peer;
    const auto name = uniqueName();
    if (!owner.create(name, 1, 64) || !peer.open(name)) {
        check(false, "create queue for blocking close");
        return;
    }
    if (writerWaits) {
        std::span<uint8_t> frame;
        check(owner.acquireWrite(frame) == Result::Success && owner.commitWrite(0) == Result::Success,
              "fill queue before blocking write");
    }
    QueueDeadline deadline(owner);
    std::promise<void> started;
    std::promise<Result> result;
    auto returned = result.get_future();
    std::thread waiter([&] {
        started.set_value();
        if (writerWaits) {
            std::span<uint8_t> frame;
            result.set_value(peer.acquireWrite(frame, true));
        } else {
            result.set_value(peer.acquireRead(true));
        }
    });
    started.get_future().wait();
    check(returned.wait_for(20ms) == std::future_status::timeout, "worker waits without busy polling");
    owner.close();
    owner.close(); // Idempotent: no repeated wake-token accumulation.
    check(returned.wait_for(1s) == std::future_status::ready, "close wakes blocked worker");
    waiter.join();
    check(returned.get() == Result::Closed, "close wakeup is not a frame/space token");
}

void testValidationAndCleanup() {
    SHMFrame invalid;
    check(!invalid.create(u8"", 1, 64) && !invalid.create(u8"invalid/name", 1, 64) &&
              !invalid.create(u8"1234567890123456789012345", 1, 64),
          "portable names validated");
    check(!invalid.create(uniqueName(), 0, 64) && !invalid.create(uniqueName(), 1, 0) &&
              !invalid.create(uniqueName(), UINT32_MAX, 64),
          "invalid queue dimensions rejected");
    check(!invalid.open(uniqueName()), "missing queue is not created by open");
    const auto name = uniqueName();
    {
        SHMFrame owner;
        check(owner.create(name, 1, 64), "create queue for cleanup");
    }
    check(!invalid.open(name), "creator removes queue name on destruction");
    check(invalid.create(name, 1, 64), "creator also removes semaphore names");
}

void testHeldViewsOnClose() {
    {
        SHMFrame owner, peer;
        if (!owner.create(uniqueName(), 1, 64)) {
            check(false, "create queue for held write");
            return;
        }
        std::span<uint8_t> frame;
        check(owner.acquireWrite(frame) == Result::Success, "acquire write before close");
        frame[0] = 42;
        owner.close();
        check(frame[0] == 42, "close keeps borrowed write memory mapped");
        check(owner.commitWrite(1) == Result::Closed, "closed write cannot publish a frame");
    }
    {
        SHMFrame owner, peer;
        const auto name = uniqueName();
        if (!owner.create(name, 1, 64) || !peer.open(name)) {
            check(false, "create queue for held read");
            return;
        }
        std::span<uint8_t> write;
        check(owner.acquireWrite(write) == Result::Success, "reserve held-read input");
        write[0] = 57;
        check(owner.commitWrite(1) == Result::Success && peer.acquireRead() == Result::Success,
              "borrow frame before close");
        owner.close();
        check(peer.readView().size() == 1 && peer.readView()[0] == 57, "peer close keeps borrowed read memory mapped");
        check(peer.releaseRead() == Result::Closed, "held read can be released after close");
    }
}

void testCrossProcess(const std::filesystem::path& executable, bool childProduces) {
    SHMFrame owner;
    const auto name = uniqueName();
    if (!owner.create(name, 3, kFrameBytes)) {
        check(false, "create process queue");
        return;
    }
    Semaphore completion;
    check(completion.create(completionName(name), 0), "create peer lifetime handshake");
    const std::string mode = childProduces ? "--produce" : "--consume";
#if CICI_OS_MACOS
    const auto exe = executable.string();
    const std::string narrowName(to_char(name));
    char* arguments[] = {const_cast<char*>(exe.c_str()), const_cast<char*>(mode.c_str()),
                         const_cast<char*>(narrowName.c_str()), nullptr};
    pid_t pid = -1;
    const bool launched = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, arguments, environ) == 0;
#else
    auto command = L"\"" + executable.wstring() + L"\" " + utf8ToWide(to_u8(mode)) + L" " + utf8ToWide(name);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                         nullptr, &startup, &process) != FALSE;
#endif
    check(launched, "launch separate process with fresh address space");
    if (!launched) {
        return;
    }
    {
        QueueDeadline deadline(owner);
        check(childProduces ? consume(owner) : produce(owner), "cross-process FIFO and full payload integrity");
    }
    // Keep the peer mapping alive until the parent has finished its last lease.
    check(completion.signal(), "allow peer to close after transfer");
#if CICI_OS_MACOS
    int status = 0;
    pid_t result = 0;
    const auto end = std::chrono::steady_clock::now() + 2s;
    do {
        result = waitpid(pid, &status, WNOHANG);
        if (result != 0) {
            break;
        }
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < end);
    if (result == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    check(result == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0, "peer process completes successfully");
#else
    const DWORD wait = WaitForSingleObject(process.hProcess, 2000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    check(wait == WAIT_OBJECT_0 && exitCode == 0, "peer process completes successfully");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#endif
}
} // namespace

int main(int argc, char** argv) {
    // Invalid-name/duplicate-open cases intentionally exercise error logging.
    // Register cleanup before constructing the logger so it runs after shutdown.
    logFile = std::filesystem::temp_directory_path() / (uniqueName() + u8".log");
    if (std::atexit(cleanupLog) != 0) {
        return 1;
    }
    initCiciLogConfig(3, logFile.u8string());
    if (argc == 3) {
        SHMFrame queue;
        if (!queue.open(to_u8(argv[2]))) {
            return 2;
        }
        QueueDeadline deadline(queue);
        const bool producer = std::strcmp(argv[1], "--produce") == 0;
        if (!producer && std::strcmp(argv[1], "--consume") != 0) {
            return 5;
        }
        if (!(producer ? produce(queue) : consume(queue))) {
            return 3;
        }
        Semaphore completion;
        if (!completion.open(completionName(to_u8(argv[2])))) {
            return 4;
        }
        return completion.wait() == Semaphore::Result::Acquired ? 0 : 6;
    }
    testSemaphore();
    testSlots();
    testCloseWait(false);
    testCloseWait(true);
    testValidationAndCleanup();
    testHeldViewsOnClose();
    const auto executable = std::filesystem::absolute(argv[0]);
    testCrossProcess(executable, false);
    testCrossProcess(executable, true);
    std::cout << (failures == 0 ? "PASS" : "FAIL")
              << ": semaphore counting, slot ownership, FIFO/wrap, close wakeups, cleanup, "
                 "and 6000 cross-process frames\n";
    return failures == 0 ? 0 : 1;
}

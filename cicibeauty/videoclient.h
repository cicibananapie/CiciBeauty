// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "protocol/video.h"
#include "shmframe.h"

struct obs_source_frame;

// OBS video-thread endpoint. Only channel switches wait for an input write slot;
// ordinary frame submissions remain nonblocking. No synchronous IPC calls.
class VideoClient {
public:
    VideoClient() = default;
    ~VideoClient();
    // Creates the control-only bootstrap pair, before instance.create is sent.
    // Returns its channel name, or empty on failure.
    std::u8string start();
    void process(obs_source_frame& frame, uint32_t transform);
    void stop();

private:
    void close();
    struct Pair {
        SHMFrame input, output;
        cici::protocol::FrameSpec spec;
        std::u8string channel;
    };
    static std::unique_ptr<Pair> createPair(const cici::protocol::FrameSpec& spec);
    std::mutex mutex_;
    std::unique_ptr<Pair> current_;
    std::vector<std::unique_ptr<Pair>> retired_;
    uint64_t sequence_ = 0;
    uint32_t transform_ = 0;
};

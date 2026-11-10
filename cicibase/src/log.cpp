// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>
#include <string>
#include <vector>

#define SPDLOG_HEADER_ONLY
#if defined(_WIN32)
#define SPDLOG_WCHAR_FILENAMES
#endif
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/async_logger.h"
#include "spdlog/async.h"

#include "log.h"
#include "misc.h"
#include "ciciconfigure.h"

unsigned int ciciDefaultLogLevel() {
#ifdef OFFICIAL_BUILD
    return static_cast<unsigned int>(LogSeverity::Info);
#else
    return static_cast<unsigned int>(LogSeverity::Trace);
#endif
}

void initCiciLogConfig(unsigned int level, const std::u8string& logFileName) {
    LogImpl::instance().setLogFileName(logFileName);
    LogImpl::instance().init();
    LogImpl::instance().enableLogLevel(level);
}

void initCiciLogConfig(unsigned int level, const char *logFileName) {
	initCiciLogConfig(level, to_u8(logFileName));
}

LogImpl::LogImpl() {
}

void LogImpl::init() {
    const std::filesystem::path path = FileSystemOp::logPath() / logFileName_;
    spdlog::init_thread_pool(8192, 1);

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path.native(), 1024 * 1024 * 5, 3);
    file_sink->set_level(spdlog::level::trace);
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

    async_logger_ = std::make_shared<spdlog::async_logger>(
            "async_logger",
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
    );

    spdlog::register_logger(async_logger_);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [PID %P] [TID %t] [%s:%!:%#] [%l] %v");
    async_logger_->set_level(spdlog::level::debug);
    async_logger_->flush_on(spdlog::level::err);
    spdlog::flush_every(std::chrono::seconds(3));
}

LogImpl::~LogImpl() {
    if (async_logger_) {
        async_logger_->flush();
    }
}

LogImpl& LogImpl::instance() {
    // spdlog's registry owns the async thread pool. Construct it before this
    // singleton so LogImpl is destroyed, and flushed, while the pool still exists.
    static const auto spdlogLifetime = spdlog::default_logger();
    (void)spdlogLifetime;
    static LogImpl impl;

    return impl;
}

void LogImpl::enableLogLevel(unsigned int level) {
    assert(level < 7);
    spdlog::level::level_enum lvl = spdlog::level::off;
    switch (level) {
        case 0:
            lvl = spdlog::level::trace;
            break;
        case 1:
            lvl = spdlog::level::debug;
            break;
        case 2:
            lvl = spdlog::level::info;
            break;
        case 3:
            lvl = spdlog::level::warn;
            break;
        case 4:
            lvl = spdlog::level::err;
            break;
        case 5:
            lvl = spdlog::level::critical;
            break;
        default:
            lvl = spdlog::level::off;
            break;
    }
    async_logger_->set_level(lvl);
}

void LogImpl::flush() {
    if (async_logger_) {
        async_logger_->flush();
    }
}

void LogImpl::write(unsigned int level, const char* filename, unsigned int line, const char* funcname, std::u8string_view msg) {
    spdlog::source_loc loc(filename, line, funcname);
    async_logger_->log(loc, static_cast<spdlog::level::level_enum>(level), std::basic_string<char>(to_char(msg.data()), msg.size()));
}

void LogImpl::writeRaw(LogSeverity severity, std::u8string_view msg) {
    async_logger_->log(static_cast<spdlog::level::level_enum>(severity),
        std::basic_string<char>(to_char(msg.data()), msg.size()));
}

LogStreamBuf::LogStreamBuf(unsigned int level, const char* filename, unsigned int line, const char* funcname)
    : level_(level)
    , filename_(filename)
    , line_(line)
    , funcname_(funcname) {
}

int LogStreamBuf::sync() {
    std::string message = str();
    if (message.empty()) {
        return 0;
    }

    str(std::string());

    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }

    LogImpl::instance().write(level_, filename_, line_, funcname_, to_u8(message));
    return 0;
}

LogStream::LogStream(unsigned int level, const char* filename, unsigned int line, const char* funcname)
    : std::ostream(nullptr)
    , buffer_(level, filename, line, funcname) {
    this->init(&buffer_);
}

LogStream::~LogStream() {
    flush();
}

std::ostream& operator<<(std::ostream& os, std::u8string_view sv) {
    if (!sv.empty()) {
        os.write(reinterpret_cast<const char*>(sv.data()), static_cast<std::streamsize>(sv.size()));
    }

    return os;
}

std::ostream& operator<<(std::ostream& os, const std::u8string& str) {
    return os << std::u8string_view(str);
}

std::ostream& operator<<(std::ostream& os, const char8_t* str) {
    if (str == nullptr) {
        return os << "nullptr";
    }

    return os << std::u8string_view(str);
}

std::ostream& operator<<(std::ostream& os, char8_t* str) {
    return os << const_cast<const char8_t*>(str);
}

// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>
#include <string>
#include <string_view>
#include <memory>
#include <ostream>
#include <sstream>
#include <type_traits>

namespace spdlog {
class async_logger;
}

enum class LogSeverity : uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};

class LogImpl {
    LogImpl();
    
public:
    ~LogImpl();
    static LogImpl& instance();
    
    void write(unsigned int level, const char* filename, unsigned int line, const char* funcname, std::u8string_view msg);
    void writeRaw(LogSeverity severity, std::u8string_view msg);

    void init();
    void flush();
    void setLogFileName(const std::u8string &name) { logFileName_ = name; }
    void enableLogLevel(unsigned int level);
    
private:
    std::u8string logFileName_ = u8"async_app.log";
    std::shared_ptr<spdlog::async_logger> async_logger_;
};


class LogStreamBuf : public std::stringbuf {
public:
    LogStreamBuf(unsigned int level, const char* filename, unsigned int line, const char* funcname);
    int sync() override;

private:
    unsigned int level_ = 0;
    const char* filename_ = nullptr;
    unsigned int line_ = 0;
    const char* funcname_ = nullptr;
};

class LogStream : public std::ostream {
public:
    LogStream(unsigned int level, const char* filename, unsigned int line, const char* funcname);
    ~LogStream() override;

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    LogStream(LogStream&&) = delete;
    LogStream& operator=(LogStream&&) = delete;

private:
    LogStreamBuf buffer_;
};

std::ostream& operator<<(std::ostream& os, std::u8string_view sv);
std::ostream& operator<<(std::ostream& os, const std::u8string& str);
std::ostream& operator<<(std::ostream& os, const char8_t* str);
std::ostream& operator<<(std::ostream& os, char8_t* str);
template <typename Enum, std::enable_if_t<std::is_enum_v<Enum> &&
                                           !std::is_convertible_v<Enum, std::underlying_type_t<Enum>>, int> = 0>
std::ostream& operator<<(std::ostream& os, Enum value) {
    return os << static_cast<std::underlying_type_t<Enum>>(value);
}

#define LogTrace    LogStream(0, __FILE__, __LINE__, __FUNCTION__)
#define LogDebug    LogStream(1, __FILE__, __LINE__, __FUNCTION__)
#define LogInfo     LogStream(2, __FILE__, __LINE__, __FUNCTION__)
#define LogWarn     LogStream(3, __FILE__, __LINE__, __FUNCTION__)
#define LogError    LogStream(4, __FILE__, __LINE__, __FUNCTION__)
#define LogFatal    LogStream(5, __FILE__, __LINE__, __FUNCTION__)
#define logEnd      std::endl

void initCiciLogConfig(unsigned int level, const std::u8string& logFileName);
unsigned int ciciDefaultLogLevel();

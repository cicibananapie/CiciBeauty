// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#ifndef CICI_OS_WINDOWS
#if defined(_WIN32) || defined(_WIN64)
#define CICI_OS_WINDOWS 1
#else
#define CICI_OS_WINDOWS 0
#endif
#endif

#ifndef CICI_OS_MACOS
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if (defined(TARGET_OS_OSX) && TARGET_OS_OSX) || (defined(TARGET_OS_MAC) && TARGET_OS_MAC && defined(TARGET_OS_IPHONE) && !TARGET_OS_IPHONE)
#define CICI_OS_MACOS 1
#else
#define CICI_OS_MACOS 0
#endif
#else
#define CICI_OS_MACOS 0
#endif
#endif

#if CICI_OS_WINDOWS && CICI_OS_MACOS
#error "CICI_OS_WINDOWS and CICI_OS_MACOS cannot both be 1"
#endif

#if !CICI_OS_WINDOWS && !CICI_OS_MACOS
#error "Unsupported platform: only Windows and macOS are supported"
#endif

#if defined(__cplusplus) && CICI_OS_WINDOWS && defined(__clang__) && defined(_M_X64)
#include <intrin.h>

static inline unsigned __int64 cici_clangcl_udiv128(
    unsigned __int64 high,
    unsigned __int64 low,
    unsigned __int64 divisor,
    unsigned __int64 *remainder) {
    const unsigned __int128 dividend =
        (static_cast<unsigned __int128>(high) << 64) | low;
    *remainder = static_cast<unsigned __int64>(dividend % divisor);
    return static_cast<unsigned __int64>(dividend / divisor);
}

// VS ships Clang 17 without the MSVC _udiv128 intrinsic used by OBS headers.
#define _udiv128 cici_clangcl_udiv128
#endif

#ifdef __cplusplus

#include <assert.h>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

typedef std::string BinaryStream;

#ifndef CICI_FORCE_INLINE
#if defined(_MSC_VER)
#define CICI_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define CICI_FORCE_INLINE inline __attribute__((always_inline))
#else
#define CICI_FORCE_INLINE inline
#endif  //defined(_MSC_VER)
#endif	//CICI_FORCE_INLINE

CICI_FORCE_INLINE const char *to_char(const char8_t *p) {
	return reinterpret_cast<const char *>(p);
}

CICI_FORCE_INLINE const char* to_char(const std::u8string& s) {
    return reinterpret_cast<const char*>(s.c_str());
}
CICI_FORCE_INLINE nlohmann::json to_json(const std::u8string &s) {
	nlohmann::json j;

	j = to_char(s);

	return j;
}

CICI_FORCE_INLINE const char8_t *to_u8(const char *p) {
	return reinterpret_cast<const char8_t *>(p);
}

// Prevent calling to_u8 on values that are already UTF-8.
// Without these deleted overloads, such calls may bind to the json overload via implicit conversion.
CICI_FORCE_INLINE const char8_t *to_u8(const char8_t *) = delete;
CICI_FORCE_INLINE std::u8string to_u8(const std::u8string&) = delete;
CICI_FORCE_INLINE std::u8string to_u8(std::u8string_view) = delete;

CICI_FORCE_INLINE std::u8string to_u8(const std::string& s) {
    return std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

CICI_FORCE_INLINE std::u8string to_u8(int _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(unsigned int _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(long _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(unsigned long _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(long long _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(unsigned long long _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(double _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(float _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(long double _Val) {
	return to_u8(std::to_string(_Val));
}

CICI_FORCE_INLINE std::u8string to_u8(const nlohmann::json& obj) {
	if (!obj.is_string()) {
		assert(false);
        return u8"";
	}

	return to_u8(obj.get<std::string>());
}

CICI_FORCE_INLINE std::u8string to_u8(const nlohmann::json& obj, const char* key) {
	if (!key || !obj.contains(key)) {
		return u8"";
	}
	return to_u8(obj[key]);
}


#endif  //__cplusplus

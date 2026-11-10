// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#ifndef CICI_STACK_STRING_H_
#define CICI_STACK_STRING_H_

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#define CICI_STACK_STRING_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define CICI_STACK_STRING_NOINLINE __attribute__((noinline))
#else
#define CICI_STACK_STRING_NOINLINE
#endif

namespace cici::detail {

constexpr std::uint32_t stackStringSeed(std::uint32_t line,
										std::uint32_t counter) noexcept {
	return 0x9E3779B9u ^ (line * 0x85EBCA6Bu) ^ (counter * 0xC2B2AE35u);
}

constexpr std::uint8_t stackStringKey(std::size_t index, std::uint32_t seed) noexcept {
	auto value = seed + static_cast<std::uint32_t>(index) * 0x9E3779B9u;
	value ^= value >> 16;
	value *= 0x7FEB352Du;
	value ^= value >> 15;
	return static_cast<std::uint8_t>(value ^ 0xA5u);
}

template<std::size_t Size, typename CharT = char>
class StackString final {
public:
	CICI_STACK_STRING_NOINLINE
	StackString(const std::array<std::uint8_t, Size> &encrypted,
				std::uint32_t seed) noexcept {
		static_assert(sizeof(CharT) == 1);
		for (std::size_t i = 0; i < Size; ++i) {
			buffer_[i] = static_cast<CharT>(encrypted[i] ^ stackStringKey(i, seed));
		}
		buffer_[Size] = CharT{};
	}

	~StackString() noexcept {
		volatile CharT *current = buffer_;
		for (std::size_t i = 0; i < Size + 1; ++i) {
			current[i] = CharT{};
		}
	}

	StackString(const StackString &) = delete;
	StackString &operator=(const StackString &) = delete;
	StackString(StackString &&) = delete;
	StackString &operator=(StackString &&) = delete;

	const CharT *c_str() const noexcept { return buffer_; }
	static constexpr std::size_t size() noexcept { return Size; }

private:
	CharT buffer_[Size + 1] = {};
};

template<std::size_t Size, typename CharT = char>
class StackStringCipher final {
public:
	consteval StackStringCipher(const CharT (&plain)[Size + 1],
							std::uint32_t seed) noexcept
		: seed_(seed) {
		static_assert(sizeof(CharT) == 1);
		for (std::size_t i = 0; i < Size; ++i) {
			encrypted_[i] = static_cast<std::uint8_t>(plain[i]) ^
				stackStringKey(i, seed_);
		}
	}

	CICI_STACK_STRING_NOINLINE StackString<Size, CharT> decrypt() const noexcept {
		return StackString<Size, CharT>(encrypted_, seed_);
	}

private:
	std::array<std::uint8_t, Size> encrypted_{};
	std::uint32_t seed_ = 0;
};

template<typename CharT, std::size_t N>
consteval auto makeStackStringCipher(const CharT (&plain)[N],
									 std::uint32_t seed) noexcept {
	static_assert(N > 0);
	return StackStringCipher<N - 1, CharT>(plain, seed);
}

} // namespace cici::detail

#define CICI_STACK_STR_IMPL(char_type, literal, line, counter)                         \
	([] {                                                                               \
		constexpr auto seed = ::cici::detail::stackStringSeed(line, counter);             \
		constexpr auto cipher =                                                          \
			::cici::detail::makeStackStringCipher<char_type>(literal, seed);               \
		return cipher.decrypt();                                                          \
	}())

#define CICI_STACK_STR(literal)                                                        \
	CICI_STACK_STR_IMPL(char, literal, __LINE__, __COUNTER__)
#define CICI_STACK_U8STR(literal)                                                      \
	CICI_STACK_STR_IMPL(char8_t, literal, __LINE__, __COUNTER__)

#undef CICI_STACK_STRING_NOINLINE

#endif // CICI_STACK_STRING_H_

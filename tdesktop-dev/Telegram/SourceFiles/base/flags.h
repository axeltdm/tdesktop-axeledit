/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <type_traits>

namespace base {

template <typename EnumType>
class flags;

template <typename ExtendedEnum>
struct extended_flags;

template <typename ExtendedEnum>
using extended_flags_t = typename extended_flags<ExtendedEnum>::type;

namespace details {

struct flags_zero_helper_struct {
};

using flags_zero_helper = void(base::details::flags_zero_helper_struct::*)();

template <typename ExtendedEnum,
	typename Enum = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto extended_flag_convert(ExtendedEnum value) {
	return static_cast<Enum>(value);
}

template <typename ExtendedEnum,
	typename Enum = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto extended_flags_convert(ExtendedEnum value) {
	return flags<Enum>(extended_flag_convert(value));
}

} // namespace details

template <typename EnumType>
class flags {
public:
	using Enum = EnumType;
	using Type = std::underlying_type_t<Enum>;

	constexpr flags() = default;
	constexpr flags(details::flags_zero_helper) noexcept {
	}
	constexpr flags(Enum value) noexcept
	: _value(static_cast<Type>(value)) {
	}
	static constexpr flags from_raw(Type value) noexcept {
		return flags(static_cast<Enum>(value));
	}

	constexpr auto value() const noexcept {
		return _value;
	}
	constexpr operator Type() const noexcept {
		return value();
	}

	constexpr auto &operator|=(flags b) noexcept {
		_value |= b.value();
		return *this;
	}
	constexpr auto &operator&=(flags b) noexcept {
		_value &= b.value();
		return *this;
	}
	constexpr auto &operator^=(flags b) noexcept {
		_value ^= b.value();
		return *this;
	}

	constexpr auto operator~() const noexcept {
		return from_raw(~value());
	}

	constexpr auto operator|(flags b) const noexcept {
		return (flags(*this) |= b);
	}
	constexpr auto operator&(flags b) const noexcept {
		return (flags(*this) &= b);
	}
	constexpr auto operator^(flags b) const noexcept {
		return (flags(*this) ^= b);
	}

	constexpr auto operator|(Enum b) const noexcept {
		return (flags(*this) |= b);
	}
	constexpr auto operator&(Enum b) const noexcept {
		return (flags(*this) &= b);
	}
	constexpr auto operator^(Enum b) const noexcept {
		return (flags(*this) ^= b);
	}

	constexpr auto operator==(Enum b) const noexcept {
		return (value() == static_cast<Type>(b));
	}
	constexpr auto operator!=(Enum b) const noexcept {
		return !(*this == b);
	}
	constexpr auto operator<(Enum b) const noexcept {
		return value() < static_cast<Type>(b);
	}
	constexpr auto operator>(Enum b) const noexcept {
		return (b < *this);
	}
	constexpr auto operator<=(Enum b) const noexcept {
		return !(b < *this);
	}
	constexpr auto operator>=(Enum b) const noexcept {
		return !(*this < b);
	}

private:
	Type _value = 0;

};

template <typename Enum>
constexpr auto make_flags(Enum value) noexcept {
	return flags<Enum>(value);
}

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator|(Enum a, flags<Enum> b) noexcept {
	return b | a;
}

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator&(Enum a, flags<Enum> b) noexcept {
	return b & a;
}

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator^(Enum a, flags<Enum> b) noexcept {
	return b ^ a;
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator|(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return a | details::extended_flags_convert(b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator|(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return b | a;
}

template <typename ExtendedEnum,
	typename = extended_flags_t<ExtendedEnum>>
inline constexpr auto operator&(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return a & details::extended_flags_convert(b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator&(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return b & a;
}

template <typename ExtendedEnum,
	typename = extended_flags_t<ExtendedEnum>>
inline constexpr auto operator^(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return a ^ details::extended_flags_convert(b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator^(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return b ^ a;
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto &operator&=(flags<extended_flags_t<ExtendedEnum>> &a, ExtendedEnum b) {
	return (a &= details::extended_flags_convert(b));
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto &operator|=(flags<extended_flags_t<ExtendedEnum>> &a, ExtendedEnum b) {
	return (a |= details::extended_flags_convert(b));
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto &operator^=(flags<extended_flags_t<ExtendedEnum>> &a, ExtendedEnum b) {
	return (a ^= details::extended_flags_convert(b));
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator==(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return a == details::extended_flags_convert(b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator==(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return (b == a);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator!=(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return !(a == b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator!=(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return !(a == b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator<(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return a < details::extended_flags_convert(b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator<(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return details::extended_flags_convert(a) < b;
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator>(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return (b < a);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator>(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return (b < a);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator<=(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return !(b < a);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator<=(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return !(b < a);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator>=(flags<extended_flags_t<ExtendedEnum>> a, ExtendedEnum b) {
	return !(a < b);
}

template <typename ExtendedEnum,
	typename = typename extended_flags<ExtendedEnum>::type>
inline constexpr auto operator>=(ExtendedEnum a, flags<extended_flags_t<ExtendedEnum>> b) {
	return !(a < b);
}

} // namespace base

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator!(Enum a) noexcept {
	return !base::make_flags(a);
}

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator~(Enum a) noexcept {
	return ~base::make_flags(a);
}

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator|(Enum a, Enum b) noexcept {
	return base::make_flags(a) | b;
}

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator|(Enum a, base::details::flags_zero_helper) noexcept {
	return base::make_flags(a);
}

template <typename Enum,
	typename = std::enable_if_t<std::is_enum<Enum>::value>,
	typename = std::enable_if_t<is_flag_type(Enum{})>>
inline constexpr auto operator|(base::details::flags_zero_helper, Enum b) noexcept {
	return base::make_flags(b);
}

template <typename ExtendedEnum,
	typename = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto operator|(ExtendedEnum a, ExtendedEnum b) {
	return base::details::extended_flags_convert(a) | b;
}

template <typename ExtendedEnum,
	typename = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto operator|(ExtendedEnum a, typename base::extended_flags<ExtendedEnum>::type b) {
	return base::details::extended_flags_convert(a) | b;
}

template <typename ExtendedEnum,
	typename = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto operator|(typename base::extended_flags<ExtendedEnum>::type a, ExtendedEnum b) {
	return b | a;
}

template <typename ExtendedEnum,
	typename = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto operator|(base::details::flags_zero_helper, ExtendedEnum b) {
	return 0 | base::details::extended_flag_convert(b);
}

template <typename ExtendedEnum,
	typename = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto operator|(ExtendedEnum a, base::details::flags_zero_helper) {
	return base::details::extended_flag_convert(a) | 0;
}

template <typename ExtendedEnum,
	typename = typename base::extended_flags<ExtendedEnum>::type>
inline constexpr auto operator~(ExtendedEnum b) {
	return ~base::details::extended_flags_convert(b);
}

#ifndef UTIL_LOG_H
#define UTIL_LOG_H

#include "../log.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <fmt/core.h>

#include <gxm/types.h>

#define LOG_TRACE(...) LOGSTR(fmt::format(__VA_ARGS__).c_str())
template <typename... Args>
auto LOG_DEBUG(std::string_view fmt, Args &&...args)
{
	return fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
}
#define LOG_INFO(...) LOGSTR(fmt::format(__VA_ARGS__).c_str())
#define LOG_WARN(...) LOGSTR(fmt::format(__VA_ARGS__).c_str())
#define LOG_ERROR(...) LOGSTR(fmt::format(__VA_ARGS__).c_str())
#define LOG_CRITICAL(...) LOGSTR(fmt::format(__VA_ARGS__).c_str())

template <typename T>
std::string log_hex(T val) {
    using unsigned_type = typename std::make_unsigned<T>::type;
    return fmt::format("0x{:0X}", static_cast<unsigned_type>(val));
}

template <typename T>
std::string log_hex_full(T val)
{
	std::stringstream ss;
	ss << "0x";
	ss << std::setfill('0') << std::setw(sizeof(T) * 2) << std::hex << val;
	return ss.str();
}

template <> struct fmt::formatter<SceGxmAttributeFormat>: formatter<uint32_t> {
  auto format(SceGxmAttributeFormat fmt, format_context& ctx) const {
	  return formatter<uint32_t>::format((uint32_t)fmt, ctx);
  }
};

#endif

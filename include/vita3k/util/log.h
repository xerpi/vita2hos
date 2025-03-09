#ifndef UTIL_LOG_H
#define UTIL_LOG_H

#include <fmt/core.h>
#include <fmt/format.h>
#include <gxm/types.h>
#include <iomanip>
#include <sstream>
#include <string>

#include "../log.h"

#undef LOG_TRACE
template <typename... Args> void LOG_TRACE(std::string_view fmt, Args &&...args)
{
    log_print(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...).c_str());
}

#undef LOG_DEBUG
template <typename... Args> void LOG_DEBUG(std::string_view fmt, Args &&...args)
{
    log_print(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...).c_str());
}

#undef LOG_INFO
template <typename... Args> void LOG_INFO(std::string_view fmt, Args &&...args)
{
    log_print(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...).c_str());
}

#undef LOG_WARN
template <typename... Args> void LOG_WARN(std::string_view fmt, Args &&...args)
{
    log_print(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...).c_str());
}

#undef LOG_ERROR
template <typename... Args> void LOG_ERROR(std::string_view fmt, Args &&...args)
{
    log_print(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...).c_str());
}

#undef LOG_CRITICAL
template <typename... Args> void LOG_CRITICAL(std::string_view fmt, Args &&...args)
{
    log_print(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...).c_str());
}

template <typename T> std::string log_hex(T val)
{
    using unsigned_type = typename std::make_unsigned<T>::type;
    return fmt::format("0x{:0X}", static_cast<unsigned_type>(val));
}

template <typename T> std::string log_hex_full(T val)
{
    std::stringstream ss;
    ss << "0x";
    ss << std::setfill('0') << std::setw(sizeof(T) * 2) << std::hex << val;
    return ss.str();
}

template <> struct fmt::formatter<SceGxmAttributeFormat> : formatter<uint32_t> {
    auto format(SceGxmAttributeFormat fmt, format_context &ctx) const
    {
        return formatter<uint32_t>::format((uint32_t)fmt, ctx);
    }
};

#endif

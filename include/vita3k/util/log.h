#ifndef UTIL_LOG_H
#define UTIL_LOG_H

#include "../log.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <fmt/core.h>

#define LOG_TRACE(...) LOGSTR(fmt::format(__VA_ARGS__).c_str())
#define LOG_DEBUG(...) LOGSTR(fmt::format(__VA_ARGS__).c_str())
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
std::string log_hex_full(T val) {
    using unsigned_type = typename std::make_unsigned<T>::type;
    std::stringstream ss;
    ss << "0x";
    ss << std::setfill('0') << std::setw(sizeof(T) * 2) << std::hex << val;
    return ss.str();
}

#endif

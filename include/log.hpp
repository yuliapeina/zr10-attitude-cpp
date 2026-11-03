// log.hpp — рядки журналу у форматі Python logging, як у api/r2mav.py:
//   08:15:42.123 r2mav DEBUG: first HEARTBEAT from 1/1
// Пишуться у stderr (так само, як у Python-версії).
#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace zr10 {

inline void log_line(const char* who, const char* level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

inline void log_line(const char* who, const char* level, const char* fmt, ...) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const int ms = static_cast<int>(
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::fprintf(stderr, "%02d:%02d:%02d.%03d %s %s: ", tm.tm_hour, tm.tm_min, tm.tm_sec,
                 ms, who, level);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

}  // namespace zr10

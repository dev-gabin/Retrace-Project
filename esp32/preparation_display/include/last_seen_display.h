#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

constexpr unsigned LAST_SEEN_LIMIT = 3;
struct LastSeenReading {
    char item[33] = {};
    uint64_t utcMicros = 0;
};

// Server emits UTC ISO timestamps, including fractional seconds and trailing Z.
// Reject invalid dates instead of presenting a fabricated observation time.
inline bool parseSeenUtc(const char *text, uint64_t &micros) {
    if (!text) return false;
    int year, month, day, hour, minute, second, used = 0;
    if (sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day,
               &hour, &minute, &second, &used) != 6 || used != 19) return false;
    if (year < 1970 || year > 2099 || month < 1 || month > 12 || day < 1
        || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > monthDays[month - 1] + (month == 2 && leap ? 1 : 0)) return false;
    const char *tail = text + used;
    uint32_t fraction = 0;
    if (*tail == '.') {
        ++tail;
        unsigned digits = 0;
        while (*tail >= '0' && *tail <= '9') {
            if (digits < 6) fraction = fraction * 10 + (*tail - '0');
            ++digits; ++tail;
        }
        if (!digits || digits > 6) return false;
        while (digits++ < 6) fraction *= 10;
    }
    if (strcmp(tail, "Z") && strcmp(tail, "+00:00")) return false;
    uint64_t days = 0;
    for (int y = 1970; y < year; ++y)
        days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 1 : 0);
    for (int m = 1; m < month; ++m) days += monthDays[m - 1] + (m == 2 && leap ? 1 : 0);
    days += day - 1;
    micros = ((days * 24 + hour) * 3600 + minute * 60 + second) * 1000000 + fraction;
    return true;
}

inline void insertLastSeen(LastSeenReading (&rows)[LAST_SEEN_LIMIT], unsigned &count,
                           const LastSeenReading &reading) {
    unsigned index = 0;
    while (index < count && (rows[index].utcMicros > reading.utcMicros
           || (rows[index].utcMicros == reading.utcMicros && strcmp(rows[index].item, reading.item) < 0))) ++index;
    if (index >= LAST_SEEN_LIMIT) return;
    unsigned nextCount = count < LAST_SEEN_LIMIT ? count + 1 : LAST_SEEN_LIMIT;
    for (unsigned i = nextCount - 1; i > index; --i) rows[i] = rows[i - 1];
    rows[index] = reading;
    count = nextCount;
}

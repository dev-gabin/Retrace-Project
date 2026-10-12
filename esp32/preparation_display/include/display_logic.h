#pragma once
#include <stdint.h>
#include <stddef.h>
inline bool morningHour(int hour, int start, int end) {
    return hour >= start && hour < end;
}
inline bool rainExpected(int probability, float precipitation, int threshold) {
    return probability >= threshold || precipitation > 0.0f;
}
inline bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval) {
    return uint32_t(now - previous) >= interval;
}

// Hold a newly observed record, then resume the next normal page.
inline bool advanceDisplayPage(uint32_t now, bool &recordUpdated, uint32_t updatedAt,
                               size_t &page, uint32_t &pageAt, size_t total,
                               uint32_t pageInterval, uint32_t holdInterval) {
    if (!total) return false;
    if (recordUpdated) {
        if (!intervalElapsed(now, updatedAt, holdInterval)) {
            page = 0;
            return true;
        }
        recordUpdated = false;
        page = total > 1 ? 1 : 0;
        pageAt = now;
    } else if (intervalElapsed(now, pageAt, pageInterval)) {
        pageAt = now;
        page = (page + 1) % total;
    }
    page %= total;
    return false;
}

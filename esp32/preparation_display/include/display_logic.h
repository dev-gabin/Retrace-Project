#pragma once
#include <stdint.h>
inline bool morningHour(int hour, int start, int end) {
    return hour >= start && hour < end;
}
inline bool rainExpected(int probability, float precipitation, int threshold) {
    return probability >= threshold || precipitation > 0.0f;
}
inline bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval) {
    return uint32_t(now - previous) >= interval;
}

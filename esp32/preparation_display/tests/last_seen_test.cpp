#include "../include/last_seen_display.h"
#include "../include/display_logic.h"
#include <assert.h>
#include <time.h>

int main() {
    uint64_t epoch = 0, value = 0;
    assert(parseSeenUtc("1970-01-01T00:00:00Z", epoch) && epoch == 0);
    assert(parseSeenUtc("2026-10-11T23:42:00.123456Z", epoch));
    assert(parseSeenUtc("2026-10-11T23:42:00.123456+00:00", value) && value == epoch);
    assert(parseSeenUtc("2026-10-11T23:42:00.1Z", value) && value % 1000000 == 100000);
    time_t korea = epoch / 1000000 + 9 * 3600;
    struct tm observed {};
#ifdef _WIN32
    gmtime_s(&observed, &korea);
#else
    gmtime_r(&korea, &observed);
#endif
    assert(observed.tm_mon == 9 && observed.tm_mday == 12 && observed.tm_hour == 8 && observed.tm_min == 42);
    assert(!parseSeenUtc(nullptr, value));
    assert(!parseSeenUtc("2026-02-30T00:00:00Z", value));
    assert(!parseSeenUtc("2026-02-29T00:00:00Z", value));
    assert(parseSeenUtc("2024-02-29T00:00:00Z", value));
    assert(!parseSeenUtc("2026-10-12T24:00:00Z", value));
    assert(!parseSeenUtc("2026-10-12T07:00:00", value));
    assert(!parseSeenUtc("2026-10-12T07:00:00+09:00", value));
    assert(!parseSeenUtc("2026-10-12T07:00:00.Z", value));
    assert(!parseSeenUtc("2026-10-12T07:00:00Zjunk", value));
    LastSeenReading rows[LAST_SEEN_LIMIT]; unsigned count = 0;
    LastSeenReading reading;
    strcpy(reading.item, "wallet"); reading.utcMicros = 300; insertLastSeen(rows, count, reading);
    strcpy(reading.item, "book"); reading.utcMicros = 100; insertLastSeen(rows, count, reading);
    strcpy(reading.item, "phone"); reading.utcMicros = 400; insertLastSeen(rows, count, reading);
    strcpy(reading.item, "keys"); reading.utcMicros = 200; insertLastSeen(rows, count, reading);
    assert(count == 3 && !strcmp(rows[0].item, "phone") && !strcmp(rows[1].item, "wallet") && !strcmp(rows[2].item, "keys"));
    strcpy(reading.item, "airpods"); reading.utcMicros = 300; insertLastSeen(rows, count, reading);
    assert(count == 3 && !strcmp(rows[1].item, "airpods") && !strcmp(rows[2].item, "wallet"));
    assert(!morningHour(6, 7, 8) && morningHour(7, 7, 8) && !morningHour(8, 7, 8));
    assert(intervalElapsed(4, UINT32_MAX - 5, 10));
    puts("PASS: UTC/KST rollover, timestamp validation, newest 3 sorting, schedule and millis rollover");
}

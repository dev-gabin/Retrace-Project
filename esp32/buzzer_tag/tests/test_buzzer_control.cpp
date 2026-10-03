#include "buzzer_control.h"

#include <cstdio>

static unsigned checks = 0;
static unsigned failures = 0;

static void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", description);
    }
}

int main() {
    BuzzerControl buzzer;
    expect(!buzzer.is_on(), "boot is OFF");
    expect(buzzer.receive("1", 1), "ASCII 1 accepted");
    expect(buzzer.is_on(), "ASCII 1 turns ON");
    expect(buzzer.receive("1", 1), "repeated ASCII 1 accepted");
    expect(buzzer.is_on(), "repeated ASCII 1 preserves ON");
    expect(buzzer.receive("0", 1), "ASCII 0 accepted");
    expect(!buzzer.is_on(), "ASCII 0 immediately turns OFF");
    expect(buzzer.receive("0", 1), "OFF while already OFF accepted");
    expect(!buzzer.is_on(), "repeated OFF preserves OFF");

    struct InvalidWrite {
        const char* data;
        std::size_t length;
    };
    const InvalidWrite invalid[] = {
        {nullptr, 0}, {nullptr, 1}, {"", 0}, {"\x01", 1}, {"\x00", 1},
        {"2", 1}, {" ", 1}, {"1\n", 2}, {"0\r", 2}, {"10", 2},
        {"01", 2}, {"1\0", 2}, {"on", 2}, {"11111111111111111111111111111111", 32}
    };
    for (const auto& write : invalid) {
        BuzzerControl active;
        active.receive("1", 1);
        expect(!active.receive(write.data, write.length), "invalid write rejected");
        expect(active.is_on(), "invalid write preserves ON");
        BuzzerControl idle;
        expect(!idle.receive(write.data, write.length), "invalid write rejected while OFF");
        expect(!idle.is_on(), "invalid write preserves OFF");
    }

    buzzer.receive("1", 1);
    buzzer.stop(); // BLE disconnect callback uses this same operation.
    expect(!buzzer.is_on(), "disconnect stop turns OFF");
    expect(buzzer.receive("1", 1), "new command after reconnect works");
    expect(buzzer.is_on(), "new session turns ON");
    expect(buzzer.receive("0", 1), "web stop command after reconnect accepted");
    expect(!buzzer.is_on(), "web stop command after reconnect turns OFF");

    std::printf("Buzzer control: %u/%u checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

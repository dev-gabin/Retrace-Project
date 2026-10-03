#include "entrance_control.h"
#include <cstdio>
#include <limits>

static unsigned checks = 0, failures = 0;
static void expect(bool result, const char* description) {
    ++checks;
    if (!result) {
        ++failures;
        std::printf("FAIL: %s\n", description);
    }
}

int main() {
    LightCommand command = LightCommand::Alert;
    expect(parse_light_command("NORMAL", 6, command), "NORMAL accepted");
    expect(command == LightCommand::Normal, "NORMAL parsed");
    const char alert[] = {'A', 'L', 'E', 'R', 'T'};
    expect(parse_light_command(alert, sizeof(alert), command), "non-terminated ALERT accepted");
    expect(command == LightCommand::Alert, "ALERT parsed");
    struct Invalid { const char* data; std::size_t length; };
    const Invalid invalid[] = {
        {nullptr, 0}, {nullptr, 5}, {"", 0}, {"alert", 5}, {"normal", 6},
        {"ALERT\n", 6}, {"NORMAL\r", 7}, {" ALERT", 6}, {"ALERT ", 6},
        {"ALERT\0", 6}, {"AL\0RT", 5}, {"ON", 2}, {"ALER", 4}, {"NORMALX", 7}
    };
    for (const auto& value : invalid) {
        command = LightCommand::Normal;
        expect(!parse_light_command(value.data, value.length, command), "invalid command rejected");
        expect(command == LightCommand::Normal, "invalid command preserves output argument");
    }

    EntranceControl light(10000, 10000, 250, 50);
    expect(!light.light_on(0), "boot OFF");
    expect(!light.sample(false, 0), "initial LOW no event");
    expect(!light.sample(true, 100), "rising begins filter");
    expect(!light.sample(true, 149), "49ms not stable yet");
    expect(!light.light_on(149), "filter keeps light OFF");
    expect(light.sample(true, 150), "50ms stable HIGH emits motion");
    expect(light.light_on(150), "stable motion turns ON");
    expect(!light.sample(true, 151), "held HIGH emits once");
    expect(!light.sample(true, 20000), "held HIGH after long interval no second event");
    expect(light.light_on(20000), "held HIGH maintains light");
    expect(!light.sample(false, 20001), "falling begins filter");
    expect(!light.sample(false, 20050), "falling boundary minus one");
    expect(!light.sample(false, 20051), "stable LOW emits no motion");
    light.sample(false, 30050);
    expect(light.light_on(30050), "hold timeout boundary minus one");
    light.sample(false, 30051);
    expect(!light.light_on(30051), "hold timeout boundary");
    light.sample(false, std::numeric_limits<uint32_t>::max());
    light.sample(false, 0);
    expect(!light.light_on(0), "expired normal light cannot reactivate after counter wraps");

    EntranceControl bounce(10000, 10000, 250, 50);
    expect(!bounce.sample(true, 0), "short pulse begins");
    expect(!bounce.sample(false, 49), "short pulse cancelled");
    expect(!bounce.sample(false, 100), "short pulse emits no motion");
    expect(!bounce.light_on(100), "short pulse leaves OFF");
    bounce.sample(true, 200);
    expect(bounce.sample(true, 250), "first stable press detected");
    bounce.sample(false, 300);
    bounce.sample(true, 349);
    expect(!bounce.sample(true, 399), "short LOW does not rearm motion");
    bounce.sample(false, 400);
    bounce.sample(false, 450);
    bounce.sample(true, 500);
    expect(bounce.sample(true, 550), "stable LOW rearms next motion");

    EntranceControl warning(10000, 10000, 250, 50);
    warning.command(LightCommand::Alert, 100);
    expect(warning.is_alert(), "ALERT mode entered");
    expect(warning.light_on(100), "ALERT starts ON");
    expect(warning.light_on(349), "blink ON boundary minus one");
    expect(!warning.light_on(350), "blink switches OFF at 250ms");
    expect(!warning.light_on(599), "blink OFF boundary minus one");
    expect(warning.light_on(600), "blink switches ON at 500ms");
    warning.sample(false, 10099);
    expect(warning.is_alert(), "ALERT timeout boundary minus one");
    warning.sample(false, 10100);
    expect(!warning.is_alert(), "ALERT timeout returns NORMAL");
    expect(!warning.light_on(10100), "NORMAL without motion is OFF");

    warning.command(LightCommand::Alert, 20000);
    warning.command(LightCommand::Alert, 29999);
    warning.sample(false, 30000);
    expect(warning.is_alert(), "repeated ALERT refreshes duration");
    expect(warning.light_on(30000), "repeated ALERT restarts ON phase");
    warning.sample(false, 39999);
    expect(!warning.is_alert(), "refreshed ALERT expires");

    warning.command(LightCommand::Alert, 40000);
    warning.command(LightCommand::Normal, 40001);
    expect(!warning.is_alert(), "NORMAL cancels ALERT immediately");
    expect(!warning.light_on(40001), "NORMAL without motion remains OFF");
    warning.sample(true, 41000);
    expect(warning.sample(true, 41050), "motion while normal detected");
    warning.command(LightCommand::Alert, 42000);
    warning.sample(true, 42500);
    warning.command(LightCommand::Normal, 42501);
    expect(warning.light_on(42501), "NORMAL keeps existing PIR light ON");
    warning.command(LightCommand::Alert, 43000);
    warning.sample(true, 53000);
    expect(!warning.is_alert(), "ALERT expires while PIR held HIGH");
    expect(warning.light_on(53000), "PIR lighting survives ALERT expiry");

    EntranceControl wrapped(10000, 10000, 250, 50);
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 4999U;
    wrapped.command(LightCommand::Alert, start);
    wrapped.sample(false, 4999);
    expect(wrapped.is_alert(), "wrap ALERT boundary minus one");
    wrapped.sample(false, 5000);
    expect(!wrapped.is_alert(), "wrap ALERT boundary");
    wrapped.sample(false, start);
    wrapped.sample(true, start + 1);
    expect(wrapped.sample(true, start + 51), "wrap motion detected");
    wrapped.sample(false, start + 52);
    wrapped.sample(false, start + 102);
    wrapped.sample(false, 5101);
    expect(wrapped.light_on(5101), "wrap normal timeout boundary minus one");
    wrapped.sample(false, 5102);
    expect(!wrapped.light_on(5102), "wrap normal timeout boundary");

    EntranceControl filter_wrap(10000, 10000, 250, 50);
    filter_wrap.sample(true, std::numeric_limits<uint32_t>::max() - 24U);
    expect(!filter_wrap.sample(true, 24), "wrap debounce boundary minus one");
    expect(filter_wrap.sample(true, 25), "wrap debounce boundary");

    std::printf("Entrance control: %u/%u checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

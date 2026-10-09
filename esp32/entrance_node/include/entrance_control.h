#ifndef ENTRANCE_CONTROL_H
#define ENTRANCE_CONTROL_H

#include <cstddef>
#include <cstdint>
#include <cstring>

enum class LightCommand { Normal, Alert };

inline bool parse_light_command(const char* data, std::size_t length, LightCommand& command) {
    if (data == nullptr) return false;
    if (length == 6 && std::memcmp(data, "NORMAL", 6) == 0) {
        command = LightCommand::Normal;
        return true;
    }
    if (length == 5 && std::memcmp(data, "ALERT", 5) == 0) {
        command = LightCommand::Alert;
        return true;
    }
    return false;
}

// HAL/Arduino에 의존하지 않는 PIR 필터와 조명 상태. 메인 루프만 접근한다.
class EntranceControl {
public:
    EntranceControl(uint32_t hold_ms, uint32_t alert_ms, uint32_t blink_ms, uint32_t debounce_ms)
        : hold_ms_(hold_ms), alert_ms_(alert_ms), blink_ms_(blink_ms), debounce_ms_(debounce_ms) {}

    void command(LightCommand command, uint32_t now_ms) {
        alert_active_ = command == LightCommand::Alert;
        if (alert_active_) alert_since_ = now_ms;
    }

    // 안정적인 LOW -> HIGH 전환마다 한 번 true를 반환한다.
    bool sample(bool raw_pir, uint32_t now_ms) {
        if (raw_pir != candidate_) {
            candidate_ = raw_pir;
            candidate_since_ = now_ms;
        }
        bool motion = false;
        if (pir_ != candidate_ && elapsed(now_ms, candidate_since_) >= debounce_ms_) {
            pir_ = candidate_;
            if (pir_) motion = true;
            else if (normal_active_) normal_since_ = now_ms;
        }
        if (pir_) {
            normal_active_ = true;
            normal_since_ = now_ms;
        } else if (normal_active_ && elapsed(now_ms, normal_since_) >= hold_ms_) {
            normal_active_ = false;
        }
        if (alert_active_ && elapsed(now_ms, alert_since_) >= alert_ms_) alert_active_ = false;
        return motion;
    }

    bool light_on(uint32_t now_ms) const {
        if (alert_active_) {
            return blink_ms_ == 0 || (elapsed(now_ms, alert_since_) / blink_ms_) % 2 == 0;
        }
        return normal_active_;
    }
    bool is_alert() const { return alert_active_; }

private:
    static uint32_t elapsed(uint32_t now, uint32_t since) { return now - since; }
    const uint32_t hold_ms_, alert_ms_, blink_ms_, debounce_ms_;
    uint32_t candidate_since_ = 0, normal_since_ = 0, alert_since_ = 0;
    bool candidate_ = false, pir_ = false, normal_active_ = false, alert_active_ = false;
};

#endif

#ifndef BUZZER_CONTROL_H
#define BUZZER_CONTROL_H

#include <cstddef>

// Arduino/BLE와 분리된 명령 로직. 시간 제한 없이 유지하며 호출하는 쪽에서 동시 접근을 보호한다.
class BuzzerControl {
public:
    bool receive(const char* data, std::size_t length) {
        if (data == nullptr || length != 1 || (data[0] != '1' && data[0] != '0')) {
            return false;
        }
        on_ = data[0] == '1';
        return true;
    }

    void stop() { on_ = false; }
    bool is_on() const { return on_; }

private:
    bool on_ = false;
};

#endif

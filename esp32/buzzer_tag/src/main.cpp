#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "buzzer_control.h"

// Retrace - 부저 태그
// 메시지 형식: docs/protocol.md 5장 (BLE)
// 실물에서 조정할 값. HIGH로 켜지는 액티브 부저/구동 회로 기준이다.
static constexpr uint8_t BUZZER_PIN = 3;
static constexpr uint32_t ADVERTISING_RESTART_MS = 500;
static constexpr char DEVICE_NAME[] = "RETRACE-TAG-01";
static constexpr char SERVICE_UUID[] = "4951013c-1654-43f1-919f-a3ff928dc0b6";
static constexpr char CHARACTERISTIC_UUID[] = "36b342d0-153b-418e-b5d7-f95af507ec1f";

static BuzzerControl buzzer;
static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool connected = false;
static bool advertising_pending = false;
static uint32_t disconnected_ms = 0;

// GPIO3의 digitalWrite는 짧은 GPIO 출력 변경이다. 상태와 출력을 같은 lock 안에서 갱신한다.
static void apply_buzzer_output() {
    digitalWrite(BUZZER_PIN, buzzer.is_on() ? HIGH : LOW);
}

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        portENTER_CRITICAL(&state_mux);
        connected = true;
        advertising_pending = false;
        buzzer.stop();
        apply_buzzer_output();
        portEXIT_CRITICAL(&state_mux);
        Serial.println("BLE connected; buzzer OFF");
    }

    void onDisconnect(BLEServer*) override {
        portENTER_CRITICAL(&state_mux);
        connected = false;
        buzzer.stop();
        apply_buzzer_output();
        disconnected_ms = millis();
        advertising_pending = true;
        portEXIT_CRITICAL(&state_mux);
        Serial.println("BLE disconnected; buzzer OFF");
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic, esp_ble_gatts_cb_param_t*) override {
        const std::string value = characteristic->getValue();
        portENTER_CRITICAL(&state_mux);
        const bool accepted = connected && buzzer.receive(value.data(), value.size());
        if (accepted) {
            apply_buzzer_output();
        }
        portEXIT_CRITICAL(&state_mux);

        if (accepted) {
            Serial.println(value[0] == '1' ? "BUZZER ON" : "BUZZER OFF");
        } else {
            Serial.println("Ignored BLE write: expected one ASCII 1 or 0");
        }
    }
};

static ServerCallbacks server_callbacks;
static CommandCallbacks command_callbacks;

void setup() {
    // BLE 초기화 전에 OFF. USB 시리얼 연결을 기다리지 않으므로 단독 전원에서도 동작한다.
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(BUZZER_PIN, OUTPUT);
    Serial.begin(115200);
    Serial.println("buzzer_tag start");

    BLEDevice::init(DEVICE_NAME);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(&server_callbacks);
    BLEService* service = server->createService(SERVICE_UUID);
    BLECharacteristic* command = service->createCharacteristic(
        CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
    command->setCallbacks(&command_callbacks);
    service->start();

    // 128비트 UUID와 전체 이름을 각각 광고/스캔 응답에 넣어 31바이트 한도 안에 둔다.
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    BLEAdvertisementData advertisement;
    advertisement.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    advertisement.setCompleteServices(BLEUUID(SERVICE_UUID));
    advertising->setAdvertisementData(advertisement);
    BLEAdvertisementData scan_response;
    scan_response.setName(DEVICE_NAME);
    advertising->setScanResponseData(scan_response);
    advertising->setScanResponse(true);
    advertising->start();
    Serial.println("BLE advertising: RETRACE-TAG-01");
}

void loop() {
    portENTER_CRITICAL(&state_mux);
    // 광고 재시작 시간만 확인한다. 부저는 OFF 명령 또는 연결 해제까지 계속 켜진다.
    const uint32_t now = millis();
    const bool restart_advertising = advertising_pending && !connected &&
        static_cast<uint32_t>(now - disconnected_ms) >= ADVERTISING_RESTART_MS;
    if (restart_advertising) {
        advertising_pending = false;
    }
    portEXIT_CRITICAL(&state_mux);

    if (restart_advertising) {
        BLEDevice::startAdvertising();
        Serial.println("BLE advertising restarted");
    }
    delay(1); // 다른 FreeRTOS 태스크에 실행 시간을 양보한다.
}

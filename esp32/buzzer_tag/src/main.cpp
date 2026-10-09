#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// BLE 표시 이름과 명령의 clientID로 사용
#define CLIENT_ID "TOKEN"

constexpr uint8_t BUZZER_PIN = 3;
constexpr uint32_t BUZZER_FREQUENCY = 2500;
constexpr uint8_t PWM_RESOLUTION = 8;
constexpr uint8_t PWM_DUTY = 128;
constexpr uint32_t BUZZER_ON_MS = 1000;
constexpr uint32_t BUZZER_OFF_MS = 300;
constexpr uint8_t BUZZER_REPEAT_COUNT = 5;

constexpr char SERVICE_UUID[] =
    "4951013c-1654-43f1-919f-a3ff928dc0b6";
constexpr char CHARACTERISTIC_UUID[] =
    "36b342d0-153b-418e-b5d7-f95af507ec1f";

// 수신 문자열 끝의 CR/LF는 onWrite()에서 제거한다.
// The Jetson server routes SET@TOKEN:BUZZER as TOKEN:BUZZER to this client.
constexpr char BUZZER_COMMAND[] = CLIENT_ID ":BUZZER";
constexpr char OK_RESPONSE[] = "OK@" CLIENT_ID "\r\n";

BLECharacteristic *bleCharacteristic = nullptr;
bool alarmRunning = false;
bool buzzerActive = false;
uint8_t completedCount = 0;
uint32_t stateChangedAt = 0;

void buzzerOn()
{
    ledcWrite(BUZZER_PIN, PWM_DUTY);
    buzzerActive = true;
}

void buzzerOff()
{
    ledcWrite(BUZZER_PIN, 0);
    buzzerActive = false;
}

void startBuzzer()
{
    completedCount = 0;
    alarmRunning = true;
    stateChangedAt = millis();
    buzzerOn();
}

// 1초 ON, 0.3초 OFF 패턴을 총 5회 실행한다.
void updateBuzzer()
{
    if (!alarmRunning) {
        return;
    }

    const uint32_t now = millis();

    if (buzzerActive) {
        if (static_cast<uint32_t>(now - stateChangedAt) >= BUZZER_ON_MS) {
            buzzerOff();
            completedCount++;
            stateChangedAt = now;

            if (completedCount >= BUZZER_REPEAT_COUNT) {
                alarmRunning = false;
            }
        }
    } else if (static_cast<uint32_t>(now - stateChangedAt) >= BUZZER_OFF_MS) {
        buzzerOn();
        stateChangedAt = now;
    }
}

void sendBleResponse(const char *response)
{
    if (bleCharacteristic == nullptr) {
        return;
    }

    bleCharacteristic->setValue(response);
    bleCharacteristic->notify();
}

class ServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *server) override
    {
        (void)server;
    }

    void onDisconnect(BLEServer *server) override
    {
        (void)server;
        BLEDevice::startAdvertising();
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *characteristic) override
    {
        String command = characteristic->getValue();
        command.trim();

        if (command == BUZZER_COMMAND) {
            startBuzzer();
            sendBleResponse(OK_RESPONSE);
            return;
        }
    }
};

void setup()
{
    ledcAttach(BUZZER_PIN, BUZZER_FREQUENCY, PWM_RESOLUTION);
    buzzerOff();

    BLEDevice::init(CLIENT_ID);
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *service = server->createService(SERVICE_UUID);
    bleCharacteristic = service->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR |
            BLECharacteristic::PROPERTY_NOTIFY);

    bleCharacteristic->setCallbacks(new CommandCallbacks());
    bleCharacteristic->addDescriptor(new BLE2902());
    bleCharacteristic->setValue("");
    service->start();

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinInterval(1600);
    advertising->setMaxInterval(1920);
    advertising->start();
}

void loop()
{
    updateBuzzer();
    delay(10);
}

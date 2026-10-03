#include <Arduino.h>
#include <WiFi.h>
#include <mqtt_client.h>
#include <atomic>
#include "entrance_control.h"

#if defined(RETRACE_NETWORK_BUILD_CHECK)
#include "../tests/network_config.build_check.h"
#elif __has_include("network_config.h")
#include "network_config.h"
#else
#include "network_config.example.h"
#endif

// Retrace - 출입 노드 (스마트 현관등)
// 메시지 형식: docs/protocol.md 4장 (MQTT)
// 실물에서 조정할 값. HIGH로 켜지는 한 색 LED/구동 회로 기준이다.
static constexpr uint8_t PIR_PIN = 34;
static constexpr uint8_t LIGHT_PIN = 25;
static constexpr uint32_t LIGHT_HOLD_MS = 10000;
static constexpr uint32_t ALERT_HOLD_MS = 10000;
static constexpr uint32_t ALERT_BLINK_MS = 250;
static constexpr uint32_t PIR_DEBOUNCE_MS = 50;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t MOTION_MAX_AGE_MS = 5000;
static constexpr unsigned MQTT_OUTBOX_LIMIT = 2048;
static constexpr char MQTT_CLIENT_ID[] = "retrace-entrance";
static constexpr char MOTION_TOPIC[] = "retrace/entrance/motion";
static constexpr char LIGHT_TOPIC[] = "retrace/entrance/light";
static constexpr char STATUS_TOPIC[] = "retrace/entrance/status";

static EntranceControl light(LIGHT_HOLD_MS, ALERT_HOLD_MS, ALERT_BLINK_MS, PIR_DEBOUNCE_MS);
static QueueHandle_t command_queue = nullptr;
static QueueHandle_t motion_queue = nullptr;
static std::atomic<bool> mqtt_ready{false};
static int subscription_id = -1; // MQTT 이벤트 태스크 안에서만 갱신한다.

static void mqtt_event(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    auto* event = static_cast<esp_mqtt_event_t*>(event_data);
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        subscription_id = esp_mqtt_client_subscribe(event->client, LIGHT_TOPIC, 1);
        Serial.println(subscription_id >= 0 ? "MQTT connected; subscribing" : "MQTT subscribe failed");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (event->msg_id == subscription_id) {
            const int message_id = esp_mqtt_client_enqueue(event->client, STATUS_TOPIC,
                "online", 6, 1, 1, true);
            mqtt_ready.store(true);
            Serial.println(message_id >= 0 ? "MQTT ready; online queued" : "MQTT online enqueue failed");
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqtt_ready.store(false);
        subscription_id = -1;
        Serial.println("MQTT disconnected; local light continues");
        break;
    case MQTT_EVENT_DATA: {
        // NORMAL/ALERT만 처리. Retain·분할·여분의 문자를 포함한 값은 무시한다.
        if (event->retain || event->current_data_offset != 0 ||
            event->data_len != event->total_data_len || event->topic == nullptr ||
            event->topic_len != static_cast<int>(sizeof(LIGHT_TOPIC) - 1) ||
            std::memcmp(event->topic, LIGHT_TOPIC, sizeof(LIGHT_TOPIC) - 1) != 0) break;
        LightCommand command;
        if (event->data_len < 0 || !parse_light_command(event->data,
            static_cast<std::size_t>(event->data_len), command)) {
            Serial.println("Ignored MQTT command: expected NORMAL or ALERT");
        } else if (xQueueSend(command_queue, &command, 0) != pdTRUE) {
            Serial.println("MQTT command queue full; command ignored");
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        Serial.println("MQTT error; waiting for automatic reconnect");
        break;
    default:
        break;
    }
}

// Wi-Fi 재연결·MQTT 큐 처리는 별도 태스크. 네트워크 대기가 PIR/LED를 막지 않게 한다.
static void network_task(void*) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("Wi-Fi init failed; local light only");
        vTaskDelete(nullptr);
        return;
    }
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t wifi_since = millis();

    esp_mqtt_client_config_t config = {};
    config.host = MQTT_HOST;
    config.port = MQTT_PORT;
    config.transport = MQTT_TRANSPORT_OVER_TCP;
    config.client_id = MQTT_CLIENT_ID;
    config.username = MQTT_USERNAME[0] != '\0' ? MQTT_USERNAME : nullptr;
    config.password = MQTT_USERNAME[0] != '\0' ? MQTT_PASSWORD : nullptr;
    config.lwt_topic = STATUS_TOPIC;
    config.lwt_msg = "offline";
    config.lwt_qos = 1;
    config.lwt_retain = 1;
    config.keepalive = 15;
    config.reconnect_timeout_ms = 5000;
    config.network_timeout_ms = 1000;
    config.buffer_size = 256;
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
    if (client == nullptr) {
        Serial.println("MQTT init failed; local light only");
        vTaskDelete(nullptr);
        return;
    }
    const esp_err_t registered = esp_mqtt_client_register_event(client,
        static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt_event, nullptr);
    if (registered != ESP_OK || esp_mqtt_client_start(client) != ESP_OK) {
        Serial.println("MQTT start failed; local light only");
        esp_mqtt_client_destroy(client);
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        const uint32_t now = millis();
        if (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(now - wifi_since) >= WIFI_RETRY_MS) {
            WiFi.reconnect();
            wifi_since = now;
        }
        uint32_t detected_at;
        while (xQueueReceive(motion_queue, &detected_at, 0) == pdTRUE) {
            if (!mqtt_ready.load() || WiFi.status() != WL_CONNECTED ||
                static_cast<uint32_t>(millis() - detected_at) >= MOTION_MAX_AGE_MS) continue;
            if (esp_mqtt_client_get_outbox_size(client) >= static_cast<int>(MQTT_OUTBOX_LIMIT) ||
                esp_mqtt_client_enqueue(client, MOTION_TOPIC, "1", 1, 1, 0, true) < 0) {
                Serial.println("Motion publish queue full/failed; event dropped");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    digitalWrite(LIGHT_PIN, LOW);
    pinMode(LIGHT_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT); // GPIO34는 내부 pull-up/down이 없다.
    Serial.begin(115200);
    Serial.println("entrance_node start");
    command_queue = xQueueCreate(8, sizeof(LightCommand));
    motion_queue = xQueueCreate(8, sizeof(uint32_t));
    if (command_queue == nullptr || motion_queue == nullptr) {
        Serial.println("Queue allocation failed; local light only");
    } else if (WIFI_SSID[0] == '\0' || MQTT_HOST[0] == '\0') {
        Serial.println("Network config empty; local light only (fill network_config.h)");
    } else if (xTaskCreate(network_task, "entrance_net", 4096, nullptr, 1, nullptr) != pdPASS) {
        Serial.println("Network task creation failed; local light only");
    }
}

void loop() {
    LightCommand command;
    while (command_queue != nullptr && xQueueReceive(command_queue, &command, 0) == pdTRUE) {
        light.command(command, millis());
        Serial.println(command == LightCommand::Alert ? "LIGHT ALERT" : "LIGHT NORMAL");
    }
    const uint32_t now = millis();
    const bool motion = light.sample(digitalRead(PIR_PIN) == HIGH, now);
    digitalWrite(LIGHT_PIN, light.light_on(now) ? HIGH : LOW);
    if (motion) {
        Serial.println("PIR motion");
        if (mqtt_ready.load() && motion_queue != nullptr && xQueueSend(motion_queue, &now, 0) != pdTRUE) {
            Serial.println("Motion queue full; event dropped");
        }
    }
    delay(5); // 긴 대기 없이 PIR·LED를 계속 처리한다.
}

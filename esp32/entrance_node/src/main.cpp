#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "entrance_control.h"

#if defined(RETRACE_NETWORK_BUILD_CHECK)
#include "network_config.build_check.h"
#elif __has_include("network_config.h")
#include "network_config.h"
#else
#include "network_config.example.h"
#endif

// Retrace - 출입 노드 (스마트 현관등)
// 서버 client 형식: [ID:PASSWORD] 로그인, [대상]payload\n 송신.
static constexpr uint8_t PIR_PIN = 34;
static constexpr uint8_t LIGHT_PIN = 25;
static constexpr uint32_t LIGHT_HOLD_MS = 7000;
static constexpr uint32_t ALERT_HOLD_MS = 10000;
static constexpr uint32_t ALERT_BLINK_MS = 250;
static constexpr uint32_t PIR_DEBOUNCE_MS = 50;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t SERVER_RETRY_MS = 5000;
static constexpr uint32_t LOGIN_TIMEOUT_MS = 3000;
static constexpr uint32_t EVENT_MAX_AGE_MS = 5000;
static constexpr size_t SERVER_LINE_MAX = 255;

struct PirEvent {
    uint32_t occurred_at;
};

static EntranceControl light(LIGHT_HOLD_MS, ALERT_HOLD_MS, ALERT_BLINK_MS,
                             PIR_DEBOUNCE_MS);
static QueueHandle_t command_queue = nullptr;
static QueueHandle_t event_queue = nullptr;
static std::atomic<bool> server_ready{false};

static void set_lights(bool on) {
    const uint8_t level = on ? HIGH : LOW;
    digitalWrite(LIGHT_PIN, level);
}

static bool send_all(WiFiClient& client, const char* data, size_t length) {
    size_t sent = 0;
    while (sent < length && client.connected()) {
        const size_t count = client.write(
            reinterpret_cast<const uint8_t*>(data + sent), length - sent);
        if (count == 0) return false;
        sent += count;
    }
    return sent == length;
}

static bool send_to_server(WiFiClient& client, const char* payload) {
    char line[SERVER_LINE_MAX + 1];
    const int length = snprintf(line, sizeof(line), "[SERVER]%s\n", payload);
    return length > 0 && static_cast<size_t>(length) < sizeof(line) &&
           send_all(client, line, static_cast<size_t>(length));
}

static bool read_line(WiFiClient& client, char* line, size_t capacity,
                      uint32_t timeout_ms) {
    size_t used = 0;
    const uint32_t started = millis();
    while (client.connected() &&
           static_cast<uint32_t>(millis() - started) < timeout_ms) {
        while (client.available()) {
            const int value = client.read();
            if (value < 0) break;
            if (value == '\n') {
                line[used] = '\0';
                return true;
            }
            if (value == '\r') continue;
            if (used + 1 >= capacity) return false;
            line[used++] = static_cast<char>(value);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

static bool login_server(WiFiClient& client) {
    char login[160];
    const int length = snprintf(login, sizeof(login), "[%s:%s]", CLIENT_ID,
                                CLIENT_PASSWORD);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(login) ||
        !send_all(client, login, static_cast<size_t>(length))) return false;

    char response[SERVER_LINE_MAX + 1];
    if (!read_line(client, response, sizeof(response), LOGIN_TIMEOUT_MS))
        return false;
    return strstr(response, "New connected!") != nullptr;
}

static void handle_server_line(WiFiClient& client, char* line) {
    Serial.printf("SERVER RX: %s\n", line);
    if (line[0] != '[') return;
    char* closing = strchr(line, ']');
    if (closing == nullptr || closing == line + 1) return;
    const char* payload = closing + 1;

    // 요청자는 서버에 PING@CLIENT_ID를 보내지만 대상 client에는 PING이 온다.
    if (strcmp(payload, "PING") == 0) {
        const bool sent = send_to_server(client, "OK@PING");
        Serial.println(sent ? "SERVER TX: OK@PING" : "SERVER TX FAILED: OK@PING");
        return;
    }

    LightCommand command;
    if (parse_light_command(payload, strlen(payload), command)) {
        if (xQueueSend(command_queue, &command, 0) == pdTRUE)
            (void)send_to_server(client, command == LightCommand::Alert
                                            ? "OK@LIGHT:ALERT"
                                            : "OK@LIGHT:NORMAL");
        else
            (void)send_to_server(client, "ERR@LIGHT:BUSY");
        return;
    }
    (void)send_to_server(client, "ERR@COMMAND:UNKNOWN");
}

static void network_task(void*) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("Wi-Fi init failed; local light only");
        vTaskDelete(nullptr);
        return;
    }
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t wifi_retry_at = millis();
    uint32_t server_retry_at = 0;
    WiFiClient client;
    char input[SERVER_LINE_MAX + 1];
    size_t input_used = 0;
    bool dropping = false;
    bool wifi_announced = false;

    for (;;) {
        const uint32_t now = millis();
        if (WiFi.status() != WL_CONNECTED) {
            server_ready.store(false);
            wifi_announced = false;
            client.stop();
            input_used = 0;
            dropping = false;
            if (static_cast<uint32_t>(now - wifi_retry_at) >= WIFI_RETRY_MS) {
                WiFi.reconnect();
                wifi_retry_at = now;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!wifi_announced) {
            Serial.printf("Wi-Fi connected; IP=%s, server=%s:%u\n",
                          WiFi.localIP().toString().c_str(), SERVER_HOST,
                          static_cast<unsigned>(SERVER_PORT));
            wifi_announced = true;
        }

        if (!client.connected()) {
            server_ready.store(false);
            input_used = 0;
            dropping = false;
            if (static_cast<int32_t>(now - server_retry_at) < 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            if (!client.connect(SERVER_HOST, SERVER_PORT) || !login_server(client)) {
                Serial.println("Server connect/login failed; retrying");
                client.stop();
                server_retry_at = millis() + SERVER_RETRY_MS;
                continue;
            }
            server_ready.store(true);
            Serial.println("Server client connected");
        }

        while (client.available()) {
            const int value = client.read();
            if (value < 0) break;
            if (value == '\r') continue;
            if (value == '\n') {
                if (!dropping && input_used > 0) {
                    input[input_used] = '\0';
                    handle_server_line(client, input);
                }
                input_used = 0;
                dropping = false;
            } else if (!dropping) {
                if (input_used + 1 < sizeof(input))
                    input[input_used++] = static_cast<char>(value);
                else
                    dropping = true;
            }
        }

        PirEvent event;
        while (xQueueReceive(event_queue, &event, 0) == pdTRUE) {
            if (static_cast<uint32_t>(millis() - event.occurred_at) >=
                EVENT_MAX_AGE_MS) continue;
            char payload[64];
            const int length = snprintf(payload, sizeof(payload),
                                        "EVT@%s:PIRON", CLIENT_ID);
            if (length <= 0 || static_cast<size_t>(length) >= sizeof(payload) ||
                !send_to_server(client, payload)) {
                client.stop();
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    digitalWrite(LIGHT_PIN, LOW);
    pinMode(LIGHT_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    Serial.begin(115200);
    Serial.println("entrance_node start");
    command_queue = xQueueCreate(8, sizeof(LightCommand));
    event_queue = xQueueCreate(8, sizeof(PirEvent));
    if (command_queue == nullptr || event_queue == nullptr) {
        Serial.println("Queue allocation failed; local light only");
    } else if (WIFI_SSID[0] == '\0' || SERVER_HOST[0] == '\0' ||
               CLIENT_ID[0] == '\0' || CLIENT_PASSWORD[0] == '\0') {
        Serial.println("Network config empty; local light only (fill network_config.h)");
    } else if (xTaskCreate(network_task, "entrance_net", 4096, nullptr, 1,
                           nullptr) != pdPASS) {
        Serial.println("Network task creation failed; local light only");
    }
}

void loop() {
    static bool last_raw_pir = false;
    LightCommand command;
    while (command_queue != nullptr &&
           xQueueReceive(command_queue, &command, 0) == pdTRUE) {
        light.command(command, millis());
        Serial.println(command == LightCommand::Alert ? "LIGHT ALERT"
                                                       : "LIGHT NORMAL");
    }

    const uint32_t now = millis();
    const bool raw_pir = digitalRead(PIR_PIN) == HIGH;
    if (raw_pir != last_raw_pir) {
        Serial.printf("PIR RAW: %s\n", raw_pir ? "HIGH" : "LOW");
        last_raw_pir = raw_pir;
    }
    const bool motion = light.sample(raw_pir, now);
    const bool lights_on = light.light_on(now);
    set_lights(lights_on);
    if (motion) {
        Serial.printf("PIR motion; LEDs=%s\n", lights_on ? "ON" : "OFF");
        const PirEvent event{now};
        if (server_ready.load() && event_queue != nullptr &&
            xQueueSend(event_queue, &event, 0) != pdTRUE)
            Serial.println("Event queue full; event dropped");
    }
    delay(5);
}

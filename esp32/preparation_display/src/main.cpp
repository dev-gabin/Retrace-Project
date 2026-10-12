#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>
#include "display_logic.h"
#include "last_seen_display.h"
#if __has_include("display_config.h")
#include "display_config.h"
#else
#include "display_config.example.h"
#endif

extern const uint8_t weatherRoots[] asm("_binary_data_weather_roots_crt_bundle_start");
LiquidCrystal_PCF8574 lcd(LCD_ADDRESS);
constexpr size_t MAX_ITEMS = 20;
String selected[MAX_ITEMS];
size_t itemCount = 0;
bool itemsLoaded = false, itemsOkay = false, weatherLoaded = false, weatherOkay = false;
bool lcdReady = false;
float temperature = 0;
int weatherCode = 0, rainProbability = 0;
bool bringUmbrella = false;
uint32_t itemsAt = 0, weatherAt = 0, wifiAt = 0, pageAt = 0, weatherSuccessAt = 0;
size_t page = 0;
bool firstItems = true, firstWeather = true;
LastSeenReading lastSeen[LAST_SEEN_LIMIT];
unsigned lastSeenCount = 0;
bool lastSeenLoaded = false, lastSeenOkay = false, firstLastSeen = true;
bool recordUpdated = false;
// Old private config headers may still contain PAGE_MS=3000; enforce 5s minimum.
constexpr uint32_t pageIntervalMs = PAGE_MS < 5000 ? 5000 : PAGE_MS;
constexpr uint32_t newRecordHoldMs = 5000;
uint32_t lastSeenAt = 0, lastSeenSuccessAt = 0, recordUpdatedAt = 0;
// Default also supports an existing private config copied before this feature.
#ifndef LAST_SEEN_REFRESH_MS
#define LAST_SEEN_REFRESH_MS 10000UL
#endif
constexpr unsigned long lastSeenRefreshMs = LAST_SEEN_REFRESH_MS;

String labelFor(const String &id) {
    for (const auto &alias : ITEM_ALIASES) if (id == alias.id) return alias.label;
    static const ItemAlias labels[] = {
        {"car_key", "Car keys"}, {"carkey", "Car keys"}, {"wallet", "Wallet"},
        {"earphones", "Earphones"}, {"airpods", "AirPods"}, {"cell_phone", "Phone"},
        {"backpack", "Backpack"}, {"umbrella", "Umbrella"}, {"handbag", "Handbag"},
        {"suitcase", "Suitcase"}, {"laptop", "Laptop"}, {"mouse", "Mouse"},
        {"remote", "Remote"}, {"keyboard", "Keyboard"}, {"book", "Book"},
        {"bottle", "Bottle"}, {"cup", "Cup"}};
    for (const auto &entry : labels) if (id == entry.id) return entry.label;
    String label = id; label.replace('_', ' '); return label;
}

bool validId(const char *id) {
    if (!id || id[0] < 'a' || id[0] > 'z' || strlen(id) > 32) return false;
    for (const char *c = id; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return false;
    return true;
}

bool readJson(HTTPClient &http, DynamicJsonDocument &doc, const JsonDocument *filter = nullptr) {
    int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("HTTP failed: %d\n", status); http.end(); return false;
    }
    // HTTP/1.0 avoids chunked encoding when parsing directly from the stream.
    auto error = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                        : deserializeJson(doc, http.getStream());
    http.end();
    if (error) Serial.printf("JSON failed: %s\n", error.c_str());
    return !error;
}

void fetchLastSeen() {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000); http.setTimeout(3000); http.useHTTP10(true);
    String base(RETRACE_API_BASE);
    while (base.endsWith("/")) base.remove(base.length() - 1);
    if (!http.begin(client, base + "/api/items")) { lastSeenOkay = false; return; }
    // Ignore coordinates and display names; retain only fields used by this LCD.
    StaticJsonDocument<256> filter;
    filter["items"][0]["item"] = true;
    filter["items"][0]["seen_at"] = true;
    filter["items"][0]["snapshot"] = true;
    DynamicJsonDocument doc(16384);
    if (!readJson(http, doc, &filter) || !doc["items"].is<JsonArray>()) { lastSeenOkay = false; return; }
    LastSeenReading next[LAST_SEEN_LIMIT]; unsigned count = 0;
    for (JsonVariant value : doc["items"].as<JsonArray>()) {
        if (!value.is<JsonObject>()) { lastSeenOkay = false; return; }
        const char *snapshot = value["snapshot"] | "";
        const char *stamp = value["seen_at"] | "";
        // Manual drawer registration has no camera snapshot and is not an observation.
        if (!*stamp || strncmp(snapshot, "snapshots/", 10)) continue;
        const char *id = value["item"] | "";
        LastSeenReading reading;
        if (!validId(id) || !parseSeenUtc(stamp, reading.utcMicros)) { lastSeenOkay = false; return; }
        strcpy(reading.item, id);
        insertLastSeen(next, count, reading);
    }
    bool initialLoad = !lastSeenLoaded;
    // First load may be an old DB record. Do not call it a newly recorded event.
    bool newer = lastSeenLoaded && count && (!lastSeenCount || next[0].utcMicros > lastSeen[0].utcMicros);
    for (unsigned i = 0; i < LAST_SEEN_LIMIT; ++i) lastSeen[i] = next[i];
    lastSeenCount = count; lastSeenLoaded = lastSeenOkay = true;
    lastSeenSuccessAt = millis();
    if (initialLoad || newer) { page = 0; pageAt = millis(); }
    if (newer) { recordUpdated = true; recordUpdatedAt = millis(); }
    if (!count) recordUpdated = false;
    Serial.printf("Last seen refreshed: %u camera records\n", count);
}

void fetchItems() {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000); http.setTimeout(3000); http.useHTTP10(true);
    String base(RETRACE_API_BASE);
    while (base.endsWith("/")) base.remove(base.length() - 1);
    if (!http.begin(client, base + "/api/leave-items")) { itemsOkay = false; return; }
    DynamicJsonDocument doc(4096);
    if (!readJson(http, doc) || !doc["items"].is<JsonArray>()) { itemsOkay = false; return; }
    JsonArray values = doc["items"].as<JsonArray>();
    if (values.size() > MAX_ITEMS) { itemsOkay = false; return; }
    String next[MAX_ITEMS]; size_t count = 0;
    for (JsonVariant value : values) {
        if (!value.is<const char *>() || !validId(value.as<const char *>())) { itemsOkay = false; return; }
        String id = value.as<const char *>(); bool duplicate = false;
        for (size_t i = 0; i < count; ++i) if (next[i] == id) duplicate = true;
        if (!duplicate) next[count++] = id;
    }
    bool changed = !itemsLoaded || count != itemCount;
    for (size_t i = 0; i < count; ++i) if (next[i] != selected[i]) changed = true;
    for (size_t i = 0; i < MAX_ITEMS; ++i) selected[i] = next[i];
    itemCount = count; // Successful empty response clears the old list too.
    itemsLoaded = itemsOkay = true;
    if (changed) { Serial.printf("Items updated: %u\n", unsigned(count)); }
}

void fetchWeather() {
    WiFiClientSecure client;
    client.setCACertBundle(weatherRoots); client.setHandshakeTimeout(5);
    HTTPClient http;
    http.setConnectTimeout(3000); http.setTimeout(3000); http.useHTTP10(true);
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LATITUDE, 4)
        + "&longitude=" + String(WEATHER_LONGITUDE, 4)
        + "&current=temperature_2m,weather_code&hourly=precipitation_probability,precipitation"
          "&forecast_hours=3&timezone=Asia%2FSeoul";
    if (!http.begin(client, url)) { weatherOkay = false; return; }
    DynamicJsonDocument doc(8192);
    if (!readJson(http, doc) || !doc["current"]["temperature_2m"].is<float>()
        || !doc["current"]["weather_code"].is<int>()) { weatherOkay = false; return; }
    JsonArray probabilities = doc["hourly"]["precipitation_probability"].as<JsonArray>();
    JsonArray amounts = doc["hourly"]["precipitation"].as<JsonArray>();
    if (probabilities.size() != 3 || amounts.size() != 3) { weatherOkay = false; return; }
    int maximum = 0; bool umbrella = false;
    for (size_t i = 0; i < 3; ++i) {
        if (!probabilities[i].is<int>() || !amounts[i].is<float>()) { weatherOkay = false; return; }
        int probability = probabilities[i].as<int>(); float amount = amounts[i].as<float>();
        if (probability < 0 || probability > 100 || amount < 0) { weatherOkay = false; return; }
        maximum = max(maximum, probability);
        umbrella |= rainExpected(probability, amount, RAIN_PROBABILITY_THRESHOLD);
    }
    temperature = doc["current"]["temperature_2m"].as<float>();
    weatherCode = doc["current"]["weather_code"].as<int>();
    rainProbability = maximum; bringUmbrella = umbrella;
    weatherLoaded = weatherOkay = true; weatherSuccessAt = millis();
    Serial.printf("Weather: %.1f C, next 3h rain %d%%\n", temperature, rainProbability);
}

const char *conditionLabel(int code) {
    if (code == 0) return "Clear";
    if (code <= 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 95) return "Thunder";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    return "Rain";
}

void writeLcd(String top, String bottom) {
    top = top.substring(0, 16); bottom = bottom.substring(0, 16);
    while (top.length() < 16) top += ' ';
    while (bottom.length() < 16) bottom += ' ';
    static String previousTop, previousBottom;
    if (top == previousTop && bottom == previousBottom) return;
    previousTop = top; previousBottom = bottom;
    if (lcdReady) { lcd.setCursor(0, 0); lcd.print(top); lcd.setCursor(0, 1); lcd.print(bottom); }
    Serial.printf("LCD |%s|%s|\n", top.c_str(), bottom.c_str());
}

void render() {
    time_t now = time(nullptr); struct tm local {};
    bool clockOkay = now > 1700000000 && localtime_r(&now, &local);
    bool morning = clockOkay && morningHour(local.tm_hour, MORNING_START_HOUR, MORNING_END_HOUR);
    bool forecastFresh = weatherLoaded && weatherOkay && WiFi.status() == WL_CONNECTED
        && !intervalElapsed(millis(), weatherSuccessAt, WEATHER_REFRESH_MS * 2);
    size_t slots = itemCount ? itemCount : 1;
    size_t seenSlots = lastSeenCount ? lastSeenCount : 1;
    size_t total = seenSlots + 2 + (morning ? slots : 0) + (forecastFresh && bringUmbrella ? 1 : 0);
    bool wasRecordUpdated = recordUpdated;
    bool priorityRecord = advanceDisplayPage(millis(), recordUpdated, recordUpdatedAt,
                                            page, pageAt, total, pageIntervalMs, newRecordHoldMs);
    if (wasRecordUpdated && !recordUpdated) {
        page = seenSlots;
        pageAt = millis();
    }
    String marker = WiFi.status() != WL_CONNECTED ? "Offline" : "Weather old";
    if (page < seenSlots) {
        bool fresh = lastSeenOkay && WiFi.status() == WL_CONNECTED
            && !intervalElapsed(millis(), lastSeenSuccessAt, lastSeenRefreshMs * 3);
        if (!lastSeenCount) {
            writeLcd("Last seen", lastSeenLoaded ? "No camera record" : "Records pending");
        } else {
            const auto &reading = lastSeen[page];
            // Explicit UTC+9 avoids relying on current NTP synchronization for DB dates.
            time_t koreaSeconds = reading.utcMicros / 1000000 + 9 * 3600;
            struct tm observed {}; gmtime_r(&koreaSeconds, &observed);
            char heading[17], observedTime[7];
            snprintf(heading, sizeof(heading), "%s %02d/%02d", fresh ? "Last seen" : "Cached", observed.tm_mon + 1, observed.tm_mday);
            snprintf(observedTime, sizeof(observedTime), " %02d:%02d", observed.tm_hour, observed.tm_min);
            if (fresh && priorityRecord)
                strcpy(heading, "Record updated");
            writeLcd(heading, labelFor(reading.item).substring(0, 10) + observedTime);
        }
    } else if (page == seenSlots) {
        char heading[17];
        if (clockOkay) snprintf(heading, sizeof(heading), "%s %02d:%02d", WEATHER_CITY, local.tm_hour, local.tm_min);
        else snprintf(heading, sizeof(heading), "%s Time sync", WEATHER_CITY);
        String detail = String(temperature, 1) + "C " + (forecastFresh ? conditionLabel(weatherCode) : marker.c_str());
        writeLcd(heading, weatherLoaded ? detail : "Weather pending");
    } else if (page == seenSlots + 1) {
        writeLcd(forecastFresh ? "Next 3h forecast" : "Weather pending", forecastFresh ? "Rain " + String(rainProbability) + "%" : (weatherLoaded ? marker : "Waiting network"));
    } else if (morning && page < seenSlots + 2 + slots) {
        size_t index = page - seenSlots - 2;
        String heading = itemsOkay && WiFi.status() == WL_CONNECTED ? "Bring " : "Cached ";
        heading += String(unsigned(index + 1)) + "/" + String(unsigned(itemCount));
        writeLcd(itemCount ? heading : "Preparation", itemCount ? labelFor(selected[index]) : (itemsLoaded ? "No items chosen" : "List pending"));
    } else writeLcd("Rain expected", "Bring umbrella!");
}

void setup() {
    Serial.begin(115200);
    Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
    Wire.beginTransmission(LCD_ADDRESS); lcdReady = Wire.endTransmission() == 0;
    if (lcdReady) { lcd.begin(16, 2, Wire); lcd.setBacklight(255); }
    else Serial.println("LCD missing: check I2C address / wiring.");
    configTime(9 * 3600, 0, "pool.ntp.org", "time.google.com");
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    if (strlen(WIFI_SSID)) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    else Serial.println("Configure include/display_config.h before use.");
    render();
}

void loop() {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
        if (strlen(WIFI_SSID) && intervalElapsed(now, wifiAt, 15000)) {
            wifiAt = now; WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    } else {
        if (firstLastSeen || intervalElapsed(now, lastSeenAt, lastSeenRefreshMs)) {
            firstLastSeen = false; lastSeenAt = now; fetchLastSeen();
            render(); // Show new record before slower weather/network requests.
        }
        if (firstItems || intervalElapsed(now, itemsAt, ITEMS_REFRESH_MS)) {
            firstItems = false; itemsAt = now; fetchItems();
        }
        unsigned long weatherInterval = weatherOkay ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS;
        if (time(nullptr) > 1700000000 && (firstWeather || intervalElapsed(now, weatherAt, weatherInterval))) {
            firstWeather = false; weatherAt = now; fetchWeather();
        }
    }
    render(); delay(20);
}

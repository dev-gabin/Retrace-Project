#pragma once

// Copy to display_config.h. Never commit real Wi-Fi credentials.
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
// Real Jetson HTTP API address, NOT localhost or the demo web server.
constexpr char RETRACE_API_BASE[] = "http://192.168.0.100:8080";
constexpr int LCD_SDA_PIN = 4;
constexpr int LCD_SCL_PIN = 5;
constexpr unsigned char LCD_ADDRESS = 0x27;
constexpr int MORNING_START_HOUR = 7;
constexpr int MORNING_END_HOUR = 8;
constexpr float WEATHER_LATITUDE = 37.5665f;
constexpr float WEATHER_LONGITUDE = 126.9780f;
constexpr char WEATHER_CITY[] = "Seoul";
constexpr unsigned long ITEMS_REFRESH_MS = 15000;
#define LAST_SEEN_REFRESH_MS 10000UL
constexpr unsigned long WEATHER_REFRESH_MS = 600000;
constexpr unsigned long WEATHER_RETRY_MS = 30000;
constexpr unsigned long PAGE_MS = 5000;
constexpr int RAIN_PROBABILITY_THRESHOLD = 50;
// ASCII aliases for custom IDs. Standard 1602 cannot show Korean.
struct ItemAlias { const char *id; const char *label; };
constexpr ItemAlias ITEM_ALIASES[] = {{"", ""}};

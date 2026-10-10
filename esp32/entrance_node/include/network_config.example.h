#ifndef ENTRANCE_NETWORK_CONFIG_H
#define ENTRANCE_NETWORK_CONFIG_H

// 이 파일을 같은 폴더의 network_config.h로 복사하고 강의실 값을 입력한다.
// network_config.h는 Git에서 제외한다. 빈 설정으로도 기본 PIR 센서등은 동작한다.
static constexpr char WIFI_SSID[] = "";
static constexpr char WIFI_PASSWORD[] = "";
static constexpr char MQTT_HOST[] = ""; // Jetson의 LAN IP. localhost가 아니다.
static constexpr unsigned MQTT_PORT = 1883;
static constexpr char MQTT_USERNAME[] = ""; // 인증을 쓰지 않으면 빈 값
static constexpr char MQTT_PASSWORD[] = "";

#endif

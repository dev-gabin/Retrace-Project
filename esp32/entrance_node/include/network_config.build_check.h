#ifndef ENTRANCE_NETWORK_CONFIG_H
#define ENTRANCE_NETWORK_CONFIG_H

// 네트워크 코드의 컴파일·링크 확인 전용 값. 실제 인증정보가 아니다.
static constexpr char WIFI_SSID[] = "BUILD_CHECK_ONLY";
static constexpr char WIFI_PASSWORD[] = "";
static constexpr char SERVER_HOST[] = "127.0.0.1";
static constexpr unsigned SERVER_PORT = 5000;
static constexpr char CLIENT_ID[] = "ENTRANCE";
static constexpr char CLIENT_PASSWORD[] = "BUILD_CHECK_ONLY";

#endif

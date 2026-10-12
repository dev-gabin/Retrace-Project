# Retrace 강의실 통합 테스트 체크리스트

메인 유닛·서랍·현관등의 테스트를 이 문서에서 함께 관리한다. 체크는 **실제로 실행하고 예상 결과와 비교한 뒤** 표시한다.

보드 단독 테스트와 통합 테스트는 최종 통합 브랜치인 `develop` 기준으로 진행한다.

## 1. 테스트 대상과 순서

보드 단독 검증 → 문제가 있으면 수정·재검증 → Jetson·웹 전체 연동 검증 순서로 진행한다.

| 대상 | 펌웨어 위치 | 강의실에서 확인할 것 |
|---|---|---|
| STM32 #1 메인 유닛 | `stm32/main_unit` | USB 명령, Pan/Tilt 보정, 레이저, PIR |
| STM32 #2 서랍 | `stm32/drawer` | USB → HC-06, LED, 서보 밀기·복귀, SOS |
| ESP32 #1 현관등 | `esp32/entrance_node` | 한 색 LED, PIR, Wi-Fi/TCP, 경고·복귀 |
| Jetson / 웹 연동 | `jetson_nano`, `web` | 보드 단독 테스트 후 전체 연동 |

권장 순서: **배선·부품 확인 → 메인 유닛 단독 → 서랍 USB → 서랍 HC-06 → 현관등 → Jetson·웹 연동**.

| 연결 | 설정 |
|---|---|
| 메인 유닛 USB VCP (USART2) | 115200, 8N1, 대문자 ASCII, 명령 끝 LF (`\n`) |
| 서랍 USB VCP (USART2, 테스트용) | 115200, 8N1, 같은 시리얼 명령 |
| 서랍 HC-06 (USART1) | 9600, 8N1 가정. 실제 모듈 속도 확인 |
| 현관등 TCP | Jetson `server_jetson` 포트 5000, ID/비밀번호는 5장 참고 |

시리얼은 명령을 보낸 UART로 `OK`/`ERR` 응답을 받는다. 서랍 SOS 이벤트는 USB와 HC-06 양쪽으로 전달한다. 자세한 약속은 [통신 프로토콜](protocol.md)을 참고한다.

## 2. 빌드 확인 (보드 없이)

아래 명령은 저장소 루트의 PowerShell에서 실행한다.

### STM32

```powershell
cmake --build stm32/drawer/build/Debug
cmake --build stm32/main_unit/build/Debug
```

- VS Code에서는 각 프로젝트 폴더를 따로 열고 `F7`로 빌드한다. 두 프로젝트 모두 `.settings/bundles-lock.store.json`으로 도구 버전(CMake 4.3.1, GCC 14.3.1, Ninja 1.13.2)을 고정한다.
- 정상 빌드 기준: 경고 0개, 메인 유닛 FLASH 약 19.8KB, 서랍 FLASH 약 21.3KB.
- 빌드 성공은 실제 UART 속도, NVIC, PWM 파형, 전원, 서보 이동 검증을 대신하지 않는다.

### 현관등

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe" run --project-dir esp32/entrance_node --environment lolin_d32 --environment lolin_d32_network_check
```

- `lolin_d32`는 실제 보드용, `lolin_d32_network_check`는 `include/network_config.build_check.h`의 예시 설정으로 네트워크 코드까지 컴파일·링크하는 빌드 검증용이다. 보드 없이 실제 TCP 서버에 접속하는 테스트가 아니다.
- 정상 빌드 기준: 경고 0개, 기본 빌드(`lolin_d32`) Flash 약 21%, 네트워크 빌드(`lolin_d32_network_check`) Flash 약 67%.
- 빌드 성공은 PIR 전압·LED 구동·Wi-Fi 연결·TCP 로그인과 이벤트 전달 검증을 대신하지 않는다.

## 3. STM32 #1 메인 유닛 (레이저·Pan/Tilt·PIR)

연결: **USB VCP 115200, 8N1**, 명령 끝 LF. 카메라는 고정하고 레이저만 Pan/Tilt로 움직이는 구성이다. 목표 좌표를 각도로 바꾸는 것은 Jetson, 받은 각도를 서보 펄스로 바꾸는 것은 STM32가 담당한다.

| 확인 | 입력·조작 | 예상 결과 |
|---|---|---|
| □ | `PING` | `OK@PING` |
| □ | `AIM:90,45` | `OK:AIM`, Pan 90° / Tilt 45°에 해당하는 PWM |
| □ | `AIM:0,0`, `AIM:180,180` | `OK:AIM`, 보정한 범위 내 이동 |
| □ | `AIM:181,90` | `ERR:AIM:RANGE`, 이전 위치 유지 |
| □ | `aim:90,45` | `ERR:UNKNOWN` |
| □ | `LASER:ON`, `LASER:OFF` | 각각 `OK:LASER`, 출력 ON / OFF |
| □ | `HOME` | `OK:HOME`, Pan/Tilt 90° + 레이저 OFF |
| □ | PIR 입력 HIGH / LOW 유지 | 50ms 필터 후 `EVT@PIR:1` / `EVT@PIR:0` |
| □ | B1 버튼 | PIR 이벤트 오발행 없음 |

- [ ] 실제 SG90 범위에 맞춰 `pan_tilt.c`의 최소·최대 펄스 보정 (현재 임시 1000~2000µs).
- [ ] 레이저 구동 회로와 PIR 모델·출력 전압 확인 후 연결.
- [ ] 보드 업로드, USB 응답, PWM 파형, 실제 이동 확인.

## 4. STM32 #2 서랍

첫 테스트는 **USB VCP 115200, 8N1**, 이후 **HC-06 USART1 9600, 8N1**로 같은 명령을 반복한다. HC-06 실제 속도는 모듈에서 확인한다. 서랍 USB IRQ는 USER CODE에서 활성화한 구현이다.

### 4-1. 서보 보정

장착 위치는 **1~6번 각 서랍 뒤에 서보 1개씩 (총 6개)**이다. 번호는 정면에서 위 `1 2`, 가운데 `3 4`, 아래 `5 6`으로 대응한다. 복귀는 **서보 팔의 원위치 복귀**이며 서랍을 자동으로 닫는 기능이 아니다.

`stm32/drawer/Core/Src/drawer.c` 상단의 값은 **기구에서 확인하지 않은 임시값**이다.

| 상수 | 현재값 | 확인할 내용 |
|---|---|---|
| `SERVO_RETURN_PULSE_US` | 1000µs | 밀지 않는 복귀 위치 |
| `SERVO_PUSH_PULSE_US` | 2000µs | 안전하게 밀어내는 위치 |
| `SERVO_PUSH_MS` | 500ms | 밀어내기 유지 시간 |
| `SERVO_RETURN_MS` | 500ms | 복귀 완료에 필요한 시간 |
| `LED_TIMEOUT_MS` | 10000ms | LED 표시 시간 |

부팅은 모든 채널 Pulse 0으로 유지한다. 기구와 서보 1개부터 분리해 위치·방향을 확인하고 값을 맞춘 뒤 팝업 테스트를 진행한다. 외부 5V 전원과 STM32 GND를 공통 연결한다. 펌웨어는 한 채널씩 신호를 주지만 실제 전류와 복귀 완료 여부는 측정해야 한다. 현재 동일한 펄스를 6개에 사용하므로 각 서보에서 동일한 값으로 동작하는지 확인한다.

### 4-2. USB와 HC-06에서 각각 확인

| USB | HC-06 | 입력·조작 | 예상 결과 |
|---|---|---|---|
| □ | □ | 전원 ON | LED OFF, 서보 Pulse 0 |
| □ | □ | `PING` | `OK@PING` |
| □ | □ | `LED:1:ON` | `OK:LED`, 1번만 점등 |
| □ | □ | `LED:6:ON` | `OK:LED`, 1번 OFF / 6번 ON |
| □ | □ | `LED:1:OFF` | `OK:LED`, 6번은 유지 |
| □ | □ | `LED:ALL:OFF` | `OK:LED`, 전체 소등 |
| □ | □ | `clientID:OPEN:3` | `OK:DRAWER`, 3번 LED ON, 밀기 → 서보 복귀 → Pulse 0 |
| □ | □ | 동작 중 `clientID:OPEN:6` | 이전 LED 즉시 OFF, 6번 LED ON, 이전 서보 복귀 후 6번 동작 |
| □ | □ | 복귀 중 여러 `clientID:OPEN` | 마지막 요청 예약, 접수한 요청마다 `OK:DRAWER` |
| □ | □ | 마지막 LED ON 이후 10초 | 자동 소등, 명령 수신 계속 가능 |
| □ | □ | `clientID:OPEN:0`, `clientID:OPEN:7` | `ERR:DRAWER:RANGE`, 동작 변경 없음 |
| □ | □ | `LED:7:ON` | `ERR:LED:RANGE` |
| □ | □ | 소문자·형식 오류·32바이트 초과 | `ERR:UNKNOWN`, 구동 없음 |
| □ | □ | 짧은 연속 명령 | UART별 8줄 큐로 순서대로 처리, 큐 초과 줄은 실행하지 않고 `ERR:UNKNOWN` |
| □ | □ | SOS 버튼 50ms 이상 누름 | `EVT@clientID:SOS` 한 번, 양쪽 UART에서 확인 |
| □ | □ | SOS 길게 누름 / 떼고 다시 누름 | 유지 중 추가 이벤트 없음 / 안정적인 해제 후 재누름 이벤트 한 번 |

LED는 **서랍 닫힘을 감지하지 않고 시간으로 소등**한다. 사람이 먼저 닫아도 남은 시간 동안 켜져 있는 것이 현재 동작이다.

- [ ] 서랍 번호 1~6과 실제 배선·기구 위치 일치 확인.
- [ ] USB IRQ가 실제 동작하고 양쪽 UART 명령이 섞이지 않는지 확인.
- [ ] HC-06 TX → STM32 PA10(RX), HC-06 RX ← STM32 PA9(TX), GND 공통 및 실제 baud rate 확인.
- [ ] 서보 보정, 순차 동작, LED 소등, 버튼 채터링 실물 확인.
- [ ] Pulse 0 후 서보가 복귀 위치를 유지하는지 확인.

## 5. ESP32 #1 현관등

한 색 LED로 구현했다. ESP32는 CubeMX가 아니라 코드의 `PIR_PIN` / `LIGHT_PIN`과 `pinMode()`로 핀을 설정한다. 네트워크 대기는 별도 태스크에서 처리하고 메인 루프는 PIR·LED·타이머를 계속 처리한다.

| 항목 | 구현값·실물 확인 필요 |
|---|---|
| 보드 | LOLIN D32 |
| PIR | GPIO34, INPUT, 50ms 필터. 센서 모델·출력 확인 필요. 내부 풀다운 없음 |
| LED | 한 색 LED, GPIO25, OUTPUT, HIGH = ON / LOW = OFF. 실제 구동 회로 확인 |
| TCP | Jetson 서버 포트 5000, 기본 클라이언트 ID `ENTRANCE` |
| 평소 점등 | PIR HIGH 동안 ON, 안정적인 LOW 감지 후 7초 유지 |
| ALERT | 250ms마다 ON/OFF, 10초 후 기본 센서등 모드 복귀 |

`src/main.cpp` 맨 위의 `LIGHT_HOLD_MS`, `ALERT_HOLD_MS`, `ALERT_BLINK_MS`, `PIR_DEBOUNCE_MS`는 임시값으로 강의실에서 조정한다.

### 5-1. 강의실 네트워크 설정

1. `include/network_config.example.h`를 같은 폴더의 `network_config.h`로 복사한다.
2. `WIFI_SSID`, `WIFI_PASSWORD`, `SERVER_HOST`, `SERVER_PORT`, `CLIENT_ID`, `CLIENT_PASSWORD`를 입력한다. ESP32에서 `localhost`는 Jetson이 아니다. 같은 ID/비밀번호를 서버의 `idpasswd.txt`에도 등록한다.
3. `lolin_d32` 환경으로 다시 빌드·업로드한다. `network_config.h`는 `.gitignore`로 제외되며 예제만 공유한다.

설정이 비어 있으면 USB 로그에 `Network config empty; local light only`가 나오고 기본 PIR 센서등만 동작한다. 네트워크 연동 테스트를 하려면 설정을 채워야 한다.

### 5-2. 센서등·TCP 확인

| 확인 | 입력·조작 | 기대 결과 |
|---|---|---|
| □ | 전원 켜기, PIR LOW | LED OFF |
| □ | 설정 없이 PIR HIGH 50ms 유지 | LED ON, 기본 센서등 동작 |
| □ | Wi-Fi·TCP 연결 | 로그에 `Server client connected` 표시 |
| □ | 연결 중 PIR HIGH 50ms 유지 | LED ON, 서버에 `EVT@ENTRANCE:PIRON` 한 번 전달 |
| □ | PIR HIGH 계속 유지 | LED 유지, motion 중복 발행 없음 |
| □ | 안정적인 LOW 이후 다시 HIGH | 새 motion 한 번 게시 |
| □ | PIR LOW 50ms 유지 후 10초 경과 | LED OFF |
| □ | 서버에서 `ALERT` 전달 | ON부터 시작, 250ms마다 ON/OFF, `OK@LIGHT:ALERT` 응답 |
| □ | 경고 시작 후 8초에 다시 `ALERT` | 다시 ON부터 시작, 마지막 명령부터 10초 뒤 기본 모드 |
| □ | `NORMAL` 또는 ALERT 10초 경과 | 기본 센서등 모드 복귀. PIR 조건이 남아 있으면 ON |
| □ | 서버에서 `PING@ENTRANCE` 요청 | 현관등이 `OK@PING` 응답 |
| □ | TCP 연결 끊기 | 5초 간격으로 재접속, 로컬 PIR·LED는 계속 동작 |
| □ | TCP 없이 PIR 감지 | 기본 센서등 기능 유지 |
| □ | TCP 연결 실패 중 경고 타이머 확인 | 기존 ALERT는 10초 후 정상 복귀, PIR·LED 처리는 계속됨 |

연결되지 않은 동안 발생한 PIR 이벤트는 보관하지 않습니다. 연결 직후 `[ID:비밀번호]` 로그인이 성공해야 이벤트와 명령을 교환합니다.

```text
[SERVER]PING@ENTRANCE
[ENTRANCE]ALERT
[ENTRANCE]NORMAL
```

## 6. Jetson·웹 통합 테스트 — 각 기능 연동 후

보드 단독 검증이 끝난 뒤 해당 담당자와 함께 실행한다. 아래는 연동 확인 항목이며 **Jetson·웹 기능 구현 완료를 의미하지 않는다**.

외출 음성 안내는 **Android 폰의 ntfy 앱 + Tasker를 활용하는 계획이며, 구현·실물 검증 전**이다. Jetson의 `PhoneNotifier`와 ntfy 서버·폰 앱을 연결하고, Tasker에서 발신 앱이 ntfy이고 제목이 `Car Key Check`인 알림만 본문 TTS로 읽도록 설정한 뒤 아래 항목을 확인한다. 웹은 검색·제어 화면이며, 음성 안내는 웹 화면과 별도 알림 경로로 처리한다.

| 확인 | 사용자 조작·상황 | 기대 결과 |
|---|---|---|
| □ | 웹에서 물건 위치 안내 | Jetson이 목표 각도를 계산하고 STM32에 `AIM` 전달, 이동 후 레이저 제어 |
| □ | 웹에서 1~6번 서랍 선택 | HC-06을 통해 해당 서랍 LED ON + 서보 밀기·복귀, LED 10초 후 OFF |
| □ | 서랍 SOS 버튼 누르기 | `EVT@clientID:SOS`가 Jetson 서버 로그에 기록됨 (폰 알림·사이렌은 후속 구현) |
| □ | 응답 대기 중 PIR·SOS 이벤트 수신 | Jetson이 이벤트와 `OK`/`ERR` 응답을 구분해 처리 |
| □ | 현관 PIR 감지·ALERT | TCP 이벤트와 경고 조명이 연결된 시나리오대로 동작 |
| □ | 외출 알림 조건 충족 → Jetson에서 ntfy 전송 | 폰의 ntfy 앱에 제목 `Car Key Check`, 본문 `차키 챙기셨나요?` 알림이 도착하고 Tasker가 본문을 음성으로 읽음 |
| □ | Retrace 웹을 닫고 폰 화면을 잠근 뒤 새 외출 알림 전송 | 웹을 열거나 알림을 누르지 않아도 폰에서 안내 문장이 들림. 폰 모델·OS·필요한 권한·배터리 설정과 실제 결과를 기록 |
| □ | 카톡·문자 등 다른 앱 알림 / ntfy의 다른 제목 알림 수신 | 외출 안내 TTS 규칙이 실행되지 않음. SOS 알림·사이렌은 별도 기존 항목으로 확인 |
| □ | 같은 외출 알림의 중복 수신·알림 갱신 | 같은 알림을 반복해서 읽지 않음. 중복 판별 기준과 억제 시간은 Jetson·폰 담당자와 정해 기록하고, 새로운 외출 알림은 정상적으로 읽는지 확인 |

## 7. 강의실 결과 기록

실측한 펄스·시간과 실패한 입력을 기록하고, 수정 후 해당 항목을 다시 확인한다.

| 보드 | 날짜·테스터 | 업로드한 브랜치·커밋 | 통과 / 미통과 항목 | 확정한 값·수정 사항 |
|---|---|---|---|---|
| 메인 유닛 | | | | |
| 서랍 USB·HC-06 | | | | |
| 현관등 | | | | |
| Jetson·웹 연동 | | | | |

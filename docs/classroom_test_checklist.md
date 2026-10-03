# Retrace 강의실 통합 테스트 체크리스트

레이저·서랍·현관등·부저의 테스트를 이 문서에서 함께 관리한다. 체크는 **실제로 실행하고 예상 결과와 비교한 뒤** 표시한다. 현재 실물 테스트는 모두 미실행이며, 빌드·PC 테스트 통과와 구분한다.

STM32 항목은 `feature/stm32`의 `3f42ba3`에 저장된 체크리스트를 가져왔고, ESP32 항목은 `feature/esp32`의 현관등·부저 구현을 반영했다. 이 문서는 두 feature 브랜치의 같은 경로에 두며, 각 보드의 펌웨어는 담당 feature에서 검증한다. `develop` 코드 통합은 실물 확인 후 별도 단계다.

## 1. 현재 상태와 테스트 순서

네 보드 펌웨어 작성·빌드·PC 테스트는 완료했다. 다음 단계는 강의실 설정·배선·보정·보드 단독 검증이며, 문제가 있으면 담당 feature에서 수정·재검증한다. 실물 확인 후 feature → develop PR로 통합하고 Jetson·웹과 전체 연동을 검증한다.

| 대상 | 펌웨어 위치·브랜치 | 보드 없이 확인한 상태 | 강의실에서 확인할 것 |
|---|---|---|---|
| STM32 #1 메인 유닛 | `stm32/main_unit`, `feature/stm32` | 2026-10-03 재빌드·경고 0개, PC 파서 24/24 통과 | USB 명령, Pan/Tilt 보정, 레이저, PIR |
| STM32 #2 서랍 | `stm32/drawer`, `feature/stm32` | 2026-10-03 재빌드·경고 0개, PC 모의 테스트 117/117 통과 | USB → HC-06, LED, 서보 밀기·복귀, SOS |
| ESP32 #1 현관등 | `esp32/entrance_node`, `feature/esp32` | 기본·네트워크 활성화 빌드 성공, PC 테스트 80/80 통과 | 한 색 LED, PIR, Wi-Fi/MQTT, 경고·복귀 |
| ESP32 #2 부저 | `esp32/buzzer_tag`, `feature/esp32` | 빌드 성공·경고 0개, PC 테스트 70/70 통과 | BLE, 계속 울림, 끄기, 재연결 |
| Jetson / 웹 연동 | 해당 담당자의 코드 | 별도 구현·검증 필요 | 보드 단독 테스트 후 진행 |

권장 순서: **배선·부품 확인 → 레이저 단독 → 서랍 USB → 서랍 HC-06 → 부저 BLE → 현관등 → Jetson·웹 연동**.

| 연결 | 설정 |
|---|---|
| 메인 유닛 USB VCP (USART2) | 115200, 8N1, 대문자 ASCII, 명령 끝 LF (`\n`) |
| 서랍 USB VCP (USART2, 테스트용) | 115200, 8N1, 같은 시리얼 명령 |
| 서랍 HC-06 (USART1) | 9600, 8N1 가정. 실제 모듈 속도 확인 |
| 부저 BLE | Text/UTF-8 한 글자 `1` / `0`, **줄 끝 없음** |
| 현관등 MQTT | Jetson 브로커 포트 1883, 토픽·값은 5장 참고 |

시리얼은 명령을 보낸 UART로 `OK`/`ERR` 응답을 받는다. 서랍 SOS 이벤트는 USB와 HC-06 양쪽으로 전달한다. 자세한 약속은 [통신 프로토콜](protocol.md)을 참고한다.

## 2. 보드 없이 다시 확인하는 방법

아래 명령은 저장소 루트의 PowerShell에서, **표에 적힌 완료 펌웨어가 있는 브랜치·체크아웃 환경**에서 실행한다. STM32 재검증은 `feature/stm32`, ESP32 재검증은 `feature/esp32` 코드로 한다. 통합 문서가 양쪽에 있어도 펌웨어까지 합쳐진 것은 아니다.

### STM32 (`feature/stm32`)

```powershell
cmake --build stm32/drawer/build/Debug
cmake --build stm32/main_unit/build/Debug
.\stm32\drawer\tests\run_host_tests.cmd
```

호스트 테스트 스크립트는 **서랍 테스트와 레이저 파서 테스트를 모두 실행**한다. 설치된 Visual Studio의 MSVC를 사용하며, Developer Command Prompt에서는 현재 `cl`을 사용한다. 그 외에는 현재 PC에서 확인한 VS 18 Insiders 경로를 사용한다. 다른 PC는 개발자 환경에서 실행하거나 스크립트의 VS 경로를 맞춘다. 결과물은 무시되는 `build/host/`에 생성된다.

- 레이저: 파서 24개 검사.
- 서랍: 파서, 두 UART 독립 수신, CR/LF, 32바이트 경계, 긴 줄 이후 복구, 비정상 문자, 큐 초과, UART 오류 이후 복구, 서보 순차 동작, 타이머 오버플로, 버튼 채터링·길게 누름 검사.
- 모의 HAL 테스트는 실제 UART 속도, NVIC, PWM 파형, 전원, 서보 이동을 검증하지 않는다.

### 부저 (`feature/esp32`)

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe" run --project-dir esp32/buzzer_tag --environment esp32c3
& .\esp32\buzzer_tag\tests\run_host_tests.cmd
```

- 플랫폼 `espressif32@7.1.3`, Arduino ESP32 2.0.17 계열의 내장 BLE 라이브러리 사용.
- PC 테스트는 MSVC로 `include/buzzer_control.h`를 실행한다. `cl`이 PATH에 없으면 현재 PC의 VS 18 Insiders 환경을 사용하며, 다른 PC에서는 Visual Studio Developer Command Prompt에서 실행한다.
- 범위: `1`/`0`, 길이·문자 오류, 반복 명령의 상태 유지, 연결 해제에 쓰는 정지 로직, 재연결 후 명령 처리. 시간에 따른 부저 정지 로직은 없다.
- 2026-10-03 확인: release 빌드 성공·경고 0개, PC 테스트 **70/70** 통과 (`/W4 /WX`). FLASH **983,938 / 1,310,720바이트 (75.1%, 기본 앱 파티션)**, 정적 RAM **38,636 / 327,680바이트 (11.8%)**. 실행 중 BLE 동적 메모리는 실물에서 확인한다.
- PC 테스트는 BLE 무선 연결·GPIO·실제 소리 검증을 대신하지 않는다. `.pio/`와 `build/`는 Git에서 제외한다.

### 현관등 (`feature/esp32`)

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe" run --project-dir esp32/entrance_node --environment lolin_d32 --environment lolin_d32_network_check
& .\esp32\entrance_node\tests\run_host_tests.cmd
```

- `lolin_d32`는 실제 보드용, `lolin_d32_network_check`는 예시 설정으로 네트워크 코드까지 컴파일·링크하는 빌드 검증용이다. 보드 없이 실제 MQTT 서버에 접속하는 테스트가 아니다.
- 2026-10-03 확인: 두 빌드 성공·경고 0개, PC 제어 로직 **80/80** 통과 (`/W4 /WX`). PIR 필터·재감지, 점등 유지, ALERT 깜빡임·복귀, 반복 명령, 잘못된 값, 시간 넘침을 검사했다.
- 빈 설정의 기본 빌드: FLASH **269,753 / 1,310,720바이트 (20.6%)**, 정적 RAM **21,504 / 327,680바이트 (6.6%)**.
- 네트워크 활성화 빌드: FLASH **878,049 / 1,310,720바이트 (67.0%)**, 정적 RAM **45,320 / 327,680바이트 (13.8%)**. 실행 중 태스크·Wi-Fi·MQTT 동적 메모리는 실물에서 확인한다.
- 테스트 통과는 PIR 전압·LED 구동·Wi-Fi 연결·실제 MQTT 발행/구독·Last Will 동작 검증을 대신하지 않는다.

## 3. STM32 #1 메인 유닛 (레이저·Pan/Tilt·PIR)

연결: **USB VCP 115200, 8N1**, 명령 끝 LF. 카메라는 고정하고 레이저만 Pan/Tilt로 움직이는 구성이다. 목표 좌표를 각도로 바꾸는 것은 Jetson, 받은 각도를 서보 펄스로 바꾸는 것은 STM32가 담당한다.

| 확인 | 입력·조작 | 예상 결과 |
|---|---|---|
| □ | `PING` | `OK:PING` |
| □ | `AIM:90,45` | `OK:AIM`, Pan 90° / Tilt 45°에 해당하는 PWM |
| □ | `AIM:0,0`, `AIM:180,180` | `OK:AIM`, 보정한 범위 내 이동 |
| □ | `AIM:181,90` | `ERR:AIM:RANGE`, 이전 위치 유지 |
| □ | `aim:90,45` | `ERR:UNKNOWN` |
| □ | `LASER:ON`, `LASER:OFF` | 각각 `OK:LASER`, 출력 ON / OFF |
| □ | `HOME` | `OK:HOME`, Pan/Tilt 90° + 레이저 OFF |
| □ | PIR 입력 HIGH / LOW 유지 | 50ms 필터 후 `EVT:PIR:1` / `EVT:PIR:0` |
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
| □ | □ | `PING` | `OK:PING` |
| □ | □ | `LED:1:ON` | `OK:LED`, 1번만 점등 |
| □ | □ | `LED:6:ON` | `OK:LED`, 1번 OFF / 6번 ON |
| □ | □ | `LED:1:OFF` | `OK:LED`, 6번은 유지 |
| □ | □ | `LED:ALL:OFF` | `OK:LED`, 전체 소등 |
| □ | □ | `DRAWER:OPEN:3` | `OK:DRAWER`, 3번 LED ON, 밀기 → 서보 복귀 → Pulse 0 |
| □ | □ | 동작 중 `DRAWER:OPEN:6` | 이전 LED 즉시 OFF, 6번 LED ON, 이전 서보 복귀 후 6번 동작 |
| □ | □ | 복귀 중 여러 `DRAWER:OPEN` | 마지막 요청 예약, 접수한 요청마다 `OK:DRAWER` |
| □ | □ | 마지막 LED ON 이후 10초 | 자동 소등, 명령 수신 계속 가능 |
| □ | □ | `DRAWER:OPEN:0`, `DRAWER:OPEN:7` | `ERR:DRAWER:RANGE`, 동작 변경 없음 |
| □ | □ | `LED:7:ON` | `ERR:LED:RANGE` |
| □ | □ | 소문자·형식 오류·32바이트 초과 | `ERR:UNKNOWN`, 구동 없음 |
| □ | □ | 짧은 연속 명령 | UART별 8줄 큐로 순서대로 처리, 큐 초과 줄은 실행하지 않고 `ERR:UNKNOWN` |
| □ | □ | SOS 버튼 50ms 이상 누름 | `EVT:BTN:SOS` 한 번, 양쪽 UART에서 확인 |
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
| MQTT | Jetson 브로커 포트 1883, 클라이언트 ID `retrace-entrance` |
| 평소 점등 | PIR HIGH 동안 ON, 안정적인 LOW 감지 후 10초 유지 |
| ALERT | 250ms마다 ON/OFF, 10초 후 기본 센서등 모드 복귀 |

`src/main.cpp` 맨 위의 `LIGHT_HOLD_MS`, `ALERT_HOLD_MS`, `ALERT_BLINK_MS`, `PIR_DEBOUNCE_MS`는 임시값으로 강의실에서 조정한다.

### 5-1. 강의실 네트워크 설정

1. `include/network_config.example.h`를 같은 폴더의 `network_config.h`로 복사한다.
2. `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_HOST`에 강의실 Wi-Fi와 Jetson LAN IP를 입력한다. ESP32에서 `localhost`는 Jetson이 아니다. 필요하면 MQTT 사용자·비밀번호도 입력한다.
3. `lolin_d32` 환경으로 다시 빌드·업로드한다. `network_config.h`는 `.gitignore`로 제외되며 예제만 공유한다.

설정이 비어 있으면 USB 로그에 `Network config empty; local light only`가 나오고 기본 PIR 센서등만 동작한다. 네트워크 연동 테스트를 하려면 설정을 채워야 한다.

### 5-2. 센서등·MQTT 확인

| 확인 | 입력·조작 | 기대 결과 |
|---|---|---|
| □ | 전원 켜기, PIR LOW | LED OFF |
| □ | 설정 없이 PIR HIGH 50ms 유지 | LED ON, 기본 센서등 동작 |
| □ | Wi-Fi·MQTT 연결 | `retrace/entrance/status`에 `online` 게시 |
| □ | 연결 중 PIR HIGH 50ms 유지 | LED ON, `retrace/entrance/motion`에 `1` 한 번 게시 |
| □ | PIR HIGH 계속 유지 | LED 유지, motion 중복 발행 없음 |
| □ | 안정적인 LOW 이후 다시 HIGH | 새 motion 한 번 게시 |
| □ | PIR LOW 50ms 유지 후 10초 경과 | LED OFF |
| □ | `retrace/entrance/light`에 `ALERT` | ON부터 시작, 250ms마다 ON/OFF |
| □ | 경고 시작 후 8초에 다시 `ALERT` | 다시 ON부터 시작, 마지막 명령부터 10초 뒤 기본 모드 |
| □ | `NORMAL` 또는 ALERT 10초 경과 | 기본 센서등 모드 복귀. PIR 조건이 남아 있으면 ON |
| □ | 소문자·공백·줄 끝·Retain 명령 | 무시, 기존 동작 유지 |
| □ | MQTT 재연결 | light 재구독, `online` 다시 게시 |
| □ | 전원 강제 해제·MQTT 연결 유실 | 브로커가 연결 유실을 감지하면 Last Will `offline` 게시 (즉시 감지를 보장하지 않음) |
| □ | MQTT 없이 PIR 감지 | 기본 센서등 기능 유지 |
| □ | MQTT 연결 실패 중 경고 타이머 확인 | 기존 ALERT는 10초 후 정상 복귀, PIR·LED 처리는 계속됨 |

status는 QoS 1·Retain이며 Last Will도 동일하다. motion은 QoS 1·Retain 없음, light 구독은 QoS 1이며 명령은 Retain 없이 보낸다. 오프라인에서 새로 발생한 움직임은 보관하지 않고, 이미 발행 큐에 넣은 QoS 1 메시지는 재전송될 수 있다.

MQTT 테스트 명령은 **Jetson 터미널**에서 실행한다. `localhost`는 Jetson의 브로커를 뜻한다.

```bash
mosquitto_sub -h localhost -t "retrace/#" -v
mosquitto_pub -h localhost -q 1 -t retrace/entrance/light -m ALERT
mosquitto_pub -h localhost -q 1 -t retrace/entrance/light -m NORMAL
```

## 6. ESP32 #2 부저 태그

| 항목 | 값 |
|---|---|
| 보드 | ESP32-C3 Super Mini |
| 부저 제어 | GPIO3, HIGH = ON / LOW = OFF |
| 부저 종류 | 액티브 부저 또는 HIGH 입력으로 켜지는 구동 회로 기준. 실제 종류·전류 확인 필요 |
| 정지 방식 | 시간 제한 없이 유지. 웹의 끄기 요청을 Jetson이 BLE `0`으로 전달하면 OFF. 연결 해제 감지 시에도 OFF |
| BLE 이름 | `RETRACE-TAG-01` |
| 서비스 UUID | `4951013c-1654-43f1-919f-a3ff928dc0b6` |
| Write 특성 UUID | `36b342d0-153b-418e-b5d7-f95af507ec1f` |

핀은 `src/main.cpp` 맨 위의 `BUZZER_PIN`에서 조정한다. 패시브 부저는 HIGH 유지로 지속 음을 내지 못하므로 부품 종류를 먼저 확인한다. 배선·구동 회로는 [핀맵 6장](pinmap.md#6-esp32-2-부저-태그-esp32-c3-super-mini)을 따른다.

PlatformIO 보드 설정은 기존 `esp32-c3-devkitm-1`(ESP32-C3 / 4MB Flash)을 유지한다. 실제 Super Mini의 칩·Flash 용량과 USB 업로드 동작은 실물에서 확인한다.

폰 BLE 테스트 도구에서 이름을 검색하고 연결한 뒤 위 서비스의 **Write 특성**을 선택한다. **Text/UTF-8 한 글자**로 쓰고 줄 끝을 붙이지 않는다. Hex 모드에서는 ON = `31`, OFF = `30`이다. 사용자가 웹을 사용할 때는 이 숫자를 직접 입력하지 않는다.

| 확인 | 입력·조작 | 기대 결과 |
|---|---|---|
| □ | 전원 켜기 | 부저 OFF, `RETRACE-TAG-01` 검색 가능 |
| □ | 연결만 하기 | OFF 유지, 서비스·Write 특성 UUID 일치 |
| □ | Text `1` | GPIO3 HIGH, 부저 ON |
| □ | Text `0` | GPIO3 LOW, 부저 OFF |
| □ | Text `1`, 연결을 유지하며 30초 이상 기다리기 | 10초가 지나도 GPIO3 HIGH, ON 유지. 확인 후 `0`으로 정지 |
| □ | 울리는 동안 다시 `1` | 끊김 없이 ON 유지. `0`을 보내면 OFF |
| □ | 켜진 동안 Text `2`, `10`, 줄 끝 포함 `1\n`, Hex `01`/`00` 쓰기 | 기존 ON 상태 유지 |
| □ | 꺼진 동안 같은 잘못된 값 쓰기 | OFF 유지 |
| □ | 울리는 동안 BLE 연결 해제 | 보드가 해제 이벤트를 감지하면 OFF, 약 0.5초 뒤 다시 검색 가능 |
| □ | 재연결 | OFF 유지, 다시 `1`/`0` 명령 가능 |
| □ | USB 시리얼 모니터 없이 전원만 연결 | 광고·부저 ON/OFF 명령 정상 동작 |
| □ | 울리는 중 전원 재시작 | 부팅 후 OFF, 다시 검색 가능 |

앱에서 Disconnect를 누르면 보통 빠르게 해제 이벤트가 발생한다. 폰 전원을 끄거나 멀리 벗어나면 무선 연결 해제 감지까지 시간이 걸릴 수 있다. 시간에 따른 자동 정지는 없으므로 보드가 해제 이벤트를 감지할 때까지 ON을 유지한다.

USB 로그는 115200이다. `BUZZER ON`, `BUZZER OFF`, `BLE disconnected; buzzer OFF`, `BLE advertising restarted`를 참고한다.

## 7. Jetson·웹 통합 테스트 — 각 기능 연동 후

보드 단독 검증이 끝난 뒤 해당 담당자와 함께 실행한다. 아래는 연동 확인 항목이며 **Jetson·웹 기능 구현 완료를 의미하지 않는다**.

외출 음성 안내는 **Android 폰의 ntfy 앱 + Tasker를 활용하는 계획이며, 구현·실물 검증 전**이다. Jetson의 `PhoneNotifier`와 ntfy 서버·폰 앱을 연결하고, Tasker에서 발신 앱이 ntfy이고 제목이 `Car Key Check`인 알림만 본문 TTS로 읽도록 설정한 뒤 아래 항목을 확인한다. 웹은 검색·제어 화면이며, 음성 안내는 웹 화면과 별도 알림 경로로 처리한다.

| 확인 | 사용자 조작·상황 | 기대 결과 |
|---|---|---|
| □ | 웹에서 물건 위치 안내 | Jetson이 목표 각도를 계산하고 STM32에 `AIM` 전달, 이동 후 레이저 제어 |
| □ | 웹에서 1~6번 서랍 선택 | HC-06을 통해 해당 서랍 LED ON + 서보 밀기·복귀, LED 10초 후 OFF |
| □ | 서랍 SOS 버튼 누르기 | `EVT:BTN:SOS`가 Jetson에 도착하고 폰 알림·사이렌 요청으로 이어짐 |
| □ | 웹에서 부저 **찾기** | Jetson이 BLE `1` 전송, 부저 ON |
| □ | 부저 연결을 유지하며 30초 이상 기다리기 | 자동으로 꺼지지 않고 계속 울림 |
| □ | 웹에서 부저 **소리 끄기** | Jetson이 BLE `0` 전송, 부저 OFF |
| □ | 응답 대기 중 PIR·SOS 이벤트 수신 | Jetson이 이벤트와 `OK`/`ERR` 응답을 구분해 처리 |
| □ | 현관 PIR 감지·ALERT | MQTT 이벤트와 경고 조명이 연결된 시나리오대로 동작 |
| □ | 외출 알림 조건 충족 → Jetson에서 ntfy 전송 | 폰의 ntfy 앱에 제목 `Car Key Check`, 본문 `차키 챙기셨나요?` 알림이 도착하고 Tasker가 본문을 음성으로 읽음 |
| □ | Retrace 웹을 닫고 폰 화면을 잠근 뒤 새 외출 알림 전송 | 웹을 열거나 알림을 누르지 않아도 폰에서 안내 문장이 들림. 폰 모델·OS·필요한 권한·배터리 설정과 실제 결과를 기록 |
| □ | 카톡·문자 등 다른 앱 알림 / ntfy의 다른 제목 알림 수신 | 외출 안내 TTS 규칙이 실행되지 않음. SOS 알림·사이렌은 별도 기존 항목으로 확인 |
| □ | 같은 외출 알림의 중복 수신·알림 갱신 | 같은 알림을 반복해서 읽지 않음. 중복 판별 기준과 억제 시간은 Jetson·폰 담당자와 정해 기록하고, 새로운 외출 알림은 정상적으로 읽는지 확인 |

## 8. 강의실 결과 기록

실측한 펄스·시간과 실패한 입력을 기록하고, 수정 후 해당 항목을 다시 확인한다.

| 보드 | 날짜·테스터 | 업로드한 브랜치·커밋 | 통과 / 미통과 항목 | 확정한 값·수정 사항 |
|---|---|---|---|---|
| 레이저 | | | | |
| 서랍 USB·HC-06 | | | | |
| 현관등 | | | | |
| 부저 BLE | | | | |
| Jetson·웹 연동 | | | | |

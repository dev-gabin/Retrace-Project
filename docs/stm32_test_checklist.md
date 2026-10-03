# STM32 강의실 테스트 체크리스트

현재는 보드 없이 빌드와 PC 모의 테스트를 확인한 상태입니다. 아래 항목은 실물 확인 후 체크합니다. Jetson 연동은 별도 단계입니다.

## PC에서 다시 확인

저장소 루트의 PowerShell에서:

```powershell
cmake --build stm32/drawer/build/Debug
cmake --build stm32/laser_head/build/Debug
.\stm32\drawer\tests\run_host_tests.cmd
```

호스트 테스트는 설치된 Visual Studio의 MSVC를 사용합니다. Developer Command Prompt 환경에서는 현재 `cl`을 사용하고, 그 외에는 현재 PC에서 확인한 VS 18 Insiders 경로를 사용합니다. 다른 PC는 개발자 환경에서 실행하거나 스크립트의 VS 경로를 맞춥니다. 추가 패키지 설치는 필요 없습니다. 결과물은 무시되는 `build/host/` 안에 생성됩니다.

- 레이저 헤드: 기존 파서 24개 검사.
- 서랍: 파서, 두 UART 독립 수신, CR/LF, 32바이트 경계, 긴 줄 이후 복구, 비정상 문자, 큐 초과, UART 오류 이후 복구, 서보 순차 동작, 타이머 오버플로, 버튼 채터링/길게 누름 검사.
- 모의 HAL 테스트는 실제 UART 속도, NVIC, PWM 파형, 전원, 서보 이동을 검증하지 않습니다.

## STM32 #1 레이저 헤드

USB VCP: **115200, 8N1**, 명령 끝 LF (`\n`).

| 입력 / 조작 | 예상 결과 |
|---|---|
| `PING` | `OK:PING` |
| `AIM:90,45` | `OK:AIM`, Pan 90° / Tilt 45°에 해당하는 PWM |
| `AIM:0,0`, `AIM:180,180` | `OK:AIM`, 보정한 범위 내 이동 |
| `AIM:181,90` | `ERR:AIM:RANGE`, 이전 위치 유지 |
| `aim:90,45` | `ERR:UNKNOWN` |
| `LASER:ON`, `LASER:OFF` | 각각 `OK:LASER`, 출력 ON / OFF |
| `HOME` | `OK:HOME`, Pan/Tilt 90° + 레이저 OFF |
| PIR 입력 HIGH / LOW 유지 | 50ms 필터 후 `EVT:PIR:1` / `EVT:PIR:0` |
| B1 버튼 | PIR 이벤트 오발행 없음 |

- [ ] 실제 SG90 범위에 맞춰 `pan_tilt.c`의 최소/최대 펄스 보정.
- [ ] 레이저 구동 회로와 PIR 모델/출력 전압 확인 후 연결.
- [ ] 보드 업로드, USB 응답, PWM 파형, 실제 이동 확인.

## STM32 #2 서랍

첫 테스트는 **USB VCP 115200, 8N1**, 이후 **HC-06 USART1 9600, 8N1**로 같은 명령을 반복합니다. HC-06 실제 속도는 모듈에서 확인합니다. 응답은 입력한 UART로, SOS 이벤트는 양쪽으로 전송됩니다.

### 서보 보정 먼저

장착 위치는 **1~6번 각 서랍 뒤에 서보 1개씩 (총 6개)**으로 확정했습니다. 아래 보정은 장착 위치가 아니라 서보를 밀고 복귀시키는 펄스 값에 대한 확인입니다.

`stm32/drawer/Core/Src/drawer.c` 상단의 값은 **기구에서 확인하지 않은 임시값**입니다.

| 상수 | 현재값 | 확인할 내용 |
|---|---|---|
| `SERVO_RETURN_PULSE_US` | 1000µs | 밀지 않는 복귀 위치 |
| `SERVO_PUSH_PULSE_US` | 2000µs | 안전하게 밀어내는 위치 |
| `SERVO_PUSH_MS` | 500ms | 밀어내기 유지 시간 |
| `SERVO_RETURN_MS` | 500ms | 복귀 완료에 필요한 시간 |
| `LED_TIMEOUT_MS` | 10000ms | LED 표시 시간 |

부팅은 모든 채널 Pulse 0으로 유지합니다. 기구와 서보 1개부터 분리해 위치/방향을 확인하고 값을 맞춘 뒤 팝업 테스트를 진행합니다. 외부 5V 전원과 STM32의 GND를 공통 연결합니다. 펌웨어는 한 채널씩 신호를 주지만 실제 전류와 복귀 완료 여부는 측정해야 합니다. 현재 동일한 펄스를 6개에 사용하므로 각 서보에서 동일한 값으로 안전하게 동작하는지 확인합니다.

| 입력 / 조작 | 예상 결과 |
|---|---|
| 전원 ON | LED OFF, 서보 Pulse 0 |
| `PING` | `OK:PING` |
| `LED:1:ON` | `OK:LED`, 1번만 점등 |
| `LED:6:ON` | `OK:LED`, 1번 OFF / 6번 ON |
| `LED:1:OFF` | `OK:LED`, 6번은 유지 |
| `LED:ALL:OFF` | `OK:LED`, 전체 소등 |
| `DRAWER:OPEN:3` | `OK:DRAWER`, 3번 LED ON, 밀기 → 복귀 → Pulse 0 |
| 동작 중 `DRAWER:OPEN:6` | 이전 LED 즉시 OFF, 6번 LED ON, 이전 서보 복귀 후 6번 동작 |
| 복귀 중 여러 `DRAWER:OPEN` | 마지막 요청을 예약, 접수한 요청마다 `OK:DRAWER` |
| 마지막 LED ON 이후 10초 | 자동 소등, 명령 수신 계속 가능 |
| `DRAWER:OPEN:0`, `DRAWER:OPEN:7` | `ERR:DRAWER:RANGE`, 동작 변경 없음 |
| `LED:7:ON` | `ERR:LED:RANGE` |
| 소문자/형식 오류/32바이트 초과 | `ERR:UNKNOWN`, 구동 없음 |
| 짧은 연속 명령 | UART별 8줄 큐로 순서대로 처리, 큐 초과 줄은 실행하지 않고 `ERR:UNKNOWN` |
| SOS 버튼 50ms 이상 누름 | `EVT:BTN:SOS` 한 번, 양쪽 UART에서 확인 |
| SOS 길게 누름 / 떼고 다시 누름 | 유지 중 추가 이벤트 없음 / 안정적인 해제 후 재누름 이벤트 한 번 |

- [ ] 서랍 번호 1~6과 실제 배선/기구 위치 일치 확인.
- [ ] USB IRQ가 실제 동작하고 양쪽 UART 명령이 섞이지 않는지 확인.
- [ ] HC-06 연결 및 실제 baud rate 확인.
- [ ] 서보 보정, 순차 동작, LED 소등, 버튼 채터링 실물 확인.
- [ ] Pulse 0 후 서보가 복귀 위치를 유지하는지 확인.

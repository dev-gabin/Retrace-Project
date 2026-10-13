# Retrace 핀맵

> STM32 두 대와 ESP32 두 대의 현재 핀 배정·구동 설정입니다. 소스 확인 기준 2026-10-13.
> 메시지는 [protocol.md](protocol.md), 검증 결과는 [classroom_test_checklist.md](classroom_test_checklist.md)를 참고합니다.

| 보드 | 역할 | 프로젝트 | 연결 |
|---|---|---|---|
| STM32 #1 NUCLEO-F411RE | 레이저·Pan/Tilt·PIR① | `stm32/main_unit` | USB VCP 115200 |
| STM32 #2 NUCLEO-F411RE | PCA9685·서랍 6개·LED·SOS | `stm32/drawer` | HC-06 UART 9600 |
| ESP32 #1 LOLIN D32 | PIR②·현관 센서등 | `esp32/entrance_node` | Wi-Fi TCP |
| ESP32 #2 ESP32-C3 Super Mini | I2C 1602 LCD | `esp32/preparation_display` | Wi-Fi HTTP |

Jetson은 USB 웹캠·STM32 VCP·Bluetooth 동글을 사용하며 직접 GPIO로 서보를 제어하지 않습니다.

---

## 1. STM32 공통

두 STM32 보드 모두 같은 보드·같은 설정 기준을 사용합니다.

### 1-1. 보드 기본 설정 (두 보드 모두 유지)

> 표의 **헤더 핀**(D0~D15, A0~A5)은 NUCLEO 보드에 인쇄된 핀 이름입니다. 배선할 때 보드에서 바로 찾을 수 있습니다.  
> `CN7`, `CN10`은 보드 양옆의 긴 핀 줄입니다.

| 핀 | 용도 | 비고 |
|---|---|---|
| PA2, PA3 | USART2 → ST-LINK VCP | #1은 Jetson 통신, #2는 PC 명령·응답 테스트용. 헤더 D0/D1에는 기본적으로 연결되지 않음 |
| PA13, PA14, PB3 | SWD (TMS, TCK, SWO) | 업로드·디버깅용 |
| PA5 (D13) | LD2 녹색 LED | 디버깅용으로 사용 가능 |
| PC13 | B1 파란 버튼 (EXTI13) | 보드 기본 설정 유지 |
| PC14, PC15 / PH0, PH1 | 외부 크리스털 (LSE / HSE) | |

### 1-2. 클럭

| 항목 | 값 |
|---|---|
| 클럭 소스 | HSI 16MHz → PLL |
| SYSCLK / HCLK | 84 MHz |
| APB1 peripheral clock | 42 MHz |
| **APB1 timer clock** | **84 MHz** (메인 유닛 TIM3 기준 클럭) |

### 1-3. STM32 직접 구동 서보 PWM 계산

> 이 설정은 메인 유닛의 Pan/Tilt 서보에 적용합니다. 서랍 노드는 STM32 타이머 대신 I2C2로 연결한 PCA9685를 사용합니다.

| 항목 | 값 |
|---|---|
| Prescaler (PSC) | **83** |
| Counter Period (ARR) | **19999** |
| auto-reload preload | Enable |
| PWM Mode | PWM mode 1 |
| Output compare preload | Enable |
| CH Polarity | High |

```text
PWM 주파수 = 타이머 클럭 / ((PSC + 1) × (ARR + 1))

84 MHz ÷ (83 + 1) = 1 MHz    → 카운터 1 증가 = 1µs
1µs × (19999 + 1) = 20 ms    → 50Hz (서보 주기)
```

**Pulse 값 의미** (1 = 1µs)

| Pulse | 펄스 폭 | 서보 위치 (일반적) |
|---|---|---|
| 0 | PWM HIGH 신호 없음 | 위치 유지·움직임은 서보 실물에서 확인 |
| 700 | 0.7 ms | 현재 코드의 0° |
| 1500 | 1.5 ms | 현재 코드의 90° |
| 2300 | 2.3 ms | 현재 코드의 180° |

- SG90은 제품마다 실제 동작 범위가 다를 수 있음 → 보드 테스트로 최소/최대값 확인 필요.

### 1-4. Project Manager

| 항목 | 값 |
|---|---|
| Toolchain / IDE | CMake |
| Firmware Package | STM32Cube FW_F4 **V1.28.3** (두 보드 동일, 최신 버전으로 마이그레이션하지 않음) |
| 주변장치 초기화 코드 위치 | `main.c` 안에 생성 (별도 `tim.c`, `usart.c` 파일 생성 안 함) |

> CubeMX에서 `.ioc`를 열 때 버전 마이그레이션 창이 뜨면 **Continue** 선택.  
> `Migrate to the latest supported Firmware version` 버튼은 누르지 않음 (팀원·강의실 환경 호환 유지).

### 1-5. 빌드 환경

| 항목 | 내용 |
|---|---|
| 에디터 | VS Code + **STM32CubeIDE for Visual Studio Code** 확장 팩 |
| 도구(번들) | 각 프로젝트 `.settings/bundles-lock.store.json`에 고정된 버전 사용 (CMake 4.3.1, Ninja 1.13.2, gnu-tools-for-stm32 14.3.1 — 두 프로젝트 동일) |
| 열 폴더 | `stm32/main_unit` 또는 `stm32/drawer` (각각 따로 열기) |
| 빌드 | `F7` (CMake: Build), 프리셋 `Debug` |
| 성공 기준 | 출력 탭 `CMake/빌드` → `빌드가 완료됨(종료 코드: 0)` |

- 빌드 결과물 `build/`는 각 프로젝트의 `.gitignore`로 제외됨.
- 줄바꿈 규칙은 최상위 `.gitattributes`(`* text=auto`)로 고정.

> **새 PC에서 빌드가 안 될 때** (`arm-none-eabi-gcc ... was not found in the PATH`)
> 1. 확장 **팩**(STM32CubeIDE for Visual Studio Code)이 설치됐는지 확인 — 일부 확장만 있으면 CMake가 컴파일러 위치를 모름
> 2. 프로젝트 폴더에서 VS Code 터미널로 `cube bundle install --project` (잠금 파일 기준으로 도구 설치)
> 3. `build/` 폴더 삭제 → `Developer: Reload Window` → STM32Cube 프로젝트 구성 **예** → `F7`

### 1-6. 공통 배선

| 항목 | 내용 |
|---|---|
| 서보 전원 | **외부 5V** 사용 (보드 5V에서 직접 X) |
| GND | 외부 5V 전원 GND와 NUCLEO GND **공통 연결** (필수) |

---

## 2. STM32 #1 메인 유닛 (레이저·Pan/Tilt·PIR)

### 2-1. 담당 기능

| 기능 | 설명 |
|---|---|
| Jetson 통신 | ST-LINK USB Serial(VCP), 명령 수신 / 이벤트 송신 (통신 프로토콜의 메인 유닛 절) |
| Pan/Tilt 서보 | SG90 ×2, 레이저 조준 |
| 레이저 | KY-008, ON/OFF |
| PIR | 공간 내 움직임 감지 → Jetson에 이벤트 전달 |

### 2-2. 핀 배정 (확정)

| 기능 | MCU 핀 | 헤더 핀 | 주변장치 | User Label |
|---|---|---|---|---|
| Jetson 통신 TX | PA2 | (D1, 기본 미연결) | USART2_TX | - |
| Jetson 통신 RX | PA3 | (D0, 기본 미연결) | USART2_RX | - |
| Pan 서보 | PA6 | D12 | TIM3_CH1 (PWM) | `SERVO_PAN` |
| Tilt 서보 | PA7 | D11 | TIM3_CH2 (PWM) | `SERVO_TILT` |
| 레이저 | PA8 | D7 | GPIO_Output | `LASER_EN` |
| PIR | PA10 | D2 | GPIO_EXTI10 | `PIR_IN` |

### 2-3. CubeMX 설정

**USART2 (Jetson 통신)**

| 항목 | 값 |
|---|---|
| Mode | Asynchronous |
| Baud Rate | **115200** |
| Word Length / Parity / Stop Bits | 8 / None / 1 |
| Data Direction | Receive and Transmit |
| NVIC | USART2 global interrupt **Enable** |

**TIM3 (Pan/Tilt)** — 1-3 공통 설정 + 아래

| 항목 | 값 |
|---|---|
| Channel1 / Channel2 | PWM Generation CH1 (PA6) / CH2 (PA7) |
| Pulse (CH1, CH2) | **1500** (중앙에서 시작) |

**PA8 (레이저)**

| 항목 | 값 |
|---|---|
| Mode | GPIO_Output |
| Output level | Low (부팅 시 레이저 OFF) |
| GPIO mode | Output Push Pull |
| Pull-up/Pull-down | No pull |
| Speed | Low |

**PA10 (PIR)**

| 항목 | 값 |
|---|---|
| Mode | GPIO_EXTI10 |
| GPIO mode | External Interrupt, Rising/Falling edge |
| Pull-up/Pull-down | Pull-down |
| NVIC | EXTI line[15:10] interrupts **Enable** |

- Rising = 움직임 감지 시작, Falling = 감지 종료 → 둘 다 받아서 활동 상태 판단에 사용.
- Pull-down: 센서가 빠졌을 때 입력이 떠서 오작동하는 것 방지.

> **주의: PA10(PIR)과 PC13(B1 버튼)은 같은 인터럽트(`EXTI15_10_IRQn`)를 공유함.**  
> 콜백(`HAL_GPIO_EXTI_Callback`)에서 `GPIO_Pin` 값으로 `PIR_IN_Pin`인지 `B1_Pin`인지 구분해서 처리해야 함.

### 2-4. 생성 코드에서 사용하는 이름

`Core/Inc/main.h`에 자동 생성됨.

| 매크로 | 값 |
|---|---|
| `SERVO_PAN_Pin` / `SERVO_PAN_GPIO_Port` | `GPIO_PIN_6` / `GPIOA` |
| `SERVO_TILT_Pin` / `SERVO_TILT_GPIO_Port` | `GPIO_PIN_7` / `GPIOA` |
| `LASER_EN_Pin` / `LASER_EN_GPIO_Port` | `GPIO_PIN_8` / `GPIOA` |
| `PIR_IN_Pin` / `PIR_IN_GPIO_Port` | `GPIO_PIN_10` / `GPIOA` |
| `PIR_IN_EXTI_IRQn` | `EXTI15_10_IRQn` |

### 2-5. 배선 메모

| 항목 | 내용 | 상태 |
|---|---|---|
| 레이저 | GPIO 직결 대신 트랜지스터 스위칭 검토 | 확인 필요 (모듈 전류) |
| PIR | 전원 5V, 출력 3.3V면 PA10 직결 가능 | 확인 필요 (모델) |

### 2-6. 현재 구동 값

- `pan_tilt.c`: 0~180°를 700~2300µs로 변환, TIM3 50Hz.
- MCU `HOME`: 90°/90°·레이저 OFF. Python 안내 후 복귀는 기본 85°/42°로 별도 설정.
- PIR①: 50ms 안정화 후 `EVT@PIR:1` / `EVT@PIR:0`.
- 각도 설정 응답은 실제 기구 위치를 센서로 측정한 결과가 아닙니다. 설치 환경의 축 방향·범위·보정은 실측 기록과 함께 관리합니다.

---

## 3. STM32 #2 서랍 노드

> 현재 핀 배정은 `drawer.ioc`와 아래 표를 기준으로 합니다. 통신 메시지는 `protocol.md` 기준입니다.

### 3-1. 담당 기능

| 기능 | 설명 |
|---|---|
| 서랍 팝업 서보 | SG90 ×6, PCA9685의 CH0~CH5로 제어 |
| 서랍 LED | LED ×6, 열린 서랍 표시 (타이머 자동 소등) |
| 비상 버튼 | `EVT@DRAWER:SOS` → C TCP 서버 → Python → ntfy 휴대폰 알림 |
| Jetson 통신 | HC-06 Bluetooth (USART1) (통신 프로토콜의 서랍 절) |
| PC 테스트 | ST-LINK VCP (USART2) — 명령 수신 · 응답 및 SOS 이벤트 |

### 3-2. 서랍 번호

`protocol.md`의 `DRAWER:OPEN:n`, `LED:n:ON`의 **n은 아래 번호**를 따릅니다. 서보·LED 번호도 같습니다.

```text
┌─────────┬─────────┐
│    1    │    2    │   Top
├─────────┼─────────┤
│    3    │    4    │   Middle
├─────────┼─────────┤
│    5    │    6    │   Bottom
└─────────┴─────────┘
   Left      Right
```

### 3-3. 핀 배정

**PCA9685 I2C 연결**

| PCA9685 핀 | STM32 핀 | 헤더 핀 | 설정 |
|---|---|---|---|
| SCL | PB10 | D6 | I2C2_SCL |
| SDA | PB9 | D14 | I2C2_SDA |
| VCC | 3.3V | | 로직 전원 |
| GND | GND | | 공통 접지 |

서보는 **1~6번 각 서랍 뒤에 1개씩** 설치하고, PCA9685 채널도 같은 번호 순서로 사용합니다.

| 서랍 | 위치 | PCA9685 채널 |
|---|---|---|
| 1 | 상단 왼쪽 | CH0 |
| 2 | 상단 오른쪽 | CH1 |
| 3 | 중단 왼쪽 | CH2 |
| 4 | 중단 오른쪽 | CH3 |
| 5 | 하단 왼쪽 | CH4 |
| 6 | 하단 오른쪽 | CH5 |

**서랍 LED**

| 서랍 | 핀 | 헤더 핀 | 설정 | User Label |
|---|---|---|---|---|
| 1 | PA0 | A0 | GPIO_Output | `LED_1` |
| 2 | PA1 | A1 | GPIO_Output | `LED_2` |
| 3 | PA4 | A2 | GPIO_Output | `LED_3` |
| 4 | PB0 | A3 | GPIO_Output | `LED_4` |
| 5 | PC1 | A4 | GPIO_Output | `LED_5` |
| 6 | PC0 | A5 | GPIO_Output | `LED_6` |

**HC-06 Bluetooth**

| HC-06 핀 | STM32 핀 | 헤더 핀 | 설정 |
|---|---|---|---|
| RXD | PA9 | D8 | USART1_TX (`HC06_TX`) |
| TXD | PA10 | D2 | USART1_RX (`HC06_RX`) |
| VCC | 5V | | 전원 |
| GND | GND | | 공통 접지 |

- TX ↔ RX **교차 연결** (STM32 TX → HC-06 RXD)
- STM32는 3.3V 로직이라 **전압 분배 저항 없이** 연결
- 서랍 보드엔 PIR이 없어서 PA10을 USART1_RX로 사용 (메인 유닛 STM32 #1과 다름)

**비상 버튼**

| 부품 핀 | STM32 핀 | 헤더 핀 | 설정 | User Label |
|---|---|---|---|---|
| 버튼 신호 | PB5 | D4 | GPIO_Input, Pull-up | `SOS_BTN` |
| 버튼 반대쪽 | GND | | 공통 접지 | |

- 평소 Pull-up으로 HIGH → 누르면 GND와 연결되어 LOW
- 인터럽트 없이 메인 루프에서 폴링하며 코드에서 50ms 디바운싱

**미사용 핀**

| 핀 | 상태 |
|---|---|
| PB1 | 사용 안 함 |

### 3-4. CubeMX 설정

**I2C2 (PCA9685)**

| 항목 | 값 |
|---|---|
| Mode | I2C |
| Clock Speed | **100000 Hz** (Standard Mode) |
| Addressing Mode | 7-bit |
| SCL / SDA | PB10 / PB9 |
| GPIO | Alternate Function Open Drain, No pull, Very High speed |
| DMA / NVIC | 사용 안 함 (폴링 방식) |
| PCA9685 주소 | 7-bit `0x40` (HAL 전달값 `0x40 << 1`) |
| PCA9685 PWM | **50Hz**, CH0~CH5 사용 |

> TIM3/TIM4는 사용하지 않습니다. 부팅 시 PCA9685 CH0~CH5를 Full OFF로 두고, 복귀·밀어내기 펄스 값은 실물 테스트에서 맞춥니다.

**USART1 (HC-06)**

| 항목 | 값 |
|---|---|
| Mode | Asynchronous |
| Baud Rate | **9600** (HC-06 기본값, 모듈 확인 필요) |
| Word Length / Parity / Stop Bits | 8 / None / 1 |
| NVIC | USART1 global interrupt **Enable** |

**USART2 (디버그)** — 보드 기본값 유지 (115200, VCP)

- USB 명령 테스트를 위해 USART2 NVIC는 `stm32f4xx_hal_msp.c`의 `USER CODE`에서 활성화하고, `stm32f4xx_it.c`의 `USER CODE`에 IRQ 핸들러를 정의합니다. `.ioc`는 변경하지 않았습니다.
- 나중에 CubeMX에서 USART2 global interrupt를 켜면 자동 생성 핸들러와 수동 핸들러가 중복되지 않도록 정리해야 합니다.

**GPIO**

| 핀 | 설정 |
|---|---|
| PA0, PA1, PA4, PB0, PC1, PC0 | GPIO_Output, Output level **Low**(부팅 시 꺼짐), Push Pull, No pull, Speed Low |
| PB5 | GPIO_Input, **Pull-up** (`SOS_BTN`) |
| NVIC | SOS 버튼 및 I2C2 인터럽트 사용 안 함 |

**Project Manager** — 1-4 공통 + Project Name `drawer`, Location `Retrace-Project/stm32/`

### 3-5. 배선 메모

| 항목 | 내용 |
|---|---|
| PCA9685 로직 전원 | VCC는 STM32 **3.3V**에 연결 |
| 서보 전원 | PCA9685 V+에 외부 **5~6V, 2A 이상** 권장 (서보 6개) |
| I2C | PB10(D6) → SCL, PB9(D14) → SDA; 모듈의 풀업 저항 사용 |
| GND | 외부 서보 전원 GND · PCA9685 GND · STM32 GND · HC-06 GND **공통 연결** |
| 서보 동작 | 코드에서 **한 번에 하나씩만** 움직이게 해서 전류 몰림 방지 |
| LED | 핀마다 **전류 제한 저항**(220~330Ω) 직렬 연결 |
| 비상 버튼 | 내부 Pull-up 사용 → 외부 저항 불필요, 채터링은 코드에서 처리 |

### 3-6. 현재 구동 값

| 항목 | 코드 값 |
|---|---|
| 밀기 펄스·유지 | 1400µs·500ms |
| 복귀 펄스·유지 | 1900µs·1000ms |
| PCA9685 주소·출력 | 0x40·50Hz·CH0~CH5 |
| LED 소등 | 마지막 점등부터 10초 |
| SOS 디바운스 | 50ms, active LOW |

동작 중 새 열기 요청은 BUSY로 거절합니다. 완료 뒤 `OK@DRAWER`, I2C 오류 시 `ERR@DRAWER:I2C`를 보냅니다. 복귀 후 해당 채널은 Full OFF이며 서랍을 자동으로 닫는 동작은 아닙니다.

### 3-7. 배선·실측 확인

서랍 정면 기준의 번호, HC-06 실제 UART 속도, PCA9685 주소·I2C 전압, 설치된 서보의 기계 범위와 펄스·유지 시간을 확인합니다. 현재 코드 값을 변경하면 이 문서와 프로토콜을 함께 갱신합니다.

---

## 4. ESP32 공통

### 4-1. 개발 방식

| 항목 | 내용 |
|---|---|
| 개발 도구 | VS Code + **PlatformIO** (STM32와 같은 에디터) |
| 프레임워크 | Arduino 프레임워크 (`setup()` / `loop()`) |
| 설정 파일 | 각 프로젝트의 `platformio.ini` (보드 · 라이브러리 버전) |
| 핀 설정 | CubeMX 같은 설정 화면 없음 → **코드에서 직접** (`pinMode()`) |
| 빌드 결과물 | `.pio/` → `.gitignore`로 제외 |

### 4-2. 피해야 할 핀 (확인 필요 — 보드 문서로 최종 확인)

ESP32는 **전원 켤 때 부팅 방식을 정하는 핀(스트래핑 핀)**이 있어서, 여기에 부품을 달면 부팅이 안 될 수 있습니다.

| 보드 | 피할 핀 | 이유 |
|---|---|---|
| LOLIN D32 | GPIO0, 2, 5, 12, 15 | 부팅 모드 결정 (스트래핑) |
| LOLIN D32 | GPIO6~11 | 내부 플래시 메모리 연결 |
| LOLIN D32 | GPIO34~39 | **입력 전용** (출력 불가, 내부 풀업·풀다운 없음) |
| ESP32-C3 | GPIO2, 8, 9 | 부팅 모드 결정 (스트래핑) |
| ESP32-C3 | GPIO18, 19 | USB 연결 (업로드·시리얼) |
| ESP32-C3 | GPIO20, 21 | UART0 (기본 시리얼) |

---

## 5. ESP32 #1 현관등 (LOLIN D32)

### 5-1. 담당 기능

| 기능 | 설명 |
|---|---|
| 외출 감지 | PIR 움직임 → TCP `EVT@ENTRANCE:PIRON` |
| 현관등 | 평소 센서등 점등 / Jetson `ALERT` 명령 시 경고 점등 |
| Jetson 통신 | Wi-Fi · 인증 TCP client (통신 프로토콜의 현관 TCP 절) |

### 5-2. 현재 핀 배정

| 부품 | 핀 | 설정 | 코드 상수 |
|---|---|---|---|
| PIR 출력 | **GPIO34** | `INPUT` | `PIR_PIN` |
| 현관등 LED | **GPIO25** | `OUTPUT`, 초기 LOW, 한 색 LED | `LIGHT_PIN` |

- **PIR → GPIO34**: 입력 전용 핀이라 센서 입력에 적합. 내부 풀다운이 없으므로 센서가 빠졌을 때 오작동이 걱정되면 **외부 10kΩ 풀다운** 추가
- **현관등 LED**: GPIO25로 제어. 저항 내장 모듈이 아니면 220~330Ω 전류 제한 저항을 직렬 연결.

- PIR 50ms 필터, 움직임 종료 후 7초 점등 유지. `ALERT`는 250ms마다 ON/OFF하며 10초 후 기본 센서등 모드로 복귀.
- Wi-Fi·TCP가 없어도 기본 센서등은 동작합니다. 실제 연결·전원·점등 결과는 검증 기록으로 관리합니다.

### 5-3. 전원·통신

- 사용자 시연 구성: **18650 배터리 두 개** 사용. 직렬·병렬 방식과 변환 회로는 실물 배선 기록을 기준으로 하며 문서만으로 추정하지 않습니다.
- PIR 출력과 LED 구동은 위 GPIO34/25를 사용합니다. 센서 모델의 공급 전압과 출력 전압, 보드의 실제 전원 입력 규격을 함께 확인합니다.
- `network_config.example.h`를 개인 `network_config.h`로 복사해 Wi-Fi·Jetson TCP 주소·로그인 계정을 설정합니다. 예제 기본 TCP 포트는 5000입니다.
- 예제 설정이 비어 있으면 기본 센서등만 동작합니다. 개인 자격 증명은 Git에 올리지 않습니다.

---

## 6. ESP32 #2 LCD (ESP32-C3 Super Mini)

### 6-1. 역할·핀

4핀 I2C 1602 LCD를 연결해 최근 목격 물건과 시각, 준비물·날씨를 표시합니다. HTTP 조회 전용 노드입니다.

| 연결 | C3 핀·설정 | 코드 설정 |
|---|---|---|
| LCD SDA | GPIO4 | `LCD_SDA_PIN` |
| LCD SCL | GPIO5 | `LCD_SCL_PIN` |
| LCD GND | 공통 GND | 실물 배선 |
| LCD I2C 주소 | 기본 0x27 | `LCD_ADDRESS` |
| 표시 크기 | 16열×2행 | `lcd.begin(16, 2, Wire)` |
| 진단 시리얼 | 115200 | PlatformIO monitor |

LCD 백팩의 전원·I2C 풀업 전압을 확인합니다. C3 GPIO는 3.3V 기준이며 5V I2C 풀업을 직접 연결하지 않습니다. 5V 백팩이면 양방향 I2C 레벨 변환을 적용하고 GND를 공통으로 연결합니다. 대비는 LCD 백팩의 가변저항으로 조절합니다.

### 6-2. 설정·조회 주기

`display_config.example.h`를 개인 `display_config.h`로 복사합니다. Wi-Fi와 `RETRACE_API_BASE=http://<Jetson-IP>:8080`을 설정하며, API 포트를 바꿨다면 함께 맞춥니다. C3의 localhost는 Jetson이 아닙니다.

| 항목 | 기본값 |
|---|---|
| `/api/items` 목격 조회 | 10초 |
| `/api/leave-items` 준비물 조회 | 15초 |
| 화면 순환 | 5초 이상 |
| 새 목격 우선 표시 | 5초 |
| 날씨 조회·실패 재시도 | 10분·30초 |
| 준비물 표시 시간 | 한국 시간 07:00 이상, 08:00 미만 |

일반 1602 LCD는 영문 이름을 표시합니다. 최근 사진·시각이 있는 물건 최대 3개를 보여주며 조회 실패 시 캐시·오프라인 상태를 표시합니다. 자세한 날씨·인증서·표시 규칙은 [LCD README](../esp32/preparation_display/README.md)를 참고합니다.

### 6-3. 빌드

PlatformIO 환경 이름은 `esp32c3`, 보드 설정 ID는 `esp32-c3-devkitm-1`, 실제 사용 제품은 ESP32-C3 Super Mini입니다. 프로젝트 폴더는 `esp32/preparation_display`입니다.

---

## 7. 전체 실측 기록

핀 배정은 `.ioc`·`main.h`·각 ESP32의 코드 및 설정 예제를 기준으로 합니다. 서보 범위, 레이저·PIR 모델, 공급 전압, HC-06 실제 속도, I2C 주소, 통신 유실 시 동작은 실제 장치에서 확인하고 [통합 체크리스트](classroom_test_checklist.md)에 결과를 기록합니다.

# Retrace 핀맵

> Retrace 프로젝트의 MCU 보드 4개(STM32 ×2, ESP32 ×2) 핀 배정과 설정 정리.  
> 메시지 형식은 [protocol.md](protocol.md)를 참고합니다. Jetson Nano는 USB 장치만 연결하므로 핀맵 대상이 아닙니다.

| 보드 | 역할 | 프로젝트 폴더 | Jetson 연결 | 상태 |
|---|---|---|---|---|
| **STM32 #1** (NUCLEO-F411RE) | 레이저 헤드 (Pan/Tilt 서보, 레이저, PIR) | `stm32/laser_head` | USB Serial (VCP) | CubeMX·빌드 완료 / 보드 동작 확인 전 |
| **STM32 #2** (NUCLEO-F411RE) | 서랍 노드 (서보 ×6, LED ×6, 비상 버튼) | `stm32/drawer` | Bluetooth (HC-06) | CubeMX 핀 설정 완료 / 코드 생성·빌드 확인 전 |
| **ESP32 #1** (LOLIN D32) | 현관등 (PIR, LED) | `esp32/entrance_node` | Wi-Fi · MQTT | 핀 초안 |
| **ESP32 #2** (ESP32-C3 Super Mini) | 부저 태그 (부저) | `esp32/buzzer_tag` | BLE | 핀 초안 |

---

## 1. STM32 공통

두 STM32 보드 모두 같은 보드·같은 설정 기준을 사용합니다.

### 1-1. 보드 기본 설정 (두 보드 모두 유지)

> 표의 **헤더 핀**(D0~D15, A0~A5)은 NUCLEO 보드에 인쇄된 핀 이름입니다. 배선할 때 보드에서 바로 찾을 수 있습니다.  
> `CN7`, `CN10`은 보드 양옆의 긴 핀 줄입니다.

| 핀 | 용도 | 비고 |
|---|---|---|
| PA2, PA3 | USART2 → ST-LINK VCP | #1은 Jetson 통신, #2는 PC 디버그 로그용. 헤더 D0/D1에는 기본적으로 연결되지 않음 |
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
| **APB1 timer clock** | **84 MHz** (TIM3, TIM4 기준 클럭) |

### 1-3. 서보 PWM 공통 계산

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
| 0 | 신호 없음 | 움직이지 않음 |
| 1000 | 1.0 ms | 한쪽 끝 |
| 1500 | 1.5 ms | 중앙 |
| 2000 | 2.0 ms | 반대쪽 끝 |

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
| 도구(번들) | 각 프로젝트 `.settings/bundles-lock.store.json`에 고정된 버전 사용 (CMake, Ninja, gnu-tools-for-stm32 14.3.1) |
| 열 폴더 | `stm32/laser_head` 또는 `stm32/drawer` (각각 따로 열기) |
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

## 2. STM32 #1 레이저 헤드

### 2-1. 담당 기능

| 기능 | 설명 |
|---|---|
| Jetson 통신 | ST-LINK USB Serial(VCP), 명령 수신 / 이벤트 송신 (`protocol.md` 2장) |
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

### 2-6. 진행 상황

- [x] CubeMX 핀 배정 (충돌 없음)
- [x] Clock 확인 → APB1 timer clock 84MHz, PSC 83 확정
- [x] 코드 생성 및 라벨·타이머 값 반영 확인
- [x] VS Code 빌드 성공 (RAM 1,728 B / 1.32%, FLASH 13,712 B / 2.62%)
- [ ] 레이저 모듈 모델 및 전류 확인
- [ ] PIR 센서 모델 및 출력 전압 확인
- [ ] 보드 업로드 및 동작 확인
- [ ] SG90 실제 동작 범위(Pulse 최소/최대) 측정

---

## 3. STM32 #2 서랍 노드

> 핀 배정 원본: 짝꿍 작성 핀맵 (핀 변경 없음). 세부 값은 1장 공통 설정과 `protocol.md` 기준.

### 3-1. 담당 기능

| 기능 | 설명 |
|---|---|
| 서랍 팝업 서보 | SG90 ×6, 서랍마다 1개 |
| 서랍 LED | LED ×6, 열린 서랍 표시 (타이머 자동 소등) |
| 비상 버튼 | 폰 사이렌 요청 (`EVT:BTN:SOS`) |
| Jetson 통신 | HC-06 Bluetooth (USART1) (`protocol.md` 3장) |
| 디버그 | ST-LINK VCP (USART2) — PC 로그용 |

### 3-2. 서랍 번호

`protocol.md`의 `DRAWER:OPEN:n`, `LED:n:ON`의 **n은 아래 번호**를 따릅니다. 서보·LED 번호도 같습니다.

```text
┌─────────┬─────────┬─────────┐
│    1    │    2    │    3    │   Top
├─────────┼─────────┼─────────┤
│    4    │    5    │    6    │   Bottom
└─────────┴─────────┴─────────┘
   Left      Middle    Right
```

### 3-3. 핀 배정

**서보 PWM** (상단 = TIM3, 하단 = TIM4)

| 서랍 | 위치 | 핀 | 헤더 핀 | 타이머 채널 | User Label |
|---|---|---|---|---|---|
| 1 | 상단 왼쪽 | PC6 | - (CN10) | TIM3_CH1 | `SERVO_1` |
| 2 | 상단 가운데 | PC7 | D9 | TIM3_CH2 | `SERVO_2` |
| 3 | 상단 오른쪽 | PC8 | - (CN10) | TIM3_CH3 | `SERVO_3` |
| 4 | 하단 왼쪽 | PB6 | D10 | TIM4_CH1 | `SERVO_4` |
| 5 | 하단 가운데 | PB7 | - (CN7) | TIM4_CH2 | `SERVO_5` |
| 6 | 하단 오른쪽 | PB8 | D15 | TIM4_CH3 | `SERVO_6` |

**서랍 LED**

| 서랍 | 핀 | 헤더 핀 | 설정 | User Label |
|---|---|---|---|---|
| 1 | PC0 | A5 | GPIO_Output | `LED_1` |
| 2 | PC1 | A4 | GPIO_Output | `LED_2` |
| 3 | PC2 | - (CN7) | GPIO_Output | `LED_3` |
| 4 | PC3 | - (CN7) | GPIO_Output | `LED_4` |
| 5 | PC4 | - (CN10) | GPIO_Output | `LED_5` |
| 6 | PC5 | - (CN10) | GPIO_Output | `LED_6` |

**HC-06 Bluetooth**

| HC-06 핀 | STM32 핀 | 헤더 핀 | 설정 |
|---|---|---|---|
| RXD | PA9 | D8 | USART1_TX (`HC06_TX`) |
| TXD | PA10 | D2 | USART1_RX (`HC06_RX`) |
| VCC | 5V | | 전원 |
| GND | GND | | 공통 접지 |

- TX ↔ RX **교차 연결** (STM32 TX → HC-06 RXD)
- STM32는 3.3V 로직이라 **전압 분배 저항 없이** 연결
- 서랍 보드엔 PIR이 없어서 PA10을 USART1_RX로 사용 (레이저 헤드와 다름)

**비상 버튼**

| 부품 핀 | STM32 핀 | 헤더 핀 | 설정 | User Label |
|---|---|---|---|---|
| 버튼 신호 | PA4 | A2 | GPIO_EXTI4, Pull-up, Falling edge | `SOS_BTN` |
| 버튼 반대쪽 | GND | | 공통 접지 | |

- 평소 Pull-up으로 HIGH → 누르면 GND와 연결되어 LOW (Falling edge 감지)
- EXTI4는 **전용 인터럽트**(`EXTI4_IRQn`)라 다른 핀과 공유하지 않음

**미사용 핀**

| 핀 | 상태 |
|---|---|
| PB1 | 사용 안 함 |

### 3-4. CubeMX 설정

**TIM3 · TIM4 (서보)** — 1-3 공통 설정 + 아래

| 항목 | 값 |
|---|---|
| Channel 1~3 | PWM Generation CH1 / CH2 / CH3 (Channel 4 Disable) |
| Pulse (CH1~3) | **0** (신호 없음 → 전원 켜도 서보가 움직이지 않음) |

> 레이저 헤드는 시작 Pulse 1500(중앙)이지만, 서랍 서보는 **"서랍을 밀지 않는 위치"**를 아직 모르기 때문에 0으로 시작합니다. 기구 조립 후 닫힘 위치 값을 정해 코드에서 넣습니다.

**USART1 (HC-06)**

| 항목 | 값 |
|---|---|
| Mode | Asynchronous |
| Baud Rate | **9600** (HC-06 기본값, 모듈 확인 필요) |
| Word Length / Parity / Stop Bits | 8 / None / 1 |
| NVIC | USART1 global interrupt **Enable** |

**USART2 (디버그)** — 보드 기본값 유지 (115200, VCP)

**GPIO**

| 핀 | 설정 |
|---|---|
| PC0~PC5 | GPIO_Output, Output level **Low**(부팅 시 꺼짐), Push Pull, No pull, Speed Low |
| PA4 | GPIO_EXTI4, External Interrupt Falling edge, **Pull-up** |
| NVIC | **EXTI line4 interrupt Enable** |

**Project Manager** — 1-4 공통 + Project Name `drawer`, Location `Retrace-Project/stm32/`

### 3-5. 배선 메모

| 항목 | 내용 |
|---|---|
| 서보 전원 | 외부 5V **2A 이상** 권장 (서보 6개) |
| GND | 외부 5V 전원 GND · STM32 GND · HC-06 GND **공통 연결** |
| 서보 동작 | 코드에서 **한 번에 하나씩만** 움직이게 해서 전류 몰림 방지 |
| LED | 핀마다 **전류 제한 저항**(220~330Ω) 직렬 연결 |
| 비상 버튼 | 내부 Pull-up 사용 → 외부 저항 불필요, 채터링은 코드에서 처리 |

### 3-6. 진행 상황

- [x] 프로젝트 생성 (`drawer.ioc`, NUCLEO-F411RE, 보드 기본 설정)
- [x] 클럭 확인 → APB1 timer clock 84MHz, PSC 83 확정
- [x] TIM3: PC6/PC7/PC8 = `SERVO_1~3`, Pulse 0
- [x] TIM4: PB6/PB7/PB8 = `SERVO_4~6`, Pulse 0
- [x] USART1: PA9/PA10 = `HC06_TX`/`HC06_RX`, 9600, NVIC
- [x] GPIO: PC0~PC5 = `LED_1~6`
- [x] PA4 = `SOS_BTN` (EXTI4), NVIC
- [ ] Project Manager → 코드 생성 → 빌드 확인

### 3-7. 확인 필요 항목

- [ ] 서랍 실제 배치 (3열 × 2행 맞는지, 위 번호 그림과 일치하는지)
- [ ] HC-06 통신 속도 (9600 기본값인지)
- [ ] 서보 시작 위치(닫힘) / 밀어내는 위치 Pulse 값
- [ ] PB1 미사용 이유 기록

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
| 외출 감지 | PIR 움직임 → MQTT `retrace/entrance/motion` |
| 현관등 | 평소 센서등 점등 / Jetson `ALERT` 명령 시 경고 점등 |
| Jetson 통신 | Wi-Fi · MQTT (`protocol.md` 4장) |

### 5-2. 핀 배정 (초안)

| 부품 | 핀 | 설정 | 코드 상수 |
|---|---|---|---|
| PIR 출력 | **GPIO34** | `INPUT` | `PIR_PIN` |
| 현관등 LED | **미정** | LED 종류 확정 후 | `LIGHT_PIN` |

- **PIR → GPIO34**: 입력 전용 핀이라 센서 입력에 적합. 내부 풀다운이 없으므로 센서가 빠졌을 때 오작동이 걱정되면 **외부 10kΩ 풀다운** 추가
- **현관등 LED 후보**
  - 한 색 LED → GPIO25 (출력, 전류 제한 저항 필요. 밝은 LED·여러 개면 트랜지스터로 구동)
  - 색 바뀌는 RGB(WS2812 등) → GPIO27 (데이터선 1개)

### 5-3. 배선 메모

| 항목 | 내용 | 상태 |
|---|---|---|
| 전원 | USB 5V 어댑터 (시연용) | |
| PIR 전원 | HC-SR501은 5V 필요, 출력은 3.3V라 GPIO 직결 가능 | 확인 필요 (모델) |
| 배터리 | 배터리로만 쓰면 5V가 없을 수 있음 → 3.3V용 PIR(AM312 등) 검토 | 시연은 USB 전원 |

---

## 6. ESP32 #2 부저 태그 (ESP32-C3 Super Mini)

### 6-1. 담당 기능

| 기능 | 설명 |
|---|---|
| 부저 | Jetson BLE 신호(`1`/`0`) 수신 → 부저 ON/OFF, 일정 시간 후 자동 정지 |
| Jetson 통신 | BLE (`protocol.md` 5장) |

### 6-2. 핀 배정 (초안)

| 부품 | 핀 | 설정 | 코드 상수 |
|---|---|---|---|
| 부저 | **GPIO3** | `OUTPUT`, 초기 LOW | `BUZZER_PIN` |

- 예비 후보: GPIO4

### 6-3. 배선 메모

| 항목 | 내용 | 상태 |
|---|---|---|
| 부저 종류 | **액티브 부저** (전압만 주면 울림) 권장 | 확인 필요 |
| 구동 | 부저 전류가 크면 **NPN 트랜지스터**(예: S8050 + 베이스 저항 1kΩ)로 구동 | 확인 필요 (부저 전류) |
| 전원 | 태그라 배터리 필요 (소형 리튬 배터리 등) | 확인 필요 |
| 납땜 | 보드가 **핀헤더 미납땜** 제품 → 납땜 필요 | |

---

## 7. 전체 확인 필요 항목

- [ ] STM32 #1: 레이저 모듈 전류, SG90 동작 범위
- [ ] STM32 #2: 서랍 실제 배치, HC-06 통신 속도, 서보 닫힘 위치, PB1 미사용 이유
- [ ] ESP32 #1: 현관등 LED 종류(한 색 / RGB)
- [ ] ESP32 #2: 부저 종류·전류, 배터리

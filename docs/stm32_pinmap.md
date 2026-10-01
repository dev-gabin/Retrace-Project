# STM32 핀맵 (NUCLEO-F411RE)

> Retrace 프로젝트의 STM32 담당 기능(PIR, Pan/Tilt 서보, 레이저, Jetson 통신) 핀 배정과 CubeMX 설정 정리.
>
> 상태: **CubeMX 설정·코드 생성·빌드 확인 완료** / 보드 동작 확인 전

## 1. 담당 기능

| 기능 | 설명 |
|---|---|
| Jetson 통신 | ST-LINK USB Serial(VCP), 명령 수신 / 이벤트 송신 |
| Pan/Tilt 서보 | SG90 ×2, 레이저 조준 |
| 레이저 | KY-008, ON/OFF |
| PIR | 공간 내 움직임 감지 → Jetson에 이벤트 전달 |

## 2. 핀 배정 (확정)

| 기능 | MCU 핀 | Arduino 헤더 | 주변장치 | User Label |
|---|---|---|---|---|
| Jetson 통신 TX | PA2 | (D1, 기본 미연결) | USART2_TX | - |
| Jetson 통신 RX | PA3 | (D0, 기본 미연결) | USART2_RX | - |
| Pan 서보 | PA6 | D12 | TIM3_CH1 (PWM) | `SERVO_PAN` |
| Tilt 서보 | PA7 | D11 | TIM3_CH2 (PWM) | `SERVO_TILT` |
| 레이저 | PA8 | D7 | GPIO_Output | `LASER_EN` |
| PIR | PA10 | D2 | GPIO_EXTI10 | `PIR_IN` |

### 보드 기본 설정 (유지)

| 핀 | 용도 | 비고 |
|---|---|---|
| PA2, PA3 | USART2 → ST-LINK VCP | 헤더 D0/D1에는 기본적으로 연결되지 않음 |
| PA13, PA14, PB3 | SWD (TMS, TCK, SWO) | 업로드·디버깅용 |
| PA5 (D13) | LD2 녹색 LED | 디버깅용으로 사용 가능 |
| PC13 | B1 파란 버튼 (EXTI13) | PIR과 같은 인터럽트 공유 (아래 참고) |
| PC14, PC15 / PH0, PH1 | 외부 크리스털 (LSE / HSE) | |

## 3. CubeMX 설정

### 3-1. 클럭

| 항목 | 값 |
|---|---|
| 클럭 소스 | HSI 16MHz → PLL |
| SYSCLK / HCLK | 84 MHz |
| APB1 peripheral clock | 42 MHz |
| **APB1 timer clock** | **84 MHz** (TIM3 기준 클럭) |

### 3-2. USART2 (Jetson 통신)

| 항목 | 값 |
|---|---|
| Mode | Asynchronous |
| Baud Rate | 115200 |
| Word Length / Parity / Stop Bits | 8 / None / 1 |
| Data Direction | Receive and Transmit |
| NVIC | USART2 global interrupt **Enable** |

- Jetson 쪽도 반드시 같은 baud rate(115200)로 포트를 열어야 함.

### 3-3. TIM3 (Pan/Tilt 서보 PWM)

| 항목 | 값 |
|---|---|
| Clock Source | Internal Clock |
| Channel1 / Channel2 | PWM Generation CH1 (PA6) / CH2 (PA7) |
| Prescaler (PSC) | **83** |
| Counter Mode | Up |
| Counter Period (ARR) | **19999** |
| auto-reload preload | Enable |
| PWM Mode | PWM mode 1 |
| Pulse (CH1, CH2) | **1500** (중앙) |
| Output compare preload | Enable |
| CH Polarity | High |

**계산**

```text
PWM 주파수 = 타이머 클럭 / ((PSC + 1) × (ARR + 1))

84 MHz ÷ (83 + 1) = 1 MHz    → 카운터 1 증가 = 1µs
1µs × (19999 + 1) = 20 ms    → 50Hz (서보 주기)
```

**Pulse 값 의미** (1 = 1µs)

| Pulse | 펄스 폭 | 서보 위치 (일반적) |
|---|---|---|
| 1000 | 1.0 ms | 한쪽 끝 |
| 1500 | 1.5 ms | 중앙 |
| 2000 | 2.0 ms | 반대쪽 끝 |

- SG90은 제품마다 실제 동작 범위가 다를 수 있음 → 보드 테스트로 최소/최대값 확인 필요.

### 3-4. PA8 (레이저)

| 항목 | 값 |
|---|---|
| Mode | GPIO_Output |
| Output level | Low (부팅 시 레이저 OFF) |
| GPIO mode | Output Push Pull |
| Pull-up/Pull-down | No pull |
| Speed | Low |
| User Label | `LASER_EN` |

### 3-5. PA10 (PIR)

| 항목 | 값 |
|---|---|
| Mode | GPIO_EXTI10 |
| GPIO mode | External Interrupt, Rising/Falling edge |
| Pull-up/Pull-down | Pull-down |
| User Label | `PIR_IN` |
| NVIC | EXTI line[15:10] interrupts **Enable** |

- Rising = 움직임 감지 시작, Falling = 감지 종료 → 둘 다 받아서 활동 상태 판단에 사용.
- Pull-down: 센서가 빠졌을 때 입력이 떠서 오작동하는 것 방지.

> **주의: PA10(PIR)과 PC13(B1 버튼)은 같은 인터럽트(`EXTI15_10_IRQn`)를 공유함.**
> 콜백(`HAL_GPIO_EXTI_Callback`)에서 `GPIO_Pin` 값으로 `PIR_IN_Pin`인지 `B1_Pin`인지 구분해서 처리해야 함.

### 3-6. Project Manager

| 항목 | 값 |
|---|---|
| Toolchain / IDE | CMake |
| Firmware Package | STM32Cube FW_F4 **V1.28.3** (최신 버전으로 마이그레이션하지 않음) |
| 주변장치 초기화 코드 위치 | `main.c` 안에 생성 (별도 `tim.c`, `usart.c` 파일 생성 안 함) |

> CubeMX에서 `.ioc`를 열 때 버전 마이그레이션 창이 뜨면 **Continue** 선택.
> `Migrate to the latest supported Firmware version` 버튼은 누르지 않음 (팀원·강의실 환경 호환 유지).

## 4. 생성 코드에서 사용하는 이름

`Core/Inc/main.h`에 자동 생성됨.

| 매크로 | 값 |
|---|---|
| `SERVO_PAN_Pin` / `SERVO_PAN_GPIO_Port` | `GPIO_PIN_6` / `GPIOA` |
| `SERVO_TILT_Pin` / `SERVO_TILT_GPIO_Port` | `GPIO_PIN_7` / `GPIOA` |
| `LASER_EN_Pin` / `LASER_EN_GPIO_Port` | `GPIO_PIN_8` / `GPIOA` |
| `PIR_IN_Pin` / `PIR_IN_GPIO_Port` | `GPIO_PIN_10` / `GPIOA` |
| `PIR_IN_EXTI_IRQn` | `EXTI15_10_IRQn` |

## 5. 빌드 환경

| 항목 | 내용 |
|---|---|
| 에디터 | VS Code + STM32Cube for VS Code 확장 |
| 열 폴더 | `Retrace-Project/stm32` (확장이 CMake 프로젝트로 인식) |
| 빌드 | `F7` (CMake: Build), 프리셋 `Debug` |
| 성공 기준 | 출력 탭 `CMake/빌드` → `빌드가 완료됨(종료 코드: 0)` |
| 현재 사용량 | RAM 1,728 B / 128 KB (1.32%), FLASH 13,712 B / 512 KB (2.62%) |

- 빌드 결과물 `stm32/build/`는 `stm32/.gitignore`로 제외됨.
- 줄바꿈 규칙은 최상위 `.gitattributes`(`* text=auto`)로 고정.

## 6. 배선 메모

| 항목 | 내용 | 상태 |
|---|---|---|
| 서보 전원 | 외부 5V 사용 (보드 5V에서 직접 X) | 구성도 기준 |
| GND | 외부 5V 전원 GND와 NUCLEO GND 공통 연결 | 필수 |
| 레이저 | GPIO 직결 대신 트랜지스터 스위칭 검토 | 확인 필요 (모듈 전류) |
| PIR | 전원 5V, 출력 3.3V면 PA10 직결 가능 | 확인 필요 (모델) |

## 7. 진행 상황

- [x] CubeMX 핀 배정 (충돌 없음)
- [x] Clock 확인 → APB1 timer clock 84MHz, PSC 83 확정
- [x] 코드 생성 및 라벨·타이머 값 반영 확인
- [x] VS Code 빌드 성공
- [ ] 레이저 모듈 모델 및 전류 확인
- [ ] PIR 센서 모델 및 출력 전압 확인
- [ ] 보드 업로드 및 동작 확인
- [ ] SG90 실제 동작 범위(Pulse 최소/최대) 측정

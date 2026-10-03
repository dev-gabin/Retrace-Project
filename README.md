# Retrace

> 스마트 공간 블랙박스 기반 분실물 위치 기억 및 탐색 시스템

Retrace는 카메라를 이용해 공간 속 물건을 지속적으로 관찰하고,  
물건이 마지막으로 확인된 **위치·시간·이미지**를 기록하여 분실 시 빠르게 찾을 수 있도록 돕는 IoT 시스템입니다.

단순히 현재 물건을 탐지하는 것이 아니라, 물건이 시야에서 사라지더라도  
**마지막 목격 정보(Last Seen)** 를 기억하고 복원하는 것을 핵심 목표로 합니다.

**허브(Jetson Nano) + 노드** 구조로, 카메라 기반 비주얼 메모리를 중심으로  
**레이저 안내**, **스마트 서랍**, **스마트 현관등**, **BLE 부저**, **Web/PWA**를 연동합니다.

---

## 주요 기능

### 1. 물건 인식

- 카메라 영상 입력
- YOLO 기반 객체 탐지
- OpenCV 기반 영상 처리
- 주요 추적 대상
  - 차키
  - 에어팟
  - 안경

### 2. Last Seen 기록

물건이 마지막으로 관찰된 순간을 **스냅샷**으로 저장합니다.  
영상을 계속 녹화하지 않고 필요한 순간만 기록합니다.

- 마지막 위치
- 마지막 관찰 시간
- 마지막 이미지 (스냅샷)
- 서랍 내부에 있는 경우 서랍 ID
- 상태 (보임 / 가림 / 불확실)

### 3. Pan/Tilt 레이저 안내

마지막으로 확인된 위치를 레이저가 직접 가리켜  
사용자가 물건을 쉽게 찾을 수 있도록 합니다.

- 2축 Pan/Tilt 서보
- 레이저 ON/OFF
- 카메라는 고정하고, 가까이 배치한 레이저만 Pan/Tilt 브래킷으로 회전
- Jetson이 목표 좌표를 각도로 계산해 전송하고, STM32는 받은 각도를 PWM 펄스로 변환

### 4. 스마트 서랍

6칸 서랍 내부의 물건을 관리하고 사용자가 해당 서랍을 쉽게 확인할 수 있도록 합니다.

- 서랍 LED ×6
- 해당 서랍 LED 점등
- 서랍 팝업 서보 ×6 (서랍마다 1개, 뒤에서 밀어내는 방식)
- 일정 시간 후 LED 자동 소등 (타이머 방식)
- 새 서랍 요청 시 이전 서랍 LED는 즉시 소등 (항상 하나만 점등)
- STM32 (서랍 노드)에서 제어

> **구현 시 주의**: 타이머는 `HAL_Delay()`가 아니라 `HAL_GetTick()`으로 구현합니다.  
> `HAL_Delay()`를 쓰면 대기하는 동안 메인 루프가 멈춰서 Bluetooth 명령 처리가 늦어집니다.  
> (점등 시각을 기록해 두고, 메인 루프에서 `HAL_GetTick() - 점등 시각 >= 소등 시간`이면 LED를 끄는 방식)  
> 현재 소등 시간은 10초이며 강의실에서 조정합니다. 서보는 밀기 후 원위치로 복귀하며, 서랍 자동 닫힘 기능은 없습니다.

### 5. PIR 활동 감지

메인 유닛의 PIR 센서로 공간 내 움직임을 감지합니다.

- 활동 감지
- 카메라 분석 활성/대기 판단에 활용
- STM32에서 센서 입력 처리

### 6. 비상 스마트폰 찾기

폰을 잃어버려 앱을 쓸 수 없을 때, 서랍에 설치된 물리 버튼으로 **폰에서 사이렌을 울립니다.**

- 비상 버튼 입력
- STM32 (서랍 노드)에서 버튼 감지
- Jetson으로 이벤트 전달
- Jetson의 **ntfy 서버**가 스마트폰 ntfy 앱으로 최고 우선순위 알림 전송
- 앱을 열 때까지 알림음 반복 → **폰 사이렌**

> **왜 ntfy인가**: 웹 페이지(PWA)는 화면이 꺼져 있거나 백그라운드에 있으면 소리를 낼 수 없습니다.  
> ntfy는 Jetson에 직접 설치하는 알림 서버로, 폰과 연결을 유지해 **화면이 꺼진 상태에서도** 알림을 받을 수 있습니다.  
> 외부 서버(Firebase)를 거치지 않고 집 안 네트워크에서 동작합니다.
>
> **폰 설정 (Android)**: ntfy 앱 배터리 최적화 제외 / "최고 우선순위 계속 울리기" 켜기  
> **한계**: 폰이 집 Wi-Fi 범위 안에 있어야 하며, 무음 모드 동작은 기기별로 검증이 필요합니다.

### 7. Web / PWA

스마트폰 또는 PC에서 앱 설치 없이 Retrace를 사용할 수 있는 인터페이스입니다.

- 물건 검색
- Last Seen 정보 확인
- 마지막 이미지 확인
- 레이저 안내 요청
- 스마트 서랍 제어
- BLE 부저 호출

### 8. BLE 부저

중요 물건에 BLE 부저 태그를 부착하여, 카메라가 보지 못하는 곳(사각지대)에서도 소리로 찾을 수 있도록 합니다.

BLE는 **위치 추정 용도로 사용하지 않습니다.**

- RSSI 거리 계산 X
- BLE 수신 노드를 이용한 위치 추정 X
- 중요 물건의 부저 호출 용도로만 사용
- BLE `1` 수신 후 시간 제한 없이 울림, `0` 또는 연결 해제 감지 시 정지
- 웹의 찾기·끄기 요청을 BLE로 전달하는 부분은 Jetson·웹 연동에서 확인

물건의 위치 탐색은 기본적으로 **카메라 Last Seen + 레이저 + 스마트 서랍**을 이용합니다.

### 9. 스마트 현관등 · 외출 알림

출입 노드(현관등)가 외출을 감지하고, 중요 물건을 두고 나가면 알려줍니다.

- 평소: 일반 현관 센서등처럼 동작 (PIR 감지 시 점등)
- 외출 감지 시 Jetson이 차키 Last Seen 확인
- 차키가 아직 책상에 있으면 → **현관등 경고 점등 + 레이저로 차키 안내 + 폰 알림(ntfy)** ("차키 챙기셨나요?")
- 기본 점등은 현관등이 스스로 처리하고, 경고만 Jetson이 명령 (Jetson이 꺼져도 기본 조명은 동작)

**폰 음성 안내 계획 (Android, 구현·실물 검증 전)**: 폰의 ntfy 앱이 알림을 수신하면, Tasker에서 **발신 앱이 ntfy이고 제목이 `Car Key Check`인 알림만** 선택해 본문을 TTS(문자를 음성으로 읽기)로 재생합니다. 카톡·문자 등 다른 앱의 알림은 이 규칙의 음성 읽기 대상에서 제외합니다.

Retrace 웹은 검색·제어 화면으로 사용하고, 음성 안내는 **Jetson → ntfy 앱 → Tasker**의 별도 경로로 처리할 계획입니다. 폰 앱을 직접 개발하지 않고 기존 ntfy·Tasker 앱을 활용합니다. Jetson의 `PhoneNotifier` 구현과 폰 설정은 아직 연결되지 않았으며, 잠금 화면·백그라운드 동작과 중복 알림 읽기 방지는 실제 폰에서 검증해야 합니다.

---

## 시스템 구성

| 노드 | 장치 | 역할 |
|---|---|---|
| **메인 유닛 (허브)** | Jetson Nano + 카메라 + STM32 | AI 객체 탐지, Last Seen, DB/이미지 저장, Web 서버, MQTT 브로커, 레이저 안내, PIR |
| **서랍 노드** | STM32 NUCLEO-F411RE + HC-06 | 서랍 LED ×6, 서랍 팝업 서보 ×6, 비상 버튼 |
| **출입 노드 (스마트 현관등)** | LOLIN D32 (ESP32) + PIR + LED | 외출 감지, 현관등 점등·경고 |
| **부저 태그** | ESP32-C3 + 부저 | 중요 물건 부저 호출 |
| **사용자 화면** | Web / PWA | 검색 및 시스템 제어 UI |

---

## 사용 기술

- Jetson Nano / Linux
- C / C++
- YOLO / OpenCV
- STM32 NUCLEO-F411RE ×2 (STM32CubeMX / CMake)
- ESP32 (LOLIN D32, ESP32-C3) / PlatformIO
- Wi-Fi / MQTT (Mosquitto)
- ntfy (폰 푸시 알림)
- Bluetooth (HC-06) / BLE
- GitHub / Jira

---

## 시스템 구조

```text
┌────────────── Main Unit (Hub) ──────────────┐
│                                             │
│  [ USB Camera ]                             │
│        │ USB                                │
│        v                                    │
│  [ Jetson Nano ]   Vision / Last Seen / DB  │
│        │           Web Server / MQTT Broker │
│        │ USB Serial                         │
│        v                                    │
│  [ STM32 Laser Head ]                       │
│     Pan/Tilt / Laser / PIR                  │
│                                             │
└────┬───────────┬───────────┬───────────┬────┘
     │           │           │           │
 Bluetooth     Wi-Fi        BLE        Wi-Fi
  (HC-06)     (MQTT)                  (HTTP)
     │           │           │           │
┌────┴────┐ ┌────┴────┐ ┌────┴────┐ ┌────┴────┐
│  Drawer │ │ Entrance│ │  Buzzer │ │ Web/PWA │
│   Node  │ │   Node  │ │   Tag   │ │         │
│ STM32 #2│ │LOLIN D32│ │ ESP32-C3│ │ Phone/PC│
│         │ │         │ │         │ │         │
│  LED x6 │ │   PIR   │ │  Buzzer │ │         │
│ Servo x6│ │   LED   │ │         │ │         │
│ SOS Btn │ │         │ │         │ │         │
└─────────┘ └─────────┘ └─────────┘ └─────────┘
```

| 노드 | 설명 |
|---|---|
| Main Unit (Hub) | Jetson Nano + 카메라 + STM32 #1 레이저 헤드를 한 몸체로 구성 |
| Drawer Node | 스마트 서랍 + 비상 폰 찾기(사이렌) 버튼 (STM32 #2 + HC-06) |
| Entrance Node | 스마트 현관등 (LOLIN D32 + PIR + LED) |
| Buzzer Tag | BLE 부저 태그 (ESP32-C3) |
| Web/PWA | 사용자 화면 (스마트폰 / PC), 폰 사이렌은 ntfy 앱으로 수신 |

Jetson Nano가 허브로서 판단·기록·서비스를 담당하고,  
각 노드는 맡은 센서 및 구동 장치를 제어하며 허브와 통신합니다.

---

## 통신 구조

설치 위치와 용도에 따라 통신 방식을 나눴습니다.

| 연결 | 방식 | 선택 이유 |
|---|---|---|
| Jetson ↔ 카메라 | USB | 메인 유닛 내부 |
| Jetson ↔ STM32 | USB Serial (ST-LINK VCP) | 메인 유닛 내부, 카메라·레이저 위치 고정 |
| Jetson ↔ 서랍 노드 | **Bluetooth** (HC-06) | 같은 방 안 가구 — 근거리, 공유기 불필요 |
| Jetson ↔ 출입 노드 | **Wi-Fi** (MQTT) | 다른 공간(현관) — 공유기 경유 |
| Jetson ↔ 부저 태그 | **BLE** | 물건에 부착되는 저전력 장치 |
| Jetson ↔ Web / PWA | **Wi-Fi** (HTTP) | 사용자 화면 |
| Jetson → 스마트폰 알림 | **Wi-Fi** (ntfy) | 화면이 꺼진 폰도 받아야 하는 알림 (폰 사이렌, 외출 알림) |

> **같은 방 = Bluetooth / 다른 공간 = Wi-Fi / 물건에 붙는 장치 = BLE**

Jetson의 Bluetooth·BLE는 USB Bluetooth 4.0 동글(CSR8510)을 사용합니다.

### MQTT 토픽

Wi-Fi 노드는 Jetson의 MQTT 브로커(Mosquitto)를 통해 이벤트와 명령을 주고받습니다.  
새 노드는 Wi-Fi + MQTT 토픽 구독만으로 추가할 수 있습니다.

| 토픽 | 방향 | 내용 |
|---|---|---|
| `retrace/entrance/motion` | 출입 노드 → Jetson | 움직임 감지 |
| `retrace/entrance/light` | Jetson → 출입 노드 | `NORMAL` / `ALERT` |

> 메시지 세부 형식(연결 상태 토픽 포함)은 [통신 프로토콜](docs/protocol.md)을 참고합니다.

---

## 하드웨어 역할

### 메인 유닛 – Jetson Nano

시스템의 허브(중앙 처리 장치)입니다.

담당 기능:

- 카메라 영상 입력
- YOLO 객체 탐지 / OpenCV 영상 처리
- Last Seen 판단 및 기록
- DB / 이미지 저장
- Web/PWA 서버
- MQTT 브로커
- ntfy 알림 서버 (폰 사이렌, 외출 알림)
- STM32 통신 (USB Serial)
- 서랍 노드 통신 (Bluetooth)
- 부저 태그 통신 (BLE)

---

### 메인 유닛 – STM32 #1 (NUCLEO-F411RE)

고정 카메라 옆에 배치하고, 레이저만 Pan/Tilt로 움직이는 **레이저 헤드**입니다.

담당 기능:

- Pan / Tilt 서보 제어
- 레이저 ON/OFF
- PIR 센서 입력
- Jetson과 통신 (USB Serial)

```text
STM32
├─ Pan Servo
├─ Tilt Servo
├─ Laser
└─ PIR
```

핀 배정은 [핀맵](docs/pinmap.md)을 참고합니다.

---

### 서랍 노드 – STM32 #2 (NUCLEO-F411RE)

**스마트 서랍**을 담당합니다.

담당 기능:

- 서랍 LED ×6 제어
- 서랍 팝업 서보 ×6 제어
- 비상 버튼 입력
- Jetson과 통신 (HC-06 Bluetooth)

```text
STM32 #2
├─ Drawer LED ×6
├─ Drawer Popup Servo ×6
├─ Emergency Button
└─ HC-06 (Bluetooth)
```

> STM32는 3.3V 로직이라 HC-06과 전압 분배 저항 없이 바로 연결합니다.  
> 서보 6개는 외부 5V 전원을 사용하고, GND는 STM32와 공통으로 연결합니다.

---

### 출입 노드 – LOLIN D32 (ESP32)

**스마트 현관등**입니다.

담당 기능:

- PIR로 외출 감지
- 현관등 점등 (평소) / 경고 점등 (물건 두고 나갈 때)
- Jetson과 통신 (Wi-Fi, MQTT)

```text
LOLIN D32
├─ PIR
└─ LED (현관등)
```

---

### 부저 태그 – ESP32-C3

중요 물건에 부착하는 **BLE 부저 태그**입니다.

담당 기능:

- Jetson의 BLE 신호 수신
- 부저 울림

---

## 데이터 흐름

### 물건 감지

```text
Camera
   ↓
Jetson Nano (YOLO / OpenCV)
   ↓
객체 탐지
   ↓
Last Seen 갱신
   ↓
DB + 스냅샷 저장
```

### 레이저 위치 안내

```text
Web / PWA
   ↓ HTTP
Jetson Nano
   ↓ USB Serial
STM32
   ↓
Pan/Tilt 이동 → Laser ON
```

### 스마트 서랍

```text
Web / PWA
   ↓ HTTP
Jetson Nano
   ↓ Bluetooth
STM32 #2 (서랍)
   ↓
해당 서랍 LED ON → Drawer Servo 동작
   ↓
일정 시간 후 LED 자동 OFF
```

### PIR 활동 감지

```text
PIR
 ↓
STM32
 ↓ USB Serial
Jetson Nano
 ↓
분석 활성 / 대기 판단
```

### 비상 버튼

```text
Emergency Button
   ↓
STM32 #2 (서랍)
   ↓ Bluetooth
Jetson Nano (ntfy 서버)
   ↓ Wi-Fi
스마트폰 ntfy 앱
   ↓
폰 사이렌 (알림음 반복)
```

### 외출 알림 (스마트 현관등, 연동 계획)

```text
출입 노드 PIR 감지
   ↓ MQTT (retrace/entrance/motion)
Jetson Nano
   ↓
차키 Last Seen 확인 → 아직 책상에 있음
   ├─ MQTT (retrace/entrance/light: ALERT) → 현관등 경고 점등
   ├─ USB Serial → STM32 레이저로 차키 안내
   └─ ntfy → 폰의 ntfy 앱에 "차키 챙기셨나요?" 알림
                 ↓
          Tasker: ntfy 앱 + Car Key Check 제목인 알림만 선택
                 ↓
          폰 TTS → "차키 챙기셨나요?" 음성 안내
```

### BLE 부저

```text
Web / PWA
   ↓ HTTP
Jetson Nano
   ↓ BLE
부저 태그 (ESP32-C3)
   ↓
부저 울림
```

---

## 프로젝트 구조

```text
Retrace-Project/
│
├─ esp32/                            # ESP32 노드 (VS Code + PlatformIO)
│  ├─ entrance_node/                 # 출입 노드 · 스마트 현관등 (LOLIN D32)
│  │  ├─ platformio.ini              # 보드 · 라이브러리 설정
│  │  ├─ include/entrance_control.h   # PIR 필터 · 센서등 · 경고 타이머
│  │  ├─ include/network_config.example.h # Wi-Fi · Jetson 주소 설정 예제
│  │  ├─ src/main.cpp                # PIR GPIO34 · LED GPIO25 · Wi-Fi · MQTT
│  │  └─ tests/                      # PC 제어 로직 · 네트워크 빌드 검증
│  └─ buzzer_tag/                    # 부저 태그 (ESP32-C3)
│     ├─ platformio.ini
│     ├─ include/buzzer_control.h    # ON/OFF 명령 (PC 테스트 가능)
│     ├─ src/main.cpp                # BLE 수신 · GPIO3 부저
│     └─ tests/                      # MSVC로 실행하는 PC 로직 테스트
│
├─ jetson_nano/                      # 허브 (C/C++)
│  ├─ main.cpp
│  ├─ CMakeLists.txt
│  ├─ vision/
│  │  └─ Detector.h/.cpp             # 객체 탐지 · 영상 처리
│  ├─ record/
│  │  └─ LastSeen.h/.cpp             # 마지막 목격 정보 생성 · 관리
│  ├─ storage/
│  │  ├─ Database.h/.cpp             # DB 저장
│  │  └─ ImageStorage.h/.cpp         # 스냅샷 저장
│  ├─ communication/
│  │  ├─ Stm32Link.h/.cpp            # USB Serial → AIM · 레이저 명령 / ← PIR 이벤트
│  │  ├─ DrawerLink.h/.cpp           # Bluetooth → 서랍 명령 / ← 비상 버튼 이벤트
│  │  ├─ MqttLink.h/.cpp             # MQTT ↔ 출입 노드 (움직임 · 현관등)
│  │  ├─ BleBuzzer.h/.cpp            # BLE → 부저 태그 호출
│  │  └─ PhoneNotifier.h/.cpp        # ntfy → 폰 사이렌 · 외출 알림
│  └─ server/
│     └─ Server.h/.cpp               # Web/PWA 요청 처리
│
├─ stm32/                            # STM32 프로젝트 묶음
│  ├─ laser_head/                    # STM32 #1 레이저 헤드 (CubeMX + CMake)
│  │  ├─ Core/
│  │  │  ├─ Inc/                     # 헤더 (아래 Src와 짝)
│  │  │  └─ Src/
│  │  │     ├─ main.c                # CubeMX 생성 (초기화 · 메인 루프)
│  │  │     ├─ serial_cmd.c          # UART 인터럽트 수신 · 응답
│  │  │     ├─ cmd_parser.c          # HAL 독립 명령 해석 (PC 테스트 가능)
│  │  │     ├─ pir_sensor.c          # PIR 입력 처리
│  │  │     ├─ pan_tilt.c            # Pan/Tilt 서보 PWM
│  │  │     ├─ laser.c               # 레이저 ON/OFF
│  │  │     └─ ...                   # 그 외 CubeMX 생성 파일
│  │  ├─ tests/                      # PC 파서 테스트
│  │  ├─ Drivers/                    # HAL · CMSIS (CubeMX 생성)
│  │  ├─ cmake/
│  │  ├─ CMakeLists.txt
│  │  ├─ CMakePresets.json
│  │  ├─ Retrace_STM32.ioc           # CubeMX 설정
│  │  ├─ startup_stm32f411xe.s
│  │  └─ STM32F411xx_FLASH.ld
│  │
│  └─ drawer/                        # STM32 #2 서랍 노드 (CubeMX + CMake)
│     ├─ Core/
│     │  ├─ Inc/                     # 아래 Src와 짝인 헤더
│     │  └─ Src/
│     │     ├─ main.c                # CubeMX 생성 (초기화 · 메인 루프)
│     │     ├─ drawer.c              # 서랍 LED ×6 · 팝업 서보 ×6
│     │     ├─ emergency_button.c    # 비상 버튼 입력
│     │     ├─ serial_cmd.c          # HC-06 · USB 수신 큐 및 응답
│     │     ├─ cmd_parser.c          # HAL 독립 서랍 명령 해석
│     │     └─ ...                   # 그 외 CubeMX 생성 파일
│     ├─ tests/                      # 두 STM32 보드 PC 테스트 실행
│     └─ ...                         # CubeMX 생성 (Drivers, cmake, .ioc 등)
│
├─ web/                              # feature/web: 정적 웹 뼈대 (기본 샘플 모드)
│  ├─ index.html                     # 물건 찾기 · 서랍 · 부저 화면
│  ├─ styles.css                     # PC · 휴대폰 레이아웃
│  ├─ app.js                         # 검색 · 선택 · 요청 · 오류 표시
│  ├─ api.js                         # 기존 HTTP 경로 · 응답 검사 · 시간 초과
│  ├─ config.js                      # 샘플/실제 모드 · 서버 주소
│  ├─ demo-data.js                   # 샘플 기록 (실제 요청 없음)
│  ├─ assets/demo-scene.svg           # 샘플 장면
│  ├─ dev-server.mjs                 # 로컬 정적 미리보기 서버
│  ├─ tests/api.test.mjs              # API 계약 · 실패 처리 검증
│  └─ README.md                      # 실행 · Jetson 연결 인계
├─ docs/                             # 회로도 · 구성도 · 개발 문서
├─ .github/
│  └─ CODEOWNERS
├─ CONTRIBUTING.md                   # 협업·검증·통합 순서
├─ .gitattributes
├─ .gitignore
└─ README.md
```

> 위 구조는 각 담당 feature의 코드 기준입니다. 아직 기능을 develop에 통합하지 않았으므로 한 feature 체크아웃에 다른 feature의 완성 코드가 모두 있는 것은 아닙니다. Jetson 항목은 담당 모듈 구조이며 서버 구현 완료를 뜻하지 않습니다. 웹 뼈대는 `feature/web`에 있고 실제 서버·장치 연동은 대기 중입니다.

---

## 개발 문서

- [핀맵](docs/pinmap.md) — STM32 ×2, ESP32 ×2
- [통신 프로토콜](docs/protocol.md)
- [협업 규칙](CONTRIBUTING.md) — feature 단독 검증 후 develop 통합
- [강의실 통합 테스트](docs/classroom_test_checklist.md) — 레이저 · 서랍 · 현관등 · 부저 · Jetson/웹 연동
- [웹 뼈대 실행·인계](web/README.md) — `feature/web`에서 샘플 화면 확인, API JSON 임시안과 실제 연결 전환

---

## 진행 상태

**2026-10-03 기준: 네 보드의 펌웨어 작성·빌드·PC 테스트 완료. 강의실 실물 검증과 Jetson·웹 통합은 대기 중입니다.**

| 보드 | 완료한 기능 | 빌드·PC 검증 | 남은 확인 |
|---|---|---|---|
| 레이저 STM32 #1 | USB 명령·응답, AIM, LASER, HOME, PIR 이벤트 | 빌드 성공·경고 0개, 파서 24/24 통과 | 업로드, 서보 범위 보정, 레이저·PIR |
| 서랍 STM32 #2 | USB·HC-06 명령, LED, 서보 순차 밀기·복귀, 소등 타이머, SOS | 빌드 성공·경고 0개, 모의 테스트 117/117 통과 | USB → HC-06, 펄스·시간 보정, 배선·실제 구동 |
| 현관등 ESP32 #1 | PIR 센서등, Wi-Fi·MQTT, NORMAL/ALERT, 연결 상태 | 기본·네트워크 활성화 빌드 성공·경고 0개, PC 테스트 80/80 통과 | 네트워크 설정, PIR·LED, 실제 MQTT |
| 부저 ESP32 #2 | BLE 1/0, 계속 울림, 연결 해제 시 정지·재광고 | 빌드 성공·경고 0개, PC 테스트 70/70 통과 | 부저 종류·구동, BLE 연결·소리·재연결 |

빌드·PC 테스트는 실제 UART·PWM·무선 통신·배선·기구 동작 확인을 대신하지 않습니다. 실물에서 문제가 발견되면 담당 feature에서 수정하고 재검증합니다.

웹은 `feature/web`에서 차키·에어팟·안경 검색, Last Seen 상세, 레이저 안내 요청, 2열×3행 서랍 선택, 부저 찾기·끄기 화면을 작성했습니다. 기본값은 샘플 모드입니다. 외부 패키지 설치 없이 `node web/dev-server.mjs`로 실행하며, API 경로는 기존 프로토콜을 유지합니다. JSON 형식은 [프로토콜 7-1](docs/protocol.md)에 명시한 임시안으로 Jetson 담당자와 합의가 필요합니다. Jetson 서버·실물 연결은 아직 확인하지 않았습니다.

웹 API 테스트 30/30 통과, PC·휴대폰 360·390px 화면과 키보드 선택을 확인했습니다. 실제 모드의 서버 부재 오류도 확인했으며, 자세한 범위와 미확인 항목은 [웹 인계 문서](web/README.md)에 정리했습니다.

### 강의실에서 할 일

1. 해당 보드의 완성 코드가 있는 feature로 이동해 업로드 준비: STM32는 `feature/stm32`, ESP32는 `feature/esp32`.
2. 배선·전원·부품을 확인하고 [통합 체크리스트](docs/classroom_test_checklist.md)로 보드별 단독 테스트.
3. 레이저 펄스 범위, 서랍 밀기·복귀 펄스와 시간을 실물에 맞춰 보정. 서랍 LED는 현재 10초 타이머이며 닫힘 감지 기능은 없음.
4. 현관등은 `network_config.example.h`를 `network_config.h`로 복사해 Wi-Fi 정보와 Jetson LAN IP를 입력한 뒤 `lolin_d32`로 재빌드·업로드. 현재 실제 설정 파일은 미작성이며 빈 설정이면 기본 PIR 센서등만 동작.
5. 부저는 액티브 부저 기준으로 BLE `1`/ `0` 테스트. 시간 제한 없이 울리고, `0` 또는 연결 해제 감지 시 정지.

### 통합 순서

**각 feature에서 실물 확인·보정 → feature에서 수정·commit·push → PR로 develop 통합 → Jetson·웹·전체 보드 연동 테스트 → 확인된 버전을 main으로 PR.**

보드 단독 확인에는 PC 시리얼·BLE 테스트 앱·MQTT 테스트 명령을 사용합니다. 웹의 찾기/끄기 요청 전달, 카메라 목표 위치의 각도 계산, SOS 알림 연결은 Jetson·웹 담당자의 구현과 함께 확인합니다. 사용자 담당 펌웨어 코드는 각 feature에 commit·push됐고 같은 원격 feature에서 pull도 확인했습니다. 기능을 develop에 통합하는 작업은 아직 진행하지 않았습니다.

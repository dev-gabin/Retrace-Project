# Retrace

> 스마트 공간 블랙박스 기반 분실물 위치 기억 및 탐색 시스템

Retrace는 카메라를 이용해 공간 속 물건을 지속적으로 관찰하고,  
물건이 마지막으로 확인된 **위치·시간·이미지**를 기록하여 분실 시 빠르게 찾을 수 있도록 돕는 IoT 시스템입니다.

단순히 현재 물건을 탐지하는 것이 아니라, 물건이 시야에서 사라지더라도  
**마지막 목격 정보(Last Seen)** 를 기억하고 복원하는 것을 핵심 목표로 합니다.

**허브(Jetson Nano) + 노드** 구조로, 카메라 기반 비주얼 메모리를 중심으로  
**레이저 안내**, **스마트 서랍**, **스마트 현관등**, **BLE 부저**, **Web/PWA**를 연동합니다.

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
│  [ STM32 Main Unit ]                        │
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
| Main Unit (Hub) | Jetson Nano + 카메라 + STM32 #1 메인 유닛 제어 보드를 한 몸체로 구성 |
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
├─ stm32/                              # STM32 프로젝트 (CubeMX + CMake)
│  ├─ main_unit/                       # STM32 #1 메인 유닛 (Jetson ↔ USB Serial)
│  │  ├─ Retrace_STM32.ioc             # CubeMX 핀·주변장치 설정
│  │  └─ Core/Src/
│  │     ├─ main.c                     # 초기화 · 메인 루프
│  │     ├─ serial_cmd.c               # UART 인터럽트 수신 · 응답
│  │     ├─ cmd_parser.c               # 명령 해석 (PING · AIM · LASER · HOME)
│  │     ├─ pan_tilt.c                 # Pan/Tilt 서보 PWM
│  │     ├─ laser.c                    # 레이저 ON/OFF
│  │     └─ pir_sensor.c               # PIR 감지 → EVT:PIR
│  └─ drawer/                          # STM32 #2 서랍 (Jetson ↔ HC-06)
│     ├─ drawer.ioc                    # CubeMX 핀·주변장치 설정
│     └─ Core/Src/
│        ├─ main.c                     # 초기화 · 메인 루프
│        ├─ serial_cmd.c               # HC-06 · USB 수신 · 응답
│        ├─ cmd_parser.c               # 명령 해석 (PING · DRAWER · LED)
│        ├─ drawer.c                   # 서랍 LED ×6 · 팝업 서보 ×6 · 소등 타이머
│        └─ emergency_button.c         # 비상 버튼 → EVT:BTN:SOS
│
├─ esp32/                              # ESP32 프로젝트 (PlatformIO · Arduino)
│  ├─ entrance_node/                   # ESP32 #1 현관등 (Jetson ↔ Wi-Fi · MQTT)
│  │  ├─ platformio.ini                # 보드 · 빌드 설정
│  │  ├─ src/main.cpp                  # PIR · LED · Wi-Fi · MQTT
│  │  └─ include/
│  │     ├─ entrance_control.h         # PIR 필터 · 센서등 · 경고 타이머
│  │     └─ network_config.example.h   # Wi-Fi · Jetson 주소 설정 예제
│  └─ buzzer_tag/                      # ESP32 #2 부저 태그 (Jetson ↔ BLE)
│     ├─ platformio.ini                # 보드 · 빌드 설정
│     ├─ src/main.cpp                  # BLE 수신 · 부저
│     └─ include/buzzer_control.h      # ON/OFF 명령 처리
│
├─ jetson_nano/                        # 허브 (Python 애플리케이션 + C 서버)
│  ├─ main.py                          # PIR 기반 카메라 추론 진입점
│  ├─ vision/                          # 객체 탐지 · 영상 처리
│  ├─ record/                          # Last Seen 생성 · 관리
│  ├─ communication/                   # STM32 및 장치 연결
│  └─ server_jetson/                   # TCP 서버 · DB · 스냅샷 저장
│
├─ web/                                # 사용자 화면 (Web / PWA)
│  ├─ index.html                       # 물건 찾기 · 서랍 · 부저 화면
│  ├─ styles.css                       # PC · 휴대폰 레이아웃
│  ├─ app.js                           # 검색 · 선택 · 요청 · 오류 표시
│  ├─ api.js                           # HTTP API 요청 · 응답 검사
│  ├─ config.js                        # 샘플/실제 모드 · 서버 주소
│  ├─ demo-data.js                     # 샘플 기록
│  ├─ assets/                          # 화면 이미지 (샘플 장면)
│  ├─ dev-server.mjs                   # 로컬 미리보기 서버
│  └─ README.md                        # 실행 · Jetson 연결 방법
│
├─ docs/
│  ├─ protocol.md                      # 보드 간 통신 프로토콜
│  ├─ pinmap.md                        # 보드별 핀 배정
│  └─ classroom_test_checklist.md      # 강의실 테스트 체크리스트
├─ CONTRIBUTING.md                     # 협업 규칙
└─ README.md
```

> 기능별 코드는 담당 feature 브랜치(STM32 = `feature/stm32`, ESP32 = `feature/esp32`, Jetson = `feature/jetson`, 웹 = `feature/web`)에 있습니다.

---

## 개발 문서

- [핀맵](docs/pinmap.md) — STM32 ×2, ESP32 ×2
- [통신 프로토콜](docs/protocol.md)
- [협업 규칙](CONTRIBUTING.md) — feature 단독 검증 후 develop 통합

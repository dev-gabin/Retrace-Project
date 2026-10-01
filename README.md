# Retrace

> 스마트 공간 블랙박스 기반 분실물 위치 기억 및 탐색 시스템

Retrace는 카메라를 이용해 공간 속 물건을 지속적으로 관찰하고,  
물건이 마지막으로 확인된 **위치·시간·이미지**를 기록하여 분실 시 빠르게 찾을 수 있도록 돕는 임베디드 시스템입니다.

단순히 현재 물건을 탐지하는 것이 아니라, 물건이 시야에서 사라지더라도  
**마지막 목격 정보(Last Seen)** 를 기억하고 복원하는 것을 핵심 목표로 합니다.

카메라 기반 비주얼 메모리를 중심으로 **Pan/Tilt 레이저**, **스마트 서랍**, **Web/PWA**, **BLE 부저**를 연동합니다.

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

물건이 마지막으로 관찰된 정보를 저장합니다.

- 마지막 위치
- 마지막 관찰 시간
- 마지막 이미지
- 서랍 내부에 있는 경우 서랍 ID

### 3. Pan/Tilt 레이저 안내

마지막으로 확인된 위치를 기반으로 레이저가 해당 위치를 가리켜  
사용자가 물건을 쉽게 찾을 수 있도록 합니다.

- 2축 Pan/Tilt 서보
- 레이저 ON/OFF
- STM32에서 제어

### 4. 스마트 서랍

6칸 서랍 내부의 물건을 관리하고 사용자가 해당 서랍을 쉽게 확인할 수 있도록 합니다.

- 서랍 LED ×6
- 해당 서랍 LED 점등
- 서랍 팝업 서보
- Arduino에서 제어

### 5. PIR 활동 감지

PIR 센서를 이용하여 공간 내 움직임을 감지합니다.

- 활동 감지
- 카메라 분석 활성/대기 판단에 활용
- STM32에서 센서 입력 처리

### 6. 비상 스마트폰 찾기

서랍에 설치된 물리 버튼을 이용해 스마트폰 찾기 기능을 요청합니다.

- 비상 버튼 입력
- Arduino에서 버튼 감지
- Jetson으로 이벤트 전달

### 7. Web / PWA

스마트폰 또는 PC에서 Retrace 시스템을 사용할 수 있는 인터페이스입니다.

- 물건 검색
- Last Seen 정보 확인
- 마지막 이미지 확인
- 레이저 안내 요청
- 스마트 서랍 제어
- BLE 부저 호출

### 8. BLE 부저

일부 중요 물건에는 BLE 장치를 부착하여 필요할 때 부저음을 울릴 수 있도록 합니다.

BLE는 **위치 추정 용도로 사용하지 않습니다.**

- RSSI 거리 계산 X
- BLE 수신 노드를 이용한 위치 추정 X
- 중요 물건의 부저 호출 용도로만 사용

물건의 위치 탐색은 기본적으로 **카메라 Last Seen + 레이저 + 스마트 서랍**을 이용합니다.

---

# 시스템 구성

| 장치 | 역할 |
|---|---|
| Jetson Nano | AI 객체 탐지, Last Seen 처리, DB/이미지 저장, Web 서버, MCU 통신 |
| Arduino Uno | 서랍 LED ×6, 서랍 팝업 서보, 비상 버튼 |
| STM32 NUCLEO-F411RE | PIR 감지, Pan/Tilt 서보, 레이저 |
| Camera | 공간 영상 입력 |
| Web / PWA | 검색 및 시스템 제어 UI |
| BLE Device | 중요 물건의 부저 호출 |

---

# 시스템 구조

```text
                         Camera
                           │
                           ▼
                    ┌─────────────┐
                    │ Jetson Nano │
                    │             │
                    │ YOLO/OpenCV │
                    │ Last Seen   │
                    │ DB / Image  │
                    │ Web Server  │
                    └──────┬──────┘
                           │
              ┌────────────┴────────────┐
              │                         │
              ▼                         ▼
        ┌─────────────┐          ┌─────────────┐
        │ Arduino Uno │          │    STM32    │
        └──────┬──────┘          └──────┬──────┘
               │                        │
        ┌──────┼──────┐          ┌──────┼──────┐
        │      │      │          │      │      │
      LED×6  Drawer  Emergency  PIR  Pan/Tilt Laser
             Servo    Button
```

Jetson Nano가 시스템의 중앙 처리 장치 역할을 담당합니다.

Arduino와 STM32는 각각 맡은 센서 및 구동 장치를 제어하고  
Jetson과 통신하여 전체 시스템을 동작시킵니다.

---

# 하드웨어 역할

## Jetson Nano

시스템의 중앙 처리 장치입니다.

담당 기능:

- 카메라 영상 입력
- YOLO 객체 탐지
- OpenCV 영상 처리
- Last Seen 판단 및 기록
- DB 저장
- 이미지 저장
- Web/PWA 서버
- Arduino 통신
- STM32 통신

---

## Arduino Uno

**스마트 서랍 영역**을 담당합니다.

담당 기능:

- 서랍 LED ×6 제어
- 서랍 팝업 서보 제어
- 비상 버튼 입력
- Jetson과 통신

```text
Arduino
├─ Drawer LED ×6
├─ Drawer Popup Servo
└─ Emergency Button
```

---

## STM32 NUCLEO-F411RE

**카메라/상단 장치 영역**을 담당합니다.

담당 기능:

- PIR 센서 입력
- Pan 서보 제어
- Tilt 서보 제어
- 레이저 ON/OFF
- Jetson과 통신

```text
STM32
├─ PIR
├─ Pan Servo
├─ Tilt Servo
└─ Laser
```

Jetson ↔ STM32 개발 통신은  
**NUCLEO ST-LINK USB Serial(VCP)** 방식을 우선 사용합니다.

---

# 데이터 흐름

## 물건 감지

```text
Camera
   ↓
Jetson Nano
   ↓
YOLO / OpenCV
   ↓
객체 탐지
   ↓
Last Seen 갱신
   ↓
DB + Image 저장
```

## 레이저 위치 안내

```text
Web / PWA
   ↓
Jetson Nano
   ↓
STM32
   ↓
Pan/Tilt 이동
   ↓
Laser ON
```

## 스마트 서랍

```text
Web / PWA
   ↓
Jetson Nano
   ↓
Arduino
   ↓
해당 서랍 LED ON
   ↓
Drawer Servo 동작
```

## PIR 감지

```text
PIR
 ↓
STM32
 ↓
Jetson Nano
 ↓
활동 상태 판단
```

## 비상 버튼

```text
Emergency Button
       ↓
    Arduino
       ↓
  Jetson Nano
       ↓
스마트폰 찾기 요청
```

---

# 프로젝트 구조

```text
Retrace-Project/
│
├─ arduino/
│  ├─ arduino.ino
│  │
│  └─ src/
│     ├─ drawer/
│     │  ├─ DrawerController.h
│     │  └─ DrawerController.cpp
│     │
│     ├─ button/
│     │  ├─ EmergencyButton.h
│     │  └─ EmergencyButton.cpp
│     │
│     └─ communication/
│        ├─ Communication.h
│        └─ Communication.cpp
│
├─ jetson_nano/
│  ├─ main.cpp
│  ├─ CMakeLists.txt
│  │
│  ├─ vision/
│  │  ├─ Detector.h
│  │  └─ Detector.cpp
│  │
│  ├─ record/
│  │  ├─ LastSeen.h
│  │  └─ LastSeen.cpp
│  │
│  ├─ storage/
│  │  ├─ Database.h
│  │  ├─ Database.cpp
│  │  ├─ ImageStorage.h
│  │  └─ ImageStorage.cpp
│  │
│  ├─ communication/
│  │  ├─ ArduinoLink.h
│  │  ├─ ArduinoLink.cpp
│  │  ├─ Stm32Link.h
│  │  └─ Stm32Link.cpp
│  │
│  └─ server/
│     ├─ Server.h
│     └─ Server.cpp
│
├─ stm32/
│  ├─ cmake/
│  │
│  ├─ Core/
│  │  ├─ Inc/
│  │  │  ├─ main.h
│  │  │  ├─ stm32f4xx_hal_conf.h
│  │  │  ├─ stm32f4xx_it.h
│  │  │  ├─ pir_sensor.h
│  │  │  ├─ pan_tilt.h
│  │  │  └─ laser.h
│  │  │
│  │  └─ Src/
│  │     ├─ main.c
│  │     ├─ stm32f4xx_hal_msp.c
│  │     ├─ stm32f4xx_it.c
│  │     ├─ syscalls.c
│  │     ├─ sysmem.c
│  │     ├─ system_stm32f4xx.c
│  │     ├─ pir_sensor.c
│  │     ├─ pan_tilt.c
│  │     └─ laser.c
│  │
│  ├─ Drivers/
│  ├─ CMakeLists.txt
│  ├─ CMakePresets.json
│  ├─ Retrace_STM32.ioc
│  ├─ startup_stm32f411xe.s
│  └─ STM32F411xx_FLASH.ld
│
├─ web/
│
├─ docs/
│
├─ .gitignore
└─ README.md
```

---

# 코드 모듈 역할

## Arduino

```text
DrawerController
├─ 서랍 LED ×6 제어
└─ 서랍 팝업 서보 제어

EmergencyButton
└─ 비상 버튼 입력 처리

Communication
├─ Jetson → Arduino : 서랍 제어 명령
└─ Arduino → Jetson : 비상 버튼 이벤트
```

## Jetson Nano

```text
Detector
└─ 객체 탐지 / 영상 처리

LastSeen
└─ 마지막 목격 정보 생성 및 관리

Database
└─ 데이터베이스 저장

ImageStorage
└─ 이미지 저장

ArduinoLink
├─ 서랍 LED/서보 명령 송신
└─ 비상 버튼 이벤트 수신

Stm32Link
├─ Pan/Tilt/레이저 명령 송신
└─ PIR 이벤트 수신

Server
└─ Web/PWA 요청 처리
```

## STM32

```text
pir_sensor
└─ PIR 입력 처리

pan_tilt
├─ Pan Servo PWM
└─ Tilt Servo PWM

laser
└─ Laser ON/OFF
```

---


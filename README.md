# Retrace

**스마트 공간 블랙박스 기반 분실물 위치 기억 및 탐색 시스템**

## 주요 기능

- YOLO / OpenCV 기반 객체 인식
- 물건의 마지막 목격 위치·시간·이미지 기록
- Pan/Tilt 레이저를 이용한 위치 안내
- 스마트 서랍 LED 제어
- BLE 태그 부저 기능
- 웹/PWA를 통한 검색 및 확인

## 사용 기술

- Jetson Nano / Linux
- YOLO / OpenCV
- Arduino Uno
- STM32 NUCLEO-F411
- Wi-Fi / BLE
- GitHub / Jira

## 진행 상태

**현재 개발 진행 중**

## 프로젝트 구조

각 디렉터리는 **단일 책임 원칙(SRP)** 을 적용하여 기능 수정 및 오류 추적이 쉽도록 구성.

```text
Retrace-Project/
├─ arduino/               # PIR · 버튼 입력 처리
│
├─ jetson_nano/           # 객체 인식 · 기록 · 시스템 제어
│  ├─ vision/             # YOLO / OpenCV
│  ├─ last_seen/          # 마지막 목격 정보 관리
│  ├─ storage/            # DB / 로그
│  ├─ communication/      # Arduino / STM32 통신
│  └─ server/             # Web / PWA 서버
│
├─ stm32/                 # 서보 · 레이저 · 스마트 서랍 제어
│  ├─ Core/
│  ├─ Drivers/
│  ├─ cmake/
│  ├─ CMakeLists.txt
│  └─ Retrace_STM32.ioc
│
├─ web/                   # 사용자 Web / PWA
│
├─ docs/                  # 회로도 · 구성도 · 개발 문서
│
├─ .gitignore
└─ README.md
```

> 프로젝트 구조는 개발 진행에 따라 변경될 수 있습니다.
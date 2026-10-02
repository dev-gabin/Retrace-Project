# Retrace 통신 프로토콜

> 각 보드가 주고받는 메시지 약속입니다. **보내는 쪽과 받는 쪽 모두 이 문서를 기준으로 구현합니다.**  
> 문서와 다르게 동작하면 **문서와 다른 쪽이 수정**합니다. 변경이 필요하면 문서를 먼저 고치고 서로 공유합니다.
>
> 상태: **초안 v0.3** (확정 필요 항목은 맨 아래 참고)  
> 보드 핀 배정은 [pinmap.md](pinmap.md)를 참고합니다.

---

## 0. 전체 연결

| 연결 | 방식 | 보드 쪽 (담당: 본인) | Jetson 쪽 (담당: 짝꿍) |
|---|---|---|---|
| Jetson ↔ STM32 #1 레이저 헤드 | USB Serial (VCP) | `stm32/laser_head` | `Stm32Link` |
| Jetson ↔ STM32 #2 서랍 | Bluetooth (HC-06) | `stm32/drawer` | `DrawerLink` |
| Jetson ↔ ESP32 #1 현관등 (LOLIN D32) | Wi-Fi · MQTT | `esp32/entrance_node` | `MqttLink` |
| Jetson → ESP32 #2 부저 태그 (ESP32-C3) | BLE | `esp32/buzzer_tag` | `BleBuzzer` |
| Jetson → 스마트폰 | ntfy (HTTP) | - | `PhoneNotifier` |
| Web/PWA ↔ Jetson | HTTP | - | `Server` |

> 카메라는 Jetson에 USB로 직접 연결되며(영상 입력), 이 문서의 메시지 대상이 아닙니다.

---

## 1. 시리얼 공통 규칙 (STM32 #1, STM32 #2)

USB Serial과 Bluetooth(HC-06) 모두 **같은 문자 형식**을 씁니다. PC 시리얼 터미널로 직접 입력해서 테스트할 수 있습니다.

| 항목 | 규칙 |
|---|---|
| 문자 | ASCII |
| 대소문자 | **대문자만** 사용. 보내는 쪽·받는 쪽 모두 대문자 기준이며, 소문자가 섞인 명령은 `ERR:UNKNOWN` (테스트할 때도 대문자로 입력) |
| 형식 | 필드는 `:`로 구분, 한 필드 안의 여러 값은 `,`로 구분 (예: `AIM:120,45`, `DRAWER:OPEN:3`, `LED:3:ON`) |
| 줄 끝 | `\n` (LF) 한 개로 메시지 하나 끝 (`\r`은 받는 쪽에서 무시) |
| 공백 | 사용하지 않음 |
| 최대 길이 | 32바이트 (`\n` 포함) |

### 응답 · 이벤트

| 종류 | 형식 | 예 |
|---|---|---|
| 성공 응답 | `OK:명령` | `OK:AIM` |
| 실패 응답 | `ERR:명령:이유` | `ERR:AIM:RANGE` |
| 모르는 명령 | `ERR:UNKNOWN` | |
| 보드가 먼저 보내는 이벤트 | `EVT:종류:값` | `EVT:PIR:1` |

- 명령을 받으면 **반드시 `OK` 또는 `ERR`로 응답**합니다.
- Jetson은 응답을 **1초** 기다리고, 없으면 **1회 재전송**합니다. 그래도 없으면 연결 오류로 처리합니다.
- **이벤트(`EVT:`)는 언제든 올 수 있습니다.** Jetson은 응답을 기다리는 중에도 `EVT:`로 시작하는 줄은 응답이 아니라 **이벤트로 처리**하고, 계속 응답을 기다립니다.

```text
Jetson → AIM:90,90
STM32  → EVT:PIR:1     ← 응답 대기 중 이벤트 도착 → 이벤트로 처리
STM32  → OK:AIM        ← 이게 AIM의 응답
```

### 공통 명령

| 명령 | 응답 | 설명 |
|---|---|---|
| `PING` | `OK:PING` | 연결 확인 |

---

## 2. Jetson ↔ STM32 #1 레이저 헤드 (USB Serial)

카메라와 레이저 헤드는 **메인 유닛 한 몸체**라 USB로 연결합니다.

| 항목 | 값 |
|---|---|
| 포트 | ST-LINK VCP (STM32 USART2: PA2/PA3), Jetson에서 `/dev/ttyACM*` |
| 설정 | 115200, 8N1 |

### Jetson → STM32 #1

| 명령 | 응답 | 설명 |
|---|---|---|
| `AIM:pan,tilt` | `OK:AIM` | 서보 각도 이동. 각도는 **0~180 정수(도)** |
| `LASER:ON` | `OK:LASER` | 레이저 켜기 |
| `LASER:OFF` | `OK:LASER` | 레이저 끄기 |
| `HOME` | `OK:HOME` | 서보 중앙(90,90) + 레이저 OFF |

- 범위 밖 각도 → `ERR:AIM:RANGE`
- 카메라 좌표 → 각도 변환(조준 보정)은 **Jetson**에서 하고, STM32는 **받은 각도만** 서보 펄스로 바꿉니다.
- 각도 → 펄스 변환(SG90 최소/최대 펄스)은 STM32가 담당합니다.

### STM32 #1 → Jetson (이벤트)

| 이벤트 | 설명 |
|---|---|
| `EVT:PIR:1` | 움직임 감지 시작 |
| `EVT:PIR:0` | 움직임 감지 종료 |

예시:

```text
Jetson → AIM:120,45
STM32  → OK:AIM
Jetson → LASER:ON
STM32  → OK:LASER
STM32  → EVT:PIR:1
```

---

## 3. Jetson ↔ STM32 #2 서랍 (Bluetooth, HC-06)

| 항목 | 값 |
|---|---|
| 연결 | HC-06 (Bluetooth Classic, SPP) ↔ STM32 USART1 (PA9/PA10), Jetson에서 `/dev/rfcomm*` |
| 설정 | **9600**, 8N1 (HC-06 기본값, 모듈 확인 필요) |
| Jetson 쪽 | USB Bluetooth 동글 (CSR 4.0) |

> STM32 #2의 USART2(VCP)는 **PC 디버그 로그 전용**이며 이 프로토콜과 무관합니다.

**서랍 번호 n** (서보·LED 번호와 동일, `pinmap.md` 3-2)

```text
┌─────────┬─────────┬─────────┐
│    1    │    2    │    3    │   Top
├─────────┼─────────┼─────────┤
│    4    │    5    │    6    │   Bottom
└─────────┴─────────┴─────────┘
   Left      Middle    Right
```

### Jetson → STM32 #2

| 명령 | 응답 | 설명 |
|---|---|---|
| `DRAWER:OPEN:n` | `OK:DRAWER` | n번 서랍(1~6) LED ON + 팝업 서보 동작 |
| `LED:n:ON` | `OK:LED` | n번 LED만 켜기 |
| `LED:n:OFF` | `OK:LED` | n번 LED만 끄기 |
| `LED:ALL:OFF` | `OK:LED` | 전체 LED 끄기 |

- 범위 밖 번호 → `ERR:DRAWER:RANGE`, `ERR:LED:RANGE`
- 서보 각도(밀어내기·복귀)는 **STM32가 처리**합니다. Jetson은 서랍 번호만 보냅니다.
- `DRAWER:OPEN` 후 LED는 **STM32가 타이머로 자동 소등**합니다 (Jetson이 끄지 않아도 됨).
- 새 `DRAWER:OPEN`이 오면 **이전 LED는 즉시 소등**하고 새 서랍만 켭니다.

### STM32 #2 → Jetson (이벤트)

| 이벤트 | 설명 |
|---|---|
| `EVT:BTN:SOS` | 비상 버튼 눌림 (폰 사이렌 요청) |

- 버튼 떨림(채터링) 처리는 **STM32**에서 하고, 한 번 누르면 이벤트 **한 번만** 보냅니다.

예시:

```text
Jetson → DRAWER:OPEN:3
STM32  → OK:DRAWER
STM32  → EVT:BTN:SOS
```

---

## 4. Jetson ↔ ESP32 #1 현관등 (Wi-Fi · MQTT)

| 항목 | 값 |
|---|---|
| 보드 | LOLIN D32 (PIR + 현관등 LED) |
| 브로커 | Jetson (Mosquitto), 포트 1883 |
| 클라이언트 ID | `retrace-entrance` |

| 토픽 | 방향 | 내용 | QoS | Retain |
|---|---|---|---|---|
| `retrace/entrance/motion` | 현관등 → Jetson | `1` (움직임 감지) | 1 | X |
| `retrace/entrance/light` | Jetson → 현관등 | `NORMAL` / `ALERT` | 1 | X |
| `retrace/entrance/status` | 현관등 → Jetson | `online` / `offline` | 1 | O |

- `retrace/entrance/status`의 `offline`은 MQTT **Last Will**로 설정해, 현관등이 갑자기 꺼지면 브로커가 대신 알립니다.
- **대소문자**: 토픽은 MQTT 관례대로 **소문자**, 우리가 정의한 명령 값(`NORMAL`/`ALERT`)은 **대문자**, 연결 상태(`online`/`offline`)는 **MQTT 관례(소문자)**를 따릅니다. 스마트홈 플랫폼(Home Assistant 등)의 기본 상태값과 같아서 연동 시 그대로 사용할 수 있습니다.
- 평소 센서등 점등은 **현관등이 스스로** 처리하고, `ALERT`만 Jetson이 보냅니다.
- `ALERT` 후 일정 시간이 지나면 현관등이 **스스로 `NORMAL`로 복귀**합니다.

테스트 (Jetson 터미널):

```bash
mosquitto_sub -h localhost -t "retrace/#" -v
mosquitto_pub -h localhost -t retrace/entrance/light -m ALERT
```

---

## 5. Jetson → ESP32 #2 부저 태그 (BLE)

| 항목 | 값 |
|---|---|
| 보드 | ESP32-C3 Super Mini (부저) |
| Jetson 쪽 | USB Bluetooth 동글 (CSR 4.0, BLE 지원) |
| 기기 이름 | `RETRACE-TAG-01` |
| 서비스 UUID | `4951013c-1654-43f1-919f-a3ff928dc0b6` |
| 특성(Characteristic) UUID | `36b342d0-153b-418e-b5d7-f95af507ec1f` (Write) |

| 쓰는 값 | 바이트 | 동작 |
|---|---|---|
| `1` | ASCII 문자 `0x31` | 부저 울림 |
| `0` | ASCII 문자 `0x30` | 부저 끔 |

- 값은 **숫자 1·0(0x01·0x00)이 아니라 ASCII 문자**입니다. BLE 테스트 앱에서 텍스트(UTF-8)로 `1`을 쓰면 됩니다.

- `1`을 받으면 부저 태그가 **일정 시간 후 스스로 끔** (끄는 값을 못 받아도 계속 울리지 않게)
- 태그가 여러 개가 되면 기기 이름을 `RETRACE-TAG-02`, `03`...으로 늘립니다.

테스트: 폰 BLE 테스트 앱(nRF Connect 등)으로 위 특성에 `1` 쓰기

---

## 6. Jetson → 스마트폰 (ntfy)

| 항목 | 값 |
|---|---|
| 서버 | Jetson (ntfy) |
| 토픽 | `retrace-phone` |

| 알림 | 우선순위 | 제목 (헤더) | 본문 | 설명 |
|---|---|---|---|---|
| 폰 사이렌 (비상 버튼) | **5 (max)** | `Retrace SOS` | `폰을 찾고 있어요!` | 앱 설정 "최고 우선순위 계속 울리기"로 반복 알림 |
| 외출 알림 | **3 (default)** | `Car Key Check` | `차키 챙기셨나요?` | 한 번만 알림 |

- 제목은 HTTP **헤더**로 보내서 한글이 깨질 수 있으므로 **영문**, 한글 문구는 **본문**에 넣습니다.

테스트:

```bash
curl -H "Priority: 5" -H "Title: Retrace SOS" -d "폰을 찾고 있어요!" http://localhost/retrace-phone
```

---

## 7. Web/PWA ↔ Jetson (HTTP API) — 초안

| 메서드 | 경로 | 설명 |
|---|---|---|
| GET | `/api/items` | 전체 물건 Last Seen 목록 |
| GET | `/api/items/{item}` | 특정 물건 Last Seen |
| POST | `/api/items/{item}/aim` | 해당 물건 위치로 레이저 안내 |
| POST | `/api/drawers/{n}/open` | n번 서랍 열기 |
| POST | `/api/buzzer` | 부저 태그 호출 |
| GET | `/snapshots/{file}` | 스냅샷 이미지 |

> 응답 형식(JSON 필드)은 Jetson 담당이 확정합니다.

---

## 8. DB 형식 (Last Seen)

| 필드 | 타입 | 설명 | 예 |
|---|---|---|---|
| `item` | TEXT (키) | 물건 이름 | `carkey` |
| `pos_x` | INTEGER | 카메라 화면 x 좌표(px) | `412` |
| `pos_y` | INTEGER | 카메라 화면 y 좌표(px) | `288` |
| `seen_at` | TEXT | 마지막 관찰 시각 (ISO 8601) | `2026-10-03T14:22:05` |
| `snapshot` | TEXT | 스냅샷 파일 경로 | `snapshots/carkey_20261003_142205.jpg` |
| `drawer_id` | INTEGER, NULL | 서랍 안이면 1~6, 아니면 NULL | `3` |
| `state` | TEXT | `visible` / `occluded` / `uncertain` | `visible` |

### 물건 이름 (item)

| 이름 | 물건 |
|---|---|
| `carkey` | 차키 |
| `airpods` | 에어팟 |
| `glasses` | 안경 |

> 학습 데이터셋 클래스 이름도 위 이름과 **똑같이** 맞춥니다.

---

## 확정 필요 항목

- [ ] HC-06 실제 통신 속도 (기본 9600인지 모듈 확인)
- [ ] 서랍 LED 자동 소등 시간 / 현관등 ALERT 유지 시간 / 부저 자동 정지 시간
- [ ] ntfy 서버 포트
- [ ] HTTP API 응답 JSON 형식
- [ ] 추적 물건 목록 최종 확정 (안경 / 지갑)

---

## 변경 기록

| 버전 | 내용 |
|---|---|
| v0.1 | 최초 초안 |
| v0.2 | 대소문자 규칙(**대문자만**, 소문자는 `ERR:UNKNOWN`), 보드 이름·담당·폴더 표기 통일, 서랍 번호 그림, STM32 #2 USART1·디버그 USART2 구분, 핀맵 문서 연결 |
| v0.3 | 이벤트 끼어들기 처리 규칙, `PING` 응답 `OK:PING`으로 통일, 형식 설명(`:` / `,`) 수정, BLE 값 ASCII 명시, ntfy 제목 영문·본문 한글로 통일, MQTT 대소문자 범위(연결 상태는 관례대로 소문자) 명시 |

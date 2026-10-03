# Retrace 통신 프로토콜

> 각 보드가 주고받는 메시지 약속입니다. **보내는 쪽과 받는 쪽 모두 이 문서를 기준으로 구현합니다.**  
> 문서와 다르게 동작하면 **문서와 다른 쪽이 수정**합니다. 변경이 필요하면 문서를 먼저 고치고 서로 공유합니다.
>
> 상태: **보드 구현 기준 v0.5** — 네 보드 작성·빌드·PC 테스트 완료, 실물·Jetson/웹 통합 검증 전. HTTP API·DB 등 허브 측 항목은 초안입니다. 남은 확정 항목은 맨 아래 참고합니다.
> 보드 핀 배정은 [pinmap.md](pinmap.md), 실행·검증 항목은 [통합 테스트 체크리스트](classroom_test_checklist.md)를 참고합니다.

---

## 0. 전체 연결

| 연결 | 방식 | 보드 쪽 (담당: 본인) | Jetson 쪽 (담당: 짝꿍) |
|---|---|---|---|
| Jetson ↔ STM32 #1 메인 유닛 | USB Serial (VCP) | `stm32/main_unit` | `Stm32Link` |
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

## 2. Jetson ↔ STM32 #1 메인 유닛 (USB Serial)

카메라와 STM32 #1은 **같은 메인 유닛**에 배치하며, STM32 #1과 Jetson은 USB로 연결합니다.

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

> STM32 #2의 USART2(VCP, 115200 8N1)는 **PC 단독 테스트용**으로 같은 명령을 받습니다. 응답은 명령을 받은 UART로 보내며, SOS 이벤트는 USART1과 USART2 양쪽으로 보냅니다. Jetson 연결은 USART1(HC-06)을 사용합니다.

**서랍 번호 n** (서보·LED 번호와 동일, `pinmap.md` 3-2)

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

### Jetson → STM32 #2

| 명령 | 응답 | 설명 |
|---|---|---|
| `DRAWER:OPEN:n` | `OK:DRAWER` | n번 서랍(1~6) LED ON + 팝업 서보 동작 |
| `LED:n:ON` | `OK:LED` | n번 LED만 켜기 |
| `LED:n:OFF` | `OK:LED` | n번 LED만 끄기 |
| `LED:ALL:OFF` | `OK:LED` | 전체 LED 끄기 |

- 범위 밖 번호 → `ERR:DRAWER:RANGE`, `ERR:LED:RANGE`
- 서보 밀어내기·복귀는 **STM32가 처리**합니다. Jetson은 서랍 번호만 보냅니다. 현재 밀기 2000µs / 복귀 1000µs, 각각 500ms이며 실물 보정 전 임시값입니다. 복귀는 서보 팔 복귀이며 서랍을 자동으로 닫지 않습니다.
- LED 점등 명령 이후 마지막 점등 시각부터 현재 **10초** 뒤 STM32가 자동 소등합니다. 서랍 닫힘 감지는 없습니다. 시간은 강의실에서 조정합니다.
- 새 `DRAWER:OPEN`이 오면 **이전 LED는 즉시 소등**하고 새 서랍만 켭니다.
- 서보는 한 번에 하나만 구동합니다. 동작 중 새 요청을 받으면 기존 서보를 복귀시키고 새 서랍을 구동합니다. 복귀 중 여러 요청이 오면 마지막 요청을 예약합니다 (`OK:DRAWER`는 요청 접수 응답).

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

구현 기준 (`esp32/entrance_node`):

- PIR은 50ms 필터 후 LOW → HIGH 전환마다 `motion = 1`을 한 번 발행합니다. HIGH 유지 중에는 중복 발행하지 않고, 안정적인 LOW 이후 다음 HIGH에서 다시 발행합니다.
- 한 색 LED(GPIO25)는 PIR이 HIGH인 동안 켜지고, 안정적인 LOW 감지 후 **10초** 유지합니다.
- `ALERT`는 **250ms마다 ON/OFF**하며 **10초** 후 기본 센서등 모드로 복귀합니다. 새 `ALERT`는 시간을 갱신하고 ON부터 다시 시작합니다. `NORMAL`은 경고를 취소하며, PIR 점등 조건이 남아 있으면 LED는 켜져 있습니다.
- 시간은 `src/main.cpp` 맨 위의 임시 상수로, 강의실에서 조정합니다.
- 명령은 대문자 `NORMAL` / `ALERT`와 정확히 일치해야 합니다. 줄 끝·공백·소문자·잘못된 값과 Retain 명령은 무시합니다.
- ESP-MQTT로 motion 발행·light 구독·status 및 Last Will에 **QoS 1**을 사용합니다. status는 Retain, motion과 light는 Retain 없이 사용합니다.
- Wi-Fi·MQTT 대기는 별도 태스크에서 처리합니다. 연결이 없어도 기본 PIR 센서등과 경고 복귀 타이머는 계속 동작합니다. 연결 중 새 움직임을 전송하며, 새 오프라인 움직임은 저장하지 않습니다. 이미 MQTT에 넣은 QoS 1 메시지는 재전송될 수 있습니다.
- Wi-Fi·Jetson 주소는 Git에서 제외되는 `include/network_config.h`에 설정합니다. 예제 설정이 비어 있으면 기본 PIR 센서등만 동작합니다.

테스트 (Jetson 터미널):

```bash
mosquitto_sub -h localhost -t "retrace/#" -v
mosquitto_pub -h localhost -q 1 -t retrace/entrance/light -m ALERT
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

- 값은 **정확히 1바이트**만 받습니다. 빈 값, `1\n`, `10`, 숫자 바이트 `0x01`/`0x00` 등은 무시하고 기존 동작을 유지합니다. 시리얼과 달리 줄 끝을 붙이지 않습니다.
- `1`을 받으면 **시간 제한 없이 계속 울립니다**. 다시 `1`을 받아도 ON을 유지하며, `0`을 받으면 즉시 끕니다. 시간에 따른 자동 정지는 없습니다.
- 사용자는 웹에서 **찾기 / 소리 끄기**를 누르고, Jetson이 각각 BLE `1` / `0`을 보내도록 연동합니다. 웹·Jetson 쪽 구현과 연동 검증은 해당 담당자의 작업입니다.
- 전원을 켰을 때와 BLE 연결이 끊겼을 때는 부저 OFF입니다. 연결 해제 후 약 0.5초 뒤 광고를 다시 시작하며, 재연결만으로 부저가 켜지지는 않습니다.
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

> 응답 형식(JSON 필드)은 Jetson 담당이 확정합니다. 부저 API는 찾기뿐 아니라 소리 끄기 요청도 BLE `0`으로 이어져야 하며, 요청 본문·경로는 Jetson·웹 담당자가 확정합니다. 위 표는 보드 펌웨어 작성 완료와 별개인 허브 측 초안입니다.

### 7-1. 웹 뼈대에서 사용하는 JSON 임시안

`feature/web`의 화면과 샘플 데이터는 아래 형식을 사용합니다. **기존 메서드·경로는 유지하며, 아래 JSON은 Jetson 담당자 합의 전 임시안입니다.** 서버에 이미 다른 형식이 있다면 이 문서와 `web/api.js`를 함께 맞춥니다. Jetson 서버 구현 완료를 의미하지 않습니다.

| 요청 | 요청 본문 | 성공 응답 (HTTP 200) |
|---|---|---|
| `GET /api/items` | 없음 | `{ "items": [LastSeen, ...] }` |
| `GET /api/items/{item}` | 없음 | `LastSeen` 객체 |
| `POST /api/items/{item}/aim` | `{}` | `{ "ok": true }` |
| `POST /api/drawers/{n}/open` | `{}` | `{ "ok": true }` |
| `POST /api/buzzer` | `{ "enabled": true }` 또는 `{ "enabled": false }` | `{ "ok": true }` |

POST는 `Content-Type: application/json`을 사용합니다. 부저 `enabled=true`는 BLE ASCII `1`, `false`는 ASCII `0`으로 Jetson이 변환합니다. 웹은 UART·MQTT·BLE를 직접 제어하지 않습니다. 레이저 요청은 물건 ID만 보내며 목표 각도는 Jetson이 계산합니다.

`LastSeen` 예제 (8장 필드 이름 사용):

```json
{
  "item": "carkey",
  "pos_x": 412,
  "pos_y": 288,
  "seen_at": "2026-10-03T14:22:05+09:00",
  "snapshot": "snapshots/carkey_20261003_142205.jpg",
  "drawer_id": null,
  "state": "visible"
}
```

- `item`: `carkey` / `airpods` / `glasses`. 화면 이름은 웹에서 차키 / 에어팟 / 안경으로 표시합니다.
- `pos_x`, `pos_y`: 이미지 픽셀 좌표, 둘 다 0 이상의 정수 또는 둘 다 `null`.
- `seen_at`: 시간대가 있는 ISO 8601 문자열 또는 `null`. 기록이 없으면 시간·좌표·사진·서랍 필드를 `null`로 보내고 웹은 기록 없음으로 표시합니다.
- `snapshot`: `snapshots/` 아래 상대 경로 또는 `null`. 웹은 동일 서버의 `GET /snapshots/{file}`로 표시합니다. 임의 외부 주소나 상위 폴더 경로는 받지 않습니다.
- `drawer_id`: 1~6 정수 또는 `null`. `state`: `visible` / `occluded` / `uncertain`.
- `{ "ok": true }`는 **서버의 요청 접수 응답**이며, 실제 서보 이동·레이저 점등·부저 소리 완료를 뜻하지 않습니다. 보드 응답 대기·장치 오류 처리 방식은 Jetson 담당자가 추가로 확정합니다.
- 실패는 HTTP 4xx/5xx와 `{ "error": { "code": "ITEM_NOT_FOUND", "message": "물건 기록이 없습니다." } }` 형태의 임시안을 사용합니다. 웹은 실패·시간 초과를 표시하며, 실제 연결 실패 시 샘플 성공으로 바꾸지 않습니다.
- 부저 켜기·끄기는 하나의 태그 기준입니다. 물건마다 별도 태그를 고르는 기능·장치 상태 조회 API는 아직 정의하지 않았습니다.

웹 기본 설정은 샘플 모드이며 실제 API 요청을 보내지 않습니다. 실행·연동 전환·검증 방법은 [feature/web의 웹 인계 문서](https://github.com/dev-gabin/Retrace-Project/blob/feature/web/web/README.md)를 참고합니다. 웹 코드와 인계 문서는 `feature/web`에서 관리하며, 이 프로토콜 문서는 각 브랜치에서 공통으로 공유합니다.

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
- [ ] 서랍 LED 10초 소등 / 현관등 기본 점등 10초·ALERT 10초·깜빡임 250ms를 실물에 맞춰 보정
- [ ] ntfy 서버 포트
- [ ] HTTP API JSON 임시안(7-1)을 Jetson·웹 담당자가 함께 확정, 장치 오류·완료 응답 방식 정의
- [ ] 추적 물건 목록 최종 확정 (안경 / 지갑)

---

## 변경 기록

| 버전 | 내용 |
|---|---|
| v0.1 | 최초 초안 |
| v0.2 | 대소문자 규칙(**대문자만**, 소문자는 `ERR:UNKNOWN`), 보드 이름·담당·폴더 표기 통일, 서랍 번호 그림, STM32 #2 USART1·디버그 USART2 구분, 핀맵 문서 연결 |
| v0.3 | 이벤트 끼어들기 처리 규칙, `PING` 응답 `OK:PING`으로 통일, 형식 설명(`:` / `,`) 수정, BLE 값 ASCII 명시, ntfy 제목 영문·본문 한글로 통일, MQTT 대소문자 범위(연결 상태는 관례대로 소문자) 명시 |
| v0.4 | 서랍 번호 배치를 pinmap.md 3-2 기준(2열 × 3행)으로 통일 |
| v0.5 | 현관등 PIR·ALERT·QoS 1 구현 기준 명시, 부저 자동 정지 제거 및 웹 끄기 요청 기준 반영 |

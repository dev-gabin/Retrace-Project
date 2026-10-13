# Retrace 통신 프로토콜

버전 **v0.9** · 소스 확인 2026-10-13. 현재 구현의 메시지·HTTP API·저장 형식을 정리합니다. 규약을 바꿀 때는 문서와 송수신 코드를 함께 검토하고 팀원과 공유합니다.

핀은 [핀맵](pinmap.md), 동작 조건은 [추론 정책](inference_policy.md), 실제 검증은 [체크리스트](classroom_test_checklist.md)를 참고합니다.

## 1. 연결과 책임

| 연결 | 구현 | 기본 설정 |
|---|---|---|
| Python ↔ STM32 #1 | `communication/stm32_link.py` → USB VCP | 115200, 8N1 |
| Python ↔ C TCP 서버 | `record/last_seen.py`, `communication/server_events.py` | TCP 5000, 인증 로그인 |
| C TCP 서버 ↔ 서랍 | `client/bluetooth/client_bluetooth.c` → HC-06 RFCOMM → USART1 | UART 9600, 8N1 |
| C TCP 서버 ↔ 현관등 | LOLIN D32 `WiFiClient` | TCP 5000, 인증 로그인 |
| Web / LCD ↔ Python | `vision/http_api.py` | HTTP 8080 |
| Python → ntfy → 휴대폰 | `communication/phone_notifier.py` | HTTP POST, 같은 토픽 구독 |

레이저는 C TCP 서버를 통과하지 않습니다. LCD는 HTTP 조회 전용이며, 준비물은 웹과 LCD가 같은 목록을 사용합니다.

## 2. STM32 시리얼 형식

명령은 ASCII 대문자를 사용합니다. 명령 본문은 `:`와 `,`로 구분하고 LF(`\n`)로 끝냅니다. 수신 버퍼는 32바이트이며 본문은 최대 31문자입니다. CR은 무시합니다. 두 UART의 수신 큐는 포트별 8줄입니다.

| 종류 | 현재 형식 | 예 |
|---|---|---|
| 성공 | `OK@명령` | `OK@AIM` |
| 오류 | `ERR@명령:이유` | `ERR@AIM:RANGE` |
| 모르는 명령·초과 입력 | `ERR@UNKNOWN` | 소문자 명령 등 |
| 메인 PIR | `EVT@PIR:1` / `EVT@PIR:0` | 움직임 시작 / 종료 |
| 서랍 SOS | `EVT@DRAWER:SOS` | 버튼 눌림 |

`EVT@`는 응답 대기 중에도 도착할 수 있습니다. Python USB 수신 스레드는 이벤트와 `OK@`/`ERR@`를 별도 큐로 분리합니다. `Stm32Link`는 명령별 기본 3초 응답 제한을 사용하며 현재 자동 재전송하지 않습니다.

### 2-1. STM32 #1 레이저 안내 유닛

| 명령 | 정상 응답 | 동작 |
|---|---|---|
| `PING` | `OK@PING` | 연결 확인 |
| `AIM:pan,tilt` | `OK@AIM` | 각도 0~180 정수, PWM 설정 |
| `LASER:ON` / `LASER:OFF` | `OK@LASER` | 레이저 제어 |
| `HOME` | `OK@HOME` | MCU 기준 90°/90° 복귀, 레이저 OFF |

범위 밖 각도는 `ERR@AIM:RANGE`입니다. STM32는 받은 각도를 현재 700~2300µs PWM으로 변환합니다. 좌표→각도 보정은 Jetson에서 합니다.

Python 안내의 복귀 위치는 `RETRACE_HOME_PAN` / `RETRACE_HOME_TILT` 기본 **85°/42°**이며 `AIM`으로 이동합니다. MCU의 `HOME` 명령과 구분합니다. 웹 안내는 소등 → 조준 → 기본 0.5초 안정화 → 최대 5초 점등 → 소등·복귀 순서입니다.

PIR①은 50ms 안정화 후 상태 변화마다 이벤트를 보내며, 물건 종류를 인식하지 않습니다.

### 2-2. STM32 #2 스마트 서랍

| 명령 | 정상 응답 | 동작 |
|---|---|---|
| `PING` | `OK@PING` | 연결 확인 |
| `DRAWER:OPEN:n` | `OK@DRAWER` | n번 LED, 서보 밀기·복귀 시퀀스 |
| `LED:n:ON` / `LED:n:OFF` | `OK@LED` | 해당 LED만 제어 |
| `LED:ALL:OFF` | `OK@LED` | 전체 LED OFF |
| `I2C:CHECK` | `OK@I2C:PCA9685` | PCA9685 설정 읽기·확인 |

`n`은 1~6입니다. 범위 오류는 `ERR@DRAWER:RANGE` / `ERR@LED:RANGE`, I2C 점검 실패는 `ERR@I2C:PCA9685`입니다.

- 서랍 배치: 정면 기준 위 `1 2`, 가운데 `3 4`, 아래 `5 6`. PCA9685 CH0~CH5에 순서대로 대응합니다.
- 밀기 **1400µs·500ms**, 복귀 **1900µs·1000ms**, 이후 해당 PWM Full OFF.
- 서랍 동작 중 새 열기 요청은 `ERR@DRAWER:BUSY`, I2C 오류는 `ERR@DRAWER:I2C`로 처리합니다.
- MCU의 `OK@DRAWER`는 밀기·복귀 시퀀스가 완료된 뒤 전송합니다. 실제 서랍 위치를 센서로 확인했다는 뜻은 아닙니다.
- 열린 번호의 LED는 마지막 점등부터 **10초** 뒤 소등하며 닫힘 센서는 없습니다.
- SOS는 PB5 active LOW, 50ms 디바운스 후 한 번 전송합니다. 유지 중 중복 전송하지 않으며 USB와 HC-06 양쪽 UART로 보냅니다.

## 3. 인증 TCP·장치 중계

연결 직후 `[ID:비밀번호]`로 로그인합니다. 서버 `idpasswd.txt`와 노드 설정의 ID·비밀번호가 일치해야 합니다. 이후 메시지는 `[대상]본문\n` 형식입니다.

| 요청 | 의미 |
|---|---|
| `[SQL]LIST` | 등록 물건·최신 관측 목록 |
| `[SQL]GET@wallet` | 단일 기록 조회 |
| `[SQL]SAVE@...` | 공개된 JPEG와 메타데이터 저장 |
| `[SERVER]PING@ENTRANCE` | 현관 노드 연결 확인 |
| `[SERVER]SET@DRAWER:OPEN:3` | 서랍 중계 요청 |
| `[SERVER]SET@ENTRANCE:LIGHT:ALERT` | 현관 경고 요청 |

`SET` 요청에 대한 `[SERVER]OK@SET:대상`은 서버가 대상에게 명령을 전달했다는 응답입니다. 보드의 시퀀스 완료나 실제 기구 동작 확인과 구분합니다. 대상 미접속·전달 실패 등은 `ERR@SET:...`입니다.

서랍에는 대상 ID가 붙은 `DRAWER:OPEN:n`을 RFCOMM 중계로 전달합니다. 현관에는 `[발신자]ALERT` / `[발신자]NORMAL`을 보냅니다. `PING`은 응답을 별도로 기다리며 기본 3초 만료, 중복 요청·대상 미연결 등의 오류를 구분합니다.

### 현관 센서등

| 방향 | 메시지 | 의미 |
|---|---|---|
| 현관 → 서버 | `[SERVER]EVT@ENTRANCE:PIRON` | PIR② LOW→HIGH |
| 서버 → 현관 | `[발신자]PING` | 연결 확인 |
| 현관 → 서버 | `[SERVER]OK@PING` | 확인 응답 |
| 서버 → 현관 | `[발신자]NORMAL` / `[발신자]ALERT` | 기본 모드 / 경고 |
| 현관 → 서버 | `[SERVER]OK@LIGHT:NORMAL` / `[SERVER]OK@LIGHT:ALERT` | 명령 접수 |

현관은 PIR 50ms 필터, HIGH 동안 점등·LOW 이후 **7초** 유지, 경고는 **250ms**마다 전환·**10초** 후 복귀합니다. Wi-Fi/TCP는 별도 태스크이며 연결이 없어도 로컬 센서등은 동작합니다. Wi-Fi 재시도 10초, TCP 재시도 5초, 대기 이벤트 최대 경과 5초가 현재 상수입니다.

서버는 이벤트 구독 ID(기본 `EVENT`)로 `[발신ID]EVT@...`를 전달합니다. Python은 현관 이벤트로 새 영상 재확인을 시작하고, SOS는 별도 알림으로 처리합니다.

## 4. 휴대폰 알림

`RETRACE_NTFY_URL`로 HTTP POST하며 `RETRACE_NTFY_TOPIC` 채널을 휴대폰 앱에서 구독합니다. 토픽이 비어 있으면 Python의 휴대폰 알림은 비활성화됩니다. 예제 URL은 Jetson 자신을 가리키는 `http://127.0.0.1`이며, 휴대폰에서는 접근 가능한 **Jetson IP와 ntfy 포트**로 같은 서버·토픽을 설정합니다. 토픽 이름은 운영 설정에 따릅니다.

| 알림 | 제목 | 우선순위 | 본문 |
|---|---|---:|---|
| 외출 준비물 | `Retrace Leave Check` | 5 | 확인된 물건 이름 목록 + `챙기셨나요?` |
| SOS | `Retrace SOS` | 4 | 발신 ID + `비상 버튼이 눌렸습니다.` |

영문 제목은 HTTP 헤더, 한글 본문은 UTF-8 텍스트로 전송합니다. 외출 확인은 기본 3초·confidence 0.50·3프레임 확인, 중복 제한은 기본 15초입니다. SOS는 재확인 없이 전송합니다. 휴대폰 TTS·사이렌 반복은 ntfy 앱·Tasker 등 휴대폰 설정으로 처리하며, 제목 필터는 위 문자열과 맞춥니다.

## 5. HTTP API

Python이 웹 정적 파일과 API를 같은 주소(기본 `http://<Jetson-IP>:8080`)로 제공합니다. 웹 기본 모드는 `live`입니다. LCD도 같은 HTTP API를 조회합니다.

| 메서드 | 경로 | 요청 본문 | 정상 응답 |
|---|---|---|---|
| GET | `/api/items` | 없음 | `{ "items": [LastSeen, ...] }` |
| GET | `/api/items/{item}` | 없음 | `LastSeen` |
| GET | `/snapshots/{file}` | 없음 | JPEG |
| POST | `/api/items/{item}/aim` | `{}` | `{ "ok": true }` |
| POST | `/api/drawers/{n}/open` | `{}` | `{ "ok": true }` |
| GET | `/api/leave-items` | 없음 | `{ "items": ["wallet", ...] }` |
| POST | `/api/leave-items` | `{ "items": ["wallet", ...] }` | `{ "ok": true, "items": [...] }` |
| POST | `/api/items/{item}/drawer` | `{ "drawer_id": 1 }` | `{ "ok": true }` |
| POST | `/api/drawer-items` | `{ "name": "물건 이름", "drawer_id": 1 }` | `{ "ok": true, "item": "custom_...", "display_name": "..." }` |

POST는 `Content-Type: application/json`, 물건 ID는 `[a-z][a-z0-9_]{0,31}`, 서랍은 1~6입니다. 외출 등록은 최대 20개이며 중복 ID를 제거합니다. 수동 이름은 제어문자를 제외한 1~40자입니다.

```json
{
  "item": "wallet",
  "pos_x": 412,
  "pos_y": 288,
  "seen_at": "2026-10-13T00:22:05.000000Z",
  "snapshot": "snapshots/rt_0123456789abcdef0123456789abcdef.jpg",
  "drawer_id": null,
  "state": "visible"
}
```

- `pos_x`·`pos_y`는 저장된 전체 프레임의 중심 좌표이며 둘 다 정수 또는 둘 다 `null`입니다.
- 관측 전 기록은 사진·시각·좌표가 `null`입니다. 미등록 조회는 `ITEM_NOT_FOUND` 오류와 구분합니다.
- `drawer_id`는 1~6 또는 `null`, `state`는 `visible` / `occluded` / `uncertain`입니다.
- 수동 서랍 등록은 `drawer_items.json`에 저장하며 API 응답에서 카메라 좌표·사진을 숨깁니다. 임의 물건에는 `display_name`도 제공됩니다.
- 서랍 열기 요청을 전달하면 등록을 해제합니다. 새 카메라 관측 전까지 좌표를 없는 상태로 표시합니다. 이 응답은 사용자가 물건을 실제로 꺼냈음을 감지한 결과가 아닙니다.
- 레이저 API의 성공은 USB 명령 시퀀스가 반환된 뒤 응답합니다. 서랍 API의 성공은 C 서버 중계 응답 기준입니다. 둘 모두 센서로 실제 조준·기구 위치를 보장하지 않습니다.

실패 응답은 `{ "error": { "code": "...", "message": "..." } }`입니다. 대표 상태는 잘못된 요청 400, 기록·경로 없음 404, 레이저 좌표 없음 409, 장치·DB 연결 오류 503입니다. 실제 연결 실패를 샘플 성공으로 바꾸지 않습니다.

### LCD 조회

`GET /api/items`는 기본 **10초**, `GET /api/leave-items`는 **15초** 주기로 조회합니다. 최근 사진·시각이 있는 기록 최대 3개를 표시하며, 수동 서랍 등록처럼 사진이 없는 기록은 목격 화면에서 제외합니다. 화면 전환은 기본 5초, 날씨 요청은 Open-Meteo로 별도 HTTPS 연결입니다.

## 6. 저장 API·사진 규칙

실제 C 공개 헤더는 [db_store.h](../jetson_nano/server_jetson/database/db_store.h)입니다. `rt_open`·`rt_close`, `rt_list`·`rt_get`·`rt_save`, `rt_load_snapshot`·`rt_recover`를 제공합니다. 핸들은 스레드별로 사용하고 목록·JPEG 버퍼는 호출자가 `free()`합니다.

| 필드 | 의미 |
|---|---|
| `item[33]` | 클래스·등록 물건 ID |
| `observed`, `has_position` | C 조회 결과의 관측·좌표 유무 |
| `pos_x`, `pos_y` | 전체 프레임 중심 좌표 |
| `seen_at[28]` | UTC `YYYY-MM-DDTHH:MM:SS.ffffffZ` |
| `snapshot[256]` | `data_dir` 아래 JPEG 상대 경로 |
| `drawer_id` | C의 0 = SQL NULL, 1~6 = 서랍 |
| `state[17]` | visible / occluded / uncertain |

Python은 `RETRACE_DATA_DIR/snapshots/rt_<소문자 16진수 32자리>.jpg`를 완전히 공개한 후 SAVE합니다. C 계층은 경로·JPEG·좌표를 검증하고 트랜잭션으로 최신 정보를 갱신합니다. 유효한 새 ID는 첫 저장 시 자동 등록합니다. 동일·과거 UTC 시각은 기존 기록을 덮어쓰지 않습니다.

TCP SAVE는 `SAVE@item:snapshot:x:y:utc:drawer:state` 형식이며 시각의 `:`는 `%3A`로 인코딩합니다. 사진 바이트는 DB에 넣지 않고 상대 경로만 저장합니다.

| 결과 | 값 | 처리 |
|---|---:|---|
| `RT_OK` | 0 | 성공 |
| `RT_NOT_FOUND` | 1 | 조회할 물건·사진 없음 |
| `RT_STALE` | 2 | 같은·과거 시각, 갱신 안 함 |
| `RT_ERROR` | -1 | 오류 문구 확인 |
| `RT_COMMIT_UNKNOWN` | -2 | 커밋 불확실, 새 사진 보존·재접속 후 확인 |
| `RT_CLEANUP_PENDING` | 3 | DB 저장 성공, 이전 파일 정리 남음 |

등록됐지만 관측 전이면 C 조회는 `RT_OK`, `observed=0`입니다. SQL 초기 목록의 `carkey`·`airpods`는 이전 이름이며 현재 탐지 이름 `car_key`·`earphones`와 별도 ID입니다. 문서 정리로 기존 DB 이름이나 데이터를 변경하지 않습니다.

사진 공개 실패면 DB 요청을 하지 않습니다. 확정된 저장 거부는 새 사진을 정리하고 기존 기록을 유지하며, 통신 오류·커밋 불확실은 사진을 보존합니다. `RT_CLEANUP_PENDING`은 저장 성공입니다. 파일·DB는 하나의 원자적 트랜잭션이 아니며, `rt_recover()`는 Python 저장을 멈춘 뒤 실행합니다. DB 오류·참조 파일 누락 시 복구 삭제를 멈춥니다.

## 변경 기록

| 버전 | 내용 |
|---|---|
| v0.8 | 현관 통신을 인증 Wi-Fi TCP로 변경, HTTP API 반영 |
| v0.9 | ESP32-C3 LCD 추가, 실제 `OK@`·`ERR@`·SOS·구동 값·ntfy·등록 API·영상 재확인·저장 동작으로 통일 |

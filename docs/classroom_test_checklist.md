# Retrace 장치·통합 검증 체크리스트

현재 소스의 예상 동작을 기준으로 한 재검증 목록입니다. 체크는 **실제로 실행하고 결과를 비교한 뒤** 표시합니다. 빈 칸은 이 문서에 결과가 기록되지 않았다는 뜻이며 기능 미구현을 뜻하지 않습니다.

## 1. 대상과 기준

| 대상 | 프로젝트 | 확인할 것 |
|---|---|---|
| 메인 STM32 | `stm32/main_unit` | USB·서보·레이저·PIR① |
| 서랍 STM32 | `stm32/drawer` | HC-06·PCA9685·서보 6개·LED·SOS |
| 현관 LOLIN D32 | `esp32/entrance_node` | PIR②·센서등·Wi-Fi TCP·경고 |
| LCD ESP32-C3 | `esp32/preparation_display` | I2C·Wi-Fi HTTP·목격·준비물·날씨 |
| Jetson·웹·휴대폰 | `jetson_nano`, `web` | 기록·조회·제어·ntfy |

순서: 전원·배선 확인 → 장치 단독 → 통신 → Jetson·웹·휴대폰·LCD 연동. 카메라와 레이저는 고정하며, 관찰 공간과 현관 PIR②의 위치는 별도로 구성합니다.

배포 문서의 기준은 `main`, 개발·통합 작업은 `develop`입니다. 실행 전에 업로드·실행한 커밋을 기록합니다. 핀·상수는 [핀맵](pinmap.md), 메시지는 [프로토콜](protocol.md)을 참고합니다.

## 2. 빌드 확인

STM32는 각 프로젝트 폴더를 VS Code에서 따로 열고 잠금 도구·CMake 프리셋을 준비한 뒤 빌드합니다. 이미 구성한 빌드 폴더가 있을 때:

```powershell
cmake --build stm32/main_unit/build/Debug
cmake --build stm32/drawer/build/Debug
```

ESP32의 PlatformIO 명령이 PATH에 등록된 경우:

```powershell
pio run --project-dir esp32/entrance_node --environment lolin_d32
pio run --project-dir esp32/entrance_node --environment lolin_d32_network_check
pio run --project-dir esp32/preparation_display --environment esp32c3
```

`lolin_d32_network_check`는 예제 설정으로 네트워크 코드의 컴파일·링크를 확인하는 환경이며 실제 업로드는 `lolin_d32`를 사용합니다. 빌드 성공·실물 성공은 구분하고 경고·오류·메모리 사용량은 실제 출력으로 기록합니다.

## 3. STM32 레이저 유닛

USB VCP 115200, 8N1, LF 종료. 카메라 좌표→각도는 Jetson, 각도→PWM은 STM32에서 처리합니다.

| 확인 | 입력·상황 | 예상 결과 |
|---|---|---|
| □ | `PING` | `OK@PING` |
| □ | `AIM:90,90` | `OK@AIM`, 실제 축 방향·위치 확인 |
| □ | `AIM:181,90` | `ERR@AIM:RANGE` |
| □ | `LASER:ON` / `LASER:OFF` | `OK@LASER`, 실제 점등·소등 |
| □ | `HOME` | `OK@HOME`, MCU 90°/90°·레이저 OFF |
| □ | PIR① 상태 변경 | 50ms 후 `EVT@PIR:1` / `EVT@PIR:0` |
| □ | PIR HIGH 유지 | 동일 이벤트 중복 없음 |
| □ | 웹 레이저 안내 | 보정 각도 이동·최대 5초 점등·소등·Python 복귀 위치 |
| □ | USB 응답 대기 중 PIR | 응답과 이벤트가 섞이지 않고 처리 |

현재 PWM 범위 700~2300µs가 설치 서보의 기계 범위에 맞는지 확인합니다. 카메라·레이저·관측 평면·해상도를 바꿨다면 보정을 다시 확인합니다.

## 4. 스마트 서랍

USB 테스트는 USART2 115200, 실제 HC-06은 USART1 9600입니다. 서랍은 정면 기준 위 `1 2` / 가운데 `3 4` / 아래 `5 6`, PCA9685 CH0~CH5입니다.

| USB | HC-06 | 입력·상황 | 예상 결과 |
|---|---|---|---|
| □ | □ | `PING` | `OK@PING` |
| □ | □ | `I2C:CHECK` | `OK@I2C:PCA9685` 또는 오류 |
| □ | □ | `DRAWER:OPEN:1`~`:6` | 해당 LED·서보, 시퀀스 뒤 `OK@DRAWER` |
| □ | □ | `DRAWER:OPEN:7` | `ERR@DRAWER:RANGE` |
| □ | □ | 동작 중 새 열기 요청 | `ERR@DRAWER:BUSY` |
| □ | □ | I2C 실패 | `ERR@DRAWER:I2C`, 성공으로 표시하지 않음 |
| □ | □ | 밀기·복귀 확인 | 1400µs·500ms → 1900µs·1000ms → Full OFF |
| □ | □ | LED 10초 경과 | 자동 OFF, 닫힘 감지와 구분 |
| □ | □ | `LED:n:ON/OFF`, `LED:ALL:OFF` | `OK@LED`, 해당 LED 동작 |
| □ | □ | SOS 50ms 이상 누름 | 양 UART에 `EVT@DRAWER:SOS` 한 번 |
| □ | □ | SOS 유지·해제·다시 누름 | 유지 중 중복 없음, 재누름 한 번 |

서보 팔 복귀가 서랍 자동 닫힘은 아닙니다. MCU 완료 응답과 C 서버의 중계 접수 응답도 구분합니다.

## 5. 현관 센서등

| 확인 | 입력·상황 | 예상 결과 |
|---|---|---|
| □ | Wi-Fi·TCP 설정 없이 PIR② | 로컬 센서등 동작 |
| □ | 서버 로그인 | `Server client connected`, 서버 ID·비밀번호 일치 |
| □ | PIR HIGH 50ms | 점등, `EVT@ENTRANCE:PIRON` 한 번 |
| □ | HIGH 유지 | 감지 이벤트 중복 없음 |
| □ | 안정적인 LOW 후 7초 | 기본 점등 OFF |
| □ | `ALERT` | `OK@LIGHT:ALERT`, 250ms 점멸·10초 후 복귀 |
| □ | `NORMAL` | 경고 취소, 남은 PIR 조건에 따른 점등 |
| □ | 새 `ALERT` | 경고 유지 시간 갱신 |
| □ | TCP 연결 끊김 | 기본 센서등 유지, 5초 간격 재접속 |
| □ | PIR 이벤트가 5초 이상 지연 | 오래된 이벤트 전달하지 않음 |

`network_config.example.h`를 개인 `network_config.h`로 복사해 Wi-Fi·Jetson 주소·서버 계정을 설정합니다. ESP32의 `localhost`는 Jetson이 아닙니다. 현관 전원은 시연 구성의 18650 두 개와 실제 전원 회로를 기준으로 확인합니다.

## 6. LCD

`display_config.h`에 Wi-Fi·Jetson HTTP 주소를 설정합니다. SDA GPIO4, SCL GPIO5, 주소 기본 0x27, 시리얼 115200입니다.

| 확인 | 입력·상황 | 예상 결과 |
|---|---|---|
| □ | 부팅·I2C | LCD 감지, 16×2 표시·대비 확인 |
| □ | 사진·시각이 있는 기록 조회 | 최근 물건 최대 3개, 이름·한국 시각 |
| □ | 목격 기록 갱신 | 다음 10초 주기 조회에 반영, 새 기록 5초 우선 표시 |
| □ | 웹 준비물 저장·전체 해제 | 다음 15초 주기 조회에 같은 목록 반영 |
| □ | 오전 07:00~08:00 | 준비물 페이지 포함 |
| □ | 날씨·시각 동기화 | 기온·예보·한국 시간 표시 |
| □ | 우산 조건 | 향후 3개 시간대 예보 기준 안내 |
| □ | 연결 실패·재연결 | `Cached`/`Offline` 등 표시, 성공 후 갱신 |
| □ | 미조회·기록 없음·수동 서랍 등록 | 대기·기록 없음 구분, 사진 없는 기록 제외 |

페이지 전환 기본 5초와 데이터 조회 주기를 구분합니다. 일반 LCD의 영문 표시, I2C 풀업 전압·전원·필요한 레벨 변환은 [LCD 문서](../esp32/preparation_display/README.md) 기준으로 확인합니다.

## 7. 전체 연동·저장 실패

| 확인 | 입력·상황 | 예상 결과 |
|---|---|---|
| □ | PIR① 움직임·종료 | `ACTIVE` → `GRACE` → LOW 10초 후 `IDLE` |
| □ | 안정 인식·위치 이동 | 저장 조건 충족 시 JPEG·DB 갱신 |
| □ | 물건 가림 | 마지막 사진·시각 유지 |
| □ | JPEG 저장 실패 | DB 요청 없음·오류 보고 |
| □ | 확정된 DB 저장 실패 | 기존 기록 유지, 새 미참조 사진 정리 |
| □ | 커밋 불확실·통신 오류 | 새 사진 보존, 성공으로 확정하지 않음 |
| □ | 웹 목록·사진·레이저 | 같은 origin, 저장 좌표로 안내 |
| □ | 수동 서랍 등록·열기 | 등록 정보 적용, 열기 후 재관측 전 좌표 없음 |
| □ | PIR②·등록 물건이 영상에 남음 | 약 3초 재확인 후 ntfy·현관 ALERT |
| □ | PIR②·확인 물건 없음 | 외출 알림 생략 |
| □ | PIR① IDLE에서 PIR② | 외출 재확인 영상 분석은 별도 실행 |
| □ | SOS | 재확인 없이 `Retrace SOS` 알림, 휴대폰 설정에 따른 소리 |
| □ | 외출 ntfy | `Retrace Leave Check`, 실제 확인한 이름 목록 |
| □ | 폰 잠금·웹 종료 상태 | 앱 수신·권한·TTS/알림음 설정 결과 기록 |
| □ | 중복 이벤트 | 기본 15초 제한 확인 |
| □ | 저장 중지 후 `rt_recover` | 참조 중인 사진 보존, 고아 관리 파일만 정리 |

## 8. 결과 기록

| 대상 | 날짜·테스터 | 실행 커밋·환경 | 통과·미통과 | 보정값·실제 결과 |
|---|---|---|---|---|
| 메인 유닛 | | | | |
| 서랍 USB·HC-06 | | | | |
| 현관 센서등 | | | | |
| LCD | | | | |
| Jetson·웹·휴대폰 | | | | |

과거의 빌드·모의 테스트 수치로 현재 실물 성공을 대신하지 않습니다. 실패 항목은 원인을 확인하고 필요한 부분만 수정한 뒤 다시 확인합니다.

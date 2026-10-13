# ESP32-C3 정보 표시 LCD

ESP32-C3 Super Mini와 4핀 I2C 1602 LCD용 독립 프로젝트입니다. Jetson의 마지막 목격 기록과 웹에서 저장한 외출 준비물 목록을 읽으며 서랍·현관등·카메라·레이저 제어는 변경하거나 호출하지 않습니다.

## 표시 동작

- **마지막 목격 화면:** 기존 `GET /api/items`를 **10초마다** 읽고, 카메라 사진과 발견 시각이 있는 물건 중 최근 기록 **3개**를 최신순으로 표시합니다. 각 물건의 마지막 목격 기록이며, 같은 물건의 전체 과거 이력은 아닙니다.
- DB의 UTC 시각을 **한국 시간으로 변환**해 월/일과 시:분을 표시합니다. 첫 조회는 기존 기록을 그대로 보여주고, 이후 더 최신의 목격 기록이 들어오면 화면을 전환해 `Record updated`와 해당 물건을 **5초간 우선 표시**한 뒤 다음 화면부터 순환을 재개합니다. 기록이 그대로면 조회만으로 화면을 처음으로 돌리지 않습니다. 현재 NTP 동기화 전에도 DB 시각은 한국 시간으로 표시됩니다.
- 수동 서랍 보관 등록은 API에서 사진이 없는 기록으로 제공되므로 목격 화면에서 제외합니다. 최근 조회가 실패하거나 Wi-Fi가 끊어지면 기존 목격 값에 `Cached`를 표시합니다. 조회 전에는 `Records pending`, 정상 조회 후 목격 값이 없으면 `No camera record`를 표시합니다.
- 서울 날씨·기온·현재 시각과 앞으로 3개 시간대의 최대 강수확률을 하루 종일 표시합니다.
- 한국 시간 **07:00 이상, 08:00 미만**에는 준비물 페이지도 함께 표시합니다.
- 기존 Jetson `GET /api/leave-items`를 **15초마다** 조회합니다. 웹에서 선택을 저장하면 다음 성공적인 조회에 자동 반영됩니다. 별도의 준비물 목록을 만들지 않습니다.
- 화면은 기본 **5초마다** 다음 페이지로 넘어갑니다. 기존 개인 설정의 `PAGE_MS`가 3초여도 최소 5초를 적용합니다. 네트워크 요청 중에는 설정한 타임아웃만큼 전환이 잠시 늦어질 수 있습니다. 데이터 조회 주기(목격 10초·준비물 15초·날씨 10분)는 그대로입니다.
- 날씨는 Open-Meteo에서 **10분마다** 갱신하며 실패하면 **30초 후 재시도**합니다. 현재 시간대부터 3개 시간대 중 강수확률이 **50% 이상**이거나 예상 강수량이 **0보다 크면** `Bring umbrella!` 페이지를 추가합니다. 비가 실제로 내리고 있음을 감지하는 기능은 아닙니다. 우산 안내는 웹 선택 목록을 변경하지 않습니다.
- 조회 실패 시 이전 성공 값을 유지하고 `Cached`, `Offline`, `Weather old`로 표시합니다. 오래된 예보로 우산 안내를 계속 표시하지 않습니다. 재부팅하면 캐시는 초기화됩니다.
- 일반 HD44780 1602는 한글을 표시하지 못하므로 `Car keys`, `Wallet`, `Phone` 등의 영문 표시를 사용합니다. 알 수 없는 항목은 저장된 영문 ID를 표시합니다. 수동 등록 물건의 `custom_...` ID는 설정의 `ITEM_ALIASES`에서 영문 이름을 지정할 수 있습니다. 16자를 넘는 이름은 잘립니다.

예:

```text
Last seen 10/12
Wallet 09:42

Seoul 07:20
18.2C Cloudy

Bring 1/3
Car keys

Rain expected
Bring umbrella!
```

## 설정과 배선

1. `include/display_config.example.h`를 같은 폴더의 `display_config.h`로 복사합니다. 실제 설정 파일은 이 프로젝트의 `.gitignore`로 제외됩니다.
2. `WIFI_SSID`, `WIFI_PASSWORD`를 입력합니다. C3는 2.4GHz Wi-Fi를 사용합니다.
3. `RETRACE_API_BASE`를 실제 Jetson 카메라 HTTP API 주소로 바꿉니다. 기본 예시 `192.168.0.100`은 실제 주소가 아닙니다. API 기본 포트는 현재 코드 기준 `8080`이며 `RETRACE_HTTP_PORT` 설정에 따라 달라질 수 있습니다. 임시 웹 서버의 `127.0.0.1:5180`을 넣으면 안 됩니다.
4. C3와 Jetson이 서로 접근 가능한 네트워크에 있어야 합니다. 날씨와 NTP 시각 동기화에는 인터넷이 필요합니다. 시각 동기화 전에는 준비물 시간대를 판단하지 않고 `Time sync`를 표시합니다.
5. 기본 배선은 SDA → **GPIO4**, SCL → **GPIO5**, GND → 공통 GND입니다. 기본 I2C 주소는 **0x27**입니다. 실제 백팩 주소가 다르면 `LCD_ADDRESS`를 바꿉니다. LCD가 응답하지 않으면 시리얼에 오류를 출력합니다.
6. LCD 백팩의 전원·I2C 풀업 전압을 확인합니다. **5V 백팩의 SDA/SCL을 C3의 3.3V GPIO에 바로 연결하지 않습니다.** 5V로 동작하는 백팩이면 양방향 I2C 레벨 변환기를 사용합니다. LCD 대비는 백팩의 가변저항으로 조절합니다.

`ITEM_ALIASES` 예:

```cpp
constexpr ItemAlias ITEM_ALIASES[] = {
    {"custom_example", "My item"}, // ID는 웹의 실제 저장 ID로 교체
};
```

ID에는 기존 API가 허용하는 영문 소문자·숫자·밑줄만 사용됩니다. 위 `custom_example`은 설명용 자리표시자입니다.

## 빌드·업로드

PlatformIO에서 이 폴더를 열고 환경 `esp32c3`로 Build를 실행합니다. 설정의 보드 ID는 `esp32-c3-devkitm-1`이며 실제 사용 제품은 ESP32-C3 Super Mini입니다.

```powershell
pio run --project-dir esp32/preparation_display --environment esp32c3
```

포트·실제 설정을 확인한 뒤 업로드합니다. CLI가 PATH에 있으면 저장소 루트에서:

```powershell
pio run -d esp32/preparation_display
pio run -d esp32/preparation_display -t upload
pio device monitor -d esp32/preparation_display
```

업로드는 실제 연결한 C3 포트를 선택해서 진행합니다. 펌웨어 빌드 성공은 LCD 배선, 인터넷, Jetson 실제 접근 확인을 대신하지 않습니다.

## 검증 순서

1. 시리얼 115200에서 LCD 발견 여부와 Wi-Fi 연결 후 `Items updated`를 확인합니다.
   `Last seen refreshed`와 이름·발견 시각도 확인합니다. 카메라 기록을 갱신하고 다음 성공적인 조회에 바뀌는지 확인합니다. LCD 기록 이름은 시각 공간을 확보하기 위해 최대 10자로 표시합니다.
2. 웹에서 선택을 변경하고 다음 조회에 목록이 바뀌는지 확인합니다. 전체 선택 해제도 확인합니다.
3. 오전 7~8시 준비물 페이지를 확인합니다. 시간대 시험은 로컬 설정에서 잠시 현재 시각에 맞추고 시험 후 7~8시로 복원합니다.
4. `Weather` 로그와 기온·예보·강수확률을 확인합니다. 비 없는 날 우산 안내 시험은 임시로 강수확률 기준을 0으로 바꾼 뒤 시험 후 50으로 복원합니다.
5. 네트워크를 끊었을 때 이전 기록에 `Offline`/`Cached`가 표시되고 재연결 후 갱신되는지 확인합니다.

## 자료·인증서

- 날씨 데이터: [Open-Meteo API](https://open-meteo.com/en/docs). 무료 비상업용 API를 사용합니다.
- LCD 드라이버: [LiquidCrystal_PCF8574 2.3.0](https://github.com/mathertel/LiquidCrystal_PCF8574).
- HTTPS는 인증서 검증을 사용합니다. `data/weather_roots.pem`은 설치된 **certifi 2026.7.22**의 Mozilla CA 번들 사본입니다. 펌웨어에는 PEM 전체 대신 **`weather_roots.crt_bundle` 압축 번들**을 넣습니다. PEM 전체를 `setCACert`로 읽으면 C3에서 `X509 - Allocation of memory failed`가 발생할 수 있어, `setCACertBundle` 방식으로 바꿨습니다. 같은 CA 목록을 유지하면서 런타임 메모리 사용을 줄입니다.
- 인증서 갱신 시 PEM을 갱신한 뒤 `python esp32/preparation_display/tools/build_cert_bundle.py`를 실행하고 다시 빌드합니다. 생성된 `.crt_bundle`도 소스와 함께 보관합니다. 형식은 [Espressif 인증서 번들](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32c3/api-reference/protocols/esp_crt_bundle.html)을 따릅니다. 라이선스는 `data/NOTICE.txt`에 기록했습니다.

## 공통 문서

- [전체 시스템](../../README.md)
- [현재 핀맵](../../docs/pinmap.md)
- [조회 API와 메시지](../../docs/protocol.md)
- [LCD·전체 연동 확인](../../docs/classroom_test_checklist.md)

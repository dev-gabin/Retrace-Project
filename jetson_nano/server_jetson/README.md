# Jetson TCP Server and Database

Jetson Nano에서 실행할 C TCP 서버와 MariaDB 저장 모듈의 안내입니다. 이 문서는 `jetson_nano/server_jetson/` 기준으로 빌드·실행 방법과 각 모듈의 책임을 설명합니다.

현재 구현은 TCP 로그인, 클라이언트 메시지 라우팅, DB의 `LIST`·`GET`·`SAVE`, JPEG 스냅샷 검증 및 관리입니다. 장치 `SET@clientID:OPEN:n`은 지정한 클라이언트로 라우팅하며, `EVT@clientID:SOS`는 서버 로그에만 기록합니다. HTTP API, 카메라/AI 파이프라인, 실제 SOS 후속 동작은 아직 구현되지 않았습니다.

## 빌드 대상과 구성

- 대상: Jetson Nano / Ubuntu 20.04 / ARM64 / Linux POSIX C
- DB: MariaDB, 기본 DB 이름 `retrace`
- C 라이브러리: MariaDB Connector/C와 libjpeg
- Windows 작업 복사본은 소스 편집용입니다. 실행 파일은 Jetson에서 빌드해야 합니다.
- 프로젝트 루트의 `.env.example`은 서버 환경 변수 예시입니다. 실제 비밀번호와 운영 자격 증명은 Git에 넣지 않습니다.

| 경로 | 역할 |
|---|---|
| `server_main.c` | 환경 변수·포트 설정 및 서버 시작 |
| `server/server.c/.h` | TCP accept, 로그인, 클라이언트 연결과 메시지 라우팅 |
| `server/db_handler.c/.h` | `LIST`·`GET`·`SAVE` 명령 파싱, DB 저장 계층 호출, 응답 작성 |
| `database/db_store.c/.h` | MariaDB 연결, 관측 조회·저장, JPEG 스냅샷 관리와 복구 |
| `database/jpeg_check.c/.h` | JPEG 디코딩·크기 검증 |
| `database/db_store_cli.c` | 서버와 별개로 DB 저장 API를 점검하는 명령줄 도구 |
| `server/device_handler.c/.h` | `SET@clientID:OPEN:n` 검증·대상 라우팅용 명령 생성 |
| `database/init.sql` | DB 테이블, 초기 물건 3개, 앱 계정과 자동 물건 등록 권한 생성 |

## 테이블

`items`: 물건 목록. `item` 기본 키와 한글 표시명 `display_name`을 저장합니다.
초기 목록은 `carkey`(차키), `airpods`(에어팟), `wallet`(지갑) 세 개입니다. `rt_save`는 유효한 새 물건 ID를 처음 저장할 때 `items`에 자동 등록하며, 이때 표시명은 우선 물건 ID와 같게 저장합니다.

`last_seen`: 관찰한 물건의 최신 기록. `item`이 기본 키이자 `items`의 외래 키이므로 물건별 최대 한 행입니다.

| 필드 | DB 타입 | 규칙 |
|---|---|---|
| item | VARCHAR(32) | 등록된 물건 ID, 대소문자 구분 |
| pos_x / pos_y | INT NULL | 전체 카메라 이미지의 물건 위치(px); 둘 다 NULL 또는 둘 다 0 이상 |
| seen_at | DATETIME(6) | NULL 불가. UTC로 변환한 실제 관찰 시각; 자동 생성 시각 아님 |
| snapshot | VARCHAR(255) NULL | `snapshots/파일명.jpg`; 파일명은 영문·숫자·밑줄·하이픈 |
| drawer_id | TINYINT NULL | NULL 또는 1~6 |
| state | VARCHAR(16) | visible / occluded / uncertain |

사진 바이트는 DB에 넣지 않습니다. 전체 화면 JPEG 파일을 저장하고 그 상대 경로만 DB에 넣습니다.
DB는 파일의 존재 여부나 픽셀 좌표가 실제 영상 크기 안에 있는지 검사할 수 없습니다. C 저장 모듈은 JPEG 전체 디코딩과 좌표 범위 검사를 수행합니다.
관찰 전에는 `last_seen` 행이 없습니다. 전체 목록은 LEFT JOIN으로 조회하며 관찰 필드는 NULL, 상태는 uncertain으로 표시합니다.
예를 들어 `SELECT * FROM items;`는 등록된 물건을, `SELECT * FROM last_seen;`는 관측된 최신 기록을 조회합니다.

## 접속

PowerShell에서 관리자 DB 콘솔:

```powershell
wsl -d Ubuntu-20.04 -u root -- mysql retrace
```

WSL에서 애플리케이션 계정으로 접속:

```sh
cd /path/to/Retrace-Project
mysql --defaults-extra-file=jetson_nano/.local/mysql-client.cnf
```

초기화 SQL은 `retrace` 애플리케이션 계정을 만들고 초기 비밀번호 `retrace`를 설정합니다. 이는 개발 초기값이므로 실제 장치에 배포하기 전에 변경하세요. 실행 시 서버는 `RETRACE_DB_PASSWORD`를 환경 변수에서 읽습니다. 비밀이 든 `.local/db.env`와 `mysql-client.cnf`는 Git에 추가하지 말고, Jetson에서는 파일 권한을 제한하세요.
`retrace`는 items 조회·추가와 last_seen 조회·추가·수정·삭제만 가능해야 합니다. 테이블 생성/삭제, 사용자 관리, 다른 DB 접근 권한은 부여하지 않습니다.
Windows의 localhost:3306 TCP 연결도 확인했지만, DB 계정의 SQL 로그인 검증은 WSL 안에서 수행했습니다.

## 새 환경에 구축

MariaDB 서버와 클라이언트를 설치한 뒤 관리자 계정으로 실행합니다.

새 환경에서는 관리자 계정으로 프로젝트 루트의 초기화 SQL을 실행합니다.

```sh
sudo mysql --default-character-set=utf8mb4 < jetson_nano/server_jetson/database/init.sql
```

초기화 SQL은 현재 저장소의 `jetson_nano/server_jetson/database/init.sql`입니다. 관리자 권한으로 실행해야 하며, 이미 있는 관측 데이터를 초기화하거나 삭제하지 않습니다.

## C 저장 모듈의 처리 규칙

1. Python이 전체 화면 사진을 고유한 새 파일명으로 `snapshots/`에 완전히 저장한 뒤 경로를 전달합니다. 파일명은 `rt_<32 lowercase hex digits>.jpg` 형식이어야 합니다. `rt_save`는 파일을 검증하고 상대 경로를 DB에 기록합니다.
2. DB별 named lock과 저장 폴더 파일 잠금을 잡고 기존 관찰 시각을 비교합니다. 소규모 물건 목록이므로 저장 요청을 직렬 처리합니다. 늦게 도착한 과거 관찰이 최신 기록을 덮어쓰면 안 됩니다.
3. 처음 보는 유효한 물건 ID는 `items`에 자동 등록하고, 준비된 문장(prepared statement)으로 해당 물건의 최신 관찰 행을 INSERT 또는 UPDATE한 뒤 함께 COMMIT합니다.
4. 커밋 성공 뒤 이전 사진을 삭제합니다. 저장/DB 실패 시 기존 기록을 유지하고 새 임시 파일을 정리합니다.

최종적으로 물건별 최신 사진 하나만 유지합니다. 갱신 도중에는 잠시 두 파일이 존재할 수 있습니다.
파일 저장과 DB 커밋은 하나의 원자적 트랜잭션이 아닙니다. `rt_recover`는 DB를 읽고 참조 중인 파일의 존재를 확인한 뒤 관리 대상 이름의 고아 파일만 정리합니다. DB 조회 실패나 참조 파일 누락 시 삭제하지 않고 오류를 반환합니다. 사용자가 만든 다른 이름의 사진은 삭제하지 않습니다.
DB 자체는 직접 들어오는 오래된 UPDATE를 차단하지 않으므로 위 시각 비교는 애플리케이션의 책임입니다.

## 검증

현재 저장소에는 자동 통합 테스트 스크립트가 포함되어 있지 않습니다. 아래 명령은 컴파일만 검증하며, 실제 DB/JPEG 통합 검증은 Jetson 또는 동등한 Linux 환경에서 별도로 수행해야 합니다.

```sh
cd /path/to/Retrace-Project
cmake --build jetson_nano/server_jetson/build -j2
```

제품 코드는 C로 구현합니다. 자동화된 DB/JPEG 통합 검증은 아직 구성되어 있지 않습니다.


## C 코드와 실행

| 파일 | 역할 |
|---|---|
| `database/db_store.h` | DB 저장 모듈의 공개 C API와 레코드 구조체 |
| `database/db_store.c` | DB 조회·prepared statement 갱신, 관리 JPEG 검증·교체/복구 |
| `jpeg_check.c` | 전체 JPEG 디코딩 검증과 크기 확인 |
| `database/db_store_cli.c` | 서버와 별도로 DB 저장 API를 점검하는 CLI |
| `server_main.c` | 환경 설정을 읽고 `server_jetson`을 시작하는 진입점 |
| `server/server.c` | TCP 인증·세션·메시지 라우팅 |
| `server/db_handler.c` | LIST/GET/SAVE 프로토콜 요청을 DB 저장 API로 연결 |
| `server/device_handler.c/.h` | `SET@clientID:OPEN:n` 검증·대상 라우팅용 명령 생성 |

Jetson의 카메라·Last Seen·STM32 연결은 Python 모듈을 사용하며, TCP 서버와 DB 저장 계층은 기존 C 구현을 사용합니다.

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libmariadb-dev libjpeg-dev
cd /path/to/Retrace-Project
cmake -S jetson_nano/server_jetson -B jetson_nano/server_jetson/build -DCMAKE_BUILD_TYPE=Release
cmake --build jetson_nano/server_jetson/build -j2
set -a
. jetson_nano/.local/db.env
set +a
./jetson_nano/server_jetson/build/retrace-db list
./jetson_nano/server_jetson/build/retrace-db get carkey
# 실제 사진 경로와 실제 UTC 관찰 시각으로 대체하세요.
./jetson_nano/server_jetson/build/retrace-db save carkey snapshots/rt_0123456789abcdef0123456789abcdef.jpg 412 288 2026-10-05T03:00:00.000001Z 0 visible
./jetson_nano/server_jetson/build/retrace-db image carkey > /tmp/carkey.jpg
./jetson_nano/server_jetson/build/retrace-db recover
```

마지막 인자 두 개는 서랍 번호(0=NULL, 1~6)와 상태(visible/occluded/uncertain)입니다.
시간은 6자리 소수초와 Z가 있는 UTC 형식만 받습니다. 동일 시각은 중복으로 간주하여 기존 기록을 유지합니다.
좌표는 0부터 시작하는 전체 화면 좌표입니다. JPEG는 최대 32 MiB, 가로·세로 각 8192 이하, 총 3200만 픽셀 이하입니다.
OpenCV/ONNX 의존성은 없습니다. Python 인식기가 JPEG 파일을 `RETRACE_DATA_DIR/snapshots/`에 완전히 저장한 뒤 상대 경로와 메타데이터를 `rt_save`에 전달합니다.

### TCP 서버 실행

저장소 루트에서 환경 변수를 내보내고 서버를 실행합니다. 인증 파일은 `idpasswd.txt.example`을 복사해 `idpasswd.txt`로 만든 뒤, 예시 자격 증명을 고유한 값으로 바꾸세요. 한 줄에 `CLIENT_ID PASSWORD` 한 쌍을 기록하며 실제 `idpasswd.txt`는 Git에 올리지 않습니다.

```sh
cmake --build jetson_nano/server_jetson/build --target server_jetson -j2
set -a
. jetson_nano/.local/db.env
set +a
./jetson_nano/server_jetson/build/server_jetson
```

기본 TCP 포트는 5000이며 `RETRACE_SERVER_PORT` 또는 첫 번째 실행 인자로 바꿀 수 있습니다. 각 연결은 별도 `RtStore` 핸들을 열어 DB 연결을 스레드 간 공유하지 않습니다. 현재 실행 파일은 TCP 서버이며 Web HTTP API는 아직 구현하지 않았습니다.

TCP 로그인은 기존 `[ID:비밀번호]` 한 번으로 시작합니다. PING 왕복 확인은 `[SERVER]PING@clientID`를 보냅니다. 서버는 대상 client에 `PING`을 중계하고, 대상에서 돌아온 `OK@PING` 또는 `ERR@PING:...` 응답 본문을 요청자에게 그대로 전달합니다. 동일 대상에 대한 확인은 한 번에 하나만 대기하며, 다른 요청이 동시에 오면 `ERR@PING:BUSY`, 3초 안에 응답이 없으면 `ERR@PING:TIMEOUT`을 반환합니다. 대상이 연결되지 않았거나 전달에 실패하면 각각 `ERR@PING:UNKNOWN_ID`, `ERR@PING:DELIVERY`입니다. DB 서버 대상 요청은 `[SQL]LIST`, `[SQL]GET@wallet`, `[SQL]SAVE@wallet:snapshots/rt_<32자리 hex>.jpg:412:288:2026-10-06T03%3A00%3A00.000001Z:0:visible`처럼 한 줄로 보냅니다. DB 응답의 발신자 표기는 `[SQL]`입니다. SAVE 필드는 콜론으로 구분하고, 시각 내부의 콜론은 `%3A`로 인코딩합니다.

### 공개 API

- `rt_open` / `rt_close`: 설정으로 접속 및 자원 정리.
- `rt_list`: 등록된 물건을 모두 조회; 아직 관찰하지 않은 물건도 포함.
- `rt_get`: 물건의 최신 메타데이터 조회.
- `rt_save`: 이미 저장한 관리 JPEG 경로와 관찰 정보를 DB에 연결. 유효한 새 물건 ID는 자동 등록합니다. 파일은 호출자가 먼저 완전히 저장하고 이후 변경하지 않아야 합니다. DB 저장에 실패한 미참조 파일은 `rt_recover`가 정리할 수 있습니다.
- `rt_load_snapshot`: 현재 사진 바이트 조회. 파일 교체와 동시에 실행해도 저장 잠금으로 보호.
- `rt_recover`: 시작 시 또는 오류 복구 시 명시적으로 호출하는 고아 파일 정리.

리스트와 JPEG 반환 버퍼는 호출자가 `free()`합니다. 핸들은 스레드 간 공유하지 않습니다.
`rt_get`으로 받은 경로는 다음 갱신 때 삭제될 수 있습니다. 사진이 필요하면 `rt_load_snapshot`을 호출하세요.
한 DB는 반드시 하나의 공유된 저장 폴더로 접근합니다. 저장 폴더에는 DB 식별 파일이 생성되며 다른 DB 설정으로 열면 거부합니다.
다른 컴퓨터로 옮긴다면 SQL 덤프와 사진 폴더를 함께 복사해야 합니다. DB 호스트·포트·이름을 바꾸면 `.database` 식별 파일을 관리자가 검토 후 함께 갱신해야 합니다.
외부 SQL 작성자는 라이브러리 잠금·시각 비교를 우회할 수 있으므로 관찰 기록 쓰기는 이 모듈로 통일합니다.

| API 결과 | CLI 종료 코드 | 의미 |
|---|---|---|
| RT_OK | 0 | 성공 |
| RT_NOT_FOUND | 2 | 조회한 물건 또는 사진 없음 |
| RT_STALE | 3 | 같거나 과거 시각; 변경 없음 |
| RT_CLEANUP_PENDING | 4 | DB 저장은 성공, 이전 파일 정리/동기화 재시도 필요 |
| RT_COMMIT_UNKNOWN | 5 | 커밋 응답 불확실; 사진 보존, 핸들을 닫고 재접속·조회·recover |
| RT_ERROR | 1 | 실패; rt_error로 원인 조회 |

커밋 불확실 상태에서 새 사진을 지우면 DB가 이미 그 사진을 가리킬 수 있어 보존합니다.
디스크 완전 고장/전원 장애까지 검증한 것은 아닙니다. `fsync` 및 원자적 파일 공개로 순서를 지키며, 실제 SD 카드의 내구성과 정전 테스트는 별도입니다.

## C 통합 테스트

```sh
cmake -S jetson_nano/server_jetson -B jetson_nano/server_jetson/build -DRETRACE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build jetson_nano/server_jetson/build -j2
echo "자동 통합 테스트 스크립트는 현재 포함되어 있지 않습니다."
```

`RETRACE_BUILD_TESTS=ON`은 현재 테스트용 CLI만 추가 빌드합니다. 배포 빌드는 `RETRACE_BUILD_TESTS=OFF`로 구성하세요.

## Jetson Nano 배포 (Ubuntu 20.04 / ARM64)

WSL 실행 파일은 x86_64이므로 Jetson에 복사해서 실행할 수 없습니다. **소스를 복사하고 Jetson에서 다시 빌드**합니다.

1. Jetson에 `mariadb-server mariadb-client build-essential cmake pkg-config libmariadb-dev libjpeg-dev`를 설치합니다. 오프라인이면 ARM64용 의존 패키지를 별도로 준비해야 하며 WSL amd64 패키지를 사용하면 안 됩니다.
2. 프로젝트 소스를 `/home/jetson/Retrace-Project` 등에 복사합니다. `build/`, 테스트 출력, WSL의 실제 비밀번호는 배포본에서 제외합니다.
3. `sudo systemctl enable --now mariadb` 후 이 문서의 `init.sql`을 관리자 권한으로 실행합니다. SQL이 `retrace` 앱 계정을 만들며, 초기 비밀번호는 운영 전에 변경해야 합니다.
4. `jetson_nano/.local/db.env`를 `jetson_nano/server_jetson/.env.example` 기준으로 만들고 권한 600을 적용합니다. `RETRACE_DATA_DIR=/home/jetson/retrace-data`처럼 영구 저장 경로를 지정합니다.
5. 새 빌드 폴더에서 `cmake -S jetson_nano/server_jetson -B jetson_nano/server_jetson/build -DRETRACE_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release` 및 `cmake --build jetson_nano/server_jetson/build -j2`를 실행합니다.
6. `jetson_nano/server_jetson/build/retrace-db list`, 실제 사진으로 save/get/image, recover를 확인합니다. 카메라·인식 모델은 이 단계에 필요하지 않습니다.

순수 C11과 Linux/POSIX API, MariaDB Connector/C, libjpeg를 사용합니다. ARM 전용 최적화나 CUDA에 의존하지 않습니다.
Jetson에서는 일반 jetson 계정이 저장 폴더 소유자가 되도록 실행하며 root로 사진을 만들지 않습니다.
새 관찰마다 SD 카드에 파일과 DB를 동기화하므로 모든 영상 프레임을 저장하지 마세요. 관찰 확정/위치 변경 같은 저장 주기는 이후 인식 단계에서 정합니다.

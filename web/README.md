# Retrace 웹 뼈대

`feature/web`에서 작업하는 정적 HTML/CSS/JavaScript 화면입니다. Jetson 서버·DB·카메라 인식 코드는 포함하지 않습니다. 기본값은 **샘플 모드**이며, 버튼은 샘플 요청만 처리합니다.

## 실행

저장소 루트에서 Node.js 20 이상으로:

```powershell
node web/dev-server.mjs
```

브라우저에서 `http://127.0.0.1:5173/`를 엽니다. 외부 패키지 설치·프런트엔드 빌드가 필요하지 않습니다. 이 서버는 로컬 화면 확인용이며 Jetson 서버 구현이 아닙니다. 종료는 터미널에서 Ctrl+C입니다. 포트가 사용 중이면 `PORT` 환경 변수로 다른 포트를 정합니다.

```powershell
$env:PORT = '5174'
node web/dev-server.mjs
```

파일을 직접 더블클릭하는 `file://` 방식은 JavaScript 모듈 로딩을 지원하지 않을 수 있으므로 HTTP로 엽니다.

## 파일 역할

| 파일 | 역할 |
|---|---|
| `index.html` | 물건 목록·상세·서랍·부저 화면, 접근성 구조 |
| `styles.css` | PC·휴대폰 레이아웃 |
| `app.js` | 화면 상태·검색·선택·요청 대기·오류·알림 |
| `api.js` | 기존 HTTP 경로·JSON 검사·샘플/실제 모드·시간 초과 |
| `config.js` | 기본 샘플 모드·서버 주소·요청 시간 제한 |
| `demo-data.js` | 임시 API 형식의 샘플 기록, 장치 요청 흉내 |
| `assets/demo-scene.svg` | 실제 사진이 아닌 샘플 장면 |
| `dev-server.mjs` | 127.0.0.1에만 연결되는 로컬 정적 파일 확인 서버 |
| `package.json` | JavaScript 모듈 설정·선택적인 start/test 명령, 외부 의존성 없음 |
| `tests/api.test.mjs` | 경로·요청 본문·응답 검사·실패 처리 검증 |

## API 기준과 짝꿍 연결 위치

기준 문서는 [통신 프로토콜 7장](../docs/protocol.md#7-webpwa--jetson-http-api--초안)입니다. **기존 메서드·경로를 그대로 사용**합니다. JSON 형식은 같은 문서 7-1의 임시안이며 Jetson 담당자와 합의 전입니다. 변경할 때 프로토콜과 `api.js`를 함께 고칩니다.

- 목록: `GET /api/items` → `{ "items": [...] }`.
- 상세: `GET /api/items/{item}` → `LastSeen` 객체.
- 안내: `POST /api/items/{item}/aim`, 본문 `{}` → `{ "ok": true }`.
- 서랍: `POST /api/drawers/{n}/open`, 본문 `{}` → `{ "ok": true }`.
- 부저: `POST /api/buzzer`, 본문 `{ "enabled": true/false }` → `{ "ok": true }`.
- 사진: `snapshot`의 `snapshots/...` 경로를 같은 서버에서 조회.

응답 접수는 실제 장치 동작 확인과 구분합니다. 화면은 접수·실패를 표시하며 장치 연결 상태를 추측하지 않습니다. 부저는 단일 태그 기준이고 켜기·끄기 요청을 별도로 보냅니다. 서랍은 실제 번호 배치와 같은 2열×3행이며, 복귀는 서보 팔 복귀입니다. API가 없는 레이저 끄기·HOME·LED 수동 제어·현관등 수동 제어는 이번 화면에 추가하지 않았습니다.

### 실제 서버로 바꾸기

1. Jetson 담당자가 위 API와 JSON 형식을 확인·구현합니다.
2. Jetson 정적 파일 서버에서 `web/` 파일을 제공합니다. 서버가 `index.html`과 `/api/...`, `/snapshots/...`를 같은 origin에서 제공하는 방식이 기본입니다.
3. `config.js`의 `mode`를 `live`로 바꿉니다. 같은 origin이면 `apiBaseUrl`은 빈 문자열로 둡니다.
4. PC의 로컬 화면에서 별도 Jetson을 테스트할 때만 `apiBaseUrl`을 Jetson HTTP 주소로 정합니다. 서로 다른 origin이면 Jetson 담당자가 CORS를 설정해야 합니다. 이 뼈대는 CORS 우회나 인증 처리를 구현하지 않습니다.
5. 목록·상세·사진 조회부터 확인하고, 실제 기기를 연결한 후 안내·서랍·부저 요청을 확인합니다. `live` 모드의 버튼은 실제 요청을 보내므로 샘플 화면과 구분합니다.

실제 서버 연결 실패·잘못된 JSON·시간 초과 시 오류를 표시하며 샘플 데이터로 자동 전환하지 않습니다. 인증·푸시 알림·실시간 스트리밍·WebSocket·PWA 설치·Service Worker·기기 상태 API는 아직 구현하지 않았습니다.

## 검증

```powershell
node --test web/tests/api.test.mjs
```

브라우저에서는 다음을 확인합니다.

- 차키·에어팟·지갑 선택, 이름 검색·검색 결과 없음, 상세·마지막 목격 정보.
- 선택한 물건의 레이저 안내 접수, 서랍 1~6 선택·열기 요청.
- 부저 찾기·소리 끄기, 샘플 표시, 새로고침.
- 키보드 이동, 휴대폰 너비에서 화면 넘침 없음.
- `live` 연결 실패가 성공이나 샘플 동작으로 표시되지 않는지 확인.

실물·Jetson·웹 전체 연동 결과는 [통합 체크리스트](../docs/classroom_test_checklist.md)에 기록합니다.

### 확인한 결과 (2026-10-03)

- Node.js v24.19.0에서 API 테스트 **30/30 통과**, JavaScript 문법 검사 통과.
- 브라우저 샘플 모드: 검색·검색 결과 없음, 세 물건 상세, 레이저 안내 요청, 서랍 선택·열기, 부저 찾기·끄기, 새로고침 확인.
- PC 화면과 휴대폰 너비 360·390px에서 레이아웃 확인. 휴대폰 가로 넘침 없음. 서랍은 위 `1 2` / 가운데 `3 4` / 아래 `5 6` 배치.
- 키보드 Tab·Enter로 물건 선택, 선택 후 포커스 유지, 사진 없는 기록의 안내 비활성화 확인.
- `live` 모드를 로컬 정적 서버에 연결해 API 부재 시 목록 오류·제어 요청 실패가 표시되고 샘플 성공으로 바뀌지 않는 것을 확인. 검증 후 `config.js`는 `demo`로 복구.
- 실제 Jetson의 정상 응답, 사진 제공, 실제 레이저·서랍·BLE 부저 동작은 **미확인**. API JSON 임시안의 합의와 전체 연동 테스트가 남아 있음.

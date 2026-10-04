# 협업 규칙

Retrace 팀이 같은 저장소에서 작업할 때 지키는 규칙입니다.

---

## 1. 담당

| 담당 | 폴더 | 작업 브랜치 |
|---|---|---|
| STM32 ×2 · ESP32 ×2 펌웨어 | `stm32/`, `esp32/` | `feature/stm32`, `feature/esp32` |
| Jetson Nano 서버·인식·DB | `jetson_nano/` | `feature/jetson` |
| 웹 화면·HTTP API 연결 틀 | `web/` | `feature/web` |
| 공통 문서 | `docs/`, `README.md` | 관련 작업 브랜치에 같이 포함 |

---

## 2. 브랜치

| 브랜치 | 용도 |
|---|---|
| `main` | 통합 테스트를 마친 **최종본** — 보호 브랜치 (PR + 승인 필수) |
| `develop` | 각 파트의 작업을 모아 **통합 테스트**하는 브랜치 |
| `feature/stm32` | STM32 작업 |
| `feature/esp32` | ESP32 작업 |
| `feature/jetson` | Jetson 작업 |
| `feature/web` | 웹 화면 작업, 프로토콜 JSON 합의 후 Jetson 연결 |

```text
feature/stm32  ─┐
feature/esp32  ─┼──▶ develop ──(통합 테스트 · PR · 승인)──▶ main
feature/jetson ─┤
feature/web    ─┘
```

---

## 3. 작업 순서

**1. 자기 작업 브랜치로 이동**

```bash
git switch feature/본인파트
```

**2. 같은 원격 feature의 최신 내용 가져오기**

```bash
git status
git pull --ff-only origin feature/본인파트
```

`--ff-only`는 이력이 갈라졌을 때 자동 merge하지 않고 멈춥니다. 기존 수정이나 로컬 커밋을 먼저 확인합니다. `git pull origin develop`은 현재 feature에 develop 내용을 합칠 수 있으므로 일상적인 동기화 명령으로 사용하지 않습니다. develop 변경이 필요한 경우 따로 확인·합의하고 진행합니다.

**3. 작업 → 커밋**

```bash
git add <파일>
git commit -m "Add drawer LED timer"
```

**4. 자기 feature에 push하고 실물 확인**

```bash
git push origin feature/본인파트
```

완료 코드는 자기 feature에 보존합니다. 강의실에서 해당 feature로 보드를 업로드해 단독 테스트·보정하고, 문제가 있으면 같은 feature에서 수정·재검증·push합니다. 빌드 성공과 실물 성공을 구분합니다.

**5. 확인된 기능을 develop으로 PR**

GitHub에서 `feature/*` → `develop` PR 생성 → 변경 확인 → merge합니다. develop에서 Jetson·웹·여러 보드의 전체 연동을 확인합니다. 담당 feature는 통합 후에도 후속 수정에 사용할 수 있습니다.

2026-10-03 기준 네 보드 펌웨어는 빌드·PC 테스트까지 완료했으며, 강의실 실물 검증과 기능의 develop 통합은 아직 진행하지 않았습니다. 테스트 결과와 보정값은 [통합 체크리스트](docs/classroom_test_checklist.md)에 기록합니다.

웹 뼈대는 `feature/web`에 별도로 보존합니다. [웹 인계 문서](web/README.md)의 실행 방법을 사용하며, 기본 샘플 모드에서는 기기로 명령을 보내지 않습니다. 기존 HTTP 메서드·경로는 [프로토콜 7장](docs/protocol.md)을 따르고 JSON은 7-1의 임시안을 Jetson 담당자와 함께 확정합니다. 화면 테스트와 실제 Jetson·장치 연동 성공을 구분합니다.

---

## 4. `main` 반영

`develop`에서 통합 테스트 후 이상이 없으면, GitHub에서 **`develop` → `main` PR**을 만들어 반영합니다.
`main`은 보호 브랜치라 직접 push할 수 없고, 승인 후 merge합니다.

---

## 5. 커밋 메시지

- **영어, 동사로 시작**, 한 줄로 짧게
- 항상 `-m`을 붙여서 커밋 (`-m` 없이 `git commit`만 하면 편집기가 열림)
- 작성자 정보는 사용자 Git 계정만 사용하며 AI 공동 작성자 표시를 추가하지 않음

| 동사 | 쓰는 경우 | 예 |
|---|---|---|
| `Add` | 새 기능 · 파일 추가 | `Add HC-06 receive handler` |
| `Fix` | 버그 수정 | `Fix servo pulse range` |
| `Update` | 기존 내용 수정 · 개선 | `Update protocol to v0.3` |
| `Remove` | 삭제 | `Remove unused Arduino files` |
| `Refactor` | 동작은 같고 구조만 정리 | `Refactor drawer timer logic` |

---

## 6. 하지 않는 것

- `git push --force` 사용 금지 (상대 작업이 사라질 수 있음)
- `main`에 직접 작업 금지
- 빌드 결과물 커밋 금지 (`build/`, `.pio/` — `.gitignore`로 제외됨)
- `.ioc`를 다른 버전 CubeMX로 열었을 때 **Migrate 누르지 않기** (Continue 선택)

---

## 7. 문서 규칙

| 문서 | 내용 | 규칙 |
|---|---|---|
| [docs/protocol.md](docs/protocol.md) | 보드 간 메시지 형식 | 코드와 다르면 **문서 기준**. 바꿀 땐 **문서 먼저 수정**하고 공유 |
| [docs/pinmap.md](docs/pinmap.md) | 보드별 핀 배정 · 설정 | 핀을 바꾸면 같은 커밋에 문서도 수정 |
| [README.md](README.md) | 프로젝트 소개 · 구조 · 완료 상태 | 구조·검증 상태가 바뀌면 같이 수정 |
| [docs/classroom_test_checklist.md](docs/classroom_test_checklist.md) | 네 보드 단독·전체 연동 테스트 | 실제 실행한 항목만 체크하고 실측값 기록 |

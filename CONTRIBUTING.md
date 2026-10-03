# 협업 규칙

Retrace 팀이 같은 저장소에서 작업할 때 지키는 규칙입니다.

---

## 1. 담당

| 담당 | 폴더 | 작업 브랜치 |
|---|---|---|
| STM32 ×2 · ESP32 ×2 펌웨어 | `stm32/`, `esp32/` | `feature/stm32`, `feature/esp32` |
| Jetson Nano · Web | `jetson_nano/`, `web/` | `feature/jetson` |
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

```text
feature/stm32  ─┐
feature/esp32  ─┼──▶ develop ──(통합 테스트 · PR · 승인)──▶ main
feature/jetson ─┘
```

---

## 3. 작업 순서

**1. 자기 작업 브랜치로 이동**

```bash
git switch feature/본인파트
```

**2. 작업 시작 전 `develop` 최신 내용 가져오기**

```bash
git pull origin develop
```

**3. 작업 → 커밋**

```bash
git add <파일>
git commit -m "Add drawer LED timer"
```

**4. `develop`에 반영** (둘 중 하나)

- **직접 push**
  ```bash
  git pull origin develop
  git push origin HEAD:develop
  ```
  push 직전에 한 번 더 `pull`합니다. 그 사이 상대가 `develop`에 올렸으면 push가 거절(rejected)되기 때문입니다.

- **PR**
  ```bash
  git push origin feature/본인파트
  ```
  GitHub에서 `feature/*` → `develop` 방향으로 PR 생성 → merge

---

## 4. `main` 반영

`develop`에서 통합 테스트 후 이상이 없으면, GitHub에서 **`develop` → `main` PR**을 만들어 반영합니다.  
`main`은 보호 브랜치라 직접 push할 수 없고, 승인 후 merge합니다.

---

## 5. 커밋 메시지

- **영어, 동사로 시작**, 한 줄로 짧게
- 항상 `-m`을 붙여서 커밋 (`-m` 없이 `git commit`만 하면 Vim 편집기가 열림)

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
| [README.md](README.md) | 프로젝트 소개 · 구조 | 폴더 구조가 바뀌면 같이 수정 |

# 개발 환경 및 버전 관리

확인일: 2026-10-06

이 문서는 Retrace 프로젝트의 개발 환경과 버전을 기록한다.
아래 버전은 현재 개발 PC와 프로젝트 설정에서 확인한 값이며, 팀 공통 기준은 협의 후 확정한다.

## 1. VS Code 및 확장

| 항목 | 확장 ID | 확인한 버전 | 사용 목적 |
|---|---|---|---|
| VS Code | — (편집기 본체) | 1.140.0 | 코드 편집 및 개발 |
| Microsoft C/C++ | `ms-vscode.cpptools` | 1.35.3 — 시험판 | C/C++ IntelliSense 및 디버깅 |
| CMake Tools | `ms-vscode.cmake-tools` | 1.24.42 | CMake 프로젝트 구성 및 빌드 |
| STM32 확장 팩 | `stmicroelectronics.stm32-vscode-extension` | 3.11.0 | STM32 개발 |
| STM32Cube CMake Build | `stmicroelectronics.stm32cube-ide-build-cmake` | 1.47.0 | STM32 CMake 빌드 지원 |
| STM32Cube clangd | `stmicroelectronics.stm32cube-ide-clangd` | 1.1.0 | STM32 코드 분석 |
| PlatformIO IDE | `platformio.platformio-ide` | 3.3.4 | ESP32 개발 |
| Python 확장 | `ms-python.python` | 2026.6.0 | Python 개발 및 실행 환경 선택 |
| Pylance | `ms-python.vscode-pylance` | 2026.4.1 | Python 코드 분석 |

확장 버전은 편집기 문제를 재현하고 비교할 때 참고한다.
같은 보드를 개발하는 팀원은 필요한 확장과 프로젝트 설정 기준을 공유한다.
테마, 언어팩, AI 확장 등 개인 편의 기능은 각자 관리한다.

### 1-1. 함께 설치되는 하위 확장

확장 팩과 하위 확장은 각각 별도의 버전을 가진다. 팩 버전만 같아도 하위 확장 버전은 다를 수 있으므로, 담당 플랫폼에 사용하는 항목을 함께 확인한다.
아래는 확인일 기준으로 현재 PC에 설치된 목록이며, 모든 팀원이 모든 항목을 설치해야 한다는 의미는 아니다.
앞 표에 기재한 CMake Build, clangd, CMake Tools, C/C++, Pylance도 해당 묶음의 관리 대상이다.

| 묶음 | 하위 확장 | 확장 ID | 확인한 버전 |
|---|---|---|---|
| STM32 | Core | `stmicroelectronics.stm32cube-ide-core` | 1.5.0 |
| STM32 | Bundles Manager | `stmicroelectronics.stm32cube-ide-bundles-manager` | 1.5.0 |
| STM32 | Build Analyzer | `stmicroelectronics.stm32cube-ide-build-analyzer` | 1.5.0 |
| STM32 | Project Manager | `stmicroelectronics.stm32cube-ide-project-manager` | 1.5.0 |
| STM32 | Debug Core | `stmicroelectronics.stm32cube-ide-debug-core` | 1.5.0 |
| STM32 | Generic GDB Server | `stmicroelectronics.stm32cube-ide-debug-generic-gdbserver` | 1.5.0 |
| STM32 | ST-LINK GDB Server | `stmicroelectronics.stm32cube-ide-debug-stlink-gdbserver` | 1.5.0 |
| STM32 | J-Link GDB Server | `stmicroelectronics.stm32cube-ide-debug-jlink-gdbserver` | 1.5.0 |
| STM32 | RTOS | `stmicroelectronics.stm32cube-ide-rtos` | 1.5.0 |
| STM32 | Registers | `stmicroelectronics.stm32cube-ide-registers` | 1.5.0 |
| STM32 | Memory Inspector | `eclipse-cdt.memory-inspector` | 1.3.0 |
| STM32 | Serial Monitor | `eclipse-cdt.serial-monitor` | 2.0.0 |
| CMake Tools | C++ DevTools | `ms-vscode.cpp-devtools` | 0.6.18 |
| Python | Python Debugger | `ms-python.debugpy` | 2026.6.0 |
| Python | Python Environments | `ms-python.vscode-python-envs` | 1.38.0 |

PlatformIO는 `ms-vscode.cpptools`에 의존한다. 현재 C/C++는 시험판이므로, 같은 버전을 기준으로 삼을 경우 시험판 여부도 함께 확인한다. 안정판으로 변경할 때는 별도로 동작을 검증한다.

### 1-2. 설치 버전 확인 및 맞추기

팀 공통 버전과 적용 대상을 먼저 정한 뒤, 각 PC에서 아래 절차를 수행한다.

1. VS Code 터미널에서 설치된 확장 ID와 버전을 조회한다.

   ```powershell
   code --list-extensions --show-versions
   ```

2. 담당 플랫폼에 필요한 확장을 앞의 표와 비교한다. 개인 편의 확장까지 일치시킬 필요는 없다.
3. 버전이 다르면 확장 화면에서 해당 확장을 오른쪽 클릭하고 **다른 버전 설치 / Install Another Version**로 합의한 버전을 선택한다. 명령으로 설치할 경우 다음과 같이 `확장ID@버전`을 지정할 수 있다.

   ```powershell
   code --install-extension platformio.platformio-ide@3.3.4
   ```

4. 설치가 끝나면 버전 조회 명령을 다시 실행하고, 관리 대상 확장과 하위 확장의 버전을 확인한다.

`code` 명령을 찾을 수 없는 PC에서는 VS Code 확장 화면에서 설치 버전을 확인하고 변경한다.
이 문서의 명령은 실행 안내이며, 문서를 공유하는 것만으로 설치나 설정 변경이 수행되지는 않는다.

### 1-3. 프로젝트 확장의 자동 업데이트 관리

같은 버전을 유지하기 위해 각 PC에서 관리 대상 확장의 자동 업데이트 상태를 확인한다.

1. `Ctrl + Shift + X`로 확장 화면을 연다.
2. 버전을 유지할 프로젝트 확장을 오른쪽 클릭한다.
3. **자동 업데이트 / Auto Update** 항목에 체크가 있으면 끈다. 이미 꺼져 있으면 그대로 유지한다.
4. 해당 확장 팩의 관리 대상 하위 확장에도 같은 절차를 적용한다.
5. 팀원도 자신의 PC에서 동일하게 확인한다.

전체 확장의 자동 업데이트를 일괄로 끌 필요는 없다. 테마, 언어팩, AI 확장은 개인 기준으로 관리한다.
이 설정은 각 PC에서 적용하는 확장 관리 설정이며, 저장소를 내려받는 것만으로 팀원 PC에 적용되지 않는다.
업데이트 알림이 표시되는 것과 실제 자동 업데이트가 활성화되어 있는 것은 구분한다. 수동 업데이트는 가능하므로, 버전을 유지하는 동안 일괄 업데이트 전에 대상을 확인한다.

확장 자동 업데이트 설정은 VS Code 본체, 컴파일러, PlatformIO 플랫폼·프레임워크, Python 패키지의 버전을 고정하지 않는다. 이 항목들은 각 설치 환경과 프로젝트 설정에서 따로 관리한다.

### 1-4. 버전 변경 절차

1. 변경할 확장 또는 빌드 도구와 목표 버전을 팀원과 정한다.
2. 해당 환경에 설치하고 담당 프로젝트의 빌드 또는 실행을 확인한다.
3. 검증한 버전과 확인일을 이 문서에 갱신한다.
4. 팀원도 필요한 항목을 같은 버전으로 맞추고, 자동 업데이트 상태를 다시 확인한다.

참고: [VS Code 확장 설치·자동 업데이트 관리](https://code.visualstudio.com/docs/configure/extensions/extension-marketplace), [VS Code 확장 버전 설치·조회 명령](https://code.visualstudio.com/docs/configure/command-line).

## 2. STM32 및 ESP32 빌드 환경

| 항목 | 확인한 버전 | 확인 기준 |
|---|---|---|
| STM32Cube F4 펌웨어 패키지 | 1.28.3 | 두 STM32 프로젝트의 .ioc 설정 |
| STM32 GCC | 14.3.1+st.2 | 프로젝트 도구 설정 및 컴파일 DB |
| CMake | 4.3.1+st.1 | STM32 프로젝트 도구 설정 |
| Ninja | 1.13.2+st.1 | STM32 프로젝트 도구 설정 |
| PlatformIO Core | 6.2.0 | 개발 PC 설치 환경 |
| ESP32 플랫폼 espressif32 | 7.1.3 | 개발 PC 설치 환경 |
| ESP32 Arduino 프레임워크 | 2.0.17 | 설치된 프레임워크 |

빌드 도구와 프레임워크는 같은 소스의 빌드 결과를 재현하기 위한 주요 관리 대상이다.

현재 feature/jetson의 ESP32 설정은 `platform = espressif32`로 되어 있어 플랫폼 버전이 고정되어 있지 않다.
팀 기준을 7.1.3으로 확정할 경우, 해당 ESP32 작업 브랜치에서 버전 지정과 재빌드 검증을 진행한다.

## 3. Python 환경

### 3-1. Windows 개인 개발 환경

현재 저장소 루트의 `.venv`에서 확인한 버전이다.

| 항목 | 설치 버전 |
|---|---|
| Python | 3.13.15 |
| Ultralytics | 8.4.173 |
| PyTorch | 2.14.1+cpu |
| Torchvision | 0.29.1+cpu |
| OpenCV | 5.0.0.93 |
| NumPy | 2.5.3 |

이 환경은 Windows 개인 개발·테스트용이며, Jetson 실행 환경과 구분하여 관리한다.

### 3-2. Jetson 카메라 실행 환경

Jetson 카메라 실행에 필요한 Python 패키지는 `jetson_nano/requirements.txt`로 관리하고 팀원과 공유한다.

현재 선언된 패키지 버전:

- Ultralytics: 8.3.0
- OpenCV: 5.0.0.93

이 파일은 Jetson 환경을 기준으로 작성했으며, Windows 개인 개발 환경과 구분한다.
Windows 설치 버전과 다르다는 이유로 변경하지 않는다.

Python·PyTorch 등 나머지 구성은 실제 Jetson 환경에 맞춰 관리한다.
패키지 버전을 변경할 때는 실제 Jetson에서 설치 및 실행을 검증한 뒤 반영한다.

카메라 확인용 코드인 `jetson_nano/vision/webcam_test.py`도 팀원과 공유한다.
실행 환경, 필요한 패키지, 사용 방법을 확인한 뒤 테스트에 사용한다.

## 4. 공유 및 변경 원칙

- 이 문서는 Git으로 공유하여 팀원이 최신 환경 기준을 확인할 수 있도록 한다.
- 버전을 변경할 때는 변경 대상과 이유를 기록하고, 해당 프로젝트의 빌드 또는 실행을 검증한다.
- VS Code 확장 추천 파일은 설치 목록을 안내하는 용도로 사용하며, 정확한 버전을 고정하지는 않는다.
- 개인 PC 절대경로가 포함된 설정, `.venv`, 컴파일 DB, 빌드 산출물은 공유하지 않는다.
- 다른 PC에서는 필요한 도구를 설치하고, 해당 PC와 현재 브랜치에 맞게 컴파일 DB와 빌드 정보를 생성한다.
- 같은 환경을 유지하더라도 Git 병합 충돌은 별도로 해결한다.

# 프로젝트 오로라 (가제)

판타지 마법과 1950년대 하드 SF가 섞인 세계에서 왕국을 세우는 3D 복셀 게임. C++20 자체 엔진.

## 이 폴더에 들어 있는 것

| 경로 | 내용 |
| --- | --- |
| `CLAUDE.md` | Claude Code가 매번 자동으로 읽는 작업 규칙 |
| `docs/` | 기획서 전체(탭별 마크다운), `ROADMAP.md`, `PROMPTS.md`, `DECISIONS.md`, `reference/`(텍스처 시트·동작 영상) |
| `game/assets/aurora/textures/` | 32px 블록·아이템 텍스처, 노멀·발광·재질 맵, 플레이어 스킨, 금 가는 단계 |
| `tools/texgen/` | 텍스처를 다시 만드는 Python 스크립트 (`python export.py`) |
| `tools/motion_viewer/` | 동작 참고용 three.js 뷰어 (`python build.py` 후 `viewer_full.html`을 브라우저로 열기, 인터넷 필요) |

## 처음 준비 (Windows)

1. **Visual Studio 2022** — 설치 시 "C++를 사용한 데스크톱 개발" 워크로드 선택
2. **Git**, **CMake** (VS에 포함된 것도 됨)
3. **vcpkg**
   ```
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\dev\vcpkg
   ```
   (`setx` 후에는 터미널을 새로 연다)
4. 선택: **Python 3.11+** (텍스처 재생성), **Node.js** (동작 뷰어)
5. GitHub 저장소를 PC에 클론하고, 그 폴더에 이 키트를 풀어 넣는다

## Visual Studio 2022에서 열고 실행하기

이 프로젝트는 `.sln` 파일이 없는 **CMake 프로젝트**다. VS 2022는 `CMakeLists.txt`와 `CMakePresets.json`이 있는 폴더를 그대로 열 수 있다.

### 처음 한 번

1. **VS를 완전히 새로 연다.** 위의 `setx VCPKG_ROOT`를 한 뒤 VS가 이미 켜져 있었다면 닫았다가 다시 연다. 환경 변수는 새로 시작한 프로그램에만 반영된다.
2. **폴더 열기.** 시작 창에서 **로컬 폴더 열기**를 누르고, 저장소 폴더(`CMakeLists.txt`가 바로 들어 있는 폴더)를 고른다.
   - 시작 창이 안 보이면 메뉴 **파일 → 열기 → 폴더...**를 쓴다.
   - "솔루션 탐색기"에 폴더 트리가 보이면 된다.
3. **프리셋 고르기.** 위쪽 도구 모음 가운데에 드롭다운이 두 개 있다.
   - 왼쪽 드롭다운은 **로컬 컴퓨터**로 둔다.
   - 오른쪽 드롭다운(구성 프리셋)에서 **Debug**를 고른다. 목록에는 `Debug`, `Release`, `Visual Studio 2022 solution`이 있다. 평소에는 `Debug`를 쓴다.
   - 드롭다운이 안 보이면: **도구 → 옵션 → CMake → 일반**에서 "CMakePresets.json을 항상 사용"을 켜고 VS를 다시 연다.
4. **첫 구성 기다리기.** 프리셋을 고르면 VS가 CMake 구성을 시작한다. **처음에는 vcpkg가 라이브러리(GLFW·glad·ImGui·Catch2)를 받아 빌드하느라 5~15분** 걸린다.
   - 진행 상황은 **보기 → 출력**에서 "출력 보기 선택"을 **CMake**로 바꾸면 보인다.
   - "CMake 생성이 완료되었습니다(CMake generation finished)"가 나오면 끝이다. 다음부터는 몇 초면 끝난다.

### 빌드하고 실행하기

5. **빌드.** 메뉴 **빌드 → 모두 빌드** (`Ctrl+Shift+B`).
6. **시작 항목 고르기.** 도구 모음의 초록색 ▶ 버튼 옆 드롭다운("시작 항목 선택")에서 **aurora.exe**를 고른다.
7. **실행.** `F5`(디버거 연결) 또는 `Ctrl+F5`(디버거 없이 실행).
8. **보여야 하는 것**
   - 어두운 남색 배경의 "Aurora" 창(1280×720)이 뜬다.
   - 왼쪽 위 ImGui **Debug** 창에 다음이 보인다.
     - FPS와 프레임 시간(모니터가 60Hz면 약 60 FPS / 16.7 ms)
     - 프레임버퍼 크기
     - OpenGL 버전(`4.5` 이상, Core Profile)
     - GPU 이름
     - VSync 체크박스(끄면 FPS가 크게 오른다)
   - 창 가장자리를 끌어 크기를 바꿔도 배경이 늘어나거나 깨지지 않고, Debug 창의 프레임버퍼 크기가 바뀐다.
   - **Esc**나 창의 X 버튼으로 닫힌다. 출력 창에 `[app] N frames, avg … ms/frame`이 찍힌다.

### 테스트 실행

- 메뉴 **테스트 → 테스트 탐색기**를 열고 **모두 실행**을 누른다. `aurora_tests`의 테스트가 모두 초록색이면 된다.
- 또는 **보기 → 터미널**(개발자 PowerShell)에서 `ctest --preset debug`를 실행한다.

### 문제가 생기면

- `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake` → `VCPKG_ROOT`가 비어 있다. `setx VCPKG_ROOT C:\dev\vcpkg`를 하고 VS를 다시 연다.
- 구성이 꼬였다 → 메뉴 **프로젝트 → CMake 캐시 삭제**를 누른 뒤, 다시 **프로젝트 → CMake 캐시 구성**을 누른다. 또는 저장소의 `build` 폴더를 지운다.
- `.sln`으로 작업하고 싶다 → 개발자 PowerShell에서 `cmake --preset vs2022`를 실행하고 `build\vs2022\aurora.sln`을 연다. 평소에는 필요 없다.

## Claude Code로 시작하기 (웹, GitHub 저장소 연결)

1. 이 폴더의 내용을 GitHub 저장소 최상위에 올린다 (`CLAUDE.md`가 저장소 바로 아래에 오게).
2. Claude Code에서 그 저장소·`main` 브랜치로 새 세션을 열고 `docs/PROMPTS.md`의 "맨 처음 한 번" 프롬프트를 보낸다.
3. 계획을 승인하면 Claude Code가 브랜치에 구현하고 푸시한다. GitHub에서 확인 후 main에 합친다.
4. 내 PC에서 `git pull` 받아 Visual Studio로 열어 실행해 본다(창·그래픽 확인은 PC에서만 된다).
5. 다음 작업은 새 세션에서 같은 방식으로 반복한다. 진행 상황은 `docs/ROADMAP.md`에 체크된다.

## 원본 기획서

claude.ai의 "프로젝트 오로라(가제) — 게임 기획서" 문서가 원본이다. 원본을 크게 고쳤으면 해당 탭을 다시 내보내 `docs/`를 갱신한다.

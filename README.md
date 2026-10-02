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
5. GitHub 저장소를 **영문·공백 없는 경로**에 클론한다(한글·공백이 든 경로에서 CMake가 비정상 종료한 적이 있다. 아래 "문제가 생기면" 참고)
   ```
   git clone https://github.com/Gamma0117/gamma.git C:\dev\aurora
   ```

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
4. **첫 구성 기다리기.** 프리셋을 고르면 VS가 CMake 구성을 시작한다. **처음에는 vcpkg가 라이브러리(GLFW·glad·ImGui·Tracy·Catch2)를 받아 빌드하느라 5~15분** 걸린다.
   - 진행 상황은 **보기 → 출력**에서 "출력 보기 선택"을 **CMake**로 바꾸면 보인다.
   - "CMake 생성이 완료되었습니다(CMake generation finished)"가 나오면 끝이다. 다음부터는 몇 초면 끝난다.
   - `vcpkg.json`이나 `cmake/triplets`가 바뀐 커밋을 받으면 이 과정을 한 번 더 거친다(P0-2에서 Tracy를 추가하며 triplet이 바뀌어 라이브러리 전체를 다시 빌드한다).

### 빌드하고 실행하기

5. **빌드.** 메뉴 **빌드 → 모두 빌드** (`Ctrl+Shift+B`).
6. **시작 항목 고르기.** 도구 모음의 초록색 ▶ 버튼 옆 드롭다운("시작 항목 선택")에서 **aurora.exe**를 고른다.
7. **실행.** `F5`(디버거 연결) 또는 `Ctrl+F5`(디버거 없이 실행).
8. **보여야 하는 것**
   - "Aurora" 창(1280×720)이 뜨고, 1초쯤 안에 어두운 남색 하늘 아래 풀이 덮인 평지가 보인다. 처음 잠깐은 y 80 높이에서 내려다보다가, 플레이어가 생기면(콘솔 `Player spawned at (0.50, 64.00, 0.50)`) 플레이어의 눈 높이(y 65.62)로 내려와 북쪽을 약간 내려다본다. 그려지는 범위는 반경 8청크(17×17)이고, 그 너머는 하늘색이다.
   - **조작** (P0-6: 플레이어가 걷는다. 서버가 위치를 정하고, 화면은 그것을 미리 계산해 보여준다)
     - 창 안의 지형을 **클릭**하면 마우스가 잡힌다(커서가 사라짐). F3 화면 위를 클릭하면 잡히지 않는다.
     - 마우스로 둘러보고, **W A S D**로 걷는다(초당 4.3블록). **Ctrl**을 누른 채 앞으로 가면 달린다(5.6). **Shift**는 웅크리기(1.3, 눈 높이가 조금 낮아짐). **Space**는 점프(약 1.26블록, 누르고 있으면 착지할 때마다 다시 뛴다).
     - 한 칸 높이의 블록은 점프해서 오른다. 두 칸 벽은 넘지 못한다. 0.6블록 이하의 턱(반블록·계단)은 걸어서 저절로 오르지만, 그런 블록은 아직 없어 화면에서는 볼 수 없다(테스트로 확인했고, 부분 블록 작업에서 화면으로 확인한다).
     - **Esc**를 누르면 마우스가 풀리고 플레이어는 멈춘다(공중이었다면 떨어져 착지). 다른 창으로 전환하거나 최소화해도 같다. 아직 메뉴·일시정지가 아니라서 서버는 계속 돈다.
     - **자유 비행(디버그)**: Esc로 마우스를 놓고 F3 화면의 `Free-flying camera (debug)`를 체크한 뒤, 지형을 클릭해 다시 잡는다. P0-5처럼 날아다닌다(Space·Shift 위아래, Ctrl 4배). 플레이어는 그 자리에 서 있고, 체크를 풀면 카메라가 플레이어 눈으로 돌아온다. 아주 멀리 날아가면 플레이어가 있는 청크가 내려가 F3에 `FROZEN`이 뜨고, 체크를 풀면 다시 불러와 풀린다.
     - **시험 코스**: 시작 위치에서 뒤로 돌아(남쪽) 몇 걸음 가면 있다. 조약돌 계단 3단(x 0~2, 점프로 한 칸씩), 판자 두 칸 벽(x 5~7, 넘지 못함), 높이 두 칸 통로(x −5, 그냥 지나감), 두 칸 깊이 구덩이(x 9~11, 안쪽 디딤돌을 밟고 점프해 나옴).
     - 창을 닫으려면 창의 X 버튼을 누른다.
   - **F3**을 누르면 왼쪽 위에 반투명 디버그 화면이 뜬다.
     - FPS와 프레임 시간(평균, 최근 240프레임의 최소·최대), CPU 시간, 프레임 시간 그래프. 모니터가 60Hz면 약 60 FPS / 16.7 ms, 120Hz면 약 120 FPS / 8.3 ms
     - `Server 20.0 TPS`와 틱 시간·틱 번호·건너뛴 틱(`skipped 0`). 서버는 렌더와 별도 스레드에서 초당 20틱으로 돈다
     - `Chunks 361 loaded, 0 pending, 0 failed`: 서버가 카메라 둘레 반경 9(19×19)에 만든 평지 청크 수. 그리는 범위(반경 8)보다 한 칸 넓게 불러와서, 그려지는 청크는 모두 이웃이 있다. 움직이는 동안에는 한 칸 더 유지하기 때문에 361보다 많을 수 있다
     - `XYZ`, `chunk`, `section`: 카메라 위치와 그 청크·섹션. `Facing north (yaw …, pitch …)`: 바라보는 방향(yaw 0 = 북쪽, 90 = 동쪽)
     - `Render distance 8: N chunks held, 289 drawable`: 클라이언트가 받은 청크 수와, 그중 그릴 수 있는 청크 수(반경 8 안이고 8방향 이웃이 모두 있음)
     - `Meshes 289 done, … empty, 0 waiting, 0 in flight, 0 failed`: 섹션 메시 상태. 평지는 청크마다 지표 섹션 하나에 면이 있고, 땅속 섹션은 면이 없어서 `empty`다
     - `GPU 289 sections, 1156 vertices, … MB, 0 to upload`와 `drawn N sections in N calls`: GPU에 올라간 메시와, 카메라 시야(절두체) 안이라 실제로 그린 섹션 수
     - 마지막 줄: 마우스가 잡혔는지(`Mouse captured (Esc releases)`) 아닌지
     - `Free-flying camera (debug)` 체크박스와 `Player …`: 플레이어 발 위치, 수평 속도(걷기 4.30, 달리기 5.60)·수직 속도, 땅에 있는지, `sneaking`/`sprinting`(Shift·Ctrl+W 중일 때). `inputs sent / settled / unsettled`: 보낸 입력 수, 서버가 처리를 끝낸 번호, 아직 처리되지 않은 입력 수(보통 1~3). `corrections`(서버 결과로 위치를 고친 횟수, 보통 0이고 포커스를 잃는 순간 1 정도 늘 수 있음), `pauses`·`resyncs`(서버가 멈췄을 때만 늘어남). `server: waiting … starved … filling … dropped … late … before spawn …`: 서버 쪽 입력 대기열 상태(평소 `starved 1, filling 1, dropped 0, late 0, before spawn 0`. `late`는 스폰 뒤 늦게 오거나 중복된 입력만 센다)
     - `Workers N`: 워커 스레드 수(논리 프로세서 수 − 2, 최소 1)
     - `Blocks 8 (10 states)`: 읽어 들인 블록 수(내장 공기·unknown 포함)와 블록 상태 수
     - 창 크기, VSync 체크박스(끄면 FPS가 크게 오르지만 서버는 20 TPS 그대로), OpenGL 버전(`4.5` 이상, Core Profile), GPU 이름, Tracy 상태
   - F3을 다시 누르면 사라진다. 누르고 있어도 한 번만 바뀐다.
   - 창 가장자리를 끌어 크기를 바꿔도 배경이 늘어나거나 깨지지 않고, 디버그 화면의 창 크기가 바뀐다.
   - 콘솔에 `Loaded 8 block textures (32x32 PNG)`, `Flat world preset: 3 layers, 128 blocks high (top layer ends at y 63), 9 boxes`, `Player movement: 0.6 x 1.8 blocks, walk 4.3 / sprint 5.6 / sneak 1.3 blocks per second`, `Player spawned at (0.50, 64.00, 0.50)`와 `Spawn area ready after … ms: 361 chunks loaded, 0 failed`가 찍힌다.
   - 창의 X 버튼으로 닫으면 콘솔에 `[INFO ] [app] (Main) N frames, avg … ms/frame`과 `Server thread stopped: … last measured 20.0x TPS` 같은 로그가 찍히고, 마지막 줄은 `Exiting with code 0`이다.

### 게임 데이터 (블록 JSON, 텍스처, 평지 프리셋, 쉐이더)

- 블록 정의는 `game/data/aurora/blocks/*.json`에 있다. 형식은 `docs/15_data_files.md`의 "블록 파일".
- 블록 텍스처는 `game/assets/aurora/textures/block/*.png`이고 32×32 PNG여야 한다. 창을 띄우기 전에 모두 읽어 검사한다.
- 청크 쉐이더는 `game/assets/aurora/shaders/chunk.vert`, `chunk.frag`이다(시작할 때 읽는다. 실행 중 다시 읽기는 P0-10).
- 평지 월드의 층 구성은 `game/data/aurora/worldgen/flat.json`에 있다(돌 124층, 흙 3층, 풀 1층, 그 위에 시험 코스 상자들). 형식은 같은 문서의 "평지 프리셋".
- 플레이어의 크기·속도·점프·중력은 `game/data/aurora/player/movement.json`에 있다. 형식은 같은 문서의 "플레이어 이동".
- 게임은 시작할 때 `./game`(실행 폴더), 없으면 저장소의 `game` 폴더를 읽는다. 다른 폴더를 쓰려면 `--game-dir <폴더>`를 준다(VS에서는 `launch.vs.json`의 `args`). 이때는 그 폴더만 쓴다.
- 파일이 틀리면 로그에 **파일 경로, 필드, 이유**가 줄마다 나오고, 창을 띄우지 않고 종료 코드 1로 끝난다. 예:
  ```
  [ERROR] [data] (Main) C:/dev/aurora/game/data/aurora/blocks/stone.json /hardness: expected a number >= 0, got string "abc"
  [ERROR] [data] (Main) Block data has 1 error(s) and 0 warning(s) (6 files read); fix the lines above
  ```
- 모르는 필드(오타 등)는 경고만 하고 시작한다.

### 로그

- 로그는 콘솔과 **실행 폴더의 `logs/latest.log`**에 함께 쓴다. 바로 전 실행분은 `logs/previous.log`로 남는다.
- 첫 줄 `Log file: …`에 절대 경로가 나온다. VS에서 F5로 실행하면 보통 `build\debug\app\logs\latest.log`다.
- 한 줄 형식: `[시:분:초.밀리초] [레벨] [모듈] (스레드) 내용`. 예: `[12:34:56.789] [INFO ] [server] (Server) Server thread started (20 ticks per second)`
- 레벨은 TRACE·DEBUG·INFO·WARN·ERROR. Debug 빌드는 DEBUG부터, Release 빌드는 INFO부터 기록한다.

### 프로파일러 (Tracy, 선택)

게임에는 Tracy 클라이언트가 들어 있다. 프로파일러 창이 붙어 있을 때만 데이터를 모으므로(on-demand) 평소 비용은 거의 없다. 게임을 켤 때 Windows 방화벽 허용 창이 뜨지 않도록 이 PC(localhost)에서 오는 접속만 받는다.

1. https://github.com/wolfpld/tracy/releases 에서 **v0.14.1**의 Windows용 압축 파일을 받아 풀고 `tracy-profiler.exe`를 실행한다. 게임에 들어 있는 클라이언트와 같은 버전을 쓴다(버전이 다르면 접속이 거부될 수 있다).
2. 게임을 실행한 상태에서 프로파일러의 주소 칸에 `127.0.0.1`을 넣고 **Connect**를 누른다. 네트워크 알림(브로드캐스트)을 끄므로 목록에 자동으로 뜨지 않는다.
3. 보이는 것
   - 메인 프레임, 그리고 50 ms 간격의 `Server` 프레임 세트
   - 스레드 `Main`, `Server`, `Worker 0…`
   - 존 `Input`·`Render`·`ImGui render`·`Debug overlay`·`Swap buffers`(메인), `Server tick`(서버)
   - 접속한 뒤에 찍힌 로그 줄(메시지 목록)
   - 디버그 화면(F3)의 Tracy 줄이 `connected`로 바뀐다

### 테스트 실행

- 메뉴 **테스트 → 테스트 탐색기**를 열고 **모두 실행**을 누른다. `aurora_tests`의 테스트가 모두 초록색이면 된다.
  - 처음 열었을 때 테스트가 0개로 보이면 메뉴 **테스트 → aurora에 대해 CTests 실행**을 한 번 누르거나 검색이 끝나기를 기다린다. 그러면 목록에 나타난다.
  - Linux 전용 `app_smoke`, `app_render_screenshot`, `app_render_screenshot_without_world`(Xvfb 창 테스트)와 링크·권한 테스트는 Windows 목록에 없다.
- 또는 **보기 → 터미널**(개발자 PowerShell)에서 `ctest --preset debug`를 실행한다.
- 테스트 하나가 약 0.3초씩 걸린다(Linux 기준). 테스트 프로세스가 끝날 때 Tracy 수신 스레드를 정리하는 시간이다.

### 문제가 생기면

- `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake` → `VCPKG_ROOT`가 비어 있다. `setx VCPKG_ROOT C:\dev\vcpkg`를 하고 VS를 다시 연다.
- 창이 뜨지 않고 로그에 `[ERROR] [data]` 줄과 `Block data has N error(s)`가 있다 → 블록 JSON이 틀렸다. 각 줄의 파일과 필드를 고친다. `--game-dir … is not an existing folder`면 지정한 폴더가 없다.
- 창이 뜨지 않고 로그에 `The flat world preset has N error(s)`가 있다 → `worldgen/flat.json`이 틀렸다. 바로 위 ERROR 줄의 필드(`/layers/0/height` 등)를 고친다.
- 창이 뜨지 않고 로그에 `Block textures have N error(s)`가 있다 → 블록 텍스처 PNG가 깨졌거나 32×32가 아니다. 바로 위 ERROR 줄에 파일과 이유가 있다.
- 창은 떴다가 바로 닫히고 로그에 `Cannot set up chunk rendering: …`이 있다 → 쉐이더 파일을 읽지 못했거나 컴파일 오류가 났다(파일과 드라이버 로그가 함께 나온다).
- 지형이 자홍·검정 체크무늬로 보인다 → 그 면의 텍스처가 텍스처 배열에 없다(unknown 블록 등). 로그의 텍스처 줄을 확인한다.
- F3에 `failed`가 0이 아니거나 `Server stopped: …`가 보인다 → 청크 생성이나 서버 틱에서 오류가 났다. 로그의 `[ERROR] [world]` 또는 `Server thread stopped by an error` 줄에 좌표와 이유가 있다.
- 창이 뜨지 않고 로그 끝에 `Fatal start-up error: …`와 `Exiting with code 1`이 있다 → 시작 단계의 치명적 오류(예: 스레드를 만들 수 없음)다. 바로 위 ERROR 줄이 원인이다.
- CMake 출력에 `Tracy client was built without TRACY_ENABLE` 경고가 나오거나 F3 화면에 `Tracy off`가 보인다 → Tracy가 프로파일링이 꺼진 채로 빌드됐다. **프로젝트 → CMake 캐시 삭제** 후 다시 구성해 vcpkg가 `cmake/triplets` 설정으로 Tracy를 다시 빌드하게 한다.
- 구성이 꼬였다 → 메뉴 **프로젝트 → CMake 캐시 삭제**를 누른 뒤, 다시 **프로젝트 → CMake 캐시 구성**을 누른다. 또는 저장소의 `build` 폴더를 지운다.
- 구성 중 CMake가 컴파일러 확인 단계에서 `0xC0000409`로 비정상 종료한다 → 저장소를 영문·공백 없는 경로(예: `C:\dev\aurora`)에 받아서 연다. 2026-09-30에 한글·공백이 든 경로에서 VS 2022 17.12의 내장 CMake 3.29.5-msvc4가 이렇게 종료했고, 같은 커밋을 `C:\dev\aurora`에 받자 같은 CMake로 구성·빌드·F5·테스트가 모두 성공했다. 다만 최소 프로젝트로는 한글 경로에서 재현되지 않아 근본 원인은 확정하지 못했다. 영문 경로에서도 안 되면 VS 2022를 최신으로 업데이트하거나, 개발자 PowerShell에서 새 CMake(vcpkg가 받아 둔 `%VCPKG_ROOT%\downloads\tools\cmake-*\...\bin\cmake.exe` 등)로 명령줄 빌드를 한다.
- F5로 실행하면 출력 창에 `wil::ResultException` first-chance 예외 알림이 반복된다 → 이미 처리된 예외라 앱 동작에는 영향이 없다. 우리 코드와 GLFW·ImGui는 WIL을 쓰지 않으므로 Windows 구성 요소나 드라이버 내부에서 나는 알림으로 보인다(원인은 조사하지 않음). 창이 정상으로 뜨고 종료 코드가 0이면 무시한다.
- `.sln`으로 작업하고 싶다 → 개발자 PowerShell에서 `cmake --preset vs2022`를 실행하고 `build\vs2022\aurora.sln`을 연다. 평소에는 필요 없다.

## Claude Code로 시작하기 (웹, GitHub 저장소 연결)

1. 이 폴더의 내용을 GitHub 저장소 최상위에 올린다 (`CLAUDE.md`가 저장소 바로 아래에 오게).
2. Claude Code에서 그 저장소·`main` 브랜치로 새 세션을 열고 `docs/PROMPTS.md`의 "맨 처음 한 번" 프롬프트를 보낸다.
3. 계획을 승인하면 Claude Code가 브랜치에 구현하고 푸시한다. GitHub에서 확인 후 main에 합친다.
4. 내 PC에서 `git pull` 받아 Visual Studio로 열어 실행해 본다(창·그래픽 확인은 PC에서만 된다).
5. 다음 작업은 새 세션에서 같은 방식으로 반복한다. 진행 상황은 `docs/ROADMAP.md`에 체크된다.

## 원본 기획서

claude.ai의 "프로젝트 오로라(가제) — 게임 기획서" 문서가 원본이다. 원본을 크게 고쳤으면 해당 탭을 다시 내보내 `docs/`를 갱신한다.

# 프로젝트 오로라 — Claude Code 작업 규칙

마인크래프트식 3D 복셀 게임. 판타지 마법과 1950년대 수준 하드 SF 기술을 섞은 세계에서 왕국을 세우고 키우는 샌드박스다. C++20으로 엔진부터 직접 만든다.
사용자는 한국어로 소통한다. 설명·보고·커밋 요약은 한국어로, 코드·식별자·주석은 영어로 쓴다.

## 먼저 읽을 것

| 언제 | 문서 |
| --- | --- |
| 작업을 시작할 때마다 | `docs/ROADMAP.md` — 지금 단계와 체크리스트 |
| P0 엔진 작업 | `docs/22_p0_tech.md` — 모듈·스레드·데이터 구조·패킷·성능 예산·작업 순서 |
| 데이터·에셋 경로, JSON 형식 | `docs/15_data_files.md` |
| 텍스처·모델·애니메이션 규칙 | `docs/14_art_guide.md`, `docs/21_animation_list.md` |
| 블록 정의가 필요할 때 | `docs/20_block_catalog.md`, `docs/01_blocks_items_recipes.md` |
| 전체 그림 | `docs/00_plan.md` (필요한 장만 읽는다. 전체를 한 번에 읽지 않는다) |
| 결정 기록 | `docs/DECISIONS.md` |

문서와 다르게 구현해야 할 이유가 생기면, 코드를 바꾸기 전에 사용자에게 알리고, 합의되면 `docs/DECISIONS.md`에 날짜와 이유를 적는다.

## 기술 스택 (고정)

- C++20, CMake 3.25 이상 + vcpkg(manifest 모드, `vcpkg.json`), `CMakePresets.json`
- Windows: MSVC (Visual Studio 2022). Linux: Clang 또는 GCC + Ninja. 둘 다 빌드되게 유지한다(프리셋을 OS별로 둔다).
- OpenGL 4.5 core + glad, GLFW, GLM, stb_image, EnTT, FastNoiseLite, nlohmann/json, LZ4, ENet, Dear ImGui, Tracy, Catch2
- P1 이후 추가: miniaudio, Jolt. 그 밖의 라이브러리는 사용자에게 먼저 묻는다.

## 실행 환경

- Claude Code는 클라우드 Linux 컨테이너에서 작업한다. GPU와 화면이 없다.
- 그래서 모든 코드는 Linux(GCC 또는 Clang + Ninja)에서 빌드되고 테스트가 통과해야 한다. 렌더링 없이 도는 로직(청크, 메싱, 지형 생성, 직렬화, 저장)은 전부 단위 테스트로 검증한다.
- 창·렌더링 확인은 사용자가 Windows(Visual Studio 2022)에서 받아 실행해서 한다. 작업이 끝나면 "Windows에서 확인할 것"을 짧게 적어준다.
- 필요하면 Xvfb + Mesa(llvmpipe, OpenGL 4.5)로 헤드리스 렌더 스모크 테스트를 만들어 스크린샷을 남긴다.
- vcpkg가 없으면 `~/vcpkg`에 클론·부트스트랩하고 `VCPKG_ROOT`를 그곳으로 잡는다. 네트워크가 막혀 설치가 안 되면 멈추고 사용자에게 알린다.
- 클라우드 컨테이너는 GitHub 아카이브 tarball(`github.com/*/archive/*`)이 403으로 막히고 `git fetch`는 된다. configure 전에 `export X_VCPKG_ASSET_SOURCES="x-script,$PWD/tools/vcpkg_github_archive.sh {url} {sha512} {dst}"`를 설정한다(git으로 같은 tarball을 만들고, vcpkg가 SHA512를 그대로 검증한다).
- 작업은 브랜치에서 하고, 끝나면 커밋·푸시한다(사용자가 GitHub에서 확인하고 main에 합친다).

## 폴더 구조 (초안, P0 1단계에서 확정)

```
engine/src/<module>/        core platform data world entity net server client render ui sim
app/                        실행 파일 진입점 (main)
tests/                      Catch2 테스트
tools/texgen/               텍스처 생성 스크립트 (Python)
tools/motion_viewer/        동작 참고용 three.js 뷰어 (게임 코드 아님)
game/data/aurora/           게임 내용 JSON
game/assets/aurora/         textures/{block,item,entity,particle,ui}, models, animations, sounds, shaders, lang
docs/                       기획서와 참고 이미지·영상
```

모듈 의존 방향: `core ← platform ← data ← world ← entity ← (server | client) ← render ← ui`. server는 render·ui를 모른다.

## 코드 규칙

- 네임스페이스 `aurora::<module>`. 파일명 snake_case, 타입 PascalCase, 함수·변수 camelCase, 멤버 `m_` 접두사, 상수 `k` 접두사.
- 헤더는 `#pragma once`. 헤더에는 필요한 것만 include하고 가능하면 전방 선언.
- 게임 내용(블록·아이템·수치)은 코드에 하드코딩하지 않고 `game/data`의 JSON에서 읽는다. ID 형식은 `aurora:snake_case`.
- 월드 데이터는 서버 스레드만 쓴다. 워커는 복사본이나 새 결과만 만들어 돌려준다.
- 좌표: 블록 `int32`, 개체 위치 `double`, 렌더링은 카메라 기준 상대 `float`.
- 오류: 로그를 남기고 반환값으로 알린다. 예외는 시작 단계의 치명적 오류에만 쓴다.
- 틱(20tps), 섹션 크기(16), 높이 범위(-64~320, 규칙상 -50~300) 같은 상수는 한 헤더에 모은다.
- 무거운 구간에는 Tracy 존을 넣는다. 예산은 `docs/22_p0_tech.md`의 성능·메모리 예산 표를 따른다.

## 에셋 규칙

- 텍스처 밀도: 1블록 = 32픽셀, 모델 1단위(1/16블록) = 2픽셀. 블록·아이템 텍스처는 32×32.
- 접미사: `_n` 노멀(OpenGL 방식, +Y 위), `_e` 발광 마스크, `_ec` 발광 색, `_s` 재질(R 매끄러움, G 금속성, B 발광 세기).
- 샘플링: 확대는 최근접(Nearest), 축소는 밉맵. 블록은 텍스처 배열 하나로 묶는다.
- 플레이어 스킨은 지금 면별 PNG(`textures/entity/player/*`)로 있다. 128×128 아틀라스로 묶는 것은 캐릭터 렌더링 단계에서 한다.
- 새 텍스처는 `tools/texgen`에서 만들고 `python export.py`로 `game/assets`에 복사한다.
- 마인크래프트·워해머·Actions & Stuff의 에셋과 고유 명칭은 쓰지 않는다.

## 빌드·실행·테스트

프리셋: 공통 `debug` / `release`(Ninja, Linux·Windows), Windows 전용 `vs2022`(`.sln` 생성). 모두 `VCPKG_ROOT`가 필요하다.

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

- Linux 준비: `apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libxkbcommon-dev pkg-config xvfb autoconf autoconf-archive automake libtool` (autotools는 vcpkg의 `pthread-stubs` 포트 빌드에 필요)
- `xvfb-run`이 있으면 ctest에 `app_smoke`(Xvfb에서 창과 F3 디버그 화면을 띄워 60프레임 뒤 종료)가 함께 돈다.
- vcpkg 트리플릿은 `cmake/triplets`의 overlay를 쓴다(glad를 core 프로파일로 생성). 의존성은 처음 쓰는 단계에서 `vcpkg.json`에 추가한다.
- Windows에서 VS 2022로 여는 방법은 `README.md`.

## 작업 방식

1. `docs/ROADMAP.md`에서 다음 작업 하나만 고른다. 한 번에 한 단계(또는 그보다 작은 하위 작업)만 한다.
2. 코드를 쓰기 전에 계획(바꿀 파일, 새 타입, 테스트, 완료 확인 방법)을 보여주고 승인을 받는다.
3. 구현 후 반드시 빌드와 테스트를 통과시킨다. 실패한 채로 완료라고 보고하지 않는다.
4. 사용자가 직접 확인하는 방법(실행해서 무엇을 보면 되는지)을 알려준다.
5. `docs/ROADMAP.md`의 체크박스를 갱신하고, 커밋 메시지를 제안한다(사용자가 원하면 커밋까지).
6. 요청 범위 밖의 대규모 리팩터링이나 파일 이동은 하지 않는다. 필요해 보이면 제안만 한다.

# 결정 기록

기획서와 다르게 정했거나, 기획서에 없던 것을 새로 정했을 때 여기에 적는다. 최신이 위.

| 날짜 | 결정 | 이유 | 관련 문서 |
| --- | --- | --- | --- |
| 2026-09-30 | F3 디버그 화면은 처음에 숨겨 두고 F3으로 켜고 끈다. 키를 누른 순간(GLFW_PRESS)만 세고 키 반복은 무시한다. 창의 키 콜백은 ImGui 초기화보다 먼저 설치해 ImGui GLFW 백엔드가 이어 부르게 한다. `--debug-overlay`는 처음부터 켜는 옵션(스모크 테스트용) | 마인크래프트식 F3 조작. ImGui 백엔드가 콜백을 덮어쓰지 않고 앞선 콜백을 호출하는 구조라서 순서만 지키면 둘 다 입력을 받는다 | 12_controls_ui.md "영웅 모드 키" |
| 2026-09-30 | 서버 루프: 틱 n은 시작 시각 + n × 50 ms(절대 시각). 밀리면 한꺼번에 따라잡되, 한 번에 40틱(2초)을 넘게 밀리면 밀린 분을 버리고 1틱만 돌린 뒤 원래 50 ms 격자로 돌아간다. 상수 `kTickInterval`, `kMaxCatchUpTicks`는 `core/constants.h` | 잠 깨는 시각 오차가 쌓여 TPS가 흐르지 않게. 오래 멈췄다 돌아와도 수백 틱을 몰아 돌리지 않게(마인크래프트의 "Can't keep up"과 같은 방식) | 22_p0_tech.md "스레드" |
| 2026-09-30 | 워커 수 = 논리 프로세서 수(`std::thread::hardware_concurrency`) − 2, 최소 1. 잡 큐는 FIFO(우선순위는 P0-5 메싱에서). `shutdown()` 시작 뒤의 `submit()`은 거부(false, `async()`는 invalid future)하고, 이미 받은 작업은 끝까지 돌린 뒤 join. 워커 스레드를 만들다 실패하면 생성자가 이미 시작한 워커를 멈추고 join한 뒤 예외를 다시 던지고, `main`이 받아 로그를 남기고 종료 코드 1로 끝낸다(시작 단계의 치명적 오류) | 문서의 "코어 수"를 물리 코어로 셀 표준 방법이 없어서. 빠진 2개는 메인(클라이언트)·서버 스레드 몫. 종료 규칙을 정해 둬야 저장 같은 작업이 종료 중에 사라지지 않는다 | 22_p0_tech.md "스레드" |
| 2026-09-30 | Tracy는 vcpkg 포트를 기본 기능 없이(`crash-handler` 끔) `on-demand`로 쓴다. overlay triplet에서 tracy 포트에만 `VCPKG_CMAKE_CONFIGURE_OPTIONS`로 `TRACY_ENABLE`·`TRACY_ONLY_LOCALHOST`·`TRACY_NO_BROADCAST`를 켠다. 엔진 코드는 `core/profiler.h`의 `AURORA_PROFILE_*` 매크로만 쓴다 | baseline(b3ae22) 포트(0.14.1 port-version 0)는 `TRACY_ENABLE`을 넘기지 않아 프로파일러가 빈 껍데기로 빌드된다(port-version 1에서 고침). baseline 전체를 올리면 다른 라이브러리 버전도 바뀌므로 triplet으로 해결. 포트는 기능(feature)에 매핑된 옵션만 전달하므로 나머지도 triplet으로 준다. on-demand는 프로파일러가 안 붙었을 때 메모리가 쌓이지 않게, localhost·브로드캐스트 끔은 방화벽 창과 LAN 알림을 막으려고. 크래시 핸들러는 디버거·다른 크래시 처리와 겹치지 않게 끔 | CLAUDE.md "기술 스택" |
| 2026-09-30 | 로그는 라이브러리 없이 `std::format`으로 직접 만든다. 레벨 TRACE~ERROR, 콘솔 + 실행 폴더 `logs/latest.log`(직전 실행분은 `previous.log`) + Tracy 메시지. 줄마다 flush | 추가 라이브러리 없이 요구(레벨·파일 출력)를 채울 수 있어서. 줄마다 flush해서 크래시 직전 로그도 남긴다 | 22_p0_tech.md "구조" |
| 2026-09-30 | 클라우드 컨테이너에서는 vcpkg 자산 소스로 `tools/vcpkg_github_archive.sh`(x-script)를 쓴다. GitHub 아카이브 URL만 `git fetch --depth 1` + `git archive \| gzip -n`으로 만들고, 나머지 URL은 그대로 받는다 | 컨테이너 네트워크 정책이 `github.com/*/archive/*`만 403으로 막는다. `git archive \| gzip -n` 결과가 GitHub tarball과 바이트 단위로 같아서 포트의 SHA512 검증을 끄지 않고 우회할 수 있다. Windows와 일반 네트워크에서는 설정하지 않는다 | CLAUDE.md "실행 환경" |
| 2026-09-30 | glad는 vcpkg 포트를 그대로 쓰되, overlay 트리플릿(`cmake/triplets/x64-{linux,windows}.cmake`)에서 glad 포트에만 `GLAD_PROFILE=core`를 준다 | vcpkg glad 포트는 기본으로 호환성(compatibility) 프로파일 로더를 만들어 폐기된 함수(`glBegin` 등)까지 헤더에 노출한다. 4.5 core만 쓰도록 컴파일 단계에서 막기 위해. 포트 자체는 문제없이 동작하므로 glad2 직접 포함으로 바꾸지는 않았다 | 22_p0_tech.md "라이브러리·도구" |
| 2026-09-30 | CMake 프리셋: 공통 `debug`/`release`(Ninja, Linux·Windows 공용) + Windows 전용 `vs2022`(Visual Studio 17 2022 생성기, `.sln`) | CLAUDE.md의 "프리셋을 OS별로 둔다" 대신, 두 OS에서 같은 명령(`cmake --preset debug`)이 되게 하기 위해. VS 2022는 Ninja 프리셋을 "폴더 열기"로 그대로 쓴다. `release`는 RelWithDebInfo | CLAUDE.md "기술 스택", "빌드·실행·테스트" |
| 2026-09-30 | vcpkg 의존성은 처음 쓰는 단계에서 추가한다. P0-1은 glfw3·glad·imgui·catch2만 | 첫 빌드 시간을 줄이고 쓰지 않는 의존성을 쌓지 않기 위해. 전체 목록은 CLAUDE.md 그대로 유지 | CLAUDE.md "기술 스택" |
| 2026-09-30 | 엔진 모듈마다 정적 라이브러리 하나(`aurora_core`, `aurora_platform`, `aurora_render`, `aurora_ui` …) | 링크 관계로 모듈 의존 방향을 강제하기 위해 | CLAUDE.md "폴더 구조" |
| 2026-09-30 | 텍스처 밀도 1블록 = 32px (모델 1단위 = 2px), 플레이어 머리 7단위 | 3차 아트 피드백 | 14_art_guide.md |
| 2026-09-30 | 3D 무기 재질 개선은 계획만 기록, 구현은 해당 단계에서 | 사용자 요청 | 14_art_guide.md "3D 무기 모델·재질" |

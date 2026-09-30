# 결정 기록

기획서와 다르게 정했거나, 기획서에 없던 것을 새로 정했을 때 여기에 적는다. 최신이 위.

| 날짜 | 결정 | 이유 | 관련 문서 |
| --- | --- | --- | --- |
| 2026-09-30 | 클라우드 컨테이너에서는 vcpkg 자산 소스로 `tools/vcpkg_github_archive.sh`(x-script)를 쓴다. GitHub 아카이브 URL만 `git fetch --depth 1` + `git archive \| gzip -n`으로 만들고, 나머지 URL은 그대로 받는다 | 컨테이너 네트워크 정책이 `github.com/*/archive/*`만 403으로 막는다. `git archive \| gzip -n` 결과가 GitHub tarball과 바이트 단위로 같아서 포트의 SHA512 검증을 끄지 않고 우회할 수 있다. Windows와 일반 네트워크에서는 설정하지 않는다 | CLAUDE.md "실행 환경" |
| 2026-09-30 | glad는 vcpkg 포트를 그대로 쓰되, overlay 트리플릿(`cmake/triplets/x64-{linux,windows}.cmake`)에서 glad 포트에만 `GLAD_PROFILE=core`를 준다 | vcpkg glad 포트는 기본으로 호환성(compatibility) 프로파일 로더를 만들어 폐기된 함수(`glBegin` 등)까지 헤더에 노출한다. 4.5 core만 쓰도록 컴파일 단계에서 막기 위해. 포트 자체는 문제없이 동작하므로 glad2 직접 포함으로 바꾸지는 않았다 | 22_p0_tech.md "라이브러리·도구" |
| 2026-09-30 | CMake 프리셋: 공통 `debug`/`release`(Ninja, Linux·Windows 공용) + Windows 전용 `vs2022`(Visual Studio 17 2022 생성기, `.sln`) | CLAUDE.md의 "프리셋을 OS별로 둔다" 대신, 두 OS에서 같은 명령(`cmake --preset debug`)이 되게 하기 위해. VS 2022는 Ninja 프리셋을 "폴더 열기"로 그대로 쓴다. `release`는 RelWithDebInfo | CLAUDE.md "기술 스택", "빌드·실행·테스트" |
| 2026-09-30 | vcpkg 의존성은 처음 쓰는 단계에서 추가한다. P0-1은 glfw3·glad·imgui·catch2만 | 첫 빌드 시간을 줄이고 쓰지 않는 의존성을 쌓지 않기 위해. 전체 목록은 CLAUDE.md 그대로 유지 | CLAUDE.md "기술 스택" |
| 2026-09-30 | 엔진 모듈마다 정적 라이브러리 하나(`aurora_core`, `aurora_platform`, `aurora_render`, `aurora_ui` …) | 링크 관계로 모듈 의존 방향을 강제하기 위해 | CLAUDE.md "폴더 구조" |
| 2026-09-30 | 텍스처 밀도 1블록 = 32px (모델 1단위 = 2px), 플레이어 머리 7단위 | 3차 아트 피드백 | 14_art_guide.md |
| 2026-09-30 | 3D 무기 재질 개선은 계획만 기록, 구현은 해당 단계에서 | 사용자 요청 | 14_art_guide.md "3D 무기 모델·재질" |

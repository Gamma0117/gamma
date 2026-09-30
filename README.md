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

## Claude Code로 시작하기 (웹, GitHub 저장소 연결)

1. 이 폴더의 내용을 GitHub 저장소 최상위에 올린다 (`CLAUDE.md`가 저장소 바로 아래에 오게).
2. Claude Code에서 그 저장소·`main` 브랜치로 새 세션을 열고 `docs/PROMPTS.md`의 "맨 처음 한 번" 프롬프트를 보낸다.
3. 계획을 승인하면 Claude Code가 브랜치에 구현하고 푸시한다. GitHub에서 확인 후 main에 합친다.
4. 내 PC에서 `git pull` 받아 Visual Studio로 열어 실행해 본다(창·그래픽 확인은 PC에서만 된다).
5. 다음 작업은 새 세션에서 같은 방식으로 반복한다. 진행 상황은 `docs/ROADMAP.md`에 체크된다.

## 원본 기획서

claude.ai의 "프로젝트 오로라(가제) — 게임 기획서" 문서가 원본이다. 원본을 크게 고쳤으면 해당 탭을 다시 내보내 `docs/`를 갱신한다.

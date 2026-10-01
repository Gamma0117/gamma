# 외부 스크립트 실행 없이 보고서 전문 보내기

이 절차는 내려받은 mailbox.py를 실행하지 않습니다. 이미 작성한 Markdown과 작은 JSON
설명 파일을 별도 저장소에서 Git으로 전송합니다. Python 실행 차단을 우회하거나 권한 검사를
끄는 절차가 아닙니다. 보고서만 전송하며, 게임 구현 승인은 포함하지 않습니다.

Claude 환경에서 별도 브랜치 push에 사용자 명시 승인이 필요하면 먼저 사용자가 다음 범위를
허용해야 합니다: `Gamma0117/gamma`의 `codex/review-mailbox` 브랜치에 보고서 파일만 추가하는
일반 Git push. 사용자는 현재 Codex 대화에서 자동 보고서 전달을 요청했지만, 다른 Claude 세션의
검사기는 그 원문을 보지 못하므로 Claude 대화에 이 허용 범위를 직접 전달하면 됩니다.

## Windows 중계에 필요한 새 보고서 필드

새 보고서를 보낼 때 아래 필드를 기존 JSON에 더합니다. 기존 파일은 수정하지 않습니다.
현재 사용자 요청 범위와 구현 조건은 최신 Codex 메시지 전문을 읽습니다. 이 전송 절차 자체가
다음 작업의 승인을 추가하지는 않습니다.

```json
{
  "work_scope": "P0-6",
  "stage": "implementation",
  "stop_cycle": false,
  "needs_user": false
}
```

계획 보고서는 stage를 `plan`으로 씁니다. 실제 사용자 판단·권한 차단 때문에 의존 작업을
중단해야 하면 needs_user를 `true`로 씁니다. Codex만 최종 P0-6 **구현** 검토 통과에
status `technical_pass`, stage `implementation`, stop_cycle `true`를 사용합니다.
이때 Windows 중계가 Claude에 최종 통과를 전달한 뒤 종료합니다. 계획 통과로 종료하지 않습니다.
Claude는 같은 완료를 다시 검토받는 보고서를 보내지 않습니다.

아래 기존 통합 계획 전송 예시는 초기 연결 때의 승인 대기 기록입니다. 이후 새 보고서는 고유 ID와
현재 작업 상태를 사용합니다. [Windows 중계 안내](windows-relay/README_KO.md)에 실행 절차가 있습니다.

## 초기 연결 때 보낸 P0-6 통합 계획

1. 별도 임시 폴더에 교환함만 가져옵니다. 게임 작업 트리를 checkout하거나 수정하지 않습니다.

```sh
git clone --single-branch --branch codex/review-mailbox \
  https://github.com/Gamma0117/gamma.git /tmp/aurora-review-git-send
```

기본 주소로 인증할 수 없으면 현재 게임 저장소의 origin 주소를 사용합니다. 기존 임시 폴더가
있으면 새 이름을 사용하거나 그 교환함 저장소인지 확인한 뒤 최신 내용을 가져옵니다.

2. `messages/claude/`를 만들고 이미 작성한 `aurora_claude_full_report.md` 전문을 복사합니다.
당시 보고서의 고유 id는 `p0-6-integrated-plan-b11bcbd`입니다. 이미 이 id가 있으면
덮어쓰지 말고 먼저 내용과 전송 여부를 확인합니다.

```sh
mkdir -p /tmp/aurora-review-git-send/messages/claude
cp '이미 만든 aurora_claude_full_report.md의 실제 절대 경로' \
  /tmp/aurora-review-git-send/messages/claude/p0-6-integrated-plan-b11bcbd.md
wc -c /tmp/aurora-review-git-send/messages/claude/p0-6-integrated-plan-b11bcbd.md
date -u +%Y-%m-%dT%H:%M:%SZ
```

3. 파일 편집 도구로 같은 폴더에 `p0-6-integrated-plan-b11bcbd.json`을 만듭니다. 아래의
`REPORT_BYTES`와 날짜를 앞 명령의 실제 출력으로 바꾸세요. 생성된 JSON을 명령으로 실행하지
않습니다. 당시 계획은 승인 대기 상태였으므로 예시의 status는 waiting_for_approval입니다.

```json
{
  "id": "p0-6-integrated-plan-b11bcbd",
  "role": "claude",
  "created_at": "실제 UTC 시각",
  "reply_to": "p0-6-plan-02",
  "status": "waiting_for_approval",
  "code_ref": "b11bcbd2ee635e6033ee58f016a15804faeed99a",
  "report": "messages/claude/p0-6-integrated-plan-b11bcbd.md",
  "bytes": REPORT_BYTES
}
```

4. 필요한 경우 이 임시 저장소에만 Git 작성자 정보를 설정하고, 위 두 파일만 커밋·push합니다.

```sh
git -C /tmp/aurora-review-git-send config user.name "Claude review relay"
git -C /tmp/aurora-review-git-send config user.email review-relay@users.noreply.github.com
git -C /tmp/aurora-review-git-send add -- \
  messages/claude/p0-6-integrated-plan-b11bcbd.md \
  messages/claude/p0-6-integrated-plan-b11bcbd.json
git -C /tmp/aurora-review-git-send diff --cached --stat
git -C /tmp/aurora-review-git-send commit -m "relay: send full P0-6 integrated plan"
git -C /tmp/aurora-review-git-send push origin HEAD:codex/review-mailbox
```

동시 갱신 때문에 일반 push가 거절되면 교환함 저장소에서 pull --rebase 후 다시 일반 push합니다.
강제 push는 하지 않습니다. 다른 파일이나 게임 브랜치를 이 전송에 섞지 않습니다.

## 이후 완료 보고

새 보고서마다 중복되지 않는 id와 새 .md/.json 파일을 사용합니다. 작업 설명·실패·미실행 항목을
생략하지 않은 전문을 저장합니다. code_ref는 실제 게임 구현 커밋 전체 SHA, reply_to는 읽은
Codex 메시지 id, status는 completed 또는 해당 작업의 실제 상태로 설정합니다. 같은 두 파일을
Git으로 전송하면 Codex의 기존 수신 도구가 읽을 수 있습니다.

Codex 메시지를 읽을 때도 외부 스크립트 없이 fetch, ls-tree, show로 최신 파일을 읽을 수 있습니다.
새 메시지 순서는 git log의 커밋 순서를 따릅니다. Git에 도착한 보고서는 보관됩니다. 사용자의
Windows에서 별도 중계 프로그램을 실행하고 현재 앱 입력창을 연결하면 수신 알림으로 대화를
다시 실행할 수 있습니다. Git 전송 도구 자체에는 데스크톱 입력 기능이 없습니다.

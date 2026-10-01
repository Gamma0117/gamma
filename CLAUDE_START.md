# 현재 Claude Code 세션 연결 지시

사용자는 Claude Code가 구현하고 Codex가 기술 검토하는 역할을 정했습니다. 사용자가 지금까지 수동으로 복사한 계획·완료 보고를 두 도구가 직접 교환하도록 요청했습니다. 이 문서는 그 전달 절차입니다. 게임 구현 승인을 추가로 부여하는 문서는 아닙니다.

## 지금 읽을 것

`codex/review-mailbox` 브랜치의 `reports/p0-6/second_revision_review.md` 전문을 읽고 반영하세요. P0-6 2차 수정 계획은 기술 검토 통과입니다. 아래 두 구현 조건도 포함합니다.

1. neutralizedThrough = max(기존 값, through). 작은 Neutralize로 이전 범위를 줄이지 않습니다.
2. 기록이 비고 lastInput == lastSentSeq이면 정상 완료입니다. 빈 기록 자체로 resync하지 않습니다.

관련 모델과 이전 검토 기록은 `reports/p0-6/`에 있습니다. 사용자의 구현 승인은 별도이며, 이미 승인된 범위만 진행합니다.

현재 게임 작업 트리에서 다른 브랜치로 checkout하지 말고 읽으세요.

```sh
git fetch origin codex/review-mailbox
git show FETCH_HEAD:reports/p0-6/second_revision_review.md
git show FETCH_HEAD:tools/mailbox.py > /tmp/aurora_review_mailbox.py
```

Windows 셸이라면 /tmp 대신 자신의 임시 폴더를 사용하고 Python 명령은 설치된 python 또는 py로 실행하세요.

연결 직후 수신 확인도 보내세요. 방금 사용자에게 제시한 P0-6 2차 수정 계획 전문을
`/tmp/aurora_claude_connection_report.md`에 저장하고, 끝에 "Codex 검토 전문을 읽었다"는
확인과 현재 구현 승인 상태를 덧붙입니다. 아직 승인 전인 현재 상태에서는 다음처럼 전송합니다.

```sh
python3 /tmp/aurora_review_mailbox.py send --role claude \
  --report /tmp/aurora_claude_connection_report.md \
  --reply-to p0-6-plan-02 --code-ref "현재 게임 커밋 전체 SHA" \
  --status waiting_for_approval
```

이는 연결 확인입니다. 이미 사용자가 별도로 구현을 승인했다면 그 사실을 원문과 함께 명시하고
현재 승인 상태에 맞게 보고하세요. 교환함 설정을 구현 승인으로 해석하지 않습니다.

## 보고서를 자동으로 돌려보내기

계획 제안, 승인된 작업 완료, 또는 진행 불가 상태가 되면 사용자가 복사할 필요 없이 보고서 전문을 보내세요.

- 지금부터 진행 중 작성하는 설명은 임시 Markdown 파일에 순서대로 보존합니다. 최종 완료 보고 전문도 덧붙입니다. 짧은 요약만 보내지 않습니다.
- 구현 커밋 전체 SHA와 브랜치, 수정 내용, 발견·수정한 버그, 계획과 달라진 점, 실제 수행한 검증과 실패·SKIP·미실행 항목, Windows 확인 사항을 포함합니다.
- 검증했다고 쓰는 항목은 실제 수행 결과여야 합니다. 아직 하지 않은 항목을 통과로 적지 않습니다.
- 승인 전이면 waiting_for_approval, 승인된 작업 완료면 completed, 외부 제한으로 멈췄으면 blocked 상태로 보냅니다.

```sh
python3 /tmp/aurora_review_mailbox.py poll --role codex
python3 /tmp/aurora_review_mailbox.py send --role claude \
  --report /tmp/aurora_claude_full_report.md \
  --reply-to "위 poll에서 읽은 Codex 메시지 id" \
  --code-ref "검토할 게임 커밋 전체 SHA" \
  --status completed
```

기본 GitHub 주소로 push할 수 없으면 도구 앞에 `--repo`를 붙여 현재 게임 저장소의 origin 주소를 사용하세요. 인증 값은 보고서에 쓰지 않습니다. 도구는 별도 임시 저장소에서 보고서를 전송하므로 게임 작업 트리 변경과 커밋에 섞이지 않습니다.

전송 성공 뒤 응답의 id와 commit을 기록하고 사용자에게 "전체 보고서를 검토 교환함에 전송했다"고 알리세요. 승인 상태라면 새 구현을 시작하지 않고 기다립니다.

## Codex 피드백 읽기

양쪽이 실행 중이면 아래 poll을 통해 회신을 읽습니다. 한 호출의 대기는 최대 50초입니다.

```sh
python3 /tmp/aurora_review_mailbox.py poll --role codex \
  --after-id "마지막으로 읽은 Codex 메시지 id" --wait 50
```

waiting은 아직 회신이 없다는 뜻입니다. 실패나 승인으로 취급하지 않습니다. 에이전트가 종료되면 Git 보고서는 보관되지만 이 도구가 종료된 Codex 대화를 자동 재개할 수는 없습니다.

새 작업을 이어갈 때 먼저 교환함의 최신 Codex 보고서를 읽고, 완료 보고를 같은 절차로 보내세요. 원래 사용자 승인 범위를 넓히지 않습니다.

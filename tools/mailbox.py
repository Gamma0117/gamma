#!/usr/bin/env python3
"""Exchange complete review reports on an isolated Git branch; never execute reports."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time
import uuid

BRANCH = "codex/review-mailbox"
DEFAULT_REPO = "https://github.com/Gamma0117/gamma.git"


def git(directory, *args):
    result = subprocess.run(["git", "-C", str(directory), *args],
                            capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"git {args[0]} failed: {result.stderr.strip()}")
    return result.stdout


def initialize(directory, repo):
    git(directory, "init", "--quiet")
    git(directory, "remote", "add", "origin", repo)


def fetch(directory):
    git(directory, "fetch", "--quiet", "origin", BRANCH)


def send(args):
    if args.stop_cycle and not (args.role == "codex" and args.status == "technical_pass"
                                and args.stage == "implementation"):
        raise ValueError("stop-cycle requires a Codex implementation technical pass")
    body = args.report.read_bytes()
    body.decode("utf-8")
    if not body:
        raise ValueError("The full report must not be empty")
    now = datetime.now(timezone.utc)
    message_id = now.strftime("%Y%m%dT%H%M%S%fZ") + "_" + uuid.uuid4().hex[:12]
    base = f"messages/{args.role}/{message_id}"
    metadata = {
        "id": message_id, "role": args.role, "created_at": now.isoformat(),
        "reply_to": args.reply_to, "status": args.status, "code_ref": args.code_ref,
        "report": base + ".md", "bytes": len(body),
        "work_scope": "P0-6", "stage": args.stage,
        "stop_cycle": args.stop_cycle, "needs_user": args.needs_user,
    }
    with tempfile.TemporaryDirectory(prefix="aurora-review-send-") as temporary:
        directory = Path(temporary)
        initialize(directory, args.repo)
        git(directory, "config", "user.name", f"Aurora {args.role} review relay")
        git(directory, "config", "user.email", "review-relay@users.noreply.github.com")
        for attempt in range(4):
            fetch(directory)
            git(directory, "checkout", "--quiet", "-B", "relay", "FETCH_HEAD")
            report = directory / metadata["report"]
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_bytes(body)
            envelope = directory / (base + ".json")
            envelope.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
                                encoding="utf-8")
            git(directory, "add", "--", str(report.relative_to(directory)),
                str(envelope.relative_to(directory)))
            git(directory, "commit", "--quiet", "-m", f"relay: {args.role} {args.status} {message_id}")
            push = subprocess.run(["git", "-C", str(directory), "push", "--quiet", "origin",
                                   f"HEAD:refs/heads/{BRANCH}"],
                                  capture_output=True, text=True, check=False)
            if push.returncode == 0:
                print(json.dumps({"status": "sent", "message": metadata,
                                  "commit": git(directory, "rev-parse", "HEAD").strip()},
                                 ensure_ascii=False))
                return
            if not any(reason in push.stderr for reason in
                       ("fetch first", "non-fast-forward", "failed to update ref", "cannot lock ref",
                        "incorrect old value provided", "stale info")):
                detail = push.stderr.replace(args.repo, "<remote>")
                detail = re.sub(r"(https?://)[^/\s@]+@", r"\1[redacted]@", detail)
                raise RuntimeError("Could not push the report; check Git repository write access: " + detail)
        raise RuntimeError("Mailbox changed during all four push attempts; retry the same report")


def read_messages(directory, role, after_id):
    # Commit ancestry establishes delivery order, independently of sender clock differences.
    paths = git(directory, "log", "--format=", "--name-only", "FETCH_HEAD", "--",
                f"messages/{role}/*.json").splitlines()
    unique = list(dict.fromkeys(path for path in paths if path))
    messages = [json.loads(git(directory, "show", f"FETCH_HEAD:{path}")) for path in unique]
    if after_id:
        index = next((i for i, message in enumerate(messages) if message["id"] == after_id), None)
        if index is None:
            raise ValueError("after-id is not a message from the requested role")
        messages = list(reversed(messages[:index]))
    else:
        messages = messages[:1]
    for message in messages:
        expected = f"messages/{role}/{message['id']}.md"
        if message.get("role") != role or message.get("report") != expected:
            raise ValueError("Invalid mailbox envelope")
        message["body"] = git(directory, "show", f"FETCH_HEAD:{expected}")
    return messages


def poll(args):
    deadline = time.monotonic() + args.wait
    with tempfile.TemporaryDirectory(prefix="aurora-review-poll-") as temporary:
        directory = Path(temporary)
        initialize(directory, args.repo)
        while True:
            fetch(directory)
            messages = read_messages(directory, args.role, args.after_id)
            if messages:
                print(json.dumps({"status": "received", "messages": messages}, ensure_ascii=False))
                return
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                print(json.dumps({"status": "waiting", "messages": []}))
                return
            time.sleep(min(5, remaining))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=DEFAULT_REPO)
    subcommands = parser.add_subparsers(dest="command", required=True)
    publish = subcommands.add_parser("send")
    publish.add_argument("--role", choices=["claude", "codex"], required=True)
    publish.add_argument("--report", type=Path, required=True)
    publish.add_argument("--reply-to", default="")
    publish.add_argument("--code-ref", default="")
    publish.add_argument("--status", choices=["completed", "technical_pass", "changes_requested",
                                             "waiting_for_approval", "blocked"], required=True)
    publish.add_argument("--stage", choices=["unspecified", "plan", "implementation"], default="unspecified")
    publish.add_argument("--stop-cycle", action="store_true")
    publish.add_argument("--needs-user", action="store_true")
    receive = subcommands.add_parser("poll")
    receive.add_argument("--role", choices=["claude", "codex"], required=True)
    receive.add_argument("--after-id", default="")
    receive.add_argument("--wait", type=int, choices=range(0, 51), default=0)
    args = parser.parse_args()
    try:
        send(args) if args.command == "send" else poll(args)
    except (RuntimeError, ValueError, OSError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()

"""Exercise the relay against a local bare remote; no GitHub mutations."""

from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys
import tempfile

TOOL = Path(__file__).with_name("mailbox.py")


def run(*args):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="aurora-relay-test-") as temporary:
    root = Path(temporary)
    remote = root / "remote.git"
    seed = root / "seed"
    run("git", "init", "--bare", remote)
    run("git", "init", "-b", "codex/review-mailbox", seed)
    run("git", "-C", seed, "config", "user.name", "Relay test")
    run("git", "-C", seed, "config", "user.email", "relay-test@example.invalid")
    (seed / "README.md").write_text("test mailbox\n", encoding="utf-8")
    run("git", "-C", seed, "add", "README.md")
    run("git", "-C", seed, "commit", "-m", "initial")
    run("git", "-C", seed, "push", str(remote), "HEAD:codex/review-mailbox")
    report = root / "report.md"
    body = '전체 보고서\n실패 1건, 통과 2건\n`$(echo keep-literal)`\n'
    report.write_text(body, encoding="utf-8")

    def relay(*args):
        return json.loads(run(sys.executable, TOOL, "--repo", remote, *args))

    assert relay("poll", "--role", "claude") == {"status": "waiting", "messages": []}
    first = relay("send", "--role", "codex", "--report", report, "--status", "technical_pass")
    first_id = first["message"]["id"]
    assert relay("poll", "--role", "codex")["messages"][0]["body"] == body
    assert relay("poll", "--role", "codex", "--after-id", first_id)["status"] == "waiting"
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(relay, "send", "--role", role, "--report", report,
                            "--reply-to", first_id, "--code-ref", "b11bcbd", "--status", "completed")
                for role in ["claude", "codex"]]
        second = [job.result() for job in jobs]
    assert relay("poll", "--role", "claude")["messages"][0]["body"] == body
    messages = relay("poll", "--role", "codex", "--after-id", first_id)["messages"]
    assert len(messages) == 1 and messages[0]["id"] == second[1]["message"]["id"]
    assert messages[0]["reply_to"] == first_id
    assert relay("poll", "--role", "codex", "--after-id", messages[0]["id"])["status"] == "waiting"
    failed = subprocess.run([sys.executable, str(TOOL), "--repo", str(remote), "poll", "--role",
                             "codex", "--after-id", "not-a-real-message"], capture_output=True, text=True)
    assert failed.returncode == 1
    assert "after-id" in failed.stderr
    run("git", "-C", seed, "fetch", str(remote), "codex/review-mailbox")
    files = run("git", "-C", seed, "ls-tree", "-r", "--name-only", "FETCH_HEAD").splitlines()
    assert "README.md" in files
    assert len([name for name in files if name.endswith(".json")]) == 3
    invalid = subprocess.run([sys.executable, str(TOOL), "--repo", str(remote), "send", "--role",
                              "claude", "--report", str(report), "--status", "completed", "--stop-cycle"],
                             capture_output=True, text=True)
    assert invalid.returncode == 1 and "stop-cycle" in invalid.stderr
    final = relay("send", "--role", "codex", "--report", report, "--status", "technical_pass",
                  "--stage", "implementation", "--stop-cycle")
    received_final = relay("poll", "--role", "codex")["messages"][0]
    assert received_final["stop_cycle"] is True and received_final["stage"] == "implementation"
    assert received_final["work_scope"] == "P0-6" and received_final["needs_user"] is False
    question = relay("send", "--role", "claude", "--report", report, "--status", "blocked", "--needs-user")
    assert relay("poll", "--role", "claude")["messages"][0]["needs_user"] is True
    print("PASS: UTF-8 full report, literal text, concurrent append-only send, reply ID, unread polling")
    print("PASS: final implementation stop signal and user-decision signal")

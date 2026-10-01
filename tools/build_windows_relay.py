"""Build a reproducible Windows relay ZIP from reviewed local sources."""
import hashlib
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1] / "windows-relay"
files = [
    "Launcher.cmd", "AuroraRelay.ps1", "Relay.Core.psm1", "Read-Mailbox.ps1",
    "Windows.Automation.cs", "README_KO.md", "VALIDATION.md",
    "tests/Relay.Tests.ps1", "tests/Read-Mailbox.Tests.ps1", "tests/Validate-Source.ps1",
]
target = root / "AuroraRelay.zip"
with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in files:
        content = (root / name).read_bytes()
        if name.endswith((".ps1", ".psm1")) and not content.startswith(b"\xef\xbb\xbf"):
            raise ValueError(f"Windows PowerShell source requires UTF-8 BOM: {name}")
        entry = zipfile.ZipInfo("AuroraRelay/" + name, (2026, 10, 1, 0, 0, 0))
        entry.create_system = 3
        entry.external_attr = 0o100644 << 16
        entry.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(entry, content)
digest = hashlib.sha256(target.read_bytes()).hexdigest()
(root / "AuroraRelay.zip.sha256").write_text(f"{digest}  AuroraRelay.zip\n", encoding="ascii")
print(f"Built {target.name}: {target.stat().st_size} bytes, {len(files)} files")
print(f"SHA256 {digest}")

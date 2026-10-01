param([string]$GitPath = (Get-Command git -CommandType Application | Select-Object -First 1).Source)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$source = Join-Path $PSScriptRoot '..'
Import-Module (Join-Path $source 'Relay.Core.psm1') -Force -DisableNameChecking
$cache = Join-Path ([IO.Path]::GetTempPath()) ('aurora mailbox 한글 ' + [guid]::NewGuid().ToString('N'))
try {
    $first = & (Join-Path $source 'Read-Mailbox.ps1') -Cache $cache -GitPath $GitPath
    if (-not $first.Changed -or $first.Commit -notmatch '^[a-f0-9]{40}$' -or @($first.Messages).Count -eq 0) {
        throw 'Cold fetch did not read the public mailbox.'
    }
    $state = New-RelayState
    Initialize-RelayBaseline $state @($first.Messages) $true
    if ($state.Queue.Count -ne 1 -or $state.Queue[0].Id -ne @($first.Messages)[-1].id) {
        throw 'Initial delivery does not match the newest Git arrival.'
    }
    Write-Output "PASS cold public fetch: $(@($first.Messages).Count) messages, UTF-8 path with spaces"
    $same = & (Join-Path $source 'Read-Mailbox.ps1') -Cache $cache -GitPath $GitPath -PreviousCommit $first.Commit
    if ($same.Changed -or @($same.Messages).Count -ne 0) { throw 'Unchanged fetch emitted messages again.' }
    Write-Output 'PASS unchanged Git commit emits no messages'
    & $GitPath -C $cache remote set-url origin 'https://invalid.example/wrong.git'
    if ($LASTEXITCODE -ne 0) { throw 'Could not change the test-only cache origin.' }
    $rejected = $false
    try { & (Join-Path $source 'Read-Mailbox.ps1') -Cache $cache -GitPath $GitPath | Out-Null }
    catch { $rejected = $_.Exception.Message.Contains('origin') }
    if (-not $rejected) { throw 'A different remote was accepted.' }
    Write-Output 'PASS fixed-remote guard rejects a changed origin before fetch'
} finally {
    if (Test-Path -LiteralPath $cache) { Remove-Item -LiteralPath $cache -Recurse -Force }
}

param([string]$Cache, [string]$GitPath, [string]$PreviousCommit = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'Relay.Core.psm1') -Force -DisableNameChecking

function Invoke-MailboxGit {
    param([string[]]$Arguments)
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $GitPath
    $info.Arguments = (@('-C', $Cache) + $Arguments | ForEach-Object { ConvertTo-RelayNativeArgument $_ }) -join ' '
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.EnvironmentVariables['GIT_TERMINAL_PROMPT'] = '0'
    $info.EnvironmentVariables['GCM_INTERACTIVE'] = 'Never'
    $info.StandardOutputEncoding = New-Object Text.UTF8Encoding($false)
    $info.StandardErrorEncoding = New-Object Text.UTF8Encoding($false)
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        $null = $process.Start()
        $out = $process.StandardOutput.ReadToEndAsync()
        $err = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
                & "$env:SystemRoot\System32\taskkill.exe" /PID $process.Id /T /F 2>$null | Out-Null
            } else { $process.Kill() }
            throw 'Git 명령이 30초 안에 끝나지 않았습니다.'
        }
        $output = $out.GetAwaiter().GetResult()
        $errorOutput = $err.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            # Only the public, fixed repo is used; do not print credential-helper output.
            throw "Git $($Arguments[0]) 실패 (종료 코드 $($process.ExitCode)). 네트워크와 Git 설치를 확인하세요."
        }
        return $output
    } finally { $process.Dispose() }
}

$null = [IO.Directory]::CreateDirectory($Cache)
if (-not (Test-Path -LiteralPath (Join-Path $Cache '.git'))) {
    $null = Invoke-MailboxGit @('init', '--quiet')
    $null = Invoke-MailboxGit @('remote', 'add', 'origin', 'https://github.com/Gamma0117/gamma.git')
}
$remote = (Invoke-MailboxGit @('remote', 'get-url', 'origin')).Trim()
if ($remote -cne 'https://github.com/Gamma0117/gamma.git') { throw '전용 교환함 캐시의 origin이 다릅니다.' }
$null = Invoke-MailboxGit @('-c', 'credential.interactive=false', '-c', 'http.lowSpeedLimit=1024',
                            '-c', 'http.lowSpeedTime=20', 'fetch', '--quiet', 'origin', 'codex/review-mailbox')
$commit = (Invoke-MailboxGit @('rev-parse', 'FETCH_HEAD')).Trim()
if ($commit -eq $PreviousCommit) {
    [pscustomobject]@{ Commit = $commit; Changed = $false; Messages = @() }
    return
}
$paths = (Invoke-MailboxGit @('log', '--format=', '--name-only', 'FETCH_HEAD', '--',
                             'messages/claude/*.json', 'messages/codex/*.json')) -split '\r?\n'
$seen = New-Object 'Collections.Generic.HashSet[string]'
$unique = New-Object 'Collections.Generic.List[string]'
foreach ($path in $paths) {
    if ($path -and $seen.Add($path)) { $unique.Add($path) }
}
$unique.Reverse()
$messages = @()
foreach ($path in $unique) {
    if ($path -notmatch '^messages/(claude|codex)/[A-Za-z0-9_-]{1,128}\.json$') {
        throw '허용되지 않은 교환함 설명 파일 경로입니다.'
    }
    $json = Invoke-MailboxGit @('show', "FETCH_HEAD:$path")
    if ($json.Length -gt 65536) { throw '설명 JSON이 너무 큽니다.' }
    $message = $json | ConvertFrom-Json
    $null = Test-RelayEnvelope $message
    if ($path -cne "messages/$($message.role)/$($message.id).json") {
        throw 'JSON 파일 이름과 메시지 ID가 다릅니다.'
    }
    $null = Invoke-MailboxGit @('cat-file', '-e', "FETCH_HEAD:$($message.report)")
    $messages += $message
}
[pscustomobject]@{ Commit = $commit; Changed = $true; Messages = $messages }

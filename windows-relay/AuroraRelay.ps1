param([string]$DataDirectory = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw '이 프로그램은 Windows 데스크톱에서 실행해야 합니다.'
}
if ([Threading.Thread]::CurrentThread.ApartmentState -ne [Threading.ApartmentState]::STA) {
    throw 'Launcher.cmd로 실행하거나 powershell.exe -STA -File AuroraRelay.ps1를 사용하세요.'
}
Import-Module (Join-Path $PSScriptRoot 'Relay.Core.psm1') -Force -DisableNameChecking
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes, WindowsBase
$references = @(
    [Diagnostics.Process].Assembly.Location, [Windows.Forms.Form].Assembly.Location,
    [Drawing.Point].Assembly.Location, [Windows.Automation.AutomationElement].Assembly.Location,
    [Windows.Automation.ControlType].Assembly.Location, [System.Windows.Point].Assembly.Location
) | Select-Object -Unique
Add-Type -TypeDefinition ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Windows.Automation.cs'))) -ReferencedAssemblies $references
[AuroraReviewRelay.Native]::EnableDpiAwareness()
[Windows.Forms.Application]::EnableVisualStyles()

$created = $false
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value.Replace('-', '_')
$mutex = New-Object Threading.Mutex($true, "Local\AuroraReviewRelay_$sid", [ref]$created)
if (-not $created) {
    [Windows.Forms.MessageBox]::Show('이미 실행 중인 중계 프로그램이 있습니다.', 'Aurora 중계') | Out-Null
    $mutex.Dispose(); return
}
if (-not $DataDirectory) { $DataDirectory = Join-Path $env:LOCALAPPDATA 'AuroraReviewRelay' }
$null = [IO.Directory]::CreateDirectory($DataDirectory)
$script:StatePath = Join-Path $DataDirectory 'state.json'
$script:ConfigPath = Join-Path $DataDirectory 'config.json'
$script:LogPath = Join-Path $DataDirectory 'relay.log'
$script:State = New-RelayState
$script:Config = [pscustomobject]@{
    Version = 1; PollSeconds = 20; MaxDeliveries = 20; MaxMinutes = 180; KickoffLatest = $true
    Claude = $null; Codex = $null
}
$script:StartupNotice = ''
foreach ($entry in @(@('State', $script:StatePath), @('Config', $script:ConfigPath))) {
    if (Test-Path -LiteralPath $entry[1]) {
        try {
            $loaded = [IO.File]::ReadAllText($entry[1]) | ConvertFrom-Json
            if ($entry[0] -eq 'State') { $script:State = Restore-RelayState $loaded }
            elseif ($loaded.Version -eq 1) { $script:Config = $loaded }
        } catch {
            # Do not silently clear the journal; that would resend old notifications.
            $script:StartupNotice = "상태 파일을 읽지 못했습니다: $($entry[1]). 파일을 보존하고 실행을 중단합니다."
        }
    }
}
if ($script:StartupNotice) {
    [Windows.Forms.MessageBox]::Show($script:StartupNotice, 'Aurora 중계') | Out-Null
    $mutex.ReleaseMutex(); $mutex.Dispose(); return
}

function Find-Git {
    $command = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'Git\cmd\git.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Git\cmd\git.exe')
    )) { if (Test-Path -LiteralPath $candidate) { return $candidate } }
    throw 'Git for Windows가 필요합니다. Git을 설치한 뒤 프로그램을 다시 실행하세요.'
}
try { $script:GitPath = Find-Git }
catch {
    [Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Aurora 중계') | Out-Null
    $mutex.ReleaseMutex(); $mutex.Dispose(); return
}

$script:Form = New-Object AuroraReviewRelay.RelayForm
$script:Form.Text = 'Aurora — Claude ↔ Codex 중계 (P0-6)'
$script:Form.ClientSize = New-Object Drawing.Size(820, 610)
$script:Form.MinimumSize = New-Object Drawing.Size(840, 650)
$script:Form.StartPosition = 'CenterScreen'
$script:Form.Font = New-Object Drawing.Font('맑은 고딕', 10)
$script:Form.AutoScaleMode = 'Dpi'

function Label-Control {
    param([string]$Text, [int]$X, [int]$Y, [int]$Width, [int]$Height = 28)
    $control = New-Object Windows.Forms.Label
    $control.Text = $Text; $control.Location = New-Object Drawing.Point($X, $Y)
    $control.Size = New-Object Drawing.Size($Width, $Height)
    $script:Form.Controls.Add($control)
    return $control
}
function Button-Control {
    param([string]$Text, [int]$X, [int]$Y, [int]$Width = 150)
    $control = New-Object Windows.Forms.Button
    $control.Text = $Text; $control.Location = New-Object Drawing.Point($X, $Y)
    $control.Size = New-Object Drawing.Size($Width, 36)
    $script:Form.Controls.Add($control)
    return $control
}
function Check-Control {
    param([string]$Text, [int]$X, [int]$Y, [int]$Width)
    $control = New-Object Windows.Forms.CheckBox
    $control.Text = $Text; $control.Location = New-Object Drawing.Point($X, $Y)
    $control.Size = New-Object Drawing.Size($Width, 28)
    $script:Form.Controls.Add($control)
    return $control
}
function Number-Control {
    param([int]$X, [int]$Y, [int]$Minimum, [int]$Maximum, [int]$Value)
    $control = New-Object Windows.Forms.NumericUpDown
    $control.Location = New-Object Drawing.Point($X, $Y); $control.Size = New-Object Drawing.Size(75, 28)
    $control.Minimum = $Minimum; $control.Maximum = $Maximum
    $control.Value = [Math]::Max($Minimum, [Math]::Min($Maximum, $Value))
    $script:Form.Controls.Add($control)
    return $control
}
$null = Label-Control 'P0-6 보고서를 감시해 지정한 대화의 빈 입력창에 알림을 보냅니다.' 16 12 780
$null = Label-Control '연결 버튼을 누른 뒤 4초 안에 해당 앱의 빈 입력창 위로 마우스를 옮기세요.' 16 40 780
$captureClaude = Button-Control '1. Claude 연결' 16 78
$previewClaude = Button-Control '입력 위치 확인' 176 78 130
$script:ClaudeLabel = Label-Control '연결되지 않음' 320 82 475
$captureCodex = Button-Control '2. Codex 연결' 16 124
$previewCodex = Button-Control '입력 위치 확인' 176 124 130
$script:CodexLabel = Label-Control '연결되지 않음' 320 128 475
$script:CoordinateCheck = Check-Control '좌표 방식: 초안·전송 확인을 생략합니다 (자동 인식 실패 시에만)' 16 170 780
$script:CtrlEnterCheck = Check-Control '이번 연결의 전송 키: Ctrl+Enter (기본 Enter)' 16 200 470
$script:KickoffCheck = Check-Control '처음 시작할 때 최신 보고서 하나를 반대편 앱에 전달' 16 232 780
$script:KickoffCheck.Checked = $script:Config.KickoffLatest
$null = Label-Control '감시 간격(초)' 16 268 105
$script:PollNumber = Number-Control 122 267 5 120 $script:Config.PollSeconds
$null = Label-Control '최대 전송(건)' 220 268 105
$script:SendNumber = Number-Control 326 267 1 200 $script:Config.MaxDeliveries
$null = Label-Control '실행 한도(분)' 425 268 105
$script:MinuteNumber = Number-Control 531 267 5 1440 $script:Config.MaxMinutes
$script:RunButton = Button-Control '3. 시작 / 재개' 16 306 170
$pauseButton = Button-Control '일시정지' 196 306 120
$script:SentButton = Button-Control '전송됨 확인' 326 306 145
$script:RetryButton = Button-Control '미전송: 다시 시도' 481 306 170
$null = Label-Control 'Ctrl+Alt+F8: 정지/재개 · 최근 3초에 사용자 입력이 있으면 전송을 기다립니다.' 16 348 790
$script:StatusLabel = Label-Control '' 16 383 785 52
$script:StatusLabel.Font = New-Object Drawing.Font('맑은 고딕', 10, [Drawing.FontStyle]::Bold)
$script:LogBox = New-Object Windows.Forms.TextBox
$script:LogBox.Location = New-Object Drawing.Point(16, 442)
$script:LogBox.Size = New-Object Drawing.Size(785, 152)
$script:LogBox.Multiline = $true; $script:LogBox.ReadOnly = $true; $script:LogBox.ScrollBars = 'Vertical'
$script:LogBox.Anchor = 'Top,Bottom,Left,Right'
$script:Form.Controls.Add($script:LogBox)

function Write-RelayLog {
    param([string]$Text)
    $line = '[' + (Get-Date).ToString('HH:mm:ss') + '] ' + $Text
    $script:LogBox.AppendText($line + [Environment]::NewLine)
    if ($script:LogBox.Lines.Count -gt 250) { $script:LogBox.Lines = @($script:LogBox.Lines | Select-Object -Last 200) }
    [IO.File]::AppendAllText($script:LogPath, $line + [Environment]::NewLine, (New-Object Text.UTF8Encoding($false)))
}
function Persist-RelayState { Save-RelayJson $script:State $script:StatePath }
function Pause-Relay {
    param([string]$Reason)
    $script:State.Paused = $true; $script:State.LastNotice = $Reason
    Persist-RelayState; Write-RelayLog $Reason
}
function Binding-ForRole {
    param([string]$Role)
    return $(if ($Role -eq 'claude') { $script:Config.Claude } else { $script:Config.Codex })
}
function Typed-Binding {
    param($Stored, [string]$Role)
    if ($null -eq $Stored) { throw "$Role 입력창을 먼저 연결하세요." }
    # Prevent an accidental connection to an editor, terminal, or other messaging app.
    if ($Stored.ProcessName -notmatch [regex]::Escape($Role) -and
        $Stored.Executable -notmatch [regex]::Escape($Role)) {
        throw "$Role 앱으로 확인할 수 없는 창입니다. 해당 데스크톱 앱의 입력창을 연결하세요."
    }
    $binding = New-Object AuroraReviewRelay.Binding
    foreach ($name in @('Handle','Title','ClassName','ProcessName','Executable','XRatio','BottomOffset','Dpi','CoordinateOnly','CtrlEnter')) {
        $binding.$name = $Stored.$name
    }
    return $binding
}
function Update-RelayUi {
    foreach ($role in @('claude','codex')) {
        $stored = Binding-ForRole $role
        $label = $(if ($role -eq 'claude') { $script:ClaudeLabel } else { $script:CodexLabel })
        if ($null -ne $stored) { $label.Text = "$($stored.ProcessName): $($stored.Title)" }
    }
    $mode = $(if ($script:State.Completed) { '완료' } elseif ($script:State.Paused) { '정지' } else { '실행 중' })
    $script:StatusLabel.Text = "$mode · 총 $($script:State.TotalCount)건 · 대기 $(@($script:State.Queue).Count)건`r`n$($script:State.LastNotice)"
    $uncertain = $null -ne $script:State.Pending -and $script:State.Pending.Phase -eq 'uncertain'
    $script:SentButton.Enabled = $uncertain; $script:RetryButton.Enabled = $uncertain
    $script:RunButton.Enabled = -not $uncertain -and -not $script:State.Completed
}
function Start-Relay {
    $null = Typed-Binding $script:Config.Claude 'claude'
    $null = Typed-Binding $script:Config.Codex 'codex'
    $script:Config.PollSeconds = [int]$script:PollNumber.Value
    $script:Config.MaxDeliveries = [int]$script:SendNumber.Value
    $script:Config.MaxMinutes = [int]$script:MinuteNumber.Value
    $script:Config.KickoffLatest = $script:KickoffCheck.Checked
    Save-RelayJson $script:Config $script:ConfigPath
    Start-RelayRun $script:State
    Persist-RelayState; Write-RelayLog '시작했습니다. Ctrl+Alt+F8로 정지할 수 있습니다.'
    $script:NextFetch = [datetime]::UtcNow
}
function Begin-Capture {
    param([string]$Role)
    Pause-Relay '입력 위치를 연결하는 동안 전송을 정지합니다.'
    $script:CaptureRole = $Role; $script:CaptureAt = [datetime]::UtcNow.AddSeconds(4)
    $script:State.LastNotice = "$Role 앱의 빈 입력창 위로 마우스를 옮기세요. 4초 뒤 위치를 읽습니다."
}
function Preview-Binding {
    param([string]$Role)
    Pause-Relay '입력 위치 확인 중입니다. 메시지를 입력하지 않습니다.'
    $binding = Typed-Binding (Binding-ForRole $Role) $Role
    $null = [AuroraReviewRelay.Native]::Prepare($binding)
    Write-RelayLog "$Role 입력창 포커스를 확인했습니다. 시작 버튼으로 재개하세요."
}
function Ui-Action {
    param([scriptblock]$Action)
    try { & $Action } catch {
        $script:State.Paused = $true; $script:State.LastNotice = $_.Exception.Message
        Write-RelayLog $_.Exception.Message
        try { Persist-RelayState } catch { }
    }
    Update-RelayUi
}
$captureClaude.Add_Click({ Ui-Action { Begin-Capture 'claude' } })
$captureCodex.Add_Click({ Ui-Action { Begin-Capture 'codex' } })
$previewClaude.Add_Click({ Ui-Action { Preview-Binding 'claude' } })
$previewCodex.Add_Click({ Ui-Action { Preview-Binding 'codex' } })
$script:RunButton.Add_Click({ Ui-Action { Start-Relay } })
$pauseButton.Add_Click({ Ui-Action { Pause-Relay '사용자가 일시정지했습니다.' } })
$script:SentButton.Add_Click({ Ui-Action {
    Resolve-RelayDelivery $script:State 'sent'; Persist-RelayState
    Write-RelayLog '사용자가 앱에서 전송된 것을 확인했습니다.'
} })
$script:RetryButton.Add_Click({ Ui-Action {
    $answer = [Windows.Forms.MessageBox]::Show(
        '앱에 아직 메시지가 전송되지 않았고 입력창을 비웠나요? 이미 보냈다면 중복 전송될 수 있습니다.',
        '재전송 확인', 'YesNo', 'Warning')
    if ($answer -eq [Windows.Forms.DialogResult]::Yes) {
        Resolve-RelayDelivery $script:State 'retry'; Persist-RelayState
    }
} })
$script:Form.Add_ToggleRequested({ Ui-Action {
    if ($script:State.Paused) { Start-Relay } else { Pause-Relay '단축키로 일시정지했습니다.' }
} })
$script:Form.Add_Shown({
    if (-not $script:Form.HotkeyAvailable) {
        Write-RelayLog 'Ctrl+Alt+F8이 다른 프로그램과 겹칩니다. 일시정지 버튼을 사용하세요.'
    }
})

$script:FetchJob = $null; $script:NextFetch = [datetime]::UtcNow
$script:FetchErrors = 0; $script:CaptureRole = ''; $script:CaptureAt = [datetime]::UtcNow
$script:TimerBusy = $false
$timer = New-Object Windows.Forms.Timer
$timer.Interval = 400
$timer.Add_Tick({
    if ($script:TimerBusy) { return }
    $script:TimerBusy = $true
    try {
        if ($script:CaptureRole -and [datetime]::UtcNow -ge $script:CaptureAt) {
            $role = $script:CaptureRole; $script:CaptureRole = ''
            $binding = [AuroraReviewRelay.Native]::Capture($script:CoordinateCheck.Checked,
                                                         $script:CtrlEnterCheck.Checked, $script:Form.Handle)
            $null = Typed-Binding $binding $role
            $other = Binding-ForRole $(if ($role -eq 'claude') { 'codex' } else { 'claude' })
            if ($null -ne $other -and $other.Handle -eq $binding.Handle) {
                throw 'Claude와 Codex를 같은 창에 연결할 수 없습니다.'
            }
            if ($role -eq 'claude') { $script:Config.Claude = $binding } else { $script:Config.Codex = $binding }
            Save-RelayJson $script:Config $script:ConfigPath
            $script:State.LastNotice = "$role 입력 위치를 저장했습니다. 입력 위치 확인 버튼으로 확인하세요."
            Write-RelayLog $script:State.LastNotice
        }
        if ($null -ne $script:FetchJob -and $script:FetchJob.State -in @('Completed','Failed','Stopped')) {
            $job = $script:FetchJob; $script:FetchJob = $null
            try {
                $received = @(Receive-Job $job -ErrorAction Stop)
                if ($received.Count -ne 1) { throw '교환함 결과 형식이 올바르지 않습니다.' }
                $data = $received[0]
                if ($data.Changed) {
                    if (-not $script:State.Initialized) {
                        Initialize-RelayBaseline $script:State @($data.Messages) $script:Config.KickoffLatest
                    } else { Add-RelayMessages $script:State @($data.Messages) }
                    Write-RelayLog "새 교환함 커밋 $($data.Commit.Substring(0, 8)) 확인"
                }
                $script:State.LastCommit = $data.Commit
                Persist-RelayState; $script:FetchErrors = 0
            } catch {
                $script:FetchErrors++
                Write-RelayLog "교환함 읽기 실패 $($script:FetchErrors)회: $($_.Exception.Message)"
                if ($script:FetchErrors -ge 3) { Pause-Relay '연속 읽기 실패로 정지했습니다. 네트워크/로그 확인 후 재개하세요.' }
            } finally { Remove-Job $job -Force -ErrorAction SilentlyContinue }
        }
        if (-not $script:State.Paused -and -not $script:State.Completed) {
            $limit = Test-RelayBudget $script:State $script:Config.MaxDeliveries $script:Config.MaxMinutes
            if ($limit) { Pause-Relay $limit }
        }
        if (-not $script:State.Paused -and -not $script:State.Completed) {
            if ($null -eq $script:FetchJob -and [datetime]::UtcNow -ge $script:NextFetch) {
                $script:FetchJob = Start-Job -FilePath (Join-Path $PSScriptRoot 'Read-Mailbox.ps1') -ArgumentList @(
                    (Join-Path $DataDirectory 'mailbox-cache'), $script:GitPath, $script:State.LastCommit)
                $script:NextFetch = [datetime]::UtcNow.AddSeconds($script:Config.PollSeconds)
            }
            $pending = Take-RelayJob $script:State
            if ($null -ne $pending -and [AuroraReviewRelay.Native]::IdleMilliseconds() -ge 3000) {
                Persist-RelayState
                $binding = Typed-Binding (Binding-ForRole $pending.Target) $pending.Target
                $prepared = [AuroraReviewRelay.Native]::Prepare($binding)
                Set-RelaySending $script:State
                Persist-RelayState # Durable intent BEFORE any text or Enter key.
                try {
                    $result = [AuroraReviewRelay.Native]::Deliver($prepared, $pending.Prompt)
                    Complete-RelayDelivery $script:State
                    Persist-RelayState
                    Write-RelayLog "$($pending.Target)에 $($pending.Id) 전달 ($result)"
                } catch {
                    Set-RelayUncertain $script:State $_.Exception.Message
                    Persist-RelayState; Write-RelayLog $script:State.LastNotice
                }
            }
        }
    } catch {
        $script:State.Paused = $true; $script:State.LastNotice = $_.Exception.Message
        # A failed journal write after entering sending is ambiguous, even before typing.
        if ($null -ne $script:State.Pending -and $script:State.Pending.Phase -eq 'sending') {
            Set-RelayUncertain $script:State $_.Exception.Message
        }
        try { Persist-RelayState } catch { }
        Write-RelayLog $script:State.LastNotice
    } finally { Update-RelayUi; $script:TimerBusy = $false }
})
$script:Form.Add_FormClosing({
    $timer.Stop(); $script:State.Paused = $true
    try { Persist-RelayState } catch { }
    if ($null -ne $script:FetchJob) {
        Stop-Job $script:FetchJob -ErrorAction SilentlyContinue
        Remove-Job $script:FetchJob -Force -ErrorAction SilentlyContinue
    }
})
try {
    Update-RelayUi
    Write-RelayLog "시작 준비. 상태와 로그: $DataDirectory"
    $timer.Start()
    [Windows.Forms.Application]::Run($script:Form)
} finally {
    $timer.Dispose(); $script:Form.Dispose()
    $mutex.ReleaseMutex(); $mutex.Dispose()
}

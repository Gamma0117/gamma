param([string]$CorePath = (Join-Path $PSScriptRoot '..\Relay.Core.psm1'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Import-Module $CorePath -Force -DisableNameChecking
$script:Passed = 0

function Assert {
    param([bool]$Condition, [string]$Reason)
    if (-not $Condition) { throw $Reason }
}
function Throws {
    param([scriptblock]$Action)
    $threw = $false
    try { & $Action | Out-Null } catch { $threw = $true }
    Assert $threw 'Expected the action to reject this input.'
}
function Case {
    param([string]$Name, [scriptblock]$Action)
    & $Action
    $script:Passed++
    Write-Output "PASS $Name"
}
function Envelope {
    param([string]$Id, [string]$Role = 'claude')
    [pscustomobject]@{
        id = $Id; role = $Role; status = 'completed'
        report = "messages/$Role/$Id.md"; work_scope = 'P0-6'
        stage = 'implementation'; stop_cycle = $false; needs_user = $false
    }
}
function Ready {
    $state = New-RelayState
    Initialize-RelayBaseline $state @() $false
    Start-RelayRun $state ([datetime]'2026-10-01T00:00:00Z')
    return $state
}

Case 'Native arguments preserve Windows paths and do not become shell code' {
    Assert ((ConvertTo-RelayNativeArgument 'C:\path with space\') -ceq '"C:\path with space\\"') 'Trailing path slash escaped the closing quote.'
    Assert ((ConvertTo-RelayNativeArgument 'a"b') -ceq '"a\"b"') 'Embedded quote was not escaped.'
    Assert ((ConvertTo-RelayNativeArgument '') -ceq '""') 'Empty argument disappeared.'
    Assert ((ConvertTo-RelayNativeArgument '$(echo 한글)&') -ceq '"$(echo 한글)&"') 'Literal report characters were changed.'
}

Case 'Routes complete reports to the other role without embedding report text' {
    $fromClaude = New-RelayJob (Envelope 'claude-1')
    $fromCodex = New-RelayJob (Envelope 'codex-1' 'codex')
    Assert ($fromClaude.Target -eq 'codex' -and $fromCodex.Target -eq 'claude') 'Wrong recipient.'
    Assert ($fromClaude.Prompt.Contains('FETCH_HEAD:messages/claude/claude-1.md')) 'Report pointer missing.'
    Assert ($fromClaude.Prompt.Contains('reply_to') -and $fromClaude.Prompt.Contains('claude-1')) 'Reply context lost.'
    Assert ($fromCodex.Prompt.Contains('P0-6')) 'Scope missing.'
}

Case 'Rejects command-like IDs and mismatched paths before any work' {
    foreach ($id in @('../parent', 'a/b', 'a:b', 'a b', 'a$(x)', 'a`x', '', ('x' * 129))) {
        $message = Envelope $id
        Throws { Test-RelayEnvelope $message }
    }
    $message = Envelope 'safe'
    $message.report = 'messages/codex/safe.md'
    Throws { Test-RelayEnvelope $message }
    $message = Envelope 'safe'
    $message.role = 'other'
    Throws { Test-RelayEnvelope $message }
    $message = Envelope 'safe'
    $message.status = 'go'
    Throws { Test-RelayEnvelope $message }
}

Case 'Preserves legacy envelopes but rejects scope and boolean confusion' {
    $old = [pscustomobject]@{ id = 'old'; role = 'codex'; status = 'technical_pass'; report = 'messages/codex/old.md' }
    Assert (Test-RelayEnvelope $old) 'Existing messages stopped working.'
    foreach ($field in @('stop_cycle', 'needs_user')) {
        $message = Envelope 'bad'
        $message.$field = 'false'
        Throws { Test-RelayEnvelope $message }
    }
    $message = Envelope 'bad'; $message.work_scope = 'P0-7'
    Throws { Test-RelayEnvelope $message }
    $message = Envelope 'bad'; $message.stage = 'next'
    Throws { Test-RelayEnvelope $message }
}

Case 'First launch suppresses history and queues only the newest report to the other app' {
    $history = @((Envelope 'old-codex' 'codex'), (Envelope 'old-claude'), (Envelope 'new-codex' 'codex'))
    $state = New-RelayState
    Initialize-RelayBaseline $state $history $true
    Assert ($state.KnownIds.Count -eq 3 -and $state.Queue.Count -eq 1) 'Old messages replayed.'
    Assert ($state.Queue[0].Id -eq 'new-codex') 'Did not use Git arrival order.'
    Add-RelayMessages $state $history
    Assert ($state.Queue.Count -eq 1) 'First poll duplicates were added.'
    $withoutKickoff = New-RelayState
    Initialize-RelayBaseline $withoutKickoff $history $false
    Assert ($withoutKickoff.Queue.Count -eq 0) 'Kickoff switch ignored.'
    $withNewCompletion = New-RelayState
    Initialize-RelayBaseline $withNewCompletion ($history + @((Envelope 'new-completion'))) $true
    Assert ($withNewCompletion.Queue[0].Target -eq 'codex') 'A just-completed Claude report was discarded on first launch.'
    Assert ($withNewCompletion.Queue[0].Id -eq 'new-completion') 'Old Codex feedback was replayed over a new completion.'
    Throws { Initialize-RelayBaseline $state $history }
}

Case 'Repeated polls and multiple arrivals preserve order without duplicate delivery' {
    $state = Ready
    $messages = @((Envelope 'one'), (Envelope 'two' 'codex'), (Envelope 'three'))
    Add-RelayMessages $state $messages
    Add-RelayMessages $state $messages
    Assert ($state.Queue.Count -eq 3) 'Duplicate poll queued duplicates.'
    foreach ($id in @('one', 'two', 'three')) {
        $job = Take-RelayJob $state
        Assert ($job.Id -eq $id) 'Arrival order changed.'
        Assert ((Take-RelayJob $state).Id -eq $id) 'Prepared job got replaced.'
        Set-RelaySending $state
        Complete-RelayDelivery $state
    }
    Assert ($state.Delivered.Count -eq 3 -and $state.TotalCount -eq 3) 'Delivery journal is incomplete.'
    Assert ($null -eq (Take-RelayJob $state)) 'A consumed notification replayed.'
}

Case 'Invalid batch leaves receipt state untouched' {
    $state = Ready
    $bad = Envelope 'bad'; $bad.report = 'outside.md'
    Throws { Add-RelayMessages $state @((Envelope 'good'), $bad) }
    Assert ($state.Queue.Count -eq 0 -and $state.KnownIds.Count -eq 0) 'Part of an invalid batch was consumed.'
    $state = New-RelayState
    Throws { Initialize-RelayBaseline $state @((Envelope 'good'), $bad) }
    Assert (-not $state.Initialized -and $state.KnownIds.Count -eq 0) 'Invalid baseline mutated state.'
}

Case 'Restart after sending is uncertain and never resends automatically' {
    $state = Ready
    Add-RelayMessages $state @((Envelope 'ambiguous'))
    $null = Take-RelayJob $state
    Set-RelaySending $state
    # Actual JSON round trip models process termination, including enum/array serialization.
    $restored = Restore-RelayState (($state | ConvertTo-Json -Depth 20) | ConvertFrom-Json)
    Assert ($restored.Paused -and $restored.Pending.Phase -eq 'uncertain') 'An unfinished send was treated as safe.'
    Assert ($null -eq (Take-RelayJob $restored)) 'Paused relay sent on restart.'
    Throws { Start-RelayRun $restored }
    Resolve-RelayDelivery $restored 'sent'
    Assert ($restored.Paused -and $restored.TotalCount -eq 1) 'Manual sent confirmation not recorded.'
    Start-RelayRun $restored
    Add-RelayMessages $restored @((Envelope 'ambiguous'))
    Assert ($null -eq (Take-RelayJob $restored)) 'Confirmed message replayed.'
}

Case 'Explicit retry is possible only from an uncertain delivery' {
    $state = Ready
    Add-RelayMessages $state @((Envelope 'retry'))
    $null = Take-RelayJob $state
    Throws { Resolve-RelayDelivery $state 'retry' }
    Set-RelaySending $state
    Set-RelayUncertain $state 'Enter confirmation missing'
    Resolve-RelayDelivery $state 'retry'
    Assert ($state.Pending.Phase -eq 'prepared' -and $state.Paused) 'Retry started without a user restart.'
    Start-RelayRun $state
    Assert ((Take-RelayJob $state).Id -eq 'retry') 'Retry lost its original ID.'
    Set-RelaySending $state; Complete-RelayDelivery $state
    Assert ($state.TotalCount -eq 1) 'One retry counted as multiple deliveries.'
}

Case 'A preparation failure and a clean restart preserve the pending notification' {
    $state = Ready
    Add-RelayMessages $state @((Envelope 'prepared'))
    $null = Take-RelayJob $state
    $state.Paused = $true
    $restored = Restore-RelayState (($state | ConvertTo-Json -Depth 20) | ConvertFrom-Json)
    Assert ($restored.Pending.Phase -eq 'prepared' -and $restored.Paused) 'Preparation failure became a sent message.'
    Start-RelayRun $restored
    Assert ((Take-RelayJob $restored).Id -eq 'prepared') 'Safe pending work disappeared.'
    Throws { Complete-RelayDelivery $restored }
}

Case 'Count and elapsed-time limits stop further work at their boundaries' {
    $state = Ready
    Assert (-not (Test-RelayBudget $state 2 10 ([datetime]'2026-10-01T00:09:59Z'))) 'Stopped too soon.'
    Assert ([bool](Test-RelayBudget $state 2 10 ([datetime]'2026-10-01T00:10:00Z'))) 'Time limit ignored.'
    $state.RunCount = 2
    Assert ([bool](Test-RelayBudget $state 2 10 ([datetime]'2026-10-01T00:00:00Z'))) 'Count limit ignored.'
    Start-RelayRun $state ([datetime]'2026-10-01T00:11:00Z')
    Assert ($state.RunCount -eq 0) 'Manual continuation did not get a fresh budget.'
}

Case 'Only Codex implementation technical pass can finish the cycle' {
    foreach ($role in @('claude', 'codex')) {
        $bad = Envelope 'bad-stop' $role
        $bad.stop_cycle = $true
        Throws { Test-RelayEnvelope $bad }
    }
    $plan = Envelope 'plan-stop' 'codex'; $plan.status = 'technical_pass'; $plan.stage = 'plan'; $plan.stop_cycle = $true
    Throws { Test-RelayEnvelope $plan }
    $state = Ready
    $plan.stop_cycle = $false
    Add-RelayMessages $state @($plan)
    $null = Take-RelayJob $state; Set-RelaySending $state; Complete-RelayDelivery $state
    Assert (-not $state.Completed) 'Plan review ended implementation.'
    $final = Envelope 'final' 'codex'; $final.status = 'technical_pass'; $final.stop_cycle = $true
    Add-RelayMessages $state @($final)
    $job = Take-RelayJob $state
    Assert ($job.Target -eq 'claude' -and $job.Terminal) 'Final result was not routed back to Claude.'
    Set-RelaySending $state; Complete-RelayDelivery $state
    Assert ($state.Completed -and $state.Paused) 'Final delivery did not stop the loop.'
    Throws { Start-RelayRun $state }
    Add-RelayMessages $state @((Envelope 'after-final'))
    Assert ($state.Queue.Count -eq 0) 'Next phase started without authorization.'
}

Case 'A needs-user report is delivered once and pauses the relay' {
    $state = Ready
    $message = Envelope 'question'; $message.needs_user = $true
    Add-RelayMessages $state @($message, (Envelope 'later'))
    $job = Take-RelayJob $state
    Assert ($job.Prompt.Contains('의존 작업을 멈추세요')) 'Prompt treated a requested user decision as approval.'
    Set-RelaySending $state; Complete-RelayDelivery $state
    Assert ($state.Paused -and $state.TotalCount -eq 1 -and $state.Queue.Count -eq 1) 'User decision did not pause the loop.'
    Assert ($null -eq (Take-RelayJob $state)) 'Later message was sent before the user decision.'
}

Case 'UTF-8 atomic state saves preserve pending phases and a previous backup' {
    $directory = Join-Path ([IO.Path]::GetTempPath()) ('aurora-relay-test-' + [guid]::NewGuid().ToString('N'))
    try {
        $path = Join-Path $directory 'space folder\state.json'
        $state = Ready
        $state.LastNotice = '한글 보고서 그대로'
        Add-RelayMessages $state @((Envelope 'saved'))
        $null = Take-RelayJob $state
        Save-RelayJson $state $path
        Set-RelaySending $state
        Save-RelayJson $state $path
        $current = [IO.File]::ReadAllText($path) | ConvertFrom-Json
        $backup = [IO.File]::ReadAllText($path + '.bak') | ConvertFrom-Json
        Assert ($current.Pending.Phase -eq 'sending' -and $backup.Pending.Phase -eq 'prepared') 'Atomic replacement lost its journal.'
        Assert ($current.LastNotice -eq '한글 보고서 그대로') 'UTF-8 text was damaged.'
        Assert (@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($path)) -Filter '*.tmp').Count -eq 0) 'Temporary files leaked.'
        $restored = Restore-RelayState $current
        Assert ($restored.Pending.Phase -eq 'uncertain') 'Durable intent was not recovered safely.'
    } finally {
        if (Test-Path -LiteralPath $directory) { Remove-Item -LiteralPath $directory -Recurse -Force }
    }
}

Write-Output "All $script:Passed relay cases passed. No Windows keyboard input was simulated by these tests."

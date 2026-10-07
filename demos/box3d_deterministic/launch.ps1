param([switch]$Validate, [switch]$RenderClients, [ValidateSet('None','Hash')][string]$Fault = 'None', [string]$Engine)
$ErrorActionPreference = 'Stop'
$projectPath = $PSScriptRoot
$repoPath = Split-Path (Split-Path $projectPath -Parent) -Parent
if (-not $Engine) { $Engine = Join-Path $repoPath 'bin/godot.windows.editor.dev.x86_64.mono.exe' }
$Engine = (Resolve-Path -LiteralPath $Engine).Path
$runPath = Join-Path $repoPath ('.build/deterministic-demo/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runPath -Force | Out-Null
$ownedProcesses = @()
function Get-RuntimeSourceHashes {
    $hashes = [ordered]@{}
    Get-ChildItem -LiteralPath $projectPath -Recurse -File | Where-Object {
        $_.FullName -notlike '*\.godot\*' -and $_.Extension -in @('.gd', '.tscn', '.godot', '.ps1')
    } | Sort-Object FullName | ForEach-Object {
        $hashes[$_.FullName.Substring($projectPath.Length + 1)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    return $hashes
}
# Capture executable inputs before any participant reads its scene or scripts.
$sourceHashes = Get-RuntimeSourceHashes
function Start-Demo([string]$Name, [string[]]$Options, [bool]$Visible) {
    $windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
    $startedProcess = Start-Process -FilePath $Engine -ArgumentList (@('--path', ('"' + $projectPath + '"'), '--max-fps', '60') + $Options) -WindowStyle $windowStyle -PassThru -RedirectStandardOutput (Join-Path $runPath "$Name.log") -RedirectStandardError (Join-Path $runPath "$Name-error.log")
    $null = $startedProcess.Handle
    return $startedProcess
}
try {
    $serverOptions = @('--headless', '--', '--role=server', ('--runtime="' + $runPath + '"'))
    if ($Validate) { $serverOptions += '--duration=26' }
    $server = Start-Demo 'server' $serverOptions $false
    $ownedProcesses += $server
    $deadline = (Get-Date).AddSeconds(12)
    while (-not (Test-Path -LiteralPath (Join-Path $runPath 'player2.token'))) {
        $server.Refresh()
        if ($server.HasExited -or (Get-Date) -gt $deadline) { throw "Deterministic server startup failed: $runPath" }
        Start-Sleep -Milliseconds 100
    }
    foreach ($index in 1..2) {
        $visible = (-not $Validate) -or $RenderClients
        $options = @()
        if (-not $visible) { $options += '--headless' }
        else { $options += @('--position', $(if ($index -eq 1) { '30,70' } else { '960,70' })) }
        $options += @('--', '--role=client', "--player=$index", ('--runtime="' + $runPath + '"'))
        if ($visible) { $options += ('--capture="' + (Join-Path $runPath "client$index.png") + '"') }
        if ($Validate) { $options += '--duration=21' }
        if ($Fault -eq 'Hash') { $options += '--fault=hash' }
        $ownedProcesses += Start-Demo "client$index" $options $visible
    }
    $manifest = [ordered]@{ project=$projectPath; runtime=$runPath; engine=$Engine; engine_sha256=(Get-FileHash -LiteralPath $Engine -Algorithm SHA256).Hash; pids=@($ownedProcesses | ForEach-Object Id); validation=[bool]$Validate; fault=$Fault; source_sha256=$sourceHashes }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runPath 'launch.json')
    if ($Validate) {
        if ($Fault -eq 'Hash') {
            if (-not $ownedProcesses[2].WaitForExit(15000) -or $ownedProcesses[2].ExitCode -eq 0) { throw 'Corrupt authoritative hash was not rejected.' }
            $state = Get-Content (Join-Path $runPath 'client2-status.json') -Raw | ConvertFrom-Json
            if (-not $state.fault_injected -or $state.hash_failures -ne 1) { throw 'Expected deterministic mismatch evidence missing.' }
            if (($sourceHashes | ConvertTo-Json -Compress) -ne ((Get-RuntimeSourceHashes) | ConvertTo-Json -Compress)) { throw 'Runtime source changed during validation; rerun with frozen inputs.' }
            [ordered]@{passed=$true; scope='Deliberate authoritative hash corruption fails closed'; client=$state; launch=$manifest} | ConvertTo-Json -Depth 7 | Set-Content (Join-Path $runPath 'validation.json')
            foreach ($process in $ownedProcesses) { $process.Refresh(); if (-not $process.HasExited) { Stop-Process -Id $process.Id } }
        } else {
            foreach ($process in $ownedProcesses) {
                if (-not $process.WaitForExit(40000) -or $process.ExitCode -ne 0) { throw "Deterministic process failed: $runPath" }
            }
            $states = @(1..2 | ForEach-Object { Get-Content (Join-Path $runPath "client$_-status.json") -Raw | ConvertFrom-Json })
            foreach ($state in $states) {
                if ($state.verified_ticks -lt 900 -or $state.corrections -lt 5 -or $state.replayed_ticks -lt 20 -or $state.hash_failures -ne 0 -or $state.history_peak_bytes -gt 33554432) { throw "Deterministic replay gates failed: $($state | ConvertTo-Json -Compress)" }
            }
            if (@(Get-ChildItem $runPath -Filter '*error.log' | Where-Object Length -gt 0).Count -gt 0) { throw 'Unexpected deterministic runtime errors.' }
            $serverState = Get-Content (Join-Path $runPath 'server1-status.json') -Raw | ConvertFrom-Json
            foreach ($state in $states) {
                if ($serverState.hashes.([string]$state.acknowledged_tick) -ne $state.acknowledged_hash) { throw 'Final acknowledged hash does not match the server journal.' }
            }
            if (($sourceHashes | ConvertTo-Json -Compress) -ne ((Get-RuntimeSourceHashes) | ConvertTo-Json -Compress)) { throw 'Runtime source changed during validation; rerun with frozen inputs.' }
            [ordered]@{passed=$true; scope='Independent server and two clients: local prediction, solver rollback, canonical input replay and per-tick state hashes under 120ms delay, 40ms jitter and 5% loss each direction'; server=$serverState; clients=$states; launch=$manifest} | ConvertTo-Json -Depth 7 | Set-Content (Join-Path $runPath 'validation.json')
        }
        Write-Output "DETERMINISTIC_VALIDATION_PASS $runPath"
    } else { Write-Output "DETERMINISTIC_LAUNCHED $runPath" }
} catch {
    foreach ($process in $ownedProcesses) { $process.Refresh(); if (-not $process.HasExited) { Stop-Process -Id $process.Id } }
    throw
}

param([switch]$Validate, [switch]$RenderClients, [string]$Engine, [ValidateSet('Clean','Broadband','RoughWifi')][string]$Network = 'Broadband', [ValidateRange(0,192)][int]$StressCount = 32, [ValidateRange(0,50)][int]$HeadlessClients = 0)
$ErrorActionPreference = 'Stop'
$projectPath = $PSScriptRoot
$repoPath = Split-Path (Split-Path $projectPath -Parent) -Parent
if (-not $Engine) { $Engine = Join-Path $repoPath 'bin/godot.windows.editor.dev.x86_64.mono.exe' }
$Engine = (Resolve-Path -LiteralPath $Engine).Path
$runPath = Join-Path $repoPath ('.build/arena-demo/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
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
$impairment = switch ($Network) {
    'Clean' { @('--latency=0', '--jitter=0', '--loss=0') }
    'Broadband' { @('--latency=70', '--jitter=20', '--loss=2') }
    'RoughWifi' { @('--latency=120', '--jitter=40', '--loss=5') }
}
$totalClients = 2 + $HeadlessClients
$impairment += @("--stress=$StressCount", "--players=$totalClients")
function Start-ArenaProcess([string]$Name, [string[]]$Options, [bool]$Visible) {
    $frameLimit = if ($Name -like 'client*' -and -not $Visible) { '20' } else { '60' }
    $launchArguments = @('--path', ('"' + $projectPath + '"'), '--max-fps', $frameLimit) + $Options
    $windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
    $startedProcess = Start-Process -FilePath $Engine -ArgumentList $launchArguments -WindowStyle $windowStyle -PassThru -RedirectStandardOutput (Join-Path $runPath "$Name.log") -RedirectStandardError (Join-Path $runPath "$Name-error.log")
    # Retain the native handle before the process exits so .NET preserves ExitCode.
    $null = $startedProcess.Handle
    if ($Name -eq 'server') { $startedProcess.PriorityClass = 'AboveNormal' }
    else { $startedProcess.PriorityClass = 'Normal' }
    return $startedProcess
}
try {
    $serverOptions = @('--headless', '--', '--role=server', ('--runtime="' + $runPath + '"')) + $impairment
    $validationSeconds = if ($HeadlessClients -gt 0) { 110 } else { 34 }
    if ($Validate) { $serverOptions += "--duration=$validationSeconds" }
    $server = Start-ArenaProcess 'server' $serverOptions $false
    $ownedProcesses += $server
    $deadline = (Get-Date).AddSeconds(12)
    while (-not (Test-Path -LiteralPath (Join-Path $runPath "player$totalClients.token"))) {
        $server.Refresh()
        if ($server.HasExited -or (Get-Date) -gt $deadline) { throw "Dedicated server startup failed. See $runPath" }
        Start-Sleep -Milliseconds 100
    }
    foreach ($index in 1..$totalClients) {
        $visible = ($index -le 2) -and ((-not $Validate) -or $RenderClients)
        $clientOptions = @()
        if (-not $visible) { $clientOptions += '--headless' }
        else { $clientOptions += @('--resolution', '900x640', '--position', $(if ($index -eq 1) { '30,70' } else { '960,70' })) }
        $clientOptions += @('--', '--role=client', "--player=$index", ('--runtime="' + $runPath + '"'))
        $clientOptions += "--bind=127.0.0.$($index + 1)"
        $clientOptions += $impairment
        if ($Validate) { $clientOptions += "--duration=$($validationSeconds - $(if ($HeadlessClients -gt 0) { 25 } else { 10 }))" }
        elseif (-not $visible) { $clientOptions += '--duration=600' }
        if ($visible) { $clientOptions += ('--capture="' + (Join-Path $runPath "client$index.png") + '"') }
        $ownedProcesses += Start-ArenaProcess "client$index" $clientOptions $visible
        if ($HeadlessClients -gt 0) { Start-Sleep -Milliseconds 150 }
    }
    $manifest = [ordered]@{ project = $projectPath; engine = $Engine; engine_sha256 = (Get-FileHash -LiteralPath $Engine -Algorithm SHA256).Hash; runtime = $runPath; validation = [bool]$Validate; network_profile = $Network; impairment_arguments = $impairment; pids = @($ownedProcesses | ForEach-Object { $_.Id }) }
    $manifest.source_sha256 = $sourceHashes
    $manifest.stress_bodies = $StressCount
    $manifest.total_clients = $totalClients
    $manifest.client_endpoints = @(1..$totalClients | ForEach-Object { [ordered]@{ player = $_; client_id = 1000 + $_; bind_ip = "127.0.0.$($_ + 1)"; pid = $ownedProcesses[$_].Id; rendered = ($_ -le 2 -and ((-not $Validate) -or $RenderClients)) } })
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runPath 'launch.json')
    $endpointDeadline = (Get-Date).AddSeconds(10)
    do {
        $udpEndpoints = @(Get-NetUDPEndpoint | Where-Object { $_.OwningProcess -in $manifest.pids } | Select-Object OwningProcess, LocalAddress, LocalPort)
        $verifiedClients = @($manifest.client_endpoints | Where-Object {
            $expected = $_
            @($udpEndpoints | Where-Object { $_.OwningProcess -eq $expected.pid -and $_.LocalAddress -eq $expected.bind_ip }).Count -eq 1
        })
        if ($verifiedClients.Count -eq $totalClients) { break }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $endpointDeadline)
    $udpEndpoints | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $runPath 'udp-endpoints.json')
    if ($verifiedClients.Count -ne $totalClients) { throw 'Independent UDP endpoint verification failed.' }
    if ($Validate) {
        foreach ($process in $ownedProcesses) {
            if (-not $process.WaitForExit(($validationSeconds + 20) * 1000)) { throw 'Demo validation exceeded watchdog.' }
            if ($process.ExitCode -ne 0) { throw "Demo process $($process.Id) exited $($process.ExitCode)." }
        }
        $serverState = Get-Content -Raw -LiteralPath (Join-Path $runPath 'server1-status.json') | ConvertFrom-Json
        if ($serverState.checkpoint_restores -lt 1 -or $serverState.tick -lt 120) { throw 'Authoritative checkpoint recovery did not execute.' }
        if ($serverState.ai -ne 6 -or $serverState.ai_attacks -lt 1 -or $serverState.pulses -lt 2 -or $serverState.spawned_props -lt 2) { throw 'AI, shockwave or prop interaction failed.' }
        if ($serverState.backend_verified_steps -lt 500 -or $serverState.backend_hash_mismatches -ne 0) { throw 'Complete-world backend determinism audit failed.' }
        if (-not $serverState.match_state_quiescent -or $serverState.superposition.sent -lt 1 -or $serverState.superposition.dirty_skips -lt 1 -or $serverState.superposition.rejected -ne 0) { throw 'Native Superposition gameplay capture or dirty suppression failed.' }
        if ($serverState.pickups_collected -lt 1) { throw 'Authoritative energy pickup scoring did not execute.' }
        if ($serverState.kinematic_mechanisms -ne 6 -or $serverState.stress_bodies -ne $StressCount -or $serverState.full_load_physics_p95_worst_ms -le 0 -or $serverState.full_load_physics_p95_worst_ms -gt 16.67) { throw 'Physics mechanisms or fixed-step budget under simultaneous client load failed.' }
        if ($serverState.max_peers_seen -ne $totalClients -or $serverState.full_load_seconds -lt $(if ($totalClients -gt 2) { 30 } else { 10 })) { throw 'Required simultaneous client load was not sustained.' }
        $clientStates = @(1..$totalClients | ForEach-Object { Get-Content -Raw -LiteralPath (Join-Path $runPath "client$_-status.json") | ConvertFrom-Json })
        foreach ($state in $clientStates) {
            if ($state.superposition.applied -lt 1 -or $state.superposition.rejected -ne 0) { throw "Client $($state.player) did not apply native Superposition gameplay state." }
            foreach ($property in @('teal_score', 'amber_score', 'pickups_collected', 'checkpoint_restores')) {
                if ($state.match_state.$property -ne $serverState.match_state.$property) { throw "Client $($state.player) Superposition property $property did not converge to authority." }
            }
            if ($state.state -ne 'Connected' -or $state.entities -lt 19 -or $state.fast_pose_updates -lt 1000 -or $state.distance -lt 3 -or $state.measured_rtt_ms -lt 1 -or $state.owned_pose_gap_p95_ms -gt 250) { throw "Client validation failed: $($state | ConvertTo-Json -Compress)" }
            if (($state.player -le 2 -and $state.recoveries -lt 1) -or $state.recovery_attempts -gt 3) { throw 'Client watchdog recovery was not demonstrated.' }
            if ($state.brain_decisions -lt 100 -or $state.bind_ip -ne "127.0.0.$($state.player + 1)") { throw 'Independent tactical player or endpoint validation failed.' }
            if ($RenderClients -and $state.player -le 2) {
                $presentation = $state.owned_presentation
                $samples = $presentation.interpolated_samples + $presentation.extrapolated_samples + $presentation.held_samples + $presentation.priming_samples
                if ($samples -lt 1000 -or ($presentation.held_samples / [double]$samples) -gt 0.05) { throw 'Owned-player presentation exhausted its snapshot buffer too often.' }
            }
        }
        $errors = @(Get-ChildItem -LiteralPath $runPath -Filter '*error.log' | Where-Object { $_.Length -gt 0 })
        if ($errors.Count -gt 0) { throw "Runtime errors recorded in $runPath" }
        if (($sourceHashes | ConvertTo-Json -Compress) -ne ((Get-RuntimeSourceHashes) | ConvertTo-Json -Compress)) { throw 'Runtime source changed during validation; rerun with frozen inputs.' }
        [ordered]@{ passed = $true; scope = "Bounded local integration: $totalClients independent tactical clients, transport impairment, mixed physics, checkpoint restore, native Superposition gameplay convergence and client watchdog recovery."; server = $serverState; clients = $clientStates; launch = $manifest } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runPath 'validation.json')
        Write-Output "ARENA_VALIDATION_PASS $runPath"
    } else {
        Write-Output "ARENA_LAUNCHED $runPath"
        Write-Output ('PIDs: ' + ($manifest.pids -join ', '))
    }
} catch {
    foreach ($process in $ownedProcesses) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    throw
}

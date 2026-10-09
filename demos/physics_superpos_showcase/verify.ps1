$ErrorActionPreference='Stop'
$engineRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$editor=Join-Path $engineRoot 'bin/godot.windows.editor.dev.x86_64.mono.exe'
$out=Join-Path $engineRoot '.build/diagnostics/physics-superpos-showcase'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$previousReShade=$env:DISABLE_VK_LAYER_reshade_1
try {
    $env:DISABLE_VK_LAYER_reshade_1='1'
    foreach($mode in @('headless','render')) {
        $arguments=@('--path',('"'+$PSScriptRoot+'"'),'--max-fps','60')
        if($mode -eq 'headless') { $arguments+= '--headless' }
        $arguments+= @('--','--smoke')
        if($mode -eq 'render') { $arguments+= ('--capture="'+(Join-Path $out 'showcase.png')+'"') }
        $stdout=Join-Path $out ($mode+'.log')
        $stderr=Join-Path $out ($mode+'-error.log')
        $process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null=$process.Handle
        $process.ProcessorAffinity=[IntPtr]15
        if(-not $process.WaitForExit(45000)) { $process.Kill(); throw "$mode watchdog expired" }
        $process.WaitForExit()
        $log=Get-Content -LiteralPath $stdout -Raw
        if($process.ExitCode -ne 0 -or $log -notmatch 'PHYSICS_SUPERPOS_SHOWCASE_PASS') { throw "$mode failed; see $out" }
        Write-Output (($log -split '\r?\n' | Where-Object {$_ -like 'PHYSICS_SUPERPOS_SHOWCASE_PASS*'}) -join '')
    }
    [ordered]@{passed=$true;engine_sha256=(Get-FileHash $editor -Algorithm SHA256).Hash;script_sha256=(Get-FileHash (Join-Path $PSScriptRoot 'showcase.gd') -Algorithm SHA256).Hash;scope='52 bodies; native UDP/DTLS two associations in one process; four commands; trusted local rewind; rendered capture';capture=(Join-Path $out 'showcase.png')} | ConvertTo-Json | Set-Content (Join-Path $out 'receipt.json')
} finally { $env:DISABLE_VK_LAYER_reshade_1=$previousReShade }

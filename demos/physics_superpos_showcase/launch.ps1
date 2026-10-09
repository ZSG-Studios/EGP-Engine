$ErrorActionPreference='Stop'
$engineRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$editor=Join-Path $engineRoot 'bin/godot.windows.editor.dev.x86_64.mono.exe'
if(-not(Test-Path -LiteralPath $editor)){throw 'Build the EGP Mono editor first.'}
$previousReShade=$env:DISABLE_VK_LAYER_reshade_1
try {
    $env:DISABLE_VK_LAYER_reshade_1='1'
    $game=Start-Process -FilePath $editor -ArgumentList '--path',('"'+$PSScriptRoot+'"'),'--max-fps','60' -WorkingDirectory $PSScriptRoot -WindowStyle Normal -PassThru
} finally { $env:DISABLE_VK_LAYER_reshade_1=$previousReShade }
$game.ProcessorAffinity=[IntPtr]15
Write-Output ('Showcase PID: '+$game.Id)

param([int]$Duration=1800,[switch]$RemoteServer)
$ErrorActionPreference='Stop'
$taskArgs=@((Join-Path $PSScriptRoot 'run_lab.py'),'--duration',$Duration)
if (-not $RemoteServer) { $taskArgs+='--local-server' }
& python @taskArgs
exit $LASTEXITCODE

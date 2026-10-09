param([int]$Duration=300)
$ErrorActionPreference='Stop'
& python (Join-Path $PSScriptRoot 'run_lab.py') --duration $Duration
if($LASTEXITCODE -ne 0){throw 'Lab failed. See EGP-Engine/.build/diagnostics/superpos-100.'}

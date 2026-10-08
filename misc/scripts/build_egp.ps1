[CmdletBinding()]
param(
    [ValidateSet('editor', 'template_debug', 'template_release', 'all')][string]$Target = 'editor',
    [ValidateSet('windows', 'linuxbsd', 'macos', 'android', 'ios', 'visionos', 'web')][string]$Platform = 'windows',
    [string]$Arch = 'x86_64',
    [ValidateRange(1, 256)][int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Setup,
    [switch]$SkipManaged,
    [string[]]$XmakeArgs = @()
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$toolRoot = Join-Path $projectRoot '.build/xmake'
$tool = @((Join-Path $toolRoot 'xmake/xmake.exe'), (Join-Path $toolRoot 'xmake.exe')) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $tool) {
    $installed = Get-Command xmake -ErrorAction SilentlyContinue
    if ($installed) { $tool = $installed.Source }
}
if (-not $tool -and $Setup) {
    if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Automatic bootstrap supports Windows x64; install xmake 3.1.1 for this host manually.' }
    New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
    $tool = Join-Path $toolRoot 'xmake.exe'
    $download = Join-Path $toolRoot 'xmake-download.exe'
    Invoke-WebRequest 'https://github.com/xmake-io/xmake/releases/download/v3.1.1/xmake-bundle-v3.1.1.win64.exe' -OutFile $download
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne '5DE3D5167A8B5E8AD95AF2FBA9AB5A62A626D7E789350D0AFFC33AF450C4A9AD') { throw 'Pinned xmake download checksum mismatch.' }
    Move-Item -LiteralPath $download -Destination $tool
}
if (-not $tool) { throw 'Run build_egp.ps1 -Setup or install xmake 3.1.1.' }
$version = & $tool --version
if ($LASTEXITCODE -ne 0 -or ($version -join "`n") -notmatch 'xmake v3\.1\.1') { throw 'EGP requires xmake 3.1.1.' }
if ($Setup) { Write-Host "xmake 3.1.1 ready: $tool"; return }
$previousXmake = $env:XMAKE_EXE
Push-Location $projectRoot
try {
    $env:XMAKE_EXE = $tool
    $targets = if ($Target -eq 'all') { @('editor', 'template_debug', 'template_release') } else { @($Target) }
    foreach ($buildTarget in $targets) {
        $options = @("arch=$Arch", 'module_mono_enabled=yes', 'angle=no', 'accesskit=no', 'd3d12=no')
        if ($buildTarget -eq 'editor') { $options += 'dev_build=yes' }
        $options += $XmakeArgs
        $optionJson = ConvertTo-Json -InputObject @($options) -Compress
        $resultRoot = Join-Path $projectRoot '.build/xmake-invocation'
        New-Item -ItemType Directory -Force -Path $resultRoot | Out-Null
        $resultPath = Join-Path $resultRoot ([Guid]::NewGuid().ToString() + '.json')
        & $tool lua (Join-Path $PSScriptRoot 'build_egp.lua') $Platform $buildTarget $Jobs (Join-Path $projectRoot '.build/xmake-cache') $optionJson 'build' $resultPath
        if ($LASTEXITCODE -ne 0) { throw "EGP $buildTarget xmake build failed (exit $LASTEXITCODE)." }
        if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) { throw 'Native build result receipt is missing.' }
        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        if ($result.platform -ne $Platform -or $result.target -ne $buildTarget -or -not (Test-Path -LiteralPath $result.editor -PathType Leaf)) { throw 'Native build result disagrees with the requested target or output.' }
        if ($buildTarget -eq 'editor' -and -not $SkipManaged -and $result.mono -eq $true) {
            & (Join-Path $PSScriptRoot 'build_egp_managed.ps1') -Editor $result.editor -Platform $result.platform -Precision $result.precision -NoDeprecated:($result.deprecated -eq $false)
        }
    }
} finally { $env:XMAKE_EXE = $previousXmake; Pop-Location }

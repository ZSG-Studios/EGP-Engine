# Generate matching Mono glue and managed assemblies after a native xmake editor build.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Editor,
    [ValidateSet('windows', 'linuxbsd', 'macos', 'android', 'ios', 'visionos')][string]$Platform = 'windows',
    [ValidateSet('single', 'double')][string]$Precision = 'single',
    [switch]$NoDeprecated
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$editorPath = (Resolve-Path -LiteralPath $Editor).Path
$logRoot = Join-Path $projectRoot '.build/xmake-managed'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$previousXmakeGlobalDir = $env:XMAKE_GLOBALDIR
Push-Location $projectRoot
try {
    $gluePath = Join-Path $projectRoot 'modules/mono/glue'
    $glueLog = Join-Path $logRoot 'mono-glue.log'
    $glueErrorLog = Join-Path $logRoot 'mono-glue-error.log'
    $process = Start-Process -FilePath $editorPath -ArgumentList @('--headless', '--generate-mono-glue', "`"$gluePath`"") -WindowStyle Hidden -PassThru -RedirectStandardOutput $glueLog -RedirectStandardError $glueErrorLog
    $null = $process.Handle
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        throw "C# glue generation timed out. See $glueLog and $glueErrorLog."
    }
    if ($process.ExitCode -ne 0) { throw "C# glue generation failed (exit $($process.ExitCode))." }
    $env:XMAKE_GLOBALDIR = Join-Path $logRoot "xmake-global"
    $xmake = if ($env:XMAKE) { $env:XMAKE } elseif ($env:XMAKE_EXE) { $env:XMAKE_EXE } else { (Get-Command xmake -ErrorAction Stop).Source }
    $assemblyArgs = @('lua', 'build/xmake/managed.lua', $editorPath, $Platform, $Precision)
    if ($NoDeprecated) { $assemblyArgs += 'no-deprecated' }
    & $xmake @assemblyArgs
    if ($LASTEXITCODE -ne 0) { throw "C# assembly build failed (exit $LASTEXITCODE)." }
} finally {
    $env:XMAKE_GLOBALDIR = $previousXmakeGlobalDir
    Pop-Location
}

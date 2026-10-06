[CmdletBinding()]
param(
    [ValidateSet('editor', 'template_debug', 'template_release', 'all')]
    [string]$Target = 'editor',
    [string]$Workers = $(if ($env:FASTBUILD_WORKERS) { $env:FASTBUILD_WORKERS } else { '10.77.64.1' }),
    [ValidateRange(1, 256)]
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Setup,
    [switch]$CheckWorker,
    [switch]$Local,
    [switch]$DistVerbose,
    [switch]$ForceRemote,
    [switch]$SkipManaged,
    [string[]]$SConsArgs = @()
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$toolDir = Join-Path $projectRoot '.build/fastbuild/tool'
$fbuild = Join-Path $toolDir 'FBuild.exe'
$python = Join-Path $projectRoot '.build/venv/Scripts/python.exe'
Push-Location $projectRoot
try {
    if (-not (Test-Path -LiteralPath $fbuild)) {
        New-Item -ItemType Directory -Force -Path $toolDir | Out-Null
        $archive = Join-Path $projectRoot '.build/fastbuild/FASTBuild-Windows-x64-v1.20.zip'
        Invoke-WebRequest 'https://www.fastbuild.org/downloads/v1.20/FASTBuild-Windows-x64-v1.20.zip' -OutFile $archive
        Expand-Archive -LiteralPath $archive -DestinationPath $toolDir -Force
    }
    $version = & $fbuild -version
    if ($LASTEXITCODE -ne 0 -or $version -notmatch 'FASTBuild v1\.20 ') {
        throw 'EGP requires FASTBuild v1.20 to match the worker.'
    }
    if (-not (Test-Path -LiteralPath $python)) {
        & python -m venv .build/venv
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the build Python environment.' }
    }
    & $python -c 'import SCons; assert tuple(map(int, SCons.__version__.split("."))) >= (4, 10, 1)'
    if ($LASTEXITCODE -ne 0) {
        & $python -m pip install 'scons==4.11.1'
        if ($LASTEXITCODE -ne 0) { throw 'Could not install SCons.' }
    }
    if ($CheckWorker -or $ForceRemote) {
        foreach ($worker in $Workers.Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
            $client = [Net.Sockets.TcpClient]::new()
            try {
                $connection = $client.ConnectAsync($worker.Trim(), 31264)
                if (-not $connection.Wait(5000) -or -not $client.Connected) {
                    throw "FASTBuild worker $worker`:31264 is unreachable. Check the WireGuard tunnel."
                }
                Write-Host "FASTBuild worker $worker`:31264 is reachable."
            } finally { $client.Dispose() }
        }
    }
    if ($Setup) {
        Write-Host "FASTBuild v1.20 ready: $fbuild"
        return
    }
    if ($ForceRemote -and $Local) { throw 'ForceRemote and Local cannot be combined.' }
    if ($Target -eq 'all') {
        foreach ($buildTarget in @('editor', 'template_debug', 'template_release')) {
            $targetParameters = @{}
            foreach ($parameterName in $PSBoundParameters.Keys) {
                if ($parameterName -ne 'Target') { $targetParameters[$parameterName] = $PSBoundParameters[$parameterName] }
            }
            $targetParameters['Target'] = $buildTarget
            & $PSCommandPath @targetParameters
        }
        [IO.File]::WriteAllText((Join-Path $projectRoot '.build/fastbuild/entry-all.stamp'), [DateTime]::UtcNow.ToString('O'))
        return
    }
    $buildArgs = @(
        '-m', 'SCons', '-j', "$Jobs", 'platform=windows', 'arch=x86_64', "target=$Target",
        'module_mono_enabled=yes', 'angle=no', 'accesskit=no', 'd3d12=no', 'fastbuild=yes', "fastbuild_exe=$fbuild",
        "fastbuild_workers=$Workers", "fastbuild_dist=$(-not $Local)",
        "fastbuild_distverbose=$([bool]$DistVerbose)", "fastbuild_forceremote=$([bool]$ForceRemote)"
    ) + $SConsArgs
    if ($Target -eq 'editor') {
        # Embed bindings for the engine's actual API, including native fork modules.
        $editorArgs = @($buildArgs | Select-Object -Skip 2 | Where-Object { $_ -notlike 'target=*' })
        & $python (Join-Path $PSScriptRoot 'build_egp_cpp_editor.py') -- @editorArgs
    } else {
        & $python @buildArgs
    }
    if ($LASTEXITCODE -ne 0) { throw "EGP $Target build failed (exit $LASTEXITCODE)." }
    if ($Target -eq 'editor' -and -not $SkipManaged) {
        & (Join-Path $PSScriptRoot 'build_egp_managed.ps1')
    }
    $stamp = Join-Path $projectRoot ".build/fastbuild/entry-$Target.stamp"
    [IO.File]::WriteAllText($stamp, [DateTime]::UtcNow.ToString('O'))
} finally { Pop-Location }

$ErrorActionPreference = 'Stop'
# Install the exact official bundle from the SDK digest lock. Compiler/SDK
# readiness is checked separately by the C++17 consumer compile/link probe.
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'xmake.lock.json') -Raw | ConvertFrom-Json
$artifact = $lock.artifacts.'windows-x64'
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
    throw 'This SDK bootstrap locks the x64 Windows host tool. Use a qualified host tool profile for ARM64.'
}
$toolDirectory = Join-Path $env:LOCALAPPDATA 'xmake'
$tool = Join-Path $toolDirectory 'xmake.exe'
$verified = (Test-Path -LiteralPath $tool) -and ((Get-FileHash -LiteralPath $tool -Algorithm SHA256).Hash.ToLowerInvariant() -eq $artifact.sha256)
if (-not $verified) {
    New-Item -ItemType Directory -Path $toolDirectory -Force | Out-Null
    $temporary = Join-Path $toolDirectory ('xmake-' + [guid]::NewGuid().ToString() + '.download')
    try {
        Invoke-WebRequest -Uri $artifact.url -OutFile $temporary -UseBasicParsing
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $artifact.sha256) {
            throw 'Official xmake bundle digest does not match the SDK lock.'
        }
        Move-Item -LiteralPath $temporary -Destination $tool -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$compilerInstalled = $false
if (Test-Path -LiteralPath $vswhere) {
    $compilerInstalled = [bool](& $vswhere -products '*' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
}
if (-not $compilerInstalled) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw 'Install the Visual Studio Desktop development with C++ workload, then select Check Toolchain.'
    }
    & winget install --id Microsoft.VisualStudio.BuildTools --exact --source winget --accept-package-agreements --accept-source-agreements --override '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    if ($LASTEXITCODE -notin @(0, 3010)) { exit 1 }
}
Write-Output 'Pinned xmake 3.1.1 bundle verified. Check Toolchain compiles and links the C++17 consumer probe; export qualification remains separate.'

$ErrorActionPreference = 'Stop'
# Existing Visual Studio installations are reused, including Community editions.
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeInstalled = $cmake -or (Test-Path "$env:ProgramFiles/CMake/bin/cmake.exe")
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$compilerInstalled = $false
if (Test-Path $vswhere) {
    $compilerInstalled = [bool](& $vswhere -products '*' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
}
if ($cmakeInstalled -and $compilerInstalled) { Write-Output 'CMake and the Visual Studio C++ toolchain are installed.'; exit 0 }
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Error 'Install Microsoft App Installer (winget), or install CMake and the Visual Studio Desktop development with C++ workload. Then select Check Toolchain.'
    exit 1
}
if (-not $cmakeInstalled) {
    & winget install --id Kitware.CMake --exact --source winget --accept-package-agreements --accept-source-agreements --disable-interactivity
    if ($LASTEXITCODE -ne 0) { exit 1 }
}
if (-not $compilerInstalled) {
    & winget install --id Microsoft.VisualStudio.BuildTools --exact --source winget --accept-package-agreements --accept-source-agreements --override '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    if ($LASTEXITCODE -notin @(0, 3010)) { exit 1 }
}
Write-Output 'Tool installation completed. Check Toolchain verifies the compiler and linker; restart Windows if the installer requests it.'

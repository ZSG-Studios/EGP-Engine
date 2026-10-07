param([Parameter(Mandatory=$true)][string]$RunPath)
$record = Get-Content -Raw -LiteralPath (Join-Path $RunPath 'launch.json') | ConvertFrom-Json
foreach ($processId in $record.pids) {
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$processId"
    if ($process -and $process.CommandLine.Contains($record.project) -and $process.CommandLine.Contains($record.runtime)) {
        Stop-Process -Id $processId
    }
}

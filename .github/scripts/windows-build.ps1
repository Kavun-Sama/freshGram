param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',
    [string[]] $ExtraArguments = @()
)

function Show-Resources {
    $os = Get-CimInstance Win32_OperatingSystem
    $pagefile = (Get-CimInstance Win32_PageFileUsage | ForEach-Object { "$($_.Name) $($_.CurrentUsage)/$($_.AllocatedBaseSize)MB" }) -join ', '
    $drives = (Get-PSDrive -PSProvider FileSystem | ForEach-Object { "$($_.Name): $([math]::Round($_.Free / 1GB, 1))GB free" }) -join ', '
    $top = (Get-Process | Sort-Object WorkingSet64 -Descending | Select-Object -First 5 | ForEach-Object { "$($_.ProcessName)=$([math]::Round($_.WorkingSet64 / 1MB))MB" }) -join ', '
    Write-Host ("[resources] memory free {0}GB of {1}GB, virtual free {2}GB, pagefile {3}; {4}; top: {5}" -f [math]::Round($os.FreePhysicalMemory / 1MB, 1), [math]::Round($os.TotalVisibleMemorySize / 1MB, 1), [math]::Round($os.FreeVirtualMemory / 1MB, 1), $pagefile, $drives, $top)
}

Show-Resources
$arguments = @(
    '--build', 'out', '--config', $Configuration, '--target', 'Telegram', '--',
    '/m:2', '/p:UseMultiToolTask=true', '/p:EnforceProcessCountAcrossBuilds=true', '/p:MultiProcMaxCount=2'
) + $ExtraArguments
$build = Start-Process cmake -WorkingDirectory freshGram -NoNewWindow -PassThru -ArgumentList $arguments
while (-not $build.WaitForExit(300000)) {
    Show-Resources
}
$build.WaitForExit()
Show-Resources
exit $build.ExitCode

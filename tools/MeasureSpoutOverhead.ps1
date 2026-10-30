param([int]$Seconds=10,[switch]$Stress)
$ErrorActionPreference='Stop'
$repo='C:\Users\gstur\Documents\Codex\RaceMate'
$exe=Join-Path $repo 'build/x64/Release/RaceMateDiagnostics.exe'
$results=@()
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RaceMateCpuCycles {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool QueryProcessCycleTime(IntPtr process, out ulong cycles);
}
'@
if($Stress) { $env:RACEMATE_SPOUT_STRESS='1' }
function Sample-Gpu([int]$ProcessId) {
    $engines=@{}
    Get-CimInstance Win32_PerfRawData_GPUPerformanceCounters_GPUEngine -Filter "Name LIKE 'pid_${ProcessId}_%'" | ForEach-Object { $engines[$_.Name]=[double]$_.RunningTime }
    return ,$engines
}
foreach($mode in @('off','on','on','off')) {
    $testProcess=Start-Process -FilePath $exe -ArgumentList "--benchmark-spout-$mode" -WorkingDirectory $repo -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Seconds 3
        if($testProcess.HasExited) { throw "Benchmark exited: $($testProcess.ExitCode)" }
        $gpuBefore=Sample-Gpu $testProcess.Id
        $testProcess.Refresh(); $cpuBefore=$testProcess.TotalProcessorTime.TotalMilliseconds; $wallBefore=[Diagnostics.Stopwatch]::GetTimestamp()
        [ulong]$cyclesBefore=0; [RaceMateCpuCycles]::QueryProcessCycleTime($testProcess.Handle,[ref]$cyclesBefore) | Out-Null
        Start-Sleep -Seconds $Seconds
        $gpuAfter=Sample-Gpu $testProcess.Id
        $testProcess.Refresh(); $cpuAfter=$testProcess.TotalProcessorTime.TotalMilliseconds; $elapsed=([Diagnostics.Stopwatch]::GetTimestamp()-$wallBefore)/[Diagnostics.Stopwatch]::Frequency
        [ulong]$cyclesAfter=0; [RaceMateCpuCycles]::QueryProcessCycleTime($testProcess.Handle,[ref]$cyclesAfter) | Out-Null
        $gpuTicks=0.0; $engineTimes=@{}
        foreach($name in $gpuAfter.Keys) {
            $before=if($gpuBefore.ContainsKey($name)) { $gpuBefore[$name] } else { 0.0 }
            $delta=[Math]::Max(0,$gpuAfter[$name]-$before)
            $gpuTicks+=$delta
            if($delta -gt 0) { $engineTimes[$name]=[Math]::Round($delta/10000/$elapsed,4) }
        }
        $row=[pscustomobject]@{ Mode=$mode; ProcessId=$testProcess.Id; Seconds=$elapsed; CpuMsPerSecond=($cpuAfter-$cpuBefore)/$elapsed; CpuCyclesPerSecond=($cyclesAfter-$cyclesBefore)/$elapsed; GpuEngineMsPerSecond=$gpuTicks/10000/$elapsed; Engines=$engineTimes }
        $results+=$row
        $row | ConvertTo-Json -Compress -Depth 4 | Write-Output
    } finally {
        New-Item -ItemType File -Path (Join-Path $repo "build/qa/$($testProcess.Id).stop") -Force | Out-Null
        $testProcess.WaitForExit(15000) | Out-Null
    }
}
$label=if($Stress) {'stress'} else {'three-overlays'}
$results | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $repo "build/qa/spout-cpu-gpu-$label.json") -Encoding utf8

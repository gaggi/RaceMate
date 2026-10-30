param(
    [int]$Blocks=10,
    [int]$Seconds=20,
    [int]$SingleSeconds=5,
    [int[]]$Configurations=1..6,
    [switch]$SkipSingles,
    [switch]$Verify,
    [string]$Exe='build/gdi-export/RaceMateDiagnostics.exe',
    [string]$Output='build/qa/gdi-export',
    [double]$ClockGHz=3.7
)
$ErrorActionPreference='Stop'
$env:RACEMATE_BENCH_VERIFY=if($Verify){'1'}else{$null}
$repo=Split-Path $PSScriptRoot -Parent
$exePath=Join-Path $repo $Exe
$outputPath=Join-Path $repo $Output
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
if(Get-Process obs32,obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS before running the benchmark.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RendererCounters {
 public sealed class DisplayGuard : IDisposable {
  [DllImport("kernel32.dll")] static extern uint SetThreadExecutionState(uint flags);
  [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr hwnd,uint msg,UIntPtr wparam,IntPtr lparam,uint flags,uint timeout,out UIntPtr result);
  readonly System.Threading.ManualResetEvent stop=new System.Threading.ManualResetEvent(false);
  readonly System.Threading.ManualResetEvent ready=new System.Threading.ManualResetEvent(false);
  readonly System.Threading.Thread thread;
  uint result;
  public DisplayGuard() {
   thread=new System.Threading.Thread(delegate() {
    result=SetThreadExecutionState(0x80000003u);
    UIntPtr messageResult;
    SendMessageTimeout(new IntPtr(0xffff),0x0112,new UIntPtr(0xf170),new IntPtr(-1),2,1000,out messageResult);
    ready.Set(); stop.WaitOne();
    SetThreadExecutionState(0x80000000u);
   });
   thread.IsBackground=true; thread.Start(); ready.WaitOne();
   if(result==0) { Dispose(); throw new InvalidOperationException("Cannot keep the benchmark display active"); }
  }
  public void Dispose() { stop.Set(); thread.Join(); stop.Dispose(); ready.Dispose(); }
 }

 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool QueryProcessCycleTime(IntPtr handle,out ulong cycles);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$displayGuard=[RendererCounters+DisplayGuard]::new()
Add-Type -AssemblyName System.Drawing
$dwmProcess=Get-Process dwm | Where-Object SessionId -eq (Get-Process -Id $PID).SessionId | Select-Object -First 1
$dwmHandle=[RendererCounters]::OpenProcess(4096,$false,$dwmProcess.Id)
$dwmCycleAccessError=if($dwmHandle -eq [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::GetLastWin32Error() } else {0}
$metadata=[ordered]@{
    StartedUtc=[DateTime]::UtcNow.ToString('o'); Cpu=(Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name)
    LogicalProcessors=[Environment]::ProcessorCount; Gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    ExeSha256=(Get-FileHash $exePath -Algorithm SHA256).Hash; Blocks=$Blocks; Seconds=$Seconds; SingleSeconds=$SingleSeconds
    Configurations=$Configurations; DwmPid=$dwmProcess.Id; DwmCycleAccessError=$dwmCycleAccessError; Verify=[bool]$Verify
    ClockGHz=$ClockGHz; ClockApproximation='nominal or user-specified; cycles are not verified elapsed CPU time'; Backdrop='visible fixed checkerboard'; CounterFrequency=[Diagnostics.Stopwatch]::Frequency
    DisplayRequired=$true; SystemRequired=$true
    Elevated=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
$metadataPath=Join-Path $outputPath 'environment.json'
if(Test-Path $metadataPath) {
    $previousMetadata=Get-Content $metadataPath -Raw | ConvertFrom-Json
    if($previousMetadata.ExeSha256 -ne $metadata.ExeSha256) { throw 'Existing measurements use a different executable; choose a new output directory.' }
    $metadata['OriginalStartedUtc']=$previousMetadata.StartedUtc
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content $metadataPath -Encoding UTF8
$config=@{
 1=@(0,0,'premultiplied'); 2=@(0,1,'straight'); 3=@(0,1,'premultiplied')
 4=@(1,0,'premultiplied'); 5=@(1,1,'straight'); 6=@(1,1,'premultiplied')
}
$expectedHashes=@{}
$completed=@{}
$csvPath=Join-Path $outputPath 'raw.csv'
if(Test-Path $csvPath) {
    $oldRows=@(Import-Csv $csvPath)
    if($oldRows.Count -and !($oldRows[0].PSObject.Properties.Name -contains 'ArtifactPrefix')) {
        Copy-Item -LiteralPath $csvPath -Destination (Join-Path $outputPath 'raw-before-resume.csv')
        foreach($old in $oldRows) { $old | Add-Member -NotePropertyName ArtifactPrefix -NotePropertyValue "renderer-$($old.Pid)" }
        $oldRows | Export-Csv $csvPath -NoTypeInformation -Encoding UTF8
    }
    foreach($old in $oldRows) {
        $completed["$($old.Block)/$($old.Configuration)/$($old.Single)"]=$true
        $duration=[int][Math]::Round([double]::Parse($old.Seconds.Replace(',','.'),[Globalization.CultureInfo]::InvariantCulture))
        foreach($kind in @(0,1,5)) { if($old."K${kind}_scene_hash") { $expectedHashes["$duration-$kind"]=$old."K${kind}_scene_hash" } }
    }
}
function Snapshot([int]$RacePid) {
    $gpu=@{}
    Get-CimInstance Win32_PerfRawData_GPUPerformanceCounters_GPUEngine -Filter "Name LIKE 'pid_${RacePid}_%' OR Name LIKE 'pid_$($dwmProcess.Id)_%'" |
        Where-Object { $_.Name -like "pid_${RacePid}_*" -or $_.Name -like "pid_$($dwmProcess.Id)_*" } |
        ForEach-Object { $gpu[$_.Name]=[double]$_.RunningTime }
    [ulong]$cycles=0; $read=$false
    if($dwmHandle -ne [IntPtr]::Zero) { $read=[RendererCounters]::QueryProcessCycleTime($dwmHandle,[ref]$cycles) }
    return @{Gpu=$gpu;DwmCycles=if($read){[double]$cycles}else{$null};Stamp=[Diagnostics.Stopwatch]::GetTimestamp()}
}
function Run-Case([int]$Id,[int]$Block,[int]$Single,[int]$Duration) {
    if(Get-Process obs32,obs64 -ErrorAction SilentlyContinue) { throw 'OBS started during the benchmark; stop it before continuing.' }
    $c=$config[$Id]
    $env:RACEMATE_SPOUT_ALPHA=$c[2]
    $env:RACEMATE_BENCH_BACKGROUND_ONLY=if($c[0]){'1'}else{$null}
    $env:RACEMATE_BENCH_SPOUT=if($c[1]){'1'}else{$null}
    $env:RACEMATE_BENCH_SINGLE=if($Single -ge 0){[string]$Single}else{$null}
    $env:RACEMATE_BENCH_SECONDS=[string]$Duration
    $p=Start-Process -FilePath $exePath -ArgumentList '--benchmark-renderers' -WorkingDirectory $repo -WindowStyle Hidden -PassThru
    $root=Join-Path $repo "build/qa/renderer-$($p.Id)"
    $artifactPrefix="b${Block}-c${Id}-s${Single}-p$($p.Id)"
    try {
        # PIDs can be reused. Only handshake files are removed; native output is
        # copied to a unique case prefix so earlier runs can never be overwritten.
        foreach($suffix in @('.ready','.go','.done','.release')) {
            $marker="$root$suffix"
            if(Test-Path -LiteralPath $marker) { Remove-Item -LiteralPath $marker }
        }
        $deadline=[DateTime]::UtcNow.AddSeconds(90)
        while(!(Test-Path "$root.ready")) {
            if($p.HasExited) { throw "Configuration $Id failed: $(Get-Content "$root.error" -ErrorAction SilentlyContinue)" }
            if([DateTime]::UtcNow -gt $deadline) { throw "Configuration $Id warmup timed out" }; Start-Sleep -Milliseconds 50
        }
        if($Block -eq 1 -and $Single -lt 0) {
            $screen=[Drawing.Bitmap]::new(1340,385)
            $graphics=[Drawing.Graphics]::FromImage($screen)
            try { $graphics.CopyFromScreen(20,100,0,0,$screen.Size); $screen.Save("$root-desktop.png",[Drawing.Imaging.ImageFormat]::Png) }
            finally { $graphics.Dispose(); $screen.Dispose() }
        }
        $before=Snapshot $p.Id
        New-Item -ItemType File -Path "$root.go" -Force | Out-Null
        while(!(Test-Path "$root.done")) {
            if($p.HasExited) { throw "Configuration $Id failed: $(Get-Content "$root.error" -ErrorAction SilentlyContinue)" }
            if([DateTime]::UtcNow -gt $deadline) { throw "Configuration $Id measurement timed out" }; Start-Sleep -Milliseconds 20
        }
        $after=Snapshot $p.Id
        $elapsed=($after.Stamp-$before.Stamp)/[Diagnostics.Stopwatch]::Frequency
        New-Item -ItemType File -Path "$root.release" -Force | Out-Null
        if(!$p.WaitForExit(15000)) { throw 'Validation did not finish' }
        if($p.ExitCode -ne 0) { throw "Validation failed: $(Get-Content "$root.error" -ErrorAction SilentlyContinue)" }
        $native=Import-Csv "$root.csv"
        foreach($item in $native) {
            $rate=if($item.kind -eq '0'){10}elseif($item.kind -eq '1'){1}else{20}
            if([int]$item.refreshes -ne $Duration*$rate) { throw "Invalid refresh count for kind $($item.kind)" }
            $key="$Duration-$($item.kind)"
            if($expectedHashes.ContainsKey($key) -and $expectedHashes[$key] -ne $item.scene_hash) { throw "Scene sequence changed for kind $($item.kind) in configuration $Id" }
            $expectedHashes[$key]=$item.scene_hash
        }
        if(Select-String -Path "$root-validation.txt" -Pattern 'mismatches=[1-9]|mismatched pixels=[1-9]|readback=FAIL' -Quiet) { throw "Image validation failed for configuration $Id" }
        $row=[ordered]@{Configuration=$Id;Block=$Block;Single=$Single;Pid=$p.Id;Seconds=[double]$native[0].elapsed;CounterSeconds=$elapsed;RaceMillionCyclesPerSec=([double]$native[0].process_cycles/[double]$native[0].elapsed/1e6);DwmMillionCyclesPerSec=$null;DwmCycleAccessError=$dwmCycleAccessError}
        if($null -ne $before.DwmCycles -and $null -ne $after.DwmCycles) { $row.DwmMillionCyclesPerSec=($after.DwmCycles-$before.DwmCycles)/$elapsed/1e6 }
        $row['ApproxOneCorePercent']=$row.RaceMillionCyclesPerSec/($ClockGHz*1000)*100
        foreach($owner in @('Race','Dwm')) { foreach($engine in @('3D','Copy','Other')) { $row["${owner}Gpu${engine}MsPerSec"]=0.0 } }
        foreach($name in $after.Gpu.Keys) {
            $old=if($before.Gpu.ContainsKey($name)){$before.Gpu[$name]}else{0.0}
            $delta=[Math]::Max(0,$after.Gpu[$name]-$old)/10000/$elapsed
            $owner=if($name -like "pid_$($p.Id)_*"){'Race'}else{'Dwm'}
            $engine=if($name -match 'engtype_.*3D$'){'3D'}elseif($name -match 'engtype_Copy$'){'Copy'}else{'Other'}
            $row["${owner}Gpu${engine}MsPerSec"]+=$delta
        }
        $dwmActivity=$row.DwmGpu3DMsPerSec+$row.DwmGpuCopyMsPerSec+$row.DwmGpuOtherMsPerSec
        if($dwmActivity -le 0) { throw 'No DWM GPU activity: the visible benchmark requires an active display; this run is invalid.' }
        foreach($kind in @(0,1,5)) { foreach($field in @('refreshes','scene_hash','median_thread_cycles','max_thread_cycles','median_wall_us','max_wall_us')) { $row["K${kind}_$field"]=$null } }
        foreach($item in $native) { foreach($field in @('refreshes','scene_hash','median_thread_cycles','max_thread_cycles','median_wall_us','max_wall_us')) { $row["K$($item.kind)_$field"]=$item.$field } }
        $row['ArtifactPrefix']=$artifactPrefix
        $csv=Join-Path $outputPath 'raw.csv'
        [pscustomobject]$row | Export-Csv $csv -NoTypeInformation -Append -Encoding UTF8
        $suffixes=@('.csv','-stages.csv','-warmup-stages.csv','-validation.txt','-outliers.csv','-worker.csv','-worker-outliers.csv','-worker-warmup.csv')
        foreach($item in $native) { $suffixes+="-$($item.kind).bmp" }
        if($Block -eq 1 -and $Single -lt 0) { $suffixes+='-desktop.png' }
        foreach($suffix in $suffixes) { Copy-Item -LiteralPath "$root$suffix" -Destination (Join-Path $outputPath "$artifactPrefix$suffix") }
        $completed["$Block/$Id/$Single"]=$true
        Write-Output "Block $Block / configuration $Id / single $Single : $([Math]::Round($row.RaceMillionCyclesPerSec,2)) Mcycles/s"
    } finally {
        if(!$p.HasExited) { New-Item -ItemType File -Path "$root.release" -Force | Out-Null; Stop-Process -Id $p.Id }
    }
}
try {
    for($block=1;$block -le $Blocks;$block++) { foreach($id in ($Configurations | Sort-Object {Get-Random})) { if(!$completed.ContainsKey("$block/$id/-1")) { Run-Case $id $block -1 $Seconds } } }
    if(!$SkipSingles) { foreach($kind in @(0,1,5)) { foreach($id in ($Configurations | Sort-Object {Get-Random})) { if(!$completed.ContainsKey("0/$id/$kind")) { Run-Case $id 0 $kind $SingleSeconds } } } }
} finally {
    $displayGuard.Dispose()
    if($dwmHandle -ne [IntPtr]::Zero) { [RendererCounters]::CloseHandle($dwmHandle) | Out-Null }
    foreach($name in @('RACEMATE_SPOUT_ALPHA','RACEMATE_BENCH_BACKGROUND_ONLY','RACEMATE_BENCH_SPOUT','RACEMATE_BENCH_SINGLE','RACEMATE_BENCH_SECONDS')) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
}

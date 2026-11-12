[CmdletBinding()]
param(
    # A local clip with burnt-in English subtitles. Game footage: never commit it or what is read from it.
    [string]$Media = "",
    [string]$Deps = "",
    [ValidateSet("x64-release", "x64-debug")][string]$Preset = "x64-release",
    # off: feature disabled (baseline for the cost comparison). on: enabled.
    [ValidateSet("on", "off")][string]$Subs = "on",
    # Effects switched on through the bridge after start: none | sr | fg2 | sr+fg2
    [string]$Effects = "none",
    [int]$RunMs = 60000,
    # Window screenshots (PNG) at these times, ms after start.
    [int[]]$ShotsAtMs = @(),
    # Transcript to compare the shown lines with (tab separated: seconds, speaker, English, Turkish reference).
    [string]$Transcript = "",
    [string]$Size = "1600x900",
    # Open Ayarlar -> Altyazı çevirisi instead of playing (screenshot of the settings tab).
    [switch]$Settings,
    # Initial contents of crispplay-subs.v1.json besides "enabled" (e.g. @{ online = $true }).
    [hashtable]$Options = @{},
    [string]$Name = ""
)
# Subtitle translation real-pipeline run (custom/docs/SUBS.md): plays a clip in
# the real app with an isolated --data-dir, samples the process's CPU / RAM /
# GPU once a second, takes window screenshots, and (with -Transcript) compares
# the lines the app showed with the transcript. Everything lands in
# out\subs-test\<name>; nothing touches the user's settings.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Deps) { $Deps = Join-Path $root "..\deps" }
$deps = (Resolve-Path $Deps).Path
if (-not $Media) { $Media = Join-Path $env:USERPROFILE "Downloads\sub_test.mp4" }
$media = (Resolve-Path -LiteralPath $Media).Path
$build = Join-Path $root "out\build\$Preset"
$exe = Join-Path $build "veyra_qml_ui.exe"
if (-not $Name) { $Name = "$Subs-" + ($Effects -replace '\+', '-') }
$work = Join-Path $root "out\subs-test\$Name"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
$data = Join-Path $work "data"
New-Item -ItemType Directory -Force $data | Out-Null

$utf8 = New-Object System.Text.UTF8Encoding($false)
$subsSettings = @{ enabled = ($Subs -eq "on") } + $Options
[IO.File]::WriteAllText((Join-Path $data "crispplay-subs.v1.json"), ($subsSettings | ConvertTo-Json), $utf8)
[IO.File]::WriteAllText((Join-Path $data "qml-preferences.v1.json"), (@{ language = "tr"; recordDir = (Join-Path $work "clips") } | ConvertTo-Json), $utf8)

$env:PATH = "$deps\qt\6.8.3\msvc2022_64\bin;$deps\vcpkg_installed\x64-windows\bin;$env:PATH"
# Development models (the release build downloads them into <data>\models): the
# model package unzipped into out\subs-models (..\dist\crispplay-subs-models-v1.zip,
# custom/subs/net/README.md). ModelStore verifies every file of it.
$models = Join-Path $root "out\subs-models"
if (-not (Test-Path (Join-Path $models "ppocr-rec"))) {
    $zip = Join-Path $root "..\dist\crispplay-subs-models-v1.zip"
    if (-not (Test-Path $zip)) { throw "Model package not found: $zip (custom\scripts\package-subs-models.ps1)" }
    Expand-Archive -LiteralPath $zip -DestinationPath $models -Force
}
$env:CRISPPLAY_SUBS_MODEL_ROOT = $models
$env:CRISPPLAY_SUBS_TRACE = "1"
$events = Join-Path $work "events.jsonl"
$env:CRISPPLAY_SUBS_EVENT_LOG = $events
if ($Effects -ne "none") { $env:CRISPPLAY_TEST_SUBS_EFFECTS = $Effects } else { Remove-Item Env:CRISPPLAY_TEST_SUBS_EFFECTS -ErrorAction SilentlyContinue }
if ($Settings) { $env:CRISPPLAY_TEST_SUBS_SETTINGS = "1" } else { Remove-Item Env:CRISPPLAY_TEST_SUBS_SETTINGS -ErrorAction SilentlyContinue }
$log = Join-Path $build "logs\veyra-qml.log"
if (Test-Path $log) { Remove-Item -Force $log }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class CrispShot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[void][CrispShot]::SetProcessDPIAware()
function Save-Window($handle, $path) {
    $r = New-Object CrispShot+RECT
    if (-not [CrispShot]::GetWindowRect($handle, [ref]$r)) { return $false }
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    if ($w -le 0 -or $h -le 0) { return $false }
    # The video is a child swap chain and the Turkish line a layered child:
    # the screen is the only place where both are composed.
    $bitmap = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return $true
}

$process = Start-Process -FilePath $exe -WorkingDirectory $build -PassThru -RedirectStandardOutput (Join-Path $work "stdout.txt") -RedirectStandardError (Join-Path $work "stderr.txt") `
    -ArgumentList $(if ($Settings) { @("--data-dir", "`"$data`"", "--size", $Size, "--exit-after", "$RunMs", "--page", "set") }
                    else { @("--data-dir", "`"$data`"", "--size", $Size, "--exit-after", "$RunMs", "`"$media`"") })
$null = $process.Handle
$started = Get-Date
$shots = @($ShotsAtMs | Sort-Object)
$shotIndex = 0
$samples = @()
$cores = [Environment]::ProcessorCount
$lastCpu = $null; $lastAt = $null
while (-not $process.HasExited) {
    $elapsed = ((Get-Date) - $started).TotalMilliseconds
    while ($shotIndex -lt $shots.Count -and $elapsed -ge $shots[$shotIndex]) {
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            [void][CrispShot]::SetForegroundWindow($process.MainWindowHandle)
            Start-Sleep -Milliseconds 120
            $file = Join-Path $work ("shot-{0:D6}.png" -f $shots[$shotIndex])
            if (Save-Window $process.MainWindowHandle $file) { Write-Host "screenshot $file" }
        }
        $shotIndex++
    }
    try {
        $id = $process.Id
        $counters = Get-Counter -Counter @("\GPU Engine(pid_${id}_*)\Utilization Percentage", "\GPU Process Memory(pid_${id}_*)\Dedicated Usage") -ErrorAction Stop
        $engines = @{}; $vram = 0.0
        foreach ($s in $counters.CounterSamples) {
            if ($s.Path -match "engtype_([a-z0-9]+)\)\\utilization") { $engines[$Matches[1]] = [double]$engines[$Matches[1]] + $s.CookedValue }
            elseif ($s.Path -match "dedicated usage") { $vram += $s.CookedValue }
        }
        $process.Refresh()
        $now = Get-Date; $cpu = $process.TotalProcessorTime.TotalMilliseconds
        $cpuPct = if ($lastAt) { 100.0 * ($cpu - $lastCpu) / ($now - $lastAt).TotalMilliseconds } else { 0 }   # percent of one core
        $lastCpu = $cpu; $lastAt = $now
        $samples += [pscustomobject]@{ ms = [int]$elapsed; gpu3d = [double]$engines["3d"]; vramMiB = $vram / 1MB
            privateMiB = $process.PrivateMemorySize64 / 1MB; workingMiB = $process.WorkingSet64 / 1MB; cpuCorePct = $cpuPct }
    } catch { Start-Sleep -Milliseconds 500 }
}
$process.WaitForExit()
Write-Host "app exit code $($process.ExitCode)"
Copy-Item $log (Join-Path $work "veyra-qml.log")
$log = Join-Path $work "veyra-qml.log"

function Median($values) { $v = @($values | Sort-Object); if (-not $v.Count) { return 0 }; return $v[[int][math]::Floor(($v.Count - 1) / 2)] }
function P95($values) { $v = @($values | Sort-Object); if (-not $v.Count) { return 0 }; return $v[[int][math]::Floor(($v.Count - 1) * 0.95)] }
# Steady state only: skip start-up (model load, effect warm-up) and the exit.
$steady = @($samples | Where-Object { $_.ms -ge 12000 -and $_.ms -le $RunMs - 2000 })
$timing = @(Select-String -LiteralPath $log -Pattern "\[player-timing\]" -Encoding UTF8 | ForEach-Object { $_.Line })
$field = { param($line, $name) if ($line -match "$name=([0-9.]+)") { [double]$Matches[1] } else { 0 } }
$timing = @($timing | Select-Object -Skip 6)
$summary = [ordered]@{
    subs = $Subs; effects = $Effects; exitCode = $process.ExitCode; counterSamples = $steady.Count; timingLines = $timing.Count
    cpuCorePctMedian = [math]::Round((Median ($steady | ForEach-Object cpuCorePct)), 1)
    cpuCorePctMean = [math]::Round((($steady | ForEach-Object cpuCorePct | Measure-Object -Average).Average), 1)
    privateMiB = [math]::Round((Median ($steady | ForEach-Object privateMiB)), 0)
    workingMiB = [math]::Round((Median ($steady | ForEach-Object workingMiB)), 0)
    gpu3dPct = [math]::Round((Median ($steady | ForEach-Object gpu3d)), 2)
    vramProcessMiB = [math]::Round((Median ($steady | ForEach-Object vramMiB)), 0)
    presentP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "presentP95Ms" })), 3)
    presentP95MsWorst = [math]::Round((P95 ($timing | ForEach-Object { & $field $_ "presentP95Ms" })), 3)
    gpuReadyP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "gpuReadyP95Ms" })), 3)
    graphSubmitP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "graphSubmitP95Ms" })), 3)
    gpuBlitP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "gpuBlitP95Ms" })), 3)
    slotWaits = [int](($timing | ForEach-Object { & $field $_ "slotWaits" } | Measure-Object -Sum).Sum)
    subsLines = @(Select-String -LiteralPath $log -Pattern "\[subs\] (timing|state|ocr|engines)|\[subs-capture\]" -Encoding UTF8 | ForEach-Object { ($_.Line -replace "^\S+ t=\d+ ", "") } | Select-Object -Last 8)
}
$failed = 0
if ($process.ExitCode -ne 0) { $failed++ }
if ($Transcript -and $Subs -eq "on") {
    $python = Join-Path $deps "tools\subs-venv\Scripts\python.exe"
    if (-not (Test-Path $python)) { $python = "python" }
    $report = Join-Path $work "regress.json"
    & $python -I -X utf8 (Join-Path $root "custom\subs\app\tools\subs_regress.py") --events $events --transcript (Resolve-Path -LiteralPath $Transcript).Path --out $report
    if ($LASTEXITCODE -ne 0) { $failed++ }
    if (Test-Path $report) { $summary.regress = Get-Content $report -Raw -Encoding UTF8 | ConvertFrom-Json }
}
$summary.failed = $failed
$json = $summary | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $work "summary.json"), $json, $utf8)
Write-Host $json
exit $failed

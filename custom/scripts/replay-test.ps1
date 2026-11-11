[CmdletBinding()]
param(
    # A local media file to play. Default: the README clip in the repo root (1080p60, 14 s).
    [string]$Media = "",
    [string]$Deps = "",
    [ValidateSet("x64-release", "x64-debug")][string]$Preset = "x64-release",
    # off: replay disabled (baseline for the cost comparison). on: enabled, one save.
    [ValidateSet("on", "off")][string]$Replay = "on",
    [int]$Seconds = 15,
    # When the save is requested and when the app closes itself, ms after start.
    [int]$SaveAfterMs = 12000,
    [int]$RunMs = 16000,
    # Press the real keys instead of the test hook: F8 starts a live recording
    # at RecordAtMs, F9 saves the replay at SaveAfterMs, F8 stops the recording.
    # Needs the app window in the foreground (the script activates it).
    [switch]$Keys,
    [int]$RecordAtMs = 6000,
    # HEVC instead of H.264 (the replay follows the recording codec setting).
    [switch]$Hevc,
    [string]$Name = ""
)
# Instant replay real-pipeline test and cost sampler (custom/docs/REPLAY.md).
# Plays a file in the real app with an isolated --data-dir, samples the
# process's GPU engines / GPU memory / RAM once a second, and validates every
# MP4 it produced with crispplay_replay_clip_check. Never touches the user's
# settings or folders: everything lands in out\replay-test\<name>.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Deps) { $Deps = Join-Path $root "..\deps" }
$deps = (Resolve-Path $Deps).Path
if (-not $Media) { $Media = Join-Path $root "REAMDE MP4.mp4" }
$media = (Resolve-Path -LiteralPath $Media).Path
$build = Join-Path $root "out\build\$Preset"
$exe = Join-Path $build "veyra_qml_ui.exe"
$check = Join-Path $build "custom\crispplay_replay_clip_check.exe"
if (-not $Name) { $Name = "$Replay" + $(if ($Keys) { "-keys" } else { "" }) }
$work = Join-Path $root "out\replay-test\$Name"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
$data = Join-Path $work "data"; $clips = Join-Path $work "clips"
New-Item -ItemType Directory -Force $data, $clips | Out-Null

$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $data "crispplay-replay.v1.json"),
    (@{ enabled = ($Replay -eq "on"); seconds = $Seconds } | ConvertTo-Json), $utf8)
# Recordings and replays go to the test folder, not the user's Videos.
[IO.File]::WriteAllText((Join-Path $data "qml-preferences.v1.json"), (@{ recordDir = $clips; recordHevc = [bool]$Hevc } | ConvertTo-Json), $utf8)

$env:PATH = "$deps\qt\6.8.3\msvc2022_64\bin;$deps\vcpkg_installed\x64-windows\bin;$env:PATH"
if ($Replay -eq "on" -and -not $Keys) {
    $env:CRISPPLAY_TEST_REPLAY_SAVE_MS = "$SaveAfterMs"
    $env:CRISPPLAY_TEST_REPLAY_DIR = $clips
    # Windows the settings page does not offer (to see eviction with a short file).
    if ($Seconds -notin 15, 30, 60, 120) { $env:CRISPPLAY_TEST_REPLAY_SECONDS = "$Seconds" } else { Remove-Item Env:CRISPPLAY_TEST_REPLAY_SECONDS -ErrorAction SilentlyContinue }
} else {
    Remove-Item Env:CRISPPLAY_TEST_REPLAY_SAVE_MS, Env:CRISPPLAY_TEST_REPLAY_DIR -ErrorAction SilentlyContinue
}
$log = Join-Path $build "logs\veyra-qml.log"
if (Test-Path $log) { Remove-Item -Force $log }

$process = Start-Process -FilePath $exe -WorkingDirectory $build -PassThru -RedirectStandardOutput (Join-Path $work "stdout.txt") -RedirectStandardError (Join-Path $work "stderr.txt") `
    -ArgumentList @("--data-dir", "`"$data`"", "--exit-after", "$RunMs", "`"$media`"")
$null = $process.Handle   # keeps the exit code readable after the process ends
$started = Get-Date
$shell = if ($Keys) { New-Object -ComObject WScript.Shell } else { $null }
$pending = @()
if ($Keys) {
    $pending = @(@{ at = $RecordAtMs; key = "{F8}"; what = "F8 record start"; done = $false },
                 @{ at = $SaveAfterMs; key = "{F9}"; what = "F9 replay save"; done = $false },
                 @{ at = $SaveAfterMs + 1500; key = "{F8}"; what = "F8 record stop"; done = $false })
}
$samples = @()
while (-not $process.HasExited) {
    $elapsed = ((Get-Date) - $started).TotalMilliseconds
    foreach ($step in $pending) {
        if (-not $step.done -and $elapsed -ge $step.at) {
            $step.done = $true
            if ($shell.AppActivate($process.Id)) { Start-Sleep -Milliseconds 150; $shell.SendKeys($step.key); Write-Host "sent $($step.what)" }
            else { Write-Host "could not activate the window for $($step.what)" }
        }
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
        $samples += [pscustomobject]@{ ms = [int]$elapsed; gpu3d = [double]$engines["3d"]; encode = [double]$engines["videoencode"];
            vramMiB = $vram / 1MB; privateMiB = $process.PrivateMemorySize64 / 1MB }
    } catch { Start-Sleep -Milliseconds 500 }
}
$process.WaitForExit()
Write-Host "app exit code $($process.ExitCode)"
Copy-Item $log (Join-Path $work "veyra-qml.log")

# Steady state only: skip start-up and the encoder's opening.
function Median($values) { $v = @($values | Sort-Object); if (-not $v.Count) { return 0 }; return $v[[int][math]::Floor(($v.Count - 1) / 2)] }
$steady = @($samples | Where-Object { $_.ms -ge 6000 -and $_.ms -le $RunMs - 1500 })
$timing = @(Select-String -LiteralPath $log -Pattern "\[player-timing\]" | ForEach-Object { $_.Line })
$field = { param($line, $name) if ($line -match "$name=([0-9.]+)") { [double]$Matches[1] } else { 0 } }
$timing = @($timing | Select-Object -Skip 3)
$summary = [ordered]@{
    replay = $Replay; keys = [bool]$Keys; exitCode = $process.ExitCode; counterSamples = $steady.Count; timingLines = $timing.Count
    gpu3dPct = [math]::Round((Median ($steady | ForEach-Object gpu3d)), 2)
    gpuEncodePct = [math]::Round((Median ($steady | ForEach-Object encode)), 2)
    vramProcessMiB = [math]::Round((Median ($steady | ForEach-Object vramMiB)), 0)
    privateMiB = [math]::Round((Median ($steady | ForEach-Object privateMiB)), 0)
    presentP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "presentP95Ms" })), 3)
    gpuReadyP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "gpuReadyP95Ms" })), 3)
    graphSubmitP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "graphSubmitP95Ms" })), 3)
    gpuBlitP95Ms = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "gpuBlitP95Ms" })), 3)
    engineVramMiB = [math]::Round((Median ($timing | ForEach-Object { & $field $_ "vramMiB" })), 0)
    slotWaits = [int](($timing | ForEach-Object { & $field $_ "slotWaits" } | Measure-Object -Sum).Sum)
    replayLines = @(Select-String -LiteralPath $log -Pattern "\[replay\]|\[live-recorder\] file closed" | ForEach-Object { ($_.Line -replace "^\S+ t=\d+ ", "") })
}
$failed = 0
$files = @(Get-ChildItem -LiteralPath $clips -Filter *.mp4 -ErrorAction SilentlyContinue)
$summary.files = @($files | ForEach-Object { $_.Name })
foreach ($file in $files) {
    $isReplay = $file.Name -like "CrispPlay-Tekrar-*"
    $extra = if ($isReplay) { @("--min-seconds", "3", "--max-seconds", "$($Seconds + 2.5)", "--audio") } else { @() }
    $report = Join-Path $work ($file.BaseName + ".check.txt")
    $p = Start-Process -FilePath $check -ArgumentList (@("`"$($file.FullName)`"") + $extra) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $report
    Write-Host "--- $($file.Name)"; Get-Content $report | Select-Object -Last 4
    # A live recording is fragmented MP4 with its own rules; only replays must pass every check.
    if ($isReplay -and $p.ExitCode -ne 0) { $failed++ }
}
if ($Replay -eq "on" -and -not ($files | Where-Object { $_.Name -like "CrispPlay-Tekrar-*" })) { Write-Host "FAILED: no replay file was written"; $failed++ }
if (Get-ChildItem -LiteralPath $clips -Filter *.part -ErrorAction SilentlyContinue) { Write-Host "FAILED: a .part file was left behind"; $failed++ }
$summary.failed = $failed
$json = $summary | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $work "summary.json"), $json, $utf8)
Write-Host $json
exit $failed

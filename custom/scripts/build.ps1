[CmdletBinding()]
param(
    # Folder holding qt\, vcpkg\ and vcpkg_installed\ (see custom/BUILD.md).
    [string]$Deps = "",
    [ValidateSet("x64-release", "x64-debug")][string]$Preset = "x64-release",
    # Default: the app plus the tests that need only Qt.
    [string[]]$Target = @("veyra_qml_ui", "crispplay_branding_tests", "crispplay_i18n_tests", "crispplay_discord_tests",
                          "veyra_update_checker_tests", "veyra_ui_i18n_tests",
                          "crispplay_replay_ring_tests", "crispplay_replay_clip_check", "crispplay_subs_core_tests",
                          "crispplay_subs_mt_tests", "crispplay_subs_ocr_tests", "crispplay_subs_net_tests"),
    [switch]$Test
)
# CrispPlay build wrapper. Upstream scripts/build.ps1 is left untouched; this one
# adds the Qt/QML switches and our dependency layout.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
# Windows PowerShell 5.1 leaves $PSScriptRoot empty inside param() defaults.
if (-not $Deps) { $Deps = Join-Path $root "..\deps" }
$deps = (Resolve-Path $Deps).Path
$qt = Join-Path $deps "qt\6.8.3\msvc2022_64"
$ffmpeg = Join-Path $deps "vcpkg_installed\x64-windows"
foreach ($p in @("$qt\lib\cmake\Qt6\Qt6Config.cmake", "$ffmpeg\include\libavformat\avformat.h",
                 "$root\third_party_local\nvidia\nv-codec-headers-13.0\include\ffnvcodec\nvEncodeAPI.h")) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Missing dependency: $p (see custom/BUILD.md)" }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio 2022 C++ tools not found" }

$build = Join-Path $root "out\build\$Preset"
# NVIDIA DLSS SDK: the engine includes nvsdk_ngx.h unconditionally, so the app
# only builds with it (see custom/BUILD.md). Same switch as upstream build.ps1.
$dlss = Join-Path $root "third_party_local\nvidia\DLSS_SDK_310.7.0"
$dlssArgs = if (Test-Path -LiteralPath "$dlss\include\nvsdk_ngx.h") {
    "-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON -DVEYRA_DLSS_SDK_ROOT=`"$($dlss -replace '\\', '/')`""
} else { "-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=OFF" }
$ffmpegCMake = $ffmpeg -replace '\\', '/'
# MKL-free CTranslate2 + SentencePiece (custom/scripts/build-ctranslate2.ps1); without it
# the subtitle translator is skipped and the rest of the app builds as before.
$ct2 = Join-Path $deps "ctranslate2-install"
$ct2Args = ""
if (Test-Path -LiteralPath "$ct2\lib\ctranslate2.lib") {
    $ct2Args = "-DCRISPPLAY_CT2_ROOT=`"$($ct2 -replace '\\', '/')`""
    if (-not $PSBoundParameters.ContainsKey("Target")) { $Target += "crispplay_mt_bench" }
}
$batch = Join-Path $root "out\crispplay-build.cmd"
New-Item -ItemType Directory -Force (Split-Path $batch) | Out-Null
@(
    '@echo off',
    "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 || exit /b 4",
    'chcp 65001 >nul',
    "cd /d `"$root`"",
    "cmake --preset $Preset -B `"$build`" -DVEYRA_BUILD_QML_UI=ON -DVEYRA_ENABLE_DEINTERLACE=OFF $dlssArgs -DCMAKE_PREFIX_PATH=`"$qt`" -DVEYRA_FFMPEG_ROOT=`"$ffmpegCMake`" $ct2Args || exit /b 5",
    "cmake --build `"$build`" --target $($Target -join ' ') || exit /b 6"
) | Set-Content -LiteralPath $batch -Encoding ASCII
& cmd.exe /c "`"$batch`""
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    $env:PATH = "$qt\bin;$ffmpeg\bin;$env:PATH"
    $failed = 0
    foreach ($t in @("custom\crispplay_branding_tests.exe", "custom\crispplay_i18n_tests.exe", "custom\crispplay_discord_tests.exe",
                     "custom\crispplay_replay_ring_tests.exe", "custom\subs\core\crispplay_subs_core_tests.exe",
                     "custom\subs\mt\crispplay_subs_mt_tests.exe", "custom\subs\ocr\crispplay_subs_ocr_tests.exe",
                     "custom\subs\net\crispplay_subs_net_tests.exe",
                     "veyra_update_checker_tests.exe", "veyra_ui_i18n_tests.exe")) {
        $exe = Join-Path $build $t
        if (-not (Test-Path $exe)) { continue }
        & $exe | Select-Object -Last 2
        if ($LASTEXITCODE -ne 0) { $failed++; Write-Host "FAILED: $t" }
    }
    exit $failed
}

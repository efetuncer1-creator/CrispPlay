[CmdletBinding()]
param(
    # Folder holding the shared dependencies (see custom/BUILD.md). Sources, build
    # trees and the install prefix all go here, never into the repo.
    [string]$Deps = "",
    # GEMM backend. dnnl = oneDNN (shipping choice, same backend the Python wheel
    # picks on AMD CPUs); ruy = Google ruy (comparison only).
    [ValidateSet("dnnl", "ruy")][string]$Backend = "dnnl",
    # Install prefix. Default: <Deps>\ctranslate2-install (dnnl) or
    # <Deps>\ctranslate2-install-ruy.
    [string]$Prefix = "",
    [switch]$Clean
)
# Builds the CPU-only, MKL-free CTranslate2 + SentencePiece used by
# custom/subs/mt (CRISPPLAY_CT2_ROOT). Pinned sources, MSVC + Ninja, Release.
# Everything is a static library with the static CRT (/MT), like the app (root
# CMakeLists sets MultiThreaded), so the translator links straight into the EXE:
# <Prefix>\lib\{ctranslate2,dnnl,cpu_features,sentencepiece}.lib + headers.
# The only runtime file is MSVC's OpenMP, vcomp140.dll (VC redist, OpenMP folder).
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Deps) { $Deps = Join-Path $root "..\deps" }
$deps = (Resolve-Path $Deps).Path
if (-not $Prefix) { $Prefix = Join-Path $deps $(if ($Backend -eq "dnnl") { "ctranslate2-install" } else { "ctranslate2-install-ruy" }) }

# --- Pinned sources (update together with custom/subs/mt/README.md) ---------------
$pins = @(
    @{ Name = "ctranslate2-src";   Url = "https://github.com/OpenNMT/CTranslate2.git";  Commit = "a22ba1c63ee35d1a0d25db149aa2a66d36a6a732" }, # v4.9.0, MIT
    @{ Name = "onednn-src";        Url = "https://github.com/uxlfoundation/oneDNN.git"; Commit = "64f6bcbcbab628e96f33a62c3e975f8535a7bde4" }, # v3.1.1, Apache-2.0 (as in the CT2 4.9.0 wheel)
    @{ Name = "sentencepiece-src"; Url = "https://github.com/google/sentencepiece.git"; Commit = "31646a467d2051eb904e0b45de3a73e91fe1c1e3" }  # v0.2.1, Apache-2.0 (last tag bundling protobuf-lite + absl subset; v0.2.2+ fetch abseil/protobuf at configure time)
)
# CTranslate2 submodules we need (commits are pinned by the CT2 commit itself).
$ct2Submodules = @("third_party/spdlog", "third_party/cpu_features")
if ($Backend -eq "ruy") { $ct2Submodules += "third_party/ruy" }

function Invoke-Git([string[]]$GitArgs) {
    & git @GitArgs
    if ($LASTEXITCODE -ne 0) { throw "git $($GitArgs -join ' ') failed ($LASTEXITCODE)" }
}

foreach ($p in $pins) {
    $dir = Join-Path $deps $p.Name
    if (Test-Path -LiteralPath (Join-Path $dir ".git")) {
        $head = (& git -C $dir rev-parse HEAD).Trim()
        if ($head -eq $p.Commit) { Write-Host "$($p.Name): $head (present)"; continue }
        Write-Host "$($p.Name): $head -> $($p.Commit)"
    } else {
        New-Item -ItemType Directory -Force $dir | Out-Null
        Invoke-Git @("-C", $dir, "init", "-q")
        Invoke-Git @("-C", $dir, "remote", "add", "origin", $p.Url)
    }
    Invoke-Git @("-C", $dir, "fetch", "-q", "--depth", "1", "origin", $p.Commit)
    Invoke-Git @("-C", $dir, "-c", "advice.detachedHead=false", "checkout", "-q", "--force", $p.Commit)
}
$ct2Src = Join-Path $deps "ctranslate2-src"
Invoke-Git (@("-C", $ct2Src, "submodule", "update", "--init", "--depth", "1", "--") + $ct2Submodules)

# --- Toolchain ---------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio 2022 C++ tools not found" }

$buildRoot = Join-Path $deps "ctranslate2-build\$Backend"
if ($Clean -and (Test-Path $buildRoot)) { Remove-Item -Recurse -Force $buildRoot }
$dnnlPrefix = Join-Path $deps "ctranslate2-build\onednn-install"
$fwd = { param($s) $s -replace '\\', '/' }

$common = "-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
$cmds = @()
if ($Backend -eq "dnnl") {
    # Same switches as CTranslate2's own Windows wheel build (python/tools/prepare_build_environment_windows.sh),
    # with MSVC's OpenMP instead of Intel's libiomp5.
    $cmds += "cmake -S `"$(Join-Path $deps 'onednn-src')`" -B `"$buildRoot\onednn`" $common -DCMAKE_INSTALL_PREFIX=`"$(& $fwd $dnnlPrefix)`" -DONEDNN_LIBRARY_TYPE=STATIC -DONEDNN_CPU_RUNTIME=OMP -DONEDNN_BUILD_EXAMPLES=OFF -DONEDNN_BUILD_TESTS=OFF -DONEDNN_ENABLE_WORKLOAD=INFERENCE -DONEDNN_ENABLE_PRIMITIVE=`"CONVOLUTION;REORDER`" -DONEDNN_BUILD_GRAPH=OFF || exit /b 10"
    $cmds += "cmake --build `"$buildRoot\onednn`" --target install || exit /b 11"
    $ct2Backend = "-DWITH_DNNL=ON -DWITH_RUY=OFF -DCMAKE_PREFIX_PATH=`"$(& $fwd $dnnlPrefix)`""
} else {
    $ct2Backend = "-DWITH_DNNL=OFF -DWITH_RUY=ON"
}
$cmds += "cmake -S `"$ct2Src`" -B `"$buildRoot\ctranslate2`" $common -DCMAKE_INSTALL_PREFIX=`"$(& $fwd $Prefix)`" -DBUILD_SHARED_LIBS=OFF -DBUILD_CLI=OFF -DBUILD_TESTS=OFF -DWITH_MKL=OFF -DWITH_CUDA=OFF -DWITH_CUDNN=OFF -DOPENMP_RUNTIME=COMP $ct2Backend || exit /b 12"
$cmds += "cmake --build `"$buildRoot\ctranslate2`" --target install || exit /b 13"
# Static CTranslate2 does not install its private static dependencies; copy them.
$libDir = (Join-Path $Prefix "lib")
$cmds += "copy /y `"$buildRoot\ctranslate2\third_party\cpu_features\cpu_features.lib`" `"$libDir`" >nul || exit /b 16"
if ($Backend -eq "dnnl") { $cmds += "copy /y `"$dnnlPrefix\lib\dnnl.lib`" `"$libDir`" >nul || exit /b 17" }
# SentencePiece: static, static CRT (/MT) like the app; protobuf-lite is built in.
$cmds += "cmake -S `"$(Join-Path $deps 'sentencepiece-src')`" -B `"$buildRoot\sentencepiece`" $common -DCMAKE_INSTALL_PREFIX=`"$(& $fwd $Prefix)`" -DSPM_ENABLE_SHARED=OFF -DSPM_ENABLE_TCMALLOC=OFF -DSPM_ENABLE_MSVC_MT_BUILD=ON -DSPM_BUILD_TEST=OFF || exit /b 14"
$cmds += "cmake --build `"$buildRoot\sentencepiece`" --target install || exit /b 15"

$batch = Join-Path $deps "ctranslate2-build\build-$Backend.cmd"
New-Item -ItemType Directory -Force (Split-Path $batch) | Out-Null
(@('@echo off', "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 || exit /b 4") + $cmds) |
    Set-Content -LiteralPath $batch -Encoding ASCII
& cmd.exe /c "`"$batch`""
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Record what was built next to the binaries.
$info = @("backend=$Backend", "linkage=static, /MT", "openmp=MSVC (vcomp140.dll)") + ($pins | ForEach-Object { "$($_.Name)=$($_.Commit)" })
$info | Set-Content -LiteralPath (Join-Path $Prefix "CRISPPLAY_BUILD.txt") -Encoding ASCII
Write-Host "Installed to $Prefix"

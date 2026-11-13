[CmdletBinding()]
param(
    [string]$Deps = "",
    [string]$Build = "",
    # Output folder; defaults to <repo>\..\dist
    [string]$Out = "",
    [switch]$NoZip,
    [switch]$Force
)
# CrispPlay portable package. Follows upstream scripts/package-qml-release.py
# (windeployqt flags, qt.conf, CRT from the VS redist, *.dxil shaders) without
# its Veyra release-provenance checks. Output:
#   <Out>\CrispPlay-<version>-win64-portable\   (runs as is; data stays inside)
#   <Out>\CrispPlay-<version>-win64-portable.zip
#   <Out>\CrispPlay-<version>-source.zip          (GPL-3 corresponding source, git archive of HEAD)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Deps) { $Deps = Join-Path $root "..\deps" }
if (-not $Build) { $Build = Join-Path $root "out\build\x64-release" }
if (-not $Out) { $Out = Join-Path $root "..\dist" }
$deps = (Resolve-Path $Deps).Path
$build = (Resolve-Path $Build).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$out = (Resolve-Path $Out).Path
$qt = Join-Path $deps "qt\6.8.3\msvc2022_64"
$ffmpeg = Join-Path $deps "vcpkg_installed\x64-windows"

$cache = Get-Content (Join-Path $build "CMakeCache.txt") -Raw
if ($cache -notmatch '(?m)^VEYRA_DISPLAY_VERSION:STRING=(.+)$') { throw "VEYRA_DISPLAY_VERSION not in CMakeCache" }
$version = $Matches[1].Trim()
if ($cache -notmatch '(?m)^VEYRA_BUILD_QML_UI:BOOL=ON\r?$') { throw "Build without VEYRA_BUILD_QML_UI" }
foreach ($p in @("veyra_qml_ui.exe", "qml\Veyra\Main.qml", "qml\CrispPlay", "runtime\experimental\nvngx_dlss.dll", "runtime_local", "licenses")) {
    if (-not (Test-Path (Join-Path $build $p))) { throw "Missing in build: $p (see custom/BUILD.md)" }
}
$dirty = git -C $root status --porcelain --untracked-files=no
if ($dirty) { Write-Warning "Working tree has uncommitted changes; the source zip is HEAD and will not contain them." }
$commit = (git -C $root rev-parse --short=8 HEAD).Trim()

$name = "CrispPlay-$version-win64-portable"
$stage = Join-Path $out $name
if (Test-Path $stage) {
    if (-not $Force) { throw "$stage exists (use -Force to replace it)" }
    Remove-Item $stage -Recurse -Force
}
New-Item -ItemType Directory $stage | Out-Null

function Copy-Tree($from, $to, [string[]]$exclude = @()) {
    $src = (Resolve-Path $from).Path
    Get-ChildItem $src -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($src.Length + 1)
        foreach ($x in $exclude) { if ($rel -like $x) { return } }
        $dst = Join-Path $to $rel
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item $_.FullName $dst
    }
}

Write-Host "Staging $name ($commit)"
Copy-Item (Join-Path $build "veyra_qml_ui.exe") (Join-Path $stage "CrispPlay.exe")
Copy-Tree (Join-Path $build "qml\Veyra") (Join-Path $stage "qml\Veyra")
Copy-Tree (Join-Path $build "qml\CrispPlay") (Join-Path $stage "qml\CrispPlay")
Get-ChildItem (Join-Path $build "shaders") -Recurse -File -Filter *.dxil | ForEach-Object {
    $rel = $_.FullName.Substring((Join-Path $build "shaders").Length + 1)
    $dst = Join-Path $stage "shaders\$rel"
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item $_.FullName $dst
}
# Runtime components (upstream 2.1.5 package, see BUILD.md). Never ship user data or logs.
Copy-Tree (Join-Path $build "runtime") (Join-Path $stage "runtime")
Copy-Tree (Join-Path $build "runtime_local") (Join-Path $stage "runtime_local") @("user-data*", "*\user-data*", "logs\*")
Copy-Tree (Join-Path $build "licenses") (Join-Path $stage "licenses")

$log = Join-Path $out "windeployqt-$version.log"
# Through cmd: Windows PowerShell 5.1 turns native stderr (windeployqt warnings) into errors.
$deploy = "`"$(Join-Path $qt 'bin\windeployqt.exe')`" --release --qmldir `"$(Join-Path $build 'qml')`" --no-compiler-runtime " +
          "--no-translations --no-opengl-sw --skip-plugin-types qmltooling,generic `"$(Join-Path $stage 'CrispPlay.exe')`""
& cmd.exe /c "$deploy > `"$log`" 2>&1"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed, see $log" }
Set-Content (Join-Path $stage "qt.conf") "[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`nTranslations=translations`n" -Encoding ASCII

foreach ($dll in @("avcodec-63.dll", "avformat-63.dll", "avutil-61.dll", "swresample-7.dll", "swscale-10.dll")) {
    Copy-Item (Join-Path $ffmpeg "bin\$dll") $stage
}
New-Item -ItemType Directory -Force (Join-Path $stage "licenses\ffmpeg-vcpkg") | Out-Null
foreach ($f in @("copyright", "vcpkg.spdx.json")) {
    $p = Join-Path $ffmpeg "share\ffmpeg\$f"
    if (Test-Path $p) { Copy-Item $p (Join-Path $stage "licenses\ffmpeg-vcpkg\$f") }
}

$redist = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
$crt = Get-ChildItem $redist -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1
foreach ($dll in @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")) {
    Copy-Item (Join-Path $crt.FullName "x64\Microsoft.VC143.CRT\$dll") $stage
}

# Game subtitle translation (custom/docs/SUBS.md): ONNX Runtime for the second OCR
# reader and the MSVC OpenMP runtime for CTranslate2 / oneDNN (linked statically
# into the EXE). Both are optional at build time; the models are downloaded by
# the app and are not part of the package.
$subsLicenses = Join-Path $stage "licenses\crispplay-subs"
New-Item -ItemType Directory -Force $subsLicenses | Out-Null
$ort = Join-Path $deps "onnxruntime-1.31.1"
if (Test-Path (Join-Path $build "onnxruntime.dll")) {
    Copy-Item (Join-Path $build "onnxruntime.dll") $stage
    foreach ($f in @("LICENSE", "ThirdPartyNotices.txt")) {
        if (Test-Path (Join-Path $ort $f)) { Copy-Item (Join-Path $ort $f) (Join-Path $subsLicenses "onnxruntime-$f") }
    }
} else { Write-Warning "onnxruntime.dll is not in the build: the package reads subtitles with Windows OCR only" }
if ($cache -match '(?m)^CRISPPLAY_VCOMP_DLL:FILEPATH=(.+)$' -and (Test-Path $Matches[1].Trim())) {
    # Same redistributable folder family as the CRT below (Microsoft.VC143.OpenMP).
    Copy-Item $Matches[1].Trim() $stage
    foreach ($pair in @(@("ctranslate2-src\LICENSE", "ctranslate2-LICENSE"), @("ctranslate2-src\third_party\cpu_features\LICENSE", "cpu_features-LICENSE"),
                        @("onednn-src\LICENSE", "oneDNN-LICENSE"), @("onednn-src\THIRD-PARTY-PROGRAMS", "oneDNN-THIRD-PARTY-PROGRAMS"),
                        @("sentencepiece-src\LICENSE", "sentencepiece-LICENSE"), @("ctranslate2-install\CRISPPLAY_BUILD.txt", "ctranslate2-build-pins.txt"))) {
        $from = Join-Path $deps $pair[0]
        if (Test-Path $from) { Copy-Item $from (Join-Path $subsLicenses $pair[1]) } else { Write-Warning "License file missing: $from" }
    }
} else { Write-Warning "vcomp140.dll is not configured (no CTranslate2 in this build): the package has no local translator" }
Copy-Item (Join-Path $root "custom\subs\core\NOTICE.md") (Join-Path $subsLicenses "couchsubs-NOTICE.md")
Get-ChildItem (Join-Path $root "custom\subs\net\model-licenses") -File | ForEach-Object { Copy-Item $_.FullName (Join-Path $subsLicenses ("models-" + $_.Name)) }

foreach ($f in @("LICENSE", "THIRD_PARTY_NOTICES.md")) { Copy-Item (Join-Path $root $f) $stage }
Copy-Item (Join-Path $root "custom\THIRD_PARTY_NOTICES_CRISPPLAY.md") $stage
@"
CrispPlay $version (Windows x64, taşınabilir)
Kaynak: https://github.com/efetuncer1-creator/CrispPlay (commit $commit)
Veyra (Likely7) tabanlıdır: https://github.com/Likely7/Veyra-NRVideo · GPL-3.0

Kurulum yok: klasörü istediğin yere çıkar, CrispPlay.exe'yi çalıştır.
Ayarlar ve günlükler bu klasörün içinde kalır (runtime_local\user-data-2.0.0, logs).
Bir videoyu CrispPlay.exe'nin üzerine sürükleyip bırakarak da açabilirsin.

Oyun altyazısı çevirisi (F10, Ayarlar → Altyazı çevirisi): ilk açılışta çeviri
modeli (~226 MB) indirilir; ayrıntı ve gizlilik: custom/docs/SUBS.md (kaynak zip'i).

SR / NR / FG için NVIDIA RTX ekran kartı ve güncel sürücü gerekir. Anlık tekrar
(F9) yalnız NVIDIA'da çalışır. NVIDIA, AMD ve Intel bileşenlerinin lisansları
licenses\ altındadır; GPL-3 kaynak kodu: CrispPlay-$version-source.zip.
"@ | Set-Content (Join-Path $stage "BENIOKU.txt") -Encoding UTF8

$sourceZip = Join-Path $out "CrispPlay-$version-source.zip"
if (Test-Path $sourceZip) { Remove-Item $sourceZip }
git -C $root archive --format=zip --prefix="CrispPlay-$version-source/" -o $sourceZip HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }

$files = Get-ChildItem $stage -Recurse -File
$size = ($files | Measure-Object Length -Sum).Sum
Write-Host ("Staged {0} files, {1:N0} MB" -f $files.Count, ($size / 1MB))

if (-not $NoZip) {
    $zip = Join-Path $out "$name.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    & tar.exe -a -c -f $zip -C $out $name
    if ($LASTEXITCODE -ne 0) { throw "zip failed" }
    Write-Host ("Zip {0} ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
}
Write-Host "Done: $stage"

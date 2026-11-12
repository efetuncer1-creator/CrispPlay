[CmdletBinding()]
param(
    # Folder holding opus-mt-tc-big-en-tr-ct2-int8\ and ppocr-rec\ (default ..\deps\models).
    [string]$Models = "",
    # Where the zip, its .sha256 and the manifest JSON are written (default ..\dist).
    [string]$OutDir = "",
    # Only print what would be packed and the source hashes; write nothing.
    [switch]$DryRun
)
# Builds the subtitle model release asset crispplay-subs-models-v1.zip (custom/subs/net/README.md)
# and regenerates custom/subs/net/src/ModelManifestData.inc, the manifest compiled into
# crispplay_subs_net. Nothing is uploaded: publishing the GitHub release is a separate, manual step.
#
# The zip is deterministic for a given .NET Framework build (fixed entry order and timestamps),
# but its SHA-256 is pinned in the app, so a rebuilt zip MUST be published together with the
# regenerated manifest (or not at all).
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Models) { $Models = Join-Path $root "..\deps\models" }
if (-not $OutDir) { $OutDir = Join-Path $root "..\dist" }
$Models = (Resolve-Path $Models).Path
$licenses = Join-Path $root "custom\subs\net\model-licenses"
$manifestInc = Join-Path $root "custom\subs\net\src\ModelManifestData.inc"

$assetName = "crispplay-subs-models-v1.zip"
$mt = "opus-mt-tc-big-en-tr-ct2-int8"
$ocr = "ppocr-rec"
# Archive path => source file. Order is the order inside the zip (small files first).
$entries = [ordered]@{
    "$mt/ATTRIBUTION.txt"                = Join-Path $licenses "opus-mt-ATTRIBUTION.txt"
    "$mt/LICENSE-CC-BY-4.0.txt"          = Join-Path $licenses "LICENSE-CC-BY-4.0.txt"
    "$mt/config.json"                    = Join-Path $Models "$mt\config.json"
    "$mt/source.spm"                     = Join-Path $Models "$mt\source.spm"
    "$mt/target.spm"                     = Join-Path $Models "$mt\target.spm"
    "$mt/shared_vocabulary.json"         = Join-Path $Models "$mt\shared_vocabulary.json"
    "$mt/model.bin"                      = Join-Path $Models "$mt\model.bin"
    "$ocr/ATTRIBUTION.txt"               = Join-Path $licenses "ppocr-ATTRIBUTION.txt"
    "$ocr/LICENSE-Apache-2.0.txt"        = Join-Path $licenses "LICENSE-Apache-2.0.txt"
    "$ocr/PP-OCRv6_rec_small_dict.txt"   = Join-Path $Models "$ocr\PP-OCRv6_rec_small_dict.txt"
    "$ocr/PP-OCRv6_rec_small.onnx"       = Join-Path $Models "$ocr\PP-OCRv6_rec_small.onnx"
}
# Model files must be the exact ones the engines were measured with (custom/subs/mt/README.md,
# custom/subs/ocr/README.md). A different conversion needs a new asset name (v2).
$expected = @{
    "$mt/model.bin"              = "a5a148aae553f2573fd1f9ba735a03385f050437f98e51f8b5c4e4f9c4de3719"
    "$mt/config.json"            = "72901fbd8abd89fb5cf4a388f26fc681f5c4c58a1e1a88b30b879f107270e7ee"
    "$mt/shared_vocabulary.json" = "44dd886d9f538a4b22e58d89758bd336e73b06c753a436f7531436daafcaedee"
    "$mt/source.spm"             = "180d3f94ddfee5585d78b702290ded0f4f9bd40f299349c8383b32a579fefad4"
    "$mt/target.spm"             = "cd0c475692178b6c626aadaef01bd8f98d8053946938e4e67005fc1e4811d797"
    "$ocr/PP-OCRv6_rec_small.onnx"     = "6f327246b50388f3c176ae304bd95767ea6dc0c9ae92153ef8cbe210b3c14884"
    "$ocr/PP-OCRv6_rec_small_dict.txt" = "b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d"
}

$files = @()
foreach ($name in $entries.Keys) {
    $src = $entries[$name]
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { throw "Missing: $src" }
    $hash = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected.ContainsKey($name) -and $expected[$name] -ne $hash) {
        throw "$name has SHA-256 $hash, expected $($expected[$name]) (wrong model version?)"
    }
    $files += [pscustomobject]@{ Path = $name; Source = $src; Size = (Get-Item -LiteralPath $src).Length; Sha256 = $hash }
}
$files | Format-Table Path, Size, Sha256 -AutoSize | Out-String -Width 200 | Write-Host
if ($DryRun) { return }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$zipPath = Join-Path $OutDir $assetName
$tmp = "$zipPath.tmp"
Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.IO.Compression
$stamp = [DateTimeOffset]::new(2026, 11, 12, 0, 0, 0, [TimeSpan]::Zero)
$stream = [IO.File]::Open($tmp, [IO.FileMode]::CreateNew)
try {
    $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($f in $files) {
            Write-Host "packing $($f.Path)"
            $entry = $zip.CreateEntry($f.Path, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $stamp
            $out = $entry.Open()
            try {
                $in = [IO.File]::OpenRead($f.Source)
                try { $in.CopyTo($out, 1MB) } finally { $in.Dispose() }
            } finally { $out.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $stream.Dispose() }
Move-Item -LiteralPath $tmp -Destination $zipPath -Force

$zipSize = (Get-Item -LiteralPath $zipPath).Length
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$zipHash *$assetName" | Set-Content -LiteralPath "$zipPath.sha256" -Encoding ASCII

$json = [ordered]@{
    asset  = $assetName
    size   = $zipSize
    sha256 = $zipHash
    files  = @($files | ForEach-Object { [ordered]@{ path = $_.Path; size = $_.Size; sha256 = $_.Sha256 } })
}
($json | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath (Join-Path $OutDir "crispplay-subs-models-v1.manifest.json") -Encoding UTF8

$lines = @(
    "// Generated by custom/scripts/package-subs-models.ps1. Do not edit by hand: rebuild the zip",
    "// and publish it together with this file (custom/subs/net/README.md).",
    "// Included by ModelStore.cpp inside namespace crispplay::subs::detail.",
    "",
    "inline constexpr char kModelsAssetName[] = `"$assetName`";",
    "inline constexpr long long kModelsZipSize = ${zipSize}LL;",
    "inline constexpr char kModelsZipSha256[] = `"$zipHash`";",
    "inline constexpr GeneratedModelFile kModelsFiles[] = {"
)
foreach ($f in $files) { $lines += "    {`"$($f.Path)`", $($f.Size)LL, `"$($f.Sha256)`"}," }
$lines += "};"
[IO.File]::WriteAllText($manifestInc, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))

Write-Host ""
Write-Host "Zip:     $zipPath"
Write-Host "Size:    $zipSize bytes"
Write-Host "SHA-256: $zipHash"
Write-Host "Manifest: $manifestInc"

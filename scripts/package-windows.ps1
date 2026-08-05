param(
    [string]$Version,
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$DistRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "dist"))
$PyProject = Join-Path $ProjectRoot "pyproject.toml"

if ([string]::IsNullOrWhiteSpace($Version)) {
    $VersionMatch = [regex]::Match(
        (Get-Content -LiteralPath $PyProject -Raw -Encoding UTF8),
        '(?m)^version\s*=\s*"([^"]+)"'
    )
    if (-not $VersionMatch.Success) {
        throw "Cannot read the release version from pyproject.toml"
    }
    $Version = $VersionMatch.Groups[1].Value
}
if ($Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "Invalid release version: $Version"
}

$PackageName = "IsaacSeedSeeker-v$Version-windows-x64"
$StageDir = [IO.Path]::GetFullPath((Join-Path $DistRoot $PackageName))
$ArchivePath = [IO.Path]::GetFullPath((Join-Path $DistRoot "$PackageName.zip"))
$ChecksumPath = [IO.Path]::GetFullPath((Join-Path $DistRoot "SHA256SUMS.txt"))
$DistPrefix = $DistRoot.TrimEnd('\') + '\'
if (-not $StageDir.StartsWith($DistPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to stage outside the dist directory: $StageDir"
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build-native.ps1") -Configuration $Configuration -RunTests
}

$BuiltExe = Join-Path $ProjectRoot "build\native\IsaacSeedSeeker.exe"
if (-not (Test-Path -LiteralPath $BuiltExe -PathType Leaf)) {
    throw "Native executable not found. Run scripts/build-native.ps1 first."
}

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
if (Test-Path -LiteralPath $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
if (Test-Path -LiteralPath $ArchivePath) {
    Remove-Item -LiteralPath $ArchivePath -Force
}

$LicensesDir = Join-Path $StageDir "LICENSES"
New-Item -ItemType Directory -Force -Path $LicensesDir | Out-Null
$PackagedExe = Join-Path $StageDir "IsaacSeedSeeker.exe"
$UsageFileName = -join ([char[]]@(0x4f7f, 0x7528, 0x8bf4, 0x660e))
$UsageFileName += ".txt"
Copy-Item -LiteralPath $BuiltExe -Destination $PackagedExe
Copy-Item -LiteralPath (Join-Path $ProjectRoot "docs\quick-start.zh-CN.txt") -Destination (Join-Path $StageDir $UsageFileName)
Copy-Item -LiteralPath (Join-Path $ProjectRoot "packaging\windows\NOTICE.txt") -Destination (Join-Path $StageDir "NOTICE.txt")
Copy-Item -LiteralPath (Join-Path $ProjectRoot "LICENSE") -Destination (Join-Path $StageDir "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $ProjectRoot "assets\fonts\LICENSE-LanaPixel.txt") -Destination (Join-Path $LicensesDir "LanaPixel-OFL-1.1.txt")
Copy-Item -LiteralPath (Join-Path $ProjectRoot "assets\fonts\LICENSE-IsaacSans.txt") -Destination (Join-Path $LicensesDir "IsaacSans-Public-Domain.txt")
Copy-Item -LiteralPath (Join-Path $ProjectRoot "data\catalog\NOTICE.md") -Destination (Join-Path $LicensesDir "Item-Catalog-CC0.md")

$ExeHash = (Get-FileHash -LiteralPath $PackagedExe -Algorithm SHA256).Hash.ToLowerInvariant()
$Manifest = [ordered]@{
    format_version = 1
    product = "Isaac Seed Seeker"
    version = $Version
    platform = "windows"
    architecture = "x64"
    executable = "IsaacSeedSeeker.exe"
    executable_sha256 = $ExeHash
    game_profile = "j460-full-unlock"
    game_version = "v1.9.7.17.J460"
    package_kind = "portable"
    built_utc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
}
$Manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $StageDir "manifest.json") -Encoding UTF8

Write-Host "Running WebUI smoke test against the staged executable..."
& (Join-Path $PSScriptRoot "smoke-webui.ps1") -Executable $PackagedExe | Out-Host

Compress-Archive -LiteralPath $StageDir -DestinationPath $ArchivePath -CompressionLevel Optimal

$VerifyRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ("isaac-seed-seeker-package-" + [Guid]::NewGuid().ToString("N"))))
$TempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\isaac-seed-seeker-package-'
if (-not $VerifyRoot.StartsWith($TempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use an unexpected verification directory: $VerifyRoot"
}
try {
    New-Item -ItemType Directory -Force -Path $VerifyRoot | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $VerifyRoot
    $ExtractedExe = Join-Path $VerifyRoot "$PackageName\IsaacSeedSeeker.exe"
    if (-not (Test-Path -LiteralPath $ExtractedExe -PathType Leaf)) {
        throw "The archive is missing IsaacSeedSeeker.exe"
    }
    $ExtractedLicense = Join-Path $VerifyRoot "$PackageName\LICENSE.txt"
    if (-not (Test-Path -LiteralPath $ExtractedLicense -PathType Leaf)) {
        throw "The archive is missing LICENSE.txt"
    }
    $ExtractedHash = (Get-FileHash -LiteralPath $ExtractedExe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ExtractedHash -ne $ExeHash) {
        throw "The archived executable hash does not match the staged executable"
    }
    Write-Host "Running WebUI smoke test against the extracted archive..."
    & (Join-Path $PSScriptRoot "smoke-webui.ps1") -Executable $ExtractedExe | Out-Host
} finally {
    if (Test-Path -LiteralPath $VerifyRoot) {
        Remove-Item -LiteralPath $VerifyRoot -Recurse -Force
    }
}

$ArchiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
@(
    "$ArchiveHash  $PackageName.zip"
    "$ExeHash  $PackageName/IsaacSeedSeeker.exe"
) | Set-Content -LiteralPath $ChecksumPath -Encoding ascii

[pscustomobject]@{
    version = $Version
    package = $ArchivePath
    package_sha256 = $ArchiveHash
    executable_sha256 = $ExeHash
    size_bytes = (Get-Item -LiteralPath $ArchivePath).Length
}

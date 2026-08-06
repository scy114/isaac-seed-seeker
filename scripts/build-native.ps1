param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$OutputDirectory = Join-Path $ProjectRoot "build\native"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$Compiler = Get-Command g++ -ErrorAction Stop
$Optimization = if ($Configuration -eq "Release") { "-O3" } else { "-O0" }
$DebugFlag = if ($Configuration -eq "Debug") { "-g" } else { "-DNDEBUG" }
$Common = @(
    "-std=c++20",
    $Optimization,
    $DebugFlag,
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-pthread",
    "-I$ProjectRoot\native\include"
)
$CoreSources = @(
    "$ProjectRoot\native\generated\builtin_profile_j460.cpp",
    "$ProjectRoot\native\src\core.cpp",
    "$ProjectRoot\native\src\daily.cpp",
    "$ProjectRoot\native\src\profile_tables.cpp"
)

$ResourceCompiler = Get-Command windres -ErrorAction Stop
$ResourceObject = "$OutputDirectory\web-resources.o"
& $ResourceCompiler.Source `
    -I "$ProjectRoot\native\resources" `
    -i "$ProjectRoot\native\resources\web.rc" `
    -o $ResourceObject
if ($LASTEXITCODE -ne 0) { throw "web resource build failed" }

& $Compiler.Source @Common @CoreSources `
    "$ProjectRoot\native\src\main.cpp" `
    "$ProjectRoot\native\src\local_web_app_win.cpp" `
    $ResourceObject `
    -static -static-libgcc -static-libstdc++ `
    -lws2_32 -lshell32 -ladvapi32 `
    -o "$OutputDirectory\IsaacSeedSeeker.exe"
if ($LASTEXITCODE -ne 0) { throw "native CLI build failed" }

& $Compiler.Source @Common @CoreSources "$ProjectRoot\native\tests\core_tests.cpp" `
    -o "$OutputDirectory\isaac-seed-core-tests.exe"
if ($LASTEXITCODE -ne 0) { throw "native tests build failed" }

if ($RunTests) {
    $TestArguments = @()
    $ProcTable = "$ProjectRoot\data\profiles\j460-full\proc.json"
    $TrinketPool = "$ProjectRoot\data\profiles\j460-full\trinket_pool.json"
    if ((Test-Path -LiteralPath $ProcTable) -and (Test-Path -LiteralPath $TrinketPool)) {
        $TestArguments = @($ProcTable, $TrinketPool)
    }
    & "$OutputDirectory\isaac-seed-core-tests.exe" @TestArguments
    if ($LASTEXITCODE -ne 0) { throw "native tests failed" }
}

Write-Host "Built $OutputDirectory\IsaacSeedSeeker.exe"

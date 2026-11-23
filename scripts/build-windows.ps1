# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

param([switch] $Headless, [string] $CoreDirectory)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$build = Join-Path $project 'build/windows'
$core = if ($CoreDirectory) {
    (Resolve-Path -LiteralPath $CoreDirectory).Path
} else {
    Join-Path $project 'core/windows-x64'
}
$pc = if ($CoreDirectory -or (Test-Path -LiteralPath (Join-Path $core 'slx_seabalance_model.dll') -PathType Leaf)) {
    'ON'
} else {
    'OFF'
}
$desktop = if ($Headless) { 'OFF' } else { $pc }
cmake -S $project -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DSLX_CORE_DIR=$core" "-DSLX_BUILD_PC=$pc" "-DSLX_BUILD_DESKTOP=$desktop"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config Release --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir $build -C Release --output-on-failure
exit $LASTEXITCODE

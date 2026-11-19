# Build both original Pico W (RP2040) devices in a separate directory.
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$cmakeExe = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninjaExe = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmakeExe -S $projectDir -B (Join-Path $projectDir 'build_wireless') -G Ninja -DPICO_BOARD=pico_w "-DCMAKE_MAKE_PROGRAM=$ninjaExe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ninjaExe -C (Join-Path $projectDir 'build_wireless') SPORTS_GROUND SPORTS_CONTROLLER
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'VGA Pico W: build_wireless/SPORTS_GROUND.uf2'
Write-Host 'IMU + joystick Pico W: build_wireless/SPORTS_CONTROLLER.uf2'

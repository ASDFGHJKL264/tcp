param(
    [string]$QtPrefix = 'D:/Qt6/6.10.3/mingw_64',
    [string]$CompilerBin = 'D:/Qt6/Tools/mingw1310_64/bin',
    [string]$CMake = 'D:/Qt6/Tools/CMake_64/bin/cmake.exe',
    [string]$Ninja = 'D:/Qt6/Tools/Ninja/ninja.exe',
    [ValidateRange(3, 1000)][int]$LifecycleCycles = 3
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$ctest = Join-Path (Split-Path $CMake -Parent) 'ctest.exe'
$compiler = Join-Path $CompilerBin 'g++.exe'
foreach ($tool in @($CMake, $ctest, $compiler, $Ninja)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Missing tool: $tool" }
}
$originalPath = $env:PATH
$originalCycles = $env:TCP_LIFECYCLE_CYCLES
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
try {
    $env:PATH = "$CompilerBin;$QtPrefix/bin;$originalPath"
    $env:TCP_LIFECYCLE_CYCLES = "$LifecycleCycles"
    $stages = @(
        @{ Source = '.'; Build = 'integration-quality'; Test = $true },
        @{ Source = 'TcpDeviceMonitorClient'; Build = 'client-quality'; Test = $true },
        @{ Source = 'CommonProtocol'; Build = 'protocol-quality'; Test = $true },
        @{ Source = 'DeviceSimulator'; Build = 'simulator-quality'; Test = $false }
    )
    foreach ($stage in $stages) {
        $source = Join-Path $projectRoot $stage.Source
        $build = Join-Path $projectRoot "build/$($stage.Build)"
        Invoke-Checked $CMake @('-S', $source, '-B', $build, '-G', 'Ninja',
            "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_CXX_COMPILER=$compiler",
            "-DCMAKE_PREFIX_PATH=$QtPrefix", '-DCMAKE_BUILD_TYPE=Debug')
        Invoke-Checked $CMake @('--build', $build, '--parallel', '4')
        if ($stage.Test) {
            Invoke-Checked $ctest @('--test-dir', $build, '--output-on-failure',
                '--output-junit', (Join-Path $build 'test-results.xml'))
        }
    }
} finally {
    $env:PATH = $originalPath
    $env:TCP_LIFECYCLE_CYCLES = $originalCycles
}

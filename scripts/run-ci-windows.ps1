$ErrorActionPreference = "Stop"

$Stage = if ($args.Length -gt 0) { $args[0] } else { "all" }
$Platform = if ($env:WINDOWS_CMAKE_PLATFORM) { $env:WINDOWS_CMAKE_PLATFORM } else { "Win32" }
$DefaultTriplet = if ($Platform -eq "x64") { "x64-windows" } else { "x86-windows" }
$Triplet = if ($env:VCPKG_TARGET_TRIPLET) { $env:VCPKG_TARGET_TRIPLET } else { $DefaultTriplet }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-ci-$Triplet" }
$InstallDir = if ($env:INSTALL_DIR) { $env:INSTALL_DIR } else { Join-Path $BuildDir "install" }
$DownstreamBuildDir = if ($env:DOWNSTREAM_BUILD_DIR) { $env:DOWNSTREAM_BUILD_DIR } else { Join-Path $BuildDir "downstream-consumer" }
$DownstreamSourceDir = if ($env:DOWNSTREAM_SOURCE_DIR) { $env:DOWNSTREAM_SOURCE_DIR } else { "test/downstream_consumer" }
$ToolchainFile = if ($env:CMAKE_TOOLCHAIN_FILE) {
    $env:CMAKE_TOOLCHAIN_FILE
} elseif ($env:VCPKG_ROOT) {
    Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
} elseif (Test-Path "C:\vcpkg\scripts\buildsystems\vcpkg.cmake") {
    "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
} else {
    ""
}

function Run-Configure {
    $CmakeArgs = @(
        "-S", ".",
        "-B", $BuildDir,
        "-A", $Platform,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DVCPKG_TARGET_TRIPLET=$Triplet",
        "-DENT_ENABLE_SQLITE=ON",
        "-DENT_ENABLE_MYSQL=ON",
        "-DENT_ENABLE_PGSQL=ON"
    )
    if ($ToolchainFile) {
        $CmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"
    }
    cmake @CmakeArgs
}

function Run-Build {
    cmake --build $BuildDir --config Release
}

function Run-Test {
    Push-Location $BuildDir
    try {
        ctest -C Release --output-on-failure `
            -R "test_ent_init|test_ent_msg|test_utl_dll|test_utl_thread|test_utl_lock_cv|test_ent_thread|test_ent_thread_failures|test_utl_tpool|test_utl_tpool_lifecycle|test_utl_tpool_integration|test_utl_timer|test_utl_socket|test_ent_db|test_ent_log|test_ent_shm|test_security"
    }
    finally {
        Pop-Location
    }
}

function Run-InstallConsumer {
    cmake --install $BuildDir --config Release --prefix $InstallDir
    $PrefixPath = [System.IO.Path]::GetFullPath($InstallDir)
    $EntPackageDir = Join-Path $PrefixPath "CMake"
    $DownstreamSourcePath = [System.IO.Path]::GetFullPath($DownstreamSourceDir)
    $DownstreamBuildPath = [System.IO.Path]::GetFullPath($DownstreamBuildDir)

    cmake -S $DownstreamSourcePath -B $DownstreamBuildPath -A $Platform -DCMAKE_BUILD_TYPE=Release "-Dent_DIR=$EntPackageDir"
    cmake --build $DownstreamBuildPath --config Release
    & "$DownstreamBuildPath\Release\ent_downstream_consumer.exe"
}

function Invoke-PerfBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [int]$TimeoutSeconds = 180
    )

    $Name = Split-Path $Path -Leaf
    Write-Host "Running $Name"
    $ResolvedPath = [System.IO.Path]::GetFullPath($Path)

    $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $ResolvedPath
    $StartInfo.WorkingDirectory = Split-Path $ResolvedPath -Parent
    $StartInfo.UseShellExecute = $false
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true

    $Process = [System.Diagnostics.Process]::new()
    $Process.StartInfo = $StartInfo

    try {
        if (-not $Process.Start()) {
            throw "Failed to start $Name"
        }
        if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            throw "$Name timed out after $TimeoutSeconds seconds"
        }

        $StdOutText = $Process.StandardOutput.ReadToEnd()
        $StdErrText = $Process.StandardError.ReadToEnd()
        if ($StdOutText) { Write-Output $StdOutText.TrimEnd() }
        if ($StdErrText) { [Console]::Error.WriteLine($StdErrText.TrimEnd()) }

        if ($Process.ExitCode -ne 0) {
            throw "$Name exited with code $($Process.ExitCode)"
        }
    }
    finally {
        $Process.Dispose()
    }
}

function Run-Perf {
    Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_utl_socket.exe"
    Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_utl_timer.exe"
    Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_utl_tpool.exe"
    Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_ent_log.exe"
}

switch ($Stage) {
    "configure" { Run-Configure }
    "build" { Run-Build }
    "test" { Run-Test }
    "install-consumer" { Run-InstallConsumer }
    "perf" { Run-Perf }
    "all" {
        Run-Configure
        Run-Build
        Run-Test
        Run-InstallConsumer
        Run-Perf
    }
    default {
        throw "Unknown stage: $Stage"
    }
}

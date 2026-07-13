$ErrorActionPreference = "Stop"

$Stage = if ($args.Length -gt 0) { $args[0] } else { "all" }
$Platform = if ($env:WINDOWS_CMAKE_PLATFORM) { $env:WINDOWS_CMAKE_PLATFORM } else { "Win32" }
$DefaultTriplet = if ($Platform -eq "x64") { "x64-windows" } else { "x86-windows" }
$Triplet = if ($env:VCPKG_TARGET_TRIPLET) { $env:VCPKG_TARGET_TRIPLET } else { $DefaultTriplet }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-ci-$Triplet" }
$InstallDir = if ($env:INSTALL_DIR) { $env:INSTALL_DIR } else { Join-Path $BuildDir "install" }
$DownstreamBuildDir = if ($env:DOWNSTREAM_BUILD_DIR) { $env:DOWNSTREAM_BUILD_DIR } else { Join-Path $BuildDir "downstream-consumer" }
$DownstreamSourceDir = if ($env:DOWNSTREAM_SOURCE_DIR) { $env:DOWNSTREAM_SOURCE_DIR } else { "test/downstream_consumer" }
$LogDir = if ($env:CI_LOG_DIR) { [System.IO.Path]::GetFullPath($env:CI_LOG_DIR) } else { "" }
$EnableSqlite = if ($env:ENT_ENABLE_SQLITE) { $env:ENT_ENABLE_SQLITE } else { "ON" }
$EnableMysql = if ($env:ENT_ENABLE_MYSQL) { $env:ENT_ENABLE_MYSQL } else { "ON" }
$EnablePgsql = if ($env:ENT_ENABLE_PGSQL) { $env:ENT_ENABLE_PGSQL } else { "ON" }
$ToolchainFile = if ($env:CMAKE_TOOLCHAIN_FILE) {
    $env:CMAKE_TOOLCHAIN_FILE
} elseif ($env:VCPKG_ROOT) {
    Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
} elseif (Test-Path "C:\vcpkg\scripts\buildsystems\vcpkg.cmake") {
    "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
} else {
    ""
}

function Test-Enabled {
    param([string]$Value)
    return $Value -match "^(1|ON|TRUE|YES|Y)$"
}

function Invoke-Logged {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogName,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    if ([string]::IsNullOrWhiteSpace($LogDir)) {
        & $Action
        return
    }

    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    $LogPath = Join-Path $LogDir "$LogName.log"
    & $Action *>&1 | Tee-Object -FilePath $LogPath

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Run-Configure {
    $CmakeArgs = @(
        "-S", ".",
        "-B", $BuildDir,
        "-A", $Platform,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DVCPKG_TARGET_TRIPLET=$Triplet",
        "-DENT_ENABLE_SQLITE=$EnableSqlite",
        "-DENT_ENABLE_MYSQL=$EnableMysql",
        "-DENT_ENABLE_PGSQL=$EnablePgsql"
    )

    if ($env:ENT_ENABLE_ASAN -eq "ON") {
        $CmakeArgs += "-DENT_ENABLE_ASAN=ON"
    }

    if ($ToolchainFile) {
        $ManifestFeatures = @()
        if (Test-Enabled $EnableSqlite) { $ManifestFeatures += "sqlite" }
        if (Test-Enabled $EnableMysql) { $ManifestFeatures += "mysql" }
        if (Test-Enabled $EnablePgsql) { $ManifestFeatures += "pgsql" }

        $CmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"
        $CmakeArgs += "-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON"
        if ($ManifestFeatures.Count -gt 0) {
            $CmakeArgs += "-DVCPKG_MANIFEST_FEATURES=$($ManifestFeatures -join ';')"
        }
        Write-Host "vcpkg manifest features: $($ManifestFeatures -join ', ')"
    }

    Write-Host "Database backends: SQLite=$EnableSqlite MySQL=$EnableMysql PostgreSQL=$EnablePgsql"
    Invoke-Logged "configure" { cmake @CmakeArgs }
}

function Run-Build {
    Invoke-Logged "build" { cmake --build $BuildDir --config Release }
}

function Run-Test {
    Invoke-Logged "test" {
        Push-Location $BuildDir
        try {
            ctest -C Release --output-on-failure `
                -R "test_ent_init|test_ent_msg|test_utl_dll|test_utl_thread|test_utl_lock_cv|test_ent_thread|test_ent_thread_failures|test_utl_tpool|test_utl_tpool_lifecycle|test_utl_tpool_integration|test_utl_timer|test_utl_socket|test_ent_db|test_ent_log|test_ent_shm|test_security|test_ent_optional_backends"
        }
        finally {
            Pop-Location
        }
    }
}

function Run-InstallConsumer {
    Invoke-Logged "install-consumer" {
        cmake --install $BuildDir --config Release --prefix $InstallDir
        $PrefixPath = [System.IO.Path]::GetFullPath($InstallDir)
        $EntPackageDir = Join-Path $PrefixPath "CMake"
        $DownstreamSourcePath = [System.IO.Path]::GetFullPath($DownstreamSourceDir)
        $DownstreamBuildPath = [System.IO.Path]::GetFullPath($DownstreamBuildDir)

        $ConsumerArgs = @(
            "-S", $DownstreamSourcePath,
            "-B", $DownstreamBuildPath,
            "-A", $Platform,
            "-DCMAKE_BUILD_TYPE=Release",
            "-Dent_DIR=$EntPackageDir",
            "-DVCPKG_TARGET_TRIPLET=$Triplet"
        )
        if ($ToolchainFile) {
            $ConsumerArgs += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"
            $ConsumerArgs += "-DVCPKG_INSTALLED_DIR=$([System.IO.Path]::GetFullPath((Join-Path $BuildDir 'vcpkg_installed')))"
        }
        cmake @ConsumerArgs
        cmake --build $DownstreamBuildPath --config Release
        & "$DownstreamBuildPath\Release\ent_downstream_consumer.exe"
    }
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
    Invoke-Logged "perf" {
        Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_utl_socket.exe"
        Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_utl_timer.exe"
        Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_utl_tpool.exe"
        Invoke-PerfBinary ".\$BuildDir\bin\Release\perf_ent_log.exe"
    }
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

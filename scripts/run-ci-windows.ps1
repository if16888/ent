$ErrorActionPreference = "Stop"

$Stage = if ($args.Length -gt 0) { $args[0] } else { "all" }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-ci" }
$Platform = if ($env:WINDOWS_CMAKE_PLATFORM) { $env:WINDOWS_CMAKE_PLATFORM } else { "Win32" }
$DisablePostgreSQL = if ($env:WINDOWS_DISABLE_PGSQL) { $env:WINDOWS_DISABLE_PGSQL } else { "" }
$InstallDir = if ($env:INSTALL_DIR) { $env:INSTALL_DIR } else { Join-Path $BuildDir "install" }
$DownstreamBuildDir = if ($env:DOWNSTREAM_BUILD_DIR) { $env:DOWNSTREAM_BUILD_DIR } else { Join-Path $BuildDir "downstream-consumer" }
$DownstreamSourceDir = if ($env:DOWNSTREAM_SOURCE_DIR) { $env:DOWNSTREAM_SOURCE_DIR } else { "test/downstream_consumer" }

function Run-Configure {
    $CmakeArgs = @(
        "-S", ".",
        "-B", $BuildDir,
        "-A", $Platform,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DENT_ENABLE_SQLITE=ON",
        "-DENT_ENABLE_MYSQL=ON",
        "-DENT_ALLOW_VENDORED_DB_LIBS=ON"
    )
    if ($DisablePostgreSQL -and $DisablePostgreSQL.ToLower() -notin @("0", "off", "false")) {
        $CmakeArgs += "-DCMAKE_DISABLE_FIND_PACKAGE_PostgreSQL=ON"
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
            -R "test_ent_init|test_utl_dll|test_utl_thread|test_utl_lock_cv|test_ent_thread|test_utl_tpool|test_utl_timer|test_utl_socket|test_ent_db|test_ent_log|test_security"
    }
    finally {
        Pop-Location
    }
}

function Run-InstallConsumer {
    cmake --install $BuildDir --config Release --prefix $InstallDir
    $PrefixPath = [System.IO.Path]::GetFullPath($InstallDir)
    $DownstreamSourcePath = [System.IO.Path]::GetFullPath($DownstreamSourceDir)
    $DownstreamBuildPath = [System.IO.Path]::GetFullPath($DownstreamBuildDir)

    cmake -S $DownstreamSourcePath -B $DownstreamBuildPath -A $Platform -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$PrefixPath
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
    $StdOut = [System.IO.Path]::GetTempFileName()
    $StdErr = [System.IO.Path]::GetTempFileName()

    try {
        $Process = Start-Process -FilePath $Path `
            -RedirectStandardOutput $StdOut `
            -RedirectStandardError $StdErr `
            -PassThru
        if ($null -eq $Process) {
            throw "Failed to start $Name"
        }

        if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            if (Test-Path $StdOut) { Get-Content $StdOut }
            if ((Test-Path $StdErr) -and ((Get-Item $StdErr).Length -gt 0)) {
                Get-Content $StdErr | ForEach-Object { [Console]::Error.WriteLine($_) }
            }
            throw "$Name timed out after $TimeoutSeconds seconds"
        }

        if (Test-Path $StdOut) { Get-Content $StdOut }
        if ((Test-Path $StdErr) -and ((Get-Item $StdErr).Length -gt 0)) {
            Get-Content $StdErr | ForEach-Object { [Console]::Error.WriteLine($_) }
        }

        if ($Process.ExitCode -ne 0) {
            throw "$Name exited with code $($Process.ExitCode)"
        }
    }
    finally {
        Remove-Item $StdOut, $StdErr -Force -ErrorAction SilentlyContinue
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

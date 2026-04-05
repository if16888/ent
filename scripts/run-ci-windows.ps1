$ErrorActionPreference = "Stop"

$Stage = if ($args.Length -gt 0) { $args[0] } else { "all" }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-ci" }
$Platform = if ($env:WINDOWS_CMAKE_PLATFORM) { $env:WINDOWS_CMAKE_PLATFORM } else { "Win32" }

function Run-Configure {
    cmake -S . -B $BuildDir -A $Platform `
        -DCMAKE_BUILD_TYPE=Release `
        -DENT_ENABLE_SQLITE=ON `
        -DENT_ENABLE_MYSQL=ON `
        -DENT_ALLOW_VENDORED_DB_LIBS=ON
}

function Run-Build {
    cmake --build $BuildDir --config Release
}

function Run-Test {
    Push-Location $BuildDir
    try {
        ctest -C Release --output-on-failure `
            -R "test_ent_init|test_utl_dll|test_utl_thread|test_ent_thread|test_utl_tpool|test_utl_timer|test_utl_socket|test_ent_db|test_ent_log|test_security"
    }
    finally {
        Pop-Location
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

    $Process = Start-Process -FilePath $Path -NoNewWindow -PassThru
    if ($null -eq $Process) {
        throw "Failed to start $Name"
    }

    try {
        Wait-Process -Id $Process.Id -Timeout $TimeoutSeconds -ErrorAction Stop
    }
    catch {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        throw "$Name timed out after $TimeoutSeconds seconds"
    }

    $Process.Refresh()
    if ($Process.ExitCode -ne 0) {
        throw "$Name exited with code $($Process.ExitCode)"
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
    "perf" { Run-Perf }
    "all" {
        Run-Configure
        Run-Build
        Run-Test
        Run-Perf
    }
    default {
        throw "Unknown stage: $Stage"
    }
}

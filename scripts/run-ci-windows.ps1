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
            if ((Test-Path $StdErr) -and ((Get-Item $StdErr).Length -gt 0)) { Get-Content $StdErr | Write-Error }
            throw "$Name timed out after $TimeoutSeconds seconds"
        }

        if (Test-Path $StdOut) { Get-Content $StdOut }
        if ((Test-Path $StdErr) -and ((Get-Item $StdErr).Length -gt 0)) { Get-Content $StdErr | Write-Error }

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

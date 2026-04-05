$ErrorActionPreference = "Stop"

$Stage = if ($args.Length -gt 0) { $args[0] } else { "all" }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-ci" }

function Run-Configure {
    cmake -S . -B $BuildDir -A x64 `
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

function Run-Perf {
    & ".\$BuildDir\bin\Release\perf_utl_socket.exe"
    & ".\$BuildDir\bin\Release\perf_utl_timer.exe"
    & ".\$BuildDir\bin\Release\perf_utl_tpool.exe"
    & ".\$BuildDir\bin\Release\perf_ent_log.exe"
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

param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$ConsumerSource,
    [ValidateSet("Win32", "x64")][string]$Platform = "x64",
    [string]$Triplet = "x64-windows",
    [string]$ToolchainFile = "",
    [string]$InstalledDir = "",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"
$Root = Join-Path ([System.IO.Path]::GetTempPath()) ("ent-sdk-" + [guid]::NewGuid().ToString("N"))
$Sdk = Join-Path $Root "sdk"
New-Item -ItemType Directory -Path $Sdk -Force | Out-Null

try {
    Expand-Archive -LiteralPath $Archive -DestinationPath $Sdk -Force

    $CmakeFiles = Get-ChildItem -LiteralPath $Sdk -Recurse -File -Filter "*.cmake"
    $Forbidden = @("vcpkg_installed", "D:/a/", "C:\a\", "/home/runner/work/")
    if ($env:GITHUB_WORKSPACE) { $Forbidden += $env:GITHUB_WORKSPACE }
    if ($env:BUILD_DIR) { $Forbidden += [System.IO.Path]::GetFullPath($env:BUILD_DIR) }
    foreach ($Pattern in $Forbidden) {
        $Matches = $CmakeFiles | Select-String -SimpleMatch $Pattern
        if ($Matches) {
            $Matches | ForEach-Object { Write-Error $_.Line }
            throw "Exported CMake metadata contains forbidden path: $Pattern"
        }
    }

    $EntDir = Join-Path $Sdk "CMake"
    foreach ($Linkage in @("shared", "static")) {
        $Build = Join-Path $Root "consumer-$Linkage"
        $Args = @(
            "-S", $ConsumerSource,
            "-B", $Build,
            "-DCMAKE_BUILD_TYPE=Release",
            "-Dent_DIR=$EntDir",
            "-DENT_CONSUMER_LINKAGE=$Linkage"
        )
        if ($Generator) {
            $Args += @("-G", $Generator)
        }
        else {
            $Args += @("-A", $Platform)
        }
        if ($ToolchainFile) {
            $Args += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"
            $Args += "-DVCPKG_TARGET_TRIPLET=$Triplet"
        }
        if ($InstalledDir) {
            $Args += "-DVCPKG_MANIFEST_MODE=OFF"
            $Args += "-DVCPKG_INSTALLED_DIR=$InstalledDir"
        }
        cmake @Args
        if ($LASTEXITCODE -ne 0) { throw "$Linkage consumer configure failed" }
        if ($Generator) {
            cmake --build $Build
        }
        else {
            cmake --build $Build --config Release
        }
        if ($LASTEXITCODE -ne 0) { throw "$Linkage consumer build failed" }

        $OldPath = $env:PATH
        try {
            $env:PATH = "$(Join-Path $Sdk 'bin');$OldPath"
            $Executable = if ($Generator) {
                Join-Path $Build "ent_downstream_consumer.exe"
            }
            else {
                Join-Path $Build "Release\ent_downstream_consumer.exe"
            }
            & $Executable
            if ($LASTEXITCODE -ne 0) { throw "$Linkage consumer run failed" }
        }
        finally {
            $env:PATH = $OldPath
        }
    }
}
finally {
    Remove-Item -LiteralPath $Root -Recurse -Force -ErrorAction SilentlyContinue
}

param(
    [string]$WorkspaceRoot = "/home/lf/workspace",
    [string]$RepoName = "ent",
    [string]$Stage = "all",
    [switch]$NoSync
)

$ErrorActionPreference = "Stop"

function Invoke-Wsl {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command
    )

    & wsl.exe @('bash', '-lc', $Command)
}

function ConvertTo-WslPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $normalized = $Path -replace '\\', '/'
    if($normalized -match '^(?<drive>[A-Za-z]):/(?<rest>.*)$')
    {
        return "/mnt/$($matches.drive.ToLower())/$($matches.rest)"
    }

    return $normalized
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repoWsl = ConvertTo-WslPath -Path $repoRoot
$workspaceRoot = $WorkspaceRoot.TrimEnd("/")
$targetWsl = "$workspaceRoot/$RepoName"

if(-not $NoSync.IsPresent)
{
    $syncCommand = "set -euo pipefail; mkdir -p '$workspaceRoot'; rm -rf '$targetWsl'; mkdir -p '$targetWsl'; cp -a '$repoWsl'/. '$targetWsl'/; rm -rf '$targetWsl'/build* '$targetWsl'/build-win-x64 '$targetWsl'/build-win-x86"
    Invoke-Wsl -Command $syncCommand
}

$runCommand = "set -euo pipefail; cd '$targetWsl'; chmod +x scripts/run-ci-wsl.sh; ./scripts/run-ci-wsl.sh '$Stage'"

Invoke-Wsl -Command $runCommand

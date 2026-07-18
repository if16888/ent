param(
    [string]$WorkspaceRoot = "",
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

function Assert-SafeWslPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if($Path -notmatch '^/[A-Za-z0-9._/-]+$')
    {
        throw "WorkspaceRoot must be an absolute WSL path containing only letters, digits, '.', '_', '-', and '/'."
    }

    $segments = @($Path.Split('/', [System.StringSplitOptions]::RemoveEmptyEntries))
    if($segments.Count -lt 2 -or $segments -contains '.' -or $segments -contains '..')
    {
        throw "WorkspaceRoot must name a non-root directory with at least two safe path components."
    }
}

if($RepoName -notmatch '^[A-Za-z0-9._-]+$' -or $RepoName -in @('.', '..'))
{
    throw "RepoName contains unsupported characters."
}

if($Stage -notin @('configure', 'build', 'test', 'all'))
{
    throw "Stage must be one of: configure, build, test, all."
}

if([string]::IsNullOrWhiteSpace($WorkspaceRoot))
{
    $wslHome = (Invoke-Wsl -Command 'printf "%s" "$HOME"' | Out-String).Trim()
    if([string]::IsNullOrWhiteSpace($wslHome))
    {
        throw "Could not resolve the WSL user home directory."
    }
    $WorkspaceRoot = "$($wslHome.TrimEnd('/'))/workspace"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repoWsl = ConvertTo-WslPath -Path $repoRoot
$workspaceRoot = $WorkspaceRoot.TrimEnd("/")
Assert-SafeWslPath -Path $workspaceRoot
$targetWsl = "$workspaceRoot/$RepoName"

if(-not $NoSync.IsPresent)
{
    $syncCommand = "set -euo pipefail; mkdir -p '$workspaceRoot'; rm -rf '$targetWsl'; mkdir -p '$targetWsl'; cp -a '$repoWsl'/. '$targetWsl'/; rm -rf '$targetWsl'/build* '$targetWsl'/build-win-x64 '$targetWsl'/build-win-x86"
    Invoke-Wsl -Command $syncCommand
}

$runCommand = "set -euo pipefail; cd '$targetWsl'; chmod +x scripts/run-ci-wsl.sh; ./scripts/run-ci-wsl.sh '$Stage'"

Invoke-Wsl -Command $runCommand

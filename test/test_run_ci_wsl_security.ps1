$ErrorActionPreference = "Stop"

$scriptPath = (Resolve-Path (Join-Path $PSScriptRoot "..\scripts\run-ci-wsl.ps1")).Path
$global:EntTestWslCommands = [System.Collections.Generic.List[string]]::new()

function global:wsl.exe {
    $wslArguments = @($args[0])
    $command = [string]$wslArguments[-1]
    if($command -eq 'printf "%s" "$HOME"')
    {
        "/home/test-user"
        return
    }

    $global:EntTestWslCommands.Add($command)
}

function Assert-Rejected {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedMessage
    )

    $before = $global:EntTestWslCommands.Count
    try
    {
        & $scriptPath @Arguments
        throw "Unsafe arguments were accepted."
    }
    catch
    {
        if($_.Exception.Message -notmatch [regex]::Escape($ExpectedMessage))
        {
            throw
        }
    }

    if($global:EntTestWslCommands.Count -ne $before)
    {
        throw "Rejected arguments reached the WSL command boundary."
    }
}

try
{
    & $scriptPath -NoSync -Stage test
    if($global:EntTestWslCommands.Count -ne 1)
    {
        throw "Expected exactly one WSL test command, got $($global:EntTestWslCommands.Count)."
    }
    if($global:EntTestWslCommands[0] -notmatch "^set -euo pipefail; cd '/home/test-user/workspace/ent';")
    {
        throw "The default workspace was not resolved from the WSL user home."
    }

    Assert-Rejected `
        -Arguments @{ WorkspaceRoot = "/tmp/ent';touch /tmp/injected;#"; NoSync = $true } `
        -ExpectedMessage "WorkspaceRoot must be an absolute WSL path"
    Assert-Rejected `
        -Arguments @{ WorkspaceRoot = "/tmp/ent-ci"; RepoName = "ent';id;#"; NoSync = $true } `
        -ExpectedMessage "RepoName contains unsupported characters"
    Assert-Rejected `
        -Arguments @{ WorkspaceRoot = "/tmp/ent-ci"; Stage = "test';id;#"; NoSync = $true } `
        -ExpectedMessage "Stage must be one of"

    Write-Host "run-ci-wsl.ps1 argument validation tests passed"
}
finally
{
    Remove-Item -LiteralPath Function:\wsl.exe -ErrorAction SilentlyContinue
    Remove-Variable -Name EntTestWslCommands -Scope Global -ErrorAction SilentlyContinue
}

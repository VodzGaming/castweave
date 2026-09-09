param(
    [string]$StagePath = (Join-Path $PSScriptRoot '../stage'),
    [string]$Destination = "$env:ProgramData/obs-studio/plugins/castweave"
)
$ErrorActionPreference = 'Stop'
if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw 'Close OBS before installing or updating CastWeave.'
}
$source = Join-Path $StagePath 'castweave'
if (!(Test-Path (Join-Path $source 'bin/64bit/castweave.dll'))) {
    throw "No staged plugin at $source. Build and stage it first."
}
$resolvedSource = [IO.Path]::GetFullPath($source)
$resolvedDestination = [IO.Path]::GetFullPath($Destination)
if ($resolvedSource -eq $resolvedDestination) { throw 'Source and destination must differ.' }
New-Item -ItemType Directory -Force -Path $resolvedDestination | Out-Null
Copy-Item -Path (Join-Path $resolvedSource '*') -Destination $resolvedDestination -Recurse -Force
Write-Output "Installed CastWeave to $resolvedDestination. Open OBS > Docks > CastWeave."

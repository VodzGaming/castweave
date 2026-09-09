param(
    [string]$StagePath = (Join-Path $PSScriptRoot '../stage'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..'),
    [string]$MakeNsis = ''
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage = [IO.Path]::GetFullPath($StagePath)
$version = (Get-Content -Raw -LiteralPath (Join-Path $root 'buildspec.json') |
    ConvertFrom-Json).version
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "buildspec.json contains an unsupported version: $version"
}
if (-not (Test-Path -LiteralPath (Join-Path $stage 'castweave/bin/64bit/castweave.dll'))) {
    throw "The staged CastWeave plugin is missing. Run cmake --install first."
}
if (-not $MakeNsis) {
    $MakeNsis = @(
        (Get-Command makensis -ErrorAction SilentlyContinue).Source,
        'C:\Program Files (x86)\NSIS\makensis.exe',
        'C:\Program Files\NSIS\makensis.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
        Select-Object -First 1
}
if (-not $MakeNsis) {
    throw 'makensis.exe was not found. Install NSIS or pass -MakeNsis.'
}

$outputDir = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$output = Join-Path $outputDir "CastWeave-$version-windows-x64-Setup.exe"
& $MakeNsis /V3 "/DCASTWEAVE_VERSION=$version" "/DCASTWEAVE_STAGE=$stage" `
    "/DCASTWEAVE_OUTPUT=$output" (Join-Path $root 'installer/CastWeave.nsi')
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output)) {
    throw "CastWeave installer build failed with exit code $LASTEXITCODE."
}

Get-Item -LiteralPath $output | Select-Object FullName, Length

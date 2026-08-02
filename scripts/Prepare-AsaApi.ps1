$ErrorActionPreference = "Stop"

$AsaApiVersion = "2.03"
$ExpectedArchiveSha256 = "ac72fb29436198ac062cd273e1c496b1ef4e6ffddeec08243d11d9b35e8b8ae3"
$RepositoryUrl = "https://github.com/ArkServerApi/AsaApi.git"
$ArchiveUrl = "https://github.com/ArkServerApi/AsaApi/releases/download/$AsaApiVersion/AsaApi_$AsaApiVersion.zip"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ExternRoot = Join-Path $ProjectRoot "extern"
$SourceRoot = Join-Path $ExternRoot "AsaApi"
$LibraryRoot = Join-Path $SourceRoot "out_lib"
$TemporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("GenesisHighWindsRemover-" + [guid]::NewGuid())
$ArchivePath = Join-Path $TemporaryRoot "AsaApi.zip"
$ExpandedRoot = Join-Path $TemporaryRoot "expanded"

New-Item -ItemType Directory -Force -Path $ExternRoot | Out-Null

if (Test-Path $SourceRoot) {
    if (-not (Test-Path (Join-Path $SourceRoot ".git"))) {
        throw "extern\AsaApi exists but is not a Git checkout. Move it aside and run this script again."
    }

    $TagOutput = & git -C $SourceRoot describe --tags --exact-match 2>$null
    $TagExitCode = $LASTEXITCODE
    $CurrentTag = if ($null -eq $TagOutput) { "" } else { ($TagOutput | Out-String).Trim() }
    if ($TagExitCode -ne 0 -or $CurrentTag -ne $AsaApiVersion) {
        throw "extern\AsaApi must be checked out at tag $AsaApiVersion; found '$CurrentTag'."
    }

    $DirtyFiles = & git -C $SourceRoot status --porcelain
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect the existing extern\AsaApi checkout."
    }
    if ($DirtyFiles) {
        throw "extern\AsaApi has local changes. Use a clean checkout at tag $AsaApiVersion."
    }
}
else {
    & git clone --depth 1 --branch $AsaApiVersion $RepositoryUrl $SourceRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone AsaApi $AsaApiVersion."
    }
}

try {
    New-Item -ItemType Directory -Force -Path $TemporaryRoot | Out-Null
    Invoke-WebRequest -Uri $ArchiveUrl -OutFile $ArchivePath

    $ActualHash = (Get-FileHash -Algorithm SHA256 -Path $ArchivePath).Hash.ToLowerInvariant()
    if ($ActualHash -ne $ExpectedArchiveSha256) {
        throw "AsaApi archive checksum mismatch. Expected $ExpectedArchiveSha256, got $ActualHash."
    }

    Expand-Archive -Path $ArchivePath -DestinationPath $ExpandedRoot
    New-Item -ItemType Directory -Force -Path $LibraryRoot | Out-Null
    Copy-Item -Force (Join-Path $ExpandedRoot "Lib\AsaApi.lib") (Join-Path $LibraryRoot "AsaApi.lib")
}
finally {
    if (Test-Path $TemporaryRoot) {
        Remove-Item -Recurse -Force $TemporaryRoot
    }
}

Write-Host "Prepared AsaApi $AsaApiVersion headers and matching import library."

<#
.SYNOPSIS
  Build a release firmware, generate manifest.json and publish both to the update server.

.EXAMPLE
  .\scripts\publish_firmware.ps1 -Version v1.0.0
  .\scripts\publish_firmware.ps1 -Version v1.0.1 -Slug esp32-s3-panel-320 -User deploy
  .\scripts\publish_firmware.ps1 -Version v1.0.0 -SkipCompile     # reuse the last build output
  .\scripts\publish_firmware.ps1 -Version v1.0.0 -DryRun          # build + manifest only, no upload

.NOTES
  Server layout (BaseUrl = http://file.myainet.com/esphome/espcontrol/firmware):
    <RemoteRoot>/<slug>/manifest.json                  <- what devices poll (uploaded last)
    <RemoteRoot>/<slug>/<slug>.ota.bin                 <- OTA image
    <RemoteRoot>/<slug>/<slug>.factory.bin             <- first-time flashing image
    <RemoteRoot>/<slug>/releases/<version>/...         <- archive of every published version

  Requires: esphome, python, and OpenSSH (ssh/scp) with key-based login to the server.
  Run from PowerShell. ESP-IDF builds need the IDF profile; it is dot-sourced automatically
  when found and IDF is not already active.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    # Must look like v1.2.3: the setup page only recognises versions with a leading "v".
    [ValidatePattern('^v\d+\.\d+\.\d+([.-][0-9A-Za-z.-]+)?$')]
    [string]$Version,

    [string]$Slug = 'esp32-s3-panel-320',
    [string]$Chip = '',

    # SSH target. The raw IP is used because the domain may resolve to a proxy fake IP locally.
    [string]$Server       = '47.110.53.106',
    [string]$User         = 'root',
    [int]$Port            = 1994,
    [string]$IdentityFile = 'D:\Workspace\thingsboard\47.110.53.106_id_ed25519',
    # Nginx root for file.myainet.com is /www/wwwroot/file.myainet.com (Baota panel).
    [string]$RemoteRoot   = '/www/wwwroot/file.myainet.com/esphome/espcontrol/firmware',
    [string]$BaseUrl    = 'http://file.myainet.com/esphome/espcontrol/firmware',

    # Defaults to esphome on PATH, else the workspace dev venv (D:\Workspace\esphome\esphome\venv).
    [string]$Esphome    = '',
    [string]$IdfProfile = 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1',

    [switch]$SkipCompile,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Factory = Join-Path $Root "builds\$Slug.factory.yaml"
if (-not (Test-Path $Factory)) { throw "Build config not found: $Factory" }

if (-not $Chip) { $Chip = if ($Slug -match 'p4') { 'ESP32-P4' } else { 'ESP32-S3' } }

if (-not $Esphome) {
    $onPath = Get-Command esphome -ErrorAction SilentlyContinue
    $venv = Join-Path (Split-Path -Parent (Split-Path -Parent $Root)) 'esphome\venv\Scripts\esphome.exe'
    if ($onPath) { $Esphome = $onPath.Source }
    elseif (Test-Path $venv) { $Esphome = $venv }
    elseif ($SkipCompile) { $Esphome = 'esphome' }
    else { throw "esphome not found. Put it on PATH or pass -Esphome <path to esphome.exe>." }
}

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

# --- 1. Compile ------------------------------------------------------------
if (-not $SkipCompile) {
    # IDF_PATH / IDF_TOOLS_PATH may be set machine-wide without an activated IDF Python env,
    # which makes ESPHome fail with "No module named 'esp_idf_monitor'". Only the profile
    # sets IDF_PYTHON_ENV_PATH, so use that to tell whether IDF is really active.
    if (-not $env:IDF_PYTHON_ENV_PATH) {
        if (-not (Test-Path $IdfProfile)) { throw "ESP-IDF profile not found: $IdfProfile (pass -IdfProfile)" }
        Write-Host "Activating ESP-IDF: $IdfProfile"
        . $IdfProfile
    }
    Write-Host "IDF_PATH=$env:IDF_PATH  IDF_PYTHON_ENV_PATH=$env:IDF_PYTHON_ENV_PATH"
    # The factory YAML pins the component source to file:///config (the CI docker mount).
    # Point it at this checkout instead, like scripts/local_esphome.py does. The component
    # is cloned at ref HEAD, so uncommitted changes under components/ are NOT included.
    $ComponentUrl = ([System.Uri]::new($Root)).AbsoluteUri
    # Same for the icon font, which the factory YAML also addresses as /config/common/...
    $MdiFont = (Join-Path $Root 'common\assets\fonts\materialdesignicons-webfont-7.4.47.ttf') -replace '\\', '/'
    if (-not (Test-Path $MdiFont)) { throw "Icon font not found: $MdiFont" }
    $dirty = git -C $Root status --porcelain -- components
    if ($dirty) {
        Write-Warning "Uncommitted changes under components/ will not be in this build (commit them first):`n$dirty"
    }
    Write-Host "==> Compiling $Slug as version $Version"
    Push-Location (Join-Path $Root 'builds')
    try {
        Invoke-Native $Esphome @(
            '-s', 'firmware_version', $Version,
            '-s', 'espcontrol_component_url', $ComponentUrl,
            '-s', 'mdi_font_file', $MdiFont,
            'compile', "$Slug.factory.yaml")
    } finally { Pop-Location }
}

# --- 2. Locate build output ------------------------------------------------
$BuildDir = Join-Path $Root 'builds\.esphome\build'
$FactoryBin = Get-ChildItem -Path $BuildDir -Recurse -Filter 'firmware.factory.bin' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $FactoryBin) { throw "firmware.factory.bin not found under $BuildDir" }
$OtaBin = Join-Path $FactoryBin.DirectoryName 'firmware.ota.bin'
if (-not (Test-Path $OtaBin)) { throw "firmware.ota.bin not found next to $($FactoryBin.FullName)" }
Write-Host "Using build: $($FactoryBin.DirectoryName) ($($FactoryBin.LastWriteTime))"

# --- 3. Stage files and manifest ------------------------------------------
$Dist = Join-Path $Root "dist\firmware\$Slug"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$DistFactory = Join-Path $Dist "$Slug.factory.bin"
$DistOta     = Join-Path $Dist "$Slug.ota.bin"
$DistManifest = Join-Path $Dist 'manifest.json'
Copy-Item $FactoryBin.FullName $DistFactory -Force
Copy-Item $OtaBin $DistOta -Force

Invoke-Native 'python' @(
    (Join-Path $Root 'scripts\firmware_release.py'), 'manifest',
    '--slug', $Slug, '--chip', $Chip, '--version', $Version,
    '--factory', $DistFactory, '--ota', $DistOta, '--out', $DistManifest
)

# The stock script points release_url at upstream GitHub; use our own server instead.
$Manifest = Get-Content $DistManifest -Raw | ConvertFrom-Json
$Manifest.builds[0].ota.release_url = "$BaseUrl/$Slug/releases/$Version/"
[System.IO.File]::WriteAllText($DistManifest, (($Manifest | ConvertTo-Json -Depth 10) + "`n"),
    (New-Object System.Text.UTF8Encoding($false)))

$Md5 = (Get-FileHash $DistOta -Algorithm MD5).Hash.ToLower()
Write-Host "==> manifest.json ready: version=$Version chip=$Chip md5=$Md5"

if ($DryRun) {
    Write-Host "DryRun: skipping upload. Files are in $Dist"
    return
}

# --- 4. Upload -------------------------------------------------------------
$Remote = "$User@$Server"
$Dir = "$RemoteRoot/$Slug"
$Rel = "$Dir/releases/$Version"
$Auth = @('-o', 'BatchMode=yes')   # fail instead of prompting for a password
if ($IdentityFile) {
    if (-not (Test-Path $IdentityFile)) { throw "SSH key not found: $IdentityFile (pass -IdentityFile)" }
    $Auth += @('-i', $IdentityFile)
}
$SshArgs = @('-p', $Port) + $Auth
$ScpArgs = @('-P', $Port) + $Auth

Write-Host "==> Uploading to ${Remote}:$Dir"
Invoke-Native 'ssh' ($SshArgs + @($Remote, "mkdir -p '$Dir' '$Rel'"))
# Bins first, manifest last, so devices never see a manifest whose OTA file is missing.
Invoke-Native 'scp' ($ScpArgs + @($DistOta, $DistFactory, "${Remote}:$Dir/"))
Invoke-Native 'scp' ($ScpArgs + @($DistManifest, "${Remote}:$Dir/manifest.json.new"))
Invoke-Native 'ssh' ($SshArgs + @($Remote,
    "cp '$Dir/$Slug.ota.bin' '$Dir/$Slug.factory.bin' '$Rel/' && cp '$Dir/manifest.json.new' '$Rel/manifest.json' && mv -f '$Dir/manifest.json.new' '$Dir/manifest.json'"))

# --- 5. Verify what devices will actually see -----------------------------
Write-Host "==> Verifying $BaseUrl/$Slug/"
$PublicManifest = Invoke-RestMethod -Uri "$BaseUrl/$Slug/manifest.json" -Headers @{ 'Cache-Control' = 'no-cache' }
if ($PublicManifest.version -ne $Version) {
    throw "Public manifest reports version '$($PublicManifest.version)', expected '$Version' (CDN cache?)"
}
$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) "$Slug.ota.verify.bin"
Invoke-WebRequest -Uri "$BaseUrl/$Slug/$($PublicManifest.builds[0].ota.path)" -OutFile $Tmp
$PublicMd5 = (Get-FileHash $Tmp -Algorithm MD5).Hash.ToLower()
Remove-Item $Tmp -Force
if ($PublicMd5 -ne $PublicManifest.builds[0].ota.md5) {
    throw "Downloaded OTA md5 $PublicMd5 does not match manifest $($PublicManifest.builds[0].ota.md5)"
}

Write-Host "Published $Slug $Version OK -> $BaseUrl/$Slug/manifest.json"

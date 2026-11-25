param(
    [string]$Configuration = "release"
)

$ErrorActionPreference = "Stop"

$repoRoot     = Split-Path -Parent $PSScriptRoot
$binDir       = Join-Path $repoRoot "bin\JavaChat"
$manifestPath = Join-Path $repoRoot "manifest.json"
$toothPath    = Join-Path $repoRoot "tooth.json"
$lipPath      = Join-Path $repoRoot "lip.toml"
$manifest     = Get-Content $manifestPath -Raw | ConvertFrom-Json
$version      = $manifest.version

if (!(Test-Path $toothPath)) {
    throw "Missing required lip manifest: $toothPath"
}
$tooth = Get-Content $toothPath -Raw | ConvertFrom-Json
if ($tooth.version -ne $version) {
    throw "Version mismatch: manifest.json ($version) != tooth.json ($($tooth.version))"
}
if (!(Test-Path $lipPath)) {
    throw "Missing required lip manifest: $lipPath"
}
$lipRaw = Get-Content $lipPath -Raw
if ($lipRaw -notmatch '(?m)^version\s*=\s*"([^"]+)"') {
    throw "Missing version in lip.toml: $lipPath"
}
if ($Matches[1] -ne $version) {
    throw "Version mismatch: manifest.json ($version) != lip.toml ($($Matches[1]))"
}

$requiredFiles = @(
    (Join-Path $binDir "JavaChat.dll"),
    (Join-Path $repoRoot "manifest.json"),
    (Join-Path $repoRoot "tooth.json"),
    (Join-Path $repoRoot "config.sample.json"),
    (Join-Path $repoRoot "LICENSE"),
    (Join-Path $repoRoot "README.md"),
    (Join-Path $repoRoot "README_JA.md"),
    (Join-Path $binDir "resource_packs\JavaChatUI\manifest.json"),
    (Join-Path $binDir "resource_packs\JavaChatUI\ui\chat_screen.json"),
    (Join-Path $binDir "resource_packs\JavaChatUI\ui\hud_screen.json")
)

foreach ($f in $requiredFiles) {
    if (!(Test-Path $f)) {
        throw "Missing required release file: $f (run 'xmake build JavaChat' first)"
    }
}

$distDir   = Join-Path $repoRoot "dist"
$stageRoot = Join-Path $distDir "stage"
$stageMod  = Join-Path $stageRoot "JavaChat"
$zipPath   = Join-Path $distDir "JavaChat-client-windows-x64.zip"

if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
if (Test-Path $zipPath)   { Remove-Item -Force $zipPath }

New-Item -ItemType Directory -Force -Path $stageMod | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stageMod "resource_packs") | Out-Null

Copy-Item (Join-Path $binDir "JavaChat.dll")        -Destination $stageMod -Force
Copy-Item (Join-Path $repoRoot "manifest.json")     -Destination $stageMod -Force
Copy-Item (Join-Path $repoRoot "config.sample.json") -Destination $stageMod -Force
Copy-Item (Join-Path $repoRoot "LICENSE")           -Destination $stageMod -Force
Copy-Item (Join-Path $repoRoot "README.md")         -Destination $stageMod -Force
Copy-Item (Join-Path $repoRoot "README_JA.md")      -Destination $stageMod -Force
Copy-Item (Join-Path $binDir "resource_packs\JavaChatUI") -Destination (Join-Path $stageMod "resource_packs") -Recurse -Force

Compress-Archive -Path $stageMod -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item -Recurse -Force $stageRoot

Write-Output "Packaged release archive: $zipPath"

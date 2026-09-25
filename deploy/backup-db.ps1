# Copy the account database (server\valhalla.db) to a dated file.
#
# The backend (sql.js) keeps the whole database in memory and rewrites the
# file on every change, so copy it while the backend is stopped: start-all.ps1
# runs this before starting anything, stop-all.ps1 after stopping everything.
#
# Backups go to %USERPROFILE%\ValhallaBackups (outside the repo; set
# VALHALLA_BACKUP_DIR to change it). The newest 14 are kept.
# B-17 L5 replaces this with a script that also uploads each copy to R2.
param(
    [string]$Label = ''
)

$ErrorActionPreference = 'Stop'
$Repo = Split-Path $PSScriptRoot -Parent
$Db = Join-Path $Repo 'server\valhalla.db'
$BackupDir = $env:VALHALLA_BACKUP_DIR
if (-not $BackupDir) { $BackupDir = Join-Path $env:USERPROFILE 'ValhallaBackups' }
$Keep = 14

if (-not (Test-Path $Db)) {
    Write-Host "  No database at $Db yet; nothing to back up." -ForegroundColor Yellow
    return
}

New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
$name = 'valhalla-' + (Get-Date -Format 'yyyy-MM-dd-HHmmss')
if ($Label) { $name += '-' + $Label }
$dest = Join-Path $BackupDir ($name + '.db')
Copy-Item -LiteralPath $Db -Destination $dest -Force

if ((Get-Item -LiteralPath $dest).Length -ne (Get-Item -LiteralPath $Db).Length) {
    throw "The backup at $dest is not the same size as $Db."
}
Write-Host "  Backed up the account database to $dest" -ForegroundColor Green

# File names start with the date, so sorting by name puts the newest first.
$old = @(Get-ChildItem -LiteralPath $BackupDir -Filter 'valhalla-*.db' |
    Sort-Object Name -Descending | Select-Object -Skip $Keep)
if ($old.Count -gt 0) {
    $old | Remove-Item -Force
    Write-Host "  Removed $($old.Count) older backup(s); the newest $Keep are kept."
}

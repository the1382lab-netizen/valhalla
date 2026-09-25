# Stop a hosting session started with start-all.bat, then back up the database.
# Run it with stop-all.bat.
#
# Order: the game server first (its restart loop, then the server), then Caddy,
# then the auth server. Only the editor process started with -server is
# stopped; an open Unreal editor is left alone.
param(
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
$Deploy = $PSScriptRoot

# Everything this window prints also goes to deploy\logs\ (gitignored, *.log).
New-Item -ItemType Directory -Force -Path (Join-Path $Deploy 'logs') | Out-Null
Start-Transcript -Path (Join-Path $Deploy ('logs\stop-all-' + (Get-Date -Format 'yyyy-MM-dd-HHmmss') + '.log')) | Out-Null

function Say([string]$Text, [string]$Color = 'Gray') {
    Write-Host ('[{0}] {1}' -f (Get-Date -Format 'HH:mm:ss'), $Text) -ForegroundColor $Color
}

function Stop-Tree([int]$ProcessId) {
    # taskkill /T also stops the child processes (cmd -> npx -> node, and so on).
    & taskkill.exe /PID $ProcessId /T /F 2>&1 | Out-Null
}

function Stop-WindowTitled([string]$TitleStart) {
    & taskkill.exe /FI "WINDOWTITLE eq $TitleStart*" /T /F 2>&1 | Out-Null
}

if (-not $Yes) {
    Write-Host ''
    Write-Host 'This stops the game server, Caddy and the auth server.' -ForegroundColor Yellow
    Write-Host 'Players still online lose anything since their last save (zone change or logout).' -ForegroundColor Yellow
    $answer = Read-Host 'Stop everything now? (y/n)'
    if ($answer -notmatch '^(y|yes)$') { Say 'Nothing was stopped.'; exit 0 }
}

# 1. Game server: the loop first, so it cannot start the server again.
$loops = @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" |
    Where-Object { $_.CommandLine -match 'gameserver-loop\.ps1' })
foreach ($l in $loops) { Stop-Tree $l.ProcessId }
$servers = @(Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" |
    Where-Object { $_.CommandLine -match '(^|\s)-server(\s|$)' })
foreach ($s in $servers) { Stop-Tree $s.ProcessId }
Say ("Game server: stopped {0} restart loop(s) and {1} server process(es)." -f $loops.Count, $servers.Count)

# 2. Caddy.
Stop-WindowTitled 'Valhalla - https'
Get-Process -Name caddy -ErrorAction SilentlyContinue | Stop-Process -Force
Say 'Caddy: stopped.'

# 3. Auth server: its window, then whatever still holds port 2567.
Stop-WindowTitled 'Valhalla - auth server'
Start-Sleep -Seconds 1
$holders = @(Get-NetTCPConnection -LocalPort 2567 -State Listen -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique)
foreach ($h in $holders) { Stop-Tree $h }
Say 'Auth server: stopped.'

# 4. Check, then back up now that nothing writes to the database.
Start-Sleep -Seconds 2
$left = @()
if (Get-NetTCPConnection -LocalPort 2567 -State Listen -ErrorAction SilentlyContinue) { $left += 'TCP 2567' }
if (Get-NetTCPConnection -LocalPort 443 -State Listen -ErrorAction SilentlyContinue) { $left += 'TCP 443' }
if (Get-NetUDPEndpoint -LocalPort 7777 -ErrorAction SilentlyContinue) { $left += 'UDP 7777' }
if ($left.Count -gt 0) {
    Say ('Still in use: ' + ($left -join ', ') + '. Something else is using it, or a window did not close; no backup taken.') 'Red'
    exit 1
}

Say 'Backing up the account database...'
& (Join-Path $Deploy 'backup-db.ps1') -Label 'stop'
Say 'Session stopped.' 'Cyan'

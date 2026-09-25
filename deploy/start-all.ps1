# Start a hosting session in one go (B-02 step 3). Run it with start-all.bat.
#
#   1. Back up server\valhalla.db (backup-db.ps1), while nothing is running yet.
#   2. Auth server (start-auth.bat), then wait for /api/health.
#   3. Caddy, the HTTPS front door (start-https.bat).
#   4. Game server in a restart loop (gameserver-loop.ps1).
#
# Each one opens in its own window. Stop them all with stop-all.bat.

$ErrorActionPreference = 'Stop'
$Deploy = $PSScriptRoot
$Repo = Split-Path $Deploy -Parent
$LocalHealth = 'http://127.0.0.1:2567/api/health'

# Everything this window prints also goes to deploy\logs\ (gitignored, *.log).
New-Item -ItemType Directory -Force -Path (Join-Path $Deploy 'logs') | Out-Null
Start-Transcript -Path (Join-Path $Deploy ('logs\start-all-' + (Get-Date -Format 'yyyy-MM-dd-HHmmss') + '.log')) | Out-Null

function Say([string]$Text, [string]$Color = 'Gray') {
    Write-Host ('[{0}] {1}' -f (Get-Date -Format 'HH:mm:ss'), $Text) -ForegroundColor $Color
}

function Test-Health {
    try {
        $r = Invoke-WebRequest -Uri $LocalHealth -UseBasicParsing -TimeoutSec 2
        return ($r.StatusCode -eq 200)
    } catch {
        return $false
    }
}

function Test-TcpListening([int]$Port) {
    return [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

function Test-UdpBound([int]$Port) {
    return [bool](Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)
}

function Start-Window([string]$Bat) {
    Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', ('"' + (Join-Path $Deploy $Bat) + '"') -WorkingDirectory $Deploy | Out-Null
}

# The public address testers use, read from the packaged client's settings so
# the reminder at the end stays right if the IP changes.
$PublicUrl = $null
$ini = Join-Path $Repo 'Valhalla2\Config\DefaultGame.ini'
if (Test-Path $ini) {
    $m = Select-String -LiteralPath $ini -Pattern '^\s*PublicBackendUrl\s*=\s*"?([^"\s]+)"?' | Select-Object -First 1
    if ($m) { $PublicUrl = $m.Matches[0].Groups[1].Value.TrimEnd('/') }
}

Write-Host ''
Say 'Starting a Valhalla hosting session.' 'Cyan'

# 0. Nothing may be running yet: the backup needs the backend stopped, and a
#    second copy of any server would fail on its port anyway.
$busy = @()
if (Test-TcpListening 2567) { $busy += 'the auth server (TCP 2567)' }
if (Test-TcpListening 443)  { $busy += 'something on TCP 443 (Caddy?)' }
if (Test-UdpBound 7777)     { $busy += 'a game server on UDP 7777 (start-gameserver.bat, or Play in the editor)' }
if ($busy.Count -gt 0) {
    Say ('Already running: ' + ($busy -join '; ') + '.') 'Red'
    Say 'Run stop-all.bat first (or stop Play in the editor), then run start-all.bat again.' 'Red'
    exit 1
}

# 1. Backup.
Say 'Backing up the account database...'
& (Join-Path $Deploy 'backup-db.ps1') -Label 'start'

# 2. Auth server.
Say 'Starting the auth server...'
Start-Window 'start-auth.bat'
$deadline = (Get-Date).AddSeconds(90)
while (-not (Test-Health)) {
    if ((Get-Date) -gt $deadline) {
        Say "The auth server did not answer on $LocalHealth within 90 seconds. Check its window for errors. Nothing else was started." 'Red'
        exit 1
    }
    Start-Sleep -Seconds 1
}
Say 'Auth server is up.' 'Green'

# 3. Caddy.
$caddyFound = (Test-Path (Join-Path $Deploy 'caddy.exe')) -or [bool](Get-Command caddy -ErrorAction SilentlyContinue)
if ($caddyFound) {
    Say 'Starting Caddy (HTTPS)...'
    Start-Window 'start-https.bat'
    $deadline = (Get-Date).AddSeconds(30)
    while (-not (Test-TcpListening 443) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1 }
    if (Test-TcpListening 443) { Say 'Caddy is listening on 443.' 'Green' }
    else { Say 'Caddy is not listening on 443 yet. Check its window.' 'Yellow' }
} else {
    Say 'caddy.exe not found (not in deploy\ and not on PATH), so HTTPS is NOT running: testers outside this PC cannot log in. See deploy\README.md, one-time setup step 2.' 'Red'
}

# 4. Game server.
Say 'Starting the game server (it restarts by itself if it stops)...'
Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + (Join-Path $Deploy 'gameserver-loop.ps1') + '"') -WorkingDirectory $Deploy | Out-Null
$deadline = (Get-Date).AddSeconds(180)
while (-not (Test-UdpBound 7777) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
if (Test-UdpBound 7777) { Say 'Game server is listening on UDP 7777.' 'Green' }
else { Say 'The game server is not listening on UDP 7777 after 3 minutes. It may still be loading; check its window.' 'Yellow' }

Write-Host ''
Say 'Session started.' 'Cyan'
if ($PublicUrl) {
    Say "Check from a phone on mobile data (not your Wi-Fi): $PublicUrl/api/health should open with no certificate warning."
}
Say 'To stop everything: stop-all.bat (it also takes a backup afterwards).'

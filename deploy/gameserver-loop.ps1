# Run start-gameserver.bat and start it again whenever it stops.
# Started by start-all.ps1 in its own window; stop-all.bat stops the loop and
# the server. Closing this window stops the loop, but may leave the server
# running: use stop-all.bat.
#
# If the server stops within 2 minutes three times in a row, the loop gives up:
# that is a startup failure (for example the FATAL server-secret check, or an
# editor build that is out of date), and restarting would not fix it.

$Host.UI.RawUI.WindowTitle = 'Valhalla - game server (auto-restart)'
$Bat = Join-Path $PSScriptRoot 'start-gameserver.bat'
$QuickExitSeconds = 120
$quickExits = 0

function Say([string]$Text, [string]$Color = 'Gray') {
    Write-Host ('[{0}] {1}' -f (Get-Date -Format 'HH:mm:ss'), $Text) -ForegroundColor $Color
}

while ($true) {
    $started = Get-Date
    Say 'Starting the game server.' 'Cyan'
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', ('"' + $Bat + '"') -NoNewWindow -Wait -PassThru
    $ran = [int]((Get-Date) - $started).TotalSeconds
    Say "The game server stopped (exit code $($p.ExitCode)) after $ran seconds." 'Yellow'

    if ($ran -lt $QuickExitSeconds) { $quickExits++ } else { $quickExits = 0 }
    if ($quickExits -ge 3) {
        Say 'It stopped within 2 minutes three times in a row, so it is not restarted again. Check its log (Valhalla2\Saved\Logs) for a FATAL line.' 'Red'
        Read-Host 'Press Enter to close this window'
        exit 1
    }

    Say 'Restarting in 10 seconds. Use stop-all.bat to stop it for good.'
    Start-Sleep -Seconds 10
}

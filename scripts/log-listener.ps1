# Nimmt die Logausgabe der Switch entgegen.
#
# Richtung ist Absicht: Die Konsole verbindet sich hierher, nicht umgekehrt.
# Deshalb ist es egal, ob dieser Empfänger hinter NAT sitzt – im Gegensatz zu
# `nxlink -s`, das genau daran scheitert.
param(
  [int]$Port = 28800,
  [int]$TimeoutSeconds = 120,
  [string]$OutFile
)

$ErrorActionPreference = 'Stop'

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
$listener.Start()
Write-Host "Warte auf Verbindung der Switch auf Port $Port (Zeitlimit ${TimeoutSeconds}s)..."

try {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  while (-not $listener.Pending()) {
    if ([DateTime]::UtcNow -gt $deadline) {
      Write-Host 'Zeitlimit erreicht, keine Verbindung.'
      return
    }
    Start-Sleep -Milliseconds 200
  }

  $client = $listener.AcceptTcpClient()
  Write-Host "Verbunden: $($client.Client.RemoteEndPoint)"
  Write-Host ('-' * 60)

  $stream = $client.GetStream()
  $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)

  $lines = @()
  while (($line = $reader.ReadLine()) -ne $null) {
    Write-Host $line
    $lines += $line
  }

  Write-Host ('-' * 60)
  Write-Host 'Verbindung beendet.'
  if ($OutFile) { $lines | Set-Content -Encoding UTF8 $OutFile }
}
finally {
  $listener.Stop()
}

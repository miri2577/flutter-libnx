# Laedt eine NRO ueber das Netz auf die Switch (hbmenu-Netloader).
#
# nxlink kommt aus dem devkitPro-Container, weil hier keine lokale
# Installation existiert. `--network host` ist zwingend: Der Netloader
# antwortet per Broadcast und Direktverbindung, was durch Dockers NAT nicht
# hindurchkommt.
#
# WICHTIG: Den Netloader-Port (28280) niemals vorher mit Test-NetConnection
# oder aehnlichem antasten. hbmenu nimmt genau eine Verbindung an; eine
# Testverbindung verbraucht sie, und der eigentliche Upload laeuft ins Leere.
param(
  [Parameter(Mandatory = $true)][string]$SwitchIp,
  [string]$Example = 'engine_link_test',
  [string[]]$Args = @()
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$nro = "examples/$Example/$Example.nro"

if (-not (Test-Path (Join-Path $repo $nro))) {
  throw "Nicht gefunden: $nro"
}

$size = (Get-Item (Join-Path $repo $nro)).Length
Write-Host ("Sende {0} ({1:N1} MB) an {2}" -f $nro, ($size / 1MB), $SwitchIp)

$cmd = @('/opt/devkitpro/tools/bin/nxlink', '-a', $SwitchIp, "/work/$nro")
if ($Args.Count -gt 0) {
  $cmd += '--'
  $cmd += $Args
}

docker run --rm --network host -v "${repo}:/work" devkitpro/devkita64 @cmd

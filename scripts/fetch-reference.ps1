# Holt die Referenzquellen nach third_party/ (nicht versioniert).
# Gepinnte Versionen siehe README.md / docs/feasibility.md.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$flutterRev = 'db50e20168db8fee486b9abf32fc912de3bc5b6a'
# Passend zur libnx-Version im Container-Image devkitpro/devkita64 (4.12.0-1).
$libnxRev   = 'v4.12.0'

New-Item -ItemType Directory -Force -Path "$root\third_party\reference" | Out-Null

Write-Host "==> embedder.h @ $flutterRev"
Invoke-WebRequest -UseBasicParsing `
  -Uri "https://raw.githubusercontent.com/flutter/flutter/$flutterRev/engine/src/flutter/shell/platform/embedder/embedder.h" `
  -OutFile "$root\third_party\reference\embedder.h"

Write-Host "==> libnx @ $libnxRev"
$libnx = "$root\third_party\libnx"
if (-not (Test-Path $libnx)) {
  git clone -q https://github.com/switchbrew/libnx $libnx
}
git -C $libnx fetch -q origin
git -C $libnx checkout -q $libnxRev

Write-Host "Fertig."

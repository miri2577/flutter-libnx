# Erzeugt aus examples/aot_poc/dart/hello.dart einen Dart-AOT-Snapshot als
# AArch64-Assembly und baut daraus eine .nro.
#
# Die Kette:
#   hello.dart --gen_kernel--> hello.dill --gen_snapshot--> hello_aot.s
#              --devkitA64--> hello_aot.o --link--> aot_poc.nro
#
# gen_snapshot stammt aus den Android-arm64-Release-Artefakten des gepinnten
# Flutter-SDK: Es läuft auf x64 und erzeugt AArch64-Code. Ein eigener Build ist
# dafür nicht nötig – das Zielbetriebssystem spielt für die Assembly-Ausgabe
# keine Rolle, nur die Zielarchitektur.
$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$flutter   = 'C:\Users\mirkorichter\flutter'
$sdk       = "$flutter\bin\cache\dart-sdk"
$genSnap   = "$flutter\bin\cache\artifacts\engine\android-arm64-release\windows-x64\gen_snapshot.exe"
$poc       = "$root\examples\aot_poc"
$generated = "$poc\generated"

foreach ($p in @($sdk, $genSnap)) {
  if (-not (Test-Path $p)) { throw "Nicht gefunden: $p" }
}

New-Item -ItemType Directory -Force -Path $generated | Out-Null
$dill = "$generated\hello.dill"

Write-Host '==> Kernel erzeugen (gen_kernel)'
& "$sdk\bin\dartaotruntime.exe" "$sdk\bin\snapshots\gen_kernel_aot.dart.snapshot" `
  --platform "$sdk\lib\_internal\vm_platform_product.dill" `
  --aot -o $dill "$poc\dart\hello.dart"
if ($LASTEXITCODE -ne 0) { throw "gen_kernel fehlgeschlagen" }

Write-Host '==> AArch64-Assembly erzeugen (gen_snapshot)'
& $genSnap --snapshot_kind=app-aot-assembly --assembly="$generated\hello_aot.s" $dill
if ($LASTEXITCODE -ne 0) { throw "gen_snapshot fehlgeschlagen" }

$lines = (Get-Content "$generated\hello_aot.s" | Measure-Object -Line).Lines
Write-Host "    $lines Zeilen Assembly"

Write-Host '==> NRO bauen (devkitA64 im Container)'
& "$root\scripts\dkp.ps1" examples/aot_poc 'make'
if ($LASTEXITCODE -ne 0) { throw "make fehlgeschlagen" }

Write-Host '==> Symbole in der ELF'
& "$root\scripts\dkp.ps1" examples/aot_poc `
  '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm --defined-only aot_poc.elf | grep -i "kDart.*Snapshot"'

Write-Host 'Fertig.'

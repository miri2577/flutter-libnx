# Erzeugt aus examples/ui_app/dart/main.dart die Artefakte, die die Engine
# zum Starten einer Dart-Anwendung braucht.
#
# Die Kette:
#   main.dart --gen_kernel--> app.dill --gen_snapshot--> app_aot.s
#
# Unterschied zum aot_poc: Dort genuegte vm_platform_product.dill, weil das
# Programm nur dart:core benutzte. Hier wird dart:ui gebraucht, und das steckt
# in der Flutter-eigenen Plattform-Dill (flutter_patched_sdk_product). Ohne sie
# scheitert schon die Kernel-Erzeugung an einem unbekannten Import.
#
# gen_snapshot stammt weiterhin aus den Android-arm64-Release-Artefakten: Es
# laeuft auf x64 und erzeugt AArch64-Assembly. Fuer die Assembly-Ausgabe zaehlt
# nur die Zielarchitektur, nicht das Zielbetriebssystem.
$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$flutter   = "$env:USERPROFILE\flutter"
$sdk       = "$flutter\bin\cache\dart-sdk"
$engine    = "$flutter\bin\cache\artifacts\engine"
$genSnap   = "$engine\android-arm64-release\windows-x64\gen_snapshot.exe"
$platform  = "$engine\common\flutter_patched_sdk_product\platform_strong.dill"
$icu       = "$engine\windows-x64\icudtl.dat"

$app       = "$root\examples\ui_app"
$generated = "$app\generated"
$assets    = "$generated\flutter_assets"

foreach ($p in @($sdk, $genSnap, $platform, $icu)) {
  if (-not (Test-Path $p)) { throw "Nicht gefunden: $p" }
}

New-Item -ItemType Directory -Force -Path $generated, $assets | Out-Null
$dill = "$generated\app.dill"

Write-Host '==> Kernel erzeugen (dart:ui aus der Flutter-Plattform-Dill)'
& "$sdk\bin\dartaotruntime.exe" "$sdk\bin\snapshots\gen_kernel_aot.dart.snapshot" `
  --platform $platform `
  --target=flutter `
  --aot `
  -o $dill `
  "$app\dart\main.dart"
if ($LASTEXITCODE -ne 0) { throw "gen_kernel fehlgeschlagen" }
Write-Host ("    {0:N2} MB" -f ((Get-Item $dill).Length / 1MB))

Write-Host '==> AArch64-Assembly erzeugen (gen_snapshot)'
& $genSnap --snapshot_kind=app-aot-assembly --assembly="$generated\app_aot.s" $dill
if ($LASTEXITCODE -ne 0) { throw "gen_snapshot fehlgeschlagen" }
$lines = (Get-Content "$generated\app_aot.s" | Measure-Object -Line).Lines
Write-Host "    $lines Zeilen Assembly"

# Die Engine erwartet ein Asset-Verzeichnis, auch wenn die Anwendung keine
# Bilder oder Schriften mitbringt. Fehlen die beiden Manifeste, bricht der
# AssetManager beim Start ab.
Write-Host '==> flutter_assets anlegen'
'{"": []}' | Set-Content -Encoding UTF8 "$assets\AssetManifest.json"
'[]'       | Set-Content -Encoding UTF8 "$assets\FontManifest.json"
Copy-Item $icu "$generated\icudtl.dat" -Force
Write-Host "    AssetManifest.json, FontManifest.json, icudtl.dat"

Write-Host "`nFertig. Artefakte in $generated"

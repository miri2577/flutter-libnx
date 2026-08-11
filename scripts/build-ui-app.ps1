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
# Kein gen_snapshot mehr hier - siehe Begruendung weiter unten.
$platform  = "$engine\common\flutter_patched_sdk_product\platform_strong.dill"
$icu       = "$engine\windows-x64\icudtl.dat"

$app       = "$root\examples\ui_app"
$generated = "$app\generated"
# Die Assets gehen ins RomFS der NRO, nicht auf die SD-Karte. Eine Flutter-App
# soll als eine einzige Datei auslieferbar sein, nicht als NRO plus einen
# Verzeichnisbaum, den jemand von Hand an die richtige Stelle kopiert. Nebenbei
# koennen Assets so nicht mehr zur Programmversion unpassend werden.
$romfs     = "$app\romfs"
$assets    = "$romfs\flutter_assets"

foreach ($p in @($sdk, $platform, $icu)) {
  if (-not (Test-Path $p)) { throw "Nicht gefunden: $p" }
}

New-Item -ItemType Directory -Force -Path $generated, $romfs, $assets | Out-Null
$dill = "$generated\app.dill"

# Sobald package:flutter im Spiel ist, reicht die Plattform-Dill nicht mehr:
# dart:ui steckt darin, das Framework liegt als Quellpaket im SDK und wird nur
# ueber die Paketaufloesung gefunden. Die erzeugt `flutter pub get` in
# examples/ui_app/dart aus der dortigen pubspec.yaml.
$packages = "$app\dart\.dart_tool\package_config.json"
if (-not (Test-Path $packages)) {
  throw "Nicht gefunden: $packages - erst 'flutter pub get' in $app\dart laufen lassen"
}

Write-Host '==> Kernel erzeugen (Framework ueber package_config.json)'
& "$sdk\bin\dartaotruntime.exe" "$sdk\bin\snapshots\gen_kernel_aot.dart.snapshot" `
  --platform $platform `
  --target=flutter `
  --aot `
  --packages $packages `
  -o $dill `
  "$app\dart\lib\main.dart"
if ($LASTEXITCODE -ne 0) { throw "gen_kernel fehlgeschlagen" }
Write-Host ("    {0:N2} MB" -f ((Get-Item $dill).Length / 1MB))

# Die Assembly entsteht bewusst NICHT hier.
#
# Der gen_snapshot aus den Android-Artefakten des SDK hat fuer den Ladeweg
# getragen (Meilenstein 1b), taugt aber nicht fuer die Ausfuehrung: Der
# Snapshot-Hash ist ein MD5 ueber 15 Dart-Quelldateien, die unsere Portierung
# veraendert, und ausserdem baut das Projekt ohne Compressed Pointers, das SDK
# mit. Beides fuehrt zu
#
#   Wrong full snapshot version, expected '...' found '...'
#
# Zustaendig ist scripts/rebuild-all.sh: Es benutzt den selbst gebauten
# clang_x64/gen_snapshot_product und haelt die Reihenfolge
# gen_snapshot -> Engine -> Snapshot -> NRO ein.
Write-Host ''
Write-Host 'Weiter mit: wsl bash /mnt/e/flutter-libnx/scripts/rebuild-all.sh ui_app'

# Die Engine erwartet ein Asset-Verzeichnis, auch wenn die Anwendung keine
# Bilder mitbringt. Fehlen die Manifeste, bricht der AssetManager beim Start ab.
#
# Die Icon-Schrift gehoert dazu: `Icons.touch_app` und Verwandte sind keine
# Grafiken, sondern Zeichen der Schriftart "MaterialIcons". Ohne sie zeichnet
# Flutter das Ersatzkaestchen mit Kreuz - auf Hardware bestaetigt. Bei einem
# gewoehnlichen `flutter build` erledigt das der Werkzeugkasten; hier legen wir
# die Assets von Hand an und muessen es selbst tun.
Write-Host '==> flutter_assets anlegen'
$materialFonts = "$flutter\bin\cache\artifacts\material_fonts"
$iconFont = "$materialFonts\materialicons-regular.otf"
if (-not (Test-Path $iconFont)) { throw "Nicht gefunden: $iconFont" }

New-Item -ItemType Directory -Force -Path "$assets\fonts" | Out-Null
Copy-Item $iconFont "$assets\fonts\MaterialIcons-Regular.otf" -Force

'{"": []}' | Set-Content -Encoding UTF8 "$assets\AssetManifest.json"
# Der Familienname muss "MaterialIcons" lauten - genau den traegt IconData aus
# dem Framework. Der Pfad ist relativ zum flutter_assets-Verzeichnis.
'[{"family":"MaterialIcons","fonts":[{"asset":"fonts/MaterialIcons-Regular.otf"}]}]' |
  Set-Content -Encoding UTF8 "$assets\FontManifest.json"
Copy-Item $icu "$romfs\icudtl.dat" -Force
Write-Host ("    MaterialIcons-Regular.otf ({0:N0} KB)" -f ((Get-Item $iconFont).Length / 1KB))
Write-Host "    AssetManifest.json, FontManifest.json, icudtl.dat"

Write-Host "`nFertig. Artefakte in $generated"

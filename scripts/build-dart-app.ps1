# Erzeugt aus einem Flutter-Projekt die Artefakte, die der Horizon-Embedder
# braucht: Assets fuer das RomFS und AOT-Assembly fuer den Linker.
#
#   build-dart-app.ps1                       -> examples/ui_app
#   build-dart-app.ps1 -Project C:\pfad\app  -> beliebiges Projekt
#
# Die Kette:
#   flutter build bundle  -> flutter_assets  (Manifeste, Schriften, Assets)
#   gen_kernel_aot        -> app.dill
#   scripts/rebuild-all.sh -> app_aot.s -> .nro
#
# WARUM DIESES SKRIPT SO AUSSIEHT
#
# Das Ziel des Projekts ist ein Werkzeug, mit dem jeder ein *unveraendertes*
# Flutter-Projekt uebersetzen kann - nicht eine auf ein Beispiel zugeschnittene
# Bastelloesung. Deshalb entstehen die Assets ueber `flutter build bundle` und
# nicht von Hand.
#
# Vorher wurden AssetManifest und FontManifest hier geschrieben und die
# Icon-Schrift kopiert. Das ging so lange gut, bis auf dem Bildschirm an jeder
# Icon-Stelle das Ersatzkaestchen mit Kreuz stand: `Icons.touch_app` ist keine
# Grafik, sondern ein Zeichen der Schriftart "MaterialIcons", die ein Projekt
# ueber `uses-material-design: true` anfordert. Bei jeder fremden App waere
# dieselbe Klasse Fehler mit deren eigenen Schriften und Assets wieder
# aufgetreten.
param(
  [string]$Project,
  # -Product setzt -Ddart.vm.product=true beim Kernel: kDebugMode wird
  # falsch, Debug-Pfade fallen aus dem AOT-Build (schneller). NICHT fuer
  # Apps mit media_kit auf Horizon: dessen NativeReferenceHolder-Pfad
  # laeuft nur, weil kDebugMode im AOT wahr ist - media_kit-Apps brauchen das.
  [switch]$Product
)

$ErrorActionPreference = 'Stop'

$root    = Split-Path -Parent $PSScriptRoot
$flutter = "$env:USERPROFILE\flutter"
$sdk     = "$flutter\bin\cache\dart-sdk"
$engine  = "$flutter\bin\cache\artifacts\engine"

# Die Plattform-Dill des Frameworks. dart:ui steckt darin; ohne sie scheitert
# schon die Kernel-Erzeugung an einem unbekannten Import.
$platform = "$engine\common\flutter_patched_sdk_product\platform_strong.dill"
$icu      = "$engine\windows-x64\icudtl.dat"

if (-not $Project) { $Project = "$root\examples\ui_app\dart" }
$Project = (Resolve-Path $Project).Path

# Das Ausgabeziel liegt neben dem Beispiel, nicht im fremden Projekt: Ein
# Werkzeug soll das Projekt, das es uebersetzt, nicht veraendern.
$app       = "$root\examples\ui_app"
$generated = "$app\generated"
$romfs     = "$app\romfs"
$assets    = "$romfs\flutter_assets"

foreach ($p in @($sdk, $platform, $icu, "$Project\pubspec.yaml")) {
  if (-not (Test-Path $p)) { throw "Nicht gefunden: $p" }
}

New-Item -ItemType Directory -Force -Path $generated, $romfs | Out-Null

Write-Host "==> Projekt: $Project"

# --- 1. Assets ---------------------------------------------------------------
Write-Host '==> flutter build bundle'
Push-Location $Project
try {
  & "$flutter\bin\flutter.bat" build bundle | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'flutter build bundle fehlgeschlagen' }
} finally {
  Pop-Location
}

$bundle = "$Project\build\flutter_assets"
if (-not (Test-Path $bundle)) { throw "Nicht gefunden: $bundle" }

# Die JIT-Artefakte bleiben draussen. `flutter build bundle` legt sie fuer den
# Debug-Modus mit an; im AOT-Betrieb liest die Engine sie nie, und
# kernel_blob.bin allein waere ueber 40 MB - in einer NRO, die sonst 17 MB
# wiegt.
$jitOnly = @('kernel_blob.bin', 'vm_snapshot_data', 'isolate_snapshot_data',
             '.last_build_id')

if (Test-Path $assets) { Remove-Item $assets -Recurse -Force }
New-Item -ItemType Directory -Force -Path $assets | Out-Null

$copied = 0
Get-ChildItem $bundle -Recurse -File | ForEach-Object {
  $relative = $_.FullName.Substring($bundle.Length + 1)
  if ($jitOnly -contains $_.Name) { return }
  $target = Join-Path $assets $relative
  New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
  Copy-Item $_.FullName $target -Force
  $script:copied++
}
$size = (Get-ChildItem $assets -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("    {0} Dateien, {1:N2} MB" -f $copied, ($size / 1MB))

Copy-Item $icu "$romfs\icudtl.dat" -Force

# --- 2. Kernel ---------------------------------------------------------------
# package:flutter liegt als Quellpaket im SDK und wird nur ueber die
# Paketaufloesung gefunden; `flutter build bundle` oben hat sie erzeugt.
$packages = "$Project\.dart_tool\package_config.json"
if (-not (Test-Path $packages)) { throw "Nicht gefunden: $packages" }

$entry = "$Project\lib\main.dart"
if (-not (Test-Path $entry)) { throw "Nicht gefunden: $entry" }

# --- Horizon-Registrant ------------------------------------------------------
# `flutter build` erzeugt normalerweise einen Plugin-Registrant, den die
# Engine beim Isolate-Start aufruft. Ohne ihn bleibt bei Paketen mit
# `static late _instance` (Beispiel: file_picker) die Plattform-Instanz
# ungesetzt - jeder Zugriff wirft LateInitializationError. Der Fund kam
# von rezkonv: "Verzeichnis waehlen" tat nichts.
#
# Hier entsteht das Horizon-Gegenstueck: ein erzeugter Einstiegspunkt, der
# vor main() die METHOD-CHANNEL-Implementierungen registriert - nicht die
# Desktop-Varianten (FilePickerLinux wuerde zenity als Prozess starten,
# was es auf Horizon nicht gibt). Die Kanaele beantwortet der Embedder
# (plugins_horizon.cpp). Die Liste waechst mit den unterstuetzten Paketen.
$appName = (Select-String -Path "$Project\pubspec.yaml" -Pattern '^name:\s*(\S+)').Matches[0].Groups[1].Value
$packageConfigText = Get-Content $packages -Raw
$registrations = @()
$imports = @()
if ($packageConfigText -match '"name"\s*:\s*"file_picker"') {
  $imports += "import 'package:file_picker/file_picker.dart' show FilePickerIO;"
  $registrations += '  FilePickerIO.registerWith();'
}

if ($registrations.Count -gt 0) {
  Write-Host ("==> Horizon-Registrant ({0} Registrierung(en))" -f $registrations.Count)
  $lines = @('// Von build-dart-app.ps1 erzeugt - nicht von Hand pflegen.')
  $lines += "import 'package:$appName/main.dart' as app;"
  $lines += $imports
  $lines += ''
  $lines += 'void main() {'
  $lines += $registrations
  $lines += '  app.main();'
  $lines += '}'
  $entry = "$generated\horizon_main.dart"
  $lines | Set-Content -Encoding utf8 $entry
}

Write-Host ('==> Kernel erzeugen' + $(if ($Product) { ' (product)' } else { '' }))
$dill = "$generated\app.dill"
$defines = @()
if ($Product) { $defines += '-Ddart.vm.product=true' }
& "$sdk\bin\dartaotruntime.exe" "$sdk\bin\snapshots\gen_kernel_aot.dart.snapshot" `
  --platform $platform `
  --target=flutter `
  --aot `
  --packages $packages `
  @defines `
  -o $dill `
  $entry
if ($LASTEXITCODE -ne 0) { throw 'gen_kernel fehlgeschlagen' }
Write-Host ("    {0:N2} MB" -f ((Get-Item $dill).Length / 1MB))

# --- 3. Weiter in WSL --------------------------------------------------------
# Die Assembly entsteht bewusst nicht hier: Sie braucht den selbst gebauten
# clang_x64/gen_snapshot_product, weil der Snapshot-Hash ueber 15 Dart-Quellen
# gebildet wird, die diese Portierung veraendert - und weil hier ohne
# Compressed Pointers gebaut wird, das SDK aber mit.
Write-Host ''
Write-Host 'Weiter mit: wsl bash /mnt/e/flutter-libnx/scripts/rebuild-all.sh ui_app'

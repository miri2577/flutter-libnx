# Prueft, welche Werkzeuge fuer den Weg "Flutter-App -> NRO" schon vorliegen.
#
# Der Weg ist: flutter build bundle (Assets) + gen_kernel (app.dill) +
# gen_snapshot (AArch64-Assembly) + devkitA64 (assemblieren und linken).
# gen_snapshot fuer arm64 stammt aus den Android-Artefakten des SDK - ein
# eigener Dart-Build ist dafuer nicht noetig.

$ErrorActionPreference = 'Continue'
$sdk = "$env:USERPROFILE\flutter"

Write-Host "=== Flutter-SDK"
if (Test-Path $sdk) {
  Write-Host "  $sdk"
  $version = Get-Content "$sdk\version" -ErrorAction SilentlyContinue
  Write-Host "  Version: $version"
} else {
  Write-Host "  nicht gefunden"
  exit 1
}

Write-Host "`n=== gen_snapshot fuer arm64"
Get-ChildItem -Path "$sdk\bin\cache\artifacts\engine" -Recurse -Filter "gen_snapshot*" `
  -ErrorAction SilentlyContinue |
  Select-Object -First 10 |
  ForEach-Object { Write-Host ("  {0}" -f $_.FullName.Replace("$sdk\bin\cache\artifacts\engine\", "")) }

Write-Host "`n=== Kernel-Compiler und Plattform-Dill"
Get-ChildItem -Path "$sdk\bin\cache\artifacts\engine" -Recurse `
  -Include "gen_kernel*.dart.snapshot", "frontend_server*.dart.snapshot", "vm_platform*.dill" `
  -ErrorAction SilentlyContinue |
  Select-Object -First 10 |
  ForEach-Object { Write-Host ("  {0}" -f $_.FullName.Replace("$sdk\bin\cache\artifacts\engine\", "")) }

Write-Host "`n=== icudtl.dat (braucht die Engine zur Laufzeit)"
Get-ChildItem -Path "$sdk\bin\cache\artifacts\engine" -Recurse -Filter "icudtl.dat" `
  -ErrorAction SilentlyContinue |
  Select-Object -First 5 |
  ForEach-Object { Write-Host ("  {0}  ({1:N1} MB)" -f $_.FullName.Replace("$sdk\bin\cache\artifacts\engine\", ""), ($_.Length / 1MB)) }

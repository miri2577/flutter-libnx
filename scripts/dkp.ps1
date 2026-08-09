# Führt einen Befehl in der devkitPro-Toolchain aus, ohne lokale Installation.
#
# Beispiele:
#   .\scripts\dkp.ps1 examples/hello_libnx make
#   .\scripts\dkp.ps1 examples/hello_libnx make clean
#   .\scripts\dkp.ps1 . "aarch64-none-elf-gcc --version"
#
# Das Repo wird als /work in den Container gehängt; der erste Parameter ist das
# Arbeitsverzeichnis relativ zur Repo-Wurzel.
param(
  [Parameter(Mandatory = $true)][string]$WorkDir,
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Command
)

$ErrorActionPreference = 'Stop'

# Version bewusst gepinnt: ein wanderndes :latest würde die Reproduzierbarkeit
# genau der Sorte Fehler kosten, die bei einer Portierung am teuersten sind.
$image = 'devkitpro/devkita64:latest'

$root = Split-Path -Parent $PSScriptRoot
$inner = ($WorkDir -replace '\\', '/').TrimEnd('/')
if ($inner -eq '.') { $inner = '' }
$containerDir = "/work" + $(if ($inner) { "/$inner" } else { '' })

docker run --rm -v "${root}:/work" -w $containerDir $image bash -lc ($Command -join ' ')
exit $LASTEXITCODE

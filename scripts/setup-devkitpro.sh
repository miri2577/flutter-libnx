#!/usr/bin/env bash
# Installiert devkitPro/devkitA64 + libnx auf einem Debian/Ubuntu-Host (hier: WSL2).
# Braucht sudo. Danach steht aarch64-none-elf-gcc unter /opt/devkitpro zur Verfügung.
set -euo pipefail

if [ "$(id -u)" -eq 0 ]; then
  echo "Bitte nicht als root starten – das Skript ruft sudo selbst auf." >&2
  exit 1
fi

echo "==> Abhängigkeiten"
sudo apt-get update
sudo apt-get install -y wget gdebi-core build-essential p7zip-full

echo "==> devkitPro pacman"
if ! command -v dkp-pacman >/dev/null 2>&1; then
  tmp="$(mktemp -d)"
  wget -q -O "$tmp/install-devkitpro-pacman" https://apt.devkitpro.org/install-devkitpro-pacman
  chmod +x "$tmp/install-devkitpro-pacman"
  sudo "$tmp/install-devkitpro-pacman"
  rm -rf "$tmp"
else
  echo "dkp-pacman ist bereits installiert."
fi

echo "==> switch-dev Gruppe"
sudo dkp-pacman -Syu --noconfirm
sudo dkp-pacman -S --needed --noconfirm switch-dev

echo "==> Umgebung"
cat <<'EOF'

Fertig. Für die Shell noch setzen (z.B. in ~/.bashrc):

  export DEVKITPRO=/opt/devkitpro
  export DEVKITA64=/opt/devkitpro/devkitA64
  export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH

Prüfen mit:

  aarch64-none-elf-gcc --version
  ls $DEVKITPRO/libnx/include/switch.h

EOF

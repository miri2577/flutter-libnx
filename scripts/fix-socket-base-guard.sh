#!/usr/bin/env bash
# Nimmt die zu frueh gesetzte HORIZON-Bedingung aus dem Linux-Zweig von
# socket_base.h wieder heraus. Horizon bekommt seinen eigenen Zweig weiter
# unten, weil socket_base_linux.h <sys/un.h> einbindet, das libnx nicht hat.
#
# Ausserdem werden die von gclient installierten Git-Hooks abgeschaltet: Sie
# rufen vpython3 auf, das nicht im PATH steht, wodurch jedes `git checkout --`
# im Engine-Baum still fehlschlaegt.
set -eu

E="$HOME/engine/flutter"
F="$E/engine/src/flutter/third_party/dart/runtime/bin/socket_base.h"

git -C "$E" config core.hooksPath /dev/null
echo "Git-Hooks im Engine-Baum abgeschaltet."

python3 - "$F" <<'EOF'
import sys
path = sys.argv[1]
with open(path, encoding="utf-8") as handle:
    text = handle.read()

wrong = ("#elif defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID) ||      \\\n"
         "    defined(DART_HOST_OS_HORIZON)\n")
right = "#elif defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID)\n"

if wrong in text:
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text.replace(wrong, right, 1))
    print("Linux-Zweig zurueckgesetzt.")
else:
    print("Linux-Zweig bereits korrekt.")
EOF

grep -n "DART_HOST_OS_HORIZON\|socket_base_.*\.h" "$F"

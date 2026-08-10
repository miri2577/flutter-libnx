#!/usr/bin/env bash
F="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin/platform_linux.cc"
echo "=== Systemincludes"
grep -n "#include <" "$F"
echo
echo "=== Linux-typische Aufrufe (Haeufigkeit)"
grep -on "uname\|utsname\|/proc/\|environ\|getpwuid\|sysconf\|gethostname\|prctl" "$F" \
  | cut -d: -f2 | sort | uniq -c

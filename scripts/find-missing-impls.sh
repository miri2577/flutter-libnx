#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime"
echo "=== Utils::StrDup / SNPrint"
grep -rln "Utils::StrDup\|Utils::SNPrint" "$R/platform" 2>/dev/null | head -5
echo
echo "=== Syslog::VPrintErr"
grep -rln "Syslog::VPrintErr" "$R/platform" 2>/dev/null | head -5
echo
echo "=== Waechter der Kandidaten"
for f in "$R"/platform/utils_linux.cc "$R"/platform/syslog_linux.cc; do
  [ -e "$f" ] || continue
  echo "--- $(basename "$f")"
  grep -n "DART_HOST_OS" "$f" | head -3
done
echo
echo "=== OS::GetCurrentMonotonicTicks in os_linux.cc?"
grep -c "OS::GetCurrentMonotonicTicks\|OS::CurrentRSS\|OS::NotifyBeforeGC\|OS::PrepareToAbort" "$R/vm/os_linux.cc"

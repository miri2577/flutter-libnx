#!/usr/bin/env bash
set -u
B="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"

echo "=== process.h: statische Member und Signaturen"
grep -n "static\|Mutex\|exit_hook\|global_exit_code\|ExitHook\|ProcessStartMode" \
  "$B/process.h" | head -60

echo
echo "=== file_system_watcher.h"
sed -n '1,70p' "$B/file_system_watcher.h"

echo
echo "=== process_linux.cc: Definition der statischen Member"
grep -n "^Mutex\|^int Process::\|^Dart_Port\|Process::global_exit_code\|Process::exit_hook_\|Process::Init\|Process::Cleanup\|Process::CurrentRSS\|Process::MaxRSS\|Process::CurrentProcessId\|Process::TerminateExitCodeHandler\|Process::SetSignalHandler\|Process::ClearSignalHandler" \
  "$B/process_linux.cc"

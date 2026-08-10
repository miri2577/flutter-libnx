#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src/flutter/fml"
echo "=== message_loop_impl.h (Schnittstelle)"
cat -n "$S/message_loop_impl.h"
echo
echo "=== message_loop_linux.h"
cat -n "$S/platform/linux/message_loop_linux.h"
echo
echo "=== message_loop_linux.cc"
cat -n "$S/platform/linux/message_loop_linux.cc"

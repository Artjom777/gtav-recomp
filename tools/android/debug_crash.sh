#!/bin/bash
# Start the game under lldb (lldb-server inside the app via run-as) and print the backtrace of
# every fault the process takes, so crashes the SDK's signal handler hides become visible.
# Usage: tools/android/debug_crash.sh [STOPS]  (needs ./lldb-server in the app's data dir)
source ~/android-tools/env.sh
PKG=com.gtavrecomp.app
LLDB=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/lldb.sh
OUT=${OUT:-/tmp/claude-1000/-home-nakas/a5592daa-a7b1-4d47-80e6-634cc01ceeaa/scratchpad/lldb_out.txt}
adb shell am force-stop $PKG
adb shell am start -n $PKG/.GameActivity >/dev/null
for i in $(seq 100); do P=$(adb shell pidof $PKG); [ -n "$P" ] && break; sleep 0.05; done
sleep ${ATTACH_DELAY:-1}
adb forward tcp:5039 localabstract:/gtavdbg >/dev/null
(adb shell "run-as $PKG ./lldb-server gdbserver unix-abstract:///gtavdbg --attach $P" </dev/null >/dev/null 2>&1 &)
sleep 4
adb shell "run-as $PKG cat /proc/$P/maps" > "${OUT%.txt}_maps.txt"
CMDS=$(mktemp)
{
  echo "gdb-remote 5039"
  echo "process handle SIGSEGV -s true -n true -p true"
  echo "process handle SIGBUS -s false -n false -p true"
  echo "process handle SIGILL -s true -n true -p true"
  echo "process handle SIGUSR1 SIGUSR2 SIGSYS SIG33 SIG34 SIG35 SIG36 SIG37 SIG38 SIG39 SIG40 SIG41 SIG42 SIG43 SIG44 SIG45 SIG46 SIG47 SIG48 SIG49 SIG50 SIG51 SIG52 SIG53 SIG54 SIG55 SIG56 SIG57 SIG58 SIG59 SIG60 SIG61 SIG62 SIG63 SIG64 -s false -n false -p true"
  echo "command script import $(dirname "$0")/lldb_catch.py"
  echo "catch"
  echo "image list -o -f libmain.so librexruntime.so"
} > "$CMDS"
CATCH_LOG="${OUT%.txt}_catch.txt" timeout ${TIMEOUT:-120} "$LLDB" --batch -s "$CMDS" > "$OUT" 2>&1
rm -f "$CMDS"
echo "lldb output: $OUT ($(wc -l < "$OUT") lines)"

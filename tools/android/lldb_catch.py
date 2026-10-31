"""lldb script (debug_crash.sh): keep the game running, skip ART's own faults (JIT implicit null
checks, GC SIGBUS) and print the first native fault with a symbolized-by-module backtrace."""
import lldb
import os
import sys

LOG = open(os.environ.get("CATCH_LOG", "/tmp/lldb_catch.txt"), "w", buffering=1)


def print(*args):  # noqa: A001 - line-buffered file instead of lldb's batch stdout
    LOG.write(" ".join(str(a) for a in args) + "\n")


def catch(debugger, command, result, internal_dict):
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    for n in range(100000):
        if n % 200 == 0:
            print("stops so far", n)
        process.Continue()
        if process.GetState() != lldb.eStateStopped:
            print("process state", process.GetState(), "exit status", process.GetExitStatus())
            return
        for thread in process:
            if thread.GetStopReason() != lldb.eStopReasonSignal:
                continue
            sig = thread.GetStopReasonDataAtIndex(0)
            frame0 = thread.GetFrameAtIndex(0)
            pc = frame0.GetPC()
            mod = frame0.GetModule().GetFileSpec().GetFilename() or ""
            if sig in (11, 7) and (0x40000000 <= pc < 0x80000000 or mod.startswith(("libart", "boot", "base.odex"))):
                continue
            if sig not in (4, 7, 11):
                continue
            print("FAULT signal %d thread %s (%s) pc %#x module %s" % (sig, thread.GetName(), thread.GetThreadID(), pc, mod))
            for f in thread.frames[:30]:
                print("  %#x %s %s" % (f.GetPC(), f.GetModule().GetFileSpec().GetFilename(), f.GetFunctionName()))
            for r in ("pc", "lr", "sp", "x0", "x1", "x8", "x19", "x20"):
                v = frame0.FindRegister(r)
                print("  %s = %s" % (r, v.GetValue()))
            return


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f lldb_catch.catch catch")

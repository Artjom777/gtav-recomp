#!/usr/bin/env python3
"""Live workaround for the cutscene hang: while the cutscene manager waits to attach a cut file
(state 2) and the cutscene audio entity is stuck in state 5 ("all segments played"), reset the
audio state to 0 so the attach can proceed. Usage: cutscene_unstick.py [pid]"""
import struct, subprocess, sys, time
G = 0x100000000
MGR_PTR, AUDIO_STATE = 0x83A6BE68, 0x83A320F8 + 5068
def find_pid():
    while True:  # wait for the game to start
        r = subprocess.run(['pgrep', '-x', 'gtav'], capture_output=True, text=True)
        if r.stdout.split():
            return int(r.stdout.split()[0])
        time.sleep(2)
pid = int(sys.argv[1]) if len(sys.argv) > 1 else find_pid()
with open(f'/proc/{pid}/mem', 'r+b', buffering=0) as m:
    def rd(a):
        m.seek(G + a); return struct.unpack('>I', m.read(4))[0]
    stuck = 0
    while True:
        try:
            mgr = rd(MGR_PTR)
            if mgr and rd(mgr + 3096) == 2 and rd(AUDIO_STATE) == 5:
                stuck += 1
                if stuck >= 4:  # ~1 s in that state: not a transient
                    m.seek(G + AUDIO_STATE); m.write(struct.pack('>I', 0))
                    print(time.strftime('%H:%M:%S'), 'unstuck cut hash 0x%08X' % rd(mgr + 3296), flush=True)
                    stuck = 0
            else:
                stuck = 0
        except OSError:
            print('game exited'); break
        time.sleep(0.25)

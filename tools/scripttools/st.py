import sys;sys.path.insert(0,'/tmp/claude-1000/-home-nakas/0dfcd704-d8b8-4292-8379-ce156f2f7a00/scratchpad');from gm import *
import struct
S=0xcb6b1cd0
for a in sys.argv[1:]:
    i=int(a); v=u32(S+i*4); print(i,hex(v),struct.unpack('>f',struct.pack('>I',v))[0])

import sys;sys.path.insert(0,'/tmp/claude-1000/-home-nakas/0dfcd704-d8b8-4292-8379-ce156f2f7a00/scratchpad');from gm import *
P=[u32(0xde08bec0+4*i) for i in range(4)]
def s(o): return rd(P[o>>14]+(o&0x3fff),80).split(b'\0')[0].decode('latin1')
if __name__=='__main__':
    for a in sys.argv[1:]: print(a,s(int(a)))

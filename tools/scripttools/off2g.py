import sys;sys.path.insert(0,'/tmp/claude-1000/-home-nakas/0dfcd704-d8b8-4292-8379-ce156f2f7a00/scratchpad');from gm import g
for l in open(sys.argv[1]):
    o,v=l.split(); print(hex(g(int(o,16))),v)

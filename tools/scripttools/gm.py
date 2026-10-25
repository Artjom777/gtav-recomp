import mmap,struct,sys
idx=[tuple(int(x,16) for x in l.split()) for l in open('/home/nakas/gtav-linux/logs/hang3_guestmem/index.txt')]
f=open('/home/nakas/gtav-linux/logs/hang3_guestmem/guest.bin','rb'); m=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
def off(a):
    for s,e,o in idx:
        if s<=a<e: return o+a-s
def rd(a,n):
    o=off(a); return None if o is None else m[o:o+n]
def u32(a): return struct.unpack('>I',rd(a,4))[0]
def g(o):
    for s,e,oo in idx:
        if oo<=o<oo+(e-s): return s+o-oo
def dump(a,n):
    o=off(a)
    if o is None: print(hex(a),'unmapped');return
    for i in range(0,n,16):
        w=struct.unpack('>4I',m[o+i:o+i+16]); print(hex(a+i),' '.join('%08x'%x for x in w), m[o+i:o+i+16])
def findall(b,lim=50):
    r=[];p=m.find(b)
    while p>=0 and len(r)<lim: r.append(g(p)); p=m.find(b,p+1)
    return r

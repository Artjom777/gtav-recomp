import struct,sys
names="NOP IADD ISUB IMUL IDIV IMOD INOT INEG IEQ INE IGT IGE ILT ILE FADD FSUB FMUL FDIV FMOD FNEG FEQ FNE FGT FGE FLT FLE VADD VSUB VMUL VDIV VNEG IAND IOR IXOR I2F F2I F2V PUSH_U8 PUSH_U8_U8 PUSH_U8_U8_U8 PUSH_U32 PUSH_F DUP DROP NATIVE ENTER LEAVE LOAD STORE STORE_REV LOAD_N STORE_N ARRAY_U8 ARRAY_U8_LOAD ARRAY_U8_STORE LOCAL_U8 LOCAL_U8_LOAD LOCAL_U8_STORE STATIC_U8 STATIC_U8_LOAD STATIC_U8_STORE IADD_U8 IMUL_U8 IOFFSET IOFFSET_U8 IOFFSET_U8_LOAD IOFFSET_U8_STORE PUSH_S16 IADD_S16 IMUL_S16 IOFFSET_S16 IOFFSET_S16_LOAD IOFFSET_S16_STORE ARRAY_U16 ARRAY_U16_LOAD ARRAY_U16_STORE LOCAL_U16 LOCAL_U16_LOAD LOCAL_U16_STORE STATIC_U16 STATIC_U16_LOAD STATIC_U16_STORE GLOBAL_U16 GLOBAL_U16_LOAD GLOBAL_U16_STORE J JZ IEQ_JZ INE_JZ IGT_JZ IGE_JZ ILT_JZ ILE_JZ CALL GLOBAL_U24 GLOBAL_U24_LOAD GLOBAL_U24_STORE PUSH_U24 SWITCH STRING STRINGHASH TL_ASSIGN_STR TL_ASSIGN_INT TL_APPEND_STR TL_APPEND_INT TL_COPY CATCH THROW CALLINDIRECT PUSH_M1 PUSH_0 PUSH_1 PUSH_2 PUSH_3 PUSH_4 PUSH_5 PUSH_6 PUSH_7 PUSH_FM1 PUSH_F0 PUSH_F1 PUSH_F2 PUSH_F3 PUSH_F4 PUSH_F5 PUSH_F6 PUSH_F7".split()
u8ops={37,46*0+52,53,54,55,56,57,58,59,60,61,62,64,65,66,101,102,103,104}
u16ops={67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84}
jops=set(range(85,93))
code=open(sys.argv[1] if len(sys.argv)>1 else 'pro1.code','rb').read()
nat={}
fnn={}
for l in open('fn2name.txt'):
    f,n=l.split(); fnn[f]=n
for l in open('pro1.natives'): i,v=l.split(); nat[int(i,16)]=fnn.get(v,v)
def ins(ip):
    op=code[ip]; n=names[op] if op<len(names) else 'BAD%02x'%op; a=''
    if op in u8ops: a=str(code[ip+1]); l=2
    elif op==38: a='%d %d'%(code[ip+1],code[ip+2]); l=3
    elif op==39: a='%d %d %d'%tuple(code[ip+1:ip+4]); l=4
    elif op==40: a=hex(struct.unpack('>I',code[ip+1:ip+5])[0]); l=5
    elif op==41: a='%g'%struct.unpack('>f',code[ip+1:ip+5])[0]; l=5
    elif op==44:
        b=code[ip+1]; idx=(code[ip+2]<<8)|code[ip+3]; a='args=%d rets=%d idx=%x fn=%s'%(b>>2,b&3,idx,nat.get(idx,'?')); l=4
    elif op==45: a='params=%d frame=%d'%(code[ip+1],(code[ip+2]<<8)|code[ip+3]); l=5+code[ip+4]
    elif op==46: a='%d %d'%(code[ip+1],code[ip+2]); l=3
    elif op in u16ops:
        v=struct.unpack('>H',code[ip+1:ip+3])[0]
        if op in (67,68,69,70,71,72): v=struct.unpack('>h',code[ip+1:ip+3])[0]
        a=str(v); l=3
    elif op in jops:
        o=struct.unpack('>h',code[ip+1:ip+3])[0]; a='-> %x'%(ip+3+o); l=3
    elif op in (93,94,95,96,97):
        v=(code[ip+1]<<16)|(code[ip+2]<<8)|code[ip+3]; a=hex(v) if op==93 else str(v); l=4
    elif op==98:
        c=code[ip+1]; cs=[]
        for k in range(c):
            p=ip+2+k*6; val=struct.unpack('>I',code[p:p+4])[0]; o=struct.unpack('>h',code[p+4:p+6])[0]; cs.append('%d:%x'%(val,p+6+o))
        a=' '.join(cs); l=2+6*c
    else: l=1
    return n,a,l
def run(lo,hi):
    ip=lo
    while ip<hi:
        n,a,l=ins(ip); print('%06x  %-16s %s'%(ip,n,a)); ip+=l
if __name__=='__main__':
    run(int(sys.argv[2],16),int(sys.argv[3],16))

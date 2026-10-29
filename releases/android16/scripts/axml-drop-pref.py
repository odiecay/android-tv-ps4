"""Remove the <Preference android:key="fw_tool_settings"> element (and its
children) from compiled Android XML. Whole chunks are removed, so the string
pool and every other element stay byte-identical."""
import struct,sys
def strings(d,off):
    hsz,=struct.unpack_from('<H',d,off+2); n,=struct.unpack_from('<I',d,off+8)
    flags,start=struct.unpack_from('<I',d,off+16)[0],struct.unpack_from('<I',d,off+20)[0]
    utf8=flags&0x100; out=[]
    for i in range(n):
        o,=struct.unpack_from('<I',d,off+hsz+i*4); p=off+start+o
        if utf8:
            l=d[p];p+=2 if l&0x80 else 1
            l=d[p]; 
            if l&0x80: l=((l&0x7f)<<8)|d[p+1];p+=2
            else: p+=1
            out.append(d[p:p+l].decode('utf8','replace'))
        else:
            l,=struct.unpack_from('<H',d,p);p+=2
            out.append(d[p:p+2*l].decode('utf-16le','replace'))
    return out
def drop(d,key='fw_tool_settings'):
    pos=8; S=None; chunks=[]
    while pos<len(d):
        t,h,sz=struct.unpack_from('<HHI',d,pos)
        if t==1: S=strings(d,pos)
        chunks.append((pos,t,sz)); pos+=sz
    out=bytearray(d[:8]); depth=None; removed=0
    for pos,t,sz in chunks:
        c=d[pos:pos+sz]
        if depth is None and t==0x102:
            ac,=struct.unpack_from('<H',c,16+12); a0=16+20
            for i in range(ac):
                ns,name,raw=struct.unpack_from('<iii',c,a0+i*20)
                if S[name]=='key' and raw>=0 and S[raw]==key: depth=0
        if depth is not None:
            if t==0x102: depth+=1
            elif t==0x103: depth-=1
            removed+=1
            if depth==0: depth=None
            continue
        out+=c
    struct.pack_into('<I',out,4,len(out))
    return bytes(out),removed
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); o,r=drop(d); open(sys.argv[2],'wb').write(o); print(sys.argv[1],'removed chunks',r)

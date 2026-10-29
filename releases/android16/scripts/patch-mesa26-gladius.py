#!/usr/bin/env python3
"""PS4 Pro (Gladius) raster config for Lineage 23.2's Mesa 26.0.8, on top of
patch-mesa26.py (Liverpool raster config already in the Kaveri case).

The Pro has twice the shader engines and also needs PA_SC_RASTER_CONFIG_1=0x2e
(kernel gfx_v7_0 Gladius setup; 1.1 shipped the same change for Android 13's
Mesa). The Kaveri case leaves raster_config_1 at 0. Point the Kaveri jump-table
entry at the Hawaii block, which sets 0x2e and is never used on a PS4, and give
that block the Gladius raster config 0x2a00161a (Hawaii's is 0x3a00161a).
The start script applies these bytes only on GPU 1002:9924.

Usage: patch-mesa26-gladius.py LIB ABI      prints "offset hexbytes" edits
       patch-mesa26-gladius.py LIB ABI OUT  also writes the patched library
"""
import hashlib,re,struct,subprocess,sys
# Jump table, the Kaveri (family 0x37) entry index, Hawaii block and table base.
ABI={
 'x86_64':dict(table=0x94eaf8,base=0x94eaf8,kaveri_target=0x19822ae,hawaii=0x19822bb,op=0xb9),
 'x86':dict(table=0x8b93b8,base=0x28bf0e0,kaveri_target=0x1a0c9a8,hawaii=0x1a0c98b,op=0xbf),
}
KAVERI_INDEX=0x37-0x31
def fileoff(path,va):
    ph=subprocess.run(['readelf','-lW',path],capture_output=True,text=True,check=True).stdout
    for fo,v,fs in re.findall(r'LOAD\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)',ph):
        fo,v,fs=int(fo,16),int(v,16),int(fs,16)
        if v<=va<v+fs: return va-v+fo
    raise SystemExit('unmapped %x'%va)
def edits(path,abi):
    a=ABI[abi]; b=open(path,'rb').read()
    entry=fileoff(path,a['table'])+4*KAVERI_INDEX
    old=struct.unpack_from('<i',b,entry)[0]
    assert (a['base']+old)&0xffffffff==a['kaveri_target'],'Kaveri entry moved'
    new=struct.pack('<i',a['hawaii']-a['base'])
    h=fileoff(path,a['hawaii'])
    assert b[h:h+5]==bytes([a['op']])+bytes.fromhex('1a16003a'),'Hawaii block moved'
    return [(entry,new),(h+4,b'\x2a')]
if __name__=='__main__':
    path,abi=sys.argv[1],sys.argv[2]
    e=edits(path,abi)
    for o,v in e: print(o,v.hex())
    if len(sys.argv)>3:
        b=bytearray(open(path,'rb').read())
        for o,v in e: b[o:o+len(v)]=v
        open(sys.argv[3],'wb').write(b)
        print('sha256',hashlib.sha256(b).hexdigest())

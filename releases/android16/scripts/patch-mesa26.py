#!/usr/bin/env python3
"""Patch Lineage 23.2's Mesa 26.0.8 libgallium_dri.so for the PS4 GPU.

1. addrlib assertions: this build keeps ADDR_ASSERT, which prints and then
   raise(SIGTRAP). On Liverpool the CI tile-mode table check
   'm_tileTable[15].mode == ADDR_TM_3D_TILED_THIN1' fails for every MSAA
   surface, killing any app that makes one (Play Store, the sign-in WebView).
   Release Mesa compiles these out; each raise(SIGTRAP) call becomes a nop.
2. Liverpool raster config, as 13's patch-mesa-raster.py: the Kaveri case of
   ac_get_raster_config uses PA_SC_RASTER_CONFIG=0x2a00161a instead of 0x2.
   Without it, small round shapes (stars, countdown circles) draw jagged.
"""
import hashlib,re,subprocess,sys
ABI={
 'x86_64':dict(raster=0x19822ae,op=0xb9),
 'x86':dict(raster=0x1a0c96d,op=0xbf),
}
def fileoff(path,va):
    ph=subprocess.run(['readelf','-lW',path],capture_output=True,text=True,check=True).stdout
    for fo,v,fs in re.findall(r'LOAD\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)',ph):
        fo,v,fs=int(fo,16),int(v,16),int(fs,16)
        if v<=va<v+fs: return va-v+fo
    raise SystemExit('unmapped %x'%va)
def patch(src,dst,abi):
    a=ABI[abi]; b=bytearray(open(src,'rb').read())
    dis=subprocess.run(['objdump','-d','--no-show-raw-insn',src],capture_output=True,text=True,check=True).stdout.splitlines()
    n=0
    for i,l in enumerate(dis):
        if not re.search(r'call\s+[0-9a-f]+ <raise@plt>',l): continue
        prev=dis[max(0,i-3):i]
        if not any(re.search(r'mov\s+\$0x5,%edi|movl\s+\$0x5,\(%esp\)|push\s+\$0x5$',x) for x in prev): continue
        o=fileoff(src,int(l.split(':')[0],16)); assert b[o]==0xe8
        b[o:o+5]=b'\x0f\x1f\x44\x00\x00'; n+=1
    o=fileoff(src,a['raster']); assert b[o:o+5]==bytes([a['op']])+bytes.fromhex('02000000'),'raster site moved'
    b[o+1:o+5]=bytes.fromhex('1a16002a')
    open(dst,'wb').write(b)
    print(abi,'traps nopped',n,'sha256',hashlib.sha256(b).hexdigest())
if __name__=='__main__':
    patch(sys.argv[1],sys.argv[2],sys.argv[3])

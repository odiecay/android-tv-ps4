#!/usr/bin/env python3
"""Pad R8 allocations to 512 pixels in Lineage 23.2's libgbm_mesa_wrapper.so.

minigbm (gbm_mesa) allocates YV12 and NV12 video buffers as one R8 buffer and
splits it into planes from the stride Mesa returns. Mesa returns the width
unpadded, but radeonsi only imports dma-buf planes whose pitch is a multiple
of 256 bytes. 1920x1080 YV12 (chroma pitch 960) and most other sizes failed,
and SurfaceFlinger aborts on the first such video frame ("Failed to create a
valid texture"). Android 13's gralloc.gbm had the same bug (YV12 at 720p).

Padding the R8 width to 512 makes the luma pitch a multiple of 512 and the
YV12 chroma pitch (half the luma) a multiple of 256. Other formats are left
alone.

  0x434f  mov 0x8(%rbx),%esi; mov 0xc(%rbx),%edx   ->  call 0x4860; nop
  0x4860  (zero padding after .plt, inside the RX page)
          mov 0x8(%rbx),%esi; mov 0xc(%rbx),%edx
          cmp $0x20203852,%r14d            ; DRM_FORMAT_R8
          jne 1f
          add $0x1ff,%esi; and $0xfffffe00,%esi
       1: ret
The RX LOAD segment grows from 0x860 to 0x880 bytes to cover the helper.
"""
import hashlib,struct,sys

CALL_SITE=0x434f
CALL_ORIG=bytes.fromhex('8b73088b530c')
CAVE=0x4860
HELPER=bytes.fromhex('8b7308' '8b530c' '4181fe52382020' '750c' '81c6ff010000' '81e600feffff' 'c3')

def patch(data):
    b=bytearray(data)
    assert b[CALL_SITE:CALL_SITE+6]==CALL_ORIG,'call site moved'
    assert all(x==0 for x in b[CAVE:CAVE+0x20]),'helper area not empty'
    rel=CAVE-(CALL_SITE+5)
    b[CALL_SITE:CALL_SITE+6]=b'\xe8'+struct.pack('<i',rel)+b'\x90'
    b[CAVE:CAVE+len(HELPER)]=HELPER
    phoff,=struct.unpack_from('<Q',b,0x20); phentsize,phnum=struct.unpack_from('<HH',b,0x36)
    for i in range(phnum):
        e=phoff+i*phentsize
        ptype,flags,off,va,pa,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',b,e)
        if ptype==1 and flags==5 and off==0x4000:
            assert filesz==memsz==0x860,'unexpected RX segment size'
            struct.pack_into('<QQ',b,e+32,0x880,0x880)
            break
    else:
        raise SystemExit('RX segment not found')
    return bytes(b)

if __name__=='__main__':
    out=patch(open(sys.argv[1],'rb').read())
    open(sys.argv[2],'wb').write(out)
    print('libgbm_mesa_wrapper.so patched, sha256',hashlib.sha256(out).hexdigest())

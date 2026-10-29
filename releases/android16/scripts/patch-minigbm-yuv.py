#!/usr/bin/env python3
"""Pad YUV luma rows to 512 bytes in Lineage 23.2's libminigbm_gralloc_gbm_mesa.so.

gbm_mesa_bo_create2 lays out formats Mesa can't allocate (YV12, NV12, ...)
itself: drv_bo_from_format(bo, width, 1, height, format) computes the planes
from the width as luma stride, then one flat R8 buffer of the total size is
allocated. radeonsi only imports dma-buf planes with a 256-byte aligned pitch,
so 1920x1080 YV12 (chroma 960) and most sizes failed and SurfaceFlinger
aborted on the first video frame ("Failed to create a valid texture").
Android 13's gralloc.gbm had the same bug (YV12 at 720p).

A luma stride that is a multiple of 512 makes the YV12 chroma stride (half,
16-aligned) a multiple of 256, and NV12's interleaved chroma equal to it.
The buffer grows with the stride because its size comes from this layout.

  0x18408  mov %r12d,%esi; mov %eax,-0xf4(%rbp)  ->  call 0x1a070; nop x4
  0x1a070  (zero padding after .plt, inside the RX page)
           mov %r12d,%esi; add $0x1ff,%esi; and $0xfffffe00,%esi
           mov %eax,-0xf4(%rbp); ret

gbm_mesa_bo_map then maps those buffers wrong: it imports the flat R8 buffer
as 4096 x pages, but asks gbm_bo_map for a region <size> wide and 1 row high.
Mesa clamps that to one 4096-byte row, so a CPU writer (the video decoder)
only reaches the first 4 KiB of real memory and the GPU shows a green frame.
The map now uses the import's shape.

  0x190d6  mov 0x60(%r14),%r13d; mov $1,%r12d  ->  call 0x1a090; nop x5
  0x1a090  mov 0x60(%r14),%r12d; add $0xfff,%r12d; shr $12,%r12d
           mov $0x1000,%r13d; ret

The RX LOAD segment grows from 0xe070 to 0xe0b0 bytes to cover both helpers.
"""
import hashlib,struct,sys

SITE=0x18408
CAVE=0x1a070
HELPER=bytes.fromhex('4489e6' '81c6ff010000' '81e600feffff' '89850cffffff' 'c3')
MAP_SITE=0x190d6
MAP_CAVE=0x1a090
MAP_HELPER=bytes.fromhex('458b6660' '4181c4ff0f0000' '41c1ec0c' '41bd00100000' 'c3')

def patch(data):
    b=bytearray(data)
    assert b[SITE:SITE+9]==bytes.fromhex('4489e689850cffffff'),'patch site moved'
    assert all(x==0 for x in b[CAVE:CAVE+0x20]),'helper area not empty'
    b[SITE:SITE+9]=b'\xe8'+struct.pack('<i',CAVE-(SITE+5))+b'\x90'*4
    b[CAVE:CAVE+len(HELPER)]=HELPER
    assert b[MAP_SITE:MAP_SITE+10]==bytes.fromhex('458b6e6041bc01000000'),'map site moved'
    assert all(x==0 for x in b[MAP_CAVE:MAP_CAVE+0x20]),'map helper area not empty'
    b[MAP_SITE:MAP_SITE+10]=b'\xe8'+struct.pack('<i',MAP_CAVE-(MAP_SITE+5))+b'\x90'*5
    b[MAP_CAVE:MAP_CAVE+len(MAP_HELPER)]=MAP_HELPER
    phoff,=struct.unpack_from('<Q',b,0x20); phentsize,phnum=struct.unpack_from('<HH',b,0x36)
    for i in range(phnum):
        e=phoff+i*phentsize
        ptype,flags,off,va,pa,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',b,e)
        if ptype==1 and flags==5 and off==0xc000:
            assert filesz==memsz==0xe070,'unexpected RX segment size'
            struct.pack_into('<QQ',b,e+32,0xe0b0,0xe0b0)
            break
    else:
        raise SystemExit('RX segment not found')
    return bytes(b)

if __name__=='__main__':
    out=patch(open(sys.argv[1],'rb').read())
    open(sys.argv[2],'wb').write(out)
    print('libminigbm_gralloc_gbm_mesa.so patched, sha256',hashlib.sha256(out).hexdigest())

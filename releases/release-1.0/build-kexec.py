#!/usr/bin/env python3
"""Compile the pinned v25.1 source with bounded allocation/ownership handling,
once per firmware in magic.h. The payload picks the blob for the running
firmware, so one PKG covers every firmware the loader has offsets for."""
from pathlib import Path
import subprocess, sys, hashlib, json, re
root=Path(__file__).resolve().parent
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
source=root/'loader-source/linux/ps4-kexec-common'
# The 13.52 blob is pinned so loader changes are deliberate. 86ab8e60 was the
# console-confirmed 1.1 loader; 2be911d9 stages the kernel and initramfs in
# pieces (no large contiguous block, the cause of error 12), reports memory
# diagnostics. 83e42de9 adds ATV_INITRD_GAP; booted on a Baikal Pro (11.50).
CONFIRMED={1352:'83e42de9e14131b9e1e2575f52fadb452c208117f3a51c52ea90fc9e769da2d0'}
defines=re.findall(r'^#(?:if|elif) defined\s+(PS4_\d+_\d+)|^#ifdef\s+(PS4_\d+_\d+)',
    (root/'loader-source/linux/magic.h').read_text(),re.M)
defines=[a or b for a,b in defines]
assert len(defines)==len(set(defines))==20,defines
def build(define,directory):
    subprocess.run(['make','-s','-C',str(source),'FW_DEFINE='+define,'BUILD_DIR='+str(directory),'clean'],check=True)
    subprocess.run(['make','-s','-C',str(source),'FW_DEFINE='+define,'BUILD_DIR='+str(directory),
        'EXTRA_CFLAGS=-fcf-protection=none -mno-sse -mno-sse2 -mno-mmx -msoft-float'],check=True)
    blob=(directory/'kexec.bin').read_bytes()
    assert 0<len(blob)<128*1024
    elf=directory/'kexec.elf'
    relocations=subprocess.check_output(['readelf','-r',str(elf)],text=True)
    assert 'There are no relocations' in relocations,relocations
    symbols=subprocess.check_output(['nm','-n',str(elf)],text=True).splitlines()
    entry=[int(x.split()[0],16) for x in symbols if x.split()[-1]=='kexec_init']
    assert entry==[0],entry
    return blob
images=[]
for define in defines:
    major,minor=define.split('_')[1:]
    fw=int(major)*100+int(minor)
    blob=build(define,out/'kexec'/define)
    if fw in CONFIRMED:assert hashlib.sha256(blob).hexdigest()==CONFIRMED[fw],define
    images.append((fw,blob))
# Keep the confirmed 13.52 build at the historical path for build-payload.py.
(out/'kexec/kexec.bin').write_bytes(dict(images)[1352])
# One flat array plus offsets: the payload is position-independent and must
# not contain pointer relocations.
data=b''.join(blob for _,blob in images)
rows=[','.join('0x%02x'%b for b in data[i:i+16]) for i in range(0,len(data),16)]
table=[];offset=0
for fw,blob in images:table.append('{%d,%d,%d}'%(fw,offset,len(blob)));offset+=len(blob)
(out/'kexec-images.h').write_text(
    'struct atv_kexec_image{unsigned short firmware;unsigned int offset,bytes;};\n'
    '#define ATV_KEXEC_MAX_BYTES %d\n'%max(len(b) for _,b in images)+
    'static const struct atv_kexec_image atv_kexec_images[]={'+','.join(table)+'};\n'
    'static const unsigned char atv_kexec_data[]={\n'+',\n'.join(rows)+'\n};\n')
manifest={'upstream':(root/'loader-source/UPSTREAM').read_text().splitlines(),
 'images':{str(fw):{'bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest(),
   'console_confirmed':fw in CONFIRMED} for fw,blob in images},
 'source_sha256':{str(p.relative_to(root/'loader-source')):hashlib.sha256(p.read_bytes()).hexdigest()
  for p in sorted((root/'loader-source').rglob('*')) if p.is_file()}}
(out/'kexec-build.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Source-built loaders:',len(images),'firmware;',len(data),'bytes')

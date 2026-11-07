#!/usr/bin/env python3
"""Build the previously successful GoldHEN runner for the bundled boot files."""
from pathlib import Path
import argparse,hashlib,json,subprocess,struct
p=argparse.ArgumentParser()
for n in ['build','probe-build','probe-linker']:p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();b=a.build.resolve();probe=a.probe_build.resolve();root=Path(__file__).resolve().parent
def run(args):subprocess.run([str(x) for x in args],check=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
# The handoff loader pinned in build-kexec.py (piece staging, pending its
# console boot test; 86ab8e60 was the console-confirmed 1.1 loader).
expected='83e42de9e14131b9e1e2575f52fadb452c208117f3a51c52ea90fc9e769da2d0'
assert sha(b/'kexec/kexec.bin')==expected,'Do not substitute the failed native handoff loader'
flags=['-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-asynchronous-unwind-tables','-mno-red-zone','-mno-sse','-mno-sse2','-mno-mmx','-msoft-float','-masm=intel','-march=btver2','-fPIE','-Wall','-Wextra']
run(['gcc',*flags,'-I',probe,'-I',b,'-I',root/'loader-source/linux/ps4-kexec-common','-c',root/'source/boot-payload.c','-o',b/'boot-payload.o'])
assert 'R_X86_64_64' not in subprocess.check_output(['readelf','-r',b/'boot-payload.o'],text=True)
objects=[probe/(x+'.o') for x in ['kernel','module','file','syscall']]+[b/'boot-payload.o']+[probe/(x+'.o') for x in ['start','memory']]
run(['ld','-T',a.probe_linker,'--gc-sections','--build-id=none','-o',b/'boot-payload.elf',*objects])
run(['objcopy','-O','binary',b/'boot-payload.elf',b/'boot-payload.bin'])
assert 'There are no relocations' in subprocess.check_output(['readelf','-r',b/'boot-payload.elf'],text=True)
blob=(b/'boot-payload.bin').read_bytes();assert 0<len(blob)<1024*1024
rows=[','.join('0x%02x'%v for v in blob[i:i+16]) for i in range(0,len(blob),16)]
(b/'bundled-payload.h').write_text('#define ATV_PAYLOAD_SHA256 "'+sha(b/'boot-payload.bin')+'"\nstatic const unsigned char atv_bundled_payload[]={\n'+',\n'.join(rows)+'\n};\n')
m={'bytes':len(blob),'sha256':sha(b/'boot-payload.bin'),'kernel_loader_sha256':expected,
   'execution':'GoldHEN BinLoader, delivered by app to 127.0.0.1:9090',
   'source_sha256':sha(root/'source/boot-payload.c'),
   'boot_input_checks':(b/'boot-inputs.h').read_text().splitlines(),
   'helper_object_sha256':{x.name:sha(x) for x in objects if x.parent==probe},
   'changed_from_successful_runner':'Keeps the small launcher visible during PREP; waits for matching display release after verified preparation and before ARM. Kernel handoff bytes unchanged.',
   'console_tested':False}
(b/'payload-build.json').write_text(json.dumps(m,indent=2)+'\n')
print('Bundled GoldHEN payload:',len(blob),'bytes;',m['sha256'])

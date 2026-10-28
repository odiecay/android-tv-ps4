"""Execute the real installer entry against an existing fixture installation.
The visible boot stage must start after root restoration and package descriptor
closure, while the display stays open. Failed elevation cannot send a payload.
"""
from pathlib import Path
import tempfile, subprocess, shutil
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp)
    shutil.copytree(root/'tests/stubs',d/'stubs')
    (d/'stubs/orbis/SystemService.h').write_text('int sceSystemServiceLoadExec(const char*,void*);\n')
    (d/'image-metadata.h').write_text('#define SYSTEM_BYTES 13ULL\n#define DATA_BYTES 34359738368ULL\n#define YOUTUBE_BYTES 13ULL\n')
    (d/'assets').mkdir();(d/'install').mkdir()
    (d/'install/ready').write_text('PS4ANDROID13-SPARSE-V015\nedition=32\n')
    (d/'install/system.sfs').write_text('package-bytes')
    for n in ['data-gib.txt','bzImage','initramfs.cpio.xz','bootargs.txt','vram.txt','linux.elf','system.sfs','libgallium-baseline.so','ndk-translation.tar.gz','app-youtube-base.apk','app-spotify-base.apk','app-spotify-x86_64.apk']:
        (d/'assets'/n).write_text('32\n' if n=='data-gib.txt' else 'package-bytes')
    (d/'test.c').write_text(r'''
#define main installer_entry
#include "install-prebuilt.c"
#undef main
#include <assert.h>
#include <setjmp.h>
static jmp_buf executed;
static int restored,root_active,closed,fail_root,attempts;
int ps4_as_root(void(*fn)(void*),void*a){
 if(fail_root)return -1;
 root_active=1;fn(a);root_active=0;restored++;return 0;
}
int installer_ui_init(void){closed=0;return 0;}
int installer_ui_close(void){closed=1;return 0;}
void installer_ui_status(const char*s){(void)s;}
void installer_ui_progress(uint64_t a,uint64_t b){(void)a;(void)b;}
int atv_launch_with_progress(void){
 assert(!root_active&&!closed&&restored==1&&boot_requested);
 for(int i=0;i<ASSET_COUNT;i++)assert(files[i]==-1);
 attempts++;longjmp(executed,1);
}
int sceSystemServiceLoadExec(const char*path,void*a){
 assert(fail_root&&!a&&!root_active&&closed&&!strcmp(path,"exit"));
 attempts++;longjmp(executed,1);
}
int main(void){
 if(!setjmp(executed)){installer_entry();assert(!"entry returned");}
 assert(attempts==1&&restored==1);
 fail_root=1;
 if(!setjmp(executed)){installer_entry();assert(!"entry returned");}
 assert(attempts==2&&!boot_requested);
 puts("PASS: installer closes assets and restores root; same app remains visible during boot; privilege failure cannot boot");
}
''')
    subprocess.run(['cc','-O2','-DRAW_IMAGES','-DEDITION_GIB=32',
        '-DROOT="'+str(d/'install')+'"','-DASSET_ROOT="'+str(d/'assets')+'"',
        '-DBOOT_PARENT="'+str(d/'linux')+'"',
        '-I'+str(d/'stubs'),'-I'+str(root/'source'),'-I'+str(d),
        str(d/'test.c'),str(root/'source/image-copy.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)

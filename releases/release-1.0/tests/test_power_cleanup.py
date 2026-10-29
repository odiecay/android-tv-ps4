"""Exercise real shutdown cleanup with syscall wrappers; never power the host."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp);manifest=d/'apps.txt'
    (d/'test.c').write_text(r'''
#define main supervisor_entry
#include "android-supervisor.c"
#undef main
#include <assert.h>
static const char *expected[]={"/android/system/product/app/Gboard/Gboard.apk","/android/system/app/atv-youtube","/android/system/app/atv-crossyroad","/android/system/product/priv-app/GooglePlayServices","/mnt/android-persistent","/android","/mnt/android-bundled-apps","/mnt/android-lower","/mnt/android-sfs","/mnt/android-share","/mnt/ps4-user"};
static int index_,reboots,busy,attempts,missing_gboard;
int __real_open(const char*,int,...);
int __wrap_open(const char *path,int flags,...){(void)path;(void)flags;return __real_open("/dev/null",O_WRONLY);}
int __wrap_fsync(int fd){(void)fd;return 0;}
int __wrap_access(const char *path,int mode){(void)mode;if(strstr(path,"bootstrap-active")){errno=ENOENT;return -1;}return 0;}
void __wrap_sync(void){}
int __wrap_usleep(useconds_t n){(void)n;return 0;}
int __wrap_umount(const char *path){
 assert(index_<11&&!strcmp(path,expected[index_]));
 attempts++;
 if(index_==0&&missing_gboard){index_++;errno=EINVAL;return -1;}
 if(index_==busy){errno=EBUSY;return -1;}
 index_++;return 0;
}
int __wrap_reboot(int how){assert(index_==11);assert(how==RB_AUTOBOOT||how==RB_POWER_OFF);reboots++;errno=EPERM;return -1;}
static void reset(int fail){index_=reboots=attempts=missing_gboard=0;busy=fail;}
int main(void){
 FILE *f=fopen(ATV_BUNDLED_APPS_LIST,"w");assert(f);fputs("youtube\ncrossyroad\ngms\n",f);fclose(f);
 reset(-1);perform_power("restart");assert(index_==11&&reboots==1);
 reset(-1);perform_power("poweroff");assert(index_==11&&reboots==1);
 reset(-1);missing_gboard=1;perform_power("restart");assert(index_==11&&reboots==1);
 reset(0);perform_power("poweroff");assert(index_==0&&attempts==10&&!reboots);
 reset(2);perform_power("poweroff");assert(index_==2&&attempts==12&&!reboots);
 reset(5);perform_power("poweroff");assert(index_==5&&attempts==15&&!reboots);
 f=fopen(ATV_BUNDLED_APPS_LIST,"w");assert(f);fputs("../userdata\n",f);fclose(f);
 reset(-1);perform_power("poweroff");assert(attempts==1&&!reboots);
 assert(!action_for_status(42<<8));assert(!action_for_status(SIGTERM));
 puts("PASS: Android 16 Gboard and app binds precede overlay/SquashFS/UFS unmount; missing Gboard is tolerated; busy or malformed input prevents console power; crash is not a power request");
}
''')
    cmd=['cc','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'power'),'-DATV_BUNDLED_APPS_LIST="'+str(manifest)+'"',str(d/'test.c'),'-o',str(d/'test')]
    cmd+=['-Wl,--wrap='+s for s in ['open','fsync','access','sync','usleep','umount','reboot']]
    subprocess.run(cmd,check=True);subprocess.run([str(d/'test')],check=True)

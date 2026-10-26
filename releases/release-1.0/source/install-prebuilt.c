#include "ps4-stat-abi.h"
/* Standalone installer with conservative interrupted-copy recovery. App assets opened before elevation. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>
#ifdef RAW_IMAGES
#include "image-copy.h"
#include "boot-inputs.h"
#else
#include "image-extract.h"
#endif
#include "image-metadata.h"
#include "installer-ui.h"
#ifndef EDITION_GIB
#error Edition required
#endif
#ifndef ROOT
#define ROOT "/user/app/ps4-android16"
#endif
/* Boot files live in this app's own folder. The shared /data/linux/boot is
 * left alone, because another PS4 Linux setup may already own it. */
#ifndef BOOT_PARENT
#define BOOT_PARENT ROOT
#endif
#ifndef ASSET_ROOT
#define ASSET_ROOT "/app0/assets"
#endif
#define BOOT_ROOT BOOT_PARENT "/boot"
#define READY ROOT "/ready"
#define GIB (1024ULL*1024*1024)
extern int ps4_as_root(void(*fn)(void*),void*arg);
/* Avoid SDK lstat/fstatat/link/statvfs stubs. */
static int path_stat(const char *path,struct stat *st){
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;
 int result=fstat(fd,st),saved=errno;close(fd);errno=saved;return result;
}
static int commit_file(const char *from,const char *to){
 struct stat st;
 if(path_stat(to,&st)==0){errno=EEXIST;return -1;}
 if(errno!=ENOENT)return -1;
 /* Exclusive staging file serializes this installer's writers. */
 return rename(from,to);
}
#include <bundled-apps.h>
#define BASE_ASSET_COUNT 11
#define ASSET_COUNT (BASE_ASSET_COUNT+BUNDLED_APP_COUNT)
static int files[ASSET_COUNT];
#ifdef RAW_IMAGES
#define DATA_ASSET "data-gib.txt"
#define SYSTEM_ASSET "system.sfs"
#define SYSTEM_OPERATION "Installing compressed Android system"
#define DATA_OPERATION "Copying your prepared data disk"
#else
#define DATA_ASSET "data.zip"
#define SYSTEM_ASSET "system.img.gz"
#define SYSTEM_OPERATION "Extracting the bundled Android system image"
#define DATA_OPERATION "Extracting your preformatted data.img from its supplied ZIP"
#endif
static const char *names[BASE_ASSET_COUNT]={DATA_ASSET,"bzImage","initramfs.cpio.xz","bootargs.txt","vram.txt","linux.elf",SYSTEM_ASSET,"libgallium-baseline.so","ndk-translation.tar.gz","bzImage-baikal","mt7668-wifi.tar.gz"};
/* Boot files copied to BOOT_ROOT: both kernels, the shared initramfs and the
 * boot arguments. The payload picks the kernel for the console's southbridge. */
static const int boot_assets[]={1,2,3,4,9};
static void notice(const char *s){char log[320];snprintf(log,sizeof(log),"[PS4ATV] %s\n",s);sceKernelDebugOutText(0,log);installer_ui_status(s);}
static void failure(const char *s){int saved=errno;char message[256];snprintf(message,sizeof(message),"%s (errno %d)",s,saved);notice(message);
 installer_ui_failed("Setup stopped. Your Android data is kept. See the message above, or report it with the files in logs.");}
static uint64_t completed_bytes;
static const uint64_t install_bytes=SYSTEM_BYTES+BUNDLED_APP_BYTES;
static void progress(uint64_t done,uint64_t total){
 (void)total;installer_ui_progress(completed_bytes+done,install_bytes);
}
/* Comparing installed files byte for byte reads about 1.5 GB, which made every
 * launch take up to two minutes after a restart and fragmented memory just
 * before Linux needed it. Once a file matches, a marker records that file's
 * package ID and its installed size and modification time; later launches
 * trust it while all three are unchanged. A file that changes in a new
 * package gets the full comparison again; unchanged files are skipped. Files
 * the launcher has just copied from the package are recorded the same way. */
/* The ID of the package file installed at this path. Unchanged files keep
 * their ID across builds, so an update doesn't re-read them. */
static const char *asset_id(const char *path){
 static const struct { const char *name, *id; } ids[]=ATV_ASSET_IDS;
 const char *base=strrchr(path,'/');base=base?base+1:path;
 for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)if(!strcmp(ids[i].name,base))return ids[i].id;
 return ATV_ASSET_SET_ID;
}
static void verified_marker(const char *path,const struct stat *st,char *marker,size_t marker_size,char *line,size_t line_size){
 snprintf(marker,marker_size,"%s.verified",path);
 snprintf(line,line_size,"%s %lld %lld\n",asset_id(path),(long long)st->st_size,(long long)st->st_mtime);
}
static int verified_before(const char *path){
 struct stat st;char marker[224],line[128],seen[128]={0};
 if(path_stat(path,&st)||!S_ISREG(st.st_mode))return 0;
 verified_marker(path,&st,marker,sizeof(marker),line,sizeof(line));
 int fd=open(marker,O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;
 ssize_t n=read(fd,seen,sizeof(seen)-1);close(fd);
 return n>0&&strcmp(seen,line)==0;
}
static void remember_verified(const char *path){
 struct stat st;char marker[224],line[128],next[232];
 if(path_stat(path,&st))return;
 verified_marker(path,&st,marker,sizeof(marker),line,sizeof(line));
 snprintf(next,sizeof(next),"%s.next",marker);
 int fd=open(next,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);if(fd<0)return;
 size_t len=strlen(line);int ok=write(fd,line,len)==(ssize_t)len&&fsync(fd)==0;close(fd);
 if(!ok||rename(next,marker))unlink(next);
}
static int file_equal_progress(int source,const char *path,void (*callback)(uint64_t,uint64_t)){
 if(verified_before(path)){
  struct stat a;if(callback&&fstat(source,&a)==0)callback((uint64_t)a.st_size,(uint64_t)a.st_size);
  return 1;
 }
 int target=open(path,O_RDWR|O_NOFOLLOW);if(target<0)return 0;
 struct stat a,b;int ok=fstat(source,&a)==0&&fstat(target,&b)==0&&a.st_size==b.st_size;
 if(ok)ok=ps4_copy_image(source,target,(uint64_t)a.st_size,callback)==0;
 int saved=errno;close(target);
 if(ok)remember_verified(path);
 errno=saved;return ok;
}
static int file_equal(int source,const char *path){return file_equal_progress(source,path,NULL);}
static int copy_file(int source,const char *path){
 struct stat st;if(fstat(source,&st)||st.st_size<=0){errno=EINVAL;return -1;}
 int target=open(path,O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(target<0)return -1;
 int result=ps4_copy_image(source,target,(uint64_t)st.st_size,NULL),saved=errno;
 if(close(target)&&result==0)return -1;errno=saved;return result;
}
static int sync_directory(const char *path){int fd=open(path,O_RDONLY|O_DIRECTORY);if(fd<0)return -1;int r=fsync(fd),e=errno;close(fd);errno=e;return r;}
static int install_translation(void){
 const char *target=ROOT "/ndk-translation.tar.gz";
 const char *stage=ROOT "/ndk-translation.tar.gz.part";
 struct stat st;
 if(path_stat(target,&st)==0){if(file_equal(files[8],target))return 0;errno=EIO;return -1;}
 if(errno!=ENOENT)return -1;
 if(copy_file(files[8],stage)||rename(stage,target)||sync_directory(ROOT))return -1;
 remember_verified(target);
 return 0;
}
/* MT7668 Wi-Fi driver and firmware for later Slim and Pro models. Linux unpacks it
 * after unlocking the drive, which keeps the boot initramfs at its 1.0 size. */
static int install_wifi(void){
 const char *target=ROOT "/mt7668-wifi.tar.gz";
 const char *stage=ROOT "/mt7668-wifi.tar.gz.part";
 if(file_equal(files[10],target))return 0;
 if(copy_file(files[10],stage)||rename(stage,target)||sync_directory(ROOT))return -1;
 remember_verified(target);
 return 0;
}
static int install_graphics(void){
 const char *target=ROOT "/libgallium-baseline.so";
 const char *stage=ROOT "/libgallium-baseline.so.ps4android-next";
 const char *backup=ROOT "/libgallium-baseline.so.before-release-100";
 struct stat st;if(file_equal(files[7],target))return 0;
 int exists=path_stat(target,&st)==0;
 if(!exists&&errno!=ENOENT)return -1;
 if(exists){if(path_stat(backup,&st)==0){errno=EEXIST;return -1;}if(errno!=ENOENT)return -1;}
 if(copy_file(files[7],stage))return -1;
 if(exists&&rename(target,backup))return -1;
 if(rename(stage,target)){int e=errno;if(exists)rename(backup,target);errno=e;return -1;}
 remember_verified(target);
 return sync_directory(ROOT);
}
/* Names the boot-setup step that failed, so an error report says where. */
static char boot_step[192];
static void boot_failure(const char *s){
 int saved=errno;char message[320];snprintf(message,sizeof(message),"%s at %s",s,boot_step);errno=saved;failure(message);
}
#define BOOT_STEP(...) snprintf(boot_step,sizeof(boot_step),__VA_ARGS__)
static int install_boot(void){
 BOOT_STEP("create %s",BOOT_PARENT);
 if(mkdir(BOOT_PARENT,0755)&&errno!=EEXIST)return -1;
 BOOT_STEP("create %s",BOOT_ROOT);
 if(mkdir(BOOT_ROOT,0755)&&errno!=EEXIST)return -1;
 /* Each boot file commits atomically. Android is launched only after every boot file
  * match this package. A retry completes interrupted commits without touching data. */
 for(unsigned i=0;i<sizeof(boot_assets)/sizeof(boot_assets[0]);i++){
  int asset=boot_assets[i];
  char target[128],stage[160],backup[160];struct stat st;
  snprintf(target,sizeof(target),BOOT_ROOT "/%s",names[asset]);
  snprintf(stage,sizeof(stage),"%s.ps4android-next",target);
  snprintf(backup,sizeof(backup),"%s.before-sparse-v015",target);
  BOOT_STEP("check %s",target);
  if(file_equal(files[asset],target))continue;
  int exists=path_stat(target,&st)==0;
  if(!exists&&errno!=ENOENT)return -1;
  /* Keep one backup: the copy being replaced. An older backup is from a previous update. */
  BOOT_STEP("remove old backup %s",backup);
  if(exists&&unlink(backup)&&errno!=ENOENT)return -1;
  BOOT_STEP("write %s",stage);
  if(copy_file(files[asset],stage))return -1;
  BOOT_STEP("back up %s",target);
  if(exists&&rename(target,backup))return -1;
  BOOT_STEP("replace %s",target);
  if(rename(stage,target)){int e=errno;if(exists)rename(backup,target);errno=e;return -1;}
  remember_verified(target);
  BOOT_STEP("sync %s",BOOT_ROOT);
  if(sync_directory(BOOT_ROOT))return -1;
 }
 return 0;
}
static int installation_ready(void){
 char actual[128]={0},expected[128];
 int fd=open(READY,O_RDONLY);if(fd<0)return 0;
 ssize_t n=read(fd,actual,sizeof(actual)-1);close(fd);
 int length=snprintf(expected,sizeof(expected),"PS4ANDROID13-SPARSE-V015\nedition=%d\n",EDITION_GIB);
 return n==length && memcmp(actual,expected,(size_t)length)==0;
}
/* Linux saves its progress log to this folder when startup ends. If the last
 * start failed, show the step it stopped at, so testers can report it without
 * FTP. A successful start overwrites the log, so this only shows after a failure. */
static void show_previous_failure(void){
 char text[4096];int fd=open(ROOT "/logs/progress.log",O_RDONLY|O_NOFOLLOW);if(fd<0)return;
 off_t size=lseek(fd,0,SEEK_END),start=size>(off_t)sizeof(text)-1?size-(off_t)sizeof(text)+1:0;
 ssize_t n=size>0?pread(fd,text,sizeof(text)-1,start):-1;close(fd);if(n<=0)return;text[n]=0;
 char *failed=NULL;for(char *p=text;(p=strstr(p,"SETUP FAILED"));p++)failed=p;
 if(!failed)return;
 while(failed>text&&failed[-1]!='\n')failed--;
 if(failed==text)return;
 char *line=failed-1;*line=0;while(line>text&&line[-1]!='\n')line--;
 char *step=strstr(line,"] ");step=step?step+2:line;
 char message[256];snprintf(message,sizeof(message),"Last start stopped at: %s",step);
 notice(message);sceKernelUsleep(6000000);
}
#include "launch-progress.h"
static int boot_requested;
static void request_boot(void){boot_requested=1;notice("Starting Android TV");}
static int component(int index,const char *name,uint64_t expected,int zipped){
 (void)zipped;char target[192],stage[192];struct stat st;
 snprintf(target,sizeof(target),ROOT "/%s",name);snprintf(stage,sizeof(stage),ROOT "/%s.part",name);
 if(path_stat(target,&st)==0){
  notice("Verifying the system already on your PS4");
  if(!file_equal_progress(files[index],target,progress)){errno=EIO;return -1;}
  completed_bytes+=expected;progress(0,expected);return 0;
 }
 if(errno!=ENOENT)return -1;
 if(path_stat(stage,&st)==0)notice("Checking and resuming the interrupted system copy");
 else if(errno!=ENOENT)return -1;
 int out=open(stage,O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(out<0)return -1;
 int result=ps4_copy_image(files[index],out,expected,progress),saved=errno;
 if(close(out)&&result==0)return -1;errno=saved;if(result)return -1;
 if(commit_file(stage,target)||sync_directory(ROOT))return -1;
 remember_verified(target);
 completed_bytes+=expected;return 0;
}
static int install_bundled_apps(void){
 struct statfs space;struct stat st;uint64_t needed=2*GIB;
 int disk=open(ROOT,O_RDONLY|O_DIRECTORY);if(disk<0)return -1;
 int result=fstatfs(disk,&space),saved=errno;close(disk);errno=saved;if(result)return -1;
 for(int i=0;i<BUNDLED_APP_COUNT;i++){
  char target[192];snprintf(target,sizeof(target),ROOT "/%s",bundled_apps[i].file);
  if(path_stat(target,&st)){
   if(errno!=ENOENT)return -1;
   uint64_t remaining=bundled_apps[i].bytes;char partial[200];snprintf(partial,sizeof(partial),"%s.part",target);
   if(path_stat(partial,&st)==0){
    if(S_ISREG(st.st_mode)&&st.st_size>0&&(uint64_t)st.st_size<remaining)remaining-=(uint64_t)st.st_size;
   }else if(errno!=ENOENT)return -1;
   needed+=remaining;
  }
 }
 uint64_t available=space.f_bavail>0?(uint64_t)space.f_bavail*space.f_bsize:0;
 if(available<needed){errno=ENOSPC;return -1;}
 for(int i=0;i<BUNDLED_APP_COUNT;i++){
  char message[128],target[192];
  snprintf(message,sizeof(message),"Installing %s",bundled_apps[i].label);notice(message);
  if(component(BASE_ASSET_COUNT+i,bundled_apps[i].file,bundled_apps[i].bytes,0))return -1;
  /* Package manager and app UIDs read these APKs through read-only binds. */
  snprintf(target,sizeof(target),ROOT "/%s",bundled_apps[i].file);
  if(chmod(target,0644))return -1;
 }
 return 0;
}
/* Refuse an edition change before any writes. Installing a different PKG must
 * never resize or erase existing userdata implicitly. v014 same-size upgrades
 * are accepted; unrelated or malformed state is preserved for recovery. */
static int compatible_installation(void){
 char text[128]={0},expected[128];int fd=open(READY,O_RDONLY|O_NOFOLLOW);
 if(fd>=0){ssize_t n=read(fd,text,sizeof(text)-1);close(fd);if(n<=0)return 0;
  snprintf(expected,sizeof(expected),"PS4ANDROID13-SPARSE-V015\nedition=%d\n",EDITION_GIB);
  if(strcmp(text,expected)){snprintf(expected,sizeof(expected),"PS4ANDROID13-SPARSE-V014\nedition=%d\n",EDITION_GIB);if(strcmp(text,expected))return 0;}
 }else if(errno!=ENOENT)return 0;
 fd=open(ROOT "/data-gib.txt",O_RDONLY|O_NOFOLLOW);
 if(fd>=0){memset(text,0,sizeof(text));ssize_t n=read(fd,text,sizeof(text)-1);close(fd);snprintf(expected,sizeof(expected),"%d\n",EDITION_GIB);if(n<=0||strcmp(text,expected))return 0;}
 else if(errno!=ENOENT)return 0;
 return 1;
}
static void install_or_boot(void *unused){
 (void)unused;struct stat st;
#ifdef RECOVERY_ONLY
 if(!installation_ready()){notice("Boot repair requires an existing completed Android TV installation.");return;}
 files[6]=open(ROOT "/system.sfs",O_RDONLY|O_NOFOLLOW);
 files[7]=open(ROOT "/libgallium-baseline.so",O_RDONLY|O_NOFOLLOW);
 if(files[6]<0||files[7]<0){failure("Installed Android files are missing; full installer required");return;}
#endif
 if(!compatible_installation()){notice("Different storage edition or unknown installation. Existing data preserved.");return;}
 if(installation_ready()){
  uint64_t check_start=sceKernelGetProcessTime();
  if(path_stat(ROOT "/system.sfs",&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=SYSTEM_BYTES){
   failure("Installed image is missing or has the wrong size");return;}
  if(install_translation()){failure("Cannot install app compatibility files; Android data preserved");return;}
  if(install_bundled_apps()){failure("Cannot install bundled apps; Android data preserved");return;}
  if(install_graphics()){failure("Cannot update graphics files; Android data preserved");return;}
  if(install_wifi()){failure("Cannot install Wi-Fi files; Android data preserved");return;}
  if(install_boot()){boot_failure("Boot files need recovery; Android data preserved");return;}
  {char took[96];uint64_t ms=(sceKernelGetProcessTime()-check_start)/1000;
   snprintf(took,sizeof(took),"Installed files checked in %llu.%01llu s",(unsigned long long)(ms/1000),(unsigned long long)(ms%1000/100));notice(took);}
  show_previous_failure();
  notice("Booting Android TV from the internal HDD");
  request_boot();
  return;
 }
 struct statfs space;int disk=open("/user",O_RDONLY|O_DIRECTORY);
 if(disk<0){failure("Cannot open internal storage");return;}
 int result=fstatfs(disk,&space),saved=errno;close(disk);errno=saved;
 if(result){failure("Cannot check internal free space");return;}
 uint64_t available=space.f_bavail>0?(uint64_t)space.f_bavail*space.f_bsize:0;
 uint64_t needed=install_bytes+2*GIB;
 if(path_stat(ROOT "/system.sfs",&st)==0)needed=2*GIB+BUNDLED_APP_BYTES;
 else if(path_stat(ROOT "/system.sfs.part",&st)==0&&st.st_size>0&&(uint64_t)st.st_size<install_bytes)needed-=(uint64_t)st.st_size;
 if(available<needed){notice("Not enough free internal storage. Keep at least 2 GiB spare after setup.");return;}
 if(mkdir(ROOT,0755)&&errno!=EEXIST){failure("Cannot create installation directory");return;}
 notice(SYSTEM_OPERATION);
 if(component(6,"system.sfs",SYSTEM_BYTES,0)){failure("System image installation failed; partial preserved");return;}
 notice("Preparing Android storage");
 if(copy_file(files[0],ROOT "/data-gib.txt")){failure("Cannot write storage selection");return;}
 notice("Installing graphics and startup files");
 if(install_graphics()){failure("Cannot install graphics library");return;}
 if(install_translation()){failure("Cannot install app compatibility files; Android data preserved");return;}
 if(install_bundled_apps()){failure("Cannot install bundled apps; Android data preserved");return;}
 if(install_wifi()){failure("Cannot install Wi-Fi files; Android data preserved");return;}
  if(install_boot()){boot_failure("Cannot finish boot setup; image files preserved");return;}
 int ready=open(READY ".next",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
 if(ready<0){failure("Cannot record completion");return;}
 char state[128];int n=snprintf(state,sizeof(state),"PS4ANDROID13-SPARSE-V015\nedition=%d\n",EDITION_GIB);
 int ok=write(ready,state,n)==n&&fsync(ready)==0;close(ready);
 if(!ok||rename(READY ".next",READY)||sync_directory(ROOT)){failure("Cannot commit completion marker");return;}
 notice("Installation complete. Starting Android TV.");
 request_boot();
}
int main(void){
 sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
 installer_ui_init();notice("Checking installation and free space");
 for(int i=0;i<ASSET_COUNT;i++)files[i]=-1;
 for(int i=0;i<ASSET_COUNT;i++){
#ifdef RECOVERY_ONLY
 if(i==6||i==7)continue;
#endif
 const char *name=i<BASE_ASSET_COUNT?names[i]:bundled_apps[i-BASE_ASSET_COUNT].file;
 char path[128];snprintf(path,sizeof(path),ASSET_ROOT "/%s",name);files[i]=open(path,O_RDONLY);if(files[i]<0){notice("Package content missing. Nothing installed.");goto end;}}
 if(ps4_as_root(install_or_boot,NULL)){boot_requested=0;notice("Could not obtain or restore required console filesystem access.");}
end:
 for(int i=0;i<ASSET_COUNT;i++)if(files[i]>=0){close(files[i]);files[i]=-1;}
 if(!boot_requested)sceKernelUsleep(10000000);
 if(boot_requested){
  /* Keep the small launcher visible through verified Linux preparation.
   * Only its display is released immediately before the receiver arms boot. */
  (void)atv_launch_with_progress();
  /* Failure stays visible. Never retry or send another payload automatically. */
  for(;;)sceKernelUsleep(1000000);
 }
 installer_ui_close();
 sceSystemServiceLoadExec("exit",NULL);
 /* Never return through the SDK _exit path: it raises SIGSYS here. */
 for(;;)sceKernelUsleep(1000000);
}

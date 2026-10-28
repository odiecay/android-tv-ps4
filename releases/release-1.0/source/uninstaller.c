/* Android TV for PS4 uninstaller. Lists everything any of our releases put on
 * the console, with sizes, and deletes it only after the user presses Cross
 * and then holds Cross for 3 seconds. Nothing outside the fixed list below is
 * touched, and no symlink is followed. */
#include "ps4-stat-abi.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>
#include <orbis/AppInstUtil.h>
#include "installer-font.h"
extern int ps4_as_root(void(*fn)(void*),void*arg);

/* Every step goes to the kernel log (readable over GoldHEN's klog port) and to
 * /data/atv-uninstaller.log, which this tool never deletes. */
static char log_text[16384];static size_t log_used;
static void log_line(const char *fmt,...) __attribute__((format(printf,1,2)));
static void log_line(const char *fmt,...){
 char line[512];va_list ap;va_start(ap,fmt);int n=vsnprintf(line,sizeof(line)-1,fmt,ap);va_end(ap);
 if(n<0)return;if(n>(int)sizeof(line)-2)n=sizeof(line)-2;line[n]='\n';line[n+1]=0;
 char k[540];snprintf(k,sizeof(k),"[ATV uninstaller] %s",line);sceKernelDebugOutText(0,k);
 if(log_used+n+1<sizeof(log_text)){memcpy(log_text+log_used,line,n+1);log_used+=n+1;}
}
static void save_log(void *arg){
 (void)arg;int fd=open("/data/atv-uninstaller.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW,0644);
 if(fd<0)return;ssize_t w=write(fd,log_text,log_used);(void)w;fsync(fd);close(fd);log_used=0;
}

/* ---- What we remove. Paths are fixed; nothing is built from user input. ---- */
enum kind {KIND_DIR,KIND_FILES,KIND_LINUX_BOOT,KIND_APP};
struct item {
 const char *label,*detail;enum kind kind;const char *paths[4];
 int found;uint64_t bytes;int failed;
};
static struct item items[]={
 {.label="Android TV for PS4 1.0 / 1.1 files",.detail="Android system, your Android apps, logins and settings",.kind=KIND_DIR,
  .paths={"/user/app/ps4-android"}},
 {.label="Android TV 16 test build files",.detail="Android system, your Android apps, logins and settings",.kind=KIND_DIR,
  .paths={"/user/app/ps4-android16"}},
 {.label="Startup records",.detail="Small files the launcher writes each start",.kind=KIND_FILES,
  .paths={"/data/atv-payload-result.bin","/data/atv-launch-control.bin","/data/atv16-payload-result.bin","/data/atv16-launch-control.bin"}},
 {.label="Android TV 1.0 boot files in /data/linux/boot",.detail="Only removed when they are Android TV's own, not another Linux",.kind=KIND_LINUX_BOOT,
  .paths={"/data/linux/boot/bzImage","/data/linux/boot/initramfs.cpio.xz","/data/linux/boot/bootargs.txt"}},
 {.label="Android TV for PS4 app",.detail="The app icon (ATVX13001)",.kind=KIND_APP,.paths={"ATVX13001"}},
 {.label="Android TV 16 test build app",.detail="The app icon (ATVX16001)",.kind=KIND_APP,.paths={"ATVX16001"}},
};
#define ITEM_COUNT ((int)(sizeof(items)/sizeof(items[0])))

/* ---- Files, as root. The SDK's lstat is a stub; open with O_NOFOLLOW instead. ---- */
static int path_stat(const char *path,struct stat *st){
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;
 int result=fstat(fd,st),saved=errno;close(fd);errno=saved;return result;
}
struct fs_dirent {uint32_t d_fileno;uint16_t d_reclen;uint8_t d_type;uint8_t d_namlen;char d_name[256];};
/* Walks a real directory. remove=0 adds up disk usage; remove=1 deletes it all.
 * A symlink is never followed: it is counted as nothing and only the link is
 * removed. Returns the number of entries that could not be removed. */
static int walk(const char *path,int remove,uint64_t *bytes){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_DIRECTORY);if(fd<0)return 1;
 int failures=0;char buf[4096];int n;
 while((n=sceKernelGetdents(fd,buf,sizeof(buf)))>0){
  for(int off=0;off<n;){
   struct fs_dirent *d=(struct fs_dirent*)(buf+off);if(!d->d_reclen)break;off+=d->d_reclen;
   if(!d->d_fileno)continue;
   char name[256];int len=d->d_namlen<255?d->d_namlen:255;memcpy(name,d->d_name,len);name[len]=0;
   if(!strcmp(name,".")||!strcmp(name,".."))continue;
   char child[1024];if(snprintf(child,sizeof(child),"%s/%s",path,name)>=(int)sizeof(child)){failures++;continue;}
   struct stat st;
   if(path_stat(child,&st)){
    /* Not openable without following: a symlink (or unreadable). Remove the entry itself only. */
    if(remove&&unlink(child)){failures++;log_line("  could not remove %s (errno %d)",child,errno);}
    continue;
   }
   if(S_ISDIR(st.st_mode)){
    failures+=walk(child,remove,bytes);
    if(remove&&rmdir(child)){failures++;log_line("  could not remove folder %s (errno %d)",child,errno);}
   }else{
    if(bytes)*bytes+=(uint64_t)st.st_blocks*512;
    if(remove&&unlink(child)){failures++;log_line("  could not remove %s (errno %d)",child,errno);}
   }
  }
 }
 close(fd);
 return failures;
}
/* 1.0 kept its kernel in the shared /data/linux/boot. Other PS4 Linux setups use
 * the same folder, so the set is ours only when the kernel names our build and
 * the boot arguments are Android's. */
static int linux_boot_is_ours(void){
 char buf[65536];int fd=open("/data/linux/boot/bzImage",O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;
 ssize_t got=read(fd,buf,sizeof(buf));close(fd);if(got<=0)return 0;
 const char mark[]="-androidtv-ps4";size_t m=strlen(mark);
 int kernel=0;for(ssize_t i=0;i+(ssize_t)m<=got;i++)if(!memcmp(buf+i,mark,m)){kernel=1;break;}
 fd=open("/data/linux/boot/bootargs.txt",O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;
 got=read(fd,buf,4095);close(fd);if(got<=0)return 0;buf[got]=0;
 return kernel&&strstr(buf,"androidboot.")!=NULL;
}
static void scan_files(void *arg){
 (void)arg;
 for(int i=0;i<ITEM_COUNT;i++){
  struct item *it=&items[i];struct stat st;
  if(it->kind==KIND_DIR){
   /* Must be a real directory, not a link to somewhere else. */
   if(path_stat(it->paths[0],&st)==0&&S_ISDIR(st.st_mode)){it->found=1;walk(it->paths[0],0,&it->bytes);}
  }else if(it->kind==KIND_FILES||(it->kind==KIND_LINUX_BOOT&&linux_boot_is_ours())){
   for(int p=0;p<4&&it->paths[p];p++)
    if(path_stat(it->paths[p],&st)==0&&S_ISREG(st.st_mode)){it->found=1;it->bytes+=(uint64_t)st.st_blocks*512;}
  }
 }
}
static void delete_files(void *arg){
 int index=*(int*)arg;struct item *it=&items[index];struct stat st;
 if(it->kind==KIND_DIR){
  if(path_stat(it->paths[0],&st)||!S_ISDIR(st.st_mode)){it->failed=1;return;}
  it->failed=walk(it->paths[0],1,NULL)||rmdir(it->paths[0]);
 }else{
  /* Re-check right before deleting, in case another Linux was put there since the scan. */
  if(it->kind==KIND_LINUX_BOOT&&!linux_boot_is_ours()){it->failed=1;return;}
  for(int p=0;p<4&&it->paths[p];p++)
   if(path_stat(it->paths[p],&st)==0&&S_ISREG(st.st_mode)&&unlink(it->paths[p]))it->failed=1;
  /* The folder is left alone unless 1.0's files were the only thing in it. */
  if(it->kind==KIND_LINUX_BOOT){rmdir("/data/linux/boot");rmdir("/data/linux");}
 }
}

/* ---- Screen ---- */
#define WIDTH 1280
#define HEIGHT 720
#define FRAME_BYTES (WIDTH*HEIGHT*4)
#define MEMORY_BYTES (8*1024*1024)
#define WHITE 0xfff4f8fa
#define DIM 0xffa6bed9
#define RED 0xffff8a80
#define GREEN 0xff8fdc9a
static int video=-1,buffer_index,frame_id;static void *memory;static off_t memory_offset;static uint32_t *buffers[2];
static void rectangle(int x,int y,int w,int h,uint32_t color){
 for(int row=y;row<y+h&&row<HEIGHT;row++){if(row<0)continue;
  for(int col=x;col<x+w&&col<WIDTH;col++)if(col>=0)buffers[buffer_index][row*WIDTH+col]=color;}
}
static void text(int x,int y,int scale,const char *message,uint32_t color){
 const unsigned char *atlas=scale==3?font34:font22,*advance=scale==3?advance34:advance22;
 for(const unsigned char *p=(const unsigned char*)message;*p;p++){
  unsigned ch=*p>=32&&*p<=126?*p-32:'?'-32;
  for(int yy=0;yy<48&&y+yy<HEIGHT;yy++)for(int xx=0;xx<40&&x+xx<WIDTH;xx++){
   if(x+xx<0||y+yy<0)continue;
   unsigned alpha=atlas[yy*3800+ch*40+xx];if(!alpha)continue;
   uint32_t *dst=&buffers[buffer_index][(y+yy)*WIDTH+x+xx],bg=*dst;
   unsigned r=(((color>>16)&255)*alpha+((bg>>16)&255)*(255-alpha))/255;
   unsigned g=(((color>>8)&255)*alpha+((bg>>8)&255)*(255-alpha))/255;
   unsigned b=((color&255)*alpha+(bg&255)*(255-alpha))/255;
   *dst=0xff000000|(r<<16)|(g<<8)|b;
  }
  x+=advance[ch];
 }
}
static void frame_begin(const char *title){
 rectangle(0,0,WIDTH,HEIGHT,0xff071327);
 text(64,36,3,title,WHITE);rectangle(64,90,1152,1,0xff3a5274);
}
static void frame_end(void){
 int id=++frame_id;
 if(sceVideoOutSubmitFlip(video,buffer_index,ORBIS_VIDEO_OUT_FLIP_VSYNC,id)<0)return;
 for(int i=0;i<200;i++){OrbisVideoOutFlipStatus flip;if(sceVideoOutGetFlipStatus(video,&flip)<0||flip.flipArg==id)break;sceKernelUsleep(5000);}
 buffer_index^=1;
}
static int screen_init(void){
 video=sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,ORBIS_VIDEO_OUT_BUS_MAIN,0,NULL);if(video<0)return -1;
 if(sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),MEMORY_BYTES,0x200000,3,&memory_offset)<0)return -1;
 if(sceKernelMapDirectMemory(&memory,MEMORY_BYTES,0x33,0,memory_offset,0x200000)<0)return -1;
 buffers[0]=memory;buffers[1]=(uint32_t*)((char*)memory+FRAME_BYTES);
 OrbisVideoOutBufferAttribute attr;sceVideoOutSetBufferAttribute(&attr,0x80000000,1,0,WIDTH,HEIGHT,WIDTH);
 if(sceVideoOutRegisterBuffers(video,0,(void**)buffers,2,&attr)<0)return -1;
 sceSystemServiceHideSplashScreen();return 0;
}
static void size_text(uint64_t bytes,char *out,size_t n){
 if(bytes>=1000000000ULL)snprintf(out,n,"%.1f GB",bytes/1e9);
 else if(bytes>=1000000ULL)snprintf(out,n,"%.0f MB",bytes/1e6);
 else if(bytes)snprintf(out,n,"%.0f KB",bytes<1000?1.:bytes/1e3);
 else out[0]=0;
}

/* ---- Controller ---- */
static int pad=-1;
static uint32_t buttons(void){
 OrbisPadData data;if(pad<0||scePadReadState(pad,&data)<0||!data.connected)return 0;return data.buttons;
}
static double seconds(void){struct timeval tv;gettimeofday(&tv,NULL);return tv.tv_sec+tv.tv_usec/1e6;}
/* Waits for a fresh press (not one still held from the last screen). */
static uint32_t wait_press(void){
 while(buttons()&(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE))sceKernelUsleep(20000);
 for(;;){uint32_t b=buttons();if(b&ORBIS_PAD_BUTTON_CROSS)return ORBIS_PAD_BUTTON_CROSS;if(b&ORBIS_PAD_BUTTON_CIRCLE)return ORBIS_PAD_BUTTON_CIRCLE;sceKernelUsleep(20000);}
}

/* ---- Screens ---- */
static int list_items(int y,int with_sizes){
 char size[32];
 for(int i=0;i<ITEM_COUNT;i++){struct item *it=&items[i];if(!it->found)continue;
  text(84,y,2,it->label,WHITE);
  if(with_sizes){size_text(it->bytes,size,sizeof(size));text(1060,y,2,size,WHITE);}
  y+=28;text(108,y,2,it->kind==KIND_DIR?it->paths[0]:it->detail,DIM);y+=36;
 }
 return y;
}
static void show_message(const char *title,const char *line1,const char *line2,const char *line3){
 frame_begin(title);text(64,130,2,line1,WHITE);if(line2)text(64,170,2,line2,DIM);if(line3)text(64,210,2,line3,DIM);
 text(64,640,2,"O  Exit",WHITE);frame_end();
}
int main(void){
 sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
 sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
 sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
 sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
 if(screen_init())return 1;
 int user=-1;sceUserServiceInitialize(NULL);sceUserServiceGetInitialUser(&user);
 scePadInit();pad=scePadOpen(user,0,0,NULL);
 frame_begin("Android TV for PS4 uninstaller");text(64,130,2,"Checking what's on this console. Nothing is deleted yet.",WHITE);frame_end();

 int apps=sceAppInstUtilInitialize()==0;
 if(ps4_as_root(scan_files,NULL)){show_message("Android TV for PS4 uninstaller","Could not get the access needed to check your storage.","Make sure GoldHEN is running, then open the uninstaller again.","Nothing was deleted.");wait_press();return 0;}
 for(int i=0;i<ITEM_COUNT;i++)if(items[i].kind==KIND_APP&&apps){int exists=0;if(sceAppInstUtilAppExists(items[i].paths[0],&exists)==0&&exists)items[i].found=1;}
 int count=0;uint64_t total=0;for(int i=0;i<ITEM_COUNT;i++)if(items[i].found){count++;total+=items[i].bytes;}
 log_line("Scan: app utility %s",apps?"ready":"unavailable");
 for(int i=0;i<ITEM_COUNT;i++)log_line("  %s: %s, %llu bytes (%s)",items[i].label,items[i].found?"found":"not found",(unsigned long long)items[i].bytes,items[i].paths[0]);
 ps4_as_root(save_log,NULL);
 if(!count){show_message("Android TV for PS4 uninstaller","Nothing from Android TV for PS4 was found on this console.","There's nothing to remove.",NULL);wait_press();return 0;}

 /* Screen 1: everything that will be removed, and what won't be touched. */
 char size[32],line[160];size_text(total,size,sizeof(size));
 frame_begin("Android TV for PS4 uninstaller");
 text(64,110,2,"This will delete the following from your PS4:",WHITE);
 int y=list_items(150,1);
 snprintf(line,sizeof(line),"Total: %s",size);text(84,y+4,2,line,WHITE);
 text(64,560,2,"Your Android apps, logins and settings go with it.",RED);
 text(64,592,2,"Nothing else is touched: your games, saves and other apps stay.",DIM);
 text(64,648,2,"X  Continue          O  Cancel and exit",WHITE);frame_end();
 if(wait_press()!=ORBIS_PAD_BUTTON_CROSS){log_line("Cancelled on the list screen; nothing deleted");ps4_as_root(save_log,NULL);return 0;}

 /* Screen 2: explicit permission. Holding Cross for 3 s means it can't happen by accident. */
 for(;;){
  while(buttons()&ORBIS_PAD_BUTTON_CROSS)sceKernelUsleep(20000);
  double held=-1;
  for(;;){
   uint32_t b=buttons();
   if(b&ORBIS_PAD_BUTTON_CIRCLE){log_line("Cancelled on the confirm screen; nothing deleted");ps4_as_root(save_log,NULL);return 0;}
   if(b&ORBIS_PAD_BUTTON_CROSS){if(held<0)held=seconds();}else held=-1;
   double progress=held<0?0:(seconds()-held)/3.0;if(progress>1)progress=1;
   frame_begin("Are you sure?");
   snprintf(line,sizeof(line),"This permanently deletes %s from your PS4. It can't be undone.",size);
   text(64,130,2,line,WHITE);
   text(64,170,2,"To delete, hold X for 3 seconds.",WHITE);
   rectangle(64,230,1152,10,0xff253951);rectangle(64,230,(int)(1152*progress),10,0xffff6b5e);
   text(64,648,2,"Hold X  Delete          O  Cancel and exit",WHITE);frame_end();
   if(progress>=1)break;
   sceKernelUsleep(16000);
  }
  break;
 }

 log_line("Confirmed (held X 3 s); deleting");
 /* Screen 3: delete, item by item, showing each as it goes. */
 for(int i=0;i<ITEM_COUNT;i++){
  if(!items[i].found)continue;
  frame_begin("Deleting");y=130;
  for(int j=0;j<ITEM_COUNT;j++){if(!items[j].found)continue;
   const char *mark=j<i?(items[j].failed?"Failed  ":"Done    "):j==i?"Deleting":"Waiting ";
   text(64,y,2,mark,j<i?(items[j].failed?RED:GREEN):j==i?WHITE:DIM);text(220,y,2,items[j].label,j<=i?WHITE:DIM);y+=36;}
  text(64,648,2,"Keep your PS4 switched on.",DIM);frame_end();
  if(items[i].kind==KIND_APP){int rc=apps?sceAppInstUtilAppUnInstall(items[i].paths[0]):-1;items[i].failed=rc!=0;log_line("  uninstall %s returned 0x%x",items[i].paths[0],(unsigned)rc);}
  else if(ps4_as_root(delete_files,&i))items[i].failed=1;
  log_line("  %s: %s",items[i].label,items[i].failed?"FAILED":"removed");
  ps4_as_root(save_log,NULL);
 }
 int failures=0;for(int i=0;i<ITEM_COUNT;i++)if(items[i].found&&items[i].failed)failures++;
 log_line("Finished: %d failed",failures);ps4_as_root(save_log,NULL);
 frame_begin(failures?"Finished with problems":"Done");y=130;
 for(int j=0;j<ITEM_COUNT;j++){if(!items[j].found)continue;
  text(64,y,2,items[j].failed?"Failed  ":"Removed ",items[j].failed?RED:GREEN);text(220,y,2,items[j].label,WHITE);y+=36;}
 if(failures)text(64,y+20,2,"Some items could not be removed. Ask in the Discord and say which ones.",RED);
 else{snprintf(line,sizeof(line),"Freed %s. Android TV for PS4 is gone from this console.",size);text(64,y+20,2,line,WHITE);}
 text(64,y+60,2,"You can delete this uninstaller from Settings > Storage when you're done.",DIM);
 text(64,648,2,"O  Exit",WHITE);frame_end();
 while(wait_press()!=ORBIS_PAD_BUTTON_CIRCLE);
 return 0;
}

/* Native Orbis display; installation remains on the PS4's internal disk. */
#include "installer-ui.h"
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/SystemService.h>
#include <sys/time.h>
#include "installer-font.h"
#include "build-id.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "installer-artwork.h"
#include "installer-slide.h"
#define WIDTH 1280
#define HEIGHT 720
#define FRAME_BYTES (WIDTH*HEIGHT*4)
#define MEMORY_BYTES (8*1024*1024)
#ifndef ATV_ARTWORK_ROOT
#define ATV_ARTWORK_ROOT "/app0/artwork"
#endif
static int video=-1, buffer_index, frame_id, displayed_percent=-1;
static off_t memory_offset;
static void *memory;
static uint32_t *buffers[2];
static int slide_fds[INSTALLER_SLIDE_COUNT]={-1,-1,-1,-1};
static char status[256]="Checking installation and free space";
static uint64_t done_bytes,total_bytes;
static int boot_mode;
/* Set once startup has stopped: the bar stops moving and the detail wraps. */
static int failed;
static char boot_detail[160];
static void rectangle(int x,int y,int w,int h,uint32_t color){
 for(int row=y;row<y+h&&row<HEIGHT;row++){
  if(row<0)continue;
  for(int col=x;col<x+w&&col<WIDTH;col++)if(col>=0)buffers[buffer_index][row*WIDTH+col]=color;
 }
}
static void log_result(const char *op,int rc){
 char message[160];snprintf(message,sizeof(message),"[PS4ATV UI] %s: 0x%x\n",op,(unsigned)rc);sceKernelDebugOutText(0,message);
}
static void text(int x,int y,int scale,const char *message,uint32_t color){
 const unsigned char *atlas=scale==3?font34:font22;
 const unsigned char *advance=scale==3?advance34:advance22;
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
/* Pixel width of a string in the given font, for wrapping to the screen. */
static int text_width(int scale,const char *s,size_t n){
 const unsigned char *advance=scale==3?advance34:advance22;int w=0;
 for(size_t i=0;i<n&&s[i];i++){unsigned char c=(unsigned char)s[i];w+=advance[c>=32&&c<=126?c-32:'?'-32];}
 return w;
}
/* Longest prefix of s, ending at a space, that fits in width pixels. */
static size_t fit_words(int scale,const char *s,int width,size_t limit){
 size_t len=strlen(s);if(len>limit)len=limit;
 if(text_width(scale,s,len)<=width)return len;
 size_t best=0;for(size_t i=1;i<=len;i++)if(s[i]==' '||s[i]==0){if(text_width(scale,s,i)>width)break;best=i;}
 if(best)return best;
 while(len>1&&text_width(scale,s,len)>width)len--;return len;
}
static double seconds(void){struct timeval tv;gettimeofday(&tv,NULL);return tv.tv_sec+tv.tv_usec/1000000.;}
static double started,last_draw,opened;
static uint64_t initial_bytes;
static int splash_hidden;
static void render(void){
 if(video<0)return;
 int slide=(int)((seconds()-opened)/12)%INSTALLER_SLIDE_COUNT;if(slide<0)slide=0;
 if(!boot_mode){
  if(atv_read_slide(slide_fds[slide],buffers[buffer_index],FRAME_BYTES)){
   rectangle(0,0,WIDTH,HEIGHT,0xff071327);
   text(64,180,3,"Android TV for PS4 16",0xfff4f8fa);
  }
  for(int i=0;i<INSTALLER_SLIDE_COUNT;i++)rectangle(64+i*30,480,18,4,i==slide?0xffe2f2ff:0xff4d6c91);
 }
 /* Keep the preview already in VideoOut memory during preparation. No image
  * allocations, file reads or mapped artwork remain in the boot phase. */
 rectangle(0,538,WIDTH,182,0xff080f1d);
 rectangle(0,538,WIDTH,1,0xff3a5274);
 char line[160];const char *p=status;int y=550;
 while(*p&&y<607){size_t n=strlen(p);if(n>79){n=79;while(n>0&&p[n]!=' ')n--;if(!n)n=79;}
  memcpy(line,p,n);line[n]=0;text(64,y,2,line,0xfff4f8fa);p+=n;while(*p==' ')p++;y+=28;}
 int percent=total_bytes?(int)(done_bytes*100/total_bytes):0;if(percent>100)percent=100;
 char detail[180];
 if(total_bytes){snprintf(detail,sizeof(detail),"%d%%",percent);text(1100,552,3,detail,0xfff4f8fa);}
 rectangle(64,620,1152,7,0xff253951);
 if(failed){
  /* A stopped start shows no activity, and its instruction gets the full width. */
  const char *q=boot_detail;int fy=648;
  while(*q&&fy<=676){size_t n=fit_words(2,q,1152,sizeof(line)-1);
   memcpy(line,q,n);line[n]=0;text(64,fy,2,line,0xffd8e6f5);q+=n;while(*q==' ')q++;fy+=28;}
  goto flip;
 }
 if(total_bytes)rectangle(64,620,1152*percent/100,7,0xff75bdff);
 else {int pos=(int)((seconds()-opened)*45)%1032;rectangle(64+pos,620,120,7,0xff75bdff);}
 if(total_bytes)snprintf(detail,sizeof(detail),"%.2f / %.2f GiB processed",(double)done_bytes/1073741824.,(double)total_bytes/1073741824.);
 else snprintf(detail,sizeof(detail),"%s",boot_mode?boot_detail:"Keep your PS4 switched on during setup.");
 text(64,648,2,detail,0xffa6bed9);
 if(started&&total_bytes&&done_bytes<total_bytes){
  double elapsed=seconds()-started;
  if(elapsed>2&&done_bytes){double rate=(done_bytes-initial_bytes)/elapsed;
   if(rate<=0)rate=1;
   snprintf(detail,sizeof(detail),"%.1f MiB/s  |  ~%.0f min remaining",rate/1048576.,(total_bytes-done_bytes)/rate/60.);
   text(480,648,2,detail,0xffa6bed9);
  }
 }
 snprintf(detail,sizeof(detail),"%d GiB  /  " ATV_BUILD_ID,EDITION_GIB);text(1030,648,2,detail,0xffa6bed9);
 flip:;
 int id=++frame_id;
 int rc=sceVideoOutSubmitFlip(video,buffer_index,ORBIS_VIDEO_OUT_FLIP_VSYNC,id);
 if(rc<0){log_result("submit flip",rc);return;}
 for(int i=0;i<200;i++){OrbisVideoOutFlipStatus flip;
  if(sceVideoOutGetFlipStatus(video,&flip)<0||flip.flipArg==id)break;
  sceKernelUsleep(5000);
 }
 if(!splash_hidden){int rc=sceSystemServiceHideSplashScreen();log_result("hide splash",rc);if(rc>=0)splash_hidden=1;}
 buffer_index^=1;
}
int installer_ui_init(void){
 if(video>=0)return 0;
 buffer_index=frame_id=boot_mode=splash_hidden=failed=0;
 done_bytes=total_bytes=initial_bytes=0;started=last_draw=0;displayed_percent=-1;
 opened=seconds();
 video=sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,ORBIS_VIDEO_OUT_BUS_MAIN,0,NULL);
 log_result("open VideoOut",video);if(video<0)return -1;
 if(sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),MEMORY_BYTES,0x200000,3,&memory_offset)<0)goto failed;
 if(sceKernelMapDirectMemory(&memory,MEMORY_BYTES,0x33,0,memory_offset,0x200000)<0){sceKernelReleaseDirectMemory(memory_offset,MEMORY_BYTES);goto failed;}
 buffers[0]=memory;buffers[1]=(uint32_t*)((char*)memory+FRAME_BYTES);
 /* Open while the app still has its normal sandbox. Installer elevation
  * changes filesystem visibility; descriptors remain usable during copying. */
 for(int i=0;i<INSTALLER_SLIDE_COUNT;i++){
  char path[512];snprintf(path,sizeof(path),ATV_ARTWORK_ROOT "/slide-%d.rgba",i);
  slide_fds[i]=open(path,O_RDONLY);
 }
 OrbisVideoOutBufferAttribute attr;
 sceVideoOutSetBufferAttribute(&attr,0x80000000,1,0,WIDTH,HEIGHT,WIDTH);
 int rc=sceVideoOutRegisterBuffers(video,0,(void**)buffers,2,&attr);log_result("register buffers",rc);if(rc<0){installer_ui_close();return -1;}
 render();return 0;
failed:sceVideoOutClose(video);video=-1;return -1;
}
void installer_ui_status(const char *message){snprintf(status,sizeof(status),"%s",message);render();}
void installer_ui_progress(uint64_t written,uint64_t total){
 if(!started){started=seconds();initial_bytes=written;}done_bytes=written;total_bytes=total;int percent=total?(int)(written*100/total):0;
 if(percent!=displayed_percent||seconds()-last_draw>=1){displayed_percent=percent;last_draw=seconds();
  render();}
}
void installer_ui_boot_status(const char *message,const char *detail){
 if(!boot_mode){
  /* buffer_index names the next draw target; preserve the displayed preview
   * in both buffers before closing every artwork descriptor. */
  if(memory)memcpy(buffers[buffer_index],buffers[buffer_index^1],FRAME_BYTES);
  for(int i=0;i<INSTALLER_SLIDE_COUNT;i++)if(slide_fds[i]>=0){close(slide_fds[i]);slide_fds[i]=-1;}
 }
 boot_mode=1;total_bytes=done_bytes=0;
 snprintf(status,sizeof(status),"%s",message);
 snprintf(boot_detail,sizeof(boot_detail),"%s",detail);
 render();
}
void installer_ui_boot_error(const char *message,const char *detail){
 failed=1;installer_ui_boot_status(message,detail);
}
void installer_ui_failed(const char *detail){
 failed=1;snprintf(boot_detail,sizeof(boot_detail),"%s",detail);render();
}
int installer_ui_close(void){
 if(video>=0){int rc=sceVideoOutClose(video);if(rc<0){log_result("close VideoOut",rc);return rc;}video=-1;}
 for(int i=0;i<INSTALLER_SLIDE_COUNT;i++)if(slide_fds[i]>=0){close(slide_fds[i]);slide_fds[i]=-1;}
 if(memory){
  int rc=sceKernelMunmap(memory,MEMORY_BYTES);if(rc<0){log_result("unmap display",rc);return rc;}
  memory=NULL;
  rc=sceKernelReleaseDirectMemory(memory_offset,MEMORY_BYTES);if(rc<0){log_result("release display",rc);return rc;}
 }
 return 0;
}

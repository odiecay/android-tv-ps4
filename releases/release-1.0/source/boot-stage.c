/* The app stays visible while GoldHEN prepares Linux in ScePartyDaemon.
 * Only the receiver executes the handoff. This app releases its display when
 * preparation is verified, then acknowledges that condition to the receiver. */
#include "../loader-source/linux/ps4-kexec-common/boot-control.h"
#include "build-id.h"
#include "ps4-stat-abi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/stat.h>
#include <orbis/libkernel.h>
#include "payload-send.h"
#include "bundled-payload.h"
#include "installer-ui.h"
#include "launch-progress.h"
#include "launch-protocol.h"
#ifndef ROOT
#define ROOT "/user/app/ps4-android16"
#endif
extern int ps4_as_root(void (*fn)(void *),void *arg);
static int logfd=-1,controlfd=-1,resultfd=-1,access_error;
static struct atv_launch_control control;
static int regular_file(int fd){struct stat st;return fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode);}
static void open_channels(void *unused){
 (void)unused;
 /* All logs live in ROOT/logs, the start before in logs/previous: after a
  * black screen the next launch would otherwise overwrite the only record
  * of how far it got. Linux moves its own logs the same way. */
 (void)mkdir(ROOT "/logs",0755);(void)mkdir(ROOT "/logs/previous",0755);
 (void)rename(ROOT "/logs/launcher.log",ROOT "/logs/previous/launcher.log");
 logfd=open(ROOT "/logs/launcher.log",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
 controlfd=open(ATV_CONTROL_PATH,O_RDWR|O_CREAT|O_NOFOLLOW,0600);
 resultfd=open(ATV_RESULT_PATH,O_RDWR|O_CREAT|O_NOFOLLOW,0600);
 if(!regular_file(logfd)||!regular_file(controlfd)||!regular_file(resultfd))goto failed;
 if(ftruncate(controlfd,0)||ftruncate(resultfd,0))goto failed;
 if(write(controlfd,&control,sizeof(control))!=(ssize_t)sizeof(control)||fsync(controlfd))goto failed;
 return;
failed:access_error=errno?errno:EIO;
}
static void marker(const char *s){
 sceKernelDebugOutText(0,s);
 if(logfd>=0){ssize_t n=write(logfd,s,strlen(s));if(n>=0)(void)fsync(logfd);}
}
/* Linux can't log anything if it stops before its startup script runs (for
 * example a kernel that hangs on unsupported hardware). Catch that here: if
 * the last launch handed over to Linux but Linux's progress.log is older than
 * that launch's log, Linux never got as far as writing it. */
static void previous_start_note(void *unused){
 (void)unused;
 char text[8192];struct stat last,linux_log;
 int fd=open(ROOT "/logs/previous/launcher.log",O_RDONLY|O_NOFOLLOW);if(fd<0)return;
 ssize_t n=read(fd,text,sizeof(text)-1);int ok=fstat(fd,&last);close(fd);
 if(n<=0||ok)return;text[n]=0;
 if(!strstr(text,"authorizing the prepared Linux handoff"))return;
 if(stat(ROOT "/logs/progress.log",&linux_log)||linux_log.st_mtime<last.st_mtime)
  marker("Previous start: handed over to Linux, but Linux never wrote its logs, so it stopped before its startup (kernel, southbridge or display).\n");
 else marker("Previous start: Linux ran; its logs are in this folder.\n");
}
static int flag_at(off_t offset){
 unsigned int flag=1;
 return pwrite(controlfd,&flag,sizeof(flag),offset)==sizeof(flag)&&!fsync(controlfd)?0:-1;
}
static void close_channels(void){
 if(logfd>=0)close(logfd);if(controlfd>=0)close(controlfd);if(resultfd>=0)close(resultfd);
 logfd=controlfd=resultfd=-1;
}
static int fail_with_detail(const char *message,const char *explanation,int code){
 char detail[160],log[320];
 if(explanation)snprintf(detail,sizeof(detail),"%s (code %d)",explanation,code);
 else snprintf(detail,sizeof(detail),"Startup stopped (code %d). See logs/launcher.log.",code);
 snprintf(log,sizeof(log),"%s: %d\n",message,code);marker(log);
 if(code==ECONNREFUSED){
  message="BinLoader unavailable. Press PS, fully close this app, then toggle BinLoader off and on.";
  snprintf(detail,sizeof(detail),"Reopen Android TV to continue. Installation is preserved. (code %d)",code);
 }
 if(code==ENOMEM){
  /* Linux needs one large contiguous block; long PS4 sessions fragment memory. */
  message="Restart your PS4, then open Android TV again";
  snprintf(detail,sizeof(detail),"There wasn't enough free memory to start Linux. This is common right after installing or updating. Nothing was changed. (code %d)",code);
 }
 /* Everything the PS4 side can stop on is explained here, so a report says
  * what happened rather than only a number. Nothing below changes the PS4. */
 const char *advice=NULL;
 switch(code){
 case -1:case -2:case -3:case -104:case -105:case -108:
  message="Android TV's startup files are missing or damaged";
  advice="Open Android TV again to repair them. Your Android data is kept.";break;
 case -103:
  message="Couldn't identify this console's southbridge";
  advice="Nothing was changed. Please report your console model and firmware.";break;
 case -106:
  message="Linux didn't pass its final check";
  advice="Nothing was started. Restart the PS4 and try again; report it if it repeats.";break;
 case -107:
  message="Linux couldn't be armed to start";
  advice="Nothing was started. Restart the PS4 and try again; report it if it repeats.";break;
 case EINVAL:
  if(strcmp(message,"Linux preparation reported an error"))break;
  message="The Linux loader couldn't prepare this console";
  advice="Its GPU firmware or kernel check failed. Nothing was changed. Please report your model.";break;
 case ETIMEDOUT:
  message="GoldHEN didn't finish starting Linux";
  advice="Nothing was changed. Restart the PS4, load GoldHEN and try again.";break;
 }
 if(advice)snprintf(detail,sizeof(detail),"%s (code %d)",advice,code);
 if(controlfd>=0)(void)flag_at(offsetof(struct atv_launch_control,cancelled));
 (void)installer_ui_init();installer_ui_boot_error(message,detail);close_channels();return -1;
}
static int fail(const char *message,int code){return fail_with_detail(message,NULL,code);}
/* get_sb_id returns the raw ID (0x10300 on a CUH-1102A); the family is the top byte. */
/* The runner's result file is the launch status followed by the loader's
 * boot report; log its memory diagnostics once they are filled in. */
static void log_memory(void){
 struct atv_boot_report r;char line[256];
 if(pread(resultfd,&r,sizeof(r),sizeof(struct atv_launch_status))!=(ssize_t)sizeof(r)||!r.old_method)return;
 snprintf(line,sizeof(line),"Memory: kernel in %u pieces, initramfs in %u pieces, smallest %u KB. Old single-block method: %s\n",
  r.kernel_pieces,r.initrd_pieces,r.smallest_piece/1024,r.old_method==1?"block free, would have worked":"no free block, would have waited and likely failed (code 12)");
 marker(line);
}
static const char *southbridge_name(int sb){
 switch(sb>>16){case 1:return "Aeolia";case 2:return "Belize";case 3:return "Baikal";case 4:return "Belize 2";default:return "unknown";}
}
static int unsupported_firmware(const struct atv_launch_status *s){
 char message[96];
 snprintf(message,sizeof(message),"Firmware %u.%02u isn't supported yet",s->firmware/100,s->firmware%100);
 return fail_with_detail(message,"The Linux loader has no kernel offsets for this firmware. Nothing was changed.",s->rc);
}
static const char *stage_message(int stage){
 switch(stage){
 case 0:return "Waiting for GoldHEN to accept the Android TV payload";
 case 1:return "Checking console compatibility";
 case 2:return "Reading the Linux kernel and startup files";
 case 3:return "Preparing the console for Linux";
 case 4:return "Preparing Linux memory and startup data";
 case 5:case 6:return "Verifying Linux is ready to start";
 default:return "Starting Linux. The display will reconnect during startup.";
 }
}
int atv_launch_with_progress(void){
 logfd=controlfd=resultfd=-1;access_error=0;
 memset(&control,0,sizeof(control));control.magic=ATV_LAUNCH_MAGIC;control.version=ATV_LAUNCH_VERSION;
 control.launcher_pid=(unsigned)getpid();
 control.nonce=((unsigned long long)sceKernelGetProcessTime()<<16)^(unsigned)getpid();if(!control.nonce)control.nonce=1;
 /* Open descriptors while privileged, then restore credentials BEFORE delivery. */
 int root_rc=ps4_as_root(open_channels,NULL);
 if(root_rc||access_error)return fail("Cannot open Android TV startup controls",root_rc?root_rc:access_error);
 marker("Android TV for PS4 " ATV_BUILD_ID "\n");
 marker("Android TV: keeping the app visible during GoldHEN loopback startup\n");
 marker("Destination 127.0.0.1:9090; payload SHA256 " ATV_PAYLOAD_SHA256 "\n");
 (void)ps4_as_root(previous_start_note,NULL);
 installer_ui_boot_status("Starting Android TV " ATV_BUILD_ID,"Preparing to start Linux. Plug in a USB stick now to save the logs to it.");
 size_t sent=0;int rc=atv_send_payload(atv_bundled_payload,sizeof(atv_bundled_payload),&sent),saved=errno;
 char line[256];snprintf(line,sizeof(line),"Payload delivery result=%d bytes=%zu/%zu errno=%d. Delivery alone does not confirm Linux boot.\n",rc,sent,sizeof(atv_bundled_payload),rc?saved:0);marker(line);
 if(rc)return fail(saved==ECONNREFUSED?"Enable GoldHEN > Servers > BinLoader, then open Android TV again.":"Android TV payload could not be sent",saved);
 unsigned long long started=sceKernelGetProcessTime(),last_draw=0;
 int last_stage=0,released=0,console_logged=0,memory_logged=0;
 for(;;){
  unsigned long long now=sceKernelGetProcessTime();
  struct atv_launch_status status;
  ssize_t n=pread(resultfd,&status,sizeof(status),0);
  if(n==sizeof(status)&&atv_status_valid(&status,control.nonce)){
   if(status.rc==ATV_RC_UNSUPPORTED_FIRMWARE)return unsupported_firmware(&status);
   if(status.stage>=5&&!memory_logged){log_memory();memory_logged=1;}
   if(status.rc)return fail("Linux preparation reported an error",status.rc);
   if(status.stage>=2&&!console_logged){
    snprintf(line,sizeof(line),"Console firmware=%u base=%u southbridge=%s (0x%x)\n",status.firmware,status.firmware_base,southbridge_name(status.southbridge),(unsigned)status.southbridge);marker(line);
    console_logged=1;
   }
   if(status.stage>last_stage){
    snprintf(line,sizeof(line),"Startup stage=%d elapsed_us=%llu prepare_us=%llu\n",status.stage,now-started,status.elapsed);marker(line);
    last_stage=status.stage;
   }
   if(last_stage==7&&!released){
    installer_ui_boot_status(stage_message(7),"Linux is ready. Android will prepare its apps on the first start.");
    rc=installer_ui_close();if(rc)return fail("Could not release the display for Linux",rc);
    marker("App display released; authorizing the prepared Linux handoff\n");
    if(flag_at(offsetof(struct atv_launch_control,released)))return fail("Could not acknowledge the Linux handoff",errno?errno:EIO);
    released=1;
   }
  }
  if(!released&&(last_draw==0||now-last_draw>=1000000)){
   snprintf(line,sizeof(line),"Starting Android TV  |  %llu seconds",(now-started)/1000000);
   installer_ui_boot_status(stage_message(last_stage),line);last_draw=now;
  }
  /* A timeout is a failure guard, never a condition for starting Linux. */
  if(now-started>300000000ULL)return fail("Android TV startup did not complete",ETIMEDOUT);
  sceKernelUsleep(50000);
 }
}

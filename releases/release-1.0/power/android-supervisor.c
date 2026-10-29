#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#define STATE "/run/androidtv-ps4"
#ifndef ATV_BUNDLED_APPS_LIST
#define ATV_BUNDLED_APPS_LIST "/opt/androidtv-ps4/bundled-apps.txt"
#endif
#define STACK_BYTES (1024*1024)
struct child_args {char **argv;int test_signal;};
static int child_main(void *arg){
 struct child_args *a=arg;
 /* A reboot test must never reach the initial PID namespace. */
 if(getpid()!=1)_exit(97);
 if(mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL))_exit(98);
 if(a->test_signal){
  if(a->test_signal==SIGHUP)reboot(RB_AUTOBOOT);
  else if(a->test_signal==SIGINT)reboot(RB_POWER_OFF);
  else _exit(42);
  _exit(99);
 }
 execv(a->argv[0],a->argv);perror("Android init exec");_exit(127);
}
static const char *action_for_status(int status){
 if(!WIFSIGNALED(status))return NULL;
 if(WTERMSIG(status)==SIGHUP)return "restart";
 if(WTERMSIG(status)==SIGINT)return "poweroff";
 return NULL;
}
static int write_state(const char *path,const char *value){
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;
 size_t length=strlen(value);int ok=write(fd,value,length)==(ssize_t)length&&fsync(fd)==0;
 int e=errno;close(fd);errno=e;return ok?0:-1;
}
static int unmount_path(const char *path){
 for(int retry=0;retry<10;retry++){
  if(umount(path)==0||errno==EINVAL||errno==ENOENT)return 0;
  if(errno!=EBUSY)break;
  sync();usleep(200000);
 }
 fprintf(stderr,"Refusing console power operation: cannot unmount %s: %s\n",path,strerror(errno));return -1;
}
static int unmount_bundled_apps(void){
 FILE *list=fopen(ATV_BUNDLED_APPS_LIST,"r");
 if(!list)return errno==ENOENT?0:-1;
 char name[128],path[192];int result=0;
 while(fgets(name,sizeof(name),list)){
  size_t length=strcspn(name,"\r\n");name[length]=0;
  if(!length)continue;
  if(length>=sizeof(name)-1){result=-1;break;}
  for(size_t i=0;i<length;i++)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_'||name[i]=='-')){result=-1;break;}
  if(result)break;
  if(!strcmp(name,"gms"))snprintf(path,sizeof(path),"/android/system/product/priv-app/GooglePlayServices");
  else snprintf(path,sizeof(path),"/android/system/app/atv-%s",name);
  if(unmount_path(path)){result=-1;break;}
 }
 if(ferror(list))result=-1;
 fclose(list);
 return result;
}
static int perform_power(const char *action){
 /* Never power down a development host accidentally. */
 if(access("/key/eap_hdd_key.bin",R_OK)||access(STATE "/allow-console-power",F_OK)){
  fprintf(stderr,"Power guard failed: PS4 console boot markers absent\n");return 1;
 }
 char request[64];snprintf(request,sizeof(request),"%s\n",action);
 if(write_state(STATE "/power-request",request)){perror("power request");return 1;}
 fprintf(stderr,"[Android TV] Android has stopped; preparing console %s\n",action);
 /* The bootstrap monitor notices power-request and finishes its local logs. */
 /* Up to 15 s for the final log copy (drive and USB stick). */
 for(int i=0;i<150&&access(STATE "/bootstrap-active",F_OK)==0;i++)usleep(100000);
 sync();
 /* These binds were created in the host before Android's mount namespace.
  * They survive Android init exiting and must be released before both the
  * system overlay and the app SquashFS, which also pins the PS4 filesystem. */
 /* Android 16 binds Gboard from the read-only system image into the host
  * overlay before cloning Android's mount namespace. Android exiting does
  * not remove that host-side bind: leaving it mounted makes /android busy
  * and also pins /mnt/android-lower. Release it before either filesystem.
  * Missing Gboard (or an already removed bind) is handled by unmount_path.
  * Never force/lazy-unmount or reboot past a failed storage cleanup. */
 if(unmount_path("/android/system/product/app/Gboard/Gboard.apk"))return 1;
 if(unmount_bundled_apps())return 1;
 const char *mounts[]={"/mnt/android-persistent","/android","/mnt/android-bundled-apps","/mnt/android-lower","/mnt/android-sfs","/mnt/android-share","/mnt/ps4-user"};
 for(size_t i=0;i<sizeof(mounts)/sizeof(mounts[0]);i++){
  if(unmount_path(mounts[i]))return 1;
 }
 sync();
 fprintf(stderr,"[Android TV] Filesystems closed; console %s\n",action);
 if(reboot(!strcmp(action,"restart")?RB_AUTOBOOT:RB_POWER_OFF))perror("PS4 power syscall");
 return 1;
}
int main(int argc,char **argv){
 int dry=0,test_signal=0;char **command=NULL;
 if(argc==3&&!strcmp(argv[1],"--test")){
  dry=1;
  if(!strcmp(argv[2],"restart"))test_signal=SIGHUP;
  else if(!strcmp(argv[2],"poweroff"))test_signal=SIGINT;
  else if(!strcmp(argv[2],"crash"))test_signal=SIGTERM;
  else return 2;
 }else if(argc>=3&&!strcmp(argv[1],"--"))command=argv+2;
 else {fprintf(stderr,"Usage: android-supervisor -- /path/to/child [args] | --test restart|poweroff|crash\n");return 2;}
 setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
 char *stack=malloc(STACK_BYTES);if(!stack)return 1;
 struct child_args a={command,test_signal};
 /* Parent remains in the initial PID namespace. The direct child is Android
  * PID 1, so Linux's namespace-reboot SIGHUP/SIGINT exit is unambiguous. */
 int flags=CLONE_NEWNS|CLONE_NEWPID|SIGCHLD;
 if(!dry)flags|=CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWNET;
 pid_t child=clone(child_main,stack+STACK_BYTES,flags,&a);
 if(child<0){perror("clone Android namespace");free(stack);return 1;}
 if(!dry){char pid[32];snprintf(pid,sizeof(pid),"%ld\n",(long)child);
  if(write_state(STATE "/android-init.pid",pid)){perror("publish Android PID");kill(child,SIGKILL);waitpid(child,NULL,0);free(stack);return 1;}}
 int status;pid_t got;do{got=waitpid(child,&status,0);}while(got<0&&errno==EINTR);
 free(stack);if(got<0){perror("wait Android init");return 1;}
 if(!dry)unlink(STATE "/android-init.pid");
 const char *action=action_for_status(status);
 fprintf(stderr,"[Android TV supervisor] init status=%d action=%s dry_run=%d\n",status,action?action:"none",dry);
 if(dry){
  if(test_signal==SIGTERM)return action==NULL&&WIFEXITED(status)&&WEXITSTATUS(status)==42?0:1;
  return action&&((test_signal==SIGHUP&&!strcmp(action,"restart"))||(test_signal==SIGINT&&!strcmp(action,"poweroff")))?0:1;
 }
 /* A crash is not permission to shut down or reboot the physical console. */
 if(!action)return 1;
 return perform_power(action);
}

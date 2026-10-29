/* GoldHEN payload runner, retained from the externally verified Linux boot.
 * Bundled in the PKG and delivered over loopback, not executed by the app. */
#include "kernel.h"
#include "module.h"
#include "file.h"
#undef errno
#include "local-payload-image.h"
#include "kexec-images.h"
#include "boot-inputs.h"
#include "boot-control.h"
#include "launch-protocol.h"
#include "display-release.h"
#include "build-id.h"
#define FN(name,type) ((type)((char*)memory+PAYLOAD_##name))
#define VAR(name,type) (*(type*)((char*)memory+PAYLOAD_##name))
struct result { unsigned int magic; int stage,rc,cancel_rc; unsigned long long elapsed,nonce; unsigned short firmware,firmware_base; int southbridge; struct atv_boot_report report; };
_Static_assert(__builtin_offsetof(struct result,report)==sizeof(struct atv_launch_status),"result begins with launch status");
static struct result out;
/* Same notification ABI as OpenOrbis's OrbisNotificationRequest. Kept local
 * because this freestanding runner does not link the native app's SDK libc. */
struct startup_notification { int type,req_id,priority,msg_id,target_id,user_id,unk1,unk2,app_id,error_num,unk3; unsigned char use_icon; char message[1024],icon[1024],extra[1024]; };
_Static_assert(sizeof(struct startup_notification)==3120,"notification ABI");
static void notice_failure(void){
 int (*notify)(int,void*,size_t,int)=0;
 if(!out.rc||getFunctionAddressByName(libKernelHandle,"sceKernelSendNotificationRequest",&notify)||!notify)return;
 struct startup_notification note={0};note.target_id=-1;
 const char *message=out.rc==12?"Android TV could not reserve Linux startup memory. Startup stopped; your installation is preserved.":"Android TV startup stopped. See the saved payload result and launcher boot log.";
 unsigned i=0;while(message[i]&&i<sizeof(note.message)-1){note.message[i]=message[i];i++;}
 notify(0,&note,sizeof(note),0);
}
static int save(void){int f=open(ATV_RESULT_PATH,O_WRONLY|O_TRUNC,0600);if(f<0)return -1;int ok=write(f,&out,sizeof(out))==sizeof(out);close(f);return ok?0:-1;}
static int control_fd=-1;
static int read_control(struct atv_launch_control *c){
 return lseek(control_fd,0,0)==0&&read(control_fd,c,sizeof(*c))==sizeof(*c)?0:-1;
}
static unsigned long long clock_us(void){return sceKernelGetProcessTime();}
static int (*mapmem)(void*,size_t,int,int,int,off_t,void**);
static int (*unmapmem)(void*,size_t);
static void copybytes(void*d,const void*s,size_t n){for(size_t i=0;i<n;i++)((unsigned char*)d)[i]=((const unsigned char*)s)[i];}
#ifdef ATV_DEV_KERNEL_OVERRIDE
/* zlib CRC-32, the same one the loader reports for the staged kernel. */
static unsigned dev_crc32(const unsigned char*p,size_t n){
 unsigned t[256];for(unsigned i=0;i<256;i++){unsigned c=i;for(int k=0;k<8;k++)c=c&1?0xedb88320U^(c>>1):c>>1;t[i]=c;}
 unsigned c=~0U;while(n--)c=t[(c^*p++)&0xff]^(c>>8);return ~c;
}
#endif
static int image(const char*path,void**data,size_t n){
 int f=open(path,O_RDONLY,0);if(f<0)return -1;
 if(lseek(f,0,2)!=(off_t)n||lseek(f,0,0)<0){close(f);return -2;}
 int r=mapmem(0,(n+16383)&~(size_t)16383,3,0x1002,-1,0,data);if(r){close(f);return r;}
 size_t pos=0;while(pos<n){ssize_t got=read(f,(char*)*data+pos,n-pos);if(got<=0){close(f);return -3;}pos+=got;}
 close(f);return 0;
}
int _main(void){
 initKernel();struct atv_launch_control control;
 control_fd=open(ATV_CONTROL_PATH,O_RDONLY,0);
 if(control_fd<0)return -110;
 if(read_control(&control)||!atv_control_valid(&control,control.nonce)||control.released||control.cancelled){close(control_fd);return -111;}
 out.magic=ATV_LAUNCH_MAGIC;out.nonce=control.nonce;out.stage=1;
 if(save()){close(control_fd);return -114;}
 out.rc=getFunctionAddressByName(libKernelHandle,"sceKernelMmap",&mapmem);
 out.rc|=getFunctionAddressByName(libKernelHandle,"sceKernelMunmap",&unmapmem);
 if(out.rc||!mapmem||!unmapmem){save();close(control_fd);return -1;}
 size_t first=(sizeof(local_payload_image)+16383)&~(size_t)16383;
 size_t bytes=first+((ATV_KEXEC_MAX_BYTES+16383)&~(size_t)16383);
 void *memory=0,*kernel=0,*initrd=0;
 /* Baikal consoles boot their own kernel; the others keep the tested one. */
 const char *kernel_path=ATV_BOOT_DIR "/bzImage";
 size_t kernel_bytes=ATV_KERNEL_BYTES;unsigned kernel_crc=ATV_KERNEL_CRC32;
 out.rc=mapmem(0,bytes,7,0x1002,-1,0,&memory);if(out.rc){save();close(control_fd);return -1;}
 copybytes(memory,local_payload_image,sizeof(local_payload_image));
 /* One loader per firmware, built from the same source. Firmware without
  * kernel offsets cannot run Linux and stops here, before anything is loaded.
  * The console model is recorded but not restricted: the kernel carries the
  * Slim/Pro southbridge drivers, and the launcher asks those users to report. */
 unsigned short fwver=FN(get_firmware,unsigned short(*)(void))();
 unsigned short norm=FN(normalize_fw_ver,unsigned short(*)(unsigned short))(fwver);
 out.firmware=fwver;out.firmware_base=norm;out.southbridge=FN(get_sb_id,int(*)(void))();
 const struct atv_kexec_image *loader=0;
 for(unsigned i=0;i<sizeof(atv_kexec_images)/sizeof(atv_kexec_images[0]);i++)if(atv_kexec_images[i].firmware==norm)loader=&atv_kexec_images[i];
 if(!loader){out.rc=ATV_RC_UNSUPPORTED_FIRMWARE;goto done;}
 copybytes((char*)memory+first,atv_kexec_data+loader->offset,loader->bytes);
 VAR(g_fw,void*)=FN(find_offsets_by_fw,void*(*)(unsigned short))(norm);
 VAR(g_kexec_s,char*)=(char*)memory+first;VAR(g_kexec_e,char*)=(char*)memory+first+loader->bytes;
 int sb=out.southbridge;
 if(!VAR(g_fw,void*)){out.rc=ATV_RC_UNSUPPORTED_FIRMWARE;goto done;}
 if(sb<0){out.rc=-103;goto done;}
 out.stage=2;
 if(save()){out.rc=-114;goto done;}
 /* The small foreground app has already released its file-backed artwork.
  * Prepare Linux now; ARM still requires an explicit display release below. */
#ifdef ATV_DEV_KERNEL_OVERRIDE
 /* Dev builds only: a kernel copied in as bzImage-dev boots without a PKG
  * rebuild. Its size and checksum come from the file, so the loader's staging
  * check still catches a damaged copy. Public builds never define this. */
 int dev_kernel=0;
 {int f=open(ATV_BOOT_DIR "/bzImage-dev",O_RDONLY,0);
  if(f>=0){off_t n=lseek(f,0,2);close(f);
   if(n>0&&(size_t)n<=ATV_DEV_KERNEL_MAX){kernel_path=ATV_BOOT_DIR "/bzImage-dev";kernel_bytes=(size_t)n;dev_kernel=1;}}}
#endif
 out.rc=image(kernel_path,&kernel,kernel_bytes);if(out.rc)goto done;
#ifdef ATV_DEV_KERNEL_OVERRIDE
 if(dev_kernel)kernel_crc=dev_crc32((const unsigned char*)kernel,kernel_bytes);
#endif
 out.rc=image(ATV_BOOT_DIR "/initramfs.cpio.xz",&initrd,ATV_INITRD_BYTES);if(out.rc)goto done;
 char args[1024]={0};int fd=open(ATV_BOOT_DIR "/bootargs.txt",O_RDONLY,0);
 if(fd<0){out.rc=-104;goto done;}ssize_t got=read(fd,args,sizeof(args)-1);close(fd);
 if(got<=0){out.rc=-105;goto done;}
 for(ssize_t i=0;i<got;i++)if(args[i]=='\n'||args[i]=='\r')args[i]=0;
 size_t used=0;while(args[used])used++;
 /* Baikal: the settings psX booted a Baikal with (docs/PS4-BOOTARGS.md):
  * Orbis SATA PHY profile, 5 ms AHCI completion poll, first link at 1.5 Gbps,
  * HDMI forced on at 1080p60 and a generic 1080p EDID if the TV's can't be
  * read. Added only when the southbridge is Baikal. */
 if((sb>>16)==ATV_SB_FAMILY_BAIKAL){
  const char baikal[]=" pci=nocrs intremap=off ps4_bpcie.ps4_sata_phy_orbit=1"
   " libahci.ps4_ahci_poll_ms=5 libata.force=1:1.5Gbps drm.edid_firmware=edid/1920x1080.bin";
  const char mode[]="video=HDMI-A-1:1920x1080@60";
  if(used+sizeof(baikal)+1>sizeof(args)){out.rc=-108;goto done;}
  /* The first video= for a connector wins, so mark the existing one forced
   * (D) rather than appending a second. */
  for(size_t i=0;i+sizeof(mode)-1<=used;i++){size_t k=0;
   while(k<sizeof(mode)-1&&args[i+k]==mode[k])k++;
   if(k==sizeof(mode)-1&&args[i+k]!='D'){size_t e=i+k;
    for(size_t j=used;j>e;j--)args[j]=args[j-1];
    args[e]='D';used++;break;}}
  copybytes(args+used,baikal,sizeof(baikal));used+=sizeof(baikal)-1;}
 const char tag[]=" androidtv.boot_id=" ATV_BUILD_ID;
 if(used+sizeof(tag)>sizeof(args)){out.rc=-108;goto done;}copybytes(args+used,tag,sizeof(tag));
 out.stage=3;save();VAR(errno,int)=0;
 FN(kexec,void(*)(void*,void*))((char*)memory+PAYLOAD_kernel_main,0);
 out.rc=VAR(errno,int);if(out.rc)goto done;
 out.stage=4;save();unsigned long long start=sceKernelGetProcessTime();
 int packed=FN(pack_kexec_args,int(*)(int,unsigned short,int))(2048,fwver,sb);
 out.rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(kernel,kernel_bytes,initrd,ATV_INITRD_BYTES,args,packed);
 out.elapsed=sceKernelGetProcessTime()-start;
 /* The loader fills its memory diagnostics even when preparation fails. */
 if(out.rc)(void)FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_QUERY,&out.report,sizeof(out.report),0,0);
 out.stage=5;save();
 if(out.rc)goto done;
 out.rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_QUERY,&out.report,sizeof(out.report),0,0);
 out.stage=6;save();
 if(!out.rc&&(out.report.magic!=ATV_BOOT_REPORT_MAGIC||out.report.state!=1||out.report.image_crc32!=kernel_crc||out.report.initrd_crc32!=ATV_INITRD_CRC32))out.rc=-106;
 if(out.rc){out.cancel_rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_CANCEL,0,0,0,0);goto done;}
 int event=FN(evf_open,int(*)(char*))("SceSysCoreReboot");
 if(event<0){out.rc=event;out.cancel_rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_CANCEL,0,0,0,0);goto done;}
 out.stage=7;save();
 out.rc=atv_wait_display_release(out.nonce,read_control,clock_us,sceKernelUsleep);
 if(out.rc){out.cancel_rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_CANCEL,0,0,0,0);goto done;}
 out.rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_ARM,0,0,0,0);
 out.stage=8;save();
 if(out.rc){out.cancel_rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_CANCEL,0,0,0,0);goto done;}
 out.rc=FN(kexec_load,int(*)(void*,unsigned long long,void*,unsigned long long,void*,int))(0,ATV_BOOT_QUERY,&out.report,sizeof(out.report),0,0);
 out.stage=9;save();
 if(out.rc||out.report.state!=2){out.rc=-107;goto done;}
 out.rc=FN(evf_cancel,int(*)(int,unsigned long long,unsigned long long))(event,0x4000,0);
 out.stage=10;save();if(out.rc<0)goto done;
 out.rc=FN(kill,int(*)(int,int))(1,30);
 out.stage=11;save();
 done:save();notice_failure();
 if(kernel)unmapmem(kernel,(kernel_bytes+16383)&~(size_t)16383);
 if(initrd)unmapmem(initrd,(ATV_INITRD_BYTES+16383)&~(size_t)16383);
 unmapmem(memory,bytes);close(control_fd);return out.rc;
}

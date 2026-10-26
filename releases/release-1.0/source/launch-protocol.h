#ifndef ATV_LAUNCH_PROTOCOL_H
#define ATV_LAUNCH_PROTOCOL_H
/* Shared by the Orbis UI and the GoldHEN runner. No network discovery is used.
 * A fresh nonce prevents a previous run's report/release from advancing boot. */
#define ATV_LAUNCH_MAGIC 0x41545632U
#define ATV_LAUNCH_VERSION 4U
#ifndef ATV_CONTROL_PATH
#define ATV_CONTROL_PATH "/data/atv16-launch-control.bin"
#endif
#ifndef ATV_RESULT_PATH
#define ATV_RESULT_PATH "/data/atv16-payload-result.bin"
#endif
#ifndef ATV_BOOT_DIR
#define ATV_BOOT_DIR "/user/app/ps4-android16/boot"
#endif
struct atv_launch_control {
 unsigned int magic, version;
 unsigned long long nonce;
 unsigned int released, cancelled;
 unsigned int launcher_pid, reserved;
};
struct atv_launch_status {
 unsigned int magic;
 int stage, rc, cancel_rc;
 unsigned long long elapsed, nonce;
 /* Detected by the runner so the launcher can name an unsupported console. */
 unsigned short firmware, firmware_base;
 int southbridge;
};
_Static_assert(sizeof(struct atv_launch_control)==32,"launch control ABI");
_Static_assert(sizeof(struct atv_launch_status)==40,"launch status ABI");
static inline int atv_control_valid(const struct atv_launch_control *c,unsigned long long nonce){
 return c->magic==ATV_LAUNCH_MAGIC&&c->version==ATV_LAUNCH_VERSION&&c->nonce==nonce&&nonce!=0&&c->launcher_pid>1&&c->launcher_pid<0x7fffffffU;
}
static inline int atv_status_valid(const struct atv_launch_status *s,unsigned long long nonce){
 return s->magic==ATV_LAUNCH_MAGIC&&s->nonce==nonce&&nonce!=0&&s->stage>=1&&s->stage<=11;
}
/* Runner result codes the launcher explains to the user. */
#define ATV_RC_UNSUPPORTED_FIRMWARE (-120)
/* get_sb_id() >> 16: 1 Aeolia, 2 Belize, 3 Baikal, 4 Belize2. */
#define ATV_SB_FAMILY_BAIKAL 3
#endif

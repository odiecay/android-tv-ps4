#ifndef ATV_BOOT_VALIDATION_H
#define ATV_BOOT_VALIDATION_H
/* The assembly transition maps 0..4 GiB and relocates the compressed kernel
 * to 96 MiB. Keep every input outside that kernel's initialization workspace.
 * Linux can use high RAM afterwards; these are transition constraints only. */
#define ATV_KERNEL_DEST 0x06000000ULL
/* Orbis low RAM ends before the ACPI reservations at 0x7efe8000.
 * Once the stub has gathered the staged pieces, the kernel's protected-mode
 * code sits at ATV_KERNEL_DEST with init_size bytes of workspace and the
 * initramfs follows ATV_INITRD_GAP after it. Every staging buffer must
 * stay clear of [ATV_KERNEL_DEST, dest_end) so the gather never overwrites
 * a piece it has yet to read. */
#define ATV_STAGE_FLOOR 0x01000000ULL
/* 7.0 Baikal kernel fails with the initramfs right after init_size */
#define ATV_INITRD_GAP 0x10000000ULL
static int atv_boot_layout(const struct setup_header *h, u64 initrd_bytes,
                           u64 *initrd_dst, u64 *dest_end, u64 *high)
{
    *initrd_dst = ((ATV_KERNEL_DEST + h->init_size + 0x1fffff) & ~0x1fffffULL) +
                  ATV_INITRD_GAP;
    *dest_end = *initrd_dst + initrd_bytes;
    *high = (u64)h->initrd_addr_max + 1;
    if (*high > 0x7efe8000ULL) *high = 0x7efe8000ULL;
    return h->init_size && *dest_end + 0x400000 <= *high;
}
static int atv_valid_header(const struct setup_header *h, size_t bytes)
{
    u64 setup = ((u64)h->setup_sects + 1) * 512;
    u64 payload = (u64)h->syssize * 16;
    return h->boot_flag == 0xaa55 && h->header == 0x53726448 &&
        h->version >= 0x20c && h->setup_sects && (h->xloadflags & 1) &&
        h->relocatable_kernel && h->kernel_alignment &&
        !(h->kernel_alignment & (h->kernel_alignment - 1)) &&
        ATV_KERNEL_DEST % h->kernel_alignment == 0 && payload > 512 &&
        setup <= bytes && payload <= bytes - setup &&
        h->init_size >= payload && h->init_size <= 0x10000000 &&
        h->cmdline_size > 0 && h->cmdline_size < 4096 &&
        offsetof(struct setup_header, header) + ((h->jump >> 8) & 255) <=
            sizeof(struct boot_params) - offsetof(struct boot_params, hdr);
}
static int atv_valid_range(u64 address, u64 bytes, u64 workspace)
{
    return bytes && address >= 0x100000 && address < 0x100000000ULL &&
        bytes <= 0x100000000ULL - address &&
        (address + bytes <= ATV_KERNEL_DEST || address >= ATV_KERNEL_DEST + workspace);
}
#endif

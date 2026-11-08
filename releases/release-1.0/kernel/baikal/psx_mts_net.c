// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 Meerzulee and Linux psX contributors.
 * Original experimental Baikal netdevice. See the adjacent contract and the
 * Linux MediaTek STAR reference; this is not a copy of Sony implementation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/hrtimer.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iommu-dma.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/utsname.h>
#include <generated/utsrelease.h>
#include <linux/workqueue.h>
#include "mts_net_core.h"

#define MTS_RELEASE UTS_RELEASE
#define MTS_DRIVER "psx_mts_net"
#define MTS_MASK MTS_IDLE_STOP_MASK
#define MTS_MAC_CFG 0x0f597c00U
#define MTS_MAP_BYTES 0x58
#define MTS_GATE_OFFSET 0x110450
#define MTS_MDIO_DONE BIT(15)
#define MTS_MDIO_BUSY (BIT(13) | BIT(14))
#define MTS_LINK_INTERVAL (HZ / 4)
#define MTS_TX_PROGRESS_TIMEOUT (10 * HZ)
#define MTS_MAC_STATUS_BITS (BIT(0) | BIT(2) | BIT(3) | BIT(4))
#define MTS_MAC_FIXED_BITS 0x00000b00U
#define MTS_TICK_NS 1000000

enum mts_state_id {
	MTS_ABSENT, MTS_PROBED, MTS_PREPARED, MTS_RUNNING,
	MTS_STOPPED_RETAINED, MTS_REFUSED
};
enum mts_memory_id { TX_RING, RX_RING, TX_POOL, RX_POOL, MEMORY_COUNT };
struct mts_desc { __le32 word[4]; };
struct mts_memory { void *cpu; dma_addr_t dma; size_t size; };
struct mts_caps { int msi, msix; u16 msi_flags, msix_flags; };

struct mts_net {
	struct pci_dev *pdev, *glue;
	struct device_driver *glue_driver;
	struct module *glue_owner;
	bool owner_held, bar_claimed, bound, registered, napi_added;
	bool attempted, allowed, published, self_pinned, engine_live, napi_enabled;
	bool admin_up, poll_enabled, link_up, terminal_fault;
	bool detached, phy_setting_attempted, stop_ack;
	bool stop_entry_attempted, stop_entry_sampled, stop_entry_valid;
	void __iomem *mac, *gate;
	struct net_device *ndev;
	struct napi_struct napi;
	struct hrtimer tick;
	struct work_struct stop_work;
	struct mutex lifecycle;
	spinlock_t tx_lock, stats_lock;
	struct mts_caps caps;
	struct mts_memory memory[MEMORY_COUNT];
	struct mts_tx_ledger tx;
	struct rtnl_link_stats64 stats;
	unsigned int rx_tail, polls;
	unsigned long next_link_check, carrier_sample_jiffies, tx_progress;
	enum mts_state_id state;
	int error, stop_error, probe_error;
	const char *reason;
	u16 phy_id2, phy_id3, phy_page, phy_bmcr, bmsr_first, bmsr_second;
	u16 mmd_before, mmd_after;
	u32 mdio_writes, mac04, mac08, arl10, mac14, mac18;
	u32 tx_control, rx_control, mac_mask, route, last_txd0, last_txd2;
	u32 last_rxd0, last_rx_length, tx_after_kick;
	u32 stop_entry_status50, stop_entry_tx_dptr3c, stop_entry_rx_dptr40;
	unsigned int stop_entry_tx_head, stop_entry_tx_tail, stop_entry_tx_count;
	unsigned int stop_entry_rx_tail;
	u64 rx_length_drops, rx_fragment_drops, rx_crc_drops, rx_oversize_drops;
	u64 rx_vlan_drops, rx_alloc_drops, rx_admin_drops, rx_link_drops;
	u64 tx_completed, rx_delivered, admin_opens, admin_closes, carrier_changes;
};

static struct mts_net mts;
static struct pci_driver mts_driver;
/* Native autoload: the first open arms itself unless booted with autoarm=0. */
static bool autoarm = true;

static bool mts_identity(struct pci_dev *p, u16 id, unsigned int function)
{
	return p && p->vendor == 0x104d && p->device == id &&
		pci_domain_nr(p->bus) == 0 && p->bus->number == 0 &&
		p->devfn == PCI_DEVFN(0x14, function);
}

static int mts_caps_read(struct pci_dev *p, struct mts_caps *c)
{
	c->msi = pci_find_capability(p, PCI_CAP_ID_MSI);
	c->msix = pci_find_capability(p, PCI_CAP_ID_MSIX);
	c->msi_flags = c->msix_flags = 0;
	if (c->msi && pci_read_config_word(p, c->msi + PCI_MSI_FLAGS,
					 &c->msi_flags) != PCIBIOS_SUCCESSFUL)
		return -EIO;
	if (c->msix && pci_read_config_word(p, c->msix + PCI_MSIX_FLAGS,
					  &c->msix_flags) != PCIBIOS_SUCCESSFUL)
		return -EIO;
	return (c->msi_flags & PCI_MSI_FLAGS_ENABLE) ||
		(c->msix_flags & PCI_MSIX_FLAGS_ENABLE) ? -EBUSY : 0;
}

/* Caller owns the GBE device lock and the glue device lock. No PCI mutations. */
static int mts_metadata(struct mts_net *s)
{
	struct device *d = &s->pdev->dev;
	struct mts_caps caps = {0};
	u16 command, glue_command;
	int error;

	if (!mts_identity(s->pdev, 0x90d8, 1) ||
	    !mts_identity(s->glue, 0x90db, 4))
		return -ENODEV;
	if (s->pdev->driver != &mts_driver || d->driver != &mts_driver.driver ||
	    READ_ONCE(s->pdev->driver_override) ||  /* 6.15: no device_match_driver_override() */
	    s->pdev->msi_enabled || s->pdev->msix_enabled ||
	    s->pdev->current_state != PCI_D0 || pci_dev_is_disconnected(s->pdev) ||
	    pci_channel_offline(s->pdev))
		return -EBUSY;
	if (!s->glue->driver || !s->glue->dev.driver ||
	    strcmp(s->glue->dev.driver->name, "baikal_pcie") ||
	    s->glue->current_state != PCI_D0 ||
	    (s->glue_driver && s->glue->dev.driver != s->glue_driver))
		return -EBUSY;
	if (!d->dma_mask || *d->dma_mask != DMA_BIT_MASK(32) ||
	    d->coherent_dma_mask != DMA_BIT_MASK(32) || get_dma_ops(d) ||
	    use_dma_iommu(d) || !dev_is_dma_coherent(d))
		return -EOPNOTSUPP;
#ifdef CONFIG_DMA_DECLARE_COHERENT
	if (d->dma_mem)
		return -EOPNOTSUPP;
#endif
	if (pci_resource_start(s->pdev, 0) != 0xc2000000ULL ||
	    pci_resource_len(s->pdev, 0) != 0x1000 ||
	    pci_resource_flags(s->pdev, 0) != 0x140204 ||
	    pci_resource_start(s->glue, 2) != 0xc8800000ULL ||
	    pci_resource_len(s->glue, 2) != 0x200000 ||
	    pci_resource_flags(s->glue, 2) != 0x140204)
		return -EINVAL;
	if (pci_read_config_word(s->pdev, PCI_COMMAND, &command) != PCIBIOS_SUCCESSFUL ||
	    pci_read_config_word(s->glue, PCI_COMMAND, &glue_command) != PCIBIOS_SUCCESSFUL)
		return -EIO;
	if (command != 0x0546 || glue_command != 0x0546)
		return -EBUSY;
	error = mts_caps_read(s->pdev, &caps);
	if (error)
		return error;
	if (s->bound && (caps.msi != s->caps.msi || caps.msix != s->caps.msix ||
	    caps.msi_flags != s->caps.msi_flags || caps.msix_flags != s->caps.msix_flags))
		return -EIO;
	return 0;
}

static int mts_baseline(struct mts_net *s, bool fresh)
{
	u32 high, low;
	const u8 *address;
	int error = -EBUSY;
	/* Probe captures the words before the netdevice exists. Open revalidates
	 * both inherited hardware filtering and the Linux address before changes.
	 */
	if (s->bound) {
		high = readl(s->mac + 0x14);
		low = readl(s->mac + 0x18);
		if (high == U32_MAX || low == U32_MAX) {
			pr_err("MAC baseline refused error=%d fresh=%u mac14=%08x mac18=%08x saved14=%08x saved18=%08x\n",
				-EIO, fresh, high, low, s->mac14, s->mac18);
			return -EIO;
		}
		address = s->ndev->dev_addr;
		if (high != s->mac14 || low != s->mac18 ||
		    (high & 0xffff) != ((u32)address[0] << 8 | address[1]) ||
		    low != ((u32)address[2] << 24 | (u32)address[3] << 16 |
			    (u32)address[4] << 8 | address[5])) {
			pr_err("MAC baseline refused error=%d fresh=%u mac14=%08x mac18=%08x saved14=%08x saved18=%08x Linux-MAC=%pM\n",
				-EBUSY, fresh, high, low, s->mac14, s->mac18, address);
			return -EBUSY;
		}
	}
	s->tx_control = readl(s->mac + 0x34);
	s->rx_control = readl(s->mac + 0x38);
	s->mac_mask = readl(s->mac + 0x54);
	s->route = readl(s->gate);
	s->mac04 = readl(s->mac + 0x04);
	s->mac08 = readl(s->mac + 0x08);
	s->arl10 = readl(s->mac + 0x10);
	if (s->tx_control == U32_MAX || s->rx_control == U32_MAX ||
	    s->mac_mask == U32_MAX || s->route == U32_MAX ||
	    s->mac04 == U32_MAX || s->mac08 == U32_MAX || s->arl10 == U32_MAX) {
		error = -EIO;
		goto refused;
	}
	if (s->tx_control || s->rx_control || s->route || s->mac08 != MTS_MAC_CFG ||
	    !mts_idle_mask_matches(fresh, s->mac_mask) ||
	    (s->mac04 & ~MTS_MAC_STATUS_BITS) != MTS_MAC_FIXED_BITS || s->arl10 != 0x85)
		goto refused;
	return 0;
refused:
	pr_err("baseline refused error=%d fresh=%u tx34=%08x rx38=%08x mask54=%08x gate=%08x mac04=%08x mac08=%08x arl10=%08x\n",
		error, fresh, s->tx_control, s->rx_control, s->mac_mask,
		s->route, s->mac04, s->mac08, s->arl10);
	return error;
}

/* Native252800/252640/252490 controller protocol, PHY0 only. */
static int mts_mdio(struct mts_net *s, u32 command, u16 *data, bool reread)
{
	u32 raw = readl(s->mac);
	int error;
	if (raw == U32_MAX || (raw & MTS_MDIO_BUSY))
		return raw == U32_MAX ? -EIO : -EBUSY;
	writel(MTS_MDIO_DONE, s->mac);
	s->mdio_writes++;
	raw = readl(s->mac);
	if (raw == U32_MAX || (raw & (MTS_MDIO_DONE | MTS_MDIO_BUSY)))
		return -EIO;
	writel(command, s->mac);
	s->mdio_writes++;
	error = readl_poll_timeout(s->mac, raw,
		(raw & MTS_MDIO_DONE) || raw == U32_MAX, 10, 10000);
	if (error || raw == U32_MAX || (raw & MTS_MDIO_BUSY))
		return error ? error : -EIO;
	if (reread) {
		raw = readl(s->mac);
		if (raw == U32_MAX || !(raw & MTS_MDIO_DONE))
			return -EIO;
	}
	if (data)
		*data = raw >> 16;
	return 0;
}

static int mts_c22(struct mts_net *s, unsigned int reg, u16 *data)
{
	return mts_mdio(s, BIT(14) | (reg << 8), data, false);
}

static int mts_mmd147_read(struct mts_net *s, u16 *data)
{
	int error = mts_mdio(s, 0x01471e20, NULL, false);
	return error ? error : mts_mdio(s, 0x00001ee0, data, true);
}

static int mts_restore_link(struct mts_net *s)
{
	int error;
#define READ_PHY(reg, member) do { error = mts_c22(s, reg, &s->member); \
	if (error) return error; } while (0)
	READ_PHY(2, phy_id2);
	READ_PHY(3, phy_id3);
	READ_PHY(31, phy_page);
	READ_PHY(0, phy_bmcr);
	if (s->phy_id2 != 0x03a2 || s->phy_id3 != 0x9441 ||
	    s->phy_page || s->phy_bmcr != 0x1040)
		return -EOPNOTSUPP;
	error = mts_mmd147_read(s, &s->mmd_before);
	if (error)
		return error;
	if (s->mmd_before != 0 && s->mmd_before != 0x10)
		return -EBUSY;
	if (s->mmd_before == 0x10) {
		error = mts_mdio(s, 0x01471e20, NULL, false);
		if (error)
			return error;
		s->phy_setting_attempted = true;
		error = mts_mdio(s, 0x00001e60, NULL, false);
		if (error)
			return error;
	}
	error = mts_mmd147_read(s, &s->mmd_after);
	if (error || s->mmd_after)
		return error ? error : -EIO;
	/* Restoring the inherited handoff is required; carrier is not. An
	 * unplugged port must remain a normal, reconnectable link-down state.
	 */
	READ_PHY(1, bmsr_first);
	READ_PHY(1, bmsr_second);
	s->mac04 = readl(s->mac + 0x04);
	if (s->mac04 == U32_MAX ||
	    (s->mac04 & ~MTS_MAC_STATUS_BITS) != MTS_MAC_FIXED_BITS)
		return -EIO;
	return 0;
#undef READ_PHY
}

static void mts_free_unpublished(struct mts_net *s)
{
	int i;
	if (s->published)
		return;
	for (i = MEMORY_COUNT - 1; i >= 0; i--) {
		struct mts_memory *m = &s->memory[i];
		if (m->cpu) {
			dma_free_coherent(&s->pdev->dev, m->size, m->cpu, m->dma);
			m->cpu = NULL;
		}
	}
}

static int mts_prepare_memory(struct mts_net *s)
{
	unsigned int i, j;
	for (i = 0; i < MEMORY_COUNT; i++) {
		struct mts_memory *m = &s->memory[i];
		m->size = i < TX_POOL ? MTS_RING_BYTES : MTS_POOL_BYTES;
		m->cpu = dma_alloc_coherent(&s->pdev->dev, m->size, &m->dma,
				GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
		if (!m->cpu)
			return -ENOMEM;
		if (!mts_extent_valid(m->dma, m->size) ||
		    (i < TX_POOL && (m->dma & (MTS_RING_BYTES - 1))))
			return -ERANGE;
		for (j = 0; j < i; j++)
			if (mts_extent_overlap(m->dma, m->size,
				s->memory[j].dma, s->memory[j].size))
				return -EADDRINUSE;
		memset(m->cpu, 0, m->size);
		pr_info("prepared extent=%u dma=%llx size=%zx\n", i,
			(unsigned long long)m->dma, m->size);
	}
	for (i = 0; i < MTS_SLOTS; i++) {
		struct mts_desc *tx = s->memory[TX_RING].cpu;
		struct mts_desc *rx = s->memory[RX_RING].cpu;
		tx[i].word[0] = cpu_to_le32(mts_tx_idle(i));
		tx[i].word[2] = cpu_to_le32(MTS_TX_SENTINEL);
		rx[i].word[1] = cpu_to_le32(s->memory[RX_POOL].dma + i * MTS_BUFFER_BYTES);
		rx[i].word[0] = cpu_to_le32(mts_rx_ready(i));
	}
	return 0;
}

static void mts_request_stop(struct mts_net *s, int error, const char *reason)
{
	WRITE_ONCE(s->terminal_fault, true);
	if (error && !READ_ONCE(s->error))
		WRITE_ONCE(s->error, error);
	netif_stop_queue(s->ndev);
	WRITE_ONCE(s->reason, reason);
	schedule_work(&s->stop_work);
}

/* Lifecycle mutex held. Published buffers/maps/refs/code are NEVER released. */
static void mts_terminal_stop(struct mts_net *s, const char *reason)
{
	unsigned long flags;
	u32 raw;
	int error = 0;
	if (!s->published)
		return;
	spin_lock_irqsave(&s->tx_lock, flags);
	if (!s->stop_entry_attempted) {
		s->stop_entry_attempted = true;
		s->stop_entry_tx_head = s->tx.head;
		s->stop_entry_tx_tail = s->tx.tail;
		s->stop_entry_tx_count = s->tx.count;
		s->stop_entry_rx_tail = READ_ONCE(s->rx_tail);
		/* One cached attribution sample before any stop write. Hardware/RX
		 * can still advance: this is not an atomic or drained snapshot.
		 * Status is read only; never acknowledge or recover from its bits.
		 */
		if (!pci_dev_is_disconnected(s->pdev) && !pci_channel_offline(s->pdev)) {
			s->stop_entry_status50 = readl(s->mac + 0x50);
			s->stop_entry_tx_dptr3c = readl(s->mac + 0x3c);
			s->stop_entry_rx_dptr40 = readl(s->mac + 0x40);
			s->stop_entry_valid = s->stop_entry_status50 != U32_MAX &&
				s->stop_entry_tx_dptr3c != U32_MAX &&
				s->stop_entry_rx_dptr40 != U32_MAX;
			s->stop_entry_sampled = true;
		}
	}
	WRITE_ONCE(s->terminal_fault, true);
	WRITE_ONCE(s->admin_up, false);
	WRITE_ONCE(s->link_up, false);
	WRITE_ONCE(s->poll_enabled, false);
	WRITE_ONCE(s->engine_live, false);
	s->state = MTS_STOPPED_RETAINED;
	spin_unlock_irqrestore(&s->tx_lock, flags);
	WRITE_ONCE(s->reason, reason);
	netif_tx_disable(s->ndev);
	netif_carrier_off(s->ndev);
	hrtimer_cancel(&s->tick);
	if (s->napi_enabled) {
		napi_disable(&s->napi);
		s->napi_enabled = false;
	}
	if (s->stop_ack || s->stop_error)
		return;
	if (pci_dev_is_disconnected(s->pdev) || pci_channel_offline(s->pdev)) {
		s->stop_error = -ENODEV;
		return;
	}
	writel(MTS_MASK, s->mac + 0x54);
	writel(2, s->mac + 0x34);
	error = readl_poll_timeout(s->mac + 0x34, raw,
		!(raw & 2) || raw == U32_MAX, 10, 2000000);
	s->tx_control = raw;
	if (error || raw == U32_MAX) {
		s->stop_error = error ? error : -EIO;
		return; /* TX timeout: no RX stop write. */
	}
	writel(2, s->mac + 0x38);
	error = readl_poll_timeout(s->mac + 0x38, raw,
		!(raw & 2) || raw == U32_MAX, 10, 2000000);
	s->rx_control = raw;
	s->mac_mask = readl(s->mac + 0x54);
	s->route = readl(s->gate);
	if (error || raw == U32_MAX || s->mac_mask != MTS_MASK || s->route ||
	    (s->tx_control & 3) || (s->rx_control & 3))
		s->stop_error = error ? error : -EIO;
	else
		s->stop_ack = true;
	pr_info("terminal reason=%s error=%d stop_error=%d stop_ack=%u retained=1 drain=unproven\n",
		reason, s->error, s->stop_error, s->stop_ack);
}

static void mts_stop_work(struct work_struct *work)
{
	struct mts_net *s = container_of(work, struct mts_net, stop_work);
	mutex_lock(&s->lifecycle);
	mts_terminal_stop(s, READ_ONCE(s->reason));
	mutex_unlock(&s->lifecycle);
}

/* Lifecycle held. Join the sole RX consumer before changing admin mode. */
static void mts_pause_poll(struct mts_net *s)
{
	WRITE_ONCE(s->poll_enabled, false);
	hrtimer_cancel(&s->tick);
	if (s->napi_enabled) {
		napi_disable(&s->napi);
		s->napi_enabled = false;
	}
}

/* DOWN mode still pumps the same retained rings, dropping rather than delivering. */
static void mts_resume_poll(struct mts_net *s)
{
	if (!READ_ONCE(s->engine_live) || READ_ONCE(s->terminal_fault) || READ_ONCE(s->error))
		return;
	napi_enable(&s->napi);
	s->napi_enabled = true;
	WRITE_ONCE(s->poll_enabled, true);
	hrtimer_start(&s->tick, ns_to_ktime(MTS_TICK_NS), HRTIMER_MODE_REL);
}

/* tx_lock held for all admission and queue start/wake decisions. */
static bool mts_tx_allowed(struct mts_net *s)
{
	return READ_ONCE(s->engine_live) && READ_ONCE(s->admin_up) &&
		READ_ONCE(s->link_up) && !READ_ONCE(s->terminal_fault) &&
		!READ_ONCE(s->error) && netif_running(s->ndev) && s->tx.count < MTS_SLOTS;
}

static bool mts_rx_allowed(struct mts_net *s)
{
	return READ_ONCE(s->engine_live) && READ_ONCE(s->admin_up) &&
		READ_ONCE(s->link_up) && !READ_ONCE(s->terminal_fault) &&
		!READ_ONCE(s->error) && netif_running(s->ndev);
}

/* Native252baa status read: only link bit0 is exposed; no PHY writes here. */
static void mts_check_carrier(struct mts_net *s)
{
	unsigned long flags;
	u32 status, control;
	bool link;
	if (time_before(jiffies, s->next_link_check))
		return;
	s->next_link_check = jiffies + MTS_LINK_INTERVAL;
	status = readl(s->mac + 0x04);
	if (status == U32_MAX || (status & ~MTS_MAC_STATUS_BITS) != MTS_MAC_FIXED_BITS) {
		mts_request_stop(s, -EIO, "mac-status-invalid");
		return;
	}
	link = !!(status & BIT(0));
	spin_lock_irqsave(&s->tx_lock, flags);
	/* Terminal work may have won while this callback sampled status. Its
	 * carrier-off and hardware quiescence must never be reversed by NAPI.
	 */
	if (!READ_ONCE(s->engine_live) || !READ_ONCE(s->poll_enabled) ||
	    READ_ONCE(s->terminal_fault) || READ_ONCE(s->error)) {
		spin_unlock_irqrestore(&s->tx_lock, flags);
		return;
	}
	WRITE_ONCE(s->mac04, status);
	WRITE_ONCE(s->carrier_sample_jiffies, jiffies);
	if (link != s->link_up) {
		s->link_up = link;
		s->carrier_changes++;
		if (!link) {
			netif_stop_queue(s->ndev);
			netif_carrier_off(s->ndev);
		} else {
			s->tx_progress = jiffies;
			netif_trans_update(s->ndev);
			netif_carrier_on(s->ndev);
			/* Same native resume kick, without resetting any descriptor. */
			if (s->tx.count) {
				control = readl(s->mac + 0x34);
				if (control == U32_MAX)
					mts_request_stop(s, -EIO, "link-up-control-invalid");
				else {
					writel(control | 4, s->mac + 0x34);
					s->tx_after_kick = readl(s->mac + 0x34);
					if (s->tx_after_kick == U32_MAX)
						mts_request_stop(s, -EIO, "link-up-kick-invalid");
				}
			}
			if (mts_tx_allowed(s))
				netif_wake_queue(s->ndev);
		}
	}
	spin_unlock_irqrestore(&s->tx_lock, flags);
}

static enum hrtimer_restart mts_tick(struct hrtimer *timer)
{
	struct mts_net *s = container_of(timer, struct mts_net, tick);
	if (!READ_ONCE(s->engine_live) || !READ_ONCE(s->poll_enabled) ||
	    READ_ONCE(s->terminal_fault) || READ_ONCE(s->error))
		return HRTIMER_NORESTART;
	napi_schedule(&s->napi);
	hrtimer_forward_now(timer, ns_to_ktime(MTS_TICK_NS));
	return HRTIMER_RESTART;
}

static void mts_tx_reclaim(struct mts_net *s)
{
	struct mts_desc *ring = s->memory[TX_RING].cpu;
	unsigned int packets = 0, bytes = 0, length, slot;
	unsigned long flags, stats_flags;
	u32 d0, d2, control;
	spin_lock_irqsave(&s->tx_lock, flags);
	while (s->tx.count) {
		slot = s->tx.tail;
		d0 = le32_to_cpu(READ_ONCE(ring[slot].word[0]));
		if (!(d0 & MTS_COWN))
			break;
		dma_rmb();
		d2 = le32_to_cpu(READ_ONCE(ring[slot].word[2]));
		s->last_txd0 = d0;
		s->last_txd2 = d2;
		if (!mts_tx_retire(&s->tx, d0, d2, &length))
			break;
		WRITE_ONCE(ring[slot].word[2], cpu_to_le32(MTS_TX_SENTINEL));
		WRITE_ONCE(ring[slot].word[0], cpu_to_le32(mts_tx_idle(slot)));
		packets++;
		bytes += length;
	}
	if (packets) {
		netdev_completed_queue(s->ndev, packets, bytes);
		s->tx_completed += packets;
		s->tx_progress = jiffies;
		spin_lock_irqsave(&s->stats_lock, stats_flags);
		s->stats.tx_packets += packets;
		s->stats.tx_bytes += bytes;
		spin_unlock_irqrestore(&s->stats_lock, stats_flags);
		/* Native2522c0..25234e resumes after reclaim when TX remains.
		 * At most one re-kick per completed reclaim round, under tx_lock.
		 */
		if (s->tx.count && READ_ONCE(s->engine_live) &&
		    !READ_ONCE(s->terminal_fault) && !READ_ONCE(s->error)) {
			control = readl(s->mac + 0x34);
			if (control == U32_MAX) {
				netif_stop_queue(s->ndev);
				mts_request_stop(s, -EIO, "tx-reclaim-control-invalid");
			} else {
				writel(control | 4, s->mac + 0x34);
				s->tx_after_kick = readl(s->mac + 0x34);
				if (s->tx_after_kick == U32_MAX) {
					netif_stop_queue(s->ndev);
					mts_request_stop(s, -EIO, "tx-reclaim-kick-invalid");
				}
			}
		}
		if (mts_tx_allowed(s) && netif_queue_stopped(s->ndev))
			netif_wake_queue(s->ndev);
	}
	/* A single stuck packet need not exhaust the ring and trigger qdisc's
	 * watchdog. Only age pending TX while administrative link use is active.
	 */
	if (s->tx.count && mts_tx_allowed(s) &&
	    time_after(jiffies, s->tx_progress + MTS_TX_PROGRESS_TIMEOUT))
		mts_request_stop(s, -ETIMEDOUT, "tx-progress-timeout");
	spin_unlock_irqrestore(&s->tx_lock, flags);
}

static int mts_rx_poll(struct mts_net *s, int budget)
{
	struct mts_desc *ring = s->memory[RX_RING].cpu;
	unsigned int slot, length;
	unsigned long flags;
	u32 raw;
	int done = 0;
	enum mts_rx_result result;
	struct sk_buff *skb;
	bool delivered;
	while (done < budget && READ_ONCE(s->engine_live) &&
	       READ_ONCE(s->poll_enabled) && !READ_ONCE(s->terminal_fault) &&
	       !READ_ONCE(s->error)) {
		slot = s->rx_tail;
		raw = le32_to_cpu(READ_ONCE(ring[slot].word[0]));
		if (!(raw & MTS_COWN))
			break;
		dma_rmb();
		result = mts_rx_classify(raw, &length);
		s->last_rxd0 = raw;
		s->last_rx_length = length;
		skb = NULL;
		delivered = false;
		if (result == MTS_RX_ACCEPT && mts_rx_allowed(s)) {
			skb = napi_alloc_skb(&s->napi, length + NET_IP_ALIGN);
			if (skb) {
				skb_reserve(skb, NET_IP_ALIGN);
				memcpy(skb_put(skb, length), s->memory[RX_POOL].cpu +
					slot * MTS_BUFFER_BYTES, length);
				skb->ip_summed = CHECKSUM_NONE;
				skb->protocol = eth_type_trans(skb, s->ndev);
				if (skb->protocol == htons(ETH_P_8021Q) ||
				    skb->protocol == htons(ETH_P_8021AD)) {
					dev_kfree_skb_any(skb);
					skb = NULL;
					result = MTS_RX_TAGGED;
				}
			} else {
				s->rx_alloc_drops++;
			}
		}
		/* Native recycle: CPU-owned size, address, EOR, ownership LAST. */
		WRITE_ONCE(ring[slot].word[0], cpu_to_le32(MTS_COWN | MTS_BUFFER_BYTES));
		WRITE_ONCE(ring[slot].word[1], cpu_to_le32(s->memory[RX_POOL].dma +
							 slot * MTS_BUFFER_BYTES));
		dma_wmb();
		WRITE_ONCE(ring[slot].word[0], cpu_to_le32(mts_rx_ready(slot)));
		WRITE_ONCE(s->rx_tail, mts_next(slot));
		done++;
		/* Recheck immediately before handing the independent copy to the
		 * stack. The NAPI join in close is the final CPU-delivery barrier.
		 */
		if (skb && !mts_rx_allowed(s)) {
			dev_kfree_skb_any(skb);
			skb = NULL;
		}
		if (skb) {
			netif_receive_skb(skb);
			delivered = true;
		}
		spin_lock_irqsave(&s->stats_lock, flags);
		if (delivered) {
			s->stats.rx_packets++;
			s->stats.rx_bytes += length;
			s->rx_delivered++;
		} else {
			s->stats.rx_dropped++;
			if (result == MTS_RX_ACCEPT && !mts_rx_allowed(s)) {
				if (!READ_ONCE(s->admin_up) || !netif_running(s->ndev))
					s->rx_admin_drops++;
				else
					s->rx_link_drops++;
			}
			switch (result) {
			case MTS_RX_BAD_LENGTH: s->rx_length_drops++; s->stats.rx_length_errors++; break;
			case MTS_RX_FRAGMENT: s->rx_fragment_drops++; break;
			case MTS_RX_BAD_CRC: s->rx_crc_drops++; s->stats.rx_crc_errors++; break;
			case MTS_RX_TOO_LARGE: s->rx_oversize_drops++; s->stats.rx_length_errors++; break;
			case MTS_RX_TAGGED: s->rx_vlan_drops++; break;
			default: break;
			}
		}
		spin_unlock_irqrestore(&s->stats_lock, flags);
	}
	return done;
}

static int mts_poll(struct napi_struct *napi, int budget)
{
	struct mts_net *s = container_of(napi, struct mts_net, napi);
	u32 control;
	int done = 0;
	if (READ_ONCE(s->engine_live) && READ_ONCE(s->poll_enabled) &&
	    !READ_ONCE(s->terminal_fault) && !READ_ONCE(s->error)) {
		s->polls++;
		mts_check_carrier(s);
		if (!READ_ONCE(s->terminal_fault))
			mts_tx_reclaim(s);
		if (budget)
			done = mts_rx_poll(s, budget);
	}
	/* Polling adaptation of native stall-resume, not literal STAR parity:
	 * resume once per positive-budget poll even when none completed. Budget0
	 * remains TX-only. Never resume after observed stop/error.
	 */
	if (budget > 0 && READ_ONCE(s->engine_live) && READ_ONCE(s->poll_enabled) &&
	    !READ_ONCE(s->terminal_fault) && !READ_ONCE(s->error)) {
		dma_wmb();
		control = readl(s->mac + 0x38);
		if (control == U32_MAX)
			mts_request_stop(s, -EIO, "rx-control-invalid");
		else
			writel(control | 4, s->mac + 0x38);
	}
	if (budget && done < budget) {
		/* No accesses to driver state after ownership release. */
		napi_complete_done(napi, done);
		return done;
	}
	return done;
}

static netdev_tx_t mts_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct mts_net *s = &mts;
	struct mts_desc *ring = s->memory[TX_RING].cpu;
	unsigned int slot, length;
	unsigned long flags, stats_flags;
	void *buffer;
	u32 control;
	spin_lock_irqsave(&s->tx_lock, flags);
	if (!mts_tx_allowed(s)) {
		netif_stop_queue(ndev);
		spin_unlock_irqrestore(&s->tx_lock, flags);
		return NETDEV_TX_BUSY; /* skb untouched. */
	}
	if (skb->len < ETH_HLEN || skb->len > ETH_FRAME_LEN ||
	    skb_vlan_tag_present(skb) || skb->protocol == htons(ETH_P_8021Q) ||
	    skb->protocol == htons(ETH_P_8021AD) || skb->ip_summed == CHECKSUM_PARTIAL)
		goto dropped;
	length = max_t(unsigned int, skb->len, ETH_ZLEN);
	slot = s->tx.head;
	/* Admission precedes touching a persistent DMA bounce buffer. */
	if (slot >= MTS_SLOTS || s->tx.submitted[slot]) {
		netif_stop_queue(ndev);
		spin_unlock_irqrestore(&s->tx_lock, flags);
		mts_request_stop(s, -EIO, "tx-ledger-invalid");
		return NETDEV_TX_BUSY;
	}
	buffer = s->memory[TX_POOL].cpu + slot * MTS_BUFFER_BYTES;
	if (skb_copy_bits(skb, 0, buffer, skb->len))
		goto dropped;
	if (length > skb->len)
		memset(buffer + skb->len, 0, length - skb->len);
	if (!s->tx.count)
		s->tx_progress = jiffies;
	if (!mts_tx_reserve(&s->tx, length, &slot)) {
		spin_unlock_irqrestore(&s->tx_lock, flags);
		mts_request_stop(s, -EIO, "tx-ledger-invalid");
		return NETDEV_TX_BUSY;
	}
	WRITE_ONCE(ring[slot].word[1], cpu_to_le32(s->memory[TX_POOL].dma + slot * MTS_BUFFER_BYTES));
	WRITE_ONCE(ring[slot].word[2], 0);
	WRITE_ONCE(ring[slot].word[3], 0);
	WRITE_ONCE(ring[slot].word[0], cpu_to_le32(mts_tx_ready(slot, length)));
	/* Account before the controller can complete ownership. */
	netdev_sent_queue(ndev, length);
	dma_wmb();
	WRITE_ONCE(ring[slot].word[0], cpu_to_le32(mts_tx_ready(slot, length) & ~MTS_COWN));
	control = readl(s->mac + 0x34);
	if (control == U32_MAX) {
		mts_request_stop(s, -EIO, "tx-control-invalid");
	} else {
		writel(control | 4, s->mac + 0x34);
		/* Flush posted kick, as in the accepted trace; timing is observable. */
		s->tx_after_kick = readl(s->mac + 0x34);
		if (s->tx_after_kick == U32_MAX)
			mts_request_stop(s, -EIO, "tx-kick-invalid");
	}
	if (s->tx.count == MTS_SLOTS)
		netif_stop_queue(ndev);
	spin_unlock_irqrestore(&s->tx_lock, flags);
	netif_trans_update(ndev);
	dev_consume_skb_any(skb);
	return NETDEV_TX_OK;
dropped:
	spin_lock_irqsave(&s->stats_lock, stats_flags);
	s->stats.tx_dropped++;
	spin_unlock_irqrestore(&s->stats_lock, stats_flags);
	spin_unlock_irqrestore(&s->tx_lock, flags);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int mts_open(struct net_device *ndev)
{
	struct mts_net *s = &mts;
	bool p_locked = false, g_locked = false;
	unsigned long flags;
	u32 control;
	int error = -EBUSY;
	mutex_lock(&s->lifecycle);
	if (READ_ONCE(THIS_MODULE->state) != MODULE_STATE_LIVE || !s->bound ||
	    s->detached || READ_ONCE(s->terminal_fault) || READ_ONCE(s->error))
		goto out;
	if (s->published) {
		if (!READ_ONCE(s->engine_live))
			goto out;
		/* Join DOWN housekeeping before reopening stack admission. No
		 * hardware/DMA/PHY reinitialization or BQL reset is allowed here.
		 */
		mts_pause_poll(s);
		spin_lock_irqsave(&s->tx_lock, flags);
		if (!READ_ONCE(s->engine_live) || READ_ONCE(s->terminal_fault) ||
		    READ_ONCE(s->error)) {
			spin_unlock_irqrestore(&s->tx_lock, flags);
			error = -EIO;
			goto out;
		}
		s->admin_up = true;
		s->admin_opens++;
		s->tx_progress = jiffies;
		s->reason = "admin-up";
		netif_trans_update(ndev);
		if (mts_tx_allowed(s))
			netif_start_queue(ndev);
		spin_unlock_irqrestore(&s->tx_lock, flags);
		mts_resume_poll(s);
		error = 0;
		goto out;
	}
	if (!(s->allowed || autoarm) || s->attempted)
		goto out;
	s->attempted = true;
	s->allowed = false;
	s->reason = "opening";
	if (!device_trylock(&s->pdev->dev))
		goto refused;
	p_locked = true;
	if (!device_trylock(&s->glue->dev))
		goto refused;
	g_locked = true;
	error = mts_metadata(s);
	if (error)
		goto refused;
	error = mts_baseline(s, true);
	if (error)
		goto refused;
	/* Establish the tested stopped prerequisite BEFORE PHY transactions. */
	writel(MTS_MASK, s->mac + 0x54);
	writel(2, s->mac + 0x34);
	error = readl_poll_timeout(s->mac + 0x34, control,
		!(control & 2) || control == U32_MAX, 10, 2000000);
	if (error || control == U32_MAX) {
		error = error ? error : -EIO;
		goto refused;
	}
	writel(2, s->mac + 0x38);
	error = readl_poll_timeout(s->mac + 0x38, control,
		!(control & 2) || control == U32_MAX, 10, 2000000);
	if (error || control == U32_MAX) {
		error = error ? error : -EIO;
		goto refused;
	}
	error = mts_baseline(s, false);
	if (error)
		goto refused;
	error = mts_restore_link(s);
	if (error)
		goto refused;
	error = mts_metadata(s);
	if (error)
		goto refused;
	error = mts_baseline(s, false);
	if (error)
		goto refused;
	error = mts_prepare_memory(s);
	if (error)
		goto refused;
	s->state = MTS_PREPARED;
	error = mts_metadata(s);
	if (error)
		goto refused;
	error = mts_baseline(s, false);
	if (error)
		goto refused;
	if (READ_ONCE(THIS_MODULE->state) != MODULE_STATE_LIVE ||
	    !try_module_get(THIS_MODULE)) {
		error = -EBUSY;
		goto refused;
	}
	s->self_pinned = true;
	s->published = true; /* Irreversible BEFORE the first address write. */
	dma_wmb();
	writel(lower_32_bits(s->memory[TX_RING].dma), s->mac + 0x44);
	writel(lower_32_bits(s->memory[TX_RING].dma), s->mac + 0x3c);
	writel(lower_32_bits(s->memory[RX_RING].dma), s->mac + 0x48);
	writel(lower_32_bits(s->memory[RX_RING].dma), s->mac + 0x40);
	control = readl(s->mac + 0x34);
	if (control == U32_MAX) {
		error = -EIO;
		goto published_failure;
	}
	writel(control | 1, s->mac + 0x34);
	control = readl(s->mac + 0x38);
	if (control == U32_MAX) {
		error = -EIO;
		goto published_failure;
	}
	writel(control | 1, s->mac + 0x38);
	s->state = MTS_RUNNING;
	s->reason = "admin-up";
	spin_lock_irqsave(&s->tx_lock, flags);
	WRITE_ONCE(s->engine_live, true);
	WRITE_ONCE(s->admin_up, true);
	s->link_up = !!(s->mac04 & BIT(0));
	s->admin_opens++;
	s->tx_progress = jiffies;
	s->next_link_check = jiffies;
	/* BQL resets only once, before the first submitted descriptor. */
	netdev_reset_queue(ndev);
	if (s->link_up)
		netif_carrier_on(ndev);
	else
		netif_carrier_off(ndev);
	if (mts_tx_allowed(s))
		netif_start_queue(ndev);
	else
		netif_stop_queue(ndev);
	spin_unlock_irqrestore(&s->tx_lock, flags);
	mts_resume_poll(s);
	pr_info("RUNNING continuous polling-only link=%u published=1 retained=1 lease=none\n", s->link_up);
	error = 0;
	goto unlock;
published_failure:
	s->error = error;
	mts_terminal_stop(s, "publication-failure");
	error = 0; /* No teardown/negative-init path after publication. */
	goto unlock;
refused:
	s->error = error;
	s->state = MTS_REFUSED;
	s->reason = "open-refused";
	mts_free_unpublished(s);
unlock:
	if (g_locked)
		device_unlock(&s->glue->dev);
	if (p_locked)
		device_unlock(&s->pdev->dev);
out:
	mutex_unlock(&s->lifecycle);
	return error;
}

#define MTS_ARL_CFG 0x10
#define MTS_ARL_MISC_MODE BIT(4) /* promiscuous */

static void mts_set_rx_mode(struct net_device *ndev)
{
	struct mts_net *s = &mts;
	u32 arl;

	if (!s->mac || READ_ONCE(s->terminal_fault))
		return;
	arl = readl(s->mac + MTS_ARL_CFG);
	if (arl == U32_MAX)
		return;
	if (ndev->flags & IFF_PROMISC)
		arl |= MTS_ARL_MISC_MODE;
	else
		arl &= ~MTS_ARL_MISC_MODE;
	writel(arl, s->mac + MTS_ARL_CFG);
}

static void mts_clear_rx_mode(struct mts_net *s)
{
	u32 arl;

	if (!s->mac)
		return;
	arl = readl(s->mac + MTS_ARL_CFG);
	if (arl != U32_MAX && (arl & MTS_ARL_MISC_MODE))
		writel(arl & ~MTS_ARL_MISC_MODE, s->mac + MTS_ARL_CFG);
}

static int mts_close(struct net_device *ndev)
{
	struct mts_net *s = &mts;
	unsigned long flags;
	mutex_lock(&s->lifecycle);
	spin_lock_irqsave(&s->tx_lock, flags);
	WRITE_ONCE(s->admin_up, false);
	s->admin_closes++;
	spin_unlock_irqrestore(&s->tx_lock, flags);
	/* Never acquire the core xmit lock while holding tx_lock. */
	netif_tx_disable(ndev);
	mts_pause_poll(s);
	mts_clear_rx_mode(s);
	if (READ_ONCE(s->terminal_fault) || READ_ONCE(s->error))
		mts_terminal_stop(s, "ndo-stop-terminal");
	else if (READ_ONCE(s->engine_live)) {
		s->reason = "admin-down-housekeeping";
		mts_resume_poll(s);
	}
	mutex_unlock(&s->lifecycle);
	return 0;
}

static void mts_timeout(struct net_device *ndev, unsigned int txqueue)
{
	(void)ndev;
	(void)txqueue;
	/* Ignore a watchdog racing with an ordinary cable/admin transition. */
	if (READ_ONCE(mts.admin_up) && READ_ONCE(mts.link_up) &&
	    READ_ONCE(mts.engine_live) && !READ_ONCE(mts.terminal_fault))
		mts_request_stop(&mts, -ETIMEDOUT, "tx-watchdog");
}

static void mts_stats(struct net_device *ndev, struct rtnl_link_stats64 *out)
{
	unsigned long flags;
	(void)ndev;
	spin_lock_irqsave(&mts.stats_lock, flags);
	*out = mts.stats;
	spin_unlock_irqrestore(&mts.stats_lock, flags);
}

static const struct net_device_ops mts_ops = {
	.ndo_open = mts_open, .ndo_stop = mts_close, .ndo_start_xmit = mts_xmit,
	.ndo_tx_timeout = mts_timeout, .ndo_get_stats64 = mts_stats,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode = mts_set_rx_mode,
};

static const struct ethtool_ops mts_ethtool_ops = {
	.get_link = ethtool_op_get_link,
};

static int mts_allow_set(const char *value, const struct kernel_param *param)
{
	int error = -EBUSY;
	(void)param;
	if (!sysfs_streq(value, "1"))
		return -EINVAL;
	if (READ_ONCE(THIS_MODULE->state) != MODULE_STATE_LIVE)
		return -EBUSY;
	mutex_lock(&mts.lifecycle);
	if (mts.bound && !mts.attempted && !mts.published && !mts.allowed &&
	    !mts.detached) {
		mts.allowed = true;
		error = 0;
	}
	mutex_unlock(&mts.lifecycle);
	return error;
}

static int mts_report_get(char *buffer, const struct kernel_param *param)
{
	struct mts_net *s = &mts;
	struct rtnl_link_stats64 stats;
	unsigned long flags;
	unsigned int count;
	(void)param;
	if (READ_ONCE(THIS_MODULE->state) != MODULE_STATE_LIVE)
		return scnprintf(buffer, PAGE_SIZE, "{\"initializing_or_going\":true}\n");
	spin_lock_irqsave(&s->stats_lock, flags);
	stats = s->stats;
	spin_unlock_irqrestore(&s->stats_lock, flags);
	spin_lock_irqsave(&s->tx_lock, flags);
	count = s->tx.count;
	spin_unlock_irqrestore(&s->tx_lock, flags);
	return scnprintf(buffer, PAGE_SIZE,
		"{\"state\":%u,\"bound\":%u,\"allowed\":%u,\"attempted\":%u,"
		"\"published\":%u,\"self_pinned\":%u,\"running\":%u,\"reason\":\"%s\","
		"\"continuous\":1,\"engine_live\":%u,\"admin_up\":%u,\"poll_enabled\":%u,"
		"\"carrier\":%u,\"terminal_fault\":%u,\"carrier_sample_jiffies\":%lu,"
		"\"admin_opens\":%llu,\"admin_closes\":%llu,\"carrier_changes\":%llu,"
		"\"error\":%d,\"stop_error\":%d,\"stop_ack\":%u,\"snapshot_atomic\":false,"
		"\"tx_packets\":%llu,\"tx_bytes\":%llu,\"tx_dropped\":%llu,\"tx_pending\":%u,"
		"\"rx_packets\":%llu,\"rx_bytes\":%llu,\"rx_dropped\":%llu,"
		"\"rx_length_drops\":%llu,\"rx_fragment_drops\":%llu,\"rx_crc_drops\":%llu,"
		"\"rx_oversize_drops\":%llu,\"rx_vlan_drops\":%llu,\"rx_alloc_drops\":%llu,"
		"\"rx_admin_drops\":%llu,\"rx_link_drops\":%llu,"
		"\"phy_id\":%u,\"phy_page\":%u,\"bmcr\":%u,\"bmsr\":%u,"
		"\"mmd147_before\":%u,\"mmd147_after\":%u,\"phy_setting_attempted\":%u,"
		"\"mdio_controller_writes\":%u,\"mac04\":%u,\"mac08\":%u,\"arl10\":%u,"
		"\"mac14\":%u,\"mac18\":%u,\"txd0\":%u,\"txd2\":%u,\"rxd0\":%u,"
		"\"rx_length\":%u,\"tx_after_kick\":%u,\"tx_control\":%u,\"rx_control\":%u,"
		"\"mask\":%u,\"gate\":%u,\"lease_seconds\":0,"
		"\"stop_entry_attempted\":%u,\"stop_entry_sampled\":%u,\"stop_entry_valid\":%u,"
		"\"stop_entry_status50\":%u,\"stop_entry_tx_dptr3c\":%u,\"stop_entry_rx_dptr40\":%u,"
		"\"stop_entry_tx_head\":%u,\"stop_entry_tx_tail\":%u,\"stop_entry_tx_count\":%u,"
		"\"stop_entry_rx_tail\":%u,"
		"\"dma\":[[%llu,%zu],[%llu,%zu],[%llu,%zu],[%llu,%zu]],\"drain_proven\":false}\n",
		READ_ONCE(s->state), READ_ONCE(s->bound), READ_ONCE(s->allowed),
		READ_ONCE(s->attempted), READ_ONCE(s->published), READ_ONCE(s->self_pinned),
		READ_ONCE(s->engine_live), READ_ONCE(s->reason),
		READ_ONCE(s->engine_live), READ_ONCE(s->admin_up), READ_ONCE(s->poll_enabled),
		READ_ONCE(s->link_up), READ_ONCE(s->terminal_fault), READ_ONCE(s->carrier_sample_jiffies),
		(unsigned long long)READ_ONCE(s->admin_opens),
		(unsigned long long)READ_ONCE(s->admin_closes),
		(unsigned long long)READ_ONCE(s->carrier_changes), READ_ONCE(s->error),
		READ_ONCE(s->stop_error), READ_ONCE(s->stop_ack),
		(unsigned long long)stats.tx_packets, (unsigned long long)stats.tx_bytes,
		(unsigned long long)stats.tx_dropped, count,
		(unsigned long long)stats.rx_packets, (unsigned long long)stats.rx_bytes,
		(unsigned long long)stats.rx_dropped,
		(unsigned long long)READ_ONCE(s->rx_length_drops),
		(unsigned long long)READ_ONCE(s->rx_fragment_drops),
		(unsigned long long)READ_ONCE(s->rx_crc_drops),
		(unsigned long long)READ_ONCE(s->rx_oversize_drops),
		(unsigned long long)READ_ONCE(s->rx_vlan_drops),
		(unsigned long long)READ_ONCE(s->rx_alloc_drops),
		(unsigned long long)READ_ONCE(s->rx_admin_drops),
		(unsigned long long)READ_ONCE(s->rx_link_drops),
		(s->phy_id2 << 16) | s->phy_id3, s->phy_page, s->phy_bmcr, s->bmsr_second,
		s->mmd_before, s->mmd_after, s->phy_setting_attempted, s->mdio_writes,
		s->mac04, s->mac08, s->arl10, s->mac14, s->mac18,
		READ_ONCE(s->last_txd0), READ_ONCE(s->last_txd2), READ_ONCE(s->last_rxd0),
		READ_ONCE(s->last_rx_length), READ_ONCE(s->tx_after_kick),
		s->tx_control, s->rx_control, s->mac_mask, s->route,
		READ_ONCE(s->stop_entry_attempted), READ_ONCE(s->stop_entry_sampled),
		READ_ONCE(s->stop_entry_valid), READ_ONCE(s->stop_entry_status50),
		READ_ONCE(s->stop_entry_tx_dptr3c), READ_ONCE(s->stop_entry_rx_dptr40),
		READ_ONCE(s->stop_entry_tx_head), READ_ONCE(s->stop_entry_tx_tail),
		READ_ONCE(s->stop_entry_tx_count), READ_ONCE(s->stop_entry_rx_tail),
		(unsigned long long)s->memory[0].dma, s->memory[0].size,
		(unsigned long long)s->memory[1].dma, s->memory[1].size,
		(unsigned long long)s->memory[2].dma, s->memory[2].size,
		(unsigned long long)s->memory[3].dma, s->memory[3].size);
}

static const struct kernel_param_ops allow_ops = {.set = mts_allow_set};
static const struct kernel_param_ops report_ops = {.get = mts_report_get};
module_param_cb(allow_open, &allow_ops, NULL, 0200);
MODULE_PARM_DESC(allow_open, "LIVE-only first-publication gate; write exactly1 after fresh-boot preflight");
module_param_cb(report, &report_ops, NULL, 0400);
MODULE_PARM_DESC(report, "Cached non-atomic diagnostic counters/state only; no hardware reads");
module_param(autoarm, bool, 0444);
MODULE_PARM_DESC(autoarm, "First open publishes without allow_open (default1); 0 keeps the explicit gate");

static void mts_release_unpublished(struct mts_net *s)
{
	if (s->published)
		return;
	mts_free_unpublished(s);
	if (s->napi_added) {
		netif_napi_del(&s->napi);
		s->napi_added = false;
	}
	if (s->ndev) {
		free_netdev(s->ndev);
		s->ndev = NULL;
	}
	if (s->gate) {
		pci_iounmap(s->glue, s->gate);
		s->gate = NULL;
	}
	if (s->mac) {
		pci_iounmap(s->pdev, s->mac);
		s->mac = NULL;
	}
	if (s->bar_claimed) {
		pci_release_region(s->pdev, 0);
		s->bar_claimed = false;
	}
	if (s->owner_held) {
		module_put(s->glue_owner);
		s->owner_held = false;
	}
	if (s->glue) {
		pci_dev_put(s->glue);
		s->glue = NULL;
	}
	if (s->pdev) {
		pci_dev_put(s->pdev);
		s->pdev = NULL;
	}
}

/* Makes psx0 visible. Never hold lifecycle here: ndo_open takes it under rtnl. */
static int mts_register(struct mts_net *s)
{
	int error = register_netdev(s->ndev);

	if (error) {
		pr_err("netdevice registration refused error=%d; nothing published\n", error);
		return error;
	}
	s->registered = true;
	pr_info("REGISTERED %s %s\n", s->ndev->name,
		autoarm ? "autoarm: first open publishes" : "open-gated");
	return 0;
}

static int mts_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mts_net *s = &mts;
	u8 address[ETH_ALEN];
	bool locked = false;
	const char *stage = "glue-reference";
	int error = -EBUSY;
	(void)id;
	/* PCI core owns the GBE device lock here. */
	if (s->pdev || strcmp(init_utsname()->release, MTS_RELEASE))
		return -EBUSY;
	s->pdev = pci_dev_get(pdev);
	s->glue = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0x14, 4));
	if (!s->glue) {
		error = -ENODEV;
		goto fail;
	}
	stage = "glue-lock";
	if (!device_trylock(&s->glue->dev))
		goto fail;
	locked = true;
	stage = "metadata";
	error = mts_metadata(s);
	if (error)
		goto fail;
	s->glue_driver = s->glue->dev.driver;
	s->glue_owner = s->glue_driver->owner;
	stage = "glue-owner";
	if (!try_module_get(s->glue_owner)) {
		error = -EBUSY;
		goto fail;
	}
	s->owner_held = true;
	stage = "interrupt-capabilities";
	error = mts_caps_read(pdev, &s->caps);
	if (error)
		goto fail;
	stage = "BAR0-claim";
	error = pci_request_region(pdev, 0, MTS_DRIVER);
	if (error)
		goto fail;
	s->bar_claimed = true;
	stage = "BAR-mapping";
	s->mac = pci_iomap_range(pdev, 0, 0, MTS_MAP_BYTES);
	s->gate = pci_iomap_range(s->glue, 2, MTS_GATE_OFFSET, 4);
	if (!s->mac || !s->gate) {
		error = -ENOMEM;
		goto fail;
	}
	stage = "fresh-baseline";
	error = mts_baseline(s, true);
	if (error)
		goto fail;
	stage = "inherited-MAC";
	s->mac14 = readl(s->mac + 0x14);
	s->mac18 = readl(s->mac + 0x18);
	if (s->mac14 == U32_MAX || s->mac18 == U32_MAX) {
		error = -EIO;
		goto fail;
	}
	/* Native2516d1..251711 and STAR my_mac_h/l byte order. */
	address[0] = s->mac14 >> 8;
	address[1] = s->mac14;
	address[2] = s->mac18 >> 24;
	address[3] = s->mac18 >> 16;
	address[4] = s->mac18 >> 8;
	address[5] = s->mac18;
	if (!is_valid_ether_addr(address)) {
		error = -EINVAL;
		goto fail;
	}
	stage = "netdevice-allocation";
	s->ndev = alloc_etherdev(0);
	if (!s->ndev) {
		error = -ENOMEM;
		goto fail;
	}
	SET_NETDEV_DEV(s->ndev, &pdev->dev);
	strscpy(s->ndev->name, "psx0", IFNAMSIZ);
	s->ndev->name_assign_type = NET_NAME_PREDICTABLE;
	s->ndev->netdev_ops = &mts_ops;
	s->ndev->ethtool_ops = &mts_ethtool_ops;
	s->ndev->watchdog_timeo = 5 * HZ;
	s->ndev->min_mtu = ETH_DATA_LEN;
	s->ndev->max_mtu = ETH_DATA_LEN;
	s->ndev->features = 0;
	s->ndev->hw_features = 0;
	s->ndev->vlan_features = 0;
	s->ndev->netns_immutable = true;
	eth_hw_addr_set(s->ndev, address);
	ether_addr_copy(s->ndev->perm_addr, address);
	s->ndev->addr_assign_type = NET_ADDR_PERM;
	netif_carrier_off(s->ndev);
	netif_napi_add_weight(s->ndev, &s->napi, mts_poll, 64);
	s->napi_added = true;
	s->state = MTS_PROBED;
	s->reason = autoarm ? "probed-autoarm" : "probed-gated";
	s->bound = true;
	pci_set_drvdata(pdev, s);
	/* Unlock glue before making the gated interface visible. */
	device_unlock(&s->glue->dev);
	locked = false;
	pr_info("PROBED inherited-MAC=%pM %s fresh-mask=%x\n", address,
		autoarm ? "autoarm" : "open-gated", s->mac_mask);
	/* A load-time probe leaves psx0 to the module LIVE notifier, so no open
	 * can race initialization; a probe after LIVE registers immediately.
	 */
	if (READ_ONCE(THIS_MODULE->state) != MODULE_STATE_LIVE)
		return 0;
	stage = "netdevice-registration";
	error = mts_register(s);
	if (error)
		goto fail;
	return 0;
fail:
	pr_err("probe refused stage=%s error=%d published=%u\n",
		stage, error, s->published);
	s->probe_error = error;
	s->bound = false;
	if (locked)
		device_unlock(&s->glue->dev);
	pci_set_drvdata(pdev, NULL);
	mts_release_unpublished(s);
	return error;
}

static void mts_remove(struct pci_dev *pdev)
{
	struct mts_net *s = pci_get_drvdata(pdev);
	if (!s)
		return;
	/* Removal already owns the PCI device lock; never lock it recursively. */
	mutex_lock(&s->lifecycle);
	s->detached = true;
	s->bound = false;
	if (s->published) {
		mts_terminal_stop(s, "pci-remove");
		netif_device_detach(s->ndev);
		mutex_unlock(&s->lifecycle);
		pr_warn("published remove: all resources/code/netdev retained; PCI-core removal recovery UNPROVEN\n");
		return;
	}
	mutex_unlock(&s->lifecycle);
	/* Work joining is outside the lifecycle mutex and before freeing state. */
	cancel_work_sync(&s->stop_work);
	if (s->registered) {
		unregister_netdev(s->ndev);
		s->registered = false;
	}
	pci_set_drvdata(pdev, NULL);
	mts_release_unpublished(s);
}

static void mts_shutdown(struct pci_dev *pdev)
{
	struct mts_net *s = pci_get_drvdata(pdev);
	if (!s)
		return;
	mutex_lock(&s->lifecycle);
	mts_terminal_stop(s, "shutdown");
	mutex_unlock(&s->lifecycle);
}

static const struct pci_device_id mts_ids[] = {
	{ PCI_DEVICE(0x104d, 0x90d8) }, {0}
};
MODULE_DEVICE_TABLE(pci, mts_ids);

static int mts_pm_prepare(struct device *dev)
{
	(void)dev;
	/* No unreviewed PCI default suspend while retained state/polling exists. */
	return -EBUSY;
}

static const struct dev_pm_ops mts_pm_ops = {.prepare = mts_pm_prepare};
/* PCI alias autoload; probe and first open keep their fresh-boot checks. */
static struct pci_driver mts_driver = {
	.name = MTS_DRIVER, .id_table = mts_ids, .probe = mts_probe,
	.remove = mts_remove, .shutdown = mts_shutdown,
	.driver = {.suppress_bind_attrs = true, .pm = &mts_pm_ops},
};

static int mts_module_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mts_net *s = &mts;
	(void)nb;
	if (event != MODULE_STATE_LIVE || data != THIS_MODULE)
		return NOTIFY_DONE;
	/* Probe finished inside init; unbind is suppressed and unload needs LIVE. */
	if (s->bound && !s->registered && mts_register(s))
		s->reason = "registration-refused";
	return NOTIFY_DONE;
}

static struct notifier_block mts_module_nb = {.notifier_call = mts_module_event};

static int __init mts_init(void)
{
	int error;
	if (strcmp(init_utsname()->release, MTS_RELEASE))
		return -EINVAL;
	mutex_init(&mts.lifecycle);
	spin_lock_init(&mts.tx_lock);
	spin_lock_init(&mts.stats_lock);
	INIT_WORK(&mts.stop_work, mts_stop_work);
	hrtimer_setup(&mts.tick, mts_tick, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	mts.reason = "absent";
	error = register_module_notifier(&mts_module_nb);
	if (error)
		return error;
	error = pci_register_driver(&mts_driver);
	if (error)
		goto unregister_notifier;
	if (!mts.bound) {
		error = mts.probe_error ? mts.probe_error : -ENODEV;
		pci_unregister_driver(&mts_driver);
		goto unregister_notifier;
	}
	return 0;
unregister_notifier:
	unregister_module_notifier(&mts_module_nb);
	return error;
}

static void __exit mts_exit(void)
{
	/* The permanent publication reference prevents normal entry when armed. */
	unregister_module_notifier(&mts_module_nb);
	pci_unregister_driver(&mts_driver);
	cancel_work_sync(&mts.stop_work);
	pr_info("unpublished unload only; force unload unsupported\n");
}
module_init(mts_init);
module_exit(mts_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Continuous Baikal polled Ethernet, one retained DMA generation per boot");
MODULE_AUTHOR("Meerzulee and Linux psX contributors");

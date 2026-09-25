// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MAC setup: reset, timing, frame buffer accounting, receive filtering
 * and the per-interface registers.
 *
 * Sequence and register tables taken from the vendor driver
 * (ssv6006C_mac.c and dev_tbl.h):
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 */
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/jhash.h>
#include <linux/mmc/card.h>
#include <linux/of_net.h>
#include <linux/unaligned.h>

#include "ssv6256.h"

/* Reserved head room in front of every buffered frame, in 16-byte units. */
#define TX_PKT_RSVD_SETTING	3
/* Bytes of descriptor the hardware skips to reach the 802.11 header. */
#define PB_OFFSET_BYTES		80

static const struct ssv6256_reg mac_ini_table[] = {
	{ ADR_CONTROL,		0x12000006 },
	{ ADR_RX_TIME_STAMP_CFG, (28 << 8) | 0x01 },
	{ ADR_GLBLE_SET,	DUP_FLT |
				(TX_PKT_RSVD_SETTING << 18) |
				(PB_OFFSET_BYTES << 8) },
	{ ADR_TX_ETHER_TYPE_0,	0x00000000 },
	{ ADR_TX_ETHER_TYPE_1,	0x00000000 },
	{ ADR_RX_ETHER_TYPE_0,	0x00000000 },
	{ ADR_RX_ETHER_TYPE_1,	0x00000000 },
	{ ADR_REASON_TRAP0,	0x7fbc7f87 },
	{ ADR_REASON_TRAP1,	0x0000013f },
	{ ADR_TRAP_HW_ID,	M_ENG_CPU },
	{ ADR_WSID0,		0x00000000 },
	{ ADR_WSID1,		0x00000000 },
	{ ADR_WSID2,		0x00000000 },
	{ ADR_WSID3,		0x00000000 },
	{ ADR_WSID4,		0x00000000 },
	{ ADR_WSID5,		0x00000000 },
	{ ADR_WSID6,		0x00000000 },
	{ ADR_WSID7,		0x00000000 },
	{ ADR_MASK_TYPHOST_INT_MAP, 0xffff7fff },
	{ ADR_MASK_TYPHOST_INT_MAP_15, 0xff0fffff },
	{ ADR_MTX_RESPFRM_RATE_TABLE_01, 0x0000 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_02, 0x0000 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_03, 0x0002 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_11, 0x0000 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_12, 0x0000 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_13, 0x0012 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_92_B2, 0x9090 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_94_B4, 0x9292 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_C1_E1, 0x9090 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_C3_E3, 0x9292 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_D1_F1, 0x9090 },
	{ ADR_MTX_RESPFRM_RATE_TABLE_D3_F3, 0x9292 },
	{ ADR_BA_CTRL,		0x9 },
};

/* Hardware station table: the valid flag, then the peer address. */
static const u32 wsid_reg[] = {
	ADR_WSID0, ADR_WSID1, ADR_WSID2, ADR_WSID3,
	ADR_WSID4, ADR_WSID5, ADR_WSID6, ADR_WSID7,
};

#define WSID_PEER_MAC0		4
#define WSID_PEER_MAC1		8

#define DECI(_type, _mask, _action, _drop) \
	((_type) << 9 | (_mask) << 3 | (_action) << 1 | (_drop))
#define DECI_NOP	0
#define DECI_NAV_UPD	1
#define DECI_NAV_RST	2
#define DECI_ACK	3

/* MAC RX filter: 16 decision entries followed by 9 enable masks. */
static const u16 deci_tbl[] = {
	DECI(0x1e, 0x3e, DECI_NAV_RST, 1),
	DECI(0x18, 0x3e, DECI_ACK, 0),
	DECI(0x1a, 0x3f, DECI_ACK, 1),
	DECI(0x10, 0x38, DECI_NOP, 1),
	DECI(0x25, 0x3f, DECI_NOP, 1),
	DECI(0x26, 0x36, DECI_NOP, 1),
	DECI(0x08, 0x3f, DECI_NOP, 0),
	DECI(0x05, 0x3f, DECI_ACK, 0),
	DECI(0x0b, 0x3f, DECI_ACK, 0),
	DECI(0x01, 0x3d, DECI_ACK, 0),
	DECI(0x20, 0x30, DECI_ACK, 0),
	DECI(0x00, 0x00, DECI_ACK, 0),
	DECI(0x00, 0x00, DECI_NOP, 1),
	DECI(0x00, 0x00, DECI_NAV_UPD, 1),
	DECI(0x00, 0x00, DECI_NAV_RST, 1),
	DECI(0x00, 0x00, DECI_ACK, 1),
	0x2008, 0x1001, 0x0808, 0x1040, 0x2008, 0x800e, 0x0bb8, 0x2b88, 0x0800,
};

#define DECI_TBL1_SIZE	16
#define DECI_TBL2_SIZE	9

/*
 * Frame buffer accounting.  The chip holds 128 packet ids and 256 pages
 * of 256 bytes; the rest of each pool is left for receive.
 */
#define ID_TX_THRESHOLD		62
#define ID_RX_THRESHOLD		64
#define PAGE_TX_THRESHOLD	192
#define PAGE_RX_THRESHOLD	64
#define TX_LOWTHRESHOLD_PAGE	96
#define TX_LOWTHRESHOLD_ID	61

/* Descriptor sizes the DMA engine has to skip over (see tx.c, rx.c). */
#define TX_DESC_SIZE		80
#define RX_DESC_SIZE		32
#define RX_PINFO_PAD		4

/* e-fuse: a chip id register plus a bit-packed list of items */
#define EFUSE_ID_READ_SWITCH	0xc2000128
#define EFUSE_ID_RAW_DATA	0xc200014c
#define EFUSE_READ_SWITCH	0xc200012c
#define EFUSE_RAW_DATA		0xc2000150
#define EFUSE_SECTIONS		((256 - 32) >> 5)
#define EFUSE_ITEM_MAC		3
#define EFUSE_ITEM_MAC_NEW	11

/* Payload width of each e-fuse item, in bits, indexed by its id. */
static const u8 efuse_item_bits[] = {
	0, 8, 8, 48, 8, 8, 8, 4, 0, 16, 16, 48, 8, 8,
};

int ssv6256_write_table(struct ssv6256_dev *sd, const struct ssv6256_reg *t, size_t n)
{
	size_t i;
	int ret;

	for (i = 0; i < n; i++) {
		ret = ssv6256_reg_write(sd, t[i].addr, t[i].data);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * The e-fuse holds a bit-packed list of items, each a 4-bit id followed
 * by its payload; only the MAC address is used here.  A newer address
 * may be programmed on top of the original one, and wins.
 */
static void ssv6256_read_efuse(struct ssv6256_dev *sd)
{
	u8 map[EFUSE_SECTIONS * 4] = {};
	u32 val, pos = 0;
	int i;

	ssv6256_reg_write(sd, EFUSE_ID_READ_SWITCH, 1);
	if (!ssv6256_reg_read(sd, EFUSE_ID_RAW_DATA, &val))
		dev_dbg(sd->dev, "e-fuse chip identity 0x%08x\n", val);

	for (i = 0; i < EFUSE_SECTIONS; i++) {
		ssv6256_reg_write(sd, EFUSE_READ_SWITCH + i * 4, 1);
		if (ssv6256_reg_read(sd, EFUSE_RAW_DATA + i * 4, &val))
			return;
		put_unaligned_le32(val, map + i * 4);
	}

	eth_zero_addr(sd->mac);
	while (map[0] && pos + 4 + 48 <= sizeof(map) * 8) {
		u8 id = (get_unaligned_le16(map + pos / 8) >> (pos % 8)) & 0xf;
		int j;

		if (id == 0 || id >= ARRAY_SIZE(efuse_item_bits))
			break;
		pos += 4;
		if (id == EFUSE_ITEM_MAC || id == EFUSE_ITEM_MAC_NEW) {
			for (j = 0; j < ETH_ALEN; j++, pos += 8)
				sd->mac[j] = get_unaligned_le16(map + pos / 8) >>
					     (pos % 8);
		} else {
			pos += efuse_item_bits[id];
		}
	}

	/*
	 * Modules with a blank e-fuse depend on the board: the device
	 * tree can carry the address the vendor printed on the label.
	 */
	if (!is_valid_ether_addr(sd->mac))
		of_get_mac_address(sd->dev->of_node, sd->mac);

	/*
	 * Still nothing: derive a stable locally administered address from
	 * the card identification, so that it survives a reload.
	 */
	if (!is_valid_ether_addr(sd->mac)) {
		u32 h = jhash_2words(sd->func->card->cid.serial,
				     sd->func->card->cid.manfid, 0);

		sd->mac[0] = 0x02;
		sd->mac[1] = sd->func->card->cid.oemid;
		put_unaligned_le32(h, sd->mac + 2);
		eth_addr_inc(sd->mac);	/* keeps it valid if the hash is zero */
		dev_warn(sd->dev, "no MAC address in e-fuse or device tree, using %pM\n",
			 sd->mac);
	}
}

/* The reset bit clears itself once the MAC has come back. */
static int ssv6256_mac_reset(struct ssv6256_dev *sd)
{
	u32 val = MAC_SW_RST;
	int i;

	ssv6256_reg_write(sd, ADR_BRG_SW_RST, MAC_SW_RST);
	for (i = 0; i < 1000; i++) {
		if (ssv6256_reg_read(sd, ADR_BRG_SW_RST, &val))
			return -EIO;
		if (!val)
			return 0;
		usleep_range(50, 100);
	}
	dev_err(sd->dev, "MAC reset did not complete\n");
	return -ETIMEDOUT;
}

/*
 * Ask the chip to group received frames: instead of interrupting the
 * host for each one, it fills a buffer and hands the lot over in a
 * single bus transaction.  Each frame inside it is preceded by a short
 * header saying how far the next one is (see rx.c).
 *
 * Up to eight frames or 12 KB, and a timer so that a lone frame is not
 * held back.
 */
#define RX_AGGR_COUNT		8
#define RX_AGGR_TIMER		20
#define RX_AGGR_HOST_SIZE	0x3000

int ssv6256_rx_aggr_init(struct ssv6256_dev *sd)
{
	u32 val;
	int i;

	/* nothing may be in flight while the format changes */
	ssv6256_reg_set_bits(sd, ADR_HCI_TRX_MODE, 0, HCI_RX_EN);
	for (i = 0; i < 1000; i++) {
		if (ssv6256_reg_read(sd, ADR_RX_PACKET_LENGTH_STATUS, &val))
			return -EIO;
		if (!(val & HCI_RX_LEN))
			break;
		usleep_range(50, 100);
	}
	if (val & HCI_RX_LEN) {
		dev_err(sd->dev, "receive engine did not drain\n");
		return -ETIMEDOUT;
	}

	ssv6256_reg_set_bits(sd, ADR_HCI_TRX_MODE, HCI_RX_FORM_1, HCI_RX_FORM_1);
	ssv6256_reg_write(sd, ADR_FORCE_RX_AGGREGATION_MODE,
			  FIELD_PREP(RX_AGG_CNT, RX_AGGR_COUNT) |
			  FIELD_PREP(RX_AGG_TIMER_RELOAD, RX_AGGR_TIMER));
	ssv6256_field_write(sd, ADR_HCI_FORCE_PRE_BULK_IN, HCI_BULK_IN_HOST_SIZE,
			    RX_AGGR_HOST_SIZE);
	ssv6256_reg_set_bits(sd, ADR_HCI_TRX_MODE, HCI_RX_EN, HCI_RX_EN);

	/*
	 * The chip does not always take the batch format: read it back,
	 * and if it did not stick, stay with one frame per read.  Slower,
	 * but the alternative is a receive side that delivers nothing.
	 */
	if (ssv6256_reg_read(sd, ADR_HCI_TRX_MODE, &val) ||
	    !(val & HCI_RX_FORM_1)) {
		dev_warn(sd->dev, "batch receive did not take (%08x)\n", val);
		ssv6256_reg_set_bits(sd, ADR_HCI_TRX_MODE, 0, HCI_RX_FORM_1);
		sd->rx_aggr = false;
		return 0;
	}
	sd->rx_aggr = true;
	return 0;
}

/*
 * MAC timing follows whatever digital clock the PLL settled on, so this
 * has to run after the radio is up.
 */
static int ssv6256_mac_clock(struct ssv6256_dev *sd)
{
	u32 clk;
	int ret;

	ret = ssv6256_field_read(sd, ADR_CLOCK_SELECTION, CLK_DIGI_SEL, &clk);
	if (ret)
		return ret;

	switch (clk) {
	case CLK_DIGI_80M:
		ssv6256_reg_set_bits(sd, ADR_MTX_TIME_FINETUNE,
				     MAC_CLK_80M | (26 << __ffs(PHYTXSTART_NCYCLE)),
				 MAC_CLK_80M | PHYTXSTART_NCYCLE);
		return ssv6256_field_write(sd, ADR_PRESCALER_USTIMER,
				       PRESCALER_US, 80);
	case CLK_DIGI_40M:
		ssv6256_reg_set_bits(sd, ADR_MTX_TIME_FINETUNE,
				     13 << __ffs(PHYTXSTART_NCYCLE),
				 MAC_CLK_80M | PHYTXSTART_NCYCLE);
		return ssv6256_field_write(sd, ADR_PRESCALER_USTIMER,
				       PRESCALER_US, 40);
	}

	dev_err(sd->dev, "invalid digital clock selection %u\n", clk);
	return -EINVAL;
}

static void ssv6256_set_macaddr(struct ssv6256_dev *sd, const u8 *addr)
{
	ssv6256_reg_write(sd, ADR_STA_MAC_0, get_unaligned_le32(addr));
	ssv6256_reg_write(sd, ADR_STA_MAC_1, get_unaligned_le16(addr + 4));
}

void ssv6256_set_bssid(struct ssv6256_dev *sd, const u8 *bssid)
{
	ssv6256_reg_write(sd, ADR_BSSID_0, get_unaligned_le32(bssid));
	ssv6256_reg_write(sd, ADR_BSSID_1, get_unaligned_le16(bssid + 4));
}

/*
 * Packet memory: the chip hands out a buffer of @size bytes and takes
 * it back through the trash can engine.
 */
u32 ssv6256_pbuf_alloc(struct ssv6256_dev *sd, size_t size, u32 type)
{
	u32 addr = 0;
	int i;

	size = round_up(size, 4);
	for (i = 0; i < 10; i++) {
		ssv6256_reg_write(sd, ADR_WR_ALC, FIELD_PREP(PBUF_SIZE, size) |
			      FIELD_PREP(PBUF_TYPE, type));
		if (ssv6256_reg_read(sd, ADR_WR_ALC, &addr) || addr)
			break;
		usleep_range(1000, 2000);
	}
	if (!addr)
		dev_err(sd->dev, "no packet buffer for %zu bytes\n", size);
	return addr;
}

void ssv6256_pbuf_free(struct ssv6256_dev *sd, u32 addr)
{
	u32 val;
	int i;

	/* the mailbox to the packet engine has to have room */
	for (i = 0; i < 1000; i++) {
		if (ssv6256_reg_read(sd, ADR_MCU_STATUS, &val) || !(val & CH0_FULL))
			break;
	}
	ssv6256_reg_write(sd, ADR_CH0_TRIG_1,
			  (M_ENG_TRASH_CAN << 7) | (addr >> 16));
}

/* In access point mode the MAC sends the beacon out of its own buffer. */
void ssv6256_set_ap_mode(struct ssv6256_dev *sd, bool ap)
{
	ssv6256_field_write(sd, ADR_GLBLE_SET, OP_MODE,
			    ap ? OPMODE_AP : OPMODE_STA);
}

void ssv6256_beacon_timing(struct ssv6256_dev *sd, u16 interval, u8 dtim_period)
{
	ssv6256_field_write(sd, ADR_MTX_BCN_PRD, MTX_BCN_PERIOD, interval ?: 100);
	ssv6256_field_write(sd, ADR_MTX_BCN_DTIM_CONFG, MTX_DTIM_NUM,
			    max_t(u8, dtim_period, 1) - 1);
	/* the MAC fills in the time stamp, sequence number and DTIM count */
	ssv6256_reg_set_bits(sd, ADR_MTX_BCN_EN_MISC,
			     MTX_TIME_STAMP_AUTO_FILL | MTX_BCN_AUTO_SEQ_NO |
			 MTX_DTIM_CNT_AUTO_FILL,
			 MTX_TIME_STAMP_AUTO_FILL | MTX_BCN_AUTO_SEQ_NO |
			 MTX_DTIM_CNT_AUTO_FILL);
}

int ssv6256_beacon_enable(struct ssv6256_dev *sd, bool enable)
{
	return ssv6256_field_write(sd, ADR_MTX_BCN_EN_MISC, MTX_BCN_TIMER_EN,
			       enable);
}

/*
 * Write a beacon into the slot the MAC is not sending from, and point
 * the MAC at it.  @dtim_offset says where the DTIM count sits, so that
 * the MAC can fill it in.
 */
int ssv6256_beacon_set(struct ssv6256_dev *sd, const u8 *buf, size_t len,
		       u16 dtim_offset)
{
	static const u32 pkt_reg[] = {
		ADR_MTX_BCN_PKT_SET0, ADR_MTX_BCN_PKT_SET1,
	};
	static const u32 dtim_reg[] = {
		ADR_MTX_BCN_DTIM_SET0, ADR_MTX_BCN_DTIM_SET1,
	};
	u32 val;
	int slot, i;

	/* hold the slot the MAC reports while it is being rewritten */
	ssv6256_field_write(sd, ADR_MTX_BCN_MISC, MTX_BCN_PKTID_CH_LOCK, 1);
	if (ssv6256_reg_read(sd, ADR_MTX_BCN_MISC, &val))
		return -EIO;
	slot = FIELD_GET(MTX_BCN_CFG_VLD, val) == 1 ? 1 : 0;

	if (sd->bcn_buf[slot] && sd->bcn_len[slot] < len) {
		ssv6256_pbuf_free(sd, sd->bcn_buf[slot]);
		sd->bcn_buf[slot] = 0;
	}
	if (!sd->bcn_buf[slot]) {
		sd->bcn_buf[slot] = ssv6256_pbuf_alloc(sd, len, PBUF_TX);
		sd->bcn_len[slot] = len;
	}
	if (!sd->bcn_buf[slot]) {
		ssv6256_field_write(sd, ADR_MTX_BCN_MISC, MTX_BCN_PKTID_CH_LOCK, 0);
		return -ENOMEM;
	}

	for (i = 0; i < len; i += 4)
		ssv6256_reg_write(sd, sd->bcn_buf[slot] + i,
				  get_unaligned_le32(buf + i));
	ssv6256_field_write(sd, pkt_reg[slot], MTX_BCN_PKT_ID,
			    FIELD_GET(PBUF_ADDR_ID, sd->bcn_buf[slot]));
	ssv6256_field_write(sd, dtim_reg[slot], MTX_DTIM_OFST, dtim_offset);
	return ssv6256_field_write(sd, ADR_MTX_BCN_MISC, MTX_BCN_PKTID_CH_LOCK, 0);
}

/* Stop the beacon and give its buffers back. */
void ssv6256_beacon_release(struct ssv6256_dev *sd)
{
	u32 val;
	int i;

	for (i = 0; i < 10; i++) {
		ssv6256_beacon_enable(sd, false);
		if (ssv6256_reg_read(sd, ADR_MTX_BCN_MISC, &val) ||
		    !(val & MTX_AUTO_BCN_ONGOING))
			break;
		usleep_range(1000, 2000);
	}
	for (i = 0; i < ARRAY_SIZE(sd->bcn_buf); i++) {
		if (sd->bcn_buf[i])
			ssv6256_pbuf_free(sd, sd->bcn_buf[i]);
		sd->bcn_buf[i] = 0;
		sd->bcn_len[i] = 0;
	}
}

static int ssv6256_mac_init(struct ssv6256_dev *sd)
{
	static const u8 zero_bssid[ETH_ALEN] = {};
	u32 val;
	int ret, i;

	ret = ssv6256_mac_reset(sd);
	if (ret)
		return ret;
	ret = ssv6256_mac_clock(sd);
	if (ret)
		return ret;
	ret = ssv6256_write_table(sd, mac_ini_table, ARRAY_SIZE(mac_ini_table));
	if (ret)
		return ret;

	ssv6256_field_write(sd, ADR_MTX_BCN_EN_MISC, MTX_TSF_TIMER_EN, 1);

	/* where the 802.11 header starts, and how long each descriptor is */
	ssv6256_reg_write(sd, ADR_HCI_TX_RX_INFO_SIZE,
			  (PB_OFFSET_BYTES << __ffs(TX_PBOFFSET)) |
		      (TX_DESC_SIZE << __ffs(TX_INFO_SIZE)) |
		      (RX_DESC_SIZE << __ffs(RX_INFO_SIZE)) |
		      (RX_PINFO_PAD << __ffs(RX_LAST_PHY_SIZE)));

	/* the receive watchdog drops frames the host is slow to read */
	if (!ssv6256_reg_read(sd, ADR_MRX_WATCH_DOG, &val))
		ssv6256_reg_write(sd, ADR_MRX_WATCH_DOG, val & ~0xfU);

	ret = ssv6256_rx_aggr_init(sd);
	if (ret)
		return ret;

	ssv6256_reg_set_bits(sd, ADR_TRX_ID_THRESHOLD,
			     (ID_TX_THRESHOLD << __ffs(TX_ID_THOLD)) |
			 (ID_RX_THRESHOLD << __ffs(RX_ID_THOLD)),
			 TX_ID_THOLD | RX_ID_THOLD);
	ssv6256_reg_set_bits(sd, ADR_ID_LEN_THREADSHOLD1,
			     (PAGE_TX_THRESHOLD << __ffs(ID_TX_LEN_THOLD)) |
			 (PAGE_RX_THRESHOLD << __ffs(ID_RX_LEN_THOLD)),
			 ID_TX_LEN_THOLD | ID_RX_LEN_THOLD);
	ssv6256_reg_write(sd, ADR_TX_LIMIT_INTR, TX_LIMIT_INT_EN |
		      (TX_LOWTHRESHOLD_ID << __ffs(TX_COUNT_LIMIT)) |
		      (TX_LOWTHRESHOLD_PAGE << __ffs(TX_PAGE_LIMIT)));

	/* halt the firmware mailbox on error rather than let it spin */
	ssv6256_field_write(sd, ADR_MBOX_HALT_CFG, MB_ERR_AUTO_HALT_EN, 1);
	ssv6256_field_write(sd, ADR_MB_DBG_CFG1, MB_DBG_EN, 1);

	ssv6256_set_macaddr(sd, sd->mac);
	ssv6256_set_bssid(sd, zero_bssid);

	/* everything goes straight to the host: crypto is done in software */
	ssv6256_reg_write(sd, ADR_RX_FLOW_DATA, M_ENG_MACRX | (M_ENG_HWHCI << 4));
	ssv6256_reg_write(sd, ADR_RX_FLOW_MNG, M_ENG_MACRX | (M_ENG_HWHCI << 4));
	ssv6256_reg_write(sd, ADR_RX_FLOW_CTRL, M_ENG_MACRX | (M_ENG_HWHCI << 4));

	for (i = 0; i < DECI_TBL1_SIZE; i++)
		ssv6256_reg_write(sd, ADR_MRX_FLT_TB0 + i * 4, deci_tbl[i]);
	for (i = 0; i < DECI_TBL2_SIZE; i++)
		ssv6256_reg_write(sd, ADR_MRX_FLT_EN0 + i * 4,
				  deci_tbl[DECI_TBL1_SIZE + i]);

	ssv6256_reg_set_bits(sd, ADR_GLBLE_SET,
			     (OPMODE_STA << __ffs(OP_MODE)) | CCMP_H_SEL |
			 SEC_LUT_SEL, OP_MODE | CCMP_H_SEL | SEC_LUT_SEL);
	ssv6256_field_write(sd, ADR_MTX_RATERPT, MTX_RATERPT_HWID, M_ENG_HWHCI);
	/* MAC counters */
	ssv6256_reg_write(sd, 0xca000800, 0);
	ssv6256_reg_write(sd, 0xca000800, 0xffffffff);
	return 0;
}

/*
 * Full bring-up.  The radio comes first: the MAC takes its timing from
 * whatever clock the PLL settles on.  The firmware is loaded last, and
 * only then is the baseband allowed to receive.
 *
 * Everything starts from a platform reset, because the chip does not
 * necessarily come as it was left: another driver may have had it, or
 * a previous run may have stopped with the radio in the middle of
 * something.  Without it the calibrations are the first to fail.
 */
int ssv6256_hw_start(struct ssv6256_dev *sd)
{
	int ret;

	ret = ssv6256_reg_write(sd, ADR_BRG_SW_RST, PLF_SW_RST);
	if (ret)
		return ret;
	usleep_range(50, 100);

	ssv6256_phy_enable(sd, false);
	ret = ssv6256_phy_init(sd);
	if (ret)
		return ret;
	ret = ssv6256_mac_init(sd);
	if (ret)
		return ret;
	ret = ssv6256_load_firmware(sd);
	if (ret)
		return ret;
	dev_info(sd->dev, "firmware running\n");

	/*
	 * The firmware start sometimes clears the batch format.  Put it
	 * back without touching the receive engine, which is already
	 * running by now.
	 */
	if (sd->rx_aggr) {
		u32 mode;

		if (!ssv6256_reg_read(sd, ADR_HCI_TRX_MODE, &mode) &&
		    !(mode & HCI_RX_FORM_1)) {
			dev_info(sd->dev, "batch format restored after start\n");
			ssv6256_reg_set_bits(sd, ADR_HCI_TRX_MODE, HCI_RX_FORM_1,
					     HCI_RX_FORM_1);
		}
	}

	ssv6256_phy_enable(sd, true);
	/* the bus was slowed down for the firmware upload */
	ssv6256_set_bus_clock(sd, SSV_BUS_CLOCK_MAX);
	return ssv6256_set_channel(sd, sd->channel);
}

/* Read what the driver needs before registering with mac80211. */
void ssv6256_hw_probe(struct ssv6256_dev *sd)
{
	u32 id;

	/* the parts that also cover 5 GHz say so in their identity */
	sd->dual_band = !ssv6256_reg_read(sd, ADR_CHIP_ID_2, &id) &&
			id == DUAL_BAND_ID;
	ssv6256_read_efuse(sd);
	dev_info(sd->dev, "chip %s (%s band), MAC %pM\n", sd->chip_id,
		 sd->dual_band ? "dual" : "single", sd->mac);
}

int ssv6256_wsid_add(struct ssv6256_dev *sd, int wsid, const u8 *addr)
{
	int ret;

	ret = ssv6256_reg_write(sd, wsid_reg[wsid] + WSID_PEER_MAC0,
				get_unaligned_le32(addr));
	ret = ret ?: ssv6256_reg_write(sd, wsid_reg[wsid] + WSID_PEER_MAC1,
				   get_unaligned_le16(addr + 4));
	return ret ?: ssv6256_reg_write(sd, wsid_reg[wsid], 1);
}

void ssv6256_wsid_del(struct ssv6256_dev *sd, int wsid)
{
	ssv6256_reg_write(sd, wsid_reg[wsid], 0);
}

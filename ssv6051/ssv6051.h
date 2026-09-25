/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mac80211 driver for the South Silicon Valley SSV6051 SDIO 802.11b/g/n
 * chip (station and access point, software crypto).
 *
 * Hardware interface derived from the iComm vendor driver:
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 */
#ifndef SSV6051_H
#define SSV6051_H

#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/mmc/sdio_func.h>
#include <net/mac80211.h>

#include "../ssv6xxx.h"

#include "reg.h"

#define SSV_FIRMWARE		"ssv/ssv6051-sw.bin"

/* SDIO function 1 registers (CMD52) */
#define SDIO_REG_DATA_PORT0	0x00
#define SDIO_REG_DATA_PORT1	0x01
#define SDIO_REG_DATA_PORT2	0x02
#define SDIO_REG_INT_MASK	0x04
#define SDIO_REG_INT_STATUS	0x08
#define SDIO_REG_FN1_STATUS	0x0c
#define SDIO_REG_RX_LEN0	0x10
#define SDIO_REG_RX_LEN1	0x11
#define SDIO_REG_OUTPUT_TIMING	0x55
#define SDIO_REG_PMU_WAKEUP	0x67
#define SDIO_REG_REG_PORT0	0x70
#define SDIO_REG_REG_PORT1	0x71
#define SDIO_REG_REG_PORT2	0x72

#define SDIO_BLOCK_SIZE		128
#define SDIO_OUTPUT_TIMING	3
#define SDIO_CLOCK_INIT		25000000U
#define SSV_MAX_FRAME		4096
#define SSV_TX_BUF_SIZE		16384

/* INT_STATUS / INT_MASK bits */
#define SSV_INT_RX		BIT(0)
/* the chip raises this when the transmit side has room again */
#define SSV_INT_RESOURCE_LOW	BIT(7)

/* Frame/command types in the descriptor c_type field */
#define M0_RXEVENT		3
#define M2_TXREQ		2
#define HOST_CMD		5
#define HOST_EVENT		6

/* Packet engines (descriptor fCmd / RX flow registers) */
#define M_ENG_CPU		0x00
#define M_ENG_HWHCI		0x01
#define M_ENG_MACRX		0x04
#define M_ENG_TX_EDCA0		0x06
#define M_ENG_ENCRYPT_SEC	0x0B
#define M_ENG_TRASH_CAN		0x0F

#define TXPB_OFFSET		80
#define RXPB_OFFSET		80
#define TX_PKT_RSVD_SETTING	3
#define TX_ALLOC_RSVD		(TXPB_OFFSET + TX_PKT_RSVD_SETTING * 16)
#define RX_PINFO_PAD		4

/* Chip-side TX resources (pages of 256 bytes, frame IDs, per-queue frames) */
#define HW_PAGE_SHIFT		8
#define HW_TX_PAGES		115
#define HW_RX_PAGES		115
#define HW_TX_IDS		19
#define HW_RX_IDS		60
#define HW_TXQ_NUM		5
#define HW_TXQ_MGMT		4
#define TX_LOWTHRESHOLD_PAGE	(HW_TX_PAGES - HW_TX_PAGES / 2)
#define TX_LOWTHRESHOLD_ID	2

#define SSV_NUM_HW_STA		2	/* stations the MAC tracks in registers */
#define SSV_NUM_STA		8	/* the rest are watched by the firmware */
#define SSV_NUM_KEY_BUFS	8

#define CHIP_ID_6051Q_P1	0x00000000
#define CHIP_ID_6051Q_P2	0x70000000
#define CHIP_ID_6051Z		0x71000000
#define CHIP_ID_6051Q		0x73000000
#define CHIP_ID_6051P		0x75000000

enum ssv6051_xtal {
	SSV_XTAL_26M = 0,
	SSV_XTAL_40M,
	SSV_XTAL_24M,
};

enum ssv6051_cmd_id {
	SSV_CMD_PS = 2,
	SSV_CMD_INIT_CALI = 3,
	SSV_CMD_WATCHDOG_START = 6,
	SSV_CMD_WSID_OP = 8,
};

enum ssv6051_event {
	SSV_EVT_RC_MPDU_REPORT = 2,
	SSV_EVT_TXLOOPBK_RESULT = 10,
};

#define SSV_TXREPORT_RC		2	/* TXD_REPORT_TYPE asking for an RC report */

enum ssv6051_wsid_op {
	SSV_WSID_OP_ADD = 0,
	SSV_WSID_OP_DEL = 1,
	SSV_WSID_OP_PAIRWISE_SET_TYPE = 5,
	SSV_WSID_OP_GROUP_SET_TYPE = 6,
};

#define SSV_WSID_SEC_SW		0

#define SSV_OPMODE_STA		0
#define SSV_OPMODE_AP		1

/*
 * Wire formats: little-endian 32-bit words.  Field masks are named
 * <struct><word>_<field>.
 */
/* TX descriptor, TXPB_OFFSET bytes in front of every frame */
#define TXD0_LEN		GENMASK(15, 0)
#define TXD0_C_TYPE		GENMASK(18, 16)
#define TXD0_F80211		BIT(19)
#define TXD0_QOS		BIT(20)
#define TXD0_USE_4ADDR		BIT(22)
#define TXD0_REPORT_TYPE	GENMASK(25, 23)
#define TXD0_MORE_DATA		BIT(28)
#define TXD0_STYPE_B5B4		GENMASK(30, 29)
#define TXD2_HDR_OFFSET		GENMASK(7, 0)
#define TXD2_FRAG		BIT(8)
#define TXD2_UNICAST		BIT(9)
#define TXD2_HDR_LEN		GENMASK(15, 10)
#define TXD2_TX_REPORT		BIT(16)
#define TXD2_TX_BURST		BIT(17)
#define TXD2_ACK_POLICY		GENMASK(19, 18)
#define TXD2_RTS_CTS		GENMASK(25, 24)
#define TXD3_PAYLOAD_OFFSET	GENMASK(7, 0)
#define TXD3_WSID		GENMASK(22, 19)
#define TXD3_TXQ_IDX		GENMASK(25, 23)
#define TXD4_RTS_CTS_NAV	GENMASK(15, 0)
#define TXD4_CONSUME_TIME	GENMASK(25, 16)
#define TXD4_CRATE		GENMASK(31, 26)
#define TXD5_DRATE		GENMASK(5, 0)
#define TXD5_DL_LENGTH		GENMASK(17, 6)

#define SSV_TX_MAX_RATES	3

/* One step of the chip's retry chain; the driver leaves it zeroed */
struct ssv6051_rc_retry {
	__le32 w0;
	__le32 w1;
};

struct ssv6051_tx_desc {
	__le32 w0;
	__le32 fcmd;		/* packet engine route */
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 rsvd[8];
	struct ssv6051_rc_retry rc[SSV_TX_MAX_RATES];
};

/* RX descriptor and PHY info in front of every received frame */
#define RXD0_C_TYPE		GENMASK(18, 16)
#define RXD3_WSID		GENMASK(22, 19)
#define RXD3_RATE_IDX		GENMASK(31, 26)

struct ssv6051_rx_desc {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
};

#define RXPHY4_RPCI		GENMASK(7, 0)

struct ssv6051_rxphy_info {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
};

static_assert(sizeof(struct ssv6051_tx_desc) == 80);
static_assert(sizeof(struct ssv6051_rx_desc) + sizeof(struct ssv6051_rxphy_info) == 36);

#define SSV_TX_DESC_LEN		sizeof(struct ssv6051_tx_desc)
#define SSV_RX_DESC_LEN		(sizeof(struct ssv6051_rx_desc) + sizeof(struct ssv6051_rxphy_info))

/* Host command / firmware event header */
#define HDR0_LEN		GENMASK(15, 0)
#define HDR0_C_TYPE		GENMASK(18, 16)
#define HDR0_ID			GENMASK(31, 24)

struct ssv6051_host_hdr {
	__le32 w0;
	__le32 seq;
	u8 data[];
};

struct ssv6051_tx_rate_rpt {
	s8 data_rate;
	u8 count;
} __packed;

struct ssv6051_rc_report {
	u8 wsid;
	struct ssv6051_tx_rate_rpt rates[SSV_TX_MAX_RATES];
	__le16 frames;		/* sent since the last report */
	__le16 acked;
	__le32 ack_signal;
} __packed;

struct ssv6051_wsid_params {
	u8 cmd;
	u8 wsid_idx;
	u8 target_wsid[ETH_ALEN];
	u8 hw_security;
} __packed;

/* Calibration request, followed by the PHY and RF tables */
struct ssv6051_iqk_cfg {
	u8 xtal;
	u8 pa;
	u8 pabias_ctrl;
	u8 pacascode_ctrl;
	u8 tssi_trgt;
	u8 tssi_div;
	u8 tx_scale_11b;
	u8 tx_scale_11b_p0d5;
	u8 tx_scale_11g;
	u8 tx_scale_11g_p0d5;
	u8 rsvd[2];
	__le32 cmd_sel;
	__le32 fx_sel;
	__le32 phy_tbl_size;
	__le32 rf_tbl_size;
} __packed;

/*
 * Chip packet-buffer security table: 3 group keys of 48 bytes and 8
 * station entries of 52.  Crypto is done by mac80211; the MAC only needs
 * the (zeroed) table to exist.
 */
#define SSV_HW_SEC_SIZE		(3 * 48 + 8 * 52)

/* Rate table: index == chip rate index */
#define SSV_RATE_CCK_SHORT	4	/* 2/5.5/11 Mbps short preamble: 4..6 */
#define SSV_RATE_OFDM		7	/* 6..54 Mbps: 7..14 */
#define SSV_NUM_RATES		15

enum ssv6051_phy {
	SSV_PHY_CCK,
	SSV_PHY_OFDM,
};

struct ssv6051_rate {
	u32 kbps;
	u8 phy;
	u8 ctrl;	/* rate index used for ACK/CTS */
	u8 dot11;	/* sband bitrate index */
};

extern const struct ssv6051_rate ssv6051_rates[SSV_NUM_RATES];

/* Rate control state for one peer */
#define SSV_RC_MAX		12
struct ssv6051_rc {
	u8 rate[SSV_RC_MAX];	/* chip rate indices, ascending speed */
	u8 n;
	u8 cur;
	u32 prob[SSV_RC_MAX];	/* EWMA of ACKs per transmission, 0..1024 */
	bool sampled[SSV_RC_MAX];
	u8 win_idx;		/* rate index used by the current window */
	u8 win_left;
	u32 windows;
};

struct ssv6051_sta {
	int wsid;
	struct ssv6051_rc rc;
};

struct ssv6051_dev {
	struct ssv6xxx_common common;	/* must stay first */
	struct sdio_func *func;
	struct device *dev;
	struct ieee80211_hw *hw;
	struct ieee80211_supported_band band;

	/* board configuration */
	u32 xtal;
	bool ldo;
	u32 tx_gain_b;
	u32 tx_gain_gn;
	u32 chip_id;
	u8 mac[ETH_ALEN];

	/* SDIO */
	u32 bus_clock;		/* negotiated by the MMC core */
	u32 data_port;
	u32 reg_port;
	u8 *io_buf;		/* DMA-safe scratch, used under the SDIO host lock */

	/* chip state */
	u32 sec_buf;
	u32 pinfo_buf;
	u32 key_buf[SSV_NUM_KEY_BUFS];
	bool ch13_14;
	int channel;
	bool started;

	/* TX */
	struct sk_buff_head txq[HW_TXQ_NUM];
	struct task_struct *tx_thread;
	wait_queue_head_t tx_wait;
	u8 *tx_buf;
	int free_pages;
	int free_ids;
	int free_frames[HW_TXQ_NUM];
	bool res_valid;
	bool queues_stopped;
	bool res_irq;		/* waiting to hear that there is room */
	bool room_kick;		/* ... and the chip said so */

	/* calibration handshake */
	wait_queue_head_t cali_wait;
	int cali_state;

	/* association */
	struct mutex mutex;
	spinlock_t sta_lock;	/* rate control state */
	struct ieee80211_vif *vif;

	/* access point: beacon kept by the chip, group frames after DTIM */
	u32 bcn_buf[2];
	u16 bcn_len[2];
	u8 *bcn_last;
	size_t bcn_last_len;
	bool dtim_bit;		/* group frames wait in chip queue 4 */
	struct work_struct beacon_work;
	struct delayed_work dtim_work;
	struct ieee80211_sta __rcu *sta[SSV_NUM_STA];
	bool short_preamble;
	u32 cca_control;
	u32 cca_1;
};

/* mac.c */
struct ssv6051_dev *ssv6051_mac_alloc(struct device *dev);
void ssv6051_mac_free(struct ssv6051_dev *sd);
int ssv6051_mac_register(struct ssv6051_dev *sd);
void ssv6051_mac_unregister(struct ssv6051_dev *sd);

/* sdio.c */
int ssv6051_reg_read(struct ssv6051_dev *sd, u32 addr, u32 *val);
int ssv6051_reg_write(struct ssv6051_dev *sd, u32 addr, u32 val);
int ssv6051_reg_set_bits(struct ssv6051_dev *sd, u32 addr, u32 set, u32 mask);
int ssv6051_write_data(struct ssv6051_dev *sd, const u8 *buf, size_t len);
int ssv6051_irq_mask(struct ssv6051_dev *sd, u8 mask);
int ssv6051_irq_enable(struct ssv6051_dev *sd);
void ssv6051_irq_disable(struct ssv6051_dev *sd);
int ssv6051_load_firmware(struct ssv6051_dev *sd);
void ssv6051_set_bus_clock(struct ssv6051_dev *sd, u32 hz);

/* hw.c */
int ssv6051_hw_probe(struct ssv6051_dev *sd);
int ssv6051_hw_start(struct ssv6051_dev *sd);
void ssv6051_hw_stop(struct ssv6051_dev *sd);
int ssv6051_set_channel(struct ssv6051_dev *sd, int ch);
int ssv6051_send_cmd(struct ssv6051_dev *sd, u8 cmd, const void *data, size_t len);
int ssv6051_calibrate(struct ssv6051_dev *sd);
void ssv6051_set_bssid(struct ssv6051_dev *sd, const u8 *bssid);
void ssv6051_set_slot(struct ssv6051_dev *sd, bool short_slot);
void ssv6051_set_qos(struct ssv6051_dev *sd, bool qos);
int ssv6051_set_edca(struct ssv6051_dev *sd, u16 ac, bool qos,
		     const struct ieee80211_tx_queue_params *p);
/* ap.c */
void ssv6051_ap_init(struct ssv6051_dev *sd);
void ssv6051_ap_stop(struct ssv6051_dev *sd);
void ssv6051_ap_update_beacon(struct ssv6051_dev *sd);
void ssv6051_ap_group_queued(struct ssv6051_dev *sd);
bool ssv6051_is_ap(struct ssv6051_dev *sd);

void ssv6051_set_ap_mode(struct ssv6051_dev *sd, bool ap);
void ssv6051_beacon_enable(struct ssv6051_dev *sd, bool on);
void ssv6051_beacon_timing(struct ssv6051_dev *sd, u16 interval, u8 dtim_period);
int ssv6051_beacon_set(struct ssv6051_dev *sd, const u8 *buf, size_t len, u8 dtim_offset);
void ssv6051_beacon_release(struct ssv6051_dev *sd);
int ssv6051_wsid_add(struct ssv6051_dev *sd, int wsid, const u8 *addr);
void ssv6051_wsid_del(struct ssv6051_dev *sd, int wsid, const u8 *addr);
void ssv6051_update_ctrl_rates(struct ssv6051_dev *sd, u32 basic_rates);
void ssv6051_rf_enable(struct ssv6051_dev *sd, bool on);
void ssv6051_scan_cca(struct ssv6051_dev *sd, bool scanning);

/* tx.c */
void ssv6051_tx_room_wanted(struct ssv6051_dev *sd, bool on);
u32 ssv6051_legacy_airtime(const struct ssv6051_rate *r, u32 len, bool short_pre);
int ssv6051_tid_to_hwq(u8 tid);
bool ssv6051_tx_budget(struct ssv6051_dev *sd, int hwq, size_t len);
int ssv6051_tx_write(struct ssv6051_dev *sd, int hwq, size_t len);
int ssv6051_tx_init(struct ssv6051_dev *sd);
void ssv6051_tx_deinit(struct ssv6051_dev *sd);
void ssv6051_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb);
void ssv6051_tx_flush(struct ssv6051_dev *sd);

/* rx.c */
void ssv6051_rx_irq(struct ssv6051_dev *sd);

/* rc.c */
void ssv6051_rc_init(struct ssv6051_dev *sd, struct ieee80211_sta *sta);
u8 ssv6051_rc_get(struct ssv6051_dev *sd, struct ssv6051_sta *ss, bool *report);
void ssv6051_rc_report(struct ssv6051_dev *sd, const struct ssv6051_rc_report *rpt);

#endif

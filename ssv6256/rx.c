// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SSV6256 receive path, run from the SDIO interrupt.
 *
 * The same path carries three kinds of buffer, told apart by the c_type
 * of the first word: received frames, firmware events and the transmit
 * reports the chip returns for frames it has sent.
 */
#include <linux/unaligned.h>

#include "ssv6256.h"

#define RX_BUDGET	32

static int ssv6256_read_status(struct ssv6256_dev *sd, u8 *status)
{
	int ret;

	sdio_claim_host(sd->func);
	*status = sdio_readb(sd->func, SDIO_REG_INT_STATUS, &ret);
	sdio_release_host(sd->func);
	return ret;
}

/*
 * The length of the waiting buffer lives in two function-1 registers.
 * The chip does not accept CMD53 on them, so it takes two CMD52s.
 */
static struct sk_buff *ssv6256_read_frame(struct ssv6256_dev *sd)
{
	struct sdio_func *func = sd->func;
	struct sk_buff *skb = NULL;
	size_t aligned;
	u32 len;
	int ret;

	sdio_claim_host(func);
	len = sdio_readb(func, SDIO_REG_RX_LEN0, &ret);
	if (!ret)
		len |= sdio_readb(func, SDIO_REG_RX_LEN1, &ret) << 8;
	if (ret)
		goto out;
	aligned = sdio_align_size(func, len);
	if (len < sizeof(struct ssv6256_rx_desc) || aligned > SSV_RX_BUF_SIZE) {
		dev_err_ratelimited(sd->dev, "bogus RX length %u\n", len);
		goto out;
	}
	skb = dev_alloc_skb(aligned);
	if (!skb)
		goto out;
	ret = sdio_memcpy_fromio(func, skb->data, sd->data_port, aligned);
	if (ret) {
		dev_err_ratelimited(sd->dev, "RX read failed: %d\n", ret);
		dev_kfree_skb(skb);
		skb = NULL;
		goto out;
	}
	skb_put(skb, len);
out:
	sdio_release_host(func);
	return skb;
}

/* Turn the chip's rate byte into what mac80211 wants to hear. */
static void ssv6256_rx_rate(struct ieee80211_rx_status *rxs, u8 code)
{
	/* the 2.4 GHz band lists the four CCK rates before the OFDM ones */
	u8 ofdm_base = rxs->band == NL80211_BAND_2GHZ ? 4 : 0;

	switch (FIELD_GET(RATE_PHY_MODE, code)) {
	case RATE_PHY_HT:
		rxs->encoding = RX_ENC_HT;
		if (code & RATE_HT40)
			rxs->bw = RATE_INFO_BW_40;
		if (code & RATE_SHORT)
			rxs->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		rxs->rate_idx = FIELD_GET(RATE_INDEX, code);
		break;
	case RATE_PHY_OFDM:
		rxs->rate_idx = FIELD_GET(RATE_INDEX, code) + ofdm_base;
		break;
	default:
		if (code & RATE_SHORT)
			rxs->enc_flags |= RX_ENC_FLAG_SHORTPRE;
		rxs->rate_idx = FIELD_GET(RATE_INDEX, code) & 3;
		break;
	}
}

static void ssv6256_rx_frame(struct ssv6256_dev *sd, struct sk_buff *skb)
{
	struct ssv6256_rx_desc *rxd = (struct ssv6256_rx_desc *)skb->data;
	struct ssv6256_rxphy_info *phy = (struct ssv6256_rxphy_info *)(rxd + 1);
	struct ieee80211_rx_status *rxs = IEEE80211_SKB_RXCB(skb);
	u32 w0 = le32_to_cpu(phy->w0);
	u32 len = le32_get_bits(rxd->w0, RXD0_LEN);

	/* the SDIO read is padded to the block size; the descriptor is not */
	if (len > skb->len || len < SSV_RX_DESC_LEN + SSV_RX_PINFO_PAD + 10) {
		dev_kfree_skb(skb);
		return;
	}
	skb_trim(skb, len);

	memset(rxs, 0, sizeof(*rxs));
	rxs->band = sd->channel >= 36 ? NL80211_BAND_5GHZ : NL80211_BAND_2GHZ;
	ssv6256_rx_rate(rxs, FIELD_GET(RXPHY0_RATE, w0));
	rxs->freq = ieee80211_channel_to_frequency(sd->channel, rxs->band);
	rxs->signal = -(int)le32_get_bits(phy->w1, RXPHY1_RSSI);
	if (w0 & RXPHY0_AGGREGATE)
		rxs->flag |= RX_FLAG_NO_SIGNAL_VAL;

	skb_pull(skb, SSV_RX_DESC_LEN);
	skb_trim(skb, skb->len - SSV_RX_PINFO_PAD);

	ieee80211_rx_irqsafe(sd->hw, skb);
}

/*
 * The chip can claim to have a frame waiting and then hand over
 * nothing.  Reading the status does not clear that, so the interrupt
 * line stays low and the host spins on it.  After a few rounds of this
 * the interrupt is masked for a while: the driver falls behind, but the
 * machine stays usable, which is not the case with the storm.
 */
#define RX_EMPTY_LIMIT		32
#define RX_EMPTY_BACKOFF	msecs_to_jiffies(100)

static void ssv6256_rx_unmask_work(struct work_struct *work)
{
	struct ssv6256_dev *sd = container_of(to_delayed_work(work), struct ssv6256_dev,
					  rx_unmask_work);

	sd->rx_empty = 0;
	if (sd->started)
		ssv6256_irq_mask(sd, (u8)~SSV_INT_RX);
}

void ssv6256_rx_init(struct ssv6256_dev *sd)
{
	INIT_DELAYED_WORK(&sd->rx_unmask_work, ssv6256_rx_unmask_work);
}

void ssv6256_rx_irq(struct ssv6256_dev *sd)
{
	u8 status;
	int n = 0;

	while (n < RX_BUDGET) {
		struct sk_buff *skb;
		struct ssv6256_rx_desc *rxd;

		if (ssv6256_read_status(sd, &status) || !(status & SSV_INT_RX))
			break;
		skb = ssv6256_read_frame(sd);
		if (!skb) {
			if (++sd->rx_empty < RX_EMPTY_LIMIT)
				break;
			dev_err_ratelimited(sd->dev,
					    "interrupt with nothing to read, backing off\n");
			ssv6256_irq_mask(sd, 0xff);
			schedule_delayed_work(&sd->rx_unmask_work,
					      RX_EMPTY_BACKOFF);
			break;
		}
		sd->rx_empty = 0;
		n++;
		rxd = (struct ssv6256_rx_desc *)skb->data;
		switch (le32_get_bits(rxd->w0, RXD0_C_TYPE)) {
		case SSV_CTYPE_RATE_RPT:
			ssv6256_tx_status(sd, skb);
			dev_kfree_skb(skb);
			break;
		case SSV_CTYPE_HOST_EVENT:
			/* watchdog ticks and firmware logs */
			dev_kfree_skb(skb);
			break;
		default:
			ssv6256_rx_frame(sd, skb);
			break;
		}
	}
}

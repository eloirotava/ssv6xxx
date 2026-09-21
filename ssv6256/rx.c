// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SSV6256 receive path, run from the SDIO interrupt.
 *
 * The same path carries three kinds of buffer, told apart by the c_type
 * of the first word: received frames, firmware events and the transmit
 * reports the chip returns for frames it has sent.
 */
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/list.h>
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

/*
 * Header the chip puts in front of every frame of a batch: the first
 * half says how far the next one starts, the second how much is waiting
 * in total.  The rest carries counters the driver does not use.
 */
struct ssv6256_rx_aggr_hdr {
	__le16 jmp_len;
	__le16 accu_len;
	__le32 w1;
	__le32 w2;
} __packed;

/*
 * Read a whole batch into sd->rx_buf and return its length.  The first
 * word at the data port announces how much is waiting; reading it does
 * not consume it, so the batch that follows still starts at its header.
 */
/* Take a free batch buffer, or NULL if the thread is behind. */
static struct ssv6256_batch *ssv6256_batch_get(struct ssv6256_dev *sd, struct list_head *from)
{
	struct ssv6256_batch *b = NULL;
	unsigned long flags;

	spin_lock_irqsave(&sd->rx_lock, flags);
	if (!list_empty(from)) {
		b = list_first_entry(from, struct ssv6256_batch, node);
		list_del(&b->node);
	}
	spin_unlock_irqrestore(&sd->rx_lock, flags);
	return b;
}

static void ssv6256_batch_put(struct ssv6256_dev *sd, struct list_head *to,
			  struct ssv6256_batch *b)
{
	unsigned long flags;

	spin_lock_irqsave(&sd->rx_lock, flags);
	list_add_tail(&b->node, to);
	spin_unlock_irqrestore(&sd->rx_lock, flags);
}

static struct ssv6256_batch *ssv6256_read_batch(struct ssv6256_dev *sd)
{
	struct sdio_func *func = sd->func;
	struct ssv6256_batch *b;
	size_t total, aligned;
	int ret;

	b = ssv6256_batch_get(sd, &sd->rx_free);
	if (!b)
		return ERR_PTR(-EBUSY);
	b->len = 0;

	sdio_claim_host(func);
	ret = sdio_memcpy_fromio(func, sd->rx_head, sd->data_port, sizeof(u32));
	if (ret)
		goto out;
	total = le32_to_cpup((__le32 *)sd->rx_head) >> 16;
	if (!total)
		goto out;
	aligned = sdio_align_size(func, total);
	if (total < sizeof(struct ssv6256_rx_aggr_hdr) ||
	    aligned > SSV_RX_BATCH_MAX) {
		dev_err_ratelimited(sd->dev, "bogus batch length %zu\n", total);
		/*
		 * The chip says it has something and hands over nothing but
		 * ones.  A few of those in a row mean the receive engine and
		 * the host disagree about where a batch starts; setting the
		 * format up again puts them back in step.
		 */
		if (++sd->rx_bogus == 4)
			schedule_work(&sd->rx_resync_work);
		goto out;
	}
	sd->rx_bogus = 0;
	ret = sdio_memcpy_fromio(func, b->buf, sd->data_port, aligned);
	if (ret) {
		dev_err_ratelimited(sd->dev, "batch read failed: %d\n", ret);
		goto out;
	}
	b->len = total;
out:
	sdio_release_host(func);
	if (!b->len) {
		ssv6256_batch_put(sd, &sd->rx_free, b);
		return NULL;
	}
	return b;
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

static void ssv6256_rx_resync_work(struct work_struct *work)
{
	struct ssv6256_dev *sd = container_of(work, struct ssv6256_dev, rx_resync_work);

	if (!sd->started)
		return;
	dev_info(sd->dev, "receive out of step, setting the format again\n");
	ssv6256_rx_aggr_init(sd);
	sd->rx_bogus = 0;
}

static void ssv6256_rx_unmask_work(struct work_struct *work)
{
	struct ssv6256_dev *sd = container_of(to_delayed_work(work), struct ssv6256_dev,
					  rx_unmask_work);

	sd->rx_empty = 0;
	if (sd->started)
		ssv6256_irq_mask(sd, (u8)~SSV_INT_RX);
}

/* Hand one buffer to whoever deals with its kind. */
static void ssv6256_rx_dispatch(struct ssv6256_dev *sd, struct sk_buff *skb)
{
	struct ssv6256_rx_desc *rxd = (struct ssv6256_rx_desc *)skb->data;

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

/*
 * Walk a batch, copying each frame into its own buffer.  The jump of
 * the last one may reach past what was read, or be zero: what is left
 * is that frame.  Returns how many frames came out of it.
 */
static int ssv6256_rx_split(struct ssv6256_dev *sd, const u8 *batch, size_t len)
{
	size_t off = 0, hdr = sizeof(struct ssv6256_rx_aggr_hdr);
	int n = 0;

	while (off + hdr < len) {
		const struct ssv6256_rx_aggr_hdr *h;
		struct sk_buff *skb;
		size_t jmp, flen;
		u64 a = ktime_get_ns(), b;

		h = (const struct ssv6256_rx_aggr_hdr *)(batch + off);
		jmp = le16_to_cpu(h->jmp_len);
		if (jmp <= hdr || off + jmp > len)
			jmp = len - off;	/* the last one */
		flen = jmp - hdr;
		if (flen < SSV_RX_DESC_LEN)
			break;

		skb = __dev_alloc_skb(flen, GFP_KERNEL);
		if (!skb)
			break;
		skb_put_data(skb, batch + off + hdr, flen);
		b = ktime_get_ns();
		ssv6256_rx_dispatch(sd, skb);
		sd->dbg_copy += b - a;
		sd->dbg_disp += ktime_get_ns() - b;
		sd->dbg_frames++;
		off += jmp;
		n++;
	}
	return n;
}

/*
 * Splitting a batch means a buffer per frame, and asking for memory
 * from the interrupt is what that costs the most: measured at 85 us a
 * frame, against 16 us to hand it to mac80211.  So the interrupt only
 * reads and queues, and this thread does the rest, where the request
 * may wait for memory instead of failing fast.
 */
static int ssv6256_rx_thread(void *data)
{
	struct ssv6256_dev *sd = data;

	while (!kthread_should_stop()) {
		struct ssv6256_batch *b = ssv6256_batch_get(sd, &sd->rx_ready);

		if (!b) {
			wait_event_interruptible(sd->rx_wait,
						 !list_empty(&sd->rx_ready) ||
						 kthread_should_stop());
			continue;
		}
		ssv6256_rx_split(sd, b->buf, b->len);
		ssv6256_batch_put(sd, &sd->rx_free, b);
	}
	return 0;
}

int ssv6256_rx_init(struct ssv6256_dev *sd)
{
	int i;

	INIT_DELAYED_WORK(&sd->rx_unmask_work, ssv6256_rx_unmask_work);
	INIT_WORK(&sd->rx_resync_work, ssv6256_rx_resync_work);

	INIT_LIST_HEAD(&sd->rx_free);
	INIT_LIST_HEAD(&sd->rx_ready);
	spin_lock_init(&sd->rx_lock);
	init_waitqueue_head(&sd->rx_wait);
	sd->rx_head = devm_kzalloc(sd->dev, SSV_RX_HEAD_SIZE, GFP_KERNEL);
	if (!sd->rx_head)
		return -ENOMEM;
	for (i = 0; i < SSV_RX_BATCHES; i++) {
		sd->rx_batch[i].buf = devm_kmalloc(sd->dev, SSV_RX_BATCH_MAX,
						   GFP_KERNEL);
		if (!sd->rx_batch[i].buf)
			return -ENOMEM;
		list_add_tail(&sd->rx_batch[i].node, &sd->rx_free);
	}
	sd->rx_thread = kthread_run(ssv6256_rx_thread, sd, "ssv6256-rx");
	if (IS_ERR(sd->rx_thread)) {
		sd->rx_thread = NULL;
		return -ENOMEM;
	}
	return 0;
}

void ssv6256_rx_deinit(struct ssv6256_dev *sd)
{
	if (sd->rx_thread)
		kthread_stop(sd->rx_thread);
	sd->rx_thread = NULL;
}

void ssv6256_rx_irq(struct ssv6256_dev *sd)
{
	u8 status;
	int n = 0;

	while (n < RX_BUDGET) {
		struct sk_buff *skb;
		size_t len;

		if (ssv6256_read_status(sd, &status) || !(status & SSV_INT_RX))
			break;

		if (sd->rx_aggr) {
			u64 t0 = ktime_get_ns(), t1;
			struct ssv6256_batch *batch = ssv6256_read_batch(sd);

			if (IS_ERR(batch)) {
				/*
				 * Every buffer is with the receive thread.
				 * Leave the loop and let it catch up; the chip
				 * keeps the frames and asks again.
				 */
				sd->rx_dropped++;
				wake_up(&sd->rx_wait);
				break;
			}
			t1 = ktime_get_ns();
			len = batch ? batch->len : 0;
			if (len) {
				/*
				 * The batch counts towards the budget on its
				 * own: the chip may keep offering the same
				 * bytes, and a loop that only counted frames
				 * would never leave.
				 */
				n++;
				ssv6256_batch_put(sd, &sd->rx_ready, batch);
				wake_up(&sd->rx_wait);
			}
			sd->dbg_read += t1 - t0;
			sd->dbg_proc += ktime_get_ns() - t1;
			sd->dbg_bytes += len;
			if (++sd->dbg_batches == 512) {
				dev_info(sd->dev,
					 "DBG 512 lotes, %u quadros: ler %llu us, copiar %llu us, entregar %llu us, %u bytes\n",
					 sd->dbg_frames, div_u64(sd->dbg_read, 1000),
					 div_u64(sd->dbg_copy, 1000),
					 div_u64(sd->dbg_disp, 1000), sd->dbg_bytes);
				sd->dbg_read = 0;
				sd->dbg_proc = 0;
				sd->dbg_copy = 0;
				sd->dbg_disp = 0;
				sd->dbg_bytes = 0;
				sd->dbg_batches = 0;
				sd->dbg_frames = 0;
			}
			skb = NULL;
		} else {
			skb = ssv6256_read_frame(sd);
			len = skb ? skb->len : 0;
		}

		if (!len) {
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
		if (skb) {
			n++;
			ssv6256_rx_dispatch(sd, skb);
		}
	}
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SSV6256 mac80211 glue: capabilities and callbacks.
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>

#include "ssv6256.h"

#define CHAN(_band, _ch, _freq) { .band = (_band), .center_freq = (_freq), \
				  .hw_value = (_ch), .max_power = 20 }
#define CHAN2(_ch, _freq)	CHAN(NL80211_BAND_2GHZ, _ch, _freq)
#define CHAN5(_ch, _freq)	CHAN(NL80211_BAND_5GHZ, _ch, _freq)

static struct ieee80211_channel ssv6256_channels[] = {
	CHAN2(1, 2412), CHAN2(2, 2417), CHAN2(3, 2422), CHAN2(4, 2427),
	CHAN2(5, 2432), CHAN2(6, 2437), CHAN2(7, 2442), CHAN2(8, 2447),
	CHAN2(9, 2452), CHAN2(10, 2457), CHAN2(11, 2462), CHAN2(12, 2467),
	CHAN2(13, 2472), CHAN2(14, 2484),
};

static struct ieee80211_channel ssv6256_channels_5g[] = {
	CHAN5(36, 5180), CHAN5(40, 5200), CHAN5(44, 5220), CHAN5(48, 5240),
	CHAN5(52, 5260), CHAN5(56, 5280), CHAN5(60, 5300), CHAN5(64, 5320),
	CHAN5(100, 5500), CHAN5(104, 5520), CHAN5(108, 5540), CHAN5(112, 5560),
	CHAN5(116, 5580), CHAN5(120, 5600), CHAN5(124, 5620), CHAN5(128, 5640),
	CHAN5(132, 5660), CHAN5(136, 5680), CHAN5(140, 5700), CHAN5(144, 5720),
	CHAN5(149, 5745), CHAN5(153, 5765), CHAN5(157, 5785), CHAN5(161, 5805), CHAN5(165, 5825),
};

/*
 * The chip's rate byte indexes CCK and OFDM rates separately, so the
 * hardware value is just the position in this table (see tx.c).
 */
static struct ieee80211_rate ssv6256_bitrates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 },
	{ .bitrate = 90 },
	{ .bitrate = 120 },
	{ .bitrate = 180 },
	{ .bitrate = 240 },
	{ .bitrate = 360 },
	{ .bitrate = 480 },
	{ .bitrate = 540 },
};

static int ssv6256_start(struct ieee80211_hw *hw)
{
	struct ssv6256_dev *sd = hw->priv;
	int ret;

	mutex_lock(&sd->mutex);
	ret = ssv6256_hw_start(sd);
	if (!ret)
		ret = ssv6256_irq_enable(sd);
	if (!ret)
		sd->started = true;
	mutex_unlock(&sd->mutex);
	if (ret)
		dev_err(sd->dev, "start failed: %d\n", ret);
	return ret;
}

static void ssv6256_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct ssv6256_dev *sd = hw->priv;

	cancel_delayed_work_sync(&sd->rx_unmask_work);
	mutex_lock(&sd->mutex);
	sd->started = false;
	ssv6256_irq_disable(sd);
	ssv6256_phy_enable(sd, false);
	ssv6256_tx_flush(sd);
	mutex_unlock(&sd->mutex);
	/* the resync work checks started under the mutex: a no-op by now */
	cancel_work_sync(&sd->rx_resync_work);
}

static int ssv6256_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ssv6256_dev *sd = hw->priv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;
	if (sd->vif)
		return -EBUSY;

	mutex_lock(&sd->mutex);
	sd->vif = vif;
	if (vif->type == NL80211_IFTYPE_AP) {
		ssv6256_set_ap_mode(sd, true);
		ssv6256_set_bssid(sd, vif->addr);
	}
	mutex_unlock(&sd->mutex);
	return 0;
}

static void ssv6256_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct ssv6256_dev *sd = hw->priv;

	if (sd->vif != vif)
		return;
	cancel_delayed_work_sync(&sd->dtim_work);
	cancel_work_sync(&sd->beacon_work);
	mutex_lock(&sd->mutex);
	if (vif->type == NL80211_IFTYPE_AP)
		ssv6256_ap_stop(sd);
	sd->vif = NULL;
	mutex_unlock(&sd->mutex);
}

static int ssv6256_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	struct ssv6256_dev *sd = hw->priv;
	struct ieee80211_channel *chan = hw->conf.chandef.chan;
	int ret = 0;

	if (!(changed & IEEE80211_CONF_CHANGE_CHANNEL) || !chan)
		return 0;

	mutex_lock(&sd->mutex);
	if (chan->hw_value != sd->channel) {
		ret = ssv6256_set_channel(sd, chan->hw_value);
		if (!ret)
			sd->channel = chan->hw_value;
	}
	mutex_unlock(&sd->mutex);
	return ret;
}

#define SSV_FILTERS (FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC | FIF_PSPOLL)

static void ssv6256_configure_filter(struct ieee80211_hw *hw, unsigned int changed,
				     unsigned int *total, u64 multicast)
{
	*total &= SSV_FILTERS;
}

static void ssv6256_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *info, u64 changed)
{
	struct ssv6256_dev *sd = hw->priv;

	mutex_lock(&sd->mutex);
	if (changed & BSS_CHANGED_ERP_PREAMBLE)
		sd->short_preamble = info->use_short_preamble;
	if (changed & BSS_CHANGED_BSSID)
		ssv6256_set_bssid(sd, info->bssid);
	if (vif->type == NL80211_IFTYPE_AP) {
		if (changed & (BSS_CHANGED_BEACON | BSS_CHANGED_BEACON_INT |
			       BSS_CHANGED_BEACON_ENABLED))
			ssv6256_ap_update_beacon(sd);
		if (changed & BSS_CHANGED_BEACON_ENABLED)
			ssv6256_beacon_enable(sd, info->enable_beacon);
	}
	mutex_unlock(&sd->mutex);
}

/* A station's power save buffer changed: the beacon TIM follows. */
static int ssv6256_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			   bool set)
{
	struct ssv6256_dev *sd = hw->priv;

	schedule_work(&sd->beacon_work);
	return 0;
}

/* Channel access parameters of one access category. */
static int ssv6256_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   unsigned int link_id, u16 ac,
		       const struct ieee80211_tx_queue_params *params)
{
	struct ssv6256_dev *sd = hw->priv;
	int hwq = ssv6256_ac_to_hwq(ac);
	u32 val;
	int ret;

	val = FIELD_PREP(TXQ_AIFSN, params->aifs) |
	      FIELD_PREP(TXQ_ECWMIN, ilog2(params->cw_min + 1)) |
	      FIELD_PREP(TXQ_ECWMAX, ilog2(params->cw_max + 1)) |
	      FIELD_PREP(TXQ_TXOP_LIMIT, params->txop);

	mutex_lock(&sd->mutex);
	ssv6256_field_write(sd, ADR_GLBLE_SET, QOS_EN, vif->bss_conf.qos);
	ret = ssv6256_reg_write(sd, ADR_TXQ0_MTX_Q_AIFSN + hwq * TXQ_STRIDE, val);
	mutex_unlock(&sd->mutex);
	return ret;
}

static int ssv6256_sta_add(struct ssv6256_dev *sd, struct ieee80211_sta *sta)
{
	struct ssv6256_sta *ss = (struct ssv6256_sta *)sta->drv_priv;
	int wsid;

	for (wsid = 0; wsid < SSV_NUM_STA; wsid++)
		if (!rcu_access_pointer(sd->sta[wsid]))
			break;
	if (wsid == SSV_NUM_STA)
		return -ENOSPC;

	ss->wsid = wsid;
	rcu_assign_pointer(sd->sta[wsid], sta);
	return ssv6256_wsid_add(sd, wsid, sta->addr);
}

static void ssv6256_sta_del(struct ssv6256_dev *sd, struct ieee80211_sta *sta)
{
	struct ssv6256_sta *ss = (struct ssv6256_sta *)sta->drv_priv;

	if (ss->wsid < 0 || ss->wsid >= SSV_NUM_STA)
		return;
	ssv6256_wsid_del(sd, ss->wsid);
	RCU_INIT_POINTER(sd->sta[ss->wsid], NULL);
	ss->wsid = -1;
	synchronize_rcu();
}

static int ssv6256_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta, enum ieee80211_sta_state old,
			 enum ieee80211_sta_state new)
{
	struct ssv6256_dev *sd = hw->priv;
	int ret = 0;

	mutex_lock(&sd->mutex);
	if (old == IEEE80211_STA_NOTEXIST && new == IEEE80211_STA_NONE)
		ret = ssv6256_sta_add(sd, sta);
	else if (old == IEEE80211_STA_NONE && new == IEEE80211_STA_NOTEXIST)
		ssv6256_sta_del(sd, sta);
	mutex_unlock(&sd->mutex);
	return ret;
}

/* Wait for what is queued to reach the chip, before a channel change. */
static void ssv6256_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  u32 queues, bool drop)
{
	struct ssv6256_dev *sd = hw->priv;
	int i;

	for (i = 0; i < 50 && ssv6256_tx_queued(sd); i++) {
		ssv6256_tx_kick(sd);
		usleep_range(1000, 2000);
	}
	if (drop)
		ssv6256_tx_flush(sd);
}

static const struct ieee80211_ops ssv6256_ops = {
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,
	.tx = ssv6256_tx,
	.start = ssv6256_start,
	.stop = ssv6256_stop,
	.add_interface = ssv6256_add_interface,
	.remove_interface = ssv6256_remove_interface,
	.config = ssv6256_config,
	.configure_filter = ssv6256_configure_filter,
	.bss_info_changed = ssv6256_bss_info_changed,
	.sta_state = ssv6256_sta_state,
	.set_tim = ssv6256_set_tim,
	.conf_tx = ssv6256_conf_tx,
	.flush = ssv6256_flush,
};

struct ssv6256_dev *ssv6256_mac_alloc(struct device *dev)
{
	struct ieee80211_hw *hw;
	struct ssv6256_dev *sd;

	hw = ieee80211_alloc_hw(sizeof(*sd), &ssv6256_ops);
	if (!hw)
		return NULL;
	sd = hw->priv;
	sd->hw = hw;
	sd->dev = dev;
	sd->channel = 1;
	mutex_init(&sd->mutex);
	ssv6256_ap_init(sd);
	if (ssv6256_rx_init(sd)) {
		ieee80211_free_hw(hw);
		return NULL;
	}
	SET_IEEE80211_DEV(hw, dev);
	return sd;
}

void ssv6256_mac_free(struct ssv6256_dev *sd)
{
	ssv6256_rx_deinit(sd);
	ieee80211_free_hw(sd->hw);
}

int ssv6256_mac_register(struct ssv6256_dev *sd)
{
	struct ieee80211_hw *hw = sd->hw;
	int ret;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	hw->queues = IEEE80211_NUM_ACS;
	hw->extra_tx_headroom = SSV_TX_DESC_LEN;
	hw->max_rates = SSV_TX_MAX_RATES;
	hw->max_rate_tries = 15;
	hw->sta_data_size = sizeof(struct ssv6256_sta);
	hw->vif_data_size = 0;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_AP);
	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	sd->band.band = NL80211_BAND_2GHZ;
	sd->band.channels = ssv6256_channels;
	sd->band.n_channels = ARRAY_SIZE(ssv6256_channels);
	sd->band.bitrates = ssv6256_bitrates;
	sd->band.n_bitrates = ARRAY_SIZE(ssv6256_bitrates);

	/* the 5 GHz band has no CCK rates: it starts at 6 Mbit/s */
	if (sd->dual_band) {
		sd->band5.band = NL80211_BAND_5GHZ;
		sd->band5.channels = ssv6256_channels_5g;
		sd->band5.n_channels = ARRAY_SIZE(ssv6256_channels_5g);
		sd->band5.bitrates = &ssv6256_bitrates[4];
		sd->band5.n_bitrates = ARRAY_SIZE(ssv6256_bitrates) - 4;
	}

	hw->wiphy->bands[NL80211_BAND_2GHZ] = &sd->band;
	if (sd->dual_band)
		hw->wiphy->bands[NL80211_BAND_5GHZ] = &sd->band5;

	SET_IEEE80211_PERM_ADDR(hw, sd->mac);

	ret = ssv6256_tx_init(sd);
	if (ret)
		return ret;
	ret = ieee80211_register_hw(hw);
	if (ret) {
		ssv6256_tx_deinit(sd);
		return ret;
	}
	wiphy_info(hw->wiphy, "SSV6256 ready\n");
	return 0;
}

void ssv6256_mac_unregister(struct ssv6256_dev *sd)
{
	ieee80211_unregister_hw(sd->hw);
	ssv6256_tx_deinit(sd);
}

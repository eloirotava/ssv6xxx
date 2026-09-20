// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PLL, RF and baseband bring-up for the Turismo C front end.
 *
 * Unlike the SSV6051, whose firmware calibrates the radio, this chip
 * expects the host to run the calibrations: receive DC offset, receive
 * filter tuning for both bandwidths, transmit LO leakage and the TX/RX
 * IQ imbalance.  The sequences and magic values come from the vendor
 * driver (ssv6006_turismoC.c).
 *
 * The register tables come from turismoC_rf_reg.c and
 * turismoC_wifi_phy_reg.c of the same driver.
 *
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 */
#include <linux/delay.h>
#include <linux/ieee80211.h>

#include "ssv6256.h"

/* Turismo C RF front end, table version 20.00. */
static const struct ssv6256_reg rf_table[] = {
	{ 0xccb0a420, 0x0033e73f },
	{ 0xccb0a554, 0x03024444 },
	{ 0xccb0a594, 0x111e0950 },
	{ 0xccb0a598, 0x0f1e00ff },
	{ 0xccb0a530, 0x001f1f01 },
	{ 0xccb0a604, 0x001f1f01 },
	{ 0xccb0a62c, 0x9264924a },
	{ 0xccb0a630, 0x96dbb6cc },
	{ 0xccb0a634, 0x00000000 },
	{ 0xccb0a8cc, 0x141e157c },
	{ 0xccb0a8d0, 0x00001644 },
	{ 0xccb0a88c, 0x00000010 },
	{ 0xccb0a808, 0x88000000 },
	{ 0xccb0b000, 0x24844214 },
};

/* Turismo C baseband, table version 20.00. */
static const struct ssv6256_reg phy_table[] = {
	{ 0xccb0e010, 0x00000fff },
	{ 0xccb0e014, 0x00807f03 },
	{ 0xccb0e018, 0x0055003c },
	{ 0xccb0e01c, 0x00000064 },
	{ 0xccb0e020, 0x00000000 },
	{ 0xccb0e02c, 0x7004606c },
	{ 0xccb0e030, 0x7004606c },
	{ 0xccb0e034, 0x1a040400 },
	{ 0xccb0e038, 0x630f36d0 },
	{ 0xccb0e03c, 0x100c0003 },
	{ 0xccb0e040, 0x11600800 },
	{ 0xccb0e044, 0x00080868 },
	{ 0xccb0e048, 0xff001160 },
	{ 0xccb0e04c, 0x00100040 },
	{ 0xccb0e060, 0x11501150 },
	{ 0xccb0e12c, 0x00001160 },
	{ 0xccb0e130, 0x00100040 },
	{ 0xccb0e134, 0x00080010 },
	{ 0xccb0e180, 0x00010060 },
	{ 0xccb0e184, 0xb5a19080 },
	{ 0xccb0e188, 0xb5a19080 },
	{ 0xccb0e18c, 0xb5a19080 },
	{ 0xccb0e190, 0x00010006 },
	{ 0xccb0e194, 0x06060606 },
	{ 0xccb0e198, 0x06060606 },
	{ 0xccb0e19c, 0x06060606 },
	{ 0xccb0e080, 0x0110000f },
	{ 0xccb0e098, 0x00102000 },
	{ 0xccb0e09c, 0x00100018 },
	{ 0xccb0e4b4, 0x00002001 },
	{ 0xccb0eca4, 0x00009001 },
	{ 0xccb0ecb8, 0x000c50cc },
	{ 0xccb0fc44, 0x00028080 },
	{ 0xccb0f008, 0x00004775 },
	{ 0xccb0f00c, 0x10000075 },
	{ 0xccb0f010, 0x3f304905 },
	{ 0xccb0f014, 0x40182000 },
	{ 0xccb0f018, 0x20600000 },
	{ 0xccb0f01c, 0x0c010080 },
	{ 0xccb0f03c, 0x0000005a },
	{ 0xccb0f020, 0x20202020 },
	{ 0xccb0f024, 0x20000000 },
	{ 0xccb0f028, 0x50505050 },
	{ 0xccb0f02c, 0x20202020 },
	{ 0xccb0f030, 0x20000000 },
	{ 0xccb0f034, 0x00002424 },
	{ 0xccb0f09c, 0x000030a0 },
	{ 0xccb0f0c0, 0x0f0003c0 },
	{ 0xccb0f0c4, 0x30023003 },
	{ 0xccb0f0cc, 0x00000120 },
	{ 0xccb0f0d0, 0x00000020 },
	{ 0xccb0f130, 0x40000000 },
	{ 0xccb0f164, 0x000e0090 },
	{ 0xccb0f188, 0x82000000 },
	{ 0xccb0f190, 0x00000020 },
	{ 0xccb0f194, 0x09360001 },
	{ 0xccb0f3f8, 0x00100001 },
	{ 0xccb0f3fc, 0x00010425 },
	{ 0xccb0e804, 0x00020000 },
	{ 0xccb0e808, 0x20280060 },
	{ 0xccb0e80c, 0x00003467 },
	{ 0xccb0e810, 0x00430000 },
	{ 0xccb0e814, 0x30000015 },
	{ 0xccb0e818, 0x00390005 },
	{ 0xccb0e81c, 0x05050005 },
	{ 0xccb0e820, 0x00570057 },
	{ 0xccb0e824, 0x00570057 },
	{ 0xccb0e828, 0x00236700 },
	{ 0xccb0e82c, 0x000d1746 },
	{ 0xccb0e830, 0x05051787 },
	{ 0xccb0e834, 0x07800000 },
	{ 0xccb0e89c, 0x009000b0 },
	{ 0xccb0e8a0, 0x00000000 },
	{ 0xccb0ebf8, 0x00100000 },
	{ 0xccb0ebfc, 0x00000001 },
};

/*
 * Charge pump current per crystal, indexed by the RG_*_XTAL_FREQ code.
 * Only used by the single band variant of the chip.
 */
static const u8 xtal_cp_isel[] = {
	0x8, 0x5, 0x5, 0x7, 0xb, 0x7, 0x5, 0x8,
};

/*
 * Every calibration reports completion through one bit of the same
 * status register.  They take well under a millisecond; the generous
 * bound only guards against a radio that never answers.
 */
static int ssv6256_cal_wait(struct ssv6256_dev *sd, u32 done, const char *what)
{
	u32 val;
	int i;

	for (i = 0; i < 1000; i++) {
		if (ssv6256_reg_read(sd, ADR_RF_D_CAL_TOP_1, &val))
			return -EIO;
		if (val & done)
			return 0;
		usleep_range(50, 100);
	}
	dev_err(sd->dev, "%s calibration did not finish\n", what);
	return -ETIMEDOUT;
}

/* Park the synthesiser on channel 6, where the calibrations are run. */
static void ssv6256_cal_channel(struct ssv6256_dev *sd)
{
	ssv6256_reg_set_bits(sd, ADR_SX_CH_TABLE,
			 (6 << __ffs(RG_SX_CHANNEL)) | RG_SX_RFCH_MAP_EN,
			 RG_SX_CHANNEL | RG_SX_RFCH_MAP_EN);
}

static void ssv6256_cal_start(struct ssv6256_dev *sd)
{
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE, MODE_STANDBY);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE_MANUAL, 1);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE, MODE_CALIBRATION);
}

/* The radio has to return to standby between two calibrations. */
static void ssv6256_cal_next(struct ssv6256_dev *sd)
{
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE, MODE_STANDBY);
	usleep_range(100, 200);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE, MODE_CALIBRATION);
}

static void ssv6256_cal_end(struct ssv6256_dev *sd)
{
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE, MODE_STANDBY);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE_MANUAL, 0);
}

/* Receive DC offset: 21 IDAC registers the hardware fills in itself. */
static int ssv6256_cal_rxdc(struct ssv6256_dev *sd)
{
	int ret;

	ssv6256_cal_channel(sd);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
			CAL_IDX_WIFI2P4G_RXDC);
	usleep_range(100, 200);

	ret = ssv6256_cal_wait(sd, RO_WF_DCCAL_DONE, "RX DC");
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	return ret;
}

/* Receive filter (RC) tuning, once per bandwidth. */
static int ssv6256_cal_rxrc(struct ssv6256_dev *sd, bool ht40)
{
	int ret;

	if (ht40) {
		ssv6256_field_write(sd, ADR_CALIBRATION_TIMER_REGISTER,
				RG_RX_N_RCCAL_DELAY, 2);
		ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_4, RG_PHASE_35M, 0x3fff);
		ssv6256_reg_set_bits(sd, ADR_RF_D_CAL_TOP_6,
				 0x213 << __ffs(RG_RX_RCCAL_40M_TARG),
				 RG_RX_RCCAL_40M_TARG | RG_RCCAL_POLAR_INV);
	} else {
		ssv6256_field_write(sd, ADR_CALIBRATION_TIMER_REGISTER,
				RG_RX_RCCAL_DELAY, 2);
		ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_2, RG_PHASE_17P5M, 0x20d0);
		ssv6256_reg_set_bits(sd, ADR_RF_D_CAL_TOP_6,
				 0x22c << __ffs(RG_RX_RCCAL_TARG),
				 RG_RX_RCCAL_TARG | RG_RCCAL_POLAR_INV);
	}
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_0, RG_ALPHA_SEL, 2);
	ssv6256_field_write(sd, ADR_CALIBRATION_GAIN_REGISTER0, RG_PGAG_RCCAL, 3);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_4, RG_TONE_SCALE, 0x80);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
			ht40 ? CAL_IDX_BW40_RXRC : CAL_IDX_BW20_RXRC);
	usleep_range(250, 500);

	ret = ssv6256_cal_wait(sd, RO_RCCAL_DONE, ht40 ? "HT40 RX RC" : "RX RC");
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	return ret;
}

/* Shared setup of the transmit calibrations: gain, tone and timing. */
static void ssv6256_cal_tx_setup(struct ssv6256_dev *sd)
{
	ssv6256_cal_channel(sd);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_TXGAIN_PHYCTRL, 1);
	ssv6256_reg_set_bits(sd, ADR_CALIBRATION_GAIN_REGISTER0,
			 (6 << __ffs(RG_TX_GAIN_TXCAL)) |
			 (3 << __ffs(RG_PGAG_TXCAL)),
			 RG_TX_GAIN_TXCAL | RG_PGAG_TXCAL);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_4, RG_TONE_SCALE, 0x80);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_9, RG_PRE_DC_AUTO, 1);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_3, RG_TX_IQCAL_TIME, 1);
	ssv6256_reg_set_bits(sd, ADR_RF_D_CAL_TOP_3,
			 (0xccc << __ffs(RG_PHASE_1M)) |
			 (0xccc << __ffs(RG_PHASE_RXIQ_1M)),
			 RG_PHASE_1M | RG_PHASE_RXIQ_1M);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_0, RG_ALPHA_SEL, 2);
}

/* Transmit LO leakage, corrected through the DAC offsets. */
static int ssv6256_cal_txdc(struct ssv6256_dev *sd)
{
	int ret;

	ssv6256_cal_tx_setup(sd);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
			CAL_IDX_WIFI2P4G_TXLO);
	usleep_range(250, 500);

	ret = ssv6256_cal_wait(sd, RO_TXDC_DONE, "TX DC");
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	return ret;
}

/* Transmit IQ imbalance (alpha and theta of the compensation matrix). */
static int ssv6256_cal_txiq(struct ssv6256_dev *sd)
{
	int ret;

	ssv6256_cal_tx_setup(sd);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
			CAL_IDX_WIFI2P4G_TXIQ);
	usleep_range(250, 500);

	ret = ssv6256_cal_wait(sd, RO_TXIQ_DONE, "TX IQ");
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	return ret;
}

/* Receive IQ imbalance; the receive chain gets its own gain settings. */
static int ssv6256_cal_rxiq(struct ssv6256_dev *sd)
{
	int ret;

	ssv6256_cal_channel(sd);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_TXGAIN_PHYCTRL, 1);
	ssv6256_reg_set_bits(sd, ADR_CALIBRATION_GAIN_REGISTER0,
			 (3 << __ffs(RG_PGAG_RXIQCAL)) |
			 (6 << __ffs(RG_TX_GAIN_RXIQCAL)),
			 RG_RFG_RXIQCAL | RG_PGAG_RXIQCAL | RG_TX_GAIN_RXIQCAL);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_4, RG_TONE_SCALE, 0x80);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_9, RG_PRE_DC_AUTO, 1);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_3, RG_TX_IQCAL_TIME, 1);
	ssv6256_reg_set_bits(sd, ADR_RF_D_CAL_TOP_3,
			 (0xccc << __ffs(RG_PHASE_1M)) |
			 (0xccc << __ffs(RG_PHASE_RXIQ_1M)),
			 RG_PHASE_1M | RG_PHASE_RXIQ_1M);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_0, RG_ALPHA_SEL, 2);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
			CAL_IDX_WIFI2P4G_RXIQ);
	usleep_range(250, 500);

	ret = ssv6256_cal_wait(sd, RO_RXIQ_DONE, "RX IQ");
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_0, RG_PHASE_STEP_VALUE, 0xccc);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	return ret;
}

/* The four 5 GHz sub-bands are calibrated on these channels. */
static const u8 cal_ch_5g[] = { 36, 40, 100, 140 };

/* Receive DC offset of the 5 GHz chain. */
static int ssv6256_cal_5g_rxdc(struct ssv6256_dev *sd)
{
	int ret;

	ssv6256_reg_set_bits(sd, ADR_SX_5GB_CH_TABLE,
			 FIELD_PREP(RG_SX5GB_CHANNEL, 100) |
			 RG_SX5GB_RFCH_MAP_EN,
			 RG_SX5GB_CHANNEL | RG_SX5GB_RFCH_MAP_EN);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
			CAL_IDX_WIFI5G_RXDC);
	usleep_range(100, 200);

	ret = ssv6256_cal_wait(sd, RO_5G_DCCAL_DONE, "5 GHz RX DC");
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	return ret;
}

/* Shared setup of the 5 GHz transmit calibrations. */
static void ssv6256_cal_5g_tx_setup(struct ssv6256_dev *sd)
{
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_TXGAIN_PHYCTRL, 1);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_4, RG_TONE_SCALE, 0x80);
	ssv6256_field_write(sd, ADR_5G_CALIBRATION_TIMER_GAIN_REGISTER,
			RG_5G_PGAG_TXCAL, 3);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_9, RG_PRE_DC_AUTO, 1);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_3, RG_TX_IQCAL_TIME, 1);
	ssv6256_reg_set_bits(sd, ADR_RF_D_CAL_TOP_3,
			 (0xccc << __ffs(RG_PHASE_1M)) |
			 (0xccc << __ffs(RG_PHASE_RXIQ_1M)),
			 RG_PHASE_1M | RG_PHASE_RXIQ_1M);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_0, RG_ALPHA_SEL, 2);
}

/* Transmit LO leakage of the 5 GHz chain. */
static int ssv6256_cal_5g_txdc(struct ssv6256_dev *sd)
{
	int ret;

	ssv6256_reg_set_bits(sd, ADR_SX_5GB_CH_TABLE,
			 FIELD_PREP(RG_SX5GB_CHANNEL, 100) |
			 RG_SX5GB_RFCH_MAP_EN,
			 RG_SX5GB_CHANNEL | RG_SX5GB_RFCH_MAP_EN);
	ssv6256_cal_5g_tx_setup(sd);
	ssv6256_field_write(sd, ADR_5G_CALIBRATION_TIMER_GAIN_REGISTER,
			RG_5G_TX_GAIN_TXCAL, 2);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
			CAL_IDX_WIFI5G_TXLO);
	usleep_range(250, 500);

	ret = ssv6256_cal_wait(sd, RO_5G_TXDC_DONE, "5 GHz TX DC");
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX, CAL_IDX_NONE);
	return ret;
}

/* Transmit IQ imbalance, once per 5 GHz sub-band. */
static int ssv6256_cal_5g_txiq(struct ssv6256_dev *sd)
{
	int ret = 0, i;

	ssv6256_field_write(sd, ADR_SX_5GB_CH_TABLE, RG_SX5GB_RFCH_MAP_EN, 1);
	ssv6256_cal_5g_tx_setup(sd);

	for (i = 0; i < ARRAY_SIZE(cal_ch_5g); i++) {
		ssv6256_field_write(sd, ADR_SX_5GB_CH_TABLE, RG_SX5GB_CHANNEL,
				cal_ch_5g[i]);
		ssv6256_field_write(sd, ADR_5G_CALIBRATION_TIMER_GAIN_REGISTER,
				RG_5G_TX_GAIN_TXCAL, 0);
		ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
				CAL_IDX_WIFI5G_TXIQ);
		usleep_range(250, 500);
		ret = ret ?: ssv6256_cal_wait(sd, RO_5G_TXIQ_DONE, "5 GHz TX IQ");
		ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
				CAL_IDX_NONE);
	}
	return ret;
}

/* Receive IQ imbalance; the hardware walks the sub-bands itself. */
static int ssv6256_cal_5g_rxiq(struct ssv6256_dev *sd)
{
	int ret = 0, i;

	ssv6256_field_write(sd, ADR_SX_5GB_CH_TABLE, RG_SX5GB_RFCH_MAP_EN, 1);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_TXGAIN_PHYCTRL, 1);
	ssv6256_reg_set_bits(sd, ADR_5G_CALIBRATION_GAIN_REGISTER1,
			 3 << __ffs(RG_5G_PGAG_RXIQCAL),
			 RG_5G_RFG_RXIQCAL | RG_5G_PGAG_RXIQCAL);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_4, RG_TONE_SCALE, 0x80);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_9, RG_PRE_DC_AUTO, 1);
	ssv6256_field_write(sd, ADR_DIGITAL_ADD_ON_3, RG_TX_IQCAL_TIME, 1);
	ssv6256_reg_set_bits(sd, ADR_RF_D_CAL_TOP_3,
			 (0xccc << __ffs(RG_PHASE_1M)) |
			 (0xccc << __ffs(RG_PHASE_RXIQ_1M)),
			 RG_PHASE_1M | RG_PHASE_RXIQ_1M);
	ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_0, RG_ALPHA_SEL, 2);

	for (i = 0; i < ARRAY_SIZE(cal_ch_5g); i++) {
		ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
				CAL_IDX_WIFI5G_RXIQ);
		usleep_range(250, 500);
		ret = ret ?: ssv6256_cal_wait(sd, RO_5G_RXIQ_DONE, "5 GHz RX IQ");
		ssv6256_field_write(sd, ADR_RF_D_CAL_TOP_0, RG_PHASE_STEP_VALUE,
				0xccc);
		ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_CAL_INDEX,
				CAL_IDX_NONE);
	}
	return ret;
}

static int ssv6256_calibrate(struct ssv6256_dev *sd)
{
	u32 alpha, theta;
	int ret;

	ssv6256_field_write(sd, ADR_WIFI_PADPD_2G_CONTROL_REG, RG_DPD_AM_EN, 0);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_TXGAIN_PHYCTRL, 1);
	if (sd->dual_band)
		ssv6256_reg_write(sd, ADR_WIFI_PADPD_5G_BB_GAIN_REG, 0x80808080);

	ssv6256_cal_start(sd);
	ret = ssv6256_cal_rxdc(sd);
	ssv6256_cal_next(sd);
	ret = ret ?: ssv6256_cal_rxrc(sd, false);
	ssv6256_cal_next(sd);
	ret = ret ?: ssv6256_cal_rxrc(sd, true);
	ssv6256_cal_next(sd);
	ret = ret ?: ssv6256_cal_txdc(sd);
	ssv6256_cal_next(sd);
	ret = ret ?: ssv6256_cal_txiq(sd);
	ssv6256_cal_next(sd);
	ret = ret ?: ssv6256_cal_rxiq(sd);

	if (sd->dual_band) {
		ssv6256_cal_next(sd);
		ret = ret ?: ssv6256_cal_5g_rxdc(sd);
		ssv6256_cal_next(sd);
		ret = ret ?: ssv6256_cal_5g_txdc(sd);
		ssv6256_cal_next(sd);
		ret = ret ?: ssv6256_cal_5g_txiq(sd);
		ssv6256_cal_next(sd);
		ret = ret ?: ssv6256_cal_5g_rxiq(sd);
	}
	ssv6256_cal_end(sd);
	if (ret)
		return ret;

	ssv6256_field_read(sd, ADR_TRX_IQ_COMP_2G, RG_TX_IQ_2500_ALPHA, &alpha);
	ssv6256_field_read(sd, ADR_TRX_IQ_COMP_2G, RG_TX_IQ_2500_THETA, &theta);
	dev_dbg(sd->dev, "calibrated, TX IQ alpha %u theta %u\n", alpha, theta);
	return 0;
}

/*
 * The PLL locks once the RF table has been handed over; the PMU then
 * reports its ready state and the digital clock can be switched over.
 */
static int ssv6256_init_pll(struct ssv6256_dev *sd)
{
	u32 val = 0;
	int i;

	ssv6256_field_write(sd, ADR_PMU_REG_2, RG_LOAD_RFTABLE_RDY, 1);
	for (i = 0; i < 100; i++) {
		usleep_range(1000, 2000);
		if (ssv6256_reg_read(sd, ADR_PMU_STATE_REG, &val))
			return -EIO;
		if (val == PMU_STATE_READY)
			break;
	}
	if (val != PMU_STATE_READY) {
		dev_err(sd->dev, "PLL did not lock (PMU state 0x%x)\n", val);
		return -ETIMEDOUT;
	}

	usleep_range(1000, 2000);
	ssv6256_reg_write(sd, ADR_WIFI_PHY_COMMON_SYS_REG, 0x80010000);
	ssv6256_reg_write(sd, ADR_CLOCK_SELECTION, CLK_DIGI_80M);
	usleep_range(1000, 2000);
	return 0;
}

/*
 * The single band parts share the die with the dual band ones but need
 * a different synthesiser setup to keep spurs away from the band.
 */
static void ssv6256_single_band_patch(struct ssv6256_dev *sd)
{
	u32 id;

	sd->dual_band = !ssv6256_reg_read(sd, ADR_CHIP_ID_2, &id) &&
			id == DUAL_BAND_ID;
	if (sd->dual_band)
		return;

	ssv6256_field_write(sd, ADR_SX_2_4GB_LPF, RG_SX_LPF_C2_WF, 0xe);
	ssv6256_field_write(sd, ADR_PMU_REG_1, RG_XO_LDO_LEVEL, 0x6);
	ssv6256_field_write(sd, ADR_2_4G_LDO_REGISTER, RG_SX_LDO_LO_LEVEL, 0x3);
	ssv6256_field_write(sd, ADR_SX_2_4GB_VCOBF, RG_SX_VCO_RXOB_AW, 1);
	ssv6256_field_write(sd, ADR_SX_2_4GB_VCOBF, RG_SX_VCO_TXOB_AW, 1);
	ssv6256_field_write(sd, ADR_SX_2_4GB_PFD_CHP, RG_SX_CP_ISEL_WF,
			xtal_cp_isel[SSV_XTAL]);
}

/*
 * Turn the baseband blocks on: receive and transmit chains, their FIFOs
 * and the 11b and 11g/n demodulators.  The master enable is separate.
 */
static int ssv6256_phy_mode(struct ssv6256_dev *sd, bool enable)
{
	u32 val = RG_PHYRX_MD_EN | RG_PHYTX_MD_EN | RG_PHY11GN_MD_EN |
		  RG_PHY11B_MD_EN | RG_PHYRXFIFO_MD_EN | RG_PHYTXFIFO_MD_EN |
		  RG_PHY11BGN_MD_EN;

	return ssv6256_reg_write(sd, ADR_WIFI_PHY_COMMON_ENABLE_REG,
			     enable ? val : 0);
}

int ssv6256_phy_enable(struct ssv6256_dev *sd, bool enable)
{
	return ssv6256_field_write(sd, ADR_WIFI_PHY_COMMON_ENABLE_REG,
			       RG_PHY_MD_EN, enable);
}

/*
 * Retune the synthesiser to @channel, with the secondary channel where
 * @bw says.  The receivers are held in reset while the radio moves.
 */
int ssv6256_set_channel(struct ssv6256_dev *sd, int channel, enum ssv6256_bandwidth bw)
{
	bool is_5g = channel >= 36;
	u32 table = is_5g ? ADR_SX_5GB_CH_TABLE : ADR_SX_CH_TABLE;
	u32 ch_mask = is_5g ? RG_SX5GB_CHANNEL : RG_SX_CHANNEL;
	u32 map_en = is_5g ? RG_SX5GB_RFCH_MAP_EN : RG_SX_RFCH_MAP_EN;
	u32 other = is_5g ? 36 : 1;
	u32 cur;

	ssv6256_field_write(sd, ADR_WIFI_11B_RX_REG_255, RG_SOFT_RST_N_11B_RX, 0);
	ssv6256_field_write(sd, ADR_WIFI_11GN_RX_REG_255, RG_SOFT_RST_N_11GN_RX, 0);
	ssv6256_set_bandwidth(sd, bw);

	/* short interframe space and signal extension differ per band */
	ssv6256_field_write(sd, ADR_MTX_TIME_IFS, MTX_SIFS, is_5g ? 16 : 10);
	ssv6256_field_write(sd, ADR_MTX_TIME_FINETUNE, MTX_SIGEXT, is_5g ? 0 : 6);

	ssv6256_field_write(sd, ADR_WIFI_PHY_COMMON_SYS_REG, RG_RF_5G_BAND, is_5g);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE_MANUAL, 1);
	ssv6256_field_write(sd, table, map_en, 1);

	/* a write that does not change the channel does not retune */
	if (!ssv6256_field_read(sd, table, ch_mask, &cur) && cur == channel)
		ssv6256_field_write(sd, table, ch_mask,
				channel != other ? other : other + 4);
	usleep_range(100, 200);
	ssv6256_field_write(sd, table, ch_mask, channel);

	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE, MODE_STANDBY);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE,
			is_5g ? MODE_WIFI5G_RX : MODE_WIFI2P4G_RX);
	ssv6256_field_write(sd, ADR_MODE_REGISTER, RG_MODE_MANUAL, 0);
	ssv6256_field_write(sd, ADR_WIFI_11GN_RX_REG_255, RG_SOFT_RST_N_11GN_RX, 1);
	return ssv6256_field_write(sd, ADR_WIFI_11B_RX_REG_255,
			       RG_SOFT_RST_N_11B_RX, 1);
}

/* Where the secondary channel sits, if there is one. */
int ssv6256_set_bandwidth(struct ssv6256_dev *sd, enum ssv6256_bandwidth bw)
{
	bool ht40 = bw != SSV_BW_20;
	bool sec_above = bw == SSV_BW_40_ABOVE;
	u32 sys = 0, add_on = 0;

	if (ht40) {
		sys = RG_SYSTEM_BW | (sec_above ? 0 : RG_PRIMARY_CH_SIDE);
		add_on = RG_40M_MODE | (sec_above ? RG_LO_UP_CH : 0);
	}
	ssv6256_field_write(sd, ADR_MTX_MISC_EN,
			MTX_BLOCKTX_IGNORE_CCA_ED_SECONDARY, !ht40);
	ssv6256_reg_set_bits(sd, ADR_WIFI_PHY_COMMON_SYS_REG, sys,
			 RG_SYSTEM_BW | RG_PRIMARY_CH_SIDE);
	return ssv6256_reg_set_bits(sd, ADR_DIGITAL_ADD_ON_0, add_on,
				RG_40M_MODE | RG_LO_UP_CH);
}

/*
 * Bring the radio up: RF table, PLL, baseband table and the
 * calibrations.  Runs before the MAC is initialised, because the MAC
 * picks its clock from what the PLL ends up providing.
 */
int ssv6256_phy_init(struct ssv6256_dev *sd)
{
	int ret;

	ret = ssv6256_write_table(sd, rf_table, ARRAY_SIZE(rf_table));
	if (ret)
		return ret;

	ssv6256_field_write(sd, ADR_PMU_DPLL_REG_0, RG_DP_XTAL_FREQ, SSV_XTAL);
	ssv6256_field_write(sd, ADR_SX_CH_TABLE, RG_SX_XTAL_FREQ, SSV_XTAL);
	ssv6256_field_write(sd, ADR_PMU_DPLL_REG_0, RG_EN_IOTADC_160M, 0);

	ret = ssv6256_init_pll(sd);
	if (ret)
		return ret;

	ssv6256_reg_write(sd, ADR_WIFI_PHY_COMMON_ENABLE_REG, 0);
	ret = ssv6256_write_table(sd, phy_table, ARRAY_SIZE(phy_table));
	if (ret)
		return ret;

	ssv6256_single_band_patch(sd);
	ssv6256_field_write(sd, ADR_CLOCK_SELECTION, CLK_DIGI_SEL, CLK_DIGI_80M);
	udelay(1);

	ret = ssv6256_calibrate(sd);
	return ret ?: ssv6256_phy_mode(sd, true);
}
